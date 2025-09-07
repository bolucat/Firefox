/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "mozilla/dom/HTMLStyleElement.h"

#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/HTMLStyleElementBinding.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "nsContentUtils.h"
#include "nsDOMTokenList.h"
#include "nsGkAtoms.h"
#include "nsStubMutationObserver.h"
#include "nsStyleConsts.h"
#include "nsThreadUtils.h"
#include "nsUnicharUtils.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Style)

namespace mozilla::dom {

HTMLStyleElement::HTMLStyleElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {
  AddMutationObserver(this);
}

HTMLStyleElement::~HTMLStyleElement() = default;

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLStyleElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLStyleElement,
                                                  nsGenericHTMLElement)
  tmp->LinkStyle::Traverse(cb);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBlocking)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLStyleElement,
                                                nsGenericHTMLElement)
  tmp->LinkStyle::Unlink();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBlocking)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(HTMLStyleElement,
                                             nsGenericHTMLElement,
                                             nsIMutationObserver)

NS_IMPL_ELEMENT_CLONE(HTMLStyleElement)

bool HTMLStyleElement::Disabled() const {
  StyleSheet* ss = GetSheet();
  return ss && ss->Disabled();
}

void HTMLStyleElement::SetDisabled(bool aDisabled) {
  if (StyleSheet* ss = GetSheet()) {
    ss->SetDisabled(aDisabled);
  }
}

void HTMLStyleElement::CharacterDataChanged(nsIContent* aContent,
                                            const CharacterDataChangeInfo&) {
  ContentChanged(aContent);
}

void HTMLStyleElement::ContentAppended(nsIContent* aFirstNewContent,
                                       const ContentAppendInfo&) {
  ContentChanged(aFirstNewContent->GetParent());
}

void HTMLStyleElement::ContentInserted(nsIContent* aChild,
                                       const ContentInsertInfo&) {
  ContentChanged(aChild);
}

void HTMLStyleElement::ContentWillBeRemoved(nsIContent* aChild,
                                            const ContentRemoveInfo& aInfo) {
  mTriggeringPrincipal = nullptr;
  if (!nsContentUtils::IsInSameAnonymousTree(this, aChild)) {
    return;
  }
  if (aInfo.mBatchRemovalState && !aInfo.mBatchRemovalState->mIsFirst) {
    return;
  }
  // Make sure to run this once the removal has taken place.
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "HTMLStyleElement::ContentWillBeRemoved",
      [self = RefPtr{this}] { self->UpdateStyleSheetInternal(); }));
}

void HTMLStyleElement::ContentChanged(nsIContent* aContent) {
  mTriggeringPrincipal = nullptr;
  if (nsContentUtils::IsInSameAnonymousTree(this, aContent)) {
    Unused << UpdateStyleSheetInternal(nullptr, nullptr);
  }
}

nsresult HTMLStyleElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);
  LinkStyle::BindToTree();
  return rv;
}

void HTMLStyleElement::UnbindFromTree(UnbindContext& aContext) {
  RefPtr<Document> oldDoc = GetUncomposedDoc();
  ShadowRoot* oldShadow = GetContainingShadow();

  nsGenericHTMLElement::UnbindFromTree(aContext);

  Unused << UpdateStyleSheetInternal(oldDoc, oldShadow);
}

bool HTMLStyleElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                      const nsAString& aValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::blocking &&
      StaticPrefs::dom_element_blocking_enabled()) {
    aResult.ParseAtomArray(aValue);
    return true;
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLStyleElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                    const nsAttrValue* aValue,
                                    const nsAttrValue* aOldValue,
                                    nsIPrincipal* aSubjectPrincipal,
                                    bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::title || aName == nsGkAtoms::media ||
        aName == nsGkAtoms::type) {
      Unused << UpdateStyleSheetInternal(nullptr, nullptr, ForceUpdate::Yes);
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void HTMLStyleElement::GetInnerHTML(nsAString& aInnerHTML,
                                    OOMReporter& aError) {
  if (!nsContentUtils::GetNodeTextContent(this, false, aInnerHTML, fallible)) {
    aError.ReportOOM();
  }
}

void HTMLStyleElement::SetInnerHTMLTrusted(const nsAString& aInnerHTML,
                                           nsIPrincipal* aSubjectPrincipal,
                                           ErrorResult& aError) {
  SetTextContentInternal(aInnerHTML, aSubjectPrincipal, aError);
}

void HTMLStyleElement::SetTextContentInternal(const nsAString& aTextContent,
                                              nsIPrincipal* aScriptedPrincipal,
                                              ErrorResult& aError,
                                              MutationEffectOnScript) {
  // Per spec, if we're setting text content to an empty string and don't
  // already have any children, we should not trigger any mutation observers, or
  // re-parse the stylesheet.
  if (aTextContent.IsEmpty() && !GetFirstChild()) {
    nsIPrincipal* principal =
        mTriggeringPrincipal ? mTriggeringPrincipal.get() : NodePrincipal();
    if (principal == aScriptedPrincipal) {
      return;
    }
  }

  const bool updatesWereEnabled = mUpdatesEnabled;
  DisableUpdates();

  aError = nsContentUtils::SetNodeTextContent(this, aTextContent, true);
  if (updatesWereEnabled) {
    mTriggeringPrincipal = aScriptedPrincipal;
    Unused << EnableUpdatesAndUpdateStyleSheet(nullptr);
  }
}

void HTMLStyleElement::SetDevtoolsAsTriggeringPrincipal() {
  mTriggeringPrincipal = CreateDevtoolsPrincipal();
}

Maybe<LinkStyle::SheetInfo> HTMLStyleElement::GetStyleSheetInfo() {
  if (!IsCSSMimeTypeAttributeForStyleElement(*this)) {
    return Nothing();
  }

  nsAutoString title;
  nsAutoString media;
  GetTitleAndMediaForElement(*this, title, media);

  return Some(SheetInfo{
      *OwnerDoc(),
      this,
      nullptr,
      do_AddRef(mTriggeringPrincipal),
      MakeAndAddRef<ReferrerInfo>(*this),
      CORS_NONE,
      title,
      media,
      /* integrity = */ u""_ns,
      /* nsStyleUtil::CSPAllowsInlineStyle takes care of nonce checking for
         inline styles. Bug 1607011 */
      /* nonce = */ u""_ns,
      HasAlternateRel::No,
      IsInline::Yes,
      IsExplicitlyEnabled::No,
      FetchPriority::Auto,
  });
}

JSObject* HTMLStyleElement::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return HTMLStyleElement_Binding::Wrap(aCx, this, aGivenProto);
}

nsDOMTokenList* HTMLStyleElement::Blocking() {
  if (!mBlocking) {
    mBlocking =
        new nsDOMTokenList(this, nsGkAtoms::blocking, sSupportedBlockingValues);
  }
  return mBlocking;
}

bool HTMLStyleElement::IsPotentiallyRenderBlocking() {
  return BlockingContainsRender();

  // TODO: handle implicitly potentially render blocking
  // https://html.spec.whatwg.org/#implicitly-potentially-render-blocking
  // A style element is implicitly potentially render-blocking if the element
  // was created by its node document's parser.
}

nsresult HTMLStyleElement::CopyInnerTo(HTMLStyleElement* aDest) {
  nsresult rv = Element::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);
  MaybeStartCopyStyleSheetTo(aDest, aDest->OwnerDoc());
  return NS_OK;
}

}  // namespace mozilla::dom
