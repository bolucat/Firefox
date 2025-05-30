/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DOMIntersectionObserver.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLIFrameElementBinding.h"
#include "mozilla/dom/FeaturePolicy.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsSubDocumentFrame.h"
#include "nsError.h"
#include "nsContentUtils.h"
#include "nsSandboxFlags.h"
#include "nsNetUtil.h"

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(IFrame)

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLIFrameElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLIFrameElement,
                                                  nsGenericHTMLFrameElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFeaturePolicy)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSandbox)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLIFrameElement,
                                                nsGenericHTMLFrameElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFeaturePolicy)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSandbox)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(HTMLIFrameElement, nsGenericHTMLFrameElement)
NS_IMPL_RELEASE_INHERITED(HTMLIFrameElement, nsGenericHTMLFrameElement)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(HTMLIFrameElement)
NS_INTERFACE_MAP_END_INHERITING(nsGenericHTMLFrameElement)

// static
const DOMTokenListSupportedToken HTMLIFrameElement::sSupportedSandboxTokens[] =
    {
#define SANDBOX_KEYWORD(string, atom, flags) string,
#include "IframeSandboxKeywordList.h"
#undef SANDBOX_KEYWORD
        nullptr};

HTMLIFrameElement::HTMLIFrameElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    FromParser aFromParser)
    : nsGenericHTMLFrameElement(std::move(aNodeInfo), aFromParser) {
  // We always need a featurePolicy, even if not exposed.
  mFeaturePolicy = new mozilla::dom::FeaturePolicy(this);
  nsCOMPtr<nsIPrincipal> origin = GetFeaturePolicyDefaultOrigin();
  MOZ_ASSERT(origin);
  mFeaturePolicy->SetDefaultOrigin(origin);
}

HTMLIFrameElement::~HTMLIFrameElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLIFrameElement)

void HTMLIFrameElement::BindToBrowsingContext(BrowsingContext*) {
  RefreshFeaturePolicy(true /* parse the feature policy attribute */);
}

bool HTMLIFrameElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::marginwidth) {
      return aResult.ParseNonNegativeIntValue(aValue);
    }
    if (aAttribute == nsGkAtoms::marginheight) {
      return aResult.ParseNonNegativeIntValue(aValue);
    }
    if (aAttribute == nsGkAtoms::width) {
      return aResult.ParseHTMLDimension(aValue);
    }
    if (aAttribute == nsGkAtoms::height) {
      return aResult.ParseHTMLDimension(aValue);
    }
    if (aAttribute == nsGkAtoms::frameborder) {
      return ParseFrameborderValue(aValue, aResult);
    }
    if (aAttribute == nsGkAtoms::scrolling) {
      return ParseScrollingValue(aValue, aResult);
    }
    if (aAttribute == nsGkAtoms::align) {
      return ParseAlignValue(aValue, aResult);
    }
    if (aAttribute == nsGkAtoms::sandbox) {
      aResult.ParseAtomArray(aValue);
      return true;
    }
    if (aAttribute == nsGkAtoms::loading) {
      return ParseLoadingAttribute(aValue, aResult);
    }
  }

  return nsGenericHTMLFrameElement::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

void HTMLIFrameElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  // frameborder: 0 | 1 (| NO | YES in quirks mode)
  // If frameborder is 0 or No, set border to 0
  // else leave it as the value set in html.css
  const nsAttrValue* value = aBuilder.GetAttr(nsGkAtoms::frameborder);
  if (value && value->Type() == nsAttrValue::eEnum) {
    auto frameborder = static_cast<FrameBorderProperty>(value->GetEnumValue());
    if (FrameBorderProperty::No == frameborder ||
        FrameBorderProperty::Zero == frameborder) {
      aBuilder.SetPixelValueIfUnset(eCSSProperty_border_top_width, 0.0f);
      aBuilder.SetPixelValueIfUnset(eCSSProperty_border_right_width, 0.0f);
      aBuilder.SetPixelValueIfUnset(eCSSProperty_border_bottom_width, 0.0f);
      aBuilder.SetPixelValueIfUnset(eCSSProperty_border_left_width, 0.0f);
    }
  }

  nsGenericHTMLElement::MapImageSizeAttributesInto(aBuilder);
  nsGenericHTMLElement::MapImageAlignAttributeInto(aBuilder);
  nsGenericHTMLElement::MapCommonAttributesInto(aBuilder);
}

NS_IMETHODIMP_(bool)
HTMLIFrameElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {
      {nsGkAtoms::width},
      {nsGkAtoms::height},
      {nsGkAtoms::frameborder},
      {nullptr},
  };

  static const MappedAttributeEntry* const map[] = {
      attributes,
      sImageAlignAttributeMap,
      sCommonAttributeMap,
  };

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc HTMLIFrameElement::GetAttributeMappingFunction()
    const {
  return &MapAttributesIntoRule;
}

void HTMLIFrameElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     bool aNotify) {
  AfterMaybeChangeAttr(aNameSpaceID, aName, aNotify);

  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::loading) {
      if (aValue && Loading(aValue->GetEnumValue()) == Loading::Lazy) {
        SetLazyLoading();
      } else if (aOldValue &&
                 Loading(aOldValue->GetEnumValue()) == Loading::Lazy) {
        StopLazyLoading();
      }
    }

    // If lazy loading and src set, set lazy loading again as we are doing a new
    // load (lazy loading is unset after a load is complete).
    if ((aName == nsGkAtoms::src || aName == nsGkAtoms::srcdoc) &&
        LoadingState() == Loading::Lazy) {
      SetLazyLoading();
    }

    if (aName == nsGkAtoms::sandbox) {
      if (mFrameLoader) {
        // If we have an nsFrameLoader, apply the new sandbox flags.
        // Since this is called after the setter, the sandbox flags have
        // alreay been updated.
        mFrameLoader->ApplySandboxFlags(GetSandboxFlags());
      }
    }

    if (aName == nsGkAtoms::allow || aName == nsGkAtoms::src ||
        aName == nsGkAtoms::srcdoc || aName == nsGkAtoms::sandbox) {
      RefreshFeaturePolicy(true /* parse the feature policy attribute */);
    } else if (aName == nsGkAtoms::allowfullscreen) {
      RefreshFeaturePolicy(false /* parse the feature policy attribute */);
    }
  }

  return nsGenericHTMLFrameElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

void HTMLIFrameElement::OnAttrSetButNotChanged(
    int32_t aNamespaceID, nsAtom* aName, const nsAttrValueOrString& aValue,
    bool aNotify) {
  AfterMaybeChangeAttr(aNamespaceID, aName, aNotify);

  return nsGenericHTMLFrameElement::OnAttrSetButNotChanged(aNamespaceID, aName,
                                                           aValue, aNotify);
}

void HTMLIFrameElement::AfterMaybeChangeAttr(int32_t aNamespaceID,
                                             nsAtom* aName, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::srcdoc) {
      // Don't propagate errors from LoadSrc. The attribute was successfully
      // set/unset, that's what we should reflect.
      LoadSrc();
    }
  }
}

uint32_t HTMLIFrameElement::GetSandboxFlags() const {
  const nsAttrValue* sandboxAttr = GetParsedAttr(nsGkAtoms::sandbox);
  // No sandbox attribute, no sandbox flags.
  if (!sandboxAttr) {
    return SANDBOXED_NONE;
  }
  return nsContentUtils::ParseSandboxAttributeToFlags(sandboxAttr);
}

JSObject* HTMLIFrameElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return HTMLIFrameElement_Binding::Wrap(aCx, this, aGivenProto);
}

mozilla::dom::FeaturePolicy* HTMLIFrameElement::FeaturePolicy() const {
  return mFeaturePolicy;
}

void HTMLIFrameElement::MaybeStoreCrossOriginFeaturePolicy() {
  if (!mFrameLoader) {
    return;
  }

  // If the browsingContext is not ready (because docshell is dead), don't try
  // to create one.
  if (!mFrameLoader->IsRemoteFrame() && !mFrameLoader->GetExistingDocShell()) {
    return;
  }

  RefPtr<BrowsingContext> browsingContext = mFrameLoader->GetBrowsingContext();

  if (!browsingContext || !browsingContext->IsContentSubframe()) {
    return;
  }

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    Unused << cc->SendSetContainerFeaturePolicy(
        browsingContext, Some(mFeaturePolicy->ToFeaturePolicyInfo()));
  }
}

already_AddRefed<nsIPrincipal>
HTMLIFrameElement::GetFeaturePolicyDefaultOrigin() const {
  nsCOMPtr<nsIPrincipal> principal;

  if (HasAttr(nsGkAtoms::srcdoc)) {
    principal = NodePrincipal();
    return principal.forget();
  }

  nsCOMPtr<nsIURI> nodeURI;
  if (GetURIAttr(nsGkAtoms::src, nullptr, getter_AddRefs(nodeURI)) && nodeURI) {
    principal = BasePrincipal::CreateContentPrincipal(
        nodeURI, BasePrincipal::Cast(NodePrincipal())->OriginAttributesRef());
  }

  if (!principal) {
    principal = NodePrincipal();
  }

  return principal.forget();
}

void HTMLIFrameElement::RefreshFeaturePolicy(bool aParseAllowAttribute) {
  if (aParseAllowAttribute) {
    mFeaturePolicy->ResetDeclaredPolicy();

    // The origin can change if 'src' and 'srcdoc' attributes change.
    nsCOMPtr<nsIPrincipal> origin = GetFeaturePolicyDefaultOrigin();
    MOZ_ASSERT(origin);
    mFeaturePolicy->SetDefaultOrigin(origin);

    nsAutoString allow;
    GetAttr(nsGkAtoms::allow, allow);

    if (!allow.IsEmpty()) {
      // Set or reset the FeaturePolicy directives.
      mFeaturePolicy->SetDeclaredPolicy(OwnerDoc(), allow, NodePrincipal(),
                                        origin);
    }
  }

  if (AllowFullscreen()) {
    mFeaturePolicy->MaybeSetAllowedPolicy(u"fullscreen"_ns);
  }

  mFeaturePolicy->InheritPolicy(OwnerDoc()->FeaturePolicy());
  MaybeStoreCrossOriginFeaturePolicy();
}

void HTMLIFrameElement::UpdateLazyLoadState() {
  // Store current base URI and referrer policy in the lazy load state.
  mLazyLoadState.mBaseURI = GetBaseURI();
  mLazyLoadState.mReferrerPolicy = GetReferrerPolicyAsEnum();
}

nsresult HTMLIFrameElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  // Update lazy load state on bind to tree again if lazy loading, as the
  // loading attribute could be set before others.
  if (mLazyLoading) {
    UpdateLazyLoadState();
  }

  return nsGenericHTMLFrameElement::BindToTree(aContext, aParent);
}

void HTMLIFrameElement::SetLazyLoading() {
  if (mLazyLoading) {
    return;
  }

  // https://html.spec.whatwg.org/multipage/urls-and-fetching.html#will-lazy-load-element-steps
  // "If scripting is disabled for element, then return false."
  Document* doc = OwnerDoc();
  if (!doc->IsScriptEnabled() || doc->IsStaticDocument()) {
    return;
  }

  doc->EnsureLazyLoadObserver().Observe(*this);
  mLazyLoading = true;

  UpdateLazyLoadState();
}

void HTMLIFrameElement::StopLazyLoading() {
  CancelLazyLoading(false /* aClearLazyLoadState */);

  LoadSrc();

  mLazyLoadState.Clear();
  if (nsSubDocumentFrame* ourFrame = do_QueryFrame(GetPrimaryFrame())) {
    ourFrame->ResetFrameLoader(nsSubDocumentFrame::RetainPaintData::No);
  }
}

void HTMLIFrameElement::NodeInfoChanged(Document* aOldDoc) {
  nsGenericHTMLElement::NodeInfoChanged(aOldDoc);

  if (mLazyLoading) {
    aOldDoc->GetLazyLoadObserver()->Unobserve(*this);
    mLazyLoading = false;
  }

  if (LoadingState() == Loading::Lazy) {
    SetLazyLoading();
  }
}

void HTMLIFrameElement::CancelLazyLoading(bool aClearLazyLoadState) {
  if (!mLazyLoading) {
    return;
  }

  Document* doc = OwnerDoc();
  if (auto* obs = doc->GetLazyLoadObserver()) {
    obs->Unobserve(*this);
  }

  mLazyLoading = false;

  if (aClearLazyLoadState) {
    mLazyLoadState.Clear();
  }
}

void HTMLIFrameElement::GetSrcdoc(OwningTrustedHTMLOrString& aSrcdoc) {
  GetHTMLAttr(nsGkAtoms::srcdoc, aSrcdoc.SetAsString());
}

void HTMLIFrameElement::SetSrcdoc(const TrustedHTMLOrString& aSrcdoc,
                                  nsIPrincipal* aSubjectPrincipal,
                                  ErrorResult& aError) {
  constexpr nsLiteralString sink = u"HTMLIFrameElement srcdoc"_ns;

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aSrcdoc, sink, kTrustedTypesOnlySinkGroup, *this, aSubjectPrincipal,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return;
  }

  SetHTMLAttr(nsGkAtoms::srcdoc, *compliantString, aError);
}

}  // namespace mozilla::dom
