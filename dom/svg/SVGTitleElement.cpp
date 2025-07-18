/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGTitleElement.h"
#include "mozilla/dom/SVGTitleElementBinding.h"

#include "mozilla/dom/Document.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(Title)

namespace mozilla::dom {

JSObject* SVGTitleElement::WrapNode(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return SVGTitleElement_Binding::Wrap(aCx, this, aGivenProto);
}

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ISUPPORTS_INHERITED(SVGTitleElement, SVGTitleElementBase,
                            nsIMutationObserver)

//----------------------------------------------------------------------
// Implementation

SVGTitleElement::SVGTitleElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : SVGTitleElementBase(std::move(aNodeInfo)) {
  AddMutationObserver(this);
  SetEnabledCallbacks(kCharacterDataChanged | kContentAppended |
                      kContentInserted | kContentWillBeRemoved);
}

void SVGTitleElement::CharacterDataChanged(nsIContent* aContent,
                                           const CharacterDataChangeInfo&) {
  SendTitleChangeEvent(false);
}

void SVGTitleElement::ContentAppended(nsIContent* aFirstNewContent,
                                      const ContentAppendInfo&) {
  SendTitleChangeEvent(false);
}

void SVGTitleElement::ContentInserted(nsIContent* aChild,
                                      const ContentInsertInfo&) {
  SendTitleChangeEvent(false);
}

void SVGTitleElement::ContentWillBeRemoved(nsIContent* aChild,
                                           const ContentRemoveInfo&) {
  SendTitleChangeEvent(false);
}

nsresult SVGTitleElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  // Let this fall through.
  nsresult rv = SVGTitleElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  SendTitleChangeEvent(true);

  return NS_OK;
}

void SVGTitleElement::UnbindFromTree(UnbindContext& aContext) {
  SendTitleChangeEvent(false);

  // Let this fall through.
  SVGTitleElementBase::UnbindFromTree(aContext);
}

void SVGTitleElement::DoneAddingChildren(bool aHaveNotified) {
  if (!aHaveNotified) {
    SendTitleChangeEvent(false);
  }
}

void SVGTitleElement::SendTitleChangeEvent(bool aBound) {
  if (Document* doc = GetUncomposedDoc()) {
    doc->NotifyPossibleTitleChange(aBound);
  }
}

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGTitleElement)

}  // namespace mozilla::dom
