/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebIdentityHandler.h"

#include "mozilla/dom/IdentityCredential.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "nsCycleCollectionParticipant.h"

namespace mozilla::dom {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebIdentityHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(WebIdentityHandler, mWindow, mGetPromise)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebIdentityHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebIdentityHandler)

WebIdentityHandler::~WebIdentityHandler() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActor) {
    if (mGetPromise) {
      mGetPromise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
      mGetPromise = nullptr;
      Unfollow();
    }
    mActor->SetHandler(nullptr);
    mWindow = nullptr;
  }
}

bool WebIdentityHandler::MaybeCreateActor() {
  if (mActor) {
    return true;
  }

  RefPtr<WebIdentityChild> actor = new WebIdentityChild();
  WindowGlobalChild* windowGlobalChild = mWindow->GetWindowGlobalChild();

  if (!windowGlobalChild ||
      !windowGlobalChild->SendPWebIdentityConstructor(actor)) {
    return false;
  }

  mActor = actor;
  mActor->SetHandler(this);
  return true;
}

void WebIdentityHandler::GetCredential(const CredentialRequestOptions& aOptions,
                                       bool aSameOriginWithAncestors,
                                       const RefPtr<Promise>& aPromise) {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(mWindow);
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(aOptions.mIdentity.WasPassed());
  // Prevent origin confusion by requiring no cross domain iframes
  // in this one's ancestry
  if (!aSameOriginWithAncestors) {
    aPromise->MaybeRejectWithNotAllowedError("Same origin ancestors only.");
    return;
  }

  if (!mActor) {
    aPromise->MaybeRejectWithUnknownError("Unknown failure");
    return;
  }

  if (mGetPromise) {
    aPromise->MaybeRejectWithNotAllowedError(
        "Concurrent requests not allowed.");
    return;
  }
  mGetPromise = aPromise;

  RefPtr<WebIdentityHandler> self(this);
  mActor
      ->SendGetIdentityCredential(
          aOptions.mIdentity.Value(), aOptions.mMediation,
          mWindow->GetWindowContext() &&
              mWindow->GetWindowContext()
                  ->HasValidTransientUserGestureActivation())
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self](const WebIdentityChild::GetIdentityCredentialPromise::
                     ResolveValueType& aResult) {
            if (self->mGetPromise) {
              if (aResult.type() ==
                  WebIdentityGetCredentialResponse::TIPCIdentityCredential) {
                const IPCIdentityCredential& result =
                    aResult.get_IPCIdentityCredential();
                self->mGetPromise->MaybeResolve(
                    new IdentityCredential(self->mWindow, result));
                self->mGetPromise = nullptr;
                self->Unfollow();
              } else {
                self->mGetPromise->MaybeRejectWithNetworkError(
                    "Failure to gather the credential");
                self->mGetPromise = nullptr;
                self->Unfollow();
              }
            }
          },
          [self](const WebIdentityChild::GetIdentityCredentialPromise::
                     RejectValueType& aResult) {
            if (self->mGetPromise) {
              self->mGetPromise->MaybeRejectWithOperationError("");
              self->mGetPromise = nullptr;
              self->Unfollow();
            }
          });
}

void WebIdentityHandler::PreventSilentAccess(const RefPtr<Promise>& aPromise) {
  if (!mActor) {
    aPromise->MaybeRejectWithUnknownError("Unknown failure");
    return;
  }
  mActor->SendPreventSilentAccess()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [aPromise](const WebIdentityChild::PreventSilentAccessPromise::
                     ResolveOrRejectValue& unused) {
        aPromise->MaybeResolveWithUndefined();
      });
}

void WebIdentityHandler::Disconnect(
    const IdentityCredentialDisconnectOptions& aOptions,
    const RefPtr<Promise>& aPromise) {
  if (!mActor) {
    aPromise->MaybeRejectWithUnknownError("Unknown failure");
    return;
  }
  mActor->SendDisconnectIdentityCredential(aOptions)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [aPromise](nsresult aResult) {
        if (aResult == NS_ERROR_DOM_MALFORMED_URI) {
          aPromise->MaybeRejectWithInvalidStateError(
              "Error parsing the provided URI");
        } else if (NS_FAILED(aResult)) {
          aPromise->MaybeRejectWithNetworkError(
              "Error sending disconnect request");
        } else {
          aPromise->MaybeResolveWithUndefined();
        }
      },
      [aPromise](mozilla::ipc::ResponseRejectReason aError) {
        aPromise->MaybeRejectWithUnknownError("Unknown failure");
      });
}

void WebIdentityHandler::SetLoginStatus(const LoginStatus& aStatus,
                                        const RefPtr<Promise>& aPromise) {
  const RefPtr<Promise>& promise = aPromise;
  if (!mActor) {
    promise->MaybeRejectWithUnknownError(
        "navigator.login.setStatus had an unexpected internal error");
    return;
  }
  mActor->SendSetLoginStatus(aStatus)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](const WebIdentityChild::SetLoginStatusPromise::ResolveValueType&
                    aResult) {
        if (NS_SUCCEEDED(aResult)) {
          promise->MaybeResolveWithUndefined();
        } else {
          promise->MaybeRejectWithUnknownError(
              "navigator.login.setStatus had an unexpected internal error");
        }
      },
      [promise](const WebIdentityChild::SetLoginStatusPromise::RejectValueType&
                    aResult) {
        promise->MaybeRejectWithUnknownError(
            "navigator.login.setStatus had an unexpected internal error");
      });
}

RefPtr<MozPromise<nsresult, nsresult, true>>
WebIdentityHandler::ResolveContinuationWindow(
    const nsACString& aToken, const IdentityResolveOptions& aOptions) {
  if (!mActor) {
    return MozPromise<nsresult, nsresult, true>::CreateAndReject(
        NS_ERROR_UNEXPECTED, __func__);
  }
  // Tell the parent process that we want to resolve with a given token and
  // options. The main process will infer what popup we are, and find the
  // pending promise.
  RefPtr<MozPromise<nsresult, nsresult, true>::Private> promise =
      new MozPromise<nsresult, nsresult, true>::Private(__func__);
  mActor->SendResolveContinuationWindow(aToken, aOptions)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise](const WebIdentityChild::ResolveContinuationWindowPromise::
                        ResolveValueType& aResult) {
            // Only resolve on success
            if (NS_SUCCEEDED(aResult)) {
              promise->Resolve(aResult, __func__);
            } else {
              promise->Reject(aResult, __func__);
            }
          },
          [promise](const WebIdentityChild::ResolveContinuationWindowPromise::
                        RejectValueType& aResult) {
            // Fall back to a not allowed error when IPC fails.
            promise->Reject(nsresult::NS_ERROR_DOM_NOT_ALLOWED_ERR, __func__);
          });
  return promise.forget();
}

RefPtr<MozPromise<bool, nsresult, true>>
WebIdentityHandler::IsContinuationWindow() {
  if (!mActor) {
    return MozPromise<bool, nsresult, true>::CreateAndReject(
        NS_ERROR_UNEXPECTED, __func__);
  }
  RefPtr<MozPromise<bool, nsresult, true>::Private> promise =
      new MozPromise<bool, nsresult, true>::Private(__func__);
  mActor->SendIsActiveContinuationWindow()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](bool result) { promise->Resolve(result, __func__); },
      [promise](mozilla::ipc::ResponseRejectReason reject) {
        promise->Resolve(false, __func__);
      });
  return promise.forget();
}

void WebIdentityHandler::ActorDestroyed() {
  MOZ_ASSERT(NS_IsMainThread());
  mActor = nullptr;
}

void WebIdentityHandler::RunAbortAlgorithm() {
  if (!mGetPromise) {
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);

  AutoJSAPI jsapi;
  if (!jsapi.Init(global)) {
    mGetPromise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    mGetPromise = nullptr;
    Unfollow();
    return;
  }
  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> reason(cx);
  Signal()->GetReason(cx, &reason);
  mGetPromise->MaybeReject(reason);
  mGetPromise = nullptr;
  Unfollow();
}

}  // namespace mozilla::dom
