/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_media_mediaelementeventrunners_h
#define mozilla_media_mediaelementeventrunners_h

#include "mozilla/dom/PlayPromise.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIContent.h"
#include "nsINamed.h"
#include "nsIRunnable.h"
#include "nsString.h"
#include "nsISupportsImpl.h"
#include "nsTString.h"

namespace mozilla::dom {

class HTMLMediaElement;

// Under certain conditions there may be no-one holding references to
// a media element from script, DOM parent, etc, but the element may still
// fire meaningful events in the future so we can't destroy it yet:
// 1) If the element is delaying the load event (or would be, if it were
// in a document), then events up to loadeddata or error could be fired,
// so we need to stay alive.
// 2) If the element is not paused and playback has not ended, then
// we will (or might) play, sending timeupdate and ended events and possibly
// audio output, so we need to stay alive.
// 3) if the element is seeking then we will fire seeking events and possibly
// start playing afterward, so we need to stay alive.
// 4) If autoplay could start playback in this element (if we got enough data),
// then we need to stay alive.
// 5) if the element is currently loading, not suspended, and its source is
// not a MediaSource, then script might be waiting for progress events or a
// 'stalled' or 'suspend' event, so we need to stay alive.
// If we're already suspended then (all other conditions being met),
// it's OK to just disappear without firing any more events,
// since we have the freedom to remain suspended indefinitely. Note
// that we could use this 'suspended' loophole to garbage-collect a suspended
// element in case 4 even if it had 'autoplay' set, but we choose not to.
// If someone throws away all references to a loading 'autoplay' element
// sound should still eventually play.
// 6) If the source is a MediaSource, most loading events will not fire unless
// appendBuffer() is called on a SourceBuffer, in which case something is
// already referencing the SourceBuffer, which keeps the associated media
// element alive. Further, a MediaSource will never time out the resource
// fetch, and so should not keep the media element alive if it is
// unreferenced. A pending 'stalled' event keeps the media element alive.
//
// Media elements owned by inactive documents (i.e. documents not contained in
// any document viewer) should never hold a self-reference because none of the
// above conditions are allowed: the element will stop loading and playing
// and never resume loading or playing unless its owner document changes to
// an active document (which can only happen if there is an external reference
// to the element).
// Media elements with no owner doc should be able to hold a self-reference.
// Something native must have created the element and may expect it to
// stay alive to play.

// It's very important that any change in state which could change the value of
// needSelfReference in AddRemoveSelfReference be followed by a call to
// AddRemoveSelfReference before this element could die!
// It's especially important if needSelfReference would change to 'true',
// since if we neglect to add a self-reference, this element might be
// garbage collected while there are still event listeners that should
// receive events. If we neglect to remove the self-reference then the element
// just lives longer than it needs to.

// Runnable for media element tasks.
// These tasks have special behavior if the load algorithm is triggered before
// the task is popped from the task queue, which is usually to skip running
// the task.  See nsResolveOrRejectPendingPlayPromisesRunner for the exception.
class nsMediaEventRunner : public nsIRunnable, public nsINamed {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsMediaEventRunner, nsIRunnable)

  explicit nsMediaEventRunner(const char* aName, HTMLMediaElement* aElement,
                              const nsAString& aEventName = u"unknown"_ns);

  void Cancel() { mElement = nullptr; }
  NS_IMETHODIMP GetName(nsACString& aName) override {
    aName.AssignASCII(mName);
    return NS_OK;
  }
  const char* Name() const { return mName; }
  nsString EventName() const { return mEventName; }

 protected:
  virtual ~nsMediaEventRunner() = default;
  bool IsCancelled() const;
  MOZ_CAN_RUN_SCRIPT nsresult FireEvent(const nsAString& aName);

  virtual void ReportProfilerMarker();
  uint64_t GetElementDurationMs() const;

  RefPtr<HTMLMediaElement> mElement;
  const char* mName;
  nsString mEventName;
  uint32_t mLoadID;
};

/**
 * This runner is used to dispatch async event on media element.
 */
class nsAsyncEventRunner : public nsMediaEventRunner {
 public:
  nsAsyncEventRunner(const nsAString& aEventName, HTMLMediaElement* aElement)
      : nsMediaEventRunner("nsAsyncEventRunner", aElement, aEventName) {}
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override;
};

/**
 * These runners are used to handle `playing` event and address play promise.
 *
 * If no error is passed while constructing an instance, the instance will
 * resolve the passed promises with undefined; otherwise, the instance will
 * reject the passed promises with the passed error.
 *
 * The constructor appends the constructed instance into the passed media
 * element's mPendingPlayPromisesRunners member and once the the runner is run
 * (whether fulfilled or canceled), it removes itself from
 * mPendingPlayPromisesRunners.
 *
 * If the load algorithm is triggered before the task is run then the pending
 * play promises passed will be settled at commencement of the load algorithm.
 */
class nsResolveOrRejectPendingPlayPromisesRunner : public nsMediaEventRunner {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(
      nsResolveOrRejectPendingPlayPromisesRunner, nsMediaEventRunner)

  nsResolveOrRejectPendingPlayPromisesRunner(
      HTMLMediaElement* aElement, nsTArray<RefPtr<PlayPromise>>&& aPromises,
      nsresult aError = NS_OK);
  void ResolveOrReject();
  NS_IMETHOD Run() override;

 protected:
  virtual ~nsResolveOrRejectPendingPlayPromisesRunner() = default;

 private:
  nsTArray<RefPtr<PlayPromise>> mPromises;
  nsresult mError;
};

class nsNotifyAboutPlayingRunner
    : public nsResolveOrRejectPendingPlayPromisesRunner {
 public:
  nsNotifyAboutPlayingRunner(
      HTMLMediaElement* aElement,
      nsTArray<RefPtr<PlayPromise>>&& aPendingPlayPromises)
      : nsResolveOrRejectPendingPlayPromisesRunner(
            aElement, std::move(aPendingPlayPromises)) {}
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override;
};

/**
 * This runner is used to dispatch a source error event, which would happen when
 * loading resource failed.
 */
class nsSourceErrorEventRunner : public nsMediaEventRunner {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsSourceErrorEventRunner,
                                           nsMediaEventRunner)
  nsSourceErrorEventRunner(HTMLMediaElement* aElement, nsIContent* aSource,
                           const nsACString& aErrorDetails)
      : nsMediaEventRunner("nsSourceErrorEventRunner", aElement),
        mSource(aSource),
        mErrorDetails(NS_ConvertUTF8toUTF16(aErrorDetails)) {}
  NS_IMETHOD Run() override;

 private:
  virtual ~nsSourceErrorEventRunner() = default;
  nsCOMPtr<nsIContent> mSource;
  const nsString mErrorDetails;
};

/**
 * This runner is used to dispatch `timeupdate` event and ensure we don't
 * dispatch `timeupdate` more often than once per `TIMEUPDATE_MS` if that is not
 * a mandatory event.
 */
class nsTimeupdateRunner : public nsMediaEventRunner {
 public:
  nsTimeupdateRunner(HTMLMediaElement* aElement, bool aIsMandatory)
      : nsMediaEventRunner("nsTimeupdateRunner", aElement, u"timeupdate"_ns),
        mIsMandatory(aIsMandatory) {}
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override;

 private:
  void ReportProfilerMarker() override;
  bool ShouldDispatchTimeupdate() const;
  bool mIsMandatory;
};

}  // namespace mozilla::dom

#endif  // mozilla_media_mediaelementeventrunners_h
