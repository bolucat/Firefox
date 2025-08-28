/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExecuteIfOnMainThreadEventTarget.h"
#include "mozilla/ClearOnShutdown.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(ExecuteIfOnMainThreadEventTarget, nsIEventTarget,
                  nsISerialEventTarget)

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::Dispatch(
    already_AddRefed<nsIRunnable> aRunnable, DispatchFlags aFlags) {
  if (NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> runnable(aRunnable);
    return runnable->Run();
  }
  return GetMainThreadSerialEventTarget()->Dispatch(std::move(aRunnable),
                                                    aFlags);
}

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::DispatchFromScript(nsIRunnable* aRunnable,
                                                     DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aRunnable), aFlags);
}

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::DelayedDispatch(already_AddRefed<nsIRunnable>,
                                                  uint32_t) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::RegisterShutdownTask(nsITargetShutdownTask*) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::UnregisterShutdownTask(
    nsITargetShutdownTask*) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
ExecuteIfOnMainThreadEventTarget::IsOnCurrentThread(bool* aIsOnCurrentThread) {
  *aIsOnCurrentThread = NS_IsMainThread();
  return NS_OK;
}

NS_IMETHODIMP_(bool)
ExecuteIfOnMainThreadEventTarget::IsOnCurrentThreadInfallible() {
  return NS_IsMainThread();
}

nsISerialEventTarget* ExecuteIfOnMainThreadEventTarget::Get() {
  static nsCOMPtr<nsISerialEventTarget> sTarget;
  if (!sTarget) {
    sTarget = new ExecuteIfOnMainThreadEventTarget();
    ClearOnShutdown(&sTarget);
  }

  return sTarget.get();
}

}  // namespace mozilla::net
