/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EffectCompositor.h"

#include "mozilla/dom/Animation.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/AnimationComparator.h"
#include "mozilla/AnimationPerformanceWarning.h"
#include "mozilla/AnimationTarget.h"
#include "mozilla/AnimationUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/EffectSet.h"
#include "mozilla/LayerAnimationInfo.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoBindings.h"  // Servo_GetProperties_Overriding_Animation
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/SVGObserverUtils.h"
#include "nsComputedDOMStyle.h"
#include "nsContentUtils.h"
#include "nsCSSPropertyIDSet.h"
#include "nsCSSProps.h"
#include "nsDisplayItemTypes.h"
#include "nsLayoutUtils.h"
#include "nsTArray.h"

using mozilla::dom::Animation;
using mozilla::dom::Element;
using mozilla::dom::KeyframeEffect;

namespace mozilla {

NS_IMPL_CYCLE_COLLECTION_CLASS(EffectCompositor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(EffectCompositor)
  for (auto& elementSet : tmp->mElementsToRestyle) {
    elementSet.Clear();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(EffectCompositor)
  for (const auto& elementSet : tmp->mElementsToRestyle) {
    for (const auto& key : elementSet.Keys()) {
      CycleCollectionNoteChild(cb, key.mElement,
                               "EffectCompositor::mElementsToRestyle[]",
                               cb.Flags());
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

/* static */
bool EffectCompositor::AllowCompositorAnimationsOnFrame(
    const nsIFrame* aFrame,
    AnimationPerformanceWarning::Type& aWarning /* out */) {
  if (aFrame->RefusedAsyncAnimation()) {
    return false;
  }

  if (!nsLayoutUtils::AreAsyncAnimationsEnabled()) {
    if (StaticPrefs::layers_offmainthreadcomposition_log_animations()) {
      nsCString message;
      message.AppendLiteral(
          "Performance warning: Async animations are "
          "disabled");
      AnimationUtils::LogAsyncAnimationFailure(message);
    }
    return false;
  }

  // Disable async animations if we have a rendering observer that
  // depends on our content (svg masking, -moz-element etc) so that
  // it gets updated correctly.
  if (SVGObserverUtils::SelfOrAncestorHasRenderingObservers(aFrame)) {
    aWarning = AnimationPerformanceWarning::Type::HasRenderingObserver;
    return false;
  }

  return true;
}

// Helper function to factor out the common logic from
// GetAnimationsForCompositor and HasAnimationsForCompositor.
//
// Takes an optional array to fill with eligible animations.
//
// Returns true if there are eligible animations, false otherwise.
bool FindAnimationsForCompositor(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet,
    nsTArray<RefPtr<dom::Animation>>* aMatches /*out*/) {
  // Do not process any animations on the compositor when in print or print
  // preview.
  if (aFrame->PresContext()->IsPrintingOrPrintPreview()) {
    return false;
  }

  MOZ_ASSERT(
      aPropertySet.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
          DisplayItemType::TYPE_TRANSFORM)) ||
          aPropertySet.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
              DisplayItemType::TYPE_OPACITY)) ||
          aPropertySet.IsSubsetOf(LayerAnimationInfo::GetCSSPropertiesFor(
              DisplayItemType::TYPE_BACKGROUND_COLOR)),
      "Should be the subset of transform-like properties, or opacity, "
      "or background color");

  MOZ_ASSERT(!aMatches || aMatches->IsEmpty(),
             "Matches array, if provided, should be empty");

  EffectSet* effects = EffectSet::GetForFrame(aFrame, aPropertySet);
  if (!effects || effects->IsEmpty()) {
    return false;
  }

  AnimationPerformanceWarning::Type warning =
      AnimationPerformanceWarning::Type::None;
  if (!EffectCompositor::AllowCompositorAnimationsOnFrame(aFrame, warning)) {
    if (warning != AnimationPerformanceWarning::Type::None) {
      EffectCompositor::SetPerformanceWarning(
          aFrame, aPropertySet, AnimationPerformanceWarning(warning));
    }
    return false;
  }

  // The animation cascade will almost always be up-to-date by this point
  // but there are some cases such as when we are restoring the refresh driver
  // from test control after seeking where it might not be the case.
  //
  // Those cases are probably not important but just to be safe, let's make
  // sure the cascade is up to date since if it *is* up to date, this is
  // basically a no-op.
  Maybe<NonOwningAnimationTarget> pseudoElement =
      EffectCompositor::GetAnimationElementAndPseudoForFrame(
          nsLayoutUtils::GetStyleFrame(aFrame));
  MOZ_ASSERT(pseudoElement,
             "We have a valid element for the frame, if we don't we should "
             "have bailed out at above the call to EffectSet::Get");
  EffectCompositor::MaybeUpdateCascadeResults(pseudoElement->mElement,
                                              pseudoElement->mPseudoRequest);

  bool foundRunningAnimations = false;
  for (KeyframeEffect* effect : *effects) {
    auto effectWarning = AnimationPerformanceWarning::Type::None;
    KeyframeEffect::MatchForCompositor matchResult =
        effect->IsMatchForCompositor(aPropertySet, aFrame, *effects,
                                     effectWarning);
    if (effectWarning != AnimationPerformanceWarning::Type::None) {
      EffectCompositor::SetPerformanceWarning(
          aFrame, aPropertySet, AnimationPerformanceWarning(effectWarning));
    }

    if (matchResult ==
        KeyframeEffect::MatchForCompositor::NoAndBlockThisProperty) {
      // For a given |aFrame|, we don't want some animations of |aPropertySet|
      // to run on the compositor and others to run on the main thread, so if
      // any need to be synchronized with the main thread, run them all there.
      if (aMatches) {
        aMatches->Clear();
      }
      return false;
    }

    if (matchResult == KeyframeEffect::MatchForCompositor::No) {
      continue;
    }

    if (aMatches) {
      aMatches->AppendElement(effect->GetAnimation());
    }

    if (matchResult == KeyframeEffect::MatchForCompositor::Yes) {
      foundRunningAnimations = true;
    }
  }

  // If all animations we added were not currently playing animations, don't
  // send them to the compositor.
  if (aMatches && !foundRunningAnimations) {
    aMatches->Clear();
  }

  MOZ_ASSERT(!foundRunningAnimations || !aMatches || !aMatches->IsEmpty(),
             "If return value is true, matches array should be non-empty");

  if (aMatches && foundRunningAnimations) {
    aMatches->Sort(AnimationPtrComparator<RefPtr<dom::Animation>>());
  }

  return foundRunningAnimations;
}

void EffectCompositor::RequestRestyle(dom::Element* aElement,
                                      const PseudoStyleRequest& aPseudoRequest,
                                      RestyleType aRestyleType,
                                      CascadeLevel aCascadeLevel) {
  if (!mPresContext) {
    // Pres context will be null after the effect compositor is disconnected.
    return;
  }

  // Ignore animations on orphaned elements and elements in documents without
  // a pres shell (e.g. XMLHttpRequest responseXML documents).
  if (!nsContentUtils::GetPresShellForContent(aElement)) {
    return;
  }

  auto& elementsToRestyle = mElementsToRestyle[aCascadeLevel];
  PseudoElementHashEntry::KeyType key = {aElement, aPseudoRequest};

  bool& restyleEntry = elementsToRestyle.LookupOrInsert(key, false);
  if (aRestyleType == RestyleType::Throttled) {
    mPresContext->PresShell()->SetNeedThrottledAnimationFlush();
  } else {
    // Update hashtable first in case PostRestyleForAnimation mutates it
    // and invalidates the restyleEntry reference.
    // (It shouldn't, but just to be sure.)
    bool skipRestyle = std::exchange(restyleEntry, true);
    if (!skipRestyle) {
      PostRestyleForAnimation(aElement, aPseudoRequest, aCascadeLevel);
    }
  }

  if (aRestyleType == RestyleType::Layer) {
    mPresContext->RestyleManager()->IncrementAnimationGeneration();
    if (auto* effectSet = EffectSet::Get(aElement, aPseudoRequest)) {
      effectSet->UpdateAnimationGeneration(mPresContext);
    }
  }
}

void EffectCompositor::PostRestyleForAnimation(
    dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    CascadeLevel aCascadeLevel) {
  if (!mPresContext) {
    return;
  }

  // FIXME: Bug 1615083 KeyframeEffect::SetTarget() and
  // KeyframeEffect::SetPseudoElement() may set a non-existing pseudo element,
  // and we still have to update its style, based on the wpt. However, we don't
  // have the generated element here, so we failed the wpt.
  //
  // See wpt for more info: web-animations/interfaces/KeyframeEffect/target.html
  Element* element = aElement->GetPseudoElement(aPseudoRequest);
  if (!element) {
    return;
  }

  RestyleHint hint = aCascadeLevel == CascadeLevel::Transitions
                         ? RestyleHint::RESTYLE_CSS_TRANSITIONS
                         : RestyleHint::RESTYLE_CSS_ANIMATIONS;

  MOZ_ASSERT(NS_IsMainThread(),
             "Restyle request during restyling should be requested only on "
             "the main-thread. e.g. after the parallel traversal");
  if (ServoStyleSet::IsInServoTraversal() || mIsInPreTraverse) {
    MOZ_ASSERT(hint == RestyleHint::RESTYLE_CSS_ANIMATIONS ||
               hint == RestyleHint::RESTYLE_CSS_TRANSITIONS);

    // We can't call Servo_NoteExplicitHints here since AtomicRefCell does not
    // allow us mutate ElementData of the |aElement| in SequentialTask.
    // Instead we call Servo_NoteExplicitHints for the element in PreTraverse()
    // which will be called right before the second traversal that we do for
    // updating CSS animations.
    // In that case PreTraverse() will return true so that we know to do the
    // second traversal so we don't need to post any restyle requests to the
    // PresShell.
    return;
  }

  MOZ_ASSERT(!mPresContext->RestyleManager()->IsInStyleRefresh());

  mPresContext->PresShell()->RestyleForAnimation(element, hint);
}

void EffectCompositor::PostRestyleForThrottledAnimations() {
  for (size_t i = 0; i < kCascadeLevelCount; i++) {
    CascadeLevel cascadeLevel = CascadeLevel(i);
    auto& elementSet = mElementsToRestyle[cascadeLevel];

    for (auto iter = elementSet.Iter(); !iter.Done(); iter.Next()) {
      bool& postedRestyle = iter.Data();
      if (postedRestyle) {
        continue;
      }

      PostRestyleForAnimation(iter.Key().mElement, iter.Key().mPseudoRequest,
                              cascadeLevel);
      postedRestyle = true;
    }
  }
}

void EffectCompositor::UpdateEffectProperties(
    const ComputedStyle* aStyle, Element* aElement,
    const PseudoStyleRequest& aPseudoRequest) {
  EffectSet* effectSet = EffectSet::Get(aElement, aPseudoRequest);
  if (!effectSet) {
    return;
  }

  // Style context (Gecko) or computed values (Stylo) change might cause CSS
  // cascade level, e.g removing !important, so we should update the cascading
  // result.
  effectSet->MarkCascadeNeedsUpdate();

  for (KeyframeEffect* effect : *effectSet) {
    effect->UpdateProperties(aStyle);
  }
}

namespace {
class EffectCompositeOrderComparator {
  mutable nsContentUtils::NodeIndexCache mCache;

 public:
  bool Equals(const KeyframeEffect* a, const KeyframeEffect* b) const {
    return a == b;
  }

  bool LessThan(const KeyframeEffect* a, const KeyframeEffect* b) const {
    MOZ_ASSERT(a->GetAnimation());
    MOZ_ASSERT(b->GetAnimation());
    const int32_t cmp =
        a->GetAnimation()->CompareCompositeOrder(*b->GetAnimation(), mCache);
    MOZ_ASSERT(Equals(a, b) || cmp != 0);
    return cmp < 0;
  }
};
}  // namespace

static void ComposeSortedEffects(
    const nsTArray<KeyframeEffect*>& aSortedEffects,
    const EffectSet* aEffectSet, EffectCompositor::CascadeLevel aCascadeLevel,
    StyleAnimationValueMap* aAnimationValues,
    dom::EndpointBehavior aEndpointBehavior =
        dom::EndpointBehavior::Exclusive) {
  const bool isTransition =
      aCascadeLevel == EffectCompositor::CascadeLevel::Transitions;
  InvertibleAnimatedPropertyIDSet propertiesToSkip;
  // Transitions should be overridden by running animations of the same
  // property per https://drafts.csswg.org/css-transitions/#application:
  //
  // > Implementations must add this value to the cascade if and only if that
  // > property is not currently undergoing a CSS Animation on the same element.
  //
  // FIXME(emilio, bug 1606176): This should assert that
  // aEffectSet->PropertiesForAnimationsLevel() is up-to-date, and it may not
  // follow the spec in those cases. There are various places where we get style
  // without flushing that would trigger the below assertion.
  //
  // MOZ_ASSERT_IF(aEffectSet, !aEffectSet->CascadeNeedsUpdate());
  if (aEffectSet) {
    // Note that we do invert the set on CascadeLevel::Animations because we
    // don't want to skip those properties when composing the animation rule on
    // CascadeLevel::Animations.
    propertiesToSkip.Setup(&aEffectSet->PropertiesForAnimationsLevel(),
                           !isTransition);
  }

  for (KeyframeEffect* effect : aSortedEffects) {
    auto* animation = effect->GetAnimation();
    MOZ_ASSERT(!isTransition || animation->CascadeLevel() == aCascadeLevel);
    animation->ComposeStyle(*aAnimationValues, propertiesToSkip,
                            aEndpointBehavior);
  }
}

bool EffectCompositor::GetServoAnimationRule(
    const dom::Element* aElement, const PseudoStyleRequest& aPseudoRequest,
    CascadeLevel aCascadeLevel, StyleAnimationValueMap* aAnimationValues) {
  MOZ_ASSERT(aAnimationValues);
  // Gecko_GetAnimationRule should have already checked this
  MOZ_ASSERT(nsContentUtils::GetPresShellForContent(aElement),
             "Should not be trying to run animations on elements in documents"
             " without a pres shell (e.g. XMLHttpRequest documents)");

  EffectSet* effectSet = EffectSet::Get(aElement, aPseudoRequest);
  if (!effectSet) {
    return false;
  }

  const bool isTransition = aCascadeLevel == CascadeLevel::Transitions;

  // Get a list of effects sorted by composite order.
  nsTArray<KeyframeEffect*> sortedEffectList(effectSet->Count());
  for (KeyframeEffect* effect : *effectSet) {
    if (isTransition &&
        effect->GetAnimation()->CascadeLevel() != aCascadeLevel) {
      // We may need to use transition rules for the animations level for the
      // case of missing keyframes in animations, but we don't ever need to look
      // at non-transition levels to build a transition rule. When the effect
      // set information is out of date (see above), this avoids creating bogus
      // transition rules, see bug 1605610.
      continue;
    }
    sortedEffectList.AppendElement(effect);
  }

  if (sortedEffectList.IsEmpty()) {
    return false;
  }

  sortedEffectList.Sort(EffectCompositeOrderComparator());

  ComposeSortedEffects(sortedEffectList, effectSet, aCascadeLevel,
                       aAnimationValues);

  MOZ_ASSERT(effectSet == EffectSet::Get(aElement, aPseudoRequest),
             "EffectSet should not change while composing style");

  return true;
}

bool EffectCompositor::ComposeServoAnimationRuleForEffect(
    KeyframeEffect& aEffect, CascadeLevel aCascadeLevel,
    StyleAnimationValueMap* aAnimationValues,
    dom::EndpointBehavior aEndpointBehavior) {
  MOZ_ASSERT(aAnimationValues);
  MOZ_ASSERT(mPresContext && mPresContext->IsDynamic(),
             "Should not be in print preview");

  NonOwningAnimationTarget target = aEffect.GetAnimationTarget();
  if (!target) {
    return false;
  }

  // Don't try to compose animations for elements in documents without a pres
  // shell (e.g. XMLHttpRequest documents).
  if (!nsContentUtils::GetPresShellForContent(target.mElement)) {
    return false;
  }

  // GetServoAnimationRule is called as part of the regular style resolution
  // where the cascade results are updated in the pre-traversal as needed.
  // This function, however, is only called when committing styles so we
  // need to ensure the cascade results are up-to-date manually.
  MaybeUpdateCascadeResults(target.mElement, target.mPseudoRequest);

  // We may need to update the base styles cached on the keyframes for |aEffect|
  // since they won't be updated as part of the regular animation processing if
  // |aEffect| has finished but doesn't have an appropriate fill mode.
  // We can get computed style without flush, because |CommitStyles| should have
  // already flushed styles.
  RefPtr<const ComputedStyle> style =
      nsComputedDOMStyle::GetComputedStyleNoFlush(target.mElement,
                                                  target.mPseudoRequest);
  aEffect.UpdateBaseStyle(style);

  EffectSet* effectSet = EffectSet::Get(target);

  // Get a list of effects sorted by composite order up to and including
  // |aEffect|, even if it is not in the EffectSet.
  auto comparator = EffectCompositeOrderComparator();
  nsTArray<KeyframeEffect*> sortedEffectList(effectSet ? effectSet->Count() + 1
                                                       : 1);
  if (effectSet) {
    for (KeyframeEffect* effect : *effectSet) {
      if (comparator.LessThan(effect, &aEffect)) {
        sortedEffectList.AppendElement(effect);
      }
    }
    sortedEffectList.Sort(comparator);
  }
  sortedEffectList.AppendElement(&aEffect);

  ComposeSortedEffects(sortedEffectList, effectSet, aCascadeLevel,
                       aAnimationValues, aEndpointBehavior);

  MOZ_ASSERT(effectSet == EffectSet::Get(target),
             "EffectSet should not change while composing style");

  return true;
}

bool EffectCompositor::HasPendingStyleUpdates() const {
  for (auto& elementSet : mElementsToRestyle) {
    if (elementSet.Count()) {
      return true;
    }
  }

  return false;
}

/* static */
bool EffectCompositor::HasAnimationsForCompositor(const nsIFrame* aFrame,
                                                  DisplayItemType aType) {
  return FindAnimationsForCompositor(
      aFrame, LayerAnimationInfo::GetCSSPropertiesFor(aType), nullptr);
}

/* static */
nsTArray<RefPtr<dom::Animation>> EffectCompositor::GetAnimationsForCompositor(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet) {
  nsTArray<RefPtr<dom::Animation>> result;

#ifdef DEBUG
  bool foundSome =
#endif
      FindAnimationsForCompositor(aFrame, aPropertySet, &result);
  MOZ_ASSERT(!foundSome || !result.IsEmpty(),
             "If return value is true, matches array should be non-empty");

  return result;
}

/* static */
void EffectCompositor::ClearIsRunningOnCompositor(const nsIFrame* aFrame,
                                                  DisplayItemType aType) {
  EffectSet* effects = EffectSet::GetForFrame(aFrame, aType);
  if (!effects) {
    return;
  }

  const nsCSSPropertyIDSet& propertySet =
      LayerAnimationInfo::GetCSSPropertiesFor(aType);
  for (KeyframeEffect* effect : *effects) {
    effect->SetIsRunningOnCompositor(propertySet, false);
  }
}

/* static */
void EffectCompositor::MaybeUpdateCascadeResults(
    Element* aElement, const PseudoStyleRequest& aPseudoRequest) {
  EffectSet* effects = EffectSet::Get(aElement, aPseudoRequest);
  if (!effects || !effects->CascadeNeedsUpdate()) {
    return;
  }

  UpdateCascadeResults(*effects, aElement, aPseudoRequest);

  MOZ_ASSERT(!effects->CascadeNeedsUpdate(), "Failed to update cascade state");
}

/* static */
Maybe<NonOwningAnimationTarget>
EffectCompositor::GetAnimationElementAndPseudoForFrame(const nsIFrame* aFrame) {
  // Always return the same object to benefit from return-value optimization.
  Maybe<NonOwningAnimationTarget> result;

  auto request = PseudoStyleRequest(aFrame->Style()->GetPseudoType());
  const bool isSupportedPseudo =
      AnimationUtils::IsSupportedPseudoForAnimations(request);

  // If it is a pseudo element but we don't support animations for it, just
  // return.
  if (!request.IsNotPseudo() && !isSupportedPseudo) {
    return result;
  }

  nsIContent* content = aFrame->GetContent();
  if (!content || !content->IsElement()) {
    return result;
  }

  Element* element = content->AsElement();
  switch (request.mType) {
    case PseudoStyleType::before:
    case PseudoStyleType::after:
    case PseudoStyleType::marker: {
      nsIContent* parent = element->GetParent();
      if (!parent || !parent->IsElement()) {
        return result;
      }
      element = parent->AsElement();
      break;
    }
    case PseudoStyleType::viewTransition:
    case PseudoStyleType::viewTransitionGroup:
    case PseudoStyleType::viewTransitionImagePair:
    case PseudoStyleType::viewTransitionOld:
    case PseudoStyleType::viewTransitionNew: {
      request.mIdentifier =
          element->HasName()
              ? element->GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
              : nullptr;
      element = element->OwnerDoc()->GetRootElement();
      break;
    }
    case PseudoStyleType::NotPseudo:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown PseudoStyleType");
  }

  result.emplace(element, request);
  return result;
}

/* static */
nsCSSPropertyIDSet EffectCompositor::GetOverriddenProperties(
    EffectSet& aEffectSet, Element* aElement,
    const PseudoStyleRequest& aPseudoRequest) {
  MOZ_ASSERT(aElement, "Should have an element to get style data from");

  nsCSSPropertyIDSet result;

  Element* elementForRestyle = aElement->GetPseudoElement(aPseudoRequest);
  if (!elementForRestyle) {
    return result;
  }

  static constexpr size_t compositorAnimatableCount =
      nsCSSPropertyIDSet::CompositorAnimatableCount();
  AutoTArray<nsCSSPropertyID, compositorAnimatableCount> propertiesToTrack;
  {
    nsCSSPropertyIDSet propertiesToTrackAsSet;
    for (KeyframeEffect* effect : aEffectSet) {
      for (const AnimationProperty& property : effect->Properties()) {
        // Custom properties don't run on the compositor.
        if (property.mProperty.IsCustom()) {
          continue;
        }

        if (nsCSSProps::PropHasFlags(property.mProperty.mID,
                                     CSSPropFlags::CanAnimateOnCompositor) &&
            !propertiesToTrackAsSet.HasProperty(property.mProperty.mID)) {
          propertiesToTrackAsSet.AddProperty(property.mProperty.mID);
          propertiesToTrack.AppendElement(property.mProperty.mID);
        }
      }
      // Skip iterating over the rest of the effects if we've already
      // found all the compositor-animatable properties.
      if (propertiesToTrack.Length() == compositorAnimatableCount) {
        break;
      }
    }
  }

  if (propertiesToTrack.IsEmpty()) {
    return result;
  }

  Servo_GetProperties_Overriding_Animation(elementForRestyle,
                                           &propertiesToTrack, &result);
  return result;
}

/* static */
void EffectCompositor::UpdateCascadeResults(
    EffectSet& aEffectSet, Element* aElement,
    const PseudoStyleRequest& aPseudoRequest) {
  MOZ_ASSERT(EffectSet::Get(aElement, aPseudoRequest) == &aEffectSet,
             "Effect set should correspond to the specified (pseudo-)element");
  if (aEffectSet.IsEmpty()) {
    aEffectSet.MarkCascadeUpdated();
    return;
  }

  // Get a list of effects sorted by composite order.
  nsTArray<KeyframeEffect*> sortedEffectList(aEffectSet.Count());
  for (KeyframeEffect* effect : aEffectSet) {
    sortedEffectList.AppendElement(effect);
  }
  sortedEffectList.Sort(EffectCompositeOrderComparator());

  // Get properties that override the *animations* level of the cascade.
  //
  // We only do this for properties that we can animate on the compositor
  // since we will apply other properties on the main thread where the usual
  // cascade applies.
  nsCSSPropertyIDSet overriddenProperties =
      GetOverriddenProperties(aEffectSet, aElement, aPseudoRequest);

  nsCSSPropertyIDSet& propertiesWithImportantRules =
      aEffectSet.PropertiesWithImportantRules();

  static constexpr nsCSSPropertyIDSet compositorAnimatables =
      nsCSSPropertyIDSet::CompositorAnimatables();
  // Record which compositor-animatable properties were originally set so we can
  // compare for changes later.
  nsCSSPropertyIDSet prevCompositorPropertiesWithImportantRules =
      propertiesWithImportantRules.Intersect(compositorAnimatables);

  propertiesWithImportantRules.Empty();

  AnimatedPropertyIDSet propertiesForAnimationsLevel;
  AnimatedPropertyIDSet propertiesForTransitionsLevel;

  for (const KeyframeEffect* effect : sortedEffectList) {
    MOZ_ASSERT(effect->GetAnimation(),
               "Effects on a target element should have an Animation");
    CascadeLevel cascadeLevel = effect->GetAnimation()->CascadeLevel();

    for (const AnimationProperty& prop : effect->Properties()) {
      // Note that nsCSSPropertyIDSet::HasProperty() returns false for custom
      // properties. We don't support custom properties for compositor
      // animations, so we are still using nsCSSPropertyIDSet to handle these
      // properties.
      // TODO: Bug 1869475. Support custom properties for compositor animations.
      if (overriddenProperties.HasProperty(prop.mProperty)) {
        propertiesWithImportantRules.AddProperty(prop.mProperty.mID);
      }

      switch (cascadeLevel) {
        case EffectCompositor::CascadeLevel::Animations:
          propertiesForAnimationsLevel.AddProperty(prop.mProperty);
          break;
        case EffectCompositor::CascadeLevel::Transitions:
          propertiesForTransitionsLevel.AddProperty(prop.mProperty);
          break;
      }
    }
  }

  aEffectSet.MarkCascadeUpdated();

  // Update EffectSet::mPropertiesForAnimationsLevel to the new set, after
  // exiting this scope.
  auto scopeExit = MakeScopeExit([&] {
    aEffectSet.PropertiesForAnimationsLevel() =
        std::move(propertiesForAnimationsLevel);
  });

  nsPresContext* presContext = nsContentUtils::GetContextForContent(aElement);
  if (!presContext) {
    return;
  }

  // If properties for compositor are newly overridden by !important rules, or
  // released from being overridden by !important rules, we need to update
  // layers for animations level because it's a trigger to send animations to
  // the compositor or pull animations back from the compositor.
  if (!prevCompositorPropertiesWithImportantRules.Equals(
          propertiesWithImportantRules.Intersect(compositorAnimatables))) {
    presContext->EffectCompositor()->RequestRestyle(
        aElement, aPseudoRequest, EffectCompositor::RestyleType::Layer,
        EffectCompositor::CascadeLevel::Animations);
  }

  // If we have transition properties and if the same propery for animations
  // level is newly added or removed, we need to update the transition level
  // rule since the it will be added/removed from the rule tree.
  const AnimatedPropertyIDSet& prevPropertiesForAnimationsLevel =
      aEffectSet.PropertiesForAnimationsLevel();
  const AnimatedPropertyIDSet& changedPropertiesForAnimationLevel =
      prevPropertiesForAnimationsLevel.Xor(propertiesForAnimationsLevel);
  const AnimatedPropertyIDSet& commonProperties =
      propertiesForTransitionsLevel.Intersect(
          changedPropertiesForAnimationLevel);
  if (!commonProperties.IsEmpty()) {
    EffectCompositor::RestyleType restyleType =
        changedPropertiesForAnimationLevel.Intersects(compositorAnimatables)
            ? EffectCompositor::RestyleType::Standard
            : EffectCompositor::RestyleType::Layer;
    presContext->EffectCompositor()->RequestRestyle(
        aElement, aPseudoRequest, restyleType,
        EffectCompositor::CascadeLevel::Transitions);
  }
}

/* static */
void EffectCompositor::SetPerformanceWarning(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet,
    const AnimationPerformanceWarning& aWarning) {
  EffectSet* effects = EffectSet::GetForFrame(aFrame, aPropertySet);
  if (!effects) {
    return;
  }

  for (KeyframeEffect* effect : *effects) {
    effect->SetPerformanceWarning(aPropertySet, aWarning);
  }
}

bool EffectCompositor::PreTraverse(ServoTraversalFlags aFlags) {
  return PreTraverseInSubtree(aFlags, nullptr);
}

bool EffectCompositor::PreTraverseInSubtree(ServoTraversalFlags aFlags,
                                            Element* aRoot) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aRoot || nsContentUtils::GetPresShellForContent(aRoot),
             "Traversal root, if provided, should be bound to a display "
             "document");

  // Convert the root element to the parent element if the root element is
  // pseudo since we check each element in mElementsToRestyle is in the subtree
  // of the root element later in this function, but for pseudo elements the
  // element in mElementsToRestyle is the parent of the pseudo.
  if (aRoot && (aRoot->IsGeneratedContentContainerForBefore() ||
                aRoot->IsGeneratedContentContainerForAfter() ||
                aRoot->IsGeneratedContentContainerForMarker())) {
    aRoot = aRoot->GetParentElement();
  }

  AutoRestore<bool> guard(mIsInPreTraverse);
  mIsInPreTraverse = true;

  // We need to force flush all throttled animations if we also have
  // non-animation restyles (since we'll want the up-to-date animation style
  // when we go to process them so we can trigger transitions correctly), and
  // if we are currently flushing all throttled animation restyles.
  bool flushThrottledRestyles =
      (aRoot && aRoot->HasDirtyDescendantsForServo()) ||
      (aFlags & ServoTraversalFlags::FlushThrottledAnimations);

  using ElementsToRestyleIterType =
      nsTHashMap<PseudoElementHashEntry, bool>::ConstIterator;
  auto getNeededRestyleTarget =
      [&](const ElementsToRestyleIterType& aIter) -> NonOwningAnimationTarget {
    NonOwningAnimationTarget returnTarget;

    // If aIter.Data() is false, the element only requested a throttled
    // (skippable) restyle, so we can skip it if flushThrottledRestyles is not
    // true.
    if (!flushThrottledRestyles && !aIter.Data()) {
      return returnTarget;
    }

    const NonOwningAnimationTarget& target = aIter.Key();

    // Skip elements in documents without a pres shell. Normally we filter out
    // such elements in RequestRestyle but it can happen that, after adding
    // them to mElementsToRestyle, they are transferred to a different document.
    //
    // We will drop them from mElementsToRestyle at the end of the next full
    // document restyle (at the end of this function) but for consistency with
    // how we treat such elements in RequestRestyle, we just ignore them here.
    if (!nsContentUtils::GetPresShellForContent(target.mElement)) {
      return returnTarget;
    }

    // Ignore restyles that aren't in the flattened tree subtree rooted at
    // aRoot.
    if (aRoot && !nsContentUtils::ContentIsFlattenedTreeDescendantOfForStyle(
                     target.mElement, aRoot)) {
      return returnTarget;
    }

    returnTarget = target;
    return returnTarget;
  };

  bool foundElementsNeedingRestyle = false;

  nsTArray<NonOwningAnimationTarget> elementsWithCascadeUpdates;
  for (size_t i = 0; i < kCascadeLevelCount; ++i) {
    CascadeLevel cascadeLevel = CascadeLevel(i);
    auto& elementSet = mElementsToRestyle[cascadeLevel];
    for (auto iter = elementSet.ConstIter(); !iter.Done(); iter.Next()) {
      const NonOwningAnimationTarget& target = getNeededRestyleTarget(iter);
      if (!target.mElement) {
        continue;
      }

      EffectSet* effects = EffectSet::Get(target);
      if (!effects || !effects->CascadeNeedsUpdate()) {
        continue;
      }

      elementsWithCascadeUpdates.AppendElement(target);
    }
  }

  for (const NonOwningAnimationTarget& target : elementsWithCascadeUpdates) {
    MaybeUpdateCascadeResults(target.mElement, target.mPseudoRequest);
  }
  elementsWithCascadeUpdates.Clear();

  for (size_t i = 0; i < kCascadeLevelCount; ++i) {
    CascadeLevel cascadeLevel = CascadeLevel(i);
    auto& elementSet = mElementsToRestyle[cascadeLevel];
    for (auto iter = elementSet.Iter(); !iter.Done(); iter.Next()) {
      const NonOwningAnimationTarget& target = getNeededRestyleTarget(iter);
      if (!target.mElement) {
        continue;
      }

      if (target.mElement->GetComposedDoc() != mPresContext->Document()) {
        iter.Remove();
        continue;
      }

      // We need to post restyle hints even if the target is not in EffectSet to
      // ensure the final restyling for removed animations.
      // We can't call PostRestyleEvent directly here since we are still in the
      // middle of the servo traversal.
      mPresContext->RestyleManager()->PostRestyleEventForAnimations(
          target.mElement, target.mPseudoRequest,
          cascadeLevel == CascadeLevel::Transitions
              ? RestyleHint::RESTYLE_CSS_TRANSITIONS
              : RestyleHint::RESTYLE_CSS_ANIMATIONS);

      foundElementsNeedingRestyle = true;

      auto* effects = EffectSet::Get(target);
      if (!effects) {
        // Drop EffectSets that have been destroyed.
        iter.Remove();
        continue;
      }

      for (KeyframeEffect* effect : *effects) {
        effect->GetAnimation()->WillComposeStyle();
      }

      // Remove the element from the list of elements to restyle since we are
      // about to restyle it.
      iter.Remove();
    }

    // If this is a full document restyle, then unconditionally clear
    // elementSet in case there are any elements that didn't match above
    // because they were moved to a document without a pres shell after
    // posting an animation restyle.
    if (!aRoot && flushThrottledRestyles) {
      elementSet.Clear();
    }
  }

  return foundElementsNeedingRestyle;
}

void EffectCompositor::NoteElementForReducing(
    const NonOwningAnimationTarget& aTarget) {
  Unused << mElementsToReduce.put(
      OwningAnimationTarget{aTarget.mElement, aTarget.mPseudoRequest});
}

static void ReduceEffectSet(EffectSet& aEffectSet) {
  // Get a list of effects sorted by composite order.
  nsTArray<KeyframeEffect*> sortedEffectList(aEffectSet.Count());
  for (KeyframeEffect* effect : aEffectSet) {
    sortedEffectList.AppendElement(effect);
  }
  sortedEffectList.Sort(EffectCompositeOrderComparator());

  AnimatedPropertyIDSet setProperties;

  // Iterate in reverse
  for (auto iter = sortedEffectList.rbegin(); iter != sortedEffectList.rend();
       ++iter) {
    MOZ_ASSERT(*iter && (*iter)->GetAnimation(),
               "Effect in an EffectSet should have an animation");
    KeyframeEffect& effect = **iter;
    Animation& animation = *effect.GetAnimation();
    if (animation.IsRemovable() &&
        effect.GetPropertySet().IsSubsetOf(setProperties)) {
      animation.Remove();
    } else if (animation.IsReplaceable()) {
      setProperties.AddProperties(effect.GetPropertySet());
    }
  }
}

void EffectCompositor::ReduceAnimations() {
  for (auto iter = mElementsToReduce.iter(); !iter.done(); iter.next()) {
    auto* effectSet = EffectSet::Get(iter.get());
    if (effectSet) {
      ReduceEffectSet(*effectSet);
    }
  }

  mElementsToReduce.clear();
}

}  // namespace mozilla
