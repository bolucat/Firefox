/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CSSTransformComponent.h"

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/CSSTransformComponentBinding.h"
#include "nsCycleCollectionParticipant.h"

namespace mozilla::dom {

CSSTransformComponent::CSSTransformComponent(nsCOMPtr<nsISupports> aParent)
    : mParent(std::move(aParent)) {
  MOZ_ASSERT(mParent);
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(CSSTransformComponent)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CSSTransformComponent)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CSSTransformComponent)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CSSTransformComponent, mParent)

nsISupports* CSSTransformComponent::GetParentObject() const { return mParent; }

JSObject* CSSTransformComponent::WrapObject(JSContext* aCx,
                                            JS::Handle<JSObject*> aGivenProto) {
  return CSSTransformComponent_Binding::Wrap(aCx, this, aGivenProto);
}

// start of CSSTransformComponent Web IDL implementation
bool CSSTransformComponent::Is2D() const { return false; }

void CSSTransformComponent::SetIs2D(bool aArg) {}

already_AddRefed<DOMMatrix> CSSTransformComponent::ToMatrix(ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_NOT_INITIALIZED);
  return nullptr;
}

void CSSTransformComponent::Stringify(nsString& aRetVal) {}

// end of CSSTransformComponent Web IDL implementation

}  // namespace mozilla::dom
