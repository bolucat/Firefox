/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ViewTransition.h"

#include "Units.h"
#include "WindowRenderer.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/EffectSet.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/WritingModes.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/ViewTransitionBinding.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsCanvasFrame.h"
#include "nsDisplayList.h"
#include "nsFrameState.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsViewManager.h"

namespace mozilla::dom {

LazyLogModule gViewTransitionsLog("ViewTransitions");

NS_DECLARE_FRAME_PROPERTY_RELEASABLE(ViewTransitionCaptureName, nsAtom)

static void SetCaptured(nsIFrame* aFrame, bool aCaptured,
                        nsAtom* aNameIfCaptured) {
  aFrame->AddOrRemoveStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION, aCaptured);
  if (aCaptured) {
    aFrame->AddProperty(ViewTransitionCaptureName(),
                        do_AddRef(aNameIfCaptured).take());
  } else {
    aFrame->RemoveProperty(ViewTransitionCaptureName());
  }
  aFrame->InvalidateFrameSubtree();
  if (aFrame->Style()->IsRootElementStyle()) {
    aFrame->PresShell()->GetRootFrame()->InvalidateFrameSubtree();
  }
}

// Set capture's old transform to a <transform-function> that would map
// element's border box from the snapshot containing block origin to its
// current visual position.
//
// Since we're using viewport as the snapshot origin, we can use
// GetBoundingClientRect() effectively...
//
// TODO(emilio): This might need revision.
static CSSToCSSMatrix4x4Flagged EffectiveTransform(nsIFrame* aFrame) {
  if (aFrame->GetSize().IsEmpty() || aFrame->Style()->IsRootElementStyle()) {
    return {};
  }

  auto matrix = CSSToCSSMatrix4x4Flagged::FromUnknownMatrix(
      nsLayoutUtils::GetTransformToAncestor(
          RelativeTo{aFrame},
          RelativeTo{nsLayoutUtils::GetContainingBlockForClientRect(aFrame)},
          nsIFrame::IN_CSS_UNITS, nullptr));

  // Compensate for the default transform-origin of 50% 50% using border box
  // dimensions.
  auto borderBoxRect = CSSRect::FromAppUnits(aFrame->GetRect());
  matrix.ChangeBasis(-borderBoxRect.Width() / 2, -borderBoxRect.Height() / 2,
                     0.0f);
  return matrix;
}

enum class CapturedSizeType { BorderBox, InkOverflowBox };

static inline nsSize CapturedSize(const nsIFrame* aFrame,
                                  const nsSize& aSnapshotContainingBlockSize,
                                  CapturedSizeType aType) {
  if (aFrame->Style()->IsRootElementStyle()) {
    return aSnapshotContainingBlockSize;
  }

  if (aType == CapturedSizeType::BorderBox) {
    return aFrame->GetRectRelativeToSelf().Size();
  }

  return aFrame->InkOverflowRectRelativeToSelf().Size();
}

// TODO(emilio): Bug 1970954. These aren't quite correct, per spec we're
// supposed to only honor names and classes coming from the document, but that's
// quite some magic, and it's getting actively discussed, see:
// https://github.com/w3c/csswg-drafts/issues/10808 and related
// https://drafts.csswg.org/css-view-transitions-1/#document-scoped-view-transition-name
// https://drafts.csswg.org/css-view-transitions-2/#additions-to-vt-name
template <typename IDGenerator>
static already_AddRefed<nsAtom> DocumentScopedTransitionNameForWithGenerator(
    nsIFrame* aFrame, IDGenerator&& aFunc) {
  // 1. Let computed be the computed value of view-transition-name.
  const auto& computed = aFrame->StyleUIReset()->mViewTransitionName;

  // 2. If computed is none, return null.
  if (computed.IsNone()) {
    return nullptr;
  }

  // 3. If computed is a <custom-ident>, return computed.
  if (computed.IsIdent()) {
    return RefPtr<nsAtom>{computed.AsIdent().AsAtom()}.forget();
  }

  // 4. Assert: computed is auto or match-element.
  // TODO: Bug 1918218. Implement auto or others, depending on the spec issue.
  // https://github.com/w3c/csswg-drafts/issues/12091
  MOZ_ASSERT(computed.IsMatchElement());

  // 5. If computed is auto, element has an associated id, and computed is
  // associated with the same root as element’s root, then return a unique
  // string starting with "-ua-". Two elements with the same id must return the
  // same string, regardless of their node document.
  // TODO: Bug 1918218. auto keyword may be changed. See the spec issue
  // mentioned above..

  // 6. Return a unique string starting with "-ua-". The string should remain
  // consistent and unique for this element and Document, at least for the
  // lifetime of element’s node document’s active view transition.
  nsIContent* content = aFrame->GetContent();
  if (MOZ_UNLIKELY(!content || !content->IsElement())) {
    return nullptr;
  }

  // We generate the unique identifier (not id attribute) of the element lazily.
  // If failed, we just return nullptr.
  Maybe<uint64_t> id = aFunc(content->AsElement());
  if (!id) {
    return nullptr;
  }

  // FIXME: We may have to revist here when working on cross document because we
  // may have to return a warning and nullptr, per the comment in the design
  // review.
  // https://github.com/w3ctag/design-reviews/issues/1001#issuecomment-2750966335
  nsCString name;
  // Note: Add the "view-transition-name" in the prefix so we know this is for
  // auto-generated view-transition-name.
  name.AppendLiteral("-ua-view-transition-name-");
  name.AppendInt(*id);
  return NS_Atomize(name);
}

static StyleViewTransitionClass DocumentScopedClassListFor(
    const nsIFrame* aFrame) {
  return aFrame->StyleUIReset()->mViewTransitionClass;
}

static constexpr wr::ImageKey kNoKey{{0}, 0};
struct OldSnapshotData {
  wr::ImageKey mImageKey = kNoKey;
  // Snapshot size should match the captured element’s InkOverflowBox size.
  nsSize mSize;
  RefPtr<layers::RenderRootStateManager> mManager;
  bool mUsed = false;

  OldSnapshotData() = default;

  explicit OldSnapshotData(nsIFrame* aFrame,
                           const nsSize& aSnapshotContainingBlockSize)
      : mSize(CapturedSize(aFrame, aSnapshotContainingBlockSize,
                           CapturedSizeType::InkOverflowBox)) {}

  void EnsureKey(layers::RenderRootStateManager* aManager,
                 wr::IpcResourceUpdateQueue& aResources) {
    if (mImageKey != kNoKey) {
      MOZ_ASSERT(mManager == aManager, "Stale manager?");
      return;
    }
    mManager = aManager;
    mImageKey = aManager->WrBridge()->GetNextImageKey();
    aResources.AddSnapshotImage(wr::SnapshotImageKey{mImageKey});
  }

  ~OldSnapshotData() {
    if (mManager) {
      wr::SnapshotImageKey key = {mImageKey};
      if (mUsed) {
        mManager->AddSnapshotImageKeyForDiscard(key);
      } else {
        mManager->AddUnusedSnapshotImageKeyForDiscard(key);
      }
    }
  }
};

struct CapturedElementOldState {
  OldSnapshotData mSnapshot;
  // Whether we tried to capture an image. Note we might fail to get a
  // snapshot, so this might not be the same as !!mImage.
  bool mTriedImage = false;

  nsSize mBorderBoxSize;
  nsPoint mInkOverflowOffset;
  CSSToCSSMatrix4x4Flagged mTransform;
  StyleWritingModeProperty mWritingMode =
      StyleWritingModeProperty::HorizontalTb;
  StyleDirection mDirection = StyleDirection::Ltr;
  StyleTextOrientation mTextOrientation = StyleTextOrientation::Mixed;
  StyleBlend mMixBlendMode = StyleBlend::Normal;
  StyleOwnedSlice<StyleFilter> mBackdropFilters;
  // Note: it's unfortunate we cannot just store the bits here. color-scheme
  // property uses idents for serialization. If the idents and bits are not
  // aligned, we assert it in ToCSS.
  StyleColorScheme mColorScheme;

  CapturedElementOldState(nsIFrame* aFrame,
                          const nsSize& aSnapshotContainingBlockSize)
      : mSnapshot(aFrame, aSnapshotContainingBlockSize),
        mTriedImage(true),
        mBorderBoxSize(CapturedSize(aFrame, aSnapshotContainingBlockSize,
                                    CapturedSizeType::BorderBox)),
        mInkOverflowOffset(aFrame->InkOverflowRectRelativeToSelf().TopLeft()),
        mTransform(EffectiveTransform(aFrame)),
        mWritingMode(aFrame->StyleVisibility()->mWritingMode),
        mDirection(aFrame->StyleVisibility()->mDirection),
        mTextOrientation(aFrame->StyleVisibility()->mTextOrientation),
        mMixBlendMode(aFrame->StyleEffects()->mMixBlendMode),
        mBackdropFilters(aFrame->StyleEffects()->mBackdropFilters),
        mColorScheme(aFrame->StyleUI()->mColorScheme) {}

  CapturedElementOldState() = default;
};

// https://drafts.csswg.org/css-view-transitions/#captured-element
struct ViewTransition::CapturedElement {
  CapturedElementOldState mOldState;
  RefPtr<Element> mNewElement;
  wr::SnapshotImageKey mNewSnapshotKey{kNoKey};
  // Snapshot size should match the captured element’s InkOverflowBox size.
  nsSize mNewSnapshotSize;
  nsSize mNewBorderBoxSize;
  nsPoint mNewInkOverflowOffset;

  CapturedElement() = default;

  CapturedElement(nsIFrame* aFrame, const nsSize& aSnapshotContainingBlockSize,
                  StyleViewTransitionClass&& aClassList)
      : mOldState(aFrame, aSnapshotContainingBlockSize),
        mClassList(std::move(aClassList)) {}

  // https://drafts.csswg.org/css-view-transitions-1/#captured-element-style-definitions
  nsTArray<Keyframe> mGroupKeyframes;
  // The group animation-name rule and group styles rule, merged into one.
  RefPtr<StyleLockedDeclarationBlock> mGroupRule;
  // The image pair isolation rule.
  RefPtr<StyleLockedDeclarationBlock> mImagePairRule;
  // The rules for ::view-transition-old(<name>).
  RefPtr<StyleLockedDeclarationBlock> mOldRule;
  // The rules for ::view-transition-new(<name>).
  RefPtr<StyleLockedDeclarationBlock> mNewRule;

  // The view-transition-class associated with this captured element.
  // https://drafts.csswg.org/css-view-transitions-2/#captured-element-class-list
  StyleViewTransitionClass mClassList;

  void CaptureClassList(StyleViewTransitionClass&& aClassList) {
    mClassList = std::move(aClassList);
  }

  ~CapturedElement() {
    if (wr::AsImageKey(mNewSnapshotKey) != kNoKey) {
      MOZ_ASSERT(mOldState.mSnapshot.mManager);
      mOldState.mSnapshot.mManager->AddSnapshotImageKeyForDiscard(
          mNewSnapshotKey);
    }
  }
};

static inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCb,
    const ViewTransition::CapturedElement& aField, const char* aName,
    uint32_t aFlags = 0) {
  ImplCycleCollectionTraverse(aCb, aField.mNewElement, aName, aFlags);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ViewTransition, mDocument,
                                      mUpdateCallback,
                                      mUpdateCallbackDonePromise, mReadyPromise,
                                      mFinishedPromise, mNamedElements,
                                      mSnapshotContainingBlock)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ViewTransition)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ViewTransition)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ViewTransition)

ViewTransition::ViewTransition(Document& aDoc,
                               ViewTransitionUpdateCallback* aCb)
    : mDocument(&aDoc), mUpdateCallback(aCb) {}

ViewTransition::~ViewTransition() { ClearTimeoutTimer(); }

Element* ViewTransition::GetViewTransitionTreeRoot() const {
  return mSnapshotContainingBlock
             ? mSnapshotContainingBlock->GetFirstElementChild()
             : nullptr;
}

Maybe<nsSize> ViewTransition::GetOldInkOverflowBoxSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mOldState.mSnapshot.mSize);
}

Maybe<nsSize> ViewTransition::GetNewInkOverflowBoxSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mNewSnapshotSize);
}

Maybe<nsSize> ViewTransition::GetOldBorderBoxSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mOldState.mBorderBoxSize);
}

Maybe<nsSize> ViewTransition::GetNewBorderBoxSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mNewBorderBoxSize);
}

Maybe<nsPoint> ViewTransition::GetOldInkOverflowOffset(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mOldState.mInkOverflowOffset);
}

Maybe<nsPoint> ViewTransition::GetNewInkOverflowOffset(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mNewInkOverflowOffset);
}

const wr::ImageKey* ViewTransition::GetOrCreateOldImageKey(
    nsAtom* aName, layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  el->mOldState.mSnapshot.EnsureKey(aManager, aResources);
  return &el->mOldState.mSnapshot.mImageKey;
}

const wr::ImageKey* ViewTransition::ReadOldImageKey(
    nsAtom* aName, layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }

  el->mOldState.mSnapshot.mUsed = true;
  return &el->mOldState.mSnapshot.mImageKey;
}

const wr::ImageKey* ViewTransition::GetNewImageKey(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  return &el->mNewSnapshotKey._0;
}

const wr::ImageKey* ViewTransition::GetImageKeyForCapturedFrame(
    nsIFrame* aFrame, layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION));

  nsAtom* name = aFrame->GetProperty(ViewTransitionCaptureName());
  if (NS_WARN_IF(!name)) {
    return nullptr;
  }
  const bool isOld = mPhase < Phase::Animating;

  VT_LOG("ViewTransition::GetImageKeyForCapturedFrame(%s, old=%d)\n",
         nsAtomCString(name).get(), isOld);

  if (isOld) {
    const auto* key = GetOrCreateOldImageKey(name, aManager, aResources);
    VT_LOG(" > old image is %s", key ? ToString(*key).c_str() : "null");
    return key;
  }
  auto* el = mNamedElements.Get(name);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  if (NS_WARN_IF(el->mNewElement != aFrame->GetContent())) {
    return nullptr;
  }
  if (wr::AsImageKey(el->mNewSnapshotKey) == kNoKey) {
    MOZ_ASSERT(!el->mOldState.mSnapshot.mManager ||
                   el->mOldState.mSnapshot.mManager == aManager,
               "Stale manager?");
    el->mNewSnapshotKey = {aManager->WrBridge()->GetNextImageKey()};
    el->mOldState.mSnapshot.mManager = aManager;
    aResources.AddSnapshotImage(el->mNewSnapshotKey);
  }
  VT_LOG(" > new image is %s", ToString(el->mNewSnapshotKey._0).c_str());
  return &el->mNewSnapshotKey._0;
}

nsIGlobalObject* ViewTransition::GetParentObject() const {
  return mDocument ? mDocument->GetParentObject() : nullptr;
}

Promise* ViewTransition::GetUpdateCallbackDone(ErrorResult& aRv) {
  if (!mUpdateCallbackDonePromise) {
    mUpdateCallbackDonePromise = Promise::Create(GetParentObject(), aRv);
  }
  return mUpdateCallbackDonePromise;
}

Promise* ViewTransition::GetReady(ErrorResult& aRv) {
  if (!mReadyPromise) {
    mReadyPromise = Promise::Create(GetParentObject(), aRv);
  }
  return mReadyPromise;
}

Promise* ViewTransition::GetFinished(ErrorResult& aRv) {
  if (!mFinishedPromise) {
    mFinishedPromise = Promise::Create(GetParentObject(), aRv);
  }
  return mFinishedPromise;
}

// This performs the step 5 in setup view transition.
// https://drafts.csswg.org/css-view-transitions-1/#setup-view-transition
void ViewTransition::MaybeScheduleUpdateCallback() {
  AUTO_PROFILER_FLOW_MARKER("ViewTransition::MaybeScheduleUpdateCallback",
                            LAYOUT, Flow::FromPointer(this));
  // 1. If transition’s phase is "done", then abort these steps.
  // Note: This happens if transition was skipped before this point.
  if (mPhase == Phase::Done) {
    return;
  }

  RefPtr doc = mDocument;

  // 2. Schedule the update callback for transition.
  doc->ScheduleViewTransitionUpdateCallback(this);

  // 3. Flush the update callback queue.
  doc->FlushViewTransitionUpdateCallbackQueue();
}

// https://drafts.csswg.org/css-view-transitions-1/#call-the-update-callback
void ViewTransition::CallUpdateCallback(ErrorResult& aRv) {
  MOZ_ASSERT(mDocument);
  // Step 1:  Assert: transition's phase is "done", or before
  // "update-callback-called".
  MOZ_ASSERT(mPhase == Phase::Done ||
             UnderlyingValue(mPhase) <
                 UnderlyingValue(Phase::UpdateCallbackCalled));
  VT_LOG("ViewTransition::CallUpdateCallback(%d)\n", int(mPhase));
  AUTO_PROFILER_FLOW_MARKER("ViewTransition::CallUpdateCallback", LAYOUT,
                            Flow::FromPointer(this));

  // Step 5: If transition's phase is not "done", then set transition's phase
  // to "update-callback-called".
  //
  // NOTE(emilio): This is swapped with the spec because the spec is broken,
  // see https://github.com/w3c/csswg-drafts/issues/10822
  if (mPhase != Phase::Done) {
    mPhase = Phase::UpdateCallbackCalled;
  }

  // Step 2: Let callbackPromise be null.
  RefPtr<Promise> callbackPromise;
  if (!mUpdateCallback) {
    // Step 3: If transition's update callback is null, then set callbackPromise
    // to a promise resolved with undefined, in transition’s relevant Realm.
    callbackPromise =
        Promise::CreateResolvedWithUndefined(GetParentObject(), aRv);
  } else {
    // Step 4: Otherwise set callbackPromise to the result of invoking
    // transition’s update callback. MOZ_KnownLive because the callback can only
    // go away when we get CCd.
    callbackPromise = MOZ_KnownLive(mUpdateCallback)->Call(aRv);
  }
  if (aRv.Failed()) {
    // TODO(emilio): Do we need extra error handling here?
    return;
  }
  MOZ_ASSERT(callbackPromise);
  // Step 8: React to callbackPromise with fulfillSteps and rejectSteps.
  callbackPromise->AddCallbacksWithCycleCollectedArgs(
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult& aRv,
         ViewTransition* aVt) {
        AUTO_PROFILER_FLOW_MARKER("ViewTransition::UpdateCallbackResolve",
                                  LAYOUT, Flow::FromPointer(aVt));
        // We clear the timeout when we are ready to activate. Otherwise, any
        // animations with the duration longer than
        // StaticPrefs::dom_viewTransitions_timeout_ms() will be interrupted.
        // FIXME: We may need a better solution to tweak the timeout, e.g. reset
        // the timeout to a longer value or so on.
        aVt->ClearTimeoutTimer();

        // Step 6: Let fulfillSteps be to following steps:
        if (Promise* ucd = aVt->GetUpdateCallbackDone(aRv)) {
          // 6.1: Resolve transition's update callback done promise with
          // undefined.
          ucd->MaybeResolveWithUndefined();
        }
        // Unlike other timings, this is not guaranteed to happen with clean
        // layout, and Activate() needs to look at the frame tree to capture the
        // new state, so we need to flush frames. Do it here so that we deal
        // with other potential script execution skipping the transition or
        // what not in a consistent way.
        aVt->mDocument->FlushPendingNotifications(FlushType::Layout);
        if (aVt->mPhase == Phase::Done) {
          // "Skip a transition" step 8. We need to resolve "finished" after
          // update-callback-done.
          if (Promise* finished = aVt->GetFinished(aRv)) {
            finished->MaybeResolveWithUndefined();
          }
        }
        aVt->Activate();
      },
      [](JSContext*, JS::Handle<JS::Value> aReason, ErrorResult& aRv,
         ViewTransition* aVt) {
        AUTO_PROFILER_FLOW_MARKER("ViewTransition::UpdateCallbackReject",
                                  LAYOUT, Flow::FromPointer(aVt));
        // Clear the timeout because we are ready to skip the view transitions.
        aVt->ClearTimeoutTimer();

        // Step 7: Let rejectSteps be to following steps:
        if (Promise* ucd = aVt->GetUpdateCallbackDone(aRv)) {
          // 7.1: Reject transition's update callback done promise with reason.
          ucd->MaybeReject(aReason);
        }

        // 7.2: If transition's phase is "done", then return.
        if (aVt->mPhase == Phase::Done) {
          // "Skip a transition" step 8. We need to resolve "finished" after
          // update-callback-done.
          if (Promise* finished = aVt->GetFinished(aRv)) {
            finished->MaybeReject(aReason);
          }
          return;
        }

        // 7.3: Mark as handled transition's ready promise.
        if (Promise* ready = aVt->GetReady(aRv)) {
          MOZ_ALWAYS_TRUE(ready->SetAnyPromiseIsHandled());
        }
        aVt->SkipTransition(SkipTransitionReason::UpdateCallbackRejected,
                            aReason);
      },
      RefPtr(this));

  // Step 9: To skip a transition after a timeout, the user agent may perform
  // the following steps in parallel:
  MOZ_ASSERT(!mTimeoutTimer);
  ClearTimeoutTimer();  // Be safe just in case.
  mTimeoutTimer = NS_NewTimer();
  mTimeoutTimer->InitWithNamedFuncCallback(
      TimeoutCallback, this, StaticPrefs::dom_viewTransitions_timeout_ms(),
      nsITimer::TYPE_ONE_SHOT, "ViewTransition::TimeoutCallback");
}

void ViewTransition::ClearTimeoutTimer() {
  if (mTimeoutTimer) {
    mTimeoutTimer->Cancel();
    mTimeoutTimer = nullptr;
  }
}

void ViewTransition::TimeoutCallback(nsITimer* aTimer, void* aClosure) {
  RefPtr vt = static_cast<ViewTransition*>(aClosure);
  MOZ_DIAGNOSTIC_ASSERT(aTimer == vt->mTimeoutTimer);
  vt->Timeout();
}

void ViewTransition::Timeout() {
  ClearTimeoutTimer();
  if (mPhase != Phase::Done && mDocument) {
    SkipTransition(SkipTransitionReason::Timeout);
  }
}

static already_AddRefed<Element> MakePseudo(Document& aDoc,
                                            PseudoStyleType aType,
                                            nsAtom* aName) {
  RefPtr<Element> el = aDoc.CreateHTMLElement(nsGkAtoms::div);
  if (aType == PseudoStyleType::mozSnapshotContainingBlock) {
    el->SetIsNativeAnonymousRoot();
  }
  el->SetPseudoElementType(aType);
  if (aName) {
    el->SetAttr(nsGkAtoms::name, nsDependentAtomString(aName), IgnoreErrors());
  }
  // This is not needed, but useful for debugging.
  el->SetAttr(nsGkAtoms::type,
              nsDependentAtomString(nsCSSPseudoElements::GetPseudoAtom(aType)),
              IgnoreErrors());
  return el.forget();
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const nsACString& aValue) {
  return Servo_DeclarationBlock_SetPropertyById(
      aDecls, aProp, &aValue,
      /* is_important = */ false, aDoc->DefaultStyleAttrURLData(),
      StyleParsingMode::DEFAULT, eCompatibility_FullStandards,
      aDoc->CSSLoader(), StyleCssRuleType::Style, {});
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp, float aLength, nsCSSUnit aUnit) {
  return Servo_DeclarationBlock_SetLengthValue(aDecls, aProp, aLength, aUnit);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp, const CSSToCSSMatrix4x4Flagged& aM) {
  MOZ_ASSERT(aProp == eCSSProperty_transform);
  AutoTArray<StyleTransformOperation, 1> ops;
  ops.AppendElement(
      StyleTransformOperation::Matrix3D(StyleGenericMatrix3D<StyleNumber>{
          aM._11, aM._12, aM._13, aM._14, aM._21, aM._22, aM._23, aM._24,
          aM._31, aM._32, aM._33, aM._34, aM._41, aM._42, aM._43, aM._44}));
  return Servo_DeclarationBlock_SetTransform(aDecls, aProp, &ops);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleWritingModeProperty aWM) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp, (int32_t)aWM);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleDirection aDirection) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp,
                                                (int32_t)aDirection);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp,
                    const StyleTextOrientation aTextOrientation) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp,
                                                (int32_t)aTextOrientation);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleBlend aBlend) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp, (int32_t)aBlend);
}

static bool SetProp(
    StyleLockedDeclarationBlock* aDecls, Document*, nsCSSPropertyID aProp,
    const StyleOwnedSlice<mozilla::StyleFilter>& aBackdropFilters) {
  return Servo_DeclarationBlock_SetBackdropFilter(aDecls, aProp,
                                                  &aBackdropFilters);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp,
                    const StyleColorScheme& aColorScheme) {
  return Servo_DeclarationBlock_SetColorScheme(aDecls, aProp, &aColorScheme);
}

static StyleLockedDeclarationBlock* EnsureRule(
    RefPtr<StyleLockedDeclarationBlock>& aRule) {
  if (!aRule) {
    aRule = Servo_DeclarationBlock_CreateEmpty().Consume();
  }
  return aRule.get();
}

static nsTArray<Keyframe> BuildGroupKeyframes(
    Document* aDoc, const CSSToCSSMatrix4x4Flagged& aTransform,
    const nsSize& aSize, const StyleOwnedSlice<StyleFilter>& aBackdropFilters) {
  nsTArray<Keyframe> result;
  auto& firstKeyframe = *result.AppendElement();
  firstKeyframe.mOffset = Some(0.0);
  PropertyValuePair transform{
      AnimatedPropertyID(eCSSProperty_transform),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  SetProp(transform.mServoDeclarationBlock, aDoc, eCSSProperty_transform,
          aTransform);
  PropertyValuePair width{
      AnimatedPropertyID(eCSSProperty_width),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  CSSSize cssSize = CSSSize::FromAppUnits(aSize);
  SetProp(width.mServoDeclarationBlock, aDoc, eCSSProperty_width, cssSize.width,
          eCSSUnit_Pixel);
  PropertyValuePair height{
      AnimatedPropertyID(eCSSProperty_height),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  SetProp(height.mServoDeclarationBlock, aDoc, eCSSProperty_height,
          cssSize.height, eCSSUnit_Pixel);
  PropertyValuePair backdropFilters{
      AnimatedPropertyID(eCSSProperty_backdrop_filter),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  SetProp(backdropFilters.mServoDeclarationBlock, aDoc,
          eCSSProperty_backdrop_filter, aBackdropFilters);
  firstKeyframe.mPropertyValues.AppendElement(std::move(transform));
  firstKeyframe.mPropertyValues.AppendElement(std::move(width));
  firstKeyframe.mPropertyValues.AppendElement(std::move(height));
  firstKeyframe.mPropertyValues.AppendElement(std::move(backdropFilters));

  auto& lastKeyframe = *result.AppendElement();
  lastKeyframe.mOffset = Some(1.0);
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_transform)});
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_width)});
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_height)});
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_backdrop_filter)});
  return result;
}

bool ViewTransition::GetGroupKeyframes(
    nsAtom* aAnimationName, const StyleComputedTimingFunction& aTimingFunction,
    nsTArray<Keyframe>& aResult) {
  MOZ_ASSERT(StringBeginsWith(nsDependentAtomString(aAnimationName),
                              kGroupAnimPrefix));
  RefPtr<nsAtom> transitionName = NS_Atomize(Substring(
      nsDependentAtomString(aAnimationName), kGroupAnimPrefix.Length()));
  auto* el = mNamedElements.Get(transitionName);
  if (NS_WARN_IF(!el) || NS_WARN_IF(el->mGroupKeyframes.IsEmpty())) {
    return false;
  }
  aResult = el->mGroupKeyframes.Clone();
  // We assign the timing function always to make sure we don't use the default
  // linear timing function.
  MOZ_ASSERT(aResult.Length() == 2);
  aResult[0].mTimingFunction = Some(aTimingFunction);
  aResult[1].mTimingFunction = Some(aTimingFunction);
  return true;
}

// Matches the class list in the captured element.
// https://drafts.csswg.org/css-view-transitions-2/#pseudo-element-class-additions
bool ViewTransition::MatchClassList(
    nsAtom* aTransitionName,
    const nsTArray<StyleAtom>& aPtNameAndClassSelector) const {
  MOZ_ASSERT(aPtNameAndClassSelector.Length() > 1);

  const auto* el = mNamedElements.Get(aTransitionName);
  MOZ_ASSERT(el);
  const auto& classList = el->mClassList._0.AsSpan();
  auto hasClass = [&classList](nsAtom* aClass) {
    // LInear search. The css class list shouldn't be very large in most cases.
    for (const auto& ident : classList) {
      if (ident.AsAtom() == aClass) {
        return true;
      }
    }
    return false;
  };

  // A named view transition pseudo-element selector which has one or more
  // <custom-ident> values in its <pt-class-selector> would only match an
  // element if the class list value in named elements for the pseudo-element’s
  // view-transition-name contains all of those values.
  // i.e. |aPtNameAndClassSelector| should be a subset of |mClassList|.
  for (const auto& atom : Span(aPtNameAndClassSelector).From(1)) {
    if (!hasClass(atom.AsAtom())) {
      return false;
    }
  }
  return true;
}

// In general, we are trying to generate the following pseudo-elements tree:
// ::-moz-snapshot-containing-block
// └─ ::view-transition
//    ├─ ::view-transition-group(name)
//    │  └─ ::view-transition-image-pair(name)
//    │     ├─ ::view-transition-old(name)
//    │     └─ ::view-transition-new(name)
//    └─ ...other groups...
//
// ::-moz-snapshot-containing-block is the top-layer of the tree. It is the
// wrapper of the view transition pseudo-elements tree for the snapshot
// containing block concept. And it is the child of the document element.
// https://drafts.csswg.org/css-view-transitions-1/#setup-transition-pseudo-elements
void ViewTransition::SetupTransitionPseudoElements() {
  MOZ_ASSERT(!mSnapshotContainingBlock);

  nsAutoScriptBlocker scriptBlocker;

  RefPtr docElement = mDocument->GetRootElement();
  if (!docElement) {
    return;
  }

  // We don't need to notify while constructing the tree.
  constexpr bool kNotify = false;

  // Step 1 is a declaration.

  // Step 2: Set document's show view transition tree to true.
  // (we lazily create this pseudo-element so we don't need the flag for now at
  // least).
  // Note: Use mSnapshotContainingBlock to wrap the pseudo-element tree.
  mSnapshotContainingBlock = MakePseudo(
      *mDocument, PseudoStyleType::mozSnapshotContainingBlock, nullptr);
  RefPtr<Element> root =
      MakePseudo(*mDocument, PseudoStyleType::viewTransition, nullptr);
  mSnapshotContainingBlock->AppendChildTo(root, kNotify, IgnoreErrors());
#ifdef DEBUG
  // View transition pseudos don't care about frame tree ordering, so can be
  // restyled just fine.
  mSnapshotContainingBlock->SetProperty(nsGkAtoms::restylableAnonymousNode,
                                        reinterpret_cast<void*>(true));
#endif

  MOZ_ASSERT(mNames.Length() == mNamedElements.Count());
  // Step 3: For each transitionName -> capturedElement of transition’s named
  // elements:
  for (nsAtom* transitionName : mNames) {
    CapturedElement& capturedElement = *mNamedElements.Get(transitionName);
    // Let group be a new ::view-transition-group(), with its view transition
    // name set to transitionName.
    RefPtr<Element> group = MakePseudo(
        *mDocument, PseudoStyleType::viewTransitionGroup, transitionName);
    // Append group to transition’s transition root pseudo-element.
    root->AppendChildTo(group, kNotify, IgnoreErrors());
    // Let imagePair be a new ::view-transition-image-pair(), with its view
    // transition name set to transitionName.
    RefPtr<Element> imagePair = MakePseudo(
        *mDocument, PseudoStyleType::viewTransitionImagePair, transitionName);
    // Append imagePair to group.
    group->AppendChildTo(imagePair, kNotify, IgnoreErrors());
    // If capturedElement's old image is not null, then:
    if (capturedElement.mOldState.mTriedImage) {
      // Let old be a new ::view-transition-old(), with its view transition
      // name set to transitionName, displaying capturedElement's old image as
      // its replaced content.
      RefPtr<Element> old = MakePseudo(
          *mDocument, PseudoStyleType::viewTransitionOld, transitionName);
      // Append old to imagePair.
      imagePair->AppendChildTo(old, kNotify, IgnoreErrors());
    } else {
      // Moved around for simplicity. If capturedElement's old image is null,
      // then: Assert: capturedElement's new element is not null.
      MOZ_ASSERT(capturedElement.mNewElement);
      // Set capturedElement's image animation name rule to a new ...
      auto* rule = EnsureRule(capturedElement.mNewRule);
      SetProp(rule, mDocument, eCSSProperty_animation_name,
              "-ua-view-transition-fade-in"_ns);
    }
    // If capturedElement's new element is not null, then:
    if (capturedElement.mNewElement) {
      // Let new be a new ::view-transition-new(), with its view transition
      // name set to transitionName.
      RefPtr<Element> new_ = MakePseudo(
          *mDocument, PseudoStyleType::viewTransitionNew, transitionName);
      // Append new to imagePair.
      imagePair->AppendChildTo(new_, kNotify, IgnoreErrors());
    } else {
      // Moved around from the next step for simplicity.
      // Assert: capturedElement's old image is not null.
      // Set capturedElement's image animation name rule to a new CSSStyleRule
      // representing the following CSS, and append it to document’s dynamic
      // view transition style sheet:
      MOZ_ASSERT(capturedElement.mOldState.mTriedImage);
      SetProp(EnsureRule(capturedElement.mOldRule), mDocument,
              eCSSProperty_animation_name, "-ua-view-transition-fade-out"_ns);

      // Moved around from "update pseudo-element styles" because it's a one
      // time operation.
      auto* rule = EnsureRule(capturedElement.mGroupRule);
      auto oldRect =
          CSSPixel::FromAppUnits(capturedElement.mOldState.mBorderBoxSize);
      SetProp(rule, mDocument, eCSSProperty_width, oldRect.width,
              eCSSUnit_Pixel);
      SetProp(rule, mDocument, eCSSProperty_height, oldRect.height,
              eCSSUnit_Pixel);
      SetProp(rule, mDocument, eCSSProperty_transform,
              capturedElement.mOldState.mTransform);
      SetProp(rule, mDocument, eCSSProperty_writing_mode,
              capturedElement.mOldState.mWritingMode);
      SetProp(rule, mDocument, eCSSProperty_direction,
              capturedElement.mOldState.mDirection);
      SetProp(rule, mDocument, eCSSProperty_text_orientation,
              capturedElement.mOldState.mTextOrientation);
      SetProp(rule, mDocument, eCSSProperty_mix_blend_mode,
              capturedElement.mOldState.mMixBlendMode);
      SetProp(rule, mDocument, eCSSProperty_backdrop_filter,
              capturedElement.mOldState.mBackdropFilters);
      SetProp(rule, mDocument, eCSSProperty_color_scheme,
              capturedElement.mOldState.mColorScheme);
    }
    // If both of capturedElement's old image and new element are not null,
    // then:
    if (capturedElement.mOldState.mTriedImage && capturedElement.mNewElement) {
      NS_ConvertUTF16toUTF8 dynamicAnimationName(
          kGroupAnimPrefix + nsDependentAtomString(transitionName));

      capturedElement.mGroupKeyframes =
          BuildGroupKeyframes(mDocument, capturedElement.mOldState.mTransform,
                              capturedElement.mOldState.mBorderBoxSize,
                              capturedElement.mOldState.mBackdropFilters);
      // Set capturedElement's group animation name rule to ...
      SetProp(EnsureRule(capturedElement.mGroupRule), mDocument,
              eCSSProperty_animation_name, dynamicAnimationName);

      // Set capturedElement's image pair isolation rule to ...
      SetProp(EnsureRule(capturedElement.mImagePairRule), mDocument,
              eCSSProperty_isolation, "isolate"_ns);

      // Set capturedElement's image animation name rule to ...
      SetProp(
          EnsureRule(capturedElement.mOldRule), mDocument,
          eCSSProperty_animation_name,
          "-ua-view-transition-fade-out, -ua-mix-blend-mode-plus-lighter"_ns);
      SetProp(
          EnsureRule(capturedElement.mNewRule), mDocument,
          eCSSProperty_animation_name,
          "-ua-view-transition-fade-in, -ua-mix-blend-mode-plus-lighter"_ns);
    }
  }
  BindContext context(*docElement, BindContext::ForNativeAnonymous);
  if (NS_FAILED(mSnapshotContainingBlock->BindToTree(context, *docElement))) {
    mSnapshotContainingBlock->UnbindFromTree();
    mSnapshotContainingBlock = nullptr;
    return;
  }
  if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
    mSnapshotContainingBlock->QueueDevtoolsAnonymousEvent(
        /* aIsRemove = */ false);
  }
  if (PresShell* ps = mDocument->GetPresShell()) {
    ps->ContentAppended(mSnapshotContainingBlock, {});
  }
}

// https://drafts.csswg.org/css-view-transitions-1/#style-transition-pseudo-elements-algorithm
bool ViewTransition::UpdatePseudoElementStyles(bool aNeedsInvalidation) {
  // 1. For each transitionName -> capturedElement of transition's "named
  // elements".
  for (auto& entry : mNamedElements) {
    nsAtom* transitionName = entry.GetKey();
    CapturedElement& capturedElement = *entry.GetData();
    // If capturedElement's new element is null, then:
    // We already did this in SetupTransitionPseudoElements().
    if (!capturedElement.mNewElement) {
      continue;
    }
    // Otherwise.
    // Return failure if any of the following conditions is true:
    //  * capturedElement's new element has a flat tree ancestor that skips its
    //    contents.
    //  * capturedElement's new element is not rendered.
    //  * capturedElement has more than one box fragment.
    nsIFrame* frame = capturedElement.mNewElement->GetPrimaryFrame();
    if (!frame || frame->IsHiddenByContentVisibilityOnAnyAncestor() ||
        frame->GetPrevContinuation() || frame->GetNextContinuation()) {
      return false;
    }
    auto* rule = EnsureRule(capturedElement.mGroupRule);
    // Note: mInitialSnapshotContainingBlockSize should be the same as the
    // current snapshot containing block size because the caller checks it
    // before calling us.
    const auto& newBorderBoxSize =
        CapturedSize(frame, mInitialSnapshotContainingBlockSize,
                     CapturedSizeType::BorderBox);
    auto size = CSSPixel::FromAppUnits(newBorderBoxSize);
    // NOTE(emilio): Intentionally not short-circuiting. Int cast is needed to
    // silence warning.
    bool groupStyleChanged =
        int(SetProp(rule, mDocument, eCSSProperty_width, size.width,
                    eCSSUnit_Pixel)) |
        SetProp(rule, mDocument, eCSSProperty_height, size.height,
                eCSSUnit_Pixel) |
        SetProp(rule, mDocument, eCSSProperty_transform,
                EffectiveTransform(frame)) |
        SetProp(rule, mDocument, eCSSProperty_writing_mode,
                frame->StyleVisibility()->mWritingMode) |
        SetProp(rule, mDocument, eCSSProperty_direction,
                frame->StyleVisibility()->mDirection) |
        SetProp(rule, mDocument, eCSSProperty_text_orientation,
                frame->StyleVisibility()->mTextOrientation) |
        SetProp(rule, mDocument, eCSSProperty_mix_blend_mode,
                frame->StyleEffects()->mMixBlendMode) |
        SetProp(rule, mDocument, eCSSProperty_backdrop_filter,
                frame->StyleEffects()->mBackdropFilters) |
        SetProp(rule, mDocument, eCSSProperty_color_scheme,
                frame->StyleUI()->mColorScheme);
    if (groupStyleChanged && aNeedsInvalidation) {
      auto* pseudo = FindPseudo(PseudoStyleRequest(
          PseudoStyleType::viewTransitionGroup, transitionName));
      MOZ_ASSERT(pseudo);
      // TODO(emilio): Maybe we need something more than recascade? But I don't
      // see how off-hand.
      nsLayoutUtils::PostRestyleEvent(pseudo, RestyleHint::RECASCADE_SELF,
                                      nsChangeHint(0));
    }

    // 5. Live capturing (nothing to do here regarding the capture itself, but
    // if the size has changed, then we need to invalidate the new frame).
    const auto& newSnapshotSize =
        CapturedSize(frame, mInitialSnapshotContainingBlockSize,
                     CapturedSizeType::InkOverflowBox);
    auto oldSize = capturedElement.mNewSnapshotSize;
    capturedElement.mNewSnapshotSize = newSnapshotSize;
    capturedElement.mNewBorderBoxSize = newBorderBoxSize;
    capturedElement.mNewInkOverflowOffset =
        frame->InkOverflowRectRelativeToSelf().TopLeft();
    if (oldSize != capturedElement.mNewSnapshotSize && aNeedsInvalidation) {
      frame->PresShell()->FrameNeedsReflow(
          frame, IntrinsicDirty::FrameAndAncestors, NS_FRAME_IS_DIRTY);
    }
  }
  return true;
}

// https://drafts.csswg.org/css-view-transitions-1/#activate-view-transition
void ViewTransition::Activate() {
  AUTO_PROFILER_FLOW_MARKER("ViewTransition::Activate", LAYOUT,
                            Flow::FromPointer(this));
  // Step 1: If transition's phase is "done", then return.
  if (mPhase == Phase::Done) {
    return;
  }

  // Step 2: Set transition’s relevant global object’s associated document’s
  // rendering suppression for view transitions to false.
  mDocument->SetRenderingSuppressedForViewTransitions(false);

  // Step 3: If transition's initial snapshot containing block size is not
  // equal to the snapshot containing block size, then skip the view transition
  // for transition, and return.
  if (mInitialSnapshotContainingBlockSize !=
      SnapshotContainingBlockRect().Size()) {
    return SkipTransition(SkipTransitionReason::Resize);
  }

  // Step 4: Capture the new state for transition.
  // Step 5 is done along step 4 for performance.
  if (auto skipReason = CaptureNewState()) {
    // We clear named elements to not leave lingering "captured in a view
    // transition" state.
    ClearNamedElements();
    // If failure is returned, then skip the view transition for transition...
    return SkipTransition(*skipReason);
  }

  // Step 6: Setup transition pseudo-elements for transition.
  SetupTransitionPseudoElements();

  // Step 7: Update pseudo-element styles for transition.
  // We don't need to invalidate the pseudo-element styles since we just
  // generated them.
  if (!UpdatePseudoElementStyles(/* aNeedsInvalidation = */ false)) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition's relevant Realm,
    // and return.
    return SkipTransition(SkipTransitionReason::PseudoUpdateFailure);
  }

  // Step 8: Set transition's phase to "animating".
  mPhase = Phase::Animating;
  // Step 9: Resolve transition's ready promise.
  if (Promise* ready = GetReady(IgnoreErrors())) {
    ready->MaybeResolveWithUndefined();
  }

  // Once this view transition is activated, we have to perform the pending
  // operations periodically.
  MOZ_ASSERT(mDocument);
  mDocument->EnsureViewTransitionOperationsHappen();
}

// https://drafts.csswg.org/css-view-transitions/#perform-pending-transition-operations
void ViewTransition::PerformPendingOperations() {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mDocument->GetActiveViewTransition() == this);
  AUTO_PROFILER_FLOW_MARKER("ViewTransition::PerformPendingOperations", LAYOUT,
                            Flow::FromPointer(this));

  // Flush the update callback queue.
  // Note: this ensures that any changes to the DOM scheduled by other skipped
  // transitions are done before the old state for this transition is captured.
  // https://github.com/w3c/csswg-drafts/issues/11943
  RefPtr doc = mDocument;
  doc->FlushViewTransitionUpdateCallbackQueue();

  switch (mPhase) {
    case Phase::PendingCapture:
      return Setup();
    case Phase::Animating:
      return HandleFrame();
    default:
      break;
  }
}

// https://drafts.csswg.org/css-view-transitions/#snapshot-containing-block
nsRect ViewTransition::SnapshotContainingBlockRect(nsPresContext* aPc) {
  // FIXME: Bug 1960762. Tweak this for mobile OS.
  return aPc ? aPc->GetVisibleArea() : nsRect();
}

// https://drafts.csswg.org/css-view-transitions/#snapshot-containing-block
nsRect ViewTransition::SnapshotContainingBlockRect() const {
  nsPresContext* pc = mDocument->GetPresContext();
  return SnapshotContainingBlockRect(pc);
}

Element* ViewTransition::FindPseudo(const PseudoStyleRequest& aRequest) const {
  Element* root = GetViewTransitionTreeRoot();
  if (!root) {
    return nullptr;
  }
  MOZ_ASSERT(root->GetPseudoElementType() == PseudoStyleType::viewTransition);

  if (aRequest.mType == PseudoStyleType::viewTransition) {
    return root;
  }

  // Linear search ::view-transition-group by |aRequest.mIdentifier|.
  // Note: perhaps we can add a hashtable to improve the performance if it's
  // common that there are a lot of view-transition-names.
  Element* group = root->GetFirstElementChild();
  for (; group; group = group->GetNextElementSibling()) {
    MOZ_ASSERT(group->HasName(),
               "The generated ::view-transition-group() should have a name");
    nsAtom* name = group->GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
    if (name == aRequest.mIdentifier) {
      break;
    }
  }

  // No one specifies view-transition-name or we mismatch all names.
  if (!group) {
    return nullptr;
  }

  if (aRequest.mType == PseudoStyleType::viewTransitionGroup) {
    return group;
  }

  Element* imagePair = group->GetFirstElementChild();
  MOZ_ASSERT(imagePair, "::view-transition-image-pair() should exist always");
  if (aRequest.mType == PseudoStyleType::viewTransitionImagePair) {
    return imagePair;
  }

  Element* child = imagePair->GetFirstElementChild();
  // Neither ::view-transition-old() nor ::view-transition-new() doesn't exist.
  if (!child) {
    return nullptr;
  }

  // Check if the first element matches our request.
  const PseudoStyleType type = child->GetPseudoElementType();
  if (type == aRequest.mType) {
    return child;
  }

  // Since the second child is either ::view-transition-new() or nullptr, so we
  // can reject viewTransitionOld request here.
  if (aRequest.mType == PseudoStyleType::viewTransitionOld) {
    return nullptr;
  }

  child = child->GetNextElementSibling();
  MOZ_ASSERT(aRequest.mType == PseudoStyleType::viewTransitionNew);
  MOZ_ASSERT(!child || !child->GetNextElementSibling(),
             "No more psuedo elements in this subtree");
  return child;
}

const StyleLockedDeclarationBlock* ViewTransition::GetDynamicRuleFor(
    const Element& aElement) const {
  if (!aElement.HasName()) {
    return nullptr;
  }
  nsAtom* name = aElement.GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
  auto* capture = mNamedElements.Get(name);
  if (!capture) {
    return nullptr;
  }

  switch (aElement.GetPseudoElementType()) {
    case PseudoStyleType::viewTransitionNew:
      return capture->mNewRule.get();
    case PseudoStyleType::viewTransitionOld:
      return capture->mOldRule.get();
    case PseudoStyleType::viewTransitionImagePair:
      return capture->mImagePairRule.get();
    case PseudoStyleType::viewTransitionGroup:
      return capture->mGroupRule.get();
    default:
      return nullptr;
  }
}

// This function collects frames in the same stacking context. We only put
// the frames which may create a new create stacking context in the list because
// they (and their descendants) are candidates for captured elements (i.e. with
// a valid view-transition-name).
static void CollectDescendantStackingContexts(nsIFrame* aStackingContextRoot,
                                              nsTArray<nsIFrame*>& aList) {
  for (auto& [list, id] : aStackingContextRoot->ChildLists()) {
    for (nsIFrame* f : list) {
      // FIXME: We probably can skip more frames, e.g. scrollbar or scrollcorner
      // to save some time.

      // We only want to sort the frames form a new stacking context in the
      // current stacking context (including the root stacking context). If it
      // creates a new stacking context, its descendants should be traversed
      // (and sorted) independently. Also, if a frame has view-transition-name,
      // it should create a stacking context as well, so this check must include
      // frames with view-transition-name.
      // Note: the root frame may not be the root element, so we still have to
      // check if |f| is the root element.
      if (f->Style()->IsRootElementStyle() || f->IsStackingContext()) {
        aList.AppendElement(f);
        // We will continue to traverse its descendants after we sort |aList|.
        continue;
      }

      // If any flat tree ancestor of this element skips its contents, then
      // continue.
      if (f->IsHiddenByContentVisibilityOnAnyAncestor()) {
        continue;
      }

      // If |insertionFrame| doesn't create stacking context, we have to check
      // its descendants because they are still in the current stacking context.
      CollectDescendantStackingContexts(f, aList);
    }
  }
}

struct ZOrderComparator {
  bool LessThan(const nsIFrame* aLeft, const nsIFrame* aRight) const {
    return aLeft->ZIndex().valueOr(0) < aRight->ZIndex().valueOr(0);
  }
};

template <typename Callback>
static bool ForEachDescendantWithViewTransitionNameInPaintOrder(
    nsIFrame* aFrame, const Callback& aCb) {
  // Call the callback if it specifies view-transition-name.
  if (!aFrame->StyleUIReset()->mViewTransitionName.IsNone() && !aCb(aFrame)) {
    return false;
  }

  nsTArray<nsIFrame*> descendantStackingContexts;
  CollectDescendantStackingContexts(aFrame, descendantStackingContexts);
  // Sort by z-index to make sure we call the callback in paint order.
  descendantStackingContexts.StableSort(ZOrderComparator());

  for (nsIFrame* f : descendantStackingContexts) {
    if (!ForEachDescendantWithViewTransitionNameInPaintOrder(f, aCb)) {
      return false;
    }
  }
  return true;
}

template <typename Callback>
static void ForEachFrameWithViewTransitionName(Document* aDoc,
                                               const Callback& aCb) {
  PresShell* ps = aDoc->GetPresShell();
  if (!ps) {
    return;
  }
  nsIFrame* root = ps->GetRootFrame();
  if (!root) {
    return;
  }
  ForEachDescendantWithViewTransitionNameInPaintOrder(root, aCb);
}

// https://drafts.csswg.org/css-view-transitions/#capture-the-old-state
Maybe<SkipTransitionReason> ViewTransition::CaptureOldState() {
  MOZ_ASSERT(mNamedElements.IsEmpty());

  // Steps 1/2 are variable declarations.
  // Step 3: Let usedTransitionNames be a new set of strings.
  nsTHashSet<nsAtom*> usedTransitionNames;
  // Step 4: Let captureElements be a new list of elements.
  AutoTArray<std::pair<nsIFrame*, RefPtr<nsAtom>>, 32> captureElements;

  // Step 5: If the snapshot containing block size exceeds an
  // implementation-defined maximum, then return failure.
  // TODO(emilio): Implement a maximum if we deem it needed.
  //
  // Step 6: Set transition's initial snapshot containing block size to the
  // snapshot containing block size.
  mInitialSnapshotContainingBlockSize = SnapshotContainingBlockRect().Size();

  // Step 7: For each element of every element that is connected, and has a node
  // document equal to document, in paint order:
  Maybe<SkipTransitionReason> result;
  ForEachFrameWithViewTransitionName(mDocument, [&](nsIFrame* aFrame) {
    RefPtr<nsAtom> name = DocumentScopedTransitionNameFor(aFrame);
    if (!name) {
      // As a fast path we check for v-t-n first.
      // If transitionName is none, or element is not rendered, then continue.
      return true;
    }
    if (aFrame->GetPrevContinuation() || aFrame->GetNextContinuation()) {
      // If element has more than one box fragment, then continue.
      return true;
    }
    if (!usedTransitionNames.EnsureInserted(name)) {
      // We don't expect to see a duplicate transition name when using
      // match-element.
      MOZ_ASSERT(!aFrame->StyleUIReset()->mViewTransitionName.IsMatchElement());

      // If usedTransitionNames contains transitionName, then return failure.
      result.emplace(
          SkipTransitionReason::DuplicateTransitionNameCapturingOldState);
      return false;
    }
    SetCaptured(aFrame, true, name.get());
    captureElements.AppendElement(std::make_pair(aFrame, std::move(name)));
    return true;
  });

  if (result) {
    for (auto& [f, name] : captureElements) {
      SetCaptured(f, false, nullptr);
    }
    return result;
  }

  // Step 8: For each element in captureElements:
  // Step 9: For each element in captureElements, set element's captured
  // in a view transition to false.
  for (auto& [f, name] : captureElements) {
    MOZ_ASSERT(f);
    MOZ_ASSERT(f->GetContent()->IsElement());
    // Capture the view-transition-class.
    // https://drafts.csswg.org/css-view-transitions-2/#vt-class-algorithms
    auto capture = MakeUnique<CapturedElement>(
        f, mInitialSnapshotContainingBlockSize, DocumentScopedClassListFor(f));
    mNamedElements.InsertOrUpdate(name, std::move(capture));
    mNames.AppendElement(name);
  }

  if (!captureElements.IsEmpty()) {
    // When snapshotting an iframe, we need to paint from the root subdoc.
    if (RefPtr<PresShell> ps =
            nsContentUtils::GetInProcessSubtreeRootDocument(mDocument)
                ->GetPresShell()) {
      VT_LOG("ViewTransitions::CaptureOldState(), requesting composite");
      // Build a display list and send it to WR in order to perform the
      // capturing of old content.
      RefPtr<nsViewManager> vm = ps->GetViewManager();
      ps->PaintAndRequestComposite(vm->GetRootView(),
                                   PaintFlags::PaintCompositeOffscreen);
      VT_LOG("ViewTransitions::CaptureOldState(), requesting composite end");
    }
  }

  for (auto& [f, name] : captureElements) {
    SetCaptured(f, false, nullptr);
  }
  return result;
}

// https://drafts.csswg.org/css-view-transitions-1/#capture-the-new-state
Maybe<SkipTransitionReason> ViewTransition::CaptureNewState() {
  nsTHashSet<nsAtom*> usedTransitionNames;
  Maybe<SkipTransitionReason> result;
  ForEachFrameWithViewTransitionName(mDocument, [&](nsIFrame* aFrame) {
    // As a fast path we check for v-t-n first.
    RefPtr<nsAtom> name = DocumentScopedTransitionNameFor(aFrame);
    if (!name) {
      return true;
    }
    if (aFrame->GetPrevContinuation() || aFrame->GetNextContinuation()) {
      // If element has more than one box fragment, then continue.
      return true;
    }
    if (!usedTransitionNames.EnsureInserted(name)) {
      // We don't expect to see a duplicate transition name when using
      // match-element.
      MOZ_ASSERT(!aFrame->StyleUIReset()->mViewTransitionName.IsMatchElement());
      result.emplace(
          SkipTransitionReason::DuplicateTransitionNameCapturingNewState);
      return false;
    }
    bool wasPresent = true;
    auto& capturedElement = mNamedElements.LookupOrInsertWith(name, [&] {
      wasPresent = false;
      return MakeUnique<CapturedElement>();
    });
    if (!wasPresent) {
      mNames.AppendElement(name);
    }
    capturedElement->mNewElement = aFrame->GetContent()->AsElement();
    // Note: mInitialSnapshotContainingBlockSize should be the same as the
    // current snapshot containing block size at this moment because the caller
    // checks it before calling us.
    capturedElement->mNewSnapshotSize =
        CapturedSize(aFrame, mInitialSnapshotContainingBlockSize,
                     CapturedSizeType::InkOverflowBox);
    capturedElement->mNewBorderBoxSize =
        CapturedSize(aFrame, mInitialSnapshotContainingBlockSize,
                     CapturedSizeType::BorderBox);
    capturedElement->mNewInkOverflowOffset =
        aFrame->InkOverflowRectRelativeToSelf().TopLeft();
    // Update its class list. This may override the existing class list because
    // the users may change view-transition-class in the callback function. We
    // have to use the latest one.
    // https://drafts.csswg.org/css-view-transitions-2/#vt-class-algorithms
    capturedElement->CaptureClassList(DocumentScopedClassListFor(aFrame));
    SetCaptured(aFrame, true, name);
    return true;
  });
  return result;
}

// https://drafts.csswg.org/css-view-transitions/#setup-view-transition
void ViewTransition::Setup() {
  AUTO_PROFILER_FLOW_MARKER("ViewTransition::Setup", LAYOUT,
                            Flow::FromPointer(this));
  // Step 2: Capture the old state for transition.
  if (auto skipReason = CaptureOldState()) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition’s relevant Realm,
    // and return.
    return SkipTransition(*skipReason);
  }

  // Step 3: Set document’s rendering suppression for view transitions to true.
  mDocument->SetRenderingSuppressedForViewTransitions(true);

  // Step 4: Queue a global task on the DOM manipulation task source, given
  // transition's relevant global object, to perform the following steps:
  //   4.1: If transition's phase is "done", then abort these steps.
  //   4.2: Schedule the update callback for transition.
  //   4.3: Flush the update callback queue.
  mDocument->Dispatch(
      NewRunnableMethod("ViewTransition::MaybeScheduleUpdateCallback", this,
                        &ViewTransition::MaybeScheduleUpdateCallback));
}

// https://drafts.csswg.org/css-view-transitions-1/#handle-transition-frame
void ViewTransition::HandleFrame() {
  // Steps 1-3: Steps 1-3: Compute active animations.
  const bool hasActiveAnimations = CheckForActiveAnimations();

  // Step 4: If hasActiveAnimations is false:
  if (!hasActiveAnimations) {
    AUTO_PROFILER_TERMINATING_FLOW_MARKER("ViewTransition::HandleFrameFinish",
                                          LAYOUT, Flow::FromPointer(this));
    // 4.1: Set transition's phase to "done".
    mPhase = Phase::Done;
    // 4.2: Clear view transition transition.
    ClearActiveTransition(false);
    // 4.3: Resolve transition's finished promise.
    if (Promise* finished = GetFinished(IgnoreErrors())) {
      finished->MaybeResolveWithUndefined();
    }
    return;
  }

  AUTO_PROFILER_FLOW_MARKER("ViewTransition::HandleFrame", LAYOUT,
                            Flow::FromPointer(this));

  // Step 5: If transition’s initial snapshot containing block size is not equal
  // to the snapshot containing block size, then skip the view transition for
  // transition with an "InvalidStateError" DOMException in transition’s
  // relevant Realm, and return.
  if (SnapshotContainingBlockRect().Size() !=
      mInitialSnapshotContainingBlockSize) {
    SkipTransition(SkipTransitionReason::Resize);
    return;
  }

  // Step 6: Update pseudo-element styles for transition.
  if (!UpdatePseudoElementStyles(/* aNeedsInvalidation= */ true)) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition's relevant Realm,
    // and return.
    return SkipTransition(SkipTransitionReason::PseudoUpdateFailure);
  }

  // If the view transition is still animating after HandleFrame(), we have to
  // periodically perform operations to check if it is still animating in the
  // following ticks.
  mDocument->EnsureViewTransitionOperationsHappen();
}

static bool CheckForActiveAnimationsForEachPseudo(
    const Element& aRoot, const AnimationTimeline& aDocTimeline,
    const AnimationEventDispatcher& aDispatcher,
    PseudoStyleRequest&& aRequest) {
  // Check EffectSet because an Animation (either a CSS Animations or a
  // script animation) is associated with a KeyframeEffect. If the animation
  // doesn't have an associated effect, we can skip it per spec.
  // If the effect target is not the element we request, it shouldn't be in
  // |effects| either.
  EffectSet* effects = EffectSet::Get(&aRoot, aRequest);
  if (!effects) {
    return false;
  }

  for (const auto* effect : *effects) {
    // 3.1: For each animation whose timeline is a document timeline associated
    // with document, and contains at least one associated effect whose effect
    // target is element, set hasActiveAnimations to true if any of the
    // following conditions is true:
    //   * animation’s play state is paused or running.
    //   * document’s pending animation event queue has any events associated
    //     with animation.

    MOZ_ASSERT(effect && effect->GetAnimation(),
               "Only effects associated with an animation should be "
               "added to an element's effect set");
    const Animation* anim = effect->GetAnimation();

    // The animation's timeline is not the document timeline.
    if (anim->GetTimeline() != &aDocTimeline) {
      continue;
    }

    // Return true if any of the following conditions is true:
    // 1. animation’s play state is paused or running.
    // 2. document’s pending animation event queue has any events associated
    //    with animation.
    const auto playState = anim->PlayState();
    if (playState != AnimationPlayState::Paused &&
        playState != AnimationPlayState::Running &&
        !aDispatcher.HasQueuedEventsFor(anim)) {
      continue;
    }
    return true;
  }
  return false;
}

// This is the implementation of step 3 in HandleFrame(). For each element of
// transition’s transition root pseudo-element’s inclusive descendants, we check
// if it has active animations.
bool ViewTransition::CheckForActiveAnimations() const {
  MOZ_ASSERT(mDocument);

  if (StaticPrefs::dom_viewTransitions_remain_active()) {
    return true;
  }

  const Element* root = mDocument->GetRootElement();
  if (!root) {
    // The documentElement could be removed during animating via script.
    return false;
  }

  const AnimationTimeline* timeline = mDocument->Timeline();
  if (!timeline) {
    return false;
  }

  nsPresContext* presContext = mDocument->GetPresContext();
  if (!presContext) {
    return false;
  }

  const AnimationEventDispatcher* dispatcher =
      presContext->AnimationEventDispatcher();
  MOZ_ASSERT(dispatcher);

  auto checkForEachPseudo = [&](PseudoStyleRequest&& aRequest) {
    return CheckForActiveAnimationsForEachPseudo(*root, *timeline, *dispatcher,
                                                 std::move(aRequest));
  };

  bool hasActiveAnimations =
      checkForEachPseudo(PseudoStyleRequest(PseudoStyleType::viewTransition));
  for (nsAtom* name : mNamedElements.Keys()) {
    if (hasActiveAnimations) {
      break;
    }

    hasActiveAnimations =
        checkForEachPseudo({PseudoStyleType::viewTransitionGroup, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionImagePair, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionOld, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionNew, name});
  }
  return hasActiveAnimations;
}

void ViewTransition::ClearNamedElements() {
  for (auto& entry : mNamedElements) {
    if (auto* element = entry.GetData()->mNewElement.get()) {
      if (nsIFrame* f = element->GetPrimaryFrame()) {
        SetCaptured(f, false, nullptr);
      }
    }
  }
  mNamedElements.Clear();
  mNames.Clear();
}

static void ClearViewTransitionsAnimationData(Element* aRoot) {
  if (!aRoot) {
    return;
  }

  auto* data = aRoot->GetAnimationData();
  if (!data) {
    return;
  }
  data->ClearViewTransitionPseudos();
}

// https://drafts.csswg.org/css-view-transitions-1/#clear-view-transition
void ViewTransition::ClearActiveTransition(bool aIsDocumentHidden) {
  // Steps 1-2
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mDocument->GetActiveViewTransition() == this);

  // Ensure that any styles associated with :active-view-transition no longer
  // apply.
  if (auto* root = mDocument->GetRootElement()) {
    root->RemoveStates(ElementState::ACTIVE_VIEW_TRANSITION);
  }

  // Step 3
  ClearNamedElements();

  // Step 4: Clear show transition tree flag (we just destroy the pseudo tree,
  // see SetupTransitionPseudoElements).
  if (mSnapshotContainingBlock) {
    nsAutoScriptBlocker scriptBlocker;
    if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
      mSnapshotContainingBlock->QueueDevtoolsAnonymousEvent(
          /* aIsRemove = */ true);
    }
    if (PresShell* ps = mDocument->GetPresShell()) {
      ps->ContentWillBeRemoved(mSnapshotContainingBlock, {});
    }
    mSnapshotContainingBlock->UnbindFromTree();
    mSnapshotContainingBlock = nullptr;

    // If the document is being destroyed, we cannot get the animation data
    // (e.g. it may crash when using nsINode::GetBoolFlag()), so we have to skip
    // this case. It's fine because those animations should still be stopped and
    // removed if no frame there.
    //
    // Another case is that the document is hidden. In that case, we don't setup
    // the pseudo elements, so it's fine to skip it as well.
    if (!aIsDocumentHidden) {
      ClearViewTransitionsAnimationData(mDocument->GetRootElement());
    }
  }
  mDocument->ClearActiveViewTransition();
}

void ViewTransition::SkipTransition(SkipTransitionReason aReason) {
  SkipTransition(aReason, JS::UndefinedHandleValue);
}

// https://drafts.csswg.org/css-view-transitions-1/#skip-the-view-transition
// https://drafts.csswg.org/css-view-transitions-1/#dom-viewtransition-skiptransition
void ViewTransition::SkipTransition(
    SkipTransitionReason aReason,
    JS::Handle<JS::Value> aUpdateCallbackRejectReason) {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT_IF(aReason != SkipTransitionReason::JS, mPhase != Phase::Done);
  MOZ_ASSERT_IF(aReason != SkipTransitionReason::UpdateCallbackRejected,
                aUpdateCallbackRejectReason == JS::UndefinedHandleValue);
  VT_LOG("ViewTransition::SkipTransition(%d, %d)\n", int(mPhase), int(aReason));
  AUTO_PROFILER_TERMINATING_FLOW_MARKER("ViewTransition::SkipTransition",
                                        LAYOUT, Flow::FromPointer(this));
  if (mPhase == Phase::Done) {
    return;
  }
  // Step 3: If transition’s phase is before "update-callback-called", then
  // schedule the update callback for transition.
  if (UnderlyingValue(mPhase) < UnderlyingValue(Phase::UpdateCallbackCalled)) {
    mDocument->ScheduleViewTransitionUpdateCallback(this);
  }

  // Step 4: Set rendering suppression for view transitions to false.
  mDocument->SetRenderingSuppressedForViewTransitions(false);

  // Step 5: If document's active view transition is transition, Clear view
  // transition transition.
  if (mDocument->GetActiveViewTransition() == this) {
    ClearActiveTransition(aReason == SkipTransitionReason::DocumentHidden);
  }

  // Step 6: Set transition's phase to "done".
  mPhase = Phase::Done;

  // Step 7: Reject transition's ready promise with reason.
  Promise* ucd = GetUpdateCallbackDone(IgnoreErrors());
  if (Promise* readyPromise = GetReady(IgnoreErrors())) {
    switch (aReason) {
      case SkipTransitionReason::JS:
        readyPromise->MaybeRejectWithAbortError(
            "Skipped ViewTransition due to skipTransition() call");
        break;
      case SkipTransitionReason::ClobberedActiveTransition:
        readyPromise->MaybeRejectWithAbortError(
            "Skipped ViewTransition due to another transition starting");
        break;
      case SkipTransitionReason::DocumentHidden:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped ViewTransition due to document being hidden");
        break;
      case SkipTransitionReason::Timeout:
        readyPromise->MaybeRejectWithTimeoutError(
            "Skipped ViewTransition due to timeout");
        break;
      case SkipTransitionReason::DuplicateTransitionNameCapturingOldState:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Duplicate view-transition-name value while capturing old state");
        break;
      case SkipTransitionReason::DuplicateTransitionNameCapturingNewState:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Duplicate view-transition-name value while capturing new state");
        break;
      case SkipTransitionReason::RootRemoved:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to root element going away");
        break;
      case SkipTransitionReason::PageSwap:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to page swap");
        break;
      case SkipTransitionReason::Resize:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to viewport resize");
        break;
      case SkipTransitionReason::PseudoUpdateFailure:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to hidden new element");
        break;
      case SkipTransitionReason::ResetRendering:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to graphics process or device reset");
        break;
      case SkipTransitionReason::UpdateCallbackRejected:
        readyPromise->MaybeReject(aUpdateCallbackRejectReason);

        // Step 8, The case we have to reject the finished promise. Do this here
        // to make sure it reacts to UpdateCallbackRejected.
        //
        // Note: we intentionally reject the finished promise after the ready
        // promise to make sure the order of promise callbacks is correct in
        // script.
        if (ucd) {
          MOZ_ASSERT(ucd->State() == Promise::PromiseState::Rejected);
          if (Promise* finished = GetFinished(IgnoreErrors())) {
            // Since the rejection of transition’s update callback done promise
            // isn’t explicitly handled here, if transition’s update callback
            // done promise rejects, then transition’s finished promise will
            // reject with the same reason.
            finished->MaybeReject(aUpdateCallbackRejectReason);
          }
        }
        break;
    }
  }

  // Step 8: Resolve transition's finished promise with the result of reacting
  // to transition's update callback done promise:
  // Note: It is not guaranteed that |mPhase| is Done in CallUpdateCallback().
  // There are two possible cases:
  // 1. If we skip the view transitions before updateCallbackDone callback
  //    is dispatched, we come here first. In this case we don't have to resolve
  //    the finsihed promise because CallUpdateCallback() will do it.
  // 2. If we skip the view transitions after updateCallbackDone callback, the
  //    finished promise hasn't been resolved because |mPhase| is not Done (i.e.
  //    |mPhase| is UpdateCallbackCalled) when we handle updateCallbackDone
  //    callback. Therefore, we have to resolve the finished promise based on
  //    the PromiseState of |mUpdateCallbackDone|.
  if (ucd && ucd->State() == Promise::PromiseState::Resolved) {
    if (Promise* finished = GetFinished(IgnoreErrors())) {
      // If the promise was fulfilled, then return undefined.
      finished->MaybeResolveWithUndefined();
    }
  }
}

Maybe<uint64_t> ViewTransition::GetElementIdentifier(Element* aElement) const {
  return mElementIdentifiers.MaybeGet(aElement);
}

uint64_t ViewTransition::EnsureElementIdentifier(Element* aElement) {
  static uint64_t sLastIdentifier = 0;
  return mElementIdentifiers.WithEntryHandle(aElement, [&](auto&& entry) {
    return entry.OrInsertWith([&]() { return sLastIdentifier++; });
  });
}

already_AddRefed<nsAtom> ViewTransition::DocumentScopedTransitionNameFor(
    nsIFrame* aFrame) {
  return DocumentScopedTransitionNameForWithGenerator(
      aFrame, [this](Element* aElement) {
        return Some(EnsureElementIdentifier(aElement));
      });
}

JSObject* ViewTransition::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return ViewTransition_Binding::Wrap(aCx, this, aGivenProto);
}

};  // namespace mozilla::dom
