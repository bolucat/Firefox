/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Navigation.h"

#include "jsapi.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedUniquePtr.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FeaturePolicy.h"
#include "mozilla/dom/NavigationActivation.h"
#include "mozilla/dom/NavigationCurrentEntryChangeEvent.h"
#include "mozilla/dom/NavigationHistoryEntry.h"
#include "mozilla/dom/NavigationTransition.h"
#include "mozilla/dom/NavigationUtils.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"
#include "nsIPrincipal.h"
#include "nsISHistory.h"
#include "nsIStructuredCloneContainer.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsTHashtable.h"

mozilla::LazyLogModule gNavigationLog("Navigation");

#define LOG_FMT(format, ...) \
  MOZ_LOG_FMT(gNavigationLog, LogLevel::Debug, format, ##__VA_ARGS__);

namespace mozilla::dom {

struct NavigationAPIMethodTracker final : public nsISupports {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(NavigationAPIMethodTracker)

  NavigationAPIMethodTracker(Navigation* aNavigationObject,
                             const Maybe<nsID> aKey, const JS::Value& aInfo,
                             nsIStructuredCloneContainer* aSerializedState,
                             NavigationHistoryEntry* aCommittedToEntry,
                             Promise* aCommittedPromise,
                             Promise* aFinishedPromise)
      : mNavigationObject(aNavigationObject),
        mKey(aKey),
        mInfo(aInfo),
        mSerializedState(aSerializedState),
        mCommittedToEntry(aCommittedToEntry),
        mCommittedPromise(aCommittedPromise),
        mFinishedPromise(aFinishedPromise) {
    mozilla::HoldJSObjects(this);
  }

  // https://html.spec.whatwg.org/#navigation-api-method-tracker-clean-up
  void CleanUp() { Navigation::CleanUp(this); }

  // https://html.spec.whatwg.org/#notify-about-the-committed-to-entry
  void NotifyAboutCommittedToEntry(NavigationHistoryEntry* aNHE) {
    MOZ_DIAGNOSTIC_ASSERT(mCommittedPromise);
    // Step 1
    mCommittedToEntry = aNHE;
    if (mSerializedState) {
      // Step 2
      aNHE->SetState(
          static_cast<nsStructuredCloneContainer*>(mSerializedState.get()));
      // At this point, apiMethodTracker's serialized state is no longer needed.
      // We drop it do now for efficiency.
      mSerializedState = nullptr;
    }
    mCommittedPromise->MaybeResolve(aNHE);
  }

  // https://html.spec.whatwg.org/#resolve-the-finished-promise
  void ResolveFinishedPromise() {
    MOZ_DIAGNOSTIC_ASSERT(mFinishedPromise);
    // Step 1
    MOZ_DIAGNOSTIC_ASSERT(mCommittedToEntry);
    // Step 2
    mFinishedPromise->MaybeResolve(mCommittedToEntry);
    // Step 3
    CleanUp();
  }

  // https://html.spec.whatwg.org/#reject-the-finished-promise
  void RejectFinishedPromise(JS::Handle<JS::Value> aException) {
    MOZ_DIAGNOSTIC_ASSERT(mFinishedPromise);
    MOZ_DIAGNOSTIC_ASSERT(mCommittedPromise);
    // Step 1
    mCommittedPromise->MaybeReject(aException);
    // Step 2
    mFinishedPromise->MaybeReject(aException);
    // Step 3
    CleanUp();
  }

  RefPtr<Navigation> mNavigationObject;
  Maybe<nsID> mKey;
  JS::Heap<JS::Value> mInfo;
  RefPtr<nsIStructuredCloneContainer> mSerializedState;
  RefPtr<NavigationHistoryEntry> mCommittedToEntry;
  RefPtr<Promise> mCommittedPromise;
  RefPtr<Promise> mFinishedPromise;

 private:
  ~NavigationAPIMethodTracker() { mozilla::DropJSObjects(this); };
};

NS_IMPL_CYCLE_COLLECTION_WITH_JS_MEMBERS(NavigationAPIMethodTracker,
                                         (mNavigationObject, mSerializedState,
                                          mCommittedToEntry, mCommittedPromise,
                                          mFinishedPromise),
                                         (mInfo))

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(NavigationAPIMethodTracker)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(NavigationAPIMethodTracker)
NS_IMPL_CYCLE_COLLECTING_RELEASE(NavigationAPIMethodTracker)

NS_IMPL_CYCLE_COLLECTION_INHERITED(Navigation, DOMEventTargetHelper, mEntries,
                                   mOngoingNavigateEvent, mTransition,
                                   mActivation, mOngoingAPIMethodTracker,
                                   mUpcomingNonTraverseAPIMethodTracker,
                                   mUpcomingTraverseAPIMethodTrackers);
NS_IMPL_ADDREF_INHERITED(Navigation, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(Navigation, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Navigation)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

Navigation::Navigation(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow) {
  MOZ_ASSERT(aWindow);
}

JSObject* Navigation::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return Navigation_Binding::Wrap(aCx, this, aGivenProto);
}

void Navigation::EventListenerAdded(nsAtom* aType) {
  if (nsPIDOMWindowInner* window = GetOwnerWindow()) {
    if (WindowGlobalChild* windowGlobal = window->GetWindowGlobalChild()) {
      windowGlobal->NavigateAdded();
    }
  }

  EventTarget::EventListenerAdded(aType);
}

void Navigation::EventListenerRemoved(nsAtom* aType) {
  if (nsPIDOMWindowInner* window = GetOwnerWindow()) {
    if (WindowGlobalChild* windowGlobal = window->GetWindowGlobalChild()) {
      windowGlobal->NavigateRemoved();
    }
  }

  EventTarget::EventListenerRemoved(aType);
}

/* static */
bool Navigation::IsAPIEnabled(JSContext* /* unused */, JSObject* /* unused */) {
  return SessionHistoryInParent() &&
         StaticPrefs::dom_navigation_webidl_enabled_DoNotUseDirectly();
}

void Navigation::Entries(
    nsTArray<RefPtr<NavigationHistoryEntry>>& aResult) const {
  aResult = mEntries.Clone();
}

already_AddRefed<NavigationHistoryEntry> Navigation::GetCurrentEntry() const {
  if (HasEntriesAndEventsDisabled()) {
    return nullptr;
  }

  if (!mCurrentEntryIndex) {
    return nullptr;
  }

  MOZ_LOG(gNavigationLog, LogLevel::Debug,
          ("Current Entry: %d; Amount of Entries: %d", int(*mCurrentEntryIndex),
           int(mEntries.Length())));
  MOZ_ASSERT(*mCurrentEntryIndex < mEntries.Length());

  RefPtr entry{mEntries[*mCurrentEntryIndex]};
  return entry.forget();
}

// https://html.spec.whatwg.org/#dom-navigation-updatecurrententry
void Navigation::UpdateCurrentEntry(
    JSContext* aCx, const NavigationUpdateCurrentEntryOptions& aOptions,
    ErrorResult& aRv) {
  RefPtr currentEntry(GetCurrentEntry());
  if (!currentEntry) {
    aRv.ThrowInvalidStateError(
        "Can't call updateCurrentEntry without a valid entry.");
    return;
  }

  JS::Rooted<JS::Value> state(aCx, aOptions.mState);
  auto serializedState = MakeRefPtr<nsStructuredCloneContainer>();
  nsresult rv = serializedState->InitFromJSVal(state, aCx);
  if (NS_FAILED(rv)) {
    aRv.ThrowDataCloneError(
        "Failed to serialize value for updateCurrentEntry.");
    return;
  }

  currentEntry->SetState(serializedState);

  NavigationCurrentEntryChangeEventInit init;
  init.mFrom = currentEntry;
  // Leaving the navigation type unspecified means it will be initialized to
  // null.
  RefPtr event = NavigationCurrentEntryChangeEvent::Constructor(
      this, u"currententrychange"_ns, init);
  DispatchEvent(*event);
}

NavigationTransition* Navigation::GetTransition() const { return mTransition; }

NavigationActivation* Navigation::GetActivation() const { return mActivation; }

// https://html.spec.whatwg.org/#has-entries-and-events-disabled
bool Navigation::HasEntriesAndEventsDisabled() const {
  Document* doc = GetAssociatedDocument();
  return !doc || !doc->IsCurrentActiveDocument() ||
         (NS_IsAboutBlankAllowQueryAndFragment(doc->GetDocumentURI()) &&
          doc->IsInitialDocument()) ||
         doc->GetPrincipal()->GetIsNullPrincipal();
}

// https://html.spec.whatwg.org/#initialize-the-navigation-api-entries-for-a-new-document
void Navigation::InitializeHistoryEntries(
    mozilla::Span<const SessionHistoryInfo> aNewSHInfos,
    const SessionHistoryInfo* aInitialSHInfo) {
  LOG_FMT("Attempting to initialize history entries for {}.",
          aInitialSHInfo->GetURI()
              ? aInitialSHInfo->GetURI()->GetSpecOrDefault()
              : "<no uri>"_ns)

  mEntries.Clear();
  mCurrentEntryIndex.reset();
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  for (auto i = 0ul; i < aNewSHInfos.Length(); i++) {
    mEntries.AppendElement(MakeRefPtr<NavigationHistoryEntry>(
        GetOwnerGlobal(), &aNewSHInfos[i], i));
    if (aNewSHInfos[i].NavigationKey() == aInitialSHInfo->NavigationKey()) {
      mCurrentEntryIndex = Some(i);
    }
  }

  LogHistory();

  nsID key = aInitialSHInfo->NavigationKey();
  nsID id = aInitialSHInfo->NavigationId();
  MOZ_LOG(
      gNavigationLog, LogLevel::Debug,
      ("aInitialSHInfo: %s %s\n", key.ToString().get(), id.ToString().get()));
}

// https://html.spec.whatwg.org/#update-the-navigation-api-entries-for-a-same-document-navigation
void Navigation::UpdateEntriesForSameDocumentNavigation(
    SessionHistoryInfo* aDestinationSHE, NavigationType aNavigationType) {
  // Step 1.
  if (HasEntriesAndEventsDisabled()) {
    return;
  }

  MOZ_LOG(gNavigationLog, LogLevel::Debug,
          ("Updating entries for same-document navigation"));

  // Steps 2-7.
  RefPtr<NavigationHistoryEntry> oldCurrentEntry = GetCurrentEntry();
  nsTArray<RefPtr<NavigationHistoryEntry>> disposedEntries;
  switch (aNavigationType) {
    case NavigationType::Traverse:
      MOZ_LOG(gNavigationLog, LogLevel::Debug, ("Traverse navigation"));
      mCurrentEntryIndex.reset();
      for (auto i = 0ul; i < mEntries.Length(); i++) {
        if (mEntries[i]->IsSameEntry(aDestinationSHE)) {
          mCurrentEntryIndex = Some(i);
          break;
        }
      }
      MOZ_ASSERT(mCurrentEntryIndex);
      break;

    case NavigationType::Push:
      MOZ_LOG(gNavigationLog, LogLevel::Debug, ("Push navigation"));
      mCurrentEntryIndex =
          Some(mCurrentEntryIndex ? *mCurrentEntryIndex + 1 : 0);
      while (*mCurrentEntryIndex < mEntries.Length()) {
        disposedEntries.AppendElement(mEntries.PopLastElement());
      }
      mEntries.AppendElement(MakeRefPtr<NavigationHistoryEntry>(
          GetOwnerGlobal(), aDestinationSHE, *mCurrentEntryIndex));
      break;

    case NavigationType::Replace:
      MOZ_LOG(gNavigationLog, LogLevel::Debug, ("Replace navigation"));
      disposedEntries.AppendElement(oldCurrentEntry);
      aDestinationSHE->NavigationKey() = oldCurrentEntry->Key();
      mEntries[*mCurrentEntryIndex] = MakeRefPtr<NavigationHistoryEntry>(
          GetOwnerGlobal(), aDestinationSHE, *mCurrentEntryIndex);
      break;

    case NavigationType::Reload:
      break;
  }

  // Step 8.
  if (mOngoingAPIMethodTracker) {
    RefPtr<NavigationHistoryEntry> currentEntry = GetCurrentEntry();
    mOngoingAPIMethodTracker->NotifyAboutCommittedToEntry(currentEntry);
  }

  // Steps 9-12.
  {
    nsAutoMicroTask mt;
    AutoEntryScript aes(GetOwnerGlobal(),
                        "UpdateEntriesForSameDocumentNavigation");

    ScheduleEventsFromNavigation(aNavigationType, oldCurrentEntry,
                                 std::move(disposedEntries));
  }
}

// https://html.spec.whatwg.org/#update-the-navigation-api-entries-for-reactivation
void Navigation::UpdateForReactivation(SessionHistoryInfo* aReactivatedEntry) {
  // NAV-TODO
}

void Navigation::ScheduleEventsFromNavigation(
    NavigationType aType, const RefPtr<NavigationHistoryEntry>& aPreviousEntry,
    nsTArray<RefPtr<NavigationHistoryEntry>>&& aDisposedEntries) {
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "mozilla::dom::Navigation::ScheduleEventsFromNavigation",
      [self = RefPtr(this), previousEntry = RefPtr(aPreviousEntry),
       disposedEntries = std::move(aDisposedEntries), aType]() {
        if (previousEntry) {
          NavigationCurrentEntryChangeEventInit init;
          init.mFrom = previousEntry;
          init.mNavigationType.SetValue(aType);
          RefPtr event = NavigationCurrentEntryChangeEvent::Constructor(
              self, u"currententrychange"_ns, init);
          self->DispatchEvent(*event);
        }

        for (const auto& entry : disposedEntries) {
          RefPtr<Event> event = NS_NewDOMEvent(entry, nullptr, nullptr);
          event->InitEvent(u"dispose"_ns, false, false);
          event->SetTrusted(true);
          event->SetTarget(entry);
          entry->DispatchEvent(*event);
        }
      }));
}

// https://html.spec.whatwg.org/#navigation-api-early-error-result
void Navigation::SetEarlyErrorResult(JSContext* aCx, NavigationResult& aResult,
                                     ErrorResult&& aRv) const {
  MOZ_ASSERT(aRv.Failed());
  // An early error result for an exception e is a NavigationResult dictionary
  // instance given by
  // «[ "committed" → a promise rejected with e,
  //    "finished" → a promise rejected with e ]».

  RefPtr global = GetOwnerGlobal();
  if (!global) {
    // Creating a promise should only fail if there is no global.
    // In this case, the only solution is to ignore the error.
    aRv.SuppressException();
    return;
  }
  JS::Rooted<JS::Value> rootedExceptionValue(aCx);
  MOZ_ALWAYS_TRUE(ToJSValue(aCx, std::move(aRv), &rootedExceptionValue));
  aResult.mCommitted.Reset();
  aResult.mCommitted.Construct(Promise::CreateInfallible(global));
  aResult.mCommitted.Value()->MaybeReject(rootedExceptionValue);

  aResult.mFinished.Reset();
  aResult.mFinished.Construct(Promise::CreateInfallible(global));
  aResult.mFinished.Value()->MaybeReject(rootedExceptionValue);
}

// https://html.spec.whatwg.org/#navigation-api-method-tracker-derived-result
static void CreateResultFromAPIMethodTracker(
    NavigationAPIMethodTracker* aApiMethodTracker, NavigationResult& aResult) {
  // A navigation API method tracker-derived result for a navigation API
  // method tracker is a NavigationResult dictionary instance given by
  // «[ "committed" → apiMethodTracker's committed promise,
  //    "finished" → apiMethodTracker's finished promise ]».
  MOZ_ASSERT(aApiMethodTracker);
  aResult.mCommitted.Reset();
  aResult.mCommitted.Construct(
      OwningNonNull<Promise>(*aApiMethodTracker->mCommittedPromise));
  aResult.mFinished.Reset();
  aResult.mFinished.Construct(
      OwningNonNull<Promise>(*aApiMethodTracker->mFinishedPromise));
}

bool Navigation::CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const Document* aDocument,
    NavigationResult& aResult) const {
  if (!aDocument || !aDocument->IsFullyActive()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Document is not fully active");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return false;
  }
  return true;
}

bool Navigation::CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const Document* aDocument,
    NavigationResult& aResult) const {
  if (!aDocument || aDocument->ShouldIgnoreOpens()) {
    ErrorResult rv;
    rv.ThrowInvalidStateError("Document is unloading");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return false;
  }
  return true;
}

already_AddRefed<nsIStructuredCloneContainer>
Navigation::CreateSerializedStateAndMaybeSetEarlyErrorResult(
    JSContext* aCx, const JS::Value& aState, NavigationResult& aResult) const {
  JS::Rooted<JS::Value> state(aCx, aState);
  RefPtr global = GetOwnerGlobal();
  MOZ_DIAGNOSTIC_ASSERT(global);

  RefPtr<nsIStructuredCloneContainer> serializedState =
      new nsStructuredCloneContainer();
  const nsresult rv = serializedState->InitFromJSVal(state, aCx);
  if (NS_FAILED(rv)) {
    JS::Rooted<JS::Value> exception(aCx);
    if (JS_GetPendingException(aCx, &exception)) {
      JS_ClearPendingException(aCx);
      aResult.mCommitted.Reset();
      aResult.mCommitted.Construct(
          Promise::Reject(global, exception, IgnoreErrors()));
      aResult.mFinished.Reset();
      aResult.mFinished.Construct(
          Promise::Reject(global, exception, IgnoreErrors()));
      return nullptr;
    }
    SetEarlyErrorResult(aCx, aResult, ErrorResult(rv));
    return nullptr;
  }
  return serializedState.forget();
}

// https://html.spec.whatwg.org/#dom-navigation-navigate
void Navigation::Navigate(JSContext* aCx, const nsAString& aUrl,
                          const NavigationNavigateOptions& aOptions,
                          NavigationResult& aResult) {
  // 3. Let document be this's relevant global object's associated Document.
  const RefPtr<Document> document = GetAssociatedDocument();
  if (!document) {
    return;
  }
  // 1. Let urlRecord be the result of parsing a URL given url, relative to
  //    this's relevant settings object.
  RefPtr<nsIURI> urlRecord;
  nsresult res = NS_NewURI(getter_AddRefs(urlRecord), aUrl, nullptr,
                           document->GetDocBaseURI());
  if (NS_FAILED(res)) {
    // 2. If urlRecord is failure, then return an early error result for a
    //    "SyntaxError" DOMException.
    ErrorResult rv;
    rv.ThrowSyntaxError("URL given to navigate() is invalid");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }
  // 4. If options["history"] is "push", and the navigation must be a replace
  //    given urlRecord and document, then return an early error result for a
  //    "NotSupportedError" DOMException.
  if (aOptions.mHistory == NavigationHistoryBehavior::Push &&
      nsContentUtils::NavigationMustBeAReplace(*urlRecord, *document)) {
    ErrorResult rv;
    rv.ThrowNotSupportedError("Navigation must be a replace navigation");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }

  // 5. Let state be options["state"], if it exists; otherwise, undefined.
  // 6. Let serializedState be StructuredSerializeForStorage(state). If this
  //    throws an exception, then return an early error result for that
  //    exception.
  nsCOMPtr<nsIStructuredCloneContainer> serializedState =
      CreateSerializedStateAndMaybeSetEarlyErrorResult(aCx, aOptions.mState,
                                                       aResult);
  if (!serializedState) {
    return;
  }

  // 7. If document is not fully active, then return an early error result for
  //    an "InvalidStateError" DOMException.
  if (!CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(aCx, document,
                                                               aResult)) {
    return;
  }

  // 8. If document's unload counter is greater than 0, then return an early
  //    error result for an "InvalidStateError" DOMException.
  if (!CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(aCx, document,
                                                             aResult)) {
    return;
  }

  // 9. Let info be options["info"], if it exists; otherwise, undefined.
  // 10. Let apiMethodTracker be the result of maybe setting the upcoming
  //    non-traverse API method tracker for this given info and serializedState.
  JS::Rooted<JS::Value> info(aCx, aOptions.mInfo);
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MaybeSetUpcomingNonTraverseAPIMethodTracker(info, serializedState);
  MOZ_ASSERT(apiMethodTracker);

  // 11. Navigate document's node navigable to urlRecord using document, with
  //     historyHandling set to options["history"] and navigationAPIState set to
  //     serializedState.

  RefPtr bc = document->GetBrowsingContext();
  MOZ_DIAGNOSTIC_ASSERT(bc);
  bc->Navigate(urlRecord, *document->NodePrincipal(),
               /* per spec, error handling defaults to false */ IgnoreErrors(),
               aOptions.mHistory);

  // 12. If this's upcoming non-traverse API method tracker is apiMethodTracker,
  //     then:
  if (mUpcomingNonTraverseAPIMethodTracker == apiMethodTracker) {
    // Note: If the upcoming non-traverse API method tracker is still
    //       apiMethodTracker, this means that the navigate algorithm bailed out
    //       before ever getting to the inner navigate event firing algorithm
    //       which would promote that upcoming API method tracker to ongoing.
    //
    // 12.1 Set this's upcoming non-traverse API method tracker to null.
    mUpcomingNonTraverseAPIMethodTracker = nullptr;
    // 12.2 Return an early error result for an "AbortError" DOMException.
    ErrorResult rv;
    rv.ThrowAbortError("Navigation aborted.");
    SetEarlyErrorResult(aCx, aResult, std::move(rv));
    return;
  }
  // 13. Return a navigation API method tracker-derived result for
  //     apiMethodTracker.
  CreateResultFromAPIMethodTracker(apiMethodTracker, aResult);
}

// https://html.spec.whatwg.org/#dom-navigation-reload
void Navigation::Reload(JSContext* aCx, const NavigationReloadOptions& aOptions,
                        NavigationResult& aResult) {
  // 1. Let document be this's relevant global object's associated Document.
  const RefPtr<Document> document = GetAssociatedDocument();
  if (!document) {
    return;
  }

  // 2. Let serializedState be StructuredSerializeForStorage(undefined).
  RefPtr<nsIStructuredCloneContainer> serializedState;

  // 3. If options["state"] exists, then set serializedState to
  //    StructuredSerializeForStorage(options["state"]). If this throws an
  //    exception, then return an early error result for that exception.
  if (!aOptions.mState.isUndefined()) {
    serializedState = CreateSerializedStateAndMaybeSetEarlyErrorResult(
        aCx, aOptions.mState, aResult);
    if (!serializedState) {
      return;
    }
  } else {
    // 4. Otherwise:
    // 4.1 Let current be the current entry of this.
    // 4.2 If current is not null, then set serializedState to current's
    //     session history entry's navigation API state.
    if (RefPtr<NavigationHistoryEntry> current = GetCurrentEntry()) {
      serializedState = current->GetNavigationState();
    }
  }
  // 5. If document is not fully active, then return an early error result for
  //    an "InvalidStateError" DOMException.
  if (!CheckIfDocumentIsFullyActiveAndMaybeSetEarlyErrorResult(aCx, document,
                                                               aResult)) {
    return;
  }

  // 6. If document's unload counter is greater than 0, then return an early
  //    error result for an "InvalidStateError" DOMException.
  if (!CheckDocumentUnloadCounterAndMaybeSetEarlyErrorResult(aCx, document,
                                                             aResult)) {
    return;
  }

  // 7. Let info be options["info"], if it exists; otherwise, undefined.
  JS::Rooted<JS::Value> info(aCx, aOptions.mInfo);
  // 8. Let apiMethodTracker be the result of maybe setting the upcoming
  //    non-traverse API method tracker for this given info and serializedState.
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MaybeSetUpcomingNonTraverseAPIMethodTracker(info, serializedState);
  MOZ_ASSERT(apiMethodTracker);
  // 9. Reload document's node navigable with navigationAPIState set to
  //    serializedState.
  RefPtr docShell = nsDocShell::Cast(document->GetDocShell());
  MOZ_ASSERT(docShell);
  docShell->ReloadNavigable(Some(WrapNotNullUnchecked(aCx)),
                            nsIWebNavigation::LOAD_FLAGS_NONE, serializedState);

  // 10. Return a navigation API method tracker-derived result for
  //     apiMethodTracker.
  CreateResultFromAPIMethodTracker(apiMethodTracker, aResult);
}

namespace {

void LogEntry(NavigationHistoryEntry* aEntry, uint64_t aIndex, uint64_t aTotal,
              bool aIsCurrent) {
  if (!aEntry) {
    MOZ_LOG(gNavigationLog, LogLevel::Debug,
            (" +- %d NHEntry null\n", int(aIndex)));
    return;
  }

  nsString key, id;
  aEntry->GetKey(key);
  aEntry->GetId(id);
  MOZ_LOG(gNavigationLog, LogLevel::Debug,
          ("%s+- %d NHEntry %p %s %s\n", aIsCurrent ? ">" : " ", int(aIndex),
           aEntry, NS_ConvertUTF16toUTF8(key).get(),
           NS_ConvertUTF16toUTF8(id).get()));

  nsAutoString url;
  aEntry->GetUrl(url);
  MOZ_LOG(gNavigationLog, LogLevel::Debug,
          ("   URL = %s\n", NS_ConvertUTF16toUTF8(url).get()));
}

}  // namespace

// https://html.spec.whatwg.org/#fire-a-traverse-navigate-event
bool Navigation::FireTraverseNavigateEvent(
    JSContext* aCx, const SessionHistoryInfo& aDestinationSessionHistoryInfo,
    Maybe<UserNavigationInvolvement> aUserInvolvement) {
  // aDestinationSessionHistoryInfo corresponds to
  // https://html.spec.whatwg.org/#fire-navigate-traverse-destinationshe

  // To not unnecessarily create an event that's never used, step 1 and step 2
  // in #fire-a-traverse-navigate-event have been moved to after step 25 in
  // #inner-navigate-event-firing-algorithm in our implementation.

  // Step 5
  RefPtr<NavigationHistoryEntry> destinationNHE =
      FindNavigationHistoryEntry(aDestinationSessionHistoryInfo);

  // Step 6.2 and step 7.2
  RefPtr<nsStructuredCloneContainer> state =
      destinationNHE ? destinationNHE->GetNavigationState() : nullptr;

  // Step 8
  bool isSameDocument =
      ToMaybeRef(
          nsDocShell::Cast(nsContentUtils::GetDocShellForEventTarget(this)))
          .andThen([](auto& aDocShell) {
            return ToMaybeRef(aDocShell.GetActiveSessionHistoryInfo());
          })
          .map([aDestinationSessionHistoryInfo](auto& aSessionHistoryInfo) {
            return aDestinationSessionHistoryInfo.SharesDocumentWith(
                aSessionHistoryInfo);
          })
          .valueOr(false);

  // Step 3, step 4, step 6.1, and step 7.1.
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(
          GetOwnerGlobal(), aDestinationSessionHistoryInfo.GetURI(),
          destinationNHE, state, isSameDocument);

  // Step 9
  return InnerFireNavigateEvent(
      aCx, NavigationType::Traverse, destination,
      aUserInvolvement.valueOr(UserNavigationInvolvement::None),
      /* aSourceElement */ nullptr,
      /* aFormDataEntryList*/ nullptr,
      /* aClassicHistoryAPIState */ nullptr,
      /* aDownloadRequestFilename */ VoidString());
}

// https://html.spec.whatwg.org/#fire-a-push/replace/reload-navigate-event
bool Navigation::FirePushReplaceReloadNavigateEvent(
    JSContext* aCx, NavigationType aNavigationType, nsIURI* aDestinationURL,
    bool aIsSameDocument, bool aIsSync,
    Maybe<UserNavigationInvolvement> aUserInvolvement, Element* aSourceElement,
    already_AddRefed<FormData> aFormDataEntryList,
    nsIStructuredCloneContainer* aNavigationAPIState,
    nsIStructuredCloneContainer* aClassicHistoryAPIState) {
  // To not unnecessarily create an event that's never used, step 1 and step 2
  // in #fire-a-push/replace/reload-navigate-event have been moved to after step
  // 25 in #inner-navigate-event-firing-algorithm in our implementation.

  // This is currently not how spec handles this.
  // See https://github.com/whatwg/html/issues/11184
  if (aIsSync) {
    while (HasOngoingNavigateEvent()) {
      AbortOngoingNavigation(aCx);
    }
  }

  // Step 3 to step 7
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(GetOwnerGlobal(), aDestinationURL,
                                           /* aEntry */ nullptr,
                                           /* aState */ nullptr,
                                           aIsSameDocument);

  // Step 8
  return InnerFireNavigateEvent(
      aCx, aNavigationType, destination,
      aUserInvolvement.valueOr(UserNavigationInvolvement::None), aSourceElement,
      std::move(aFormDataEntryList), aClassicHistoryAPIState,
      /* aDownloadRequestFilename */ VoidString());
}

// https://html.spec.whatwg.org/#fire-a-download-request-navigate-event
bool Navigation::FireDownloadRequestNavigateEvent(
    JSContext* aCx, nsIURI* aDestinationURL,
    UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
    const nsAString& aFilename) {
  // To not unnecessarily create an event that's never used, step 1 and step 2
  // in #fire-a-download-request-navigate-event have been moved to after step
  // 25 in #inner-navigate-event-firing-algorithm in our implementation.

  // Step 3 to step 7
  RefPtr<NavigationDestination> destination =
      MakeAndAddRef<NavigationDestination>(GetOwnerGlobal(), aDestinationURL,
                                           /* aEntry */ nullptr,
                                           /* aState */ nullptr,
                                           /* aIsSameDocument */ false);

  // Step 8
  return InnerFireNavigateEvent(
      aCx, NavigationType::Push, destination, aUserInvolvement, aSourceElement,
      /* aFormDataEntryList */ nullptr,
      /* aClassicHistoryAPIState */ nullptr, aFilename);
}

static bool HasHistoryActionActivation(
    Maybe<nsGlobalWindowInner&> aRelevantGlobalObject) {
  return aRelevantGlobalObject
      .map([](auto& aRelevantGlobalObject) {
        WindowContext* windowContext = aRelevantGlobalObject.GetWindowContext();
        return windowContext && windowContext->HasValidHistoryActivation();
      })
      .valueOr(false);
}

static void ConsumeHistoryActionUserActivation(
    Maybe<nsGlobalWindowInner&> aRelevantGlobalObject) {
  aRelevantGlobalObject.apply([](auto& aRelevantGlobalObject) {
    if (WindowContext* windowContext =
            aRelevantGlobalObject.GetWindowContext()) {
      windowContext->ConsumeHistoryActivation();
    }
  });
}

// Implementation of this will be done in Bug 1948593.
static bool HasUAVisualTransition(Maybe<Document&>) { return false; }

static bool EqualsExceptRef(nsIURI* aURI, nsIURI* aOtherURI) {
  bool equalsExceptRef = false;
  return aURI && aOtherURI &&
         NS_SUCCEEDED(aURI->EqualsExceptRef(aOtherURI, &equalsExceptRef)) &&
         equalsExceptRef;
}

static bool Equals(nsIURI* aURI, nsIURI* aOtherURI) {
  bool equals = false;
  return aURI && aOtherURI && NS_SUCCEEDED(aURI->Equals(aOtherURI, &equals)) &&
         equals;
}

static bool HasIdenticalFragment(nsIURI* aURI, nsIURI* aOtherURI) {
  nsAutoCString ref;

  if (NS_FAILED(aURI->GetRef(ref))) {
    return false;
  }

  nsAutoCString otherRef;
  if (NS_FAILED(aOtherURI->GetRef(otherRef))) {
    return false;
  }

  return ref.Equals(otherRef);
}

static void LogEvent(Event* aEvent, NavigateEvent* aOngoingEvent) {
  if (!MOZ_LOG_TEST(gNavigationLog, LogLevel::Debug)) {
    return;
  }

  RefPtr<NavigationDestination> destination =
      aOngoingEvent ? aOngoingEvent->Destination() : nullptr;
  nsAutoString eventType;
  aEvent->GetType(eventType);
  LOG_FMT("Fire {} {}", NS_ConvertUTF16toUTF8(eventType),
          destination ? destination->GetURI()->GetSpecOrDefault() : ""_ns);
}

nsresult Navigation::FireEvent(const nsAString& aName) {
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  // it doesn't bubble, and it isn't cancelable
  event->InitEvent(aName, false, false);
  event->SetTrusted(true);
  ErrorResult rv;
  LogEvent(event, mOngoingNavigateEvent);
  DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

static void ExtractErrorInformation(JSContext* aCx,
                                    JS::Handle<JS::Value> aError,
                                    ErrorEventInit& aErrorEventInitDict) {
  nsContentUtils::ExtractErrorValues(
      aCx, aError, aErrorEventInitDict.mFilename, &aErrorEventInitDict.mLineno,
      &aErrorEventInitDict.mColno, aErrorEventInitDict.mMessage);
  aErrorEventInitDict.mError = aError;
  aErrorEventInitDict.mBubbles = false;
  aErrorEventInitDict.mCancelable = false;
}

nsresult Navigation::FireErrorEvent(const nsAString& aName,
                                    const ErrorEventInit& aEventInitDict) {
  RefPtr<Event> event = ErrorEvent::Constructor(this, aName, aEventInitDict);
  ErrorResult rv;

  LogEvent(event, mOngoingNavigateEvent);
  DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

struct NavigationWaitForAllScope final : public nsISupports,
                                         public SupportsWeakPtr {
  NavigationWaitForAllScope(Navigation* aNavigation,
                            NavigationAPIMethodTracker* aApiMethodTracker,
                            NavigateEvent* aEvent)
      : mNavigation(aNavigation),
        mAPIMethodTracker(aApiMethodTracker),
        mEvent(aEvent) {}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(NavigationWaitForAllScope)
  RefPtr<Navigation> mNavigation;
  RefPtr<NavigationAPIMethodTracker> mAPIMethodTracker;
  RefPtr<NavigateEvent> mEvent;

 private:
  ~NavigationWaitForAllScope() {}
};

NS_IMPL_CYCLE_COLLECTION_WEAK_PTR(NavigationWaitForAllScope, mNavigation,
                                  mAPIMethodTracker, mEvent)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(NavigationWaitForAllScope)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(NavigationWaitForAllScope)
NS_IMPL_CYCLE_COLLECTING_RELEASE(NavigationWaitForAllScope)

// https://html.spec.whatwg.org/#inner-navigate-event-firing-algorithm
bool Navigation::InnerFireNavigateEvent(
    JSContext* aCx, NavigationType aNavigationType,
    NavigationDestination* aDestination,
    UserNavigationInvolvement aUserInvolvement, Element* aSourceElement,
    already_AddRefed<FormData> aFormDataEntryList,
    nsIStructuredCloneContainer* aClassicHistoryAPIState,
    const nsAString& aDownloadRequestFilename) {
  nsCOMPtr<nsIGlobalObject> globalObject = GetOwnerGlobal();

  // Step 1
  if (HasEntriesAndEventsDisabled()) {
    // Step 1.1 to step 1.3
    MOZ_DIAGNOSTIC_ASSERT(!mOngoingAPIMethodTracker);
    MOZ_DIAGNOSTIC_ASSERT(!mUpcomingNonTraverseAPIMethodTracker);
    MOZ_DIAGNOSTIC_ASSERT(mUpcomingTraverseAPIMethodTrackers.IsEmpty());

    // Step 1.4
    return true;
  }

  RootedDictionary<NavigateEventInit> init(RootingCx());

  // Step 2
  Maybe<nsID> destinationKey;

  // Step 3
  if (auto* entry = aDestination->GetEntry()) {
    destinationKey.emplace(entry->Key());
  }

  // Step 4
  MOZ_DIAGNOSTIC_ASSERT(!destinationKey || !destinationKey->Equals(nsID{}));

  // Step 5
  PromoteUpcomingAPIMethodTrackerToOngoing(std::move(destinationKey));

  // Step 6
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      mOngoingAPIMethodTracker;

  // Step 7
  Maybe<BrowsingContext&> navigable =
      ToMaybeRef(GetOwnerWindow()).andThen([](auto& aWindow) {
        return ToMaybeRef(aWindow.GetBrowsingContext());
      });

  // Step 8
  Document* document =
      navigable.map([](auto& aNavigable) { return aNavigable.GetDocument(); })
          .valueOr(nullptr);

  // Step 9
  init.mCanIntercept = document &&
                       document->CanRewriteURL(aDestination->GetURI()) &&
                       (aDestination->SameDocument() ||
                        aNavigationType != NavigationType::Traverse);

  // Step 10
  bool traverseCanBeCanceled =
      navigable->IsTop() && aDestination->SameDocument() &&
      (aUserInvolvement != UserNavigationInvolvement::BrowserUI ||
       HasHistoryActionActivation(ToMaybeRef(GetOwnerWindow())));

  // Step 11
  init.mCancelable =
      aNavigationType != NavigationType::Traverse || traverseCanBeCanceled;

  // Step 13
  init.mNavigationType = aNavigationType;

  // Step 14
  init.mDestination = aDestination;

  // Step 15
  init.mDownloadRequest = aDownloadRequestFilename;

  // Step 16
  if (apiMethodTracker) {
    init.mInfo = apiMethodTracker->mInfo;
  }

  // Step 17
  init.mHasUAVisualTransition =
      HasUAVisualTransition(ToMaybeRef(GetAssociatedDocument()));

  // Step 18
  init.mSourceElement = aSourceElement;

  // Step 19
  RefPtr<AbortController> abortController = new AbortController(globalObject);

  // Step 20
  init.mSignal = abortController->Signal();

  // step 21
  nsCOMPtr<nsIURI> currentURL = document->GetDocumentURI();

  // step 22
  init.mHashChange = !aClassicHistoryAPIState && aDestination->SameDocument() &&
                     EqualsExceptRef(aDestination->GetURI(), currentURL) &&
                     !HasIdenticalFragment(aDestination->GetURI(), currentURL);

  // Step 23
  init.mUserInitiated = aUserInvolvement != UserNavigationInvolvement::None;

  // Step 24
  init.mFormData = aFormDataEntryList;

  // Step 25
  MOZ_DIAGNOSTIC_ASSERT(!mOngoingNavigateEvent);

  // We now have everything we need to fully initialize the NavigateEvent, so
  // we'll go ahead and create it now. This is done by the spec in step 1 and
  // step 2 of #fire-a-traverse-navigate-event,
  // #fire-a-push/replace/reload-navigate-event, or
  // #fire-a-download-request-navigate-event, but there's no reason to not
  // delay it until here. This also performs step 12.
  RefPtr<NavigateEvent> event = NavigateEvent::Constructor(
      this, u"navigate"_ns, init, aClassicHistoryAPIState, abortController);
  // Here we're running #concept-event-create from https://dom.spec.whatwg.org/
  // which explicitly sets event's isTrusted attribute to true.
  event->SetTrusted(true);

  // Step 26
  mOngoingNavigateEvent = event;

  // Step 27
  mFocusChangedDuringOngoingNavigation = false;

  // Step 28
  mSuppressNormalScrollRestorationDuringOngoingNavigation = false;

  // Step 29 and step 30
  LogEvent(event, mOngoingNavigateEvent);
  if (!DispatchEvent(*event, CallerType::NonSystem, IgnoreErrors())) {
    // Step 30.1
    if (aNavigationType == NavigationType::Traverse) {
      ConsumeHistoryActionUserActivation(ToMaybeRef(GetOwnerWindow()));
    }

    // Step 30.2
    if (!abortController->Signal()->Aborted()) {
      AbortOngoingNavigation(aCx);
    }

    // Step 30.3
    return false;
  }

  // Step 31
  bool endResultIsSameDocument =
      event->InterceptionState() != NavigateEvent::InterceptionState::None ||
      aDestination->SameDocument();

  // Step 32 (and the destructor of this is step 36)
  nsAutoMicroTask mt;

  // Step 33
  if (event->InterceptionState() != NavigateEvent::InterceptionState::None) {
    // Step 33.1
    event->SetInterceptionState(NavigateEvent::InterceptionState::Committed);

    // Step 33.2
    RefPtr<NavigationHistoryEntry> fromNHE = GetCurrentEntry();

    // Step 33.3
    MOZ_DIAGNOSTIC_ASSERT(fromNHE);

    // Step 33.4
    RefPtr<Promise> promise = Promise::CreateInfallible(globalObject);
    mTransition = MakeAndAddRef<NavigationTransition>(
        globalObject, aNavigationType, fromNHE, promise);

    // Step 33.5
    MOZ_ALWAYS_TRUE(promise->SetAnyPromiseIsHandled());

    switch (aNavigationType) {
      case NavigationType::Traverse:
        // Step 33.6
        mSuppressNormalScrollRestorationDuringOngoingNavigation = true;
        break;
      case NavigationType::Push:
      case NavigationType::Replace:
        // Step 33.7
        if (nsDocShell* docShell = nsDocShell::Cast(document->GetDocShell())) {
          docShell->UpdateURLAndHistory(
              document, aDestination->GetURI(), event->ClassicHistoryAPIState(),
              *NavigationUtils::NavigationHistoryBehavior(aNavigationType),
              document->GetDocumentURI(),
              Equals(aDestination->GetURI(), document->GetDocumentURI()));
        }
        break;
      case NavigationType::Reload:
        // Step 33.8
        if (nsDocShell* docShell = nsDocShell::Cast(document->GetDocShell())) {
          UpdateEntriesForSameDocumentNavigation(
              docShell->GetActiveSessionHistoryInfo(), aNavigationType);
        }
        break;
      default:
        break;
    }
  }

  // Step 34
  if (endResultIsSameDocument) {
    // Step 34.1
    AutoTArray<RefPtr<Promise>, 16> promiseList;
    // Step 34.2
    for (auto& handler : event->NavigationHandlerList().Clone()) {
      // Step 34.2.1
      RefPtr promise = MOZ_KnownLive(handler)->Call();
      if (promise) {
        promiseList.AppendElement(promise);
      }
    }

    // Step 34.3
    if (promiseList.IsEmpty()) {
      RefPtr promise = Promise::CreateResolvedWithUndefined(
          globalObject, IgnoredErrorResult());
      if (promise) {
        promiseList.AppendElement(promise);
      }
    }

    // Step 34.4
    // We capture the scope which we wish to keep alive in the lambdas passed to
    // Promise::WaitForAll. We pass it as the cycle collected argument to
    // Promise::WaitForAll, which makes it stay alive until all promises
    // resolved, or we've become cycle collected. This means that we can pass
    // the scope as a weak reference.
    RefPtr scope =
        MakeRefPtr<NavigationWaitForAllScope>(this, apiMethodTracker, event);
    Promise::WaitForAll(
        globalObject, promiseList,
        [weakScope = WeakPtr(scope)](const Span<JS::Heap<JS::Value>>&)
            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              // If weakScope is null we've been cycle collected
              if (!weakScope) {
                return;
              }

              RefPtr event = weakScope->mEvent;
              RefPtr self = weakScope->mNavigation;
              RefPtr apiMethodTracker = weakScope->mAPIMethodTracker;
              // Success steps
              // Step 1
              if (RefPtr document = event->GetDocument();
                  !document || !document->IsFullyActive()) {
                return;
              }

              // Step 2
              if (AbortSignal* signal = event->Signal(); signal->Aborted()) {
                return;
              }

              // Step 3
              MOZ_DIAGNOSTIC_ASSERT(event == self->mOngoingNavigateEvent);

              // Step 4
              self->mOngoingNavigateEvent = nullptr;

              // Step 5
              event->Finish(true);

              // Step 6
              self->FireEvent(u"navigatesuccess"_ns);

              // Step 7
              if (apiMethodTracker) {
                apiMethodTracker->ResolveFinishedPromise();
              }

              // Step 8
              if (self->mTransition) {
                self->mTransition->Finished()->MaybeResolveWithUndefined();
              }

              // Step 9
              self->mTransition = nullptr;
            },
        [weakScope = WeakPtr(scope)](JS::Handle<JS::Value> aRejectionReason)
            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              // If weakScope is null we've been cycle collected
              if (!weakScope) {
                return;
              }

              RefPtr event = weakScope->mEvent;
              RefPtr self = weakScope->mNavigation;
              RefPtr apiMethodTracker = weakScope->mAPIMethodTracker;

              // Failure steps
              // Step 1
              if (RefPtr document = event->GetDocument();
                  !document || !document->IsFullyActive()) {
                return;
              }

              // Step 2
              if (AbortSignal* signal = event->Signal(); signal->Aborted()) {
                return;
              }

              // Step 3
              MOZ_DIAGNOSTIC_ASSERT(event == self->mOngoingNavigateEvent);

              // Step 4
              self->mOngoingNavigateEvent = nullptr;

              // Step 5
              event->Finish(false);

              if (AutoJSAPI jsapi;
                  !NS_WARN_IF(!jsapi.Init(event->GetParentObject()))) {
                // Step 6
                RootedDictionary<ErrorEventInit> init(jsapi.cx());
                ExtractErrorInformation(jsapi.cx(), aRejectionReason, init);

                // Step 7
                self->FireErrorEvent(u"navigateerror"_ns, init);
              }

              // Step 8
              if (apiMethodTracker) {
                apiMethodTracker->mFinishedPromise->MaybeReject(
                    aRejectionReason);
              }

              // Step 9
              if (self->mTransition) {
                self->mTransition->Finished()->MaybeReject(aRejectionReason);
              }

              // Step 10
              self->mTransition = nullptr;
            },
        scope);
  } else if (apiMethodTracker) {
    // Step 35
    apiMethodTracker->CleanUp();
  }

  // Step 37 and step 38
  return event->InterceptionState() == NavigateEvent::InterceptionState::None;
}

NavigationHistoryEntry* Navigation::FindNavigationHistoryEntry(
    const SessionHistoryInfo& aSessionHistoryInfo) const {
  for (const auto& navigationHistoryEntry : mEntries) {
    if (navigationHistoryEntry->IsSameEntry(&aSessionHistoryInfo)) {
      return navigationHistoryEntry;
    }
  }

  return nullptr;
}

// https://html.spec.whatwg.org/#promote-an-upcoming-api-method-tracker-to-ongoing
void Navigation::PromoteUpcomingAPIMethodTrackerToOngoing(
    Maybe<nsID>&& aDestinationKey) {
  MOZ_DIAGNOSTIC_ASSERT(!mOngoingAPIMethodTracker);
  if (aDestinationKey) {
    MOZ_DIAGNOSTIC_ASSERT(!mUpcomingNonTraverseAPIMethodTracker);
    Maybe<NavigationAPIMethodTracker&> tracker(NavigationAPIMethodTracker);
    if (auto entry =
            mUpcomingTraverseAPIMethodTrackers.Extract(*aDestinationKey)) {
      mOngoingAPIMethodTracker = std::move(*entry);
    }
    return;
  }

  mOngoingAPIMethodTracker = std::move(mUpcomingNonTraverseAPIMethodTracker);
}

// https://html.spec.whatwg.org/#navigation-api-method-tracker-clean-up
/* static */ void Navigation::CleanUp(
    NavigationAPIMethodTracker* aNavigationAPIMethodTracker) {
  // Step 1
  RefPtr<Navigation> navigation =
      aNavigationAPIMethodTracker->mNavigationObject;

  // Step 2
  if (navigation->mOngoingAPIMethodTracker == aNavigationAPIMethodTracker) {
    navigation->mOngoingAPIMethodTracker = nullptr;

    return;
  }

  // Step 3.1
  Maybe<nsID> key = aNavigationAPIMethodTracker->mKey;

  // Step 3.2
  MOZ_DIAGNOSTIC_ASSERT(key);

  // Step 3.3
  MOZ_DIAGNOSTIC_ASSERT(
      navigation->mUpcomingTraverseAPIMethodTrackers.Contains(*key));

  navigation->mUpcomingTraverseAPIMethodTrackers.Remove(*key);
}

// https://html.spec.whatwg.org/#abort-the-ongoing-navigation
void Navigation::AbortOngoingNavigation(JSContext* aCx,
                                        JS::Handle<JS::Value> aError) {
  // Step 1
  RefPtr<NavigateEvent> event = mOngoingNavigateEvent;

  // Step 2
  MOZ_DIAGNOSTIC_ASSERT(event);

  // Step 3
  mFocusChangedDuringOngoingNavigation = false;

  // Step 4
  mSuppressNormalScrollRestorationDuringOngoingNavigation = false;

  JS::Rooted<JS::Value> error(aCx, aError);

  // Step 5
  if (aError.isUndefined()) {
    RefPtr<DOMException> exception =
        DOMException::Create(NS_ERROR_DOM_ABORT_ERR);
    // It's OK if this fails, it just means that we'll get an empty error
    // dictionary below.
    GetOrCreateDOMReflector(aCx, exception, &error);
  }

  // Step 6
  if (event->IsBeingDispatched()) {
    // Here NonSystem is needed since it needs to be the same as what we
    // dispatch with.
    event->PreventDefault(aCx, CallerType::NonSystem);
  }

  // Step 7
  event->AbortController()->Abort(aCx, error);

  // Step 8
  mOngoingNavigateEvent = nullptr;

  // Step 9
  RootedDictionary<ErrorEventInit> init(aCx);
  ExtractErrorInformation(aCx, error, init);

  // Step 10
  FireErrorEvent(u"navigateerror"_ns, init);

  // Step 11
  if (mOngoingAPIMethodTracker) {
    mOngoingAPIMethodTracker->RejectFinishedPromise(error);
  }

  // Step 12
  if (mTransition) {
    // Step 12.1
    mTransition->Finished()->MaybeReject(error);

    // Step 12.2
    mTransition = nullptr;
  }
}

bool Navigation::FocusedChangedDuringOngoingNavigation() const {
  return mFocusChangedDuringOngoingNavigation;
}

void Navigation::SetFocusedChangedDuringOngoingNavigation(
    bool aFocusChangedDUringOngoingNavigation) {
  mFocusChangedDuringOngoingNavigation = aFocusChangedDUringOngoingNavigation;
}

bool Navigation::HasOngoingNavigateEvent() const {
  return mOngoingNavigateEvent;
}

// The associated document of navigation's relevant global object.
Document* Navigation::GetAssociatedDocument() const {
  nsGlobalWindowInner* window = GetOwnerWindow();
  return window ? window->GetDocument() : nullptr;
}

void Navigation::LogHistory() const {
  if (!MOZ_LOG_TEST(gNavigationLog, LogLevel::Debug)) {
    return;
  }

  MOZ_LOG(gNavigationLog, LogLevel::Debug,
          ("Navigation %p (current entry index: %d)\n", this,
           mCurrentEntryIndex ? int(*mCurrentEntryIndex) : -1));
  auto length = mEntries.Length();
  for (uint64_t i = 0; i < length; i++) {
    LogEntry(mEntries[i], i, length,
             mCurrentEntryIndex && i == *mCurrentEntryIndex);
  }
}

// https://html.spec.whatwg.org/#maybe-set-the-upcoming-non-traverse-api-method-tracker
RefPtr<NavigationAPIMethodTracker>
Navigation::MaybeSetUpcomingNonTraverseAPIMethodTracker(
    JS::Handle<JS::Value> aInfo,
    nsIStructuredCloneContainer* aSerializedState) {
  // To maybe set the upcoming non-traverse API method tracker given a
  // Navigation navigation, a JavaScript value info, and a serialized
  // state-or-null serializedState:
  // 1. Let committedPromise and finishedPromise be new promises created in
  //    navigation's relevant realm.
  RefPtr committedPromise = Promise::CreateInfallible(GetOwnerGlobal());
  RefPtr finishedPromise = Promise::CreateInfallible(GetOwnerGlobal());
  // 2. Mark as handled finishedPromise.
  MOZ_ALWAYS_TRUE(finishedPromise->SetAnyPromiseIsHandled());

  // 3. Let apiMethodTracker be a new navigation API method tracker with:
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MakeAndAddRef<NavigationAPIMethodTracker>(
          this, /* aKey */ Nothing{}, aInfo, aSerializedState,
          /* aCommittedToEntry */ nullptr, committedPromise, finishedPromise);

  // 4. Assert: navigation's upcoming non-traverse API method tracker is null.
  MOZ_DIAGNOSTIC_ASSERT(!mUpcomingNonTraverseAPIMethodTracker);

  // 5. If navigation does not have entries and events disabled, then set
  //    navigation's upcoming non-traverse API method tracker to
  //    apiMethodTracker.
  if (!HasEntriesAndEventsDisabled()) {
    mUpcomingNonTraverseAPIMethodTracker = apiMethodTracker;
  }
  // 6. Return apiMethodTracker.
  return apiMethodTracker;
}

// https://html.spec.whatwg.org/#add-an-upcoming-traverse-api-method-tracker
RefPtr<NavigationAPIMethodTracker>
Navigation::AddUpcomingTraverseAPIMethodTracker(const nsID& aKey,
                                                JS::Handle<JS::Value> aInfo) {
  // To add an upcoming traverse API method tracker given a Navigation
  // navigation, a string destinationKey, and a JavaScript value info:
  // 1. Let committedPromise and finishedPromise be new promises created in
  //    navigation's relevant realm.
  RefPtr committedPromise = Promise::CreateInfallible(GetOwnerGlobal());
  RefPtr finishedPromise = Promise::CreateInfallible(GetOwnerGlobal());

  // 2. Mark as handled finishedPromise.
  MOZ_ALWAYS_TRUE(finishedPromise->SetAnyPromiseIsHandled());

  // 3. Let apiMethodTracker be a new navigation API method tracker with:
  RefPtr<NavigationAPIMethodTracker> apiMethodTracker =
      MakeAndAddRef<NavigationAPIMethodTracker>(
          this, Some(aKey), aInfo,
          /* aSerializedState */ nullptr,
          /* aCommittedToEntry */ nullptr, committedPromise, finishedPromise);

  // 4. Set navigation's upcoming traverse API method trackers[destinationKey]
  //    to apiMethodTracker.
  // 5. Return apiMethodTracker.
  return mUpcomingTraverseAPIMethodTrackers.InsertOrUpdate(aKey,
                                                           apiMethodTracker);
}
}  // namespace mozilla::dom
