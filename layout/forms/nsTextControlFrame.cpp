/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTextControlFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresState.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/TextEditor.h"
#include "mozilla/Try.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Text.h"
#include "nsAttrValueInlines.h"
#include "nsCOMPtr.h"
#include "nsCSSPseudoElements.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsFocusManager.h"
#include "nsFontMetrics.h"
#include "nsFrameSelection.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIEditor.h"
#include "nsILayoutHistoryState.h"
#include "nsINode.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPIDOMWindow.h"  //needed for notify selection changed to update the menus ect.
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsRange.h"  //for selection setting helper func
#include "nsTextFragment.h"
#include "nsTextNode.h"

using namespace mozilla;
using namespace mozilla::dom;

nsIFrame* NS_NewTextControlFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell)
      nsTextControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTextControlFrame)

NS_QUERYFRAME_HEAD(nsTextControlFrame)
  NS_QUERYFRAME_ENTRY(nsTextControlFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
  NS_QUERYFRAME_ENTRY(nsIStatefulFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsTextControlFrame::AccessibleType() {
  return a11y::eHTMLTextFieldType;
}
#endif

#ifdef DEBUG
class EditorInitializerEntryTracker {
 public:
  explicit EditorInitializerEntryTracker(nsTextControlFrame& frame)
      : mFrame(frame), mFirstEntry(false) {
    if (!mFrame.mInEditorInitialization) {
      mFrame.mInEditorInitialization = true;
      mFirstEntry = true;
    }
  }
  ~EditorInitializerEntryTracker() {
    if (mFirstEntry) {
      mFrame.mInEditorInitialization = false;
    }
  }
  bool EnteredMoreThanOnce() const { return !mFirstEntry; }

 private:
  nsTextControlFrame& mFrame;
  bool mFirstEntry;
};
#endif

class nsTextControlFrame::nsAnonDivObserver final
    : public nsStubMutationObserver {
 public:
  explicit nsAnonDivObserver(nsTextControlFrame& aFrame) : mFrame(aFrame) {}
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

 private:
  ~nsAnonDivObserver() = default;
  nsTextControlFrame& mFrame;
};

nsTextControlFrame::nsTextControlFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext,
                                       nsIFrame::ClassID aClassID)
    : nsContainerFrame(aStyle, aPresContext, aClassID) {}

nsTextControlFrame::~nsTextControlFrame() = default;

ScrollContainerFrame* nsTextControlFrame::GetScrollTargetFrame() const {
  if (!mRootNode) {
    return nullptr;
  }
  return do_QueryFrame(mRootNode->GetPrimaryFrame());
}

void nsTextControlFrame::Destroy(DestroyContext& aContext) {
  RemoveProperty(TextControlInitializer());

  // Unbind the text editor state object from the frame.  The editor will live
  // on, but things like controllers will be released.
  RefPtr textControlElement = ControlElement();
  if (mMutationObserver) {
    textControlElement->UnbindFromFrame(this);
    mRootNode->RemoveMutationObserver(mMutationObserver);
    mMutationObserver = nullptr;
  }

  // If there is a drag session, user may be dragging selection in removing
  // text node in the text control.  If so, we should set source node to the
  // text control because another text node may be recreated soon if the text
  // control is just reframed.
  if (nsCOMPtr<nsIDragSession> dragSession =
          nsContentUtils::GetDragSession(PresContext())) {
    if (dragSession->IsDraggingTextInTextControl() && mRootNode &&
        mRootNode->GetFirstChild()) {
      nsCOMPtr<nsINode> sourceNode;
      if (NS_SUCCEEDED(
              dragSession->GetSourceNode(getter_AddRefs(sourceNode))) &&
          mRootNode->Contains(sourceNode)) {
        MOZ_ASSERT(sourceNode->IsText());
        dragSession->UpdateSource(textControlElement, nullptr);
      }
    }
  }
  // Otherwise, EventStateManager may track gesture to start drag with native
  // anonymous nodes in the text control element.
  else if (textControlElement->GetPresContext(Element::eForComposedDoc)) {
    textControlElement->GetPresContext(Element::eForComposedDoc)
        ->EventStateManager()
        ->TextControlRootWillBeRemoved(*textControlElement);
  }

  // If we're a subclass like nsNumberControlFrame, then it owns the root of the
  // anonymous subtree where mRootNode is.
  aContext.AddAnonymousContent(mRootNode.forget());
  aContext.AddAnonymousContent(mPlaceholderDiv.forget());
  aContext.AddAnonymousContent(mPreviewDiv.forget());
  aContext.AddAnonymousContent(mButton.forget());

  nsContainerFrame::Destroy(aContext);
}

LogicalSize nsTextControlFrame::CalcIntrinsicSize(gfxContext* aRenderingContext,
                                                  WritingMode aWM) const {
  LogicalSize intrinsicSize(aWM);
  const float inflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetFontMetricsForFrame(this, inflation);
  const nscoord lineHeight = ReflowInput::CalcLineHeight(
      *Style(), PresContext(), GetContent(), NS_UNCONSTRAINEDSIZE, inflation);
  // Use the larger of the font's "average" char width or the width of the
  // zero glyph (if present) as the basis for resolving the size attribute.
  const nscoord charWidth =
      std::max(fontMet->ZeroOrAveCharWidth(), fontMet->AveCharWidth());
  const nscoord charMaxAdvance = fontMet->MaxAdvance();

  // Initialize based on the width in characters.
  const Maybe<int32_t> maybeCols = GetCols();
  const int32_t cols = maybeCols.valueOr(TextControlElement::DEFAULT_COLS);
  intrinsicSize.ISize(aWM) = cols * charWidth;

  // If we do not have what appears to be a fixed-width font, add a "slop"
  // amount based on the max advance of the font (clamped to twice charWidth,
  // because some fonts have a few extremely-wide outliers that would result
  // in excessive width here; e.g. the triple-emdash ligature in SFNS Text),
  // minus 4px. This helps avoid input fields becoming unusably narrow with
  // small size values.
  if (charMaxAdvance - charWidth > AppUnitsPerCSSPixel()) {
    nscoord internalPadding =
        std::max(0, std::min(charMaxAdvance, charWidth * 2) -
                        nsPresContext::CSSPixelsToAppUnits(4));
    internalPadding = RoundToMultiple(internalPadding, AppUnitsPerCSSPixel());
    intrinsicSize.ISize(aWM) += internalPadding;
  }

  // Increment width with cols * letter-spacing.
  {
    const auto& letterSpacing = StyleText()->mLetterSpacing;
    if (!letterSpacing.IsDefinitelyZero()) {
      intrinsicSize.ISize(aWM) +=
          cols * letterSpacing.Resolve(fontMet->EmHeight());
    }
  }

  // Set the height equal to total number of rows (times the height of each
  // line, of course)
  intrinsicSize.BSize(aWM) = lineHeight * GetRows();

  // Add in the size of the scrollbars for textarea
  if (IsTextArea()) {
    ScrollContainerFrame* scrollContainerFrame = GetScrollTargetFrame();
    NS_ASSERTION(scrollContainerFrame, "Child must be scrollable");
    if (scrollContainerFrame) {
      LogicalMargin scrollbarSizes(
          aWM, scrollContainerFrame->GetDesiredScrollbarSizes());
      intrinsicSize.ISize(aWM) += scrollbarSizes.IStartEnd(aWM);

      // We only include scrollbar-thickness in our BSize if the scrollbar on
      // that side is explicitly forced-to-be-present.
      const bool includeScrollbarBSize = [&] {
        if (!StaticPrefs::
                layout_forms_textarea_sizing_excludes_auto_scrollbar_enabled()) {
          return true;
        }
        auto overflow = aWM.IsVertical() ? StyleDisplay()->mOverflowY
                                         : StyleDisplay()->mOverflowX;
        return overflow == StyleOverflow::Scroll;
      }();
      if (includeScrollbarBSize) {
        intrinsicSize.BSize(aWM) += scrollbarSizes.BStartEnd(aWM);
      }
    }
  }

  // Add the inline size of the button if our char size is explicit, so as to
  // make sure to make enough space for it.
  if (maybeCols.isSome() && mButton && mButton->GetPrimaryFrame()) {
    const IntrinsicSizeInput input(aRenderingContext, Nothing(), Nothing());
    intrinsicSize.ISize(aWM) += mButton->GetPrimaryFrame()->GetMinISize(input);
  }

  return intrinsicSize;
}

nsresult nsTextControlFrame::EnsureEditorInitialized() {
  // This method initializes our editor, if needed.

  // This code used to be called from CreateAnonymousContent(), but
  // when the editor set the initial string, it would trigger a
  // PresShell listener which called FlushPendingNotifications()
  // during frame construction. This was causing other form controls
  // to display wrong values.  Additionally, calling this every time
  // a text frame control is instantiated means that we're effectively
  // instantiating the editor for all text fields, even if they
  // never get used.  So, now this method is being called lazily only
  // when we actually need an editor.

  if (mEditorHasBeenInitialized) {
    return NS_OK;
  }

  Document* doc = mContent->GetComposedDoc();
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  AutoWeakFrame weakFrame(this);

  // Flush out content on our document.  Have to do this, because script
  // blockers don't prevent the sink flushing out content and notifying in the
  // process, which can destroy frames.
  doc->FlushPendingNotifications(FlushType::ContentAndNotify);
  NS_ENSURE_TRUE(weakFrame.IsAlive(), NS_ERROR_FAILURE);

  // Make sure that editor init doesn't do things that would kill us off
  // (especially off the script blockers it'll create for its DOM mutations).
  {
    RefPtr<TextControlElement> textControlElement = ControlElement();

    // Hide selection changes during the initialization, as webpages should not
    // be aware of these initializations
    AutoHideSelectionChanges hideSelectionChanges(
        textControlElement->GetIndependentFrameSelection());

    nsAutoScriptBlocker scriptBlocker;

    // Time to mess with our security context... See comments in GetValue()
    // for why this is needed.
    mozilla::dom::AutoNoJSAPI nojsapi;

#ifdef DEBUG
    // Make sure we are not being called again until we're finished.
    // If reentrancy happens, just pretend that we don't have an editor.
    const EditorInitializerEntryTracker tracker(*this);
    NS_ASSERTION(!tracker.EnteredMoreThanOnce(),
                 "EnsureEditorInitialized has been called while a previous "
                 "call was in progress");
#endif

    // Create an editor for the frame, if one doesn't already exist
    nsresult rv = textControlElement->CreateEditor();
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_STATE(weakFrame.IsAlive());

    // Set mEditorHasBeenInitialized so that subsequent calls will use the
    // editor.
    mEditorHasBeenInitialized = true;

    if (weakFrame.IsAlive()) {
      uint32_t position = 0;

      // Set the selection to the end of the text field (bug 1287655),
      // but only if the contents has changed (bug 1337392).
      if (textControlElement->ValueChanged()) {
        nsAutoString val;
        textControlElement->GetTextEditorValue(val);
        position = val.Length();
      }

      SetSelectionEndPoints(position, position, SelectionDirection::None);
    }
  }
  NS_ENSURE_STATE(weakFrame.IsAlive());
  return NS_OK;
}

already_AddRefed<Element> nsTextControlFrame::MakeAnonElement(
    PseudoStyleType aPseudoType, Element* aParent, nsAtom* aTag) const {
  MOZ_ASSERT(aPseudoType != PseudoStyleType::NotPseudo);
  Document* doc = PresContext()->Document();
  RefPtr<Element> element = doc->CreateHTMLElement(aTag);
  element->SetPseudoElementType(aPseudoType);
  if (aPseudoType == PseudoStyleType::mozTextControlEditingRoot) {
    // Make our root node editable
    element->SetFlags(NODE_IS_EDITABLE);
  }

  if (aPseudoType == PseudoStyleType::mozNumberSpinDown ||
      aPseudoType == PseudoStyleType::mozNumberSpinUp) {
    element->SetAttr(kNameSpaceID_None, nsGkAtoms::aria_hidden, u"true"_ns,
                     false);
  }

  if (aParent) {
    aParent->AppendChildTo(element, false, IgnoreErrors());
  }

  return element.forget();
}

already_AddRefed<Element> nsTextControlFrame::MakeAnonDivWithTextNode(
    PseudoStyleType aPseudoType) const {
  RefPtr<Element> div = MakeAnonElement(aPseudoType);

  // Create the text node for the anonymous <div> element.
  nsNodeInfoManager* nim = div->OwnerDoc()->NodeInfoManager();
  RefPtr<nsTextNode> textNode = new (nim) nsTextNode(nim);
  // If the anonymous div element is not for the placeholder, we should
  // mark the text node as "maybe modified frequently" for avoiding ASCII
  // range checks at every input.
  if (aPseudoType != PseudoStyleType::placeholder) {
    textNode->MarkAsMaybeModifiedFrequently();
    // Additionally, this is a password field, the text node needs to be
    // marked as "maybe masked" unless it's in placeholder.
    if (IsPasswordTextControl()) {
      textNode->MarkAsMaybeMasked();
    }
  }
  div->AppendChildTo(textNode, false, IgnoreErrors());
  return div.forget();
}

nsresult nsTextControlFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(mContent, "We should have a content!");

  AddStateBits(NS_FRAME_INDEPENDENT_SELECTION);

  RefPtr<TextControlElement> textControlElement = ControlElement();
  mRootNode = MakeAnonElement(PseudoStyleType::mozTextControlEditingRoot);
  if (NS_WARN_IF(!mRootNode)) {
    return NS_ERROR_FAILURE;
  }

  mMutationObserver = new nsAnonDivObserver(*this);
  mRootNode->AddMutationObserver(mMutationObserver);

  // Bind the frame to its text control.
  //
  // This can realistically fail in paginated mode, where we may replicate
  // fixed-positioned elements and the replicated frame will not get the chance
  // to get an editor.
  nsresult rv = textControlElement->BindToFrame(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mRootNode->RemoveMutationObserver(mMutationObserver);
    mMutationObserver = nullptr;
    mRootNode = nullptr;
    return rv;
  }

  CreatePlaceholderIfNeeded();
  if (mPlaceholderDiv) {
    aElements.AppendElement(mPlaceholderDiv);
  }
  CreatePreviewIfNeeded();
  if (mPreviewDiv) {
    aElements.AppendElement(mPreviewDiv);
  }

  // NOTE(emilio): We want the root node always after the placeholder so that
  // background on the placeholder doesn't obscure the caret.
  aElements.AppendElement(mRootNode);

  rv = UpdateValueDisplay(false);
  NS_ENSURE_SUCCESS(rv, rv);

  if ((StaticPrefs::layout_forms_reveal_password_button_enabled() ||
       PresContext()->Document()->ChromeRulesEnabled()) &&
      IsPasswordTextControl() &&
      StyleDisplay()->EffectiveAppearance() != StyleAppearance::Textfield) {
    mButton =
        MakeAnonElement(PseudoStyleType::mozReveal, nullptr, nsGkAtoms::button);
    mButton->SetAttr(kNameSpaceID_None, nsGkAtoms::aria_hidden, u"true"_ns,
                     false);
    mButton->SetAttr(kNameSpaceID_None, nsGkAtoms::tabindex, u"-1"_ns, false);
    aElements.AppendElement(mButton);
  }

  InitializeEagerlyIfNeeded();
  return NS_OK;
}

bool nsTextControlFrame::ShouldInitializeEagerly() const {
  // textareas are eagerly initialized.
  if (!IsSingleLineTextControl()) {
    return true;
  }

  // Also, input elements which have a cached selection should get eager
  // editor initialization.
  TextControlElement* textControlElement = ControlElement();
  if (textControlElement->HasCachedSelection()) {
    return true;
  }

  // So do input text controls with spellcheck=true
  if (auto* htmlElement = nsGenericHTMLElement::FromNode(mContent)) {
    if (htmlElement->Spellcheck()) {
      return true;
    }
  }

  // If text in the editor is being dragged, we need the editor to create
  // new source node for the drag session (TextEditor creates the text node
  // in the anonymous <div> element.
  if (nsCOMPtr<nsIDragSession> dragSession =
          nsContentUtils::GetDragSession(PresContext())) {
    if (dragSession->IsDraggingTextInTextControl()) {
      nsCOMPtr<nsINode> sourceNode;
      if (NS_SUCCEEDED(
              dragSession->GetSourceNode(getter_AddRefs(sourceNode))) &&
          sourceNode == textControlElement) {
        return true;
      }
    }
  }

  return false;
}

void nsTextControlFrame::InitializeEagerlyIfNeeded() {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript(),
             "Someone forgot a script blocker?");
  if (!ShouldInitializeEagerly()) {
    return;
  }

  EditorInitializer* initializer = new EditorInitializer(this);
  SetProperty(TextControlInitializer(), initializer);
  nsContentUtils::AddScriptRunner(initializer);
}

void nsTextControlFrame::CreatePlaceholderIfNeeded() {
  MOZ_ASSERT(!mPlaceholderDiv);

  // Do we need a placeholder node?
  nsAutoString placeholder;
  if (!mContent->AsElement()->GetAttr(nsGkAtoms::placeholder, placeholder)) {
    return;
  }

  mPlaceholderDiv = MakeAnonDivWithTextNode(PseudoStyleType::placeholder);
  UpdatePlaceholderText(placeholder, false);
}

void nsTextControlFrame::PlaceholderChanged(const nsAttrValue* aOld,
                                            const nsAttrValue* aNew) {
  if (!aOld || !aNew) {
    return;  // This should be handled by GetAttributeChangeHint.
  }

  // If we've changed the attribute but we still haven't reframed, there's
  // nothing to do either.
  if (!mPlaceholderDiv) {
    return;
  }

  nsAutoString placeholder;
  aNew->ToString(placeholder);
  UpdatePlaceholderText(placeholder, true);
}

void nsTextControlFrame::UpdatePlaceholderText(nsString& aPlaceholder,
                                               bool aNotify) {
  MOZ_DIAGNOSTIC_ASSERT(mPlaceholderDiv);
  MOZ_DIAGNOSTIC_ASSERT(mPlaceholderDiv->GetFirstChild());

  if (IsTextArea()) {  // <textarea>s preserve newlines...
    nsContentUtils::PlatformToDOMLineBreaks(aPlaceholder);
  } else {  // ...<input>s don't
    nsContentUtils::RemoveNewlines(aPlaceholder);
  }

  mPlaceholderDiv->GetFirstChild()->AsText()->SetText(aPlaceholder, aNotify);
}

void nsTextControlFrame::CreatePreviewIfNeeded() {
  if (!ControlElement()->IsPreviewEnabled()) {
    return;
  }
  mPreviewDiv = MakeAnonDivWithTextNode(PseudoStyleType::mozTextControlPreview);
}

void nsTextControlFrame::AppendAnonymousContentTo(
    nsTArray<nsIContent*>& aElements, uint32_t aFilter) {
  if (mPlaceholderDiv && !(aFilter & nsIContent::eSkipPlaceholderContent)) {
    aElements.AppendElement(mPlaceholderDiv);
  }

  if (mPreviewDiv) {
    aElements.AppendElement(mPreviewDiv);
  }

  if (mButton) {
    aElements.AppendElement(mButton);
  }

  aElements.AppendElement(mRootNode);
}

nscoord nsTextControlFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  // Our min inline size is just our preferred inline-size if we have auto
  // inline size.
  WritingMode wm = GetWritingMode();
  return CalcIntrinsicSize(aInput.mContext, wm).ISize(wm);
}

Maybe<nscoord> nsTextControlFrame::ComputeBaseline(
    const nsIFrame* aFrame, const ReflowInput& aReflowInput,
    bool aForSingleLineControl) {
  // If we're layout-contained, we have no baseline.
  if (aReflowInput.mStyleDisplay->IsContainLayout()) {
    return Nothing();
  }
  WritingMode wm = aReflowInput.GetWritingMode();

  nscoord lineHeight = aReflowInput.ComputedBSize();
  if (!aForSingleLineControl || lineHeight == NS_UNCONSTRAINEDSIZE) {
    lineHeight = aReflowInput.ApplyMinMaxBSize(aReflowInput.GetLineHeight());
  }
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(aFrame);
  return Some(nsLayoutUtils::GetCenteredFontBaseline(fontMet, lineHeight,
                                                     wm.IsLineInverted()) +
              aReflowInput.ComputedLogicalBorderPadding(wm).BStart(wm));
}

void nsTextControlFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTextControlFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // set values of reflow's out parameters
  WritingMode wm = aReflowInput.GetWritingMode();
  const auto contentBoxSize = aReflowInput.ComputedSizeWithBSizeFallback([&] {
    return CalcIntrinsicSize(aReflowInput.mRenderingContext, wm).BSize(wm);
  });
  aDesiredSize.SetSize(
      wm,
      contentBoxSize + aReflowInput.ComputedLogicalBorderPadding(wm).Size(wm));

  {
    // Calculate the baseline and store it in mFirstBaseline.
    auto baseline =
        ComputeBaseline(this, aReflowInput, IsSingleLineTextControl());
    mFirstBaseline = baseline.valueOr(NS_INTRINSIC_ISIZE_UNKNOWN);
    if (baseline) {
      aDesiredSize.SetBlockStartAscent(*baseline);
    }
  }

  // overflow handling
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  nsIFrame* buttonBox = [&]() -> nsIFrame* {
    nsIFrame* last = mFrames.LastChild();
    if (!last || !IsButtonBox(last)) {
      return nullptr;
    }
    return last;
  }();

  // Reflow the button box first, so that we can use its size for the other
  // frames.
  nscoord buttonBoxISize = 0;
  if (buttonBox) {
    ReflowTextControlChild(buttonBox, aPresContext, aReflowInput, aStatus,
                           aDesiredSize, contentBoxSize, buttonBoxISize);
  }

  // perform reflow on all kids
  nsIFrame* kid = mFrames.FirstChild();
  while (kid) {
    if (kid != buttonBox) {
      MOZ_ASSERT(!IsButtonBox(kid),
                 "Should only have one button box, and should be last");
      ReflowTextControlChild(kid, aPresContext, aReflowInput, aStatus,
                             aDesiredSize, contentBoxSize, buttonBoxISize);
    }
    kid = kid->GetNextSibling();
  }

  // take into account css properties that affect overflow handling
  FinishAndStoreOverflow(&aDesiredSize);

  aStatus.Reset();  // This type of frame can't be split.
}

void nsTextControlFrame::ReflowTextControlChild(
    nsIFrame* aKid, nsPresContext* aPresContext,
    const ReflowInput& aReflowInput, nsReflowStatus& aStatus,
    ReflowOutput& aParentDesiredSize, const LogicalSize& aParentContentBoxSize,
    nscoord& aButtonBoxISize) {
  const WritingMode outerWM = aReflowInput.GetWritingMode();
  // compute available size and frame offsets for child
  const WritingMode wm = aKid->GetWritingMode();
  const auto parentPadding = aReflowInput.ComputedLogicalPadding(wm);
  const LogicalSize contentBoxSize =
      aParentContentBoxSize.ConvertTo(wm, outerWM);
  const LogicalSize paddingBoxSize = contentBoxSize + parentPadding.Size(wm);
  const LogicalSize borderBoxSize =
      paddingBoxSize + aReflowInput.ComputedLogicalBorder(wm).Size(wm);
  LogicalSize availSize = paddingBoxSize;
  availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;

  const bool isButtonBox = IsButtonBox(aKid);

  ReflowInput kidReflowInput(aPresContext, aReflowInput, aKid, availSize,
                             Nothing(), ReflowInput::InitFlag::CallerWillInit);

  // Override padding with our computed padding in case we got it from theming
  // or percentage, if we're not the button box.
  auto overridePadding = isButtonBox ? Nothing() : Some(parentPadding);
  if (!isButtonBox && aButtonBoxISize) {
    // Button box respects inline-end-padding, so we don't need to.
    overridePadding->IEnd(outerWM) = 0;
  }

  // We want to let our button box fill the frame in the block axis, up to the
  // edge of the control's border. So, we use the control's padding-box as the
  // containing block size for our button box.
  auto overrideCBSize = isButtonBox ? Some(paddingBoxSize) : Nothing();
  kidReflowInput.Init(aPresContext, overrideCBSize, Nothing(), overridePadding);

  LogicalPoint position(wm);
  if (!isButtonBox) {
    MOZ_ASSERT(wm == outerWM,
               "Shouldn't have to care about orthogonal "
               "writing-modes and such inside the control, "
               "except for the number spin-box which forces "
               "horizontal-tb");

    const auto& border = aReflowInput.ComputedLogicalBorder(wm);

    // Offset the frame by the size of the parent's border. Note that we don't
    // have to account for the parent's padding here, because this child
    // actually "inherits" that padding and manages it on behalf of the parent.
    position.B(wm) = border.BStart(wm);
    position.I(wm) = border.IStart(wm);

    // Set computed width and computed height for the child (the button box is
    // the only exception, which has an auto size).
    kidReflowInput.SetComputedISize(
        std::max(0, aReflowInput.ComputedISize() - aButtonBoxISize));
    kidReflowInput.SetComputedBSize(contentBoxSize.BSize(wm));
  }

  // reflow the child
  ReflowOutput desiredSize(aReflowInput);
  const nsSize containerSize = borderBoxSize.GetPhysicalSize(wm);
  ReflowChild(aKid, aPresContext, desiredSize, kidReflowInput, wm, position,
              containerSize, ReflowChildFlags::Default, aStatus);

  if (isButtonBox) {
    const auto& bp = aReflowInput.ComputedLogicalBorderPadding(outerWM);
    auto size = desiredSize.Size(outerWM);
    // Center button in the block axis of our content box. We do this
    // computation in terms of outerWM for simplicity.
    LogicalRect buttonRect(outerWM);
    buttonRect.BSize(outerWM) = size.BSize(outerWM);
    buttonRect.ISize(outerWM) = size.ISize(outerWM);
    buttonRect.BStart(outerWM) =
        bp.BStart(outerWM) +
        (aParentContentBoxSize.BSize(outerWM) - size.BSize(outerWM)) / 2;
    // Align to the inline-end of the content box.
    buttonRect.IStart(outerWM) =
        bp.IStart(outerWM) + aReflowInput.ComputedISize() - size.ISize(outerWM);
    buttonRect = buttonRect.ConvertTo(wm, outerWM, containerSize);
    position = buttonRect.Origin(wm);
    aButtonBoxISize = size.ISize(outerWM);
  }

  // place the child
  FinishReflowChild(aKid, aPresContext, desiredSize, &kidReflowInput, wm,
                    position, containerSize, ReflowChildFlags::Default);

  // consider the overflow
  aParentDesiredSize.mOverflowAreas.UnionWith(desiredSize.mOverflowAreas);
}

// IMPLEMENTING NS_IFORMCONTROLFRAME
void nsTextControlFrame::OnFocus() {
  nsISelectionController* selCon = GetSelectionController();
  if (!selCon) {
    return;
  }

  RefPtr<Selection> ourSel =
      selCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  if (!ourSel) {
    return;
  }

  mozilla::PresShell* presShell = PresShell();
  RefPtr<nsCaret> caret = presShell->GetCaret();
  if (!caret) {
    return;
  }

  // Tell the caret to use our selection
  caret->SetSelection(ourSel);

  // mutual-exclusion: the selection is either controlled by the
  // document or by the text input/area. Clear any selection in the
  // document since the focus is now on our independent selection.

  RefPtr<Selection> docSel =
      presShell->GetSelection(nsISelectionController::SELECTION_NORMAL);
  if (!docSel) {
    return;
  }

  if (!docSel->IsCollapsed()) {
    docSel->RemoveAllRanges(IgnoreErrors());
  }

  // If the focus moved to a text control during text selection by pointer
  // device, stop extending the selection.
  if (RefPtr<nsFrameSelection> frameSelection = presShell->FrameSelection()) {
    frameSelection->SetDragState(false);
  }
}

already_AddRefed<TextEditor> nsTextControlFrame::GetTextEditor() {
  if (NS_WARN_IF(NS_FAILED(EnsureEditorInitialized()))) {
    return nullptr;
  }
  RefPtr el = ControlElement();
  return do_AddRef(el->GetTextEditor());
}

nsresult nsTextControlFrame::SetSelectionInternal(
    nsINode* aStartNode, uint32_t aStartOffset, nsINode* aEndNode,
    uint32_t aEndOffset, SelectionDirection aDirection) {
  // Get the selection, clear it and add the new range to it!
  nsISelectionController* selCon = GetSelectionController();
  NS_ENSURE_TRUE(selCon, NS_ERROR_FAILURE);

  RefPtr<Selection> selection =
      selCon->GetSelection(nsISelectionController::SELECTION_NORMAL);
  NS_ENSURE_TRUE(selection, NS_ERROR_FAILURE);

  nsDirection direction;
  if (aDirection == SelectionDirection::None) {
    // Preserve the direction
    direction = selection->GetDirection();
  } else {
    direction =
        aDirection == SelectionDirection::Backward ? eDirPrevious : eDirNext;
  }

  MOZ_TRY(selection->SetStartAndEndInLimiter(*aStartNode, aStartOffset,
                                             *aEndNode, aEndOffset, direction,
                                             nsISelectionListener::JS_REASON));
  return NS_OK;
}

void nsTextControlFrame::ScrollSelectionIntoViewAsync(
    ScrollAncestors aScrollAncestors) {
  nsCOMPtr<nsISelectionController> selCon = GetSelectionController();
  if (!selCon) {
    return;
  }

  // Scroll the selection into view (see bug 231389).
  const auto flags = aScrollAncestors == ScrollAncestors::Yes
                         ? ScrollFlags::None
                         : ScrollFlags::ScrollFirstAncestorOnly;
  selCon->ScrollSelectionIntoView(
      SelectionType::eNormal, nsISelectionController::SELECTION_FOCUS_REGION,
      ScrollAxis(), ScrollAxis(), flags);
}

nsresult nsTextControlFrame::SelectAll() {
  nsresult rv = EnsureEditorInitialized();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr<nsINode> rootNode = mRootNode;
  NS_ENSURE_TRUE(rootNode, NS_ERROR_FAILURE);

  RefPtr<Text> text = Text::FromNodeOrNull(rootNode->GetFirstChild());
  MOZ_ASSERT(text);

  uint32_t length = text->Length();

  rv = SetSelectionInternal(text, 0, text, length, SelectionDirection::None);
  NS_ENSURE_SUCCESS(rv, rv);

  ScrollSelectionIntoViewAsync();
  return NS_OK;
}

nsresult nsTextControlFrame::SetSelectionEndPoints(
    uint32_t aSelStart, uint32_t aSelEnd, SelectionDirection aDirection) {
  NS_ASSERTION(aSelStart <= aSelEnd, "Invalid selection offsets!");

  if (aSelStart > aSelEnd) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsINode> startNode, endNode;
  uint32_t startOffset, endOffset;

  // Calculate the selection start point.

  nsresult rv =
      OffsetToDOMPoint(aSelStart, getter_AddRefs(startNode), &startOffset);

  NS_ENSURE_SUCCESS(rv, rv);

  if (aSelStart == aSelEnd) {
    // Collapsed selection, so start and end are the same!
    endNode = startNode;
    endOffset = startOffset;
  } else {
    // Selection isn't collapsed so we have to calculate
    // the end point too.

    rv = OffsetToDOMPoint(aSelEnd, getter_AddRefs(endNode), &endOffset);

    NS_ENSURE_SUCCESS(rv, rv);
  }

  return SetSelectionInternal(startNode, startOffset, endNode, endOffset,
                              aDirection);
}

NS_IMETHODIMP
nsTextControlFrame::SetSelectionRange(uint32_t aSelStart, uint32_t aSelEnd,
                                      SelectionDirection aDirection) {
  nsresult rv = EnsureEditorInitialized();
  NS_ENSURE_SUCCESS(rv, rv);

  if (aSelStart > aSelEnd) {
    // Simulate what we'd see SetSelectionStart() was called, followed
    // by a SetSelectionEnd().

    aSelStart = aSelEnd;
  }

  return SetSelectionEndPoints(aSelStart, aSelEnd, aDirection);
}

nsresult nsTextControlFrame::OffsetToDOMPoint(uint32_t aOffset,
                                              nsINode** aResult,
                                              uint32_t* aPosition) {
  NS_ENSURE_ARG_POINTER(aResult && aPosition);

  *aResult = nullptr;
  *aPosition = 0;

  nsresult rv = EnsureEditorInitialized();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr<Element> rootNode = mRootNode;
  NS_ENSURE_TRUE(rootNode, NS_ERROR_FAILURE);

  nsCOMPtr<nsINodeList> nodeList = rootNode->ChildNodes();
  uint32_t length = nodeList->Length();

  NS_ASSERTION(length <= 2,
               "We should have one text node and one mozBR at most");

  nsCOMPtr<nsINode> firstNode = nodeList->Item(0);
  Text* textNode = firstNode ? firstNode->GetAsText() : nullptr;

  if (length == 0) {
    rootNode.forget(aResult);
    *aPosition = 0;
  } else if (textNode) {
    uint32_t textLength = textNode->Length();
    firstNode.forget(aResult);
    *aPosition = std::min(aOffset, textLength);
  } else {
    rootNode.forget(aResult);
    *aPosition = 0;
  }

  return NS_OK;
}

/////END INTERFACE IMPLEMENTATIONS

////NSIFRAME
nsresult nsTextControlFrame::AttributeChanged(int32_t aNameSpaceID,
                                              nsAtom* aAttribute,
                                              int32_t aModType) {
  if (aAttribute == nsGkAtoms::value && !mEditorHasBeenInitialized) {
    UpdateValueDisplay(true);
    return NS_OK;
  }

  if (aAttribute == nsGkAtoms::maxlength) {
    if (RefPtr<TextEditor> textEditor = GetTextEditor()) {
      textEditor->SetMaxTextLength(ControlElement()->UsedMaxLength());
      return NS_OK;
    }
  }
  return nsContainerFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

void nsTextControlFrame::HandleReadonlyOrDisabledChange() {
  RefPtr<TextControlElement> el = ControlElement();
  const RefPtr<TextEditor> editor = el->GetExtantTextEditor();
  if (!editor) {
    return;
  }
  nsISelectionController* const selCon = el->GetSelectionController();
  if (!selCon) {
    return;
  }
  if (el->IsDisabledOrReadOnly()) {
    if (nsFocusManager::GetFocusedElementStatic() == el) {
      selCon->SetCaretEnabled(false);
    }
    editor->AddFlags(nsIEditor::eEditorReadonlyMask);
  } else {
    if (nsFocusManager::GetFocusedElementStatic() == el) {
      selCon->SetCaretEnabled(true);
    }
    editor->RemoveFlags(nsIEditor::eEditorReadonlyMask);
  }
}

void nsTextControlFrame::ElementStateChanged(dom::ElementState aStates) {
  if (aStates.HasAtLeastOneOfStates(dom::ElementState::READONLY |
                                    dom::ElementState::DISABLED)) {
    HandleReadonlyOrDisabledChange();
  }
  if (aStates.HasState(dom::ElementState::FOCUS) &&
      mContent->AsElement()->State().HasState(dom::ElementState::FOCUS)) {
    OnFocus();
  }
  return nsContainerFrame::ElementStateChanged(aStates);
}

/// END NSIFRAME OVERLOADS

// NOTE(emilio): This is needed because the root->primary frame map is not set
// up by the time this is called.
static nsIFrame* FindRootNodeFrame(const nsFrameList& aChildList,
                                   const nsIContent* aRoot) {
  for (nsIFrame* f : aChildList) {
    if (f->GetContent() == aRoot) {
      return f;
    }
    if (nsIFrame* root = FindRootNodeFrame(f->PrincipalChildList(), aRoot)) {
      return root;
    }
  }
  return nullptr;
}

void nsTextControlFrame::SetInitialChildList(ChildListID aListID,
                                             nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  if (aListID != FrameChildListID::Principal) {
    return;
  }

  // Mark the scroll frame as being a reflow root. This will allow incremental
  // reflows to be initiated at the scroll frame, rather than descending from
  // the root frame of the frame hierarchy.
  if (nsIFrame* frame = FindRootNodeFrame(PrincipalChildList(), mRootNode)) {
    frame->AddStateBits(NS_FRAME_REFLOW_ROOT);

    ControlElement()->InitializeKeyboardEventListeners();

    bool hasProperty;
    nsPoint contentScrollPos = TakeProperty(ContentScrollPos(), &hasProperty);
    if (hasProperty) {
      // If we have a scroll pos stored to be passed to our anonymous
      // div, do it here!
      nsIStatefulFrame* statefulFrame = do_QueryFrame(frame);
      NS_ASSERTION(statefulFrame,
                   "unexpected type of frame for the anonymous div");
      UniquePtr<PresState> fakePresState = NewPresState();
      fakePresState->scrollState() = contentScrollPos;
      statefulFrame->RestoreState(fakePresState.get());
    }
  } else {
    MOZ_ASSERT(!mRootNode || PrincipalChildList().IsEmpty());
  }
}

nsresult nsTextControlFrame::UpdateValueDisplay(bool aNotify,
                                                bool aBeforeEditorInit,
                                                const nsAString* aValue) {
  if (!IsSingleLineTextControl()) {  // textareas don't use this
    return NS_OK;
  }

  MOZ_ASSERT(mRootNode, "Must have a div content\n");
  MOZ_ASSERT(!mEditorHasBeenInitialized,
             "Do not call this after editor has been initialized");

  nsIContent* childContent = mRootNode->GetFirstChild();
  Text* textContent;
  if (!childContent) {
    // Set up a textnode with our value
    RefPtr<nsTextNode> textNode = new (mContent->NodeInfo()->NodeInfoManager())
        nsTextNode(mContent->NodeInfo()->NodeInfoManager());
    textNode->MarkAsMaybeModifiedFrequently();
    if (IsPasswordTextControl()) {
      textNode->MarkAsMaybeMasked();
    }
    mRootNode->AppendChildTo(textNode, aNotify, IgnoreErrors());
    textContent = textNode;
  } else {
    textContent = childContent->GetAsText();
  }

  NS_ENSURE_TRUE(textContent, NS_ERROR_UNEXPECTED);

  // Get the current value of the textfield from the content.
  nsAutoString value;
  if (aValue) {
    value = *aValue;
  } else {
    ControlElement()->GetTextEditorValue(value);
  }

  return textContent->SetText(value, aNotify);
}

NS_IMETHODIMP
nsTextControlFrame::GetOwnedSelectionController(
    nsISelectionController** aSelCon) {
  NS_ENSURE_ARG_POINTER(aSelCon);
  NS_IF_ADDREF(*aSelCon = GetSelectionController());
  return NS_OK;
}

UniquePtr<PresState> nsTextControlFrame::SaveState() {
  if (nsIStatefulFrame* scrollStateFrame = GetScrollTargetFrame()) {
    return scrollStateFrame->SaveState();
  }

  return nullptr;
}

NS_IMETHODIMP
nsTextControlFrame::RestoreState(PresState* aState) {
  NS_ENSURE_ARG_POINTER(aState);

  if (nsIStatefulFrame* scrollStateFrame = GetScrollTargetFrame()) {
    return scrollStateFrame->RestoreState(aState);
  }

  // Most likely, we don't have our anonymous content constructed yet, which
  // would cause us to end up here.  In this case, we'll just store the scroll
  // pos ourselves, and forward it to the scroll frame later when it's created.
  SetProperty(ContentScrollPos(), aState->scrollState());
  return NS_OK;
}

nsresult nsTextControlFrame::PeekOffset(PeekOffsetStruct* aPos) {
  return NS_ERROR_FAILURE;
}

void nsTextControlFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  DO_GLOBAL_REFLOW_COUNT_DSP("nsTextControlFrame");

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  // Redirect all lists to the Content list so that nothing can escape, ie
  // opacity creating stacking contexts that then get sorted with stacking
  // contexts external to us.
  nsDisplayList* content = aLists.Content();
  nsDisplayListSet set(content, content, content, content, content, content);

  for (auto* kid : mFrames) {
    BuildDisplayListForChild(aBuilder, kid, set);
  }
}

NS_IMETHODIMP
nsTextControlFrame::EditorInitializer::Run() {
  if (!mFrame) {
    return NS_OK;
  }

  // Need to block script to avoid bug 669767.
  nsAutoScriptBlocker scriptBlocker;

  RefPtr<mozilla::PresShell> presShell = mFrame->PresShell();
  bool observes = presShell->ObservesNativeAnonMutationsForPrint();
  presShell->ObserveNativeAnonMutationsForPrint(true);
  // This can cause the frame to be destroyed (and call Revoke()).
  mFrame->EnsureEditorInitialized();
  presShell->ObserveNativeAnonMutationsForPrint(observes);

  // The frame can *still* be destroyed even though we have a scriptblocker,
  // bug 682684.
  if (!mFrame) {
    return NS_ERROR_FAILURE;
  }

  // If there is a drag session which is for dragging text in a text control
  // and its source node is the text control element, we're being reframed.
  // In this case we should restore the source node of the drag session to
  // new text node because it's required for dispatching `dragend` event.
  if (nsCOMPtr<nsIDragSession> dragSession =
          nsContentUtils::GetDragSession(mFrame->PresContext())) {
    if (dragSession->IsDraggingTextInTextControl()) {
      nsCOMPtr<nsINode> sourceNode;
      if (NS_SUCCEEDED(
              dragSession->GetSourceNode(getter_AddRefs(sourceNode))) &&
          mFrame->GetContent() == sourceNode) {
        if (const TextEditor* const textEditor =
                mFrame->ControlElement()->GetExtantTextEditor()) {
          if (Element* anonymousDivElement = textEditor->GetRoot()) {
            if (anonymousDivElement && anonymousDivElement->GetFirstChild()) {
              MOZ_ASSERT(anonymousDivElement->GetFirstChild()->IsText());
              dragSession->UpdateSource(anonymousDivElement->GetFirstChild(),
                                        textEditor->GetSelection());
            }
          }
        }
      }
    }
  }
  // Otherwise, EventStateManager may be tracking gesture to start a drag.
  else {
    TextControlElement* textControlElement = mFrame->ControlElement();
    if (nsPresContext* presContext =
            textControlElement->GetPresContext(Element::eForComposedDoc)) {
      if (const TextEditor* const textEditor =
              textControlElement->GetExtantTextEditor()) {
        if (Element* anonymousDivElement = textEditor->GetRoot()) {
          presContext->EventStateManager()->TextControlRootAdded(
              *anonymousDivElement, *textControlElement);
        }
      }
    }
  }

  mFrame->FinishedInitializer();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsTextControlFrame::nsAnonDivObserver, nsIMutationObserver)

void nsTextControlFrame::nsAnonDivObserver::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo&) {
  mFrame.ClearCachedValue();
}

void nsTextControlFrame::nsAnonDivObserver::ContentAppended(
    nsIContent* aFirstNewContent, const ContentAppendInfo&) {
  mFrame.ClearCachedValue();
}

void nsTextControlFrame::nsAnonDivObserver::ContentInserted(
    nsIContent* aChild, const ContentInsertInfo&) {
  mFrame.ClearCachedValue();
}

void nsTextControlFrame::nsAnonDivObserver::ContentWillBeRemoved(
    nsIContent* aChild, const ContentRemoveInfo&) {
  mFrame.ClearCachedValue();
}

Maybe<nscoord> nsTextControlFrame::GetNaturalBaselineBOffset(
    mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  if (!IsSingleLineTextControl()) {
    if (StyleDisplay()->IsContainLayout()) {
      return Nothing{};
    }

    if (aBaselineGroup == BaselineSharingGroup::First) {
      return Some(CSSMinMax(mFirstBaseline, 0, BSize(aWM)));
    }
    // This isn't great, but the content of the root NAC isn't guaranteed
    // to be loaded, so the best we can do is the edge of the border-box.
    if (aWM.IsCentralBaseline()) {
      return Some(BSize(aWM) / 2);
    }
    return Some(0);
  }
  NS_ASSERTION(!IsSubtreeDirty(), "frame must not be dirty");
  return GetSingleLineTextControlBaseline(this, mFirstBaseline, aWM,
                                          aBaselineGroup);
}
