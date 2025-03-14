/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/JSActorManager.h"

#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/PWindowGlobal.h"
#include "mozilla/dom/JSProcessActorProtocol.h"
#include "mozilla/dom/JSWindowActorProtocol.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/ScopeExit.h"
#include "mozJSModuleLoader.h"
#include "jsapi.h"
#include "js/CallAndConstruct.h"    // JS::Construct
#include "js/PropertyAndElement.h"  // JS_GetProperty
#include "nsContentUtils.h"

namespace mozilla::dom {

already_AddRefed<JSActor> JSActorManager::GetActor(JSContext* aCx,
                                                   const nsACString& aName,
                                                   ErrorResult& aRv) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  // If our connection has been closed, return an error.
  mozilla::ipc::IProtocol* nativeActor = AsNativeActor();
  if (!nativeActor->CanSend()) {
    aRv.ThrowInvalidStateError(nsPrintfCString(
        "Cannot get actor '%s'. Native '%s' actor is destroyed.",
        PromiseFlatCString(aName).get(), nativeActor->GetProtocolName()));
    return nullptr;
  }

  // Check if this actor has already been created, and return it if it has.
  if (RefPtr<JSActor> actor = mJSActors.Get(aName)) {
    return actor.forget();
  }

  RefPtr<JSActorService> actorSvc = JSActorService::GetSingleton();
  if (!actorSvc) {
    aRv.ThrowInvalidStateError("JSActorService hasn't been initialized");
    return nullptr;
  }

  // Check if this actor satisfies the requirements of the protocol
  // corresponding to `aName`, and get the module which implements it.
  RefPtr<JSActorProtocol> protocol =
      MatchingJSActorProtocol(actorSvc, aName, aRv);
  if (!protocol) {
    return nullptr;
  }

  auto& side = nativeActor->GetSide() == mozilla::ipc::ParentSide
                   ? protocol->Parent()
                   : protocol->Child();

  // Load the module using mozJSModuleLoader.
  // If the JSActor uses `loadInDevToolsLoader`, force loading in the DevTools
  // specific's loader.
  RefPtr loader = protocol->mLoadInDevToolsLoader
                      ? mozJSModuleLoader::GetOrCreateDevToolsLoader(aCx)
                      : mozJSModuleLoader::Get();
  MOZ_ASSERT(loader);

  // We're about to construct the actor, so make sure we're in the loader realm
  // while importing etc.
  JSAutoRealm ar(aCx, loader->GetSharedGlobal());

  // If a module URI was provided, use it to construct an instance of the actor.
  JS::Rooted<JSObject*> actorObj(aCx);
  if (side.mESModuleURI) {
    JS::Rooted<JSObject*> exports(aCx);
    aRv = loader->ImportESModule(aCx, side.mESModuleURI.ref(), &exports);
    if (aRv.Failed()) {
      return nullptr;
    }
    MOZ_ASSERT(exports, "null exports!");

    // Load the specific property from our module.
    JS::Rooted<JS::Value> ctor(aCx);
    nsAutoCString ctorName(aName);
    ctorName.Append(StringFromIPCSide(nativeActor->GetSide()));
    if (!JS_GetProperty(aCx, exports, ctorName.get(), &ctor)) {
      aRv.NoteJSContextException(aCx);
      return nullptr;
    }

    if (NS_WARN_IF(!ctor.isObject())) {
      aRv.ThrowNotFoundError(nsPrintfCString(
          "Could not find actor constructor '%s'", ctorName.get()));
      return nullptr;
    }

    // Invoke the constructor loaded from the module.
    if (!JS::Construct(aCx, ctor, JS::HandleValueArray::empty(), &actorObj)) {
      aRv.NoteJSContextException(aCx);
      return nullptr;
    }
  }

  // Initialize our newly-constructed actor, and return it.
  RefPtr<JSActor> actor = InitJSActor(actorObj, aName, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  mJSActors.InsertOrUpdate(aName, RefPtr{actor});
  return actor.forget();
}

already_AddRefed<JSActor> JSActorManager::GetExistingActor(
    const nsACString& aName) {
  if (!AsNativeActor()->CanSend()) {
    return nullptr;
  }
  return mJSActors.Get(aName);
}

#define CHILD_DIAGNOSTIC_ASSERT(test, msg) \
  do {                                     \
    if (XRE_IsParentProcess()) {           \
      MOZ_ASSERT(test, msg);               \
    } else {                               \
      MOZ_DIAGNOSTIC_ASSERT(test, msg);    \
    }                                      \
  } while (0)

void JSActorManager::ReceiveRawMessage(
    const JSActorMessageMeta& aMetadata,
    Maybe<ipc::StructuredCloneData>&& aData,
    Maybe<ipc::StructuredCloneData>&& aStack) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  CrashReporter::AutoRecordAnnotation autoActorName(
      CrashReporter::Annotation::JSActorName, aMetadata.actorName());
  CrashReporter::AutoRecordAnnotation autoMessageName(
      CrashReporter::Annotation::JSActorMessage,
      NS_LossyConvertUTF16toASCII(aMetadata.messageName()));

  // We're going to be running JS. Enter the privileged junk realm so we can set
  // up our JS state correctly.
  AutoEntryScript aes(xpc::PrivilegedJunkScope(), "JSActor message handler");
  JSContext* cx = aes.cx();

  // Ensure any errors reported to `error` are set on the scope, so they're
  // reported.
  ErrorResult error;
  auto autoSetException =
      MakeScopeExit([&] { Unused << error.MaybeSetPendingException(cx); });

  // If an async stack was provided, set up our async stack state.
  JS::Rooted<JSObject*> stack(cx);
  Maybe<JS::AutoSetAsyncStackForNewCalls> stackSetter;
  {
    JS::Rooted<JS::Value> stackVal(cx);
    if (aStack) {
      aStack->Read(cx, &stackVal, error);
      if (error.Failed()) {
        error.SuppressException();
        JS_ClearPendingException(cx);
        stackVal.setUndefined();
      }
    }

    if (stackVal.isObject()) {
      stack = &stackVal.toObject();
      if (!js::IsSavedFrame(stack)) {
        CHILD_DIAGNOSTIC_ASSERT(false, "Stack must be a SavedFrame object");
        error.ThrowDataError("Actor async stack must be a SavedFrame object");
        return;
      }
      stackSetter.emplace(cx, stack, "JSActor query");
    }
  }

  RefPtr<JSActor> actor = GetActor(cx, aMetadata.actorName(), error);
  if (error.Failed()) {
    return;
  }

#ifdef DEBUG
  {
    RefPtr<JSActorService> actorSvc = JSActorService::GetSingleton();
    RefPtr windowProtocol(
        actorSvc->GetJSWindowActorProtocol(aMetadata.actorName()));
    RefPtr processProtocol(
        actorSvc->GetJSProcessActorProtocol(aMetadata.actorName()));
    MOZ_ASSERT(windowProtocol || processProtocol,
               "The protocol of this actor should exist");
  }
#endif  // DEBUG

  JS::Rooted<JS::Value> data(cx);
  if (aData) {
    aData->Read(cx, &data, error);
    // StructuredCloneHolder populates an array of ports for MessageEvent.ports
    // which we don't need, but which StructuredCloneHolder's destructor will
    // assert on for thread safety reasons (that do not apply in this case) if
    // we do not consume the array.  It's possible for the Read call above to
    // populate this array even in event of an error, so we must consume the
    // array before processing the error.
    nsTArray<RefPtr<MessagePort>> ports = aData->TakeTransferredPorts();
    // Cast to void so that the ports will actually be moved, and then
    // discarded.
    (void)ports;
    if (error.Failed()) {
      CHILD_DIAGNOSTIC_ASSERT(CycleCollectedJSRuntime::Get()->OOMReported(),
                              "Should not receive non-decodable data");
      return;
    }
  }

  switch (aMetadata.kind()) {
    case JSActorMessageKind::QueryResolve:
    case JSActorMessageKind::QueryReject:
      actor->ReceiveQueryReply(cx, aMetadata, data, error);
      break;

    case JSActorMessageKind::Message:
      actor->ReceiveMessage(cx, aMetadata, data, error);
      break;

    case JSActorMessageKind::Query:
      actor->ReceiveQuery(cx, aMetadata, data, error);
      break;

    default:
      MOZ_ASSERT_UNREACHABLE();
  }
}

void JSActorManager::JSActorWillDestroy() {
  for (const auto& entry : mJSActors.Values()) {
    entry->StartDestroy();
  }
}

void JSActorManager::JSActorDidDestroy() {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
  CrashReporter::AutoRecordAnnotation autoMessageName(
      CrashReporter::Annotation::JSActorMessage, "<DidDestroy>"_ns);

  // Swap the table with `mJSActors` so that we don't invalidate it while
  // iterating.
  const nsRefPtrHashtable<nsCStringHashKey, JSActor> actors =
      std::move(mJSActors);
  for (const auto& entry : actors.Values()) {
    CrashReporter::AutoRecordAnnotation autoActorName(
        CrashReporter::Annotation::JSActorName, entry->Name());
    // Do not risk to run script very late in shutdown
    if (!AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownFinal)) {
      entry->AfterDestroy();
    }
  }
}

void JSActorManager::JSActorUnregister(const nsACString& aName) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  RefPtr<JSActor> actor;
  if (mJSActors.Remove(aName, getter_AddRefs(actor))) {
    actor->AfterDestroy();
  }
}

}  // namespace mozilla::dom
