/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ViewTransition_h
#define mozilla_dom_ViewTransition_h

#include "mozilla/Attributes.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "nsAtomHashKeys.h"
#include "nsClassHashtable.h"
#include "nsRect.h"
#include "nsRefPtrHashtable.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;
class nsITimer;

namespace mozilla {

class ErrorResult;
struct Keyframe;
struct PseudoStyleRequest;
struct StyleLockedDeclarationBlock;

namespace layers {
class RenderRootStateManager;
}

namespace wr {
struct ImageKey;
class IpcResourceUpdateQueue;
}  // namespace wr

namespace dom {

extern LazyLogModule gViewTransitionsLog;

#define VT_LOG(...)                                                    \
  MOZ_LOG(mozilla::dom::gViewTransitionsLog, mozilla::LogLevel::Debug, \
          (__VA_ARGS__))

#ifdef DEBUG
#  define VT_LOG_DEBUG(...) VT_LOG(__VA_ARGS__)
#else
#  define VT_LOG_DEBUG(...)
#endif

class Document;
class Element;
class Promise;
class ViewTransitionUpdateCallback;

enum class SkipTransitionReason : uint8_t {
  JS,
  DocumentHidden,
  RootRemoved,
  ClobberedActiveTransition,
  Timeout,
  UpdateCallbackRejected,
  DuplicateTransitionNameCapturingOldState,
  DuplicateTransitionNameCapturingNewState,
  PseudoUpdateFailure,
  Resize,
  PageSwap,
  // Can happen due to various recoverable internal errors such as GPU process
  // crashes or GPU device resets.
  ResetRendering,
};

// https://drafts.csswg.org/css-view-transitions-1/#viewtransition-phase
enum class ViewTransitionPhase : uint8_t {
  PendingCapture = 0,
  UpdateCallbackCalled,
  Animating,
  Done,
};

class ViewTransition final : public nsISupports, public nsWrapperCache {
 public:
  using Phase = ViewTransitionPhase;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ViewTransition)

  ViewTransition(Document&, ViewTransitionUpdateCallback*);

  Promise* GetUpdateCallbackDone(ErrorResult&);
  Promise* GetReady(ErrorResult&);
  Promise* GetFinished(ErrorResult&);

  void SkipTransition(SkipTransitionReason = SkipTransitionReason::JS);
  MOZ_CAN_RUN_SCRIPT void PerformPendingOperations();

  // Get the snapshot containing block, which is the top-layer for rendering the
  // view transition tree.
  Element* GetSnapshotContainingBlock() const {
    return mSnapshotContainingBlock;
  }
  // Get ::view-transition pseudo element, which is the view transition tree
  // root. We find the pseudo element of this tree from this node.
  Element* GetViewTransitionTreeRoot() const;

  Maybe<nsSize> GetOldInkOverflowBoxSize(nsAtom* aName) const;
  Maybe<nsSize> GetNewInkOverflowBoxSize(nsAtom* aName) const;
  Maybe<nsSize> GetOldBorderBoxSize(nsAtom* aName) const;
  Maybe<nsSize> GetNewBorderBoxSize(nsAtom* aName) const;
  Maybe<nsPoint> GetOldInkOverflowOffset(nsAtom* aName) const;
  Maybe<nsPoint> GetNewInkOverflowOffset(nsAtom* aName) const;
  Maybe<nsRect> GetOldActiveRect(nsAtom* aName) const;
  Maybe<nsRect> GetNewActiveRect(nsAtom* aName) const;
  // Use this to generate the old state image key for use in a stacking context.
  // Do not use the returned image key in an image display item, use
  // ReadOldImageKey instead.
  const wr::ImageKey* GetOrCreateOldImageKey(nsAtom* aName,
                                             layers::RenderRootStateManager*,
                                             wr::IpcResourceUpdateQueue&) const;
  // Use this to get the already-created old state image key for use in an image
  // display item.
  // This marks the old state image key as used which influences the how eagerly
  // it can be deleted.
  const wr::ImageKey* ReadOldImageKey(nsAtom* aName,
                                      layers::RenderRootStateManager*,
                                      wr::IpcResourceUpdateQueue&) const;
  const wr::ImageKey* GetNewImageKey(nsAtom* aName) const;
  const wr::ImageKey* GetImageKeyForCapturedFrame(
      nsIFrame* aFrame, layers::RenderRootStateManager*,
      wr::IpcResourceUpdateQueue&) const;
  void UpdateActiveRectForCapturedFrame(
      nsIFrame* capturedFrame, const gfx::MatrixScales& aInheritedScale,
      nsRect& aOutCapturedRect);

  Element* FindPseudo(const PseudoStyleRequest&) const;

  const StyleLockedDeclarationBlock* GetDynamicRuleFor(const Element&) const;

  static constexpr nsLiteralString kGroupAnimPrefix =
      u"-ua-view-transition-group-anim-"_ns;

  [[nodiscard]] bool GetGroupKeyframes(nsAtom* aAnimationName,
                                       const StyleComputedTimingFunction&,
                                       nsTArray<Keyframe>&);

  bool MatchClassList(nsAtom*, const nsTArray<StyleAtom>&) const;

  nsIGlobalObject* GetParentObject() const;
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*> aGivenProto) override;

  struct CapturedElement;

  static nsRect SnapshotContainingBlockRect(nsPresContext*);
  MOZ_CAN_RUN_SCRIPT void CallUpdateCallback(ErrorResult&);

 private:
  MOZ_CAN_RUN_SCRIPT void MaybeScheduleUpdateCallback();
  void Activate();

  void ClearActiveTransition(bool aIsDocumentHidden);
  void Timeout();
  MOZ_CAN_RUN_SCRIPT void Setup();
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Maybe<SkipTransitionReason>
  CaptureOldState();
  [[nodiscard]] Maybe<SkipTransitionReason> CaptureNewState();
  void SetupTransitionPseudoElements();
  [[nodiscard]] bool UpdatePseudoElementStyles(bool aNeedsInvalidation);
  void ClearNamedElements();
  void HandleFrame();
  bool CheckForActiveAnimations() const;
  void SkipTransition(SkipTransitionReason, JS::Handle<JS::Value>);
  void ClearTimeoutTimer();

  nsRect SnapshotContainingBlockRect() const;

  Maybe<uint64_t> GetElementIdentifier(Element* aElement) const;
  uint64_t EnsureElementIdentifier(Element* aElement);

  already_AddRefed<nsAtom> DocumentScopedTransitionNameFor(nsIFrame* aFrame);

  ~ViewTransition();

  // Stored for the whole lifetime of the object (until CC).
  RefPtr<Document> mDocument;
  RefPtr<ViewTransitionUpdateCallback> mUpdateCallback;

  // https://drafts.csswg.org/css-view-transitions/#viewtransition-named-elements
  using NamedElements = nsClassHashtable<nsAtomHashKey, CapturedElement>;
  NamedElements mNamedElements;
  // mNamedElements is an unordered map, we need to keep the tree order. This
  // also keeps the strong reference to the view-transition-name which may be
  // auto-generated for this view transition.
  AutoTArray<RefPtr<nsAtom>, 8> mNames;

  // The element identifier for the elements which need the auto-generated
  // view-transition-name. The lifetime of those element identifiers is
  // element’s node document’s active view transition.
  // Note: Use a non-owning element pointer because we never dereference it.
  // It is just a key to map an id. The size of this hashmap is fixed after we
  // capture the old state and new state,
  using ElementIdentifiers = nsTHashMap<Element*, uint64_t>;
  ElementIdentifiers mElementIdentifiers;

  // https://drafts.csswg.org/css-view-transitions/#viewtransition-initial-snapshot-containing-block-size
  nsSize mInitialSnapshotContainingBlockSize;

  // Allocated lazily, but same object once allocated (again until CC).
  RefPtr<Promise> mUpdateCallbackDonePromise;
  RefPtr<Promise> mReadyPromise;
  RefPtr<Promise> mFinishedPromise;

  static void TimeoutCallback(nsITimer*, void*);
  RefPtr<nsITimer> mTimeoutTimer;

  Phase mPhase = Phase::PendingCapture;
  // The wrapper of the pseudo-elements tree, to make sure it is always
  // out-of-flow. This is the top-layer for rendering the view transition tree.
  // So in general, its child (and only one) is the transition root
  // pseudo-element.
  RefPtr<Element> mSnapshotContainingBlock;
};

}  // namespace dom
}  // namespace mozilla

#endif
