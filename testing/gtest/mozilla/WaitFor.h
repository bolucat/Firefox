/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef TESTING_GTEST_MOZILLA_WAITFOR_H_
#define TESTING_GTEST_MOZILLA_WAITFOR_H_

#include "MediaEventSource.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/SpinEventLoopUntil.h"

namespace mozilla {

/**
 * Waits for an occurrence of aEvent on the current thread (by blocking it,
 * except tasks added to the event loop may run) and returns the event's
 * templated value, if it's non-void.
 *
 * The caller must be wary of eventloop issues, in
 * particular cases where we rely on a stable state runnable, but there is never
 * a task to trigger stable state. In such cases it is the responsibility of the
 * caller to create the needed tasks, as JS would. A noteworthy API that relies
 * on stable state is MediaTrackGraph::GetInstance.
 */
template <ListenerPolicy Lp, typename First, typename... Rest>
inline auto WaitFor(MediaEventSourceImpl<Lp, First, Rest...>& aEvent) {
  constexpr size_t num_params = 1 + sizeof...(Rest);
  using Storage =
      std::conditional_t<num_params == 1, First, std::tuple<First, Rest...>>;
  Maybe<Storage> value;
  if constexpr (Lp == ListenerPolicy::NonExclusive) {
    MediaEventListener listener =
        aEvent.Connect(AbstractThread::GetCurrent(),
                       [&value](const First& aFirst, const Rest&... aRest) {
                         if constexpr (num_params == 1) {
                           value = Some(aFirst);
                         } else {
                           value = Some<Storage>({aFirst, aRest...});
                         }
                       });
    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "WaitFor(MediaEventSource<T>& aEvent)"_ns,
        [&] { return value.isSome(); });
    listener.Disconnect();
    return value.value();
  } else {
    MediaEventListener listener = aEvent.Connect(
        AbstractThread::GetCurrent(),
        [&value](First&& aFirst, Rest&&... aRest) {
          if constexpr (num_params == 1) {
            value = Some<Storage>(std::forward<First>(aFirst));
          } else {
            value = Some<Storage>(
                {std::forward<First>(aFirst), std::forward<Rest...>(aRest...)});
          }
        });
    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "WaitFor(MediaEventSource<T>& aEvent)"_ns,
        [&] { return value.isSome(); });
    listener.Disconnect();
    return value.value();
  }
}

/**
 * Specialization of WaitFor<T> for void.
 */
template <ListenerPolicy Lp>
inline void WaitFor(MediaEventSourceImpl<Lp, void>& aEvent) {
  bool done = false;
  MediaEventListener listener =
      aEvent.Connect(AbstractThread::GetCurrent(), [&] { done = true; });
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitFor(MediaEventSource<void>& aEvent)"_ns, [&] { return done; });
  listener.Disconnect();
}

/**
 * Variant of WaitFor that spins the event loop until a MozPromise has either
 * been resolved or rejected.  Result accepts R and E only if their types
 * differ.  Consider also WaitForResolve() and WaitForReject(), which are
 * suitable even when resolve and reject types are the same.
 */
template <typename R, typename E, bool Exc>
inline Result<R, E> WaitFor(const RefPtr<MozPromise<R, E, Exc>>& aPromise) {
  Maybe<R> success;
  Maybe<E> error;
  aPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [&](R aResult) { success = Some(aResult); },
      [&](E aError) { error = Some(aError); });
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitFor(const RefPtr<MozPromise<R, E, Exc>>& aPromise)"_ns,
      [&] { return success.isSome() || error.isSome(); });
  if (success.isSome()) {
    return success.extract();
  }
  return Err(error.extract());
}

/**
 * Variation on WaitFor that spins the event loop until a MozPromise has been
 * resolved.
 */
template <typename R, typename E, bool Exc>
inline R WaitForResolve(const RefPtr<MozPromise<R, E, Exc>>& aPromise) {
  Maybe<R> success;
  // Use r-value reference for exclusive promises to support move-only types.
  using RRef = typename std::conditional_t<Exc, R&&, const R&>;
  using ERef = typename std::conditional_t<Exc, E&&, const E&>;
  aPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [&](RRef aResult) { success.emplace(std::forward<RRef>(aResult)); },
      [&](ERef aError) { MOZ_CRASH("rejection was not expected"); });
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitForResolve(const RefPtr<MozPromise<R, E, Exc>>& aPromise)"_ns,
      [&] { return success.isSome(); });
  return success.extract();
}

/**
 * Variation on WaitFor that spins the event loop until a MozPromise has been
 * rejected.
 */
template <typename R, typename E, bool Exc>
inline E WaitForReject(const RefPtr<MozPromise<R, E, Exc>>& aPromise) {
  Maybe<E> error;
  // Use r-value reference for exclusive promises to support move-only types.
  using RRef = typename std::conditional_t<Exc, R&&, const R&>;
  using ERef = typename std::conditional_t<Exc, E&&, const E&>;
  aPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [&](RRef aResult) { MOZ_CRASH("resolution was not expected"); },
      [&](ERef aError) { error.emplace(std::forward<ERef>(aError)); });
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitForReject(const RefPtr<MozPromise<R, E, Exc>>& aPromise)"_ns,
      [&] { return error.isSome(); });
  return error.extract();
}

/**
 * A variation of WaitFor that takes a callback to be called each time aEvent is
 * raised. Blocks the caller until the callback function returns true.
 */
template <ListenerPolicy Lp, typename... Args, typename CallbackFunction>
inline void WaitUntil(MediaEventSourceImpl<Lp, Args...>& aEvent,
                      CallbackFunction&& aF) {
  bool done = false;
  MediaEventListener listener =
      aEvent.Connect(AbstractThread::GetCurrent(), [&](Args... aValue) {
        if (!done) {
          done = aF(std::forward<Args>(aValue)...);
        }
      });
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "WaitUntil(MediaEventSource<Args...>& aEvent, CallbackFunction&& aF)"_ns,
      [&] { return done; });
  listener.Disconnect();
}

template <typename... Args>
using TakeNPromise = MozPromise<std::vector<std::tuple<Args...>>, bool, true>;

template <ListenerPolicy Lp, typename... Args>
inline auto TakeN(MediaEventSourceImpl<Lp, Args...>& aEvent, size_t aN)
    -> RefPtr<TakeNPromise<Args...>> {
  using Storage = std::vector<std::tuple<Args...>>;
  using Promise = TakeNPromise<Args...>;
  using Holder = media::Refcountable<MozPromiseHolder<Promise>>;
  using Values = media::Refcountable<Storage>;
  using Listener = media::Refcountable<MediaEventListener>;
  auto values = MakeRefPtr<Values>();
  values->reserve(aN);
  auto listener = MakeRefPtr<Listener>();
  auto holder = MakeRefPtr<Holder>();
  *listener = aEvent.Connect(AbstractThread::GetCurrent(),
                             [values, listener, aN, holder](Args... aValue) {
                               values->push_back({aValue...});
                               if (values->size() == aN) {
                                 listener->Disconnect();
                                 holder->Resolve(std::move(*values),
                                                 "TakeN listener callback");
                               }
                             });
  return holder->Ensure(__func__);
}

using TakeNVoidPromise = MozPromise<size_t, bool, true>;

template <ListenerPolicy Lp>
inline auto TakeN(MediaEventSourceImpl<Lp, void>& aEvent, size_t aN)
    -> RefPtr<TakeNVoidPromise> {
  using Storage = Maybe<size_t>;
  using Promise = TakeNVoidPromise;
  using Holder = media::Refcountable<MozPromiseHolder<Promise>>;
  using Values = media::Refcountable<Storage>;
  using Listener = media::Refcountable<MediaEventListener>;
  auto values = MakeRefPtr<Values>();
  *values = Some(0);
  auto listener = MakeRefPtr<Listener>();
  auto holder = MakeRefPtr<Holder>();
  *listener = aEvent.Connect(
      AbstractThread::GetCurrent(), [values, listener, aN, holder]() {
        if (++(values->ref()) == aN) {
          listener->Disconnect();
          holder->Resolve(**values, "TakeN (void) listener callback");
        }
      });
  return holder->Ensure(__func__);
}

/**
 * Helper that, given that canonicals have just been updated on the current
 * thread, will block its execution until mirrors and their watchers have
 * executed on aTarget.
 */
inline void WaitForMirrors(const RefPtr<nsISerialEventTarget>& aTarget) {
  Unused << WaitFor(InvokeAsync(aTarget, __func__, [] {
    return GenericPromise::CreateAndResolve(true, "WaitForMirrors resolver");
  }));
}

/**
 * Short form of WaitForMirrors that assumes mirrors are on the current thread
 * (like canonicals).
 */
inline void WaitForMirrors() { WaitForMirrors(GetCurrentSerialEventTarget()); }

}  // namespace mozilla

#endif  // TESTING_GTEST_MOZILLA_WAITFOR_H_
