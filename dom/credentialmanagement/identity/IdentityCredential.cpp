/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/IdentityCredential.h"

#include "mozilla/dom/Document.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WebIdentityHandler.h"
#include "nsGlobalWindowInner.h"
#include "nsIGlobalObject.h"
#include "nsIIdentityCredentialStorageService.h"
#include "nsNetUtil.h"
#include "nsTArray.h"

namespace mozilla::dom {

IdentityCredential::~IdentityCredential() = default;

JSObject* IdentityCredential::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return IdentityCredential_Binding::Wrap(aCx, this, aGivenProto);
}

IdentityCredential::IdentityCredential(nsPIDOMWindowInner* aParent,
                                       const IPCIdentityCredential& aOther)
    : Credential(aParent) {
  CopyValuesFrom(aOther);
}

void IdentityCredential::CopyValuesFrom(const IPCIdentityCredential& aOther) {
  this->SetId(aOther.id());
  this->SetType(u"identity"_ns);
  if (aOther.token().isSome()) {
    this->mToken = aOther.token().value();
  }
  this->mIsAutoSelected = aOther.isAutoSelected();
  this->mConfigURL = aOther.configURL();
}

IPCIdentityCredential IdentityCredential::MakeIPCIdentityCredential() const {
  IPCIdentityCredential result;
  this->GetId(result.id());
  if (!this->mToken.IsEmpty()) {
    result.token() = Some(this->mToken);
  }
  result.isAutoSelected() = mIsAutoSelected;
  result.configURL() = mConfigURL;
  return result;
}

void IdentityCredential::GetToken(nsACString& aToken) const {
  aToken.Assign(mToken);
}
void IdentityCredential::SetToken(const nsACString& aToken) {
  mToken.Assign(aToken);
}
bool IdentityCredential::IsAutoSelected() const { return mIsAutoSelected; }
void IdentityCredential::GetConfigURL(nsACString& aConfigURL) const {
  aConfigURL.Assign(mConfigURL);
}
void IdentityCredential::SetConfigURL(const nsACString& aConfigURL) {
  mConfigURL.Assign(aConfigURL);
}

// static
already_AddRefed<Promise> IdentityCredential::Disconnect(
    const GlobalObject& aGlobal,
    const IdentityCredentialDisconnectOptions& aOptions, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.ThrowNotAllowedError("Must be called on an appropriate global object.");
    return nullptr;
  }
  nsPIDOMWindowInner* window = global->GetAsInnerWindow();
  if (!window) {
    aRv.ThrowNotAllowedError("Must be called on a window.");
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed() || !promise)) {
    return nullptr;
  }
  WebIdentityHandler* identityHandler = window->GetOrCreateWebIdentityHandler();
  if (!identityHandler) {
    promise->MaybeRejectWithOperationError("");
    return promise.forget();
  }
  identityHandler->Disconnect(aOptions, promise);
  return promise.forget();
}

}  // namespace mozilla::dom
