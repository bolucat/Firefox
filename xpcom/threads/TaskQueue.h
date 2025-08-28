/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TaskQueue_h_
#define TaskQueue_h_

#include "mozilla/AbstractThread.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Queue.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TaskDispatcher.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "nsIDirectTaskDispatcher.h"
#include "nsThreadUtils.h"

#define MOZILLA_TASKQUEUE_IID \
  {0xb5181e3a, 0x39cf, 0x4d32, {0x81, 0x4a, 0xea, 0x86, 0x94, 0x16, 0x95, 0xd1}}

namespace mozilla {

typedef MozPromise<bool, bool, false> ShutdownPromise;

class TaskQueueTrackerEntry;

// Abstracts executing runnables in order on an arbitrary event target. The
// runnables dispatched to the TaskQueue will be executed in the order in which
// they're received, and are guaranteed to not be executed concurrently.
// They may be executed on different threads, and a memory barrier is used
// to make this threadsafe for objects that aren't already threadsafe.
//
// Note, since a TaskQueue can also be converted to an nsIEventTarget using
// WrapAsEventTarget() its possible to construct a hierarchy of TaskQueues.
// Consider these three TaskQueues:
//
//  TQ1 dispatches to the main thread
//  TQ2 dispatches to TQ1
//  TQ3 dispatches to TQ1
//
// This ensures there is only ever a single runnable from the entire chain on
// the main thread.  It also ensures that TQ2 and TQ3 only have a single
// runnable in TQ1 at any time.
//
// This arrangement lets you prioritize work by dispatching runnables directly
// to TQ1.  You can issue many runnables for important work.  Meanwhile the TQ2
// and TQ3 work will always execute at most one runnable and then yield.
//
// A TaskQueue does not require explicit shutdown, however it provides a
// BeginShutdown() method that places TaskQueue in a shut down state and returns
// a promise that gets resolved once all pending tasks have completed
class TaskQueue final : public AbstractThread,
                        public nsIDirectTaskDispatcher,
                        public SupportsThreadSafeWeakPtr<TaskQueue> {
  class EventTargetWrapper;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDIRECTTASKDISPATCHER
  MOZ_DECLARE_REFCOUNTED_TYPENAME(TaskQueue)
  NS_INLINE_DECL_STATIC_IID(MOZILLA_TASKQUEUE_IID)

  static RefPtr<TaskQueue> Create(already_AddRefed<nsIEventTarget> aTarget,
                                  const char* aName,
                                  bool aSupportsTailDispatch = false);

  TaskDispatcher& TailDispatcher() override;

  NS_IMETHOD DispatchFromScript(nsIRunnable* aEvent,
                                DispatchFlags aFlags) override {
    return Dispatch(do_AddRef(aEvent), aFlags);
  }

  NS_IMETHOD Dispatch(already_AddRefed<nsIRunnable> aEvent,
                      DispatchFlags aFlags) override {
    // NOTE: This dispatch implementation never leaks the runnable on failure,
    // even if `NS_DISPATCH_FALLIBLE` is not specified.
    nsCOMPtr<nsIRunnable> runnable = aEvent;
    {
      MonitorAutoLock mon(mQueueMonitor);
      return DispatchLocked(/* passed by ref */ runnable, aFlags,
                            NormalDispatch);
    }
    // If the ownership of |r| is not transferred in DispatchLocked() due to
    // dispatch failure, it will be deleted here outside the lock. We do so
    // since the destructor of the runnable might access TaskQueue and result
    // in deadlocks.
  }

  [[nodiscard]] nsresult Dispatch(
      already_AddRefed<nsIRunnable> aRunnable,
      DispatchReason aReason = NormalDispatch) override {
    nsCOMPtr<nsIRunnable> r = aRunnable;
    {
      MonitorAutoLock mon(mQueueMonitor);
      return DispatchLocked(/* passed by ref */ r, NS_DISPATCH_NORMAL, aReason);
    }
    // If the ownership of |r| is not transferred in DispatchLocked() due to
    // dispatch failure, it will be deleted here outside the lock. We do so
    // since the destructor of the runnable might access TaskQueue and result
    // in deadlocks.
  }

  // So we can access nsIEventTarget::Dispatch(nsIRunnable*, uint32_t aFlags)
  using nsIEventTarget::Dispatch;

  NS_IMETHOD RegisterShutdownTask(nsITargetShutdownTask* aTask) override;
  NS_IMETHOD UnregisterShutdownTask(nsITargetShutdownTask* aTask) override;

  using CancelPromise = MozPromise<bool, bool, false>;

  // Puts the queue in a shutdown state and returns immediately. The queue will
  // remain alive at least until all the events are drained, because the Runners
  // hold a strong reference to the task queue, and one of them is always held
  // by the target event queue when the task queue is non-empty.
  //
  // The returned promise is resolved when the queue goes empty.
  RefPtr<ShutdownPromise> BeginShutdown();

  // Blocks until all task finish executing.
  void AwaitIdle();

  // Blocks until the queue is flagged for shutdown and all tasks have finished
  // executing.
  void AwaitShutdownAndIdle();

  bool IsEmpty();

  // Returns true if the current thread is currently running a Runnable in
  // the task queue.
  bool IsCurrentThreadIn() const override;
  using nsISerialEventTarget::IsOnCurrentThread;

  class Observer {
   public:
    NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING
    // Called before an event is processed on the TaskQueue on its event target.
    virtual void WillProcessEvent(TaskQueue* aQueue) = 0;
    // Called after an event has been processed on the TaskQueue on its event
    // target.
    // Note that it is not safe to add direct tasks from DidProcessEvent().
    virtual void DidProcessEvent(TaskQueue* aQueue) = 0;

   protected:
    virtual ~Observer() = default;
  };

  // Set an observer to be notified as this TaskQueue processes events.
  // Callable from any thread. Transactional, i.e. WillProcess always comes
  // first and is always matched by DidProcess.
  void SetObserver(Observer* aObserver);

 private:
  friend class SupportsThreadSafeWeakPtr<TaskQueue>;

  TaskQueue(already_AddRefed<nsIEventTarget> aTarget, const char* aName,
            bool aSupportsTailDispatch);

  virtual ~TaskQueue();

  // Blocks until all task finish executing. Called internally by methods
  // that need to wait until the task queue is idle.
  // mQueueMonitor must be held.
  void AwaitIdleLocked();

  nsresult DispatchLocked(nsCOMPtr<nsIRunnable>& aRunnable,
                          DispatchFlags aFlags,
                          DispatchReason aReason = NormalDispatch);

  void MaybeResolveShutdown();

  nsCOMPtr<nsIEventTarget> mTarget MOZ_GUARDED_BY(mQueueMonitor);

  // Handle for this TaskQueue being registered with our target if it implements
  // TaskQueueTracker.
  UniquePtr<TaskQueueTrackerEntry> mTrackerEntry MOZ_GUARDED_BY(mQueueMonitor);

  // Monitor that protects the queue, mIsRunning, mIsShutdown and
  // mShutdownTasks;
  Monitor mQueueMonitor;

  typedef struct TaskStruct {
    nsCOMPtr<nsIRunnable> event;
    DispatchFlags flags;
  } TaskStruct;

  // Queue of tasks to run.
  Queue<TaskStruct> mTasks MOZ_GUARDED_BY(mQueueMonitor);

  // List of tasks to run during shutdown.
  nsTArray<nsCOMPtr<nsITargetShutdownTask>> mShutdownTasks
      MOZ_GUARDED_BY(mQueueMonitor);

  // The thread currently running the task queue. We store a reference
  // to this so that IsCurrentThreadIn() can tell if the current thread
  // is the thread currently running in the task queue.
  //
  // This may be read on any thread, but may only be written on mRunningThread.
  // The thread can't die while we're running in it, and we only use it for
  // pointer-comparison with the current thread anyway - so we make it atomic
  // and don't refcount it.
  Atomic<PRThread*> mRunningThread;

  // RAII class that gets instantiated for each dispatched task.
  class AutoTaskGuard {
   public:
    AutoTaskGuard(TaskQueue* aQueue, TaskQueue::Observer* aObserver)
        : mQueue(aQueue), mObserver(aObserver), mLastCurrentThread(nullptr) {
      // NB: We don't hold the lock to aQueue here. Don't do anything that
      // might require it.
      MOZ_ASSERT(!mQueue->mTailDispatcher);
      mTaskDispatcher.emplace(aQueue,
                              /* aIsTailDispatcher = */ true);
      mQueue->mTailDispatcher = mTaskDispatcher.ptr();

      mLastCurrentThread = sCurrentThreadTLS.get();
      sCurrentThreadTLS.set(aQueue);

      MOZ_ASSERT(mQueue->mRunningThread == nullptr);
      mQueue->mRunningThread = PR_GetCurrentThread();

      mEventTargetGuard.emplace(mQueue);

      if (mObserver) {
        mObserver->WillProcessEvent(mQueue);
      }
    }

    ~AutoTaskGuard() {
      mTaskDispatcher->DrainDirectTasks();

      if (mObserver) {
        mObserver->DidProcessEvent(mQueue);
        MOZ_ASSERT(!mTaskDispatcher->HaveDirectTasks(),
                   "TaskQueue::Observer instance in "
                   "DidProcessEvent(TaskQueue*) added direct tasks in error");
      }

      mTaskDispatcher.reset();
      mQueue->mTailDispatcher = nullptr;

      mEventTargetGuard = Nothing();

      MOZ_ASSERT(mQueue->mRunningThread == PR_GetCurrentThread());
      mQueue->mRunningThread = nullptr;

      sCurrentThreadTLS.set(mLastCurrentThread);
    }

   private:
    Maybe<AutoTaskDispatcher> mTaskDispatcher;
    Maybe<SerialEventTargetGuard> mEventTargetGuard;
    TaskQueue* mQueue;
    TaskQueue::Observer* mObserver;
    AbstractThread* mLastCurrentThread;
  };

  TaskDispatcher* mTailDispatcher;

  // True if we've dispatched an event to the target to execute events from
  // the queue.
  bool mIsRunning MOZ_GUARDED_BY(mQueueMonitor);

  // True if we've started our shutdown process.
  bool mIsShutdown MOZ_GUARDED_BY(mQueueMonitor);
  MozPromiseHolder<ShutdownPromise> mShutdownPromise
      MOZ_GUARDED_BY(mQueueMonitor);

  // The name of this TaskQueue. Useful when debugging dispatch failures.
  const char* const mName;

  SimpleTaskQueue mDirectTasks;

  RefPtr<Observer> mObserver MOZ_GUARDED_BY(mQueueMonitor);

  class Runner : public Runnable {
   public:
    explicit Runner(TaskQueue* aQueue)
        : Runnable("TaskQueue::Runner"), mQueue(aQueue) {}
    NS_IMETHOD Run() override;

   private:
    RefPtr<TaskQueue> mQueue;
  };
};

#define MOZILLA_TASKQUEUETRACKER_IID \
  {0x765c4b56, 0xd5f6, 0x4a9f, {0x91, 0xcf, 0x51, 0x47, 0xb3, 0xc1, 0x7e, 0xa6}}

// XPCOM "interface" which may be implemented by nsIEventTarget implementations
// which want to keep track of what TaskQueue instances are currently targeting
// them. This may be used to asynchronously shutdown TaskQueues targeting a
// threadpool or other event target before the threadpool goes away.
//
// This explicitly TaskQueue-aware tracker is used instead of
// `nsITargetShutdownTask` as the operations required to shut down a TaskQueue
// are asynchronous, which is not a requirement of that interface.
class TaskQueueTracker : public nsISupports {
 public:
  NS_INLINE_DECL_STATIC_IID(MOZILLA_TASKQUEUETRACKER_IID)

  // Get a strong reference to every TaskQueue currently tracked by this
  // TaskQueueTracker. May be called from any thraed.
  nsTArray<RefPtr<TaskQueue>> GetAllTrackedTaskQueues();

 protected:
  virtual ~TaskQueueTracker();

 private:
  friend class TaskQueueTrackerEntry;

  Mutex mMutex{"TaskQueueTracker"};
  LinkedList<TaskQueueTrackerEntry> mEntries MOZ_GUARDED_BY(mMutex);
};

}  // namespace mozilla

#endif  // TaskQueue_h_
