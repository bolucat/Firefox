/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JavaScript modules (as in, the syntactic construct) implementation. */

#include "vm/Modules.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/ScopeExit.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit

#include <stdint.h>  // uint32_t

#include "jstypes.h"  // JS_PUBLIC_API

#include "builtin/JSON.h"  // js::ParseJSONWithReviver
#include "builtin/ModuleObject.h"
#include "builtin/Promise.h"  // js::CreatePromiseObjectForAsync, js::AsyncFunctionReturned
#include "ds/Sort.h"
#include "frontend/BytecodeCompiler.h"  // js::frontend::CompileModule
#include "frontend/FrontendContext.h"   // js::AutoReportFrontendContext
#include "js/ColumnNumber.h"            // JS::ColumnNumberOneOrigin
#include "js/Context.h"                 // js::AssertHeapIsIdle
#include "js/ErrorReport.h"             // JSErrorBase
#include "js/friend/StackLimits.h"      // js::AutoCheckRecursionLimit
#include "js/RootingAPI.h"              // JS::MutableHandle
#include "js/Value.h"                   // JS::Value
#include "vm/EnvironmentObject.h"       // js::ModuleEnvironmentObject
#include "vm/JSAtomUtils.h"             // AtomizeString
#include "vm/JSContext.h"               // CHECK_THREAD, JSContext
#include "vm/JSObject.h"                // JSObject
#include "vm/JSONParser.h"              // JSONParser
#include "vm/List.h"                    // ListObject
#include "vm/Runtime.h"                 // JSRuntime

#include "builtin/HandlerFunction-inl.h"  // js::ExtraValueFromHandler, js::NewHandler{,WithExtraValue}, js::TargetFromHandler
#include "vm/JSAtomUtils-inl.h"           // AtomToId
#include "vm/JSContext-inl.h"             // JSContext::{c,releaseC}heck
#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

using mozilla::Utf8Unit;

class DynamicImportContextObject;

static bool ModuleLink(JSContext* cx, Handle<ModuleObject*> module);
static bool ModuleEvaluate(JSContext* cx, Handle<ModuleObject*> module,
                           MutableHandle<Value> rval);
static bool SyntheticModuleEvaluate(JSContext* cx, Handle<ModuleObject*> module,
                                    MutableHandle<Value> rval);
static bool ContinueModuleLoading(JSContext* cx,
                                  Handle<GraphLoadingStateRecordObject*> state,
                                  Handle<ModuleObject*> moduleCompletion,
                                  Handle<Value> error);
static bool TryStartDynamicModuleImport(JSContext* cx, HandleScript script,
                                        HandleValue specifierArg,
                                        HandleValue optionsArg,
                                        HandleObject promise);
static bool ContinueDynamicImport(JSContext* cx, Handle<JSScript*> referrer,
                                  Handle<JSObject*> moduleRequest,
                                  Handle<PromiseObject*> promiseCapability,
                                  Handle<ModuleObject*> module,
                                  bool usePromise);
static bool LinkAndEvaluateDynamicImport(JSContext* cx, unsigned argc,
                                         Value* vp);
static bool LinkAndEvaluateDynamicImport(
    JSContext* cx, Handle<DynamicImportContextObject*> context);
static bool DynamicImportResolved(JSContext* cx, unsigned argc, Value* vp);
static bool DynamicImportRejected(JSContext* cx, unsigned argc, Value* vp);

////////////////////////////////////////////////////////////////////////////////
// Public API

JS_PUBLIC_API JS::ModuleLoadHook JS::GetModuleLoadHook(JSRuntime* rt) {
  AssertHeapIsIdle();

  return rt->moduleLoadHook;
}

JS_PUBLIC_API void JS::SetModuleLoadHook(JSRuntime* rt, ModuleLoadHook func) {
  AssertHeapIsIdle();

  rt->moduleLoadHook = func;
}

JS_PUBLIC_API JS::ModuleMetadataHook JS::GetModuleMetadataHook(JSRuntime* rt) {
  AssertHeapIsIdle();

  return rt->moduleMetadataHook;
}

JS_PUBLIC_API void JS::SetModuleMetadataHook(JSRuntime* rt,
                                             ModuleMetadataHook func) {
  AssertHeapIsIdle();

  rt->moduleMetadataHook = func;
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
JS_PUBLIC_API bool JS::FinishLoadingImportedModule(
    JSContext* cx, Handle<JSScript*> referrer, Handle<JSObject*> moduleRequest,
    Handle<Value> payload, Handle<JSObject*> result, bool usePromise) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(referrer, moduleRequest, payload, result);

  MOZ_ASSERT(result);
  Rooted<ModuleObject*> module(cx, &result->as<ModuleObject>());

  if (referrer && referrer->isModule()) {
    // |loadedModules| is only required to be stored on modules.

    // Step 1. If result is a normal completion, then
    // Step 1.a. If referrer.[[LoadedModules]] contains a Record whose
    //           [[Specifier]] is specifier, then
    LoadedModuleMap& loadedModules = referrer->module()->loadedModules();
    if (auto record = loadedModules.lookup(moduleRequest)) {
      //  Step 1.a.i. Assert: That Record's [[Module]] is result.[[Value]].
      MOZ_ASSERT(record->value() == module);
    } else {
      // Step 1.b. Else, append the Record { moduleRequest.[[Specifer]],
      //           [[Attributes]]: moduleRequest.[[Attributes]],
      //           [[Module]]: result.[[Value]] } to referrer.[[LoadedModules]].
      if (!loadedModules.putNew(moduleRequest, module)) {
        ReportOutOfMemory(cx);
        return FinishLoadingImportedModuleFailedWithPendingException(cx,
                                                                     payload);
      }
    }
  }

  // Step 2. If payload is a GraphLoadingState Record, then
  // Step 2.a. Perform ContinueModuleLoading(payload, result).
  JSObject* object = &payload.toObject();
  if (object->is<GraphLoadingStateRecordObject>()) {
    Rooted<GraphLoadingStateRecordObject*> state(cx);
    state = &object->as<GraphLoadingStateRecordObject>();
    return ContinueModuleLoading(cx, state, module, UndefinedHandleValue);
  }

  // Step 3. Else,
  // Step 3.a. Perform ContinueDynamicImport(payload, result).
  MOZ_ASSERT(object->is<PromiseObject>());
  Rooted<PromiseObject*> promise(cx, &object->as<PromiseObject>());
  return ContinueDynamicImport(cx, referrer, moduleRequest, promise, module,
                               usePromise);
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
// Failure path where |result| is a throw completion, supplied as |error|.
JS_PUBLIC_API bool JS::FinishLoadingImportedModuleFailed(
    JSContext* cx, Handle<Value> payloadArg, Handle<Value> error) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(payloadArg, error);
  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  // Step 2. If payload is a GraphLoadingState Record, then
  // Step 2.a. Perform ContinueModuleLoading(payload, result).
  JSObject* payload = &payloadArg.toObject();
  if (payload->is<GraphLoadingStateRecordObject>()) {
    Rooted<GraphLoadingStateRecordObject*> state(cx);
    state = &payload->as<GraphLoadingStateRecordObject>();
    return ContinueModuleLoading(cx, state, nullptr, error);
  }

  // Step 3. Else,
  // Step 3.a. Perform ContinueDynamicImport(payload, result).
  // ContinueDynamicImport:
  // Step 1. If moduleCompletion is an abrupt completion, then
  // Step 1. a. Perform ! Call(promiseCapability.[[Reject]], undefined,
  //            moduleCompletion.[[Value]]).
  Rooted<PromiseObject*> promise(cx, &payload->as<PromiseObject>());
  return PromiseObject::reject(cx, promise, error);
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
// Failure path where |result| is a throw completion, set as the pending
// exception.
JS_PUBLIC_API bool JS::FinishLoadingImportedModuleFailedWithPendingException(
    JSContext* cx, Handle<Value> payload) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(payload);
  MOZ_ASSERT(JS_IsExceptionPending(cx));

  RootedValue error(cx);
  if (!cx->getPendingException(&error)) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    MOZ_ALWAYS_TRUE(cx->getPendingException(&error));
  }
  cx->clearPendingException();

  return FinishLoadingImportedModuleFailed(cx, payload, error);
}

template <typename Unit>
static JSObject* CompileModuleHelper(JSContext* cx,
                                     const JS::ReadOnlyCompileOptions& options,
                                     JS::SourceText<Unit>& srcBuf) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  JS::Rooted<JSObject*> mod(cx);
  {
    AutoReportFrontendContext fc(cx);
    mod = frontend::CompileModule(cx, &fc, options, srcBuf);
  }
  return mod;
}

JS_PUBLIC_API JSObject* JS::CompileModule(JSContext* cx,
                                          const ReadOnlyCompileOptions& options,
                                          SourceText<char16_t>& srcBuf) {
  return CompileModuleHelper(cx, options, srcBuf);
}

JS_PUBLIC_API JSObject* JS::CompileModule(JSContext* cx,
                                          const ReadOnlyCompileOptions& options,
                                          SourceText<Utf8Unit>& srcBuf) {
  return CompileModuleHelper(cx, options, srcBuf);
}

JS_PUBLIC_API JSObject* JS::CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf) {
  size_t length = srcBuf.length();
  auto chars =
      UniqueTwoByteChars(UTF8CharsToNewTwoByteCharsZ(
                             cx, JS::UTF8Chars(srcBuf.get(), srcBuf.length()),
                             &length, js::MallocArena)
                             .get());
  if (!chars) {
    return nullptr;
  }

  JS::SourceText<char16_t> source;
  if (!source.init(cx, std::move(chars), length)) {
    return nullptr;
  }

  return CompileJsonModule(cx, options, source);
}

JS_PUBLIC_API JSObject* JS::CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  auto charRange =
      mozilla::Range<const char16_t>(srcBuf.get(), srcBuf.length());
  Rooted<JSONParser<char16_t>> parser(
      cx, cx, charRange, JSONParser<char16_t>::ParseType::JSONParse);

  parser.reportLineNumbersFromParsedData(true);
  parser.setFilename(options.filename());

  JS::RootedValue jsonValue(cx);
  if (!parser.parse(&jsonValue)) {
    return nullptr;
  }

  Rooted<ExportNameVector> exportNames(cx);
  if (!exportNames.append(cx->names().default_)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<ModuleObject*> moduleObject(
      cx, ModuleObject::createSynthetic(cx, &exportNames));
  if (!moduleObject) {
    return nullptr;
  }

  RootedVector<Value> exportValues(cx);
  if (!exportValues.append(jsonValue)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  if (!ModuleObject::createSyntheticEnvironment(cx, moduleObject,
                                                exportValues)) {
    return nullptr;
  }

  return moduleObject;
}

JS_PUBLIC_API void JS::SetModulePrivate(JSObject* module, const Value& value) {
  JSRuntime* rt = module->zone()->runtimeFromMainThread();
  module->as<ModuleObject>().scriptSourceObject()->setPrivate(rt, value);
}

JS_PUBLIC_API void JS::ClearModulePrivate(JSObject* module) {
  // |module| may be gray, be careful not to create edges to it.
  JSRuntime* rt = module->zone()->runtimeFromMainThread();
  module->as<ModuleObject>().scriptSourceObject()->clearPrivate(rt);
}

JS_PUBLIC_API JS::Value JS::GetModulePrivate(JSObject* module) {
  return module->as<ModuleObject>().scriptSourceObject()->getPrivate();
}

JS_PUBLIC_API bool JS::IsCyclicModule(JSObject* module) {
  return module->as<ModuleObject>().hasCyclicModuleFields();
}

JS_PUBLIC_API bool JS::ModuleLink(JSContext* cx, Handle<JSObject*> moduleArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg);

  return ::ModuleLink(cx, moduleArg.as<ModuleObject>());
}

JS_PUBLIC_API bool JS::LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> moduleArg, Handle<Value> hostDefined,
    JS::LoadModuleResolvedCallback resolved,
    JS::LoadModuleRejectedCallback rejected) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg);

  return js::LoadRequestedModules(cx, moduleArg.as<ModuleObject>(), hostDefined,
                                  resolved, rejected);
}

JS_PUBLIC_API bool JS::LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> moduleArg, Handle<Value> hostDefined,
    MutableHandle<JSObject*> promiseOut) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg);

  return js::LoadRequestedModules(cx, moduleArg.as<ModuleObject>(), hostDefined,
                                  promiseOut);
}

JS_PUBLIC_API bool JS::ModuleEvaluate(JSContext* cx,
                                      Handle<JSObject*> moduleRecord,
                                      MutableHandle<JS::Value> rval) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleRecord);

  cx->isEvaluatingModule++;
  auto guard = mozilla::MakeScopeExit([cx] {
    MOZ_ASSERT(cx->isEvaluatingModule != 0);
    cx->isEvaluatingModule--;
  });

  if (moduleRecord.as<ModuleObject>()->hasSyntheticModuleFields()) {
    return SyntheticModuleEvaluate(cx, moduleRecord.as<ModuleObject>(), rval);
  }

  return ::ModuleEvaluate(cx, moduleRecord.as<ModuleObject>(), rval);
}

JS_PUBLIC_API bool JS::ThrowOnModuleEvaluationFailure(
    JSContext* cx, Handle<JSObject*> evaluationPromise,
    ModuleErrorBehaviour errorBehaviour) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(evaluationPromise);

  return OnModuleEvaluationFailure(cx, evaluationPromise, errorBehaviour);
}

JS_PUBLIC_API uint32_t
JS::GetRequestedModulesCount(JSContext* cx, Handle<JSObject*> moduleRecord) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);

  return moduleRecord->as<ModuleObject>().requestedModules().Length();
}

JS_PUBLIC_API JSString* JS::GetRequestedModuleSpecifier(
    JSContext* cx, Handle<JSObject*> moduleRecord, uint32_t index) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);

  auto* moduleRequest = moduleRecord->as<ModuleObject>()
                            .requestedModules()[index]
                            .moduleRequest();

  // This implements step 7.1.1 in HostLoadImportedModule.
  // https://html.spec.whatwg.org/multipage/webappapis.html#hostloadimportedmodule
  //
  // If moduleRequest.[[Attributes]] contains a Record entry such that
  // entry.[[Key]] is not "type",
  if (moduleRequest->hasFirstUnsupportedAttributeKey()) {
    UniqueChars printableKey = AtomToPrintableString(
        cx, moduleRequest->getFirstUnsupportedAttributeKey());
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr,
        JSMSG_IMPORT_ATTRIBUTES_STATIC_IMPORT_UNSUPPORTED_ATTRIBUTE,
        printableKey ? printableKey.get() : "");
    return nullptr;
  }

  return moduleRequest->specifier();
}

JS_PUBLIC_API void JS::GetRequestedModuleSourcePos(
    JSContext* cx, Handle<JSObject*> moduleRecord, uint32_t index,
    uint32_t* lineNumber, JS::ColumnNumberOneOrigin* columnNumber) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);
  MOZ_ASSERT(lineNumber);
  MOZ_ASSERT(columnNumber);

  auto& module = moduleRecord->as<ModuleObject>();
  *lineNumber = module.requestedModules()[index].lineNumber();
  *columnNumber = module.requestedModules()[index].columnNumber();
}

JS_PUBLIC_API JS::ModuleType JS::GetRequestedModuleType(
    JSContext* cx, Handle<JSObject*> moduleRecord, uint32_t index) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);

  auto& module = moduleRecord->as<ModuleObject>();
  return module.requestedModules()[index].moduleRequest()->moduleType();
}

JS_PUBLIC_API JSScript* JS::GetModuleScript(JS::HandleObject moduleRecord) {
  AssertHeapIsIdle();

  auto& module = moduleRecord->as<ModuleObject>();

  // A synthetic module does not have a script associated with it.
  if (module.hasSyntheticModuleFields()) {
    return nullptr;
  }

  return module.script();
}

JS_PUBLIC_API JSObject* JS::GetModuleObject(HandleScript moduleScript) {
  AssertHeapIsIdle();
  MOZ_ASSERT(moduleScript->isModule());

  return moduleScript->module();
}

JS_PUBLIC_API JSObject* JS::GetModuleNamespace(JSContext* cx,
                                               HandleObject moduleRecord) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);
  MOZ_ASSERT(moduleRecord->is<ModuleObject>());

  return GetOrCreateModuleNamespace(cx, moduleRecord.as<ModuleObject>());
}

JS_PUBLIC_API JSObject* JS::GetModuleForNamespace(
    JSContext* cx, HandleObject moduleNamespace) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleNamespace);
  MOZ_ASSERT(moduleNamespace->is<ModuleNamespaceObject>());

  return &moduleNamespace->as<ModuleNamespaceObject>().module();
}

JS_PUBLIC_API JSObject* JS::GetModuleEnvironment(JSContext* cx,
                                                 Handle<JSObject*> moduleObj) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleObj);
  MOZ_ASSERT(moduleObj->is<ModuleObject>());

  return moduleObj->as<ModuleObject>().environment();
}

JS_PUBLIC_API JSObject* JS::CreateModuleRequest(JSContext* cx,
                                                Handle<JSString*> specifierArg,
                                                JS::ModuleType moduleType) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  Rooted<JSAtom*> specifierAtom(cx, AtomizeString(cx, specifierArg));
  if (!specifierAtom) {
    return nullptr;
  }

  return ModuleRequestObject::create(cx, specifierAtom, moduleType);
}

JS_PUBLIC_API JSString* JS::GetModuleRequestSpecifier(
    JSContext* cx, Handle<JSObject*> moduleRequestArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRequestArg);

  return moduleRequestArg->as<ModuleRequestObject>().specifier();
}

JS_PUBLIC_API JS::ModuleType JS::GetModuleRequestType(
    JSContext* cx, Handle<JSObject*> moduleRequestArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRequestArg);

  return moduleRequestArg->as<ModuleRequestObject>().moduleType();
}

JS_PUBLIC_API void JS::ClearModuleEnvironment(JSObject* moduleObj) {
  MOZ_ASSERT(moduleObj);
  AssertHeapIsIdle();

  js::ModuleEnvironmentObject* env =
      moduleObj->as<js::ModuleObject>().environment();
  if (!env) {
    return;
  }

  const JSClass* clasp = env->getClass();
  uint32_t numReserved = JSCLASS_RESERVED_SLOTS(clasp);
  uint32_t numSlots = env->slotSpan();
  for (uint32_t i = numReserved; i < numSlots; i++) {
    env->setSlot(i, UndefinedValue());
  }
}

JS_PUBLIC_API bool JS::ModuleIsLinked(JSObject* moduleObj) {
  AssertHeapIsIdle();
  return moduleObj->as<ModuleObject>().status() != ModuleStatus::New &&
         moduleObj->as<ModuleObject>().status() != ModuleStatus::Unlinked;
}

////////////////////////////////////////////////////////////////////////////////
// Internal implementation

class ResolveSetEntry {
  ModuleObject* module_;
  JSAtom* exportName_;

 public:
  ResolveSetEntry(ModuleObject* module, JSAtom* exportName)
      : module_(module), exportName_(exportName) {}

  ModuleObject* module() const { return module_; }
  JSAtom* exportName() const { return exportName_; }

  void trace(JSTracer* trc) {
    TraceRoot(trc, &module_, "ResolveSetEntry::module_");
    TraceRoot(trc, &exportName_, "ResolveSetEntry::exportName_");
  }
};

using ResolveSet = GCVector<ResolveSetEntry, 0, SystemAllocPolicy>;

using ModuleSet =
    GCHashSet<ModuleObject*, DefaultHasher<ModuleObject*>, SystemAllocPolicy>;

static bool CyclicModuleResolveExport(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      Handle<JSAtom*> exportName,
                                      MutableHandle<ResolveSet> resolveSet,
                                      MutableHandle<Value> result,
                                      ModuleErrorInfo* errorInfoOut = nullptr);
static bool SyntheticModuleResolveExport(JSContext* cx,
                                         Handle<ModuleObject*> module,
                                         Handle<JSAtom*> exportName,
                                         MutableHandle<Value> result,
                                         ModuleErrorInfo* errorInfoOut);
static ModuleNamespaceObject* ModuleNamespaceCreate(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<UniquePtr<ExportNameVector>> exports);
static bool InnerModuleLinking(JSContext* cx, Handle<ModuleObject*> module,
                               MutableHandle<ModuleVector> stack, size_t index,
                               size_t* indexOut);
static bool InnerModuleEvaluation(JSContext* cx, Handle<ModuleObject*> module,
                                  MutableHandle<ModuleVector> stack,
                                  size_t index, size_t* indexOut);
static bool ExecuteAsyncModule(JSContext* cx, Handle<ModuleObject*> module);
static bool GatherAvailableModuleAncestors(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleVector> execList);

static const char* ModuleStatusName(ModuleStatus status) {
  switch (status) {
    case ModuleStatus::New:
      return "New";
    case ModuleStatus::Unlinked:
      return "Unlinked";
    case ModuleStatus::Linking:
      return "Linking";
    case ModuleStatus::Linked:
      return "Linked";
    case ModuleStatus::Evaluating:
      return "Evaluating";
    case ModuleStatus::EvaluatingAsync:
      return "EvaluatingAsync";
    case ModuleStatus::Evaluated:
      return "Evaluated";
    default:
      MOZ_CRASH("Unexpected ModuleStatus");
  }
}

static bool ContainsElement(const ExportNameVector& list, JSAtom* atom) {
  for (JSAtom* a : list) {
    if (a == atom) {
      return true;
    }
  }

  return false;
}

static bool ContainsElement(Handle<ModuleVector> stack, ModuleObject* module) {
  for (ModuleObject* m : stack) {
    if (m == module) {
      return true;
    }
  }

  return false;
}

#ifdef DEBUG
static size_t CountElements(Handle<ModuleVector> stack, ModuleObject* module) {
  size_t count = 0;
  for (ModuleObject* m : stack) {
    if (m == module) {
      count++;
    }
  }

  return count;
}
#endif

// https://tc39.es/proposal-json-modules/#sec-smr-getexportednames
static bool SyntheticModuleGetExportedNames(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ExportNameVector> exportedNames) {
  MOZ_ASSERT(exportedNames.empty());

  if (!exportedNames.appendAll(module->syntheticExportNames())) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

// https://tc39.es/ecma262/#sec-GetImportedModule
static ModuleObject* GetImportedModule(
    JSContext* cx, Handle<ModuleObject*> referrer,
    Handle<ModuleRequestObject*> moduleRequest) {
  MOZ_ASSERT(referrer);
  MOZ_ASSERT(moduleRequest);

  // Step 1. Assert: Exactly one element of referrer.[[LoadedModules]] is a
  //         Record whose [[Specifier]] is specifier, since LoadRequestedModules
  //         has completed successfully on referrer prior to invoking this
  //         abstract operation.
  //
  //         Impl note: Updated to perform lookup using ModuleRequestObject as
  //         per the Import Attributes Proposal.
  auto record = referrer->loadedModules().lookup(moduleRequest);
  MOZ_ASSERT(record);

  // Step 2. Let record be the Record in referrer.[[LoadedModules]] whose
  //         [[Specifier]] is specifier.
  // Step 3. Return record.[[Module]].
  return record->value();
}

// https://tc39.es/ecma262/#sec-getexportednames
// ES2023 16.2.1.6.2 GetExportedNames
static bool ModuleGetExportedNames(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleSet> exportStarSet,
    MutableHandle<ExportNameVector> exportedNames) {
  // Step 4. Let exportedNames be a new empty List.
  MOZ_ASSERT(exportedNames.empty());

  if (module->hasSyntheticModuleFields()) {
    return SyntheticModuleGetExportedNames(cx, module, exportedNames);
  }

  // Step 2. If exportStarSet contains module, then:
  if (exportStarSet.has(module)) {
    // Step 2.a. We've reached the starting point of an export * circularity.
    // Step 2.b. Return a new empty List.
    return true;
  }

  // Step 3. Append module to exportStarSet.
  if (!exportStarSet.put(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. For each ExportEntry Record e of module.[[LocalExportEntries]], do:
  for (const ExportEntry& e : module->localExportEntries()) {
    // Step 5.a. Assert: module provides the direct binding for this export.
    // Step 5.b. Append e.[[ExportName]] to exportedNames.
    if (!exportedNames.append(e.exportName())) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  // Step 6. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 6.a. Assert: module imports a specific binding for this export.
    // Step 6.b. Append e.[[ExportName]] to exportedNames.
    if (!exportedNames.append(e.exportName())) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  // Step 7. For each ExportEntry Record e of module.[[StarExportEntries]], do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> requestedModule(cx);
  Rooted<JSAtom*> name(cx);
  for (const ExportEntry& e : module->starExportEntries()) {
    // Step 7.a. Let requestedModule be ? GetImportedModule(module,
    //           e.[[ModuleRequest]]).
    moduleRequest = e.moduleRequest();
    requestedModule = GetImportedModule(cx, module, moduleRequest);
    if (!requestedModule) {
      return false;
    }
    MOZ_ASSERT(requestedModule->status() >= ModuleStatus::Unlinked);

    // Step 7.b. Let starNames be ?
    //           requestedModule.GetExportedNames(exportStarSet).
    Rooted<ExportNameVector> starNames(cx);
    if (!ModuleGetExportedNames(cx, requestedModule, exportStarSet,
                                &starNames)) {
      return false;
    }

    // Step 7.c. For each element n of starNames, do:
    for (JSAtom* name : starNames) {
      // Step 7.c.i. If SameValue(n, "default") is false, then:
      if (name != cx->names().default_) {
        // Step 7.c.i.1. If n is not an element of exportedNames, then:
        if (!ContainsElement(exportedNames, name)) {
          // Step 7.c.i.1.a. Append n to exportedNames.
          if (!exportedNames.append(name)) {
            ReportOutOfMemory(cx);
            return false;
          }
        }
      }
    }
  }

  // Step 8. Return exportedNames.
  return true;
}

static void ThrowUnexpectedModuleStatus(JSContext* cx, ModuleStatus status) {
  JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                           JSMSG_BAD_MODULE_STATUS, ModuleStatusName(status));
}

// https://tc39.es/ecma262/#sec-HostLoadImportedModule
//
// According to spec the referrer can be a module script, classic script or
// realm. The first two are supplied to this function by passing the script.
// When the referrer is a realm nullptr is passed.
bool js::HostLoadImportedModule(JSContext* cx, Handle<JSScript*> referrer,
                                Handle<JSObject*> moduleRequest,
                                Handle<Value> hostDefined,
                                Handle<Value> payload) {
  MOZ_ASSERT(moduleRequest);
  MOZ_ASSERT(!payload.isUndefined());

  JS::ModuleLoadHook moduleLoadHook = cx->runtime()->moduleLoadHook;
  if (!moduleLoadHook) {
    JS_ReportErrorASCII(cx, "Module load hook not set");
    return false;
  }

  bool ok = moduleLoadHook(cx, referrer, moduleRequest, hostDefined, payload);

  if (!ok) {
    MOZ_ASSERT(JS_IsExceptionPending(cx));
    if (JS_IsExceptionPending(cx)) {
      return JS::FinishLoadingImportedModuleFailedWithPendingException(cx,
                                                                       payload);
    }

    return JS::FinishLoadingImportedModuleFailed(cx, payload,
                                                 UndefinedHandleValue);
  }

  return true;
}

static bool ModuleResolveExportImpl(JSContext* cx, Handle<ModuleObject*> module,
                                    Handle<JSAtom*> exportName,
                                    MutableHandle<ResolveSet> resolveSet,
                                    MutableHandle<Value> result,
                                    ModuleErrorInfo* errorInfoOut = nullptr) {
  if (module->hasSyntheticModuleFields()) {
    return SyntheticModuleResolveExport(cx, module, exportName, result,
                                        errorInfoOut);
  }

  return CyclicModuleResolveExport(cx, module, exportName, resolveSet, result,
                                   errorInfoOut);
}

// https://tc39.es/ecma262/#sec-resolveexport
// ES2023 16.2.1.6.3 ResolveExport
//
// Returns an value describing the location of the resolved export or indicating
// a failure.
//
// On success this returns a resolved binding record: { module, bindingName }
//
// There are two failure cases:
//
//  - If no definition was found or the request is found to be circular, *null*
//    is returned.
//
//  - If the request is found to be ambiguous, the string `"ambiguous"` is
//    returned.
//
static bool ModuleResolveExport(JSContext* cx, Handle<ModuleObject*> module,
                                Handle<JSAtom*> exportName,
                                MutableHandle<Value> result,
                                ModuleErrorInfo* errorInfoOut = nullptr) {
  // Step 1. Assert: module.[[Status]] is not new.
  MOZ_ASSERT(module->status() != ModuleStatus::New);

  // Step 2. If resolveSet is not present, set resolveSet to a new empty List.
  Rooted<ResolveSet> resolveSet(cx);
  return ModuleResolveExportImpl(cx, module, exportName, &resolveSet, result,
                                 errorInfoOut);
}

static bool CreateResolvedBindingObject(JSContext* cx,
                                        Handle<ModuleObject*> module,
                                        Handle<JSAtom*> bindingName,
                                        MutableHandle<Value> result) {
  ResolvedBindingObject* obj =
      ResolvedBindingObject::create(cx, module, bindingName);
  if (!obj) {
    return false;
  }

  result.setObject(*obj);
  return true;
}

static bool CyclicModuleResolveExport(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      Handle<JSAtom*> exportName,
                                      MutableHandle<ResolveSet> resolveSet,
                                      MutableHandle<Value> result,
                                      ModuleErrorInfo* errorInfoOut) {
  // Step 2. For each Record { [[Module]], [[ExportName]] } r of resolveSet, do:
  for (const auto& entry : resolveSet) {
    // Step 3.a. If module and r.[[Module]] are the same Module Record and
    //           exportName is r.[[ExportName]], then:
    if (entry.module() == module && entry.exportName() == exportName) {
      // Step 3.a.i. Assert: This is a circular import request.
      // Step 3.a.ii. Return null.
      result.setNull();
      if (errorInfoOut) {
        errorInfoOut->setCircularImport(cx, module);
      }
      return true;
    }
  }

  // Step 4. Append the Record { [[Module]]: module, [[ExportName]]: exportName
  //         } to resolveSet.
  if (!resolveSet.emplaceBack(module, exportName)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. For each ExportEntry Record e of module.[[LocalExportEntries]], do:
  for (const ExportEntry& e : module->localExportEntries()) {
    // Step 5.a. If e.[[ExportName]] is exportName, then:
    if (exportName == e.exportName()) {
      // Step 5.a.i. Assert: module provides the direct binding for this export.
      // Step 5.a.ii. Return ResolvedBinding Record { [[Module]]: module,
      //              [[BindingName]]: e.[[LocalName]] }.
      Rooted<JSAtom*> localName(cx, e.localName());
      return CreateResolvedBindingObject(cx, module, localName, result);
    }
  }

  // Step 6. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> name(cx);
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 6.a. If e.[[ExportName]] is exportName, then:
    if (exportName == e.exportName()) {
      // Step 6.a.i. Assert: e.[[ModuleRequest]] is not null.
      MOZ_ASSERT(e.moduleRequest());

      // Step 6.a.ii. Let importedModule be ? GetImportedModule(module,
      //              e.[[ModuleRequest]]).
      moduleRequest = e.moduleRequest();
      importedModule = GetImportedModule(cx, module, moduleRequest);
      if (!importedModule) {
        return false;
      }
      MOZ_ASSERT(importedModule->status() >= ModuleStatus::Unlinked);

      // Step 6.a.iii. If e.[[ImportName]] is ALL, then:
      if (!e.importName()) {
        // Step 6.a.iii.1. Assert: module does not provide the direct binding
        //                 for this export.
        // Step 6.a.iii.2. Return ResolvedBinding Record { [[Module]]:
        //                 importedModule, [[BindingName]]: NAMESPACE }.
        name = cx->names().star_namespace_star_;
        return CreateResolvedBindingObject(cx, importedModule, name, result);
      } else {
        // Step 6.a.iv.1. Assert: module imports a specific binding for this
        //                export.
        // Step 6.a.iv.2. Return ? importedModule.ResolveExport(e.[[ImportName]]
        //                , resolveSet).
        name = e.importName();

        return ModuleResolveExportImpl(cx, importedModule, name, resolveSet,
                                       result, errorInfoOut);
      }
    }
  }

  // Step 7. If exportName is "default"), then:
  if (exportName == cx->names().default_) {
    // Step 7.a. Assert: A default export was not explicitly defined by this
    //           module.
    // Step 7.b. Return null.
    // Step 7.c. NOTE: A default export cannot be provided by an export * from
    //           "mod" declaration.
    result.setNull();
    if (errorInfoOut) {
      errorInfoOut->setImportedModule(cx, module);
    }
    return true;
  }

  // Step 8. Let starResolution be null.
  Rooted<ResolvedBindingObject*> starResolution(cx);

  // Step 9. For each ExportEntry Record e of module.[[StarExportEntries]], do:
  Rooted<Value> resolution(cx);
  Rooted<ResolvedBindingObject*> binding(cx);
  for (const ExportEntry& e : module->starExportEntries()) {
    // Step 9.a. Assert: e.[[ModuleRequest]] is not null.
    MOZ_ASSERT(e.moduleRequest());

    // Step 9.b. Let importedModule be ? GetImportedModule(module,
    //           e.[[ModuleRequest]]).
    moduleRequest = e.moduleRequest();
    importedModule = GetImportedModule(cx, module, moduleRequest);
    if (!importedModule) {
      return false;
    }
    MOZ_ASSERT(importedModule->status() >= ModuleStatus::Unlinked);

    // Step 9.c. Let resolution be ? importedModule.ResolveExport(exportName,
    //           resolveSet).
    if (!CyclicModuleResolveExport(cx, importedModule, exportName, resolveSet,
                                   &resolution, errorInfoOut)) {
      return false;
    }

    // Step 9.d. If resolution is AMBIGUOUS, return AMBIGUOUS.
    if (resolution == StringValue(cx->names().ambiguous)) {
      result.set(resolution);
      return true;
    }

    // Step 9.e. If resolution is not null, then:
    if (!resolution.isNull()) {
      // Step 9.e.i. Assert: resolution is a ResolvedBinding Record.
      binding = &resolution.toObject().as<ResolvedBindingObject>();

      // Step 9.e.ii. If starResolution is null, set starResolution to
      // resolution.
      if (!starResolution) {
        starResolution = binding;
      } else {
        // Step 9.e.iii. Else:
        // Step 9.e.iii.1. Assert: There is more than one * import that includes
        //                 the requested name.
        // Step 9.e.iii.2. If resolution.[[Module]] and
        //                 starResolution.[[Module]] are not the same Module
        //                 Record, return AMBIGUOUS.
        // Step 9.e.iii.3. If resolution.[[BindingName]] is not
        //                 starResolution.[[BindingName]] and either
        //                 resolution.[[BindingName]] or
        //                 starResolution.[[BindingName]] is namespace, return
        //                 AMBIGUOUS.
        // Step 9.e.iii.4. If resolution.[[BindingName]] is a String,
        //                 starResolution.[[BindingName]] is a String, and
        //                 resolution.[[BindingName]] is not
        //                 starResolution.[[BindingName]]), return AMBIGUOUS.
        if (binding->module() != starResolution->module() ||
            binding->bindingName() != starResolution->bindingName()) {
          result.set(StringValue(cx->names().ambiguous));

          if (errorInfoOut) {
            ModuleObject* module1 = starResolution->module();
            ModuleObject* module2 = binding->module();
            errorInfoOut->setForAmbiguousImport(cx, module, module1, module2);
          }
          return true;
        }
      }
    }
  }

  // Step 10. Return starResolution.
  result.setObjectOrNull(starResolution);
  if (!starResolution && errorInfoOut) {
    errorInfoOut->setImportedModule(cx, module);
  }
  return true;
}

// https://tc39.es/proposal-json-modules/#sec-smr-resolveexport
static bool SyntheticModuleResolveExport(JSContext* cx,
                                         Handle<ModuleObject*> module,
                                         Handle<JSAtom*> exportName,
                                         MutableHandle<Value> result,
                                         ModuleErrorInfo* errorInfoOut) {
  // Step 2. If module.[[ExportNames]] does not contain exportName, return null.
  if (!ContainsElement(module->syntheticExportNames(), exportName)) {
    result.setNull();
    if (errorInfoOut) {
      errorInfoOut->setImportedModule(cx, module);
    }
    return true;
  }

  // Step 3. Return ResolvedBinding Record { [[Module]]: module,
  // [[BindingName]]: exportName }.
  return CreateResolvedBindingObject(cx, module, exportName, result);
}

// https://tc39.es/ecma262/#sec-getmodulenamespace
// ES2023 16.2.1.10 GetModuleNamespace
ModuleNamespaceObject* js::GetOrCreateModuleNamespace(
    JSContext* cx, Handle<ModuleObject*> module) {
  // Step 1. Assert: If module is a Cyclic Module Record, then module.[[Status]]
  //         is not new or unlinked.
  MOZ_ASSERT(module->status() != ModuleStatus::New ||
             module->status() != ModuleStatus::Unlinked);

  // Step 2. Let namespace be module.[[Namespace]].
  Rooted<ModuleNamespaceObject*> ns(cx, module->namespace_());

  // Step 3. If namespace is empty, then:
  if (!ns) {
    // Step 3.a. Let exportedNames be ? module.GetExportedNames().
    Rooted<ModuleSet> exportStarSet(cx);
    Rooted<ExportNameVector> exportedNames(cx);
    if (!ModuleGetExportedNames(cx, module, &exportStarSet, &exportedNames)) {
      return nullptr;
    }

    // Step 3.b. Let unambiguousNames be a new empty List.
    Rooted<UniquePtr<ExportNameVector>> unambiguousNames(
        cx, cx->make_unique<ExportNameVector>());
    if (!unambiguousNames) {
      return nullptr;
    }

    // Step 3.c. For each element name of exportedNames, do:
    Rooted<JSAtom*> name(cx);
    Rooted<Value> resolution(cx);
    for (JSAtom* atom : exportedNames) {
      name = atom;

      // Step 3.c.i. Let resolution be ? module.ResolveExport(name).
      if (!ModuleResolveExport(cx, module, name, &resolution)) {
        return nullptr;
      }

      // Step 3.c.ii. If resolution is a ResolvedBinding Record, append name to
      //              unambiguousNames.
      if (resolution.isObject() && !unambiguousNames->append(name)) {
        ReportOutOfMemory(cx);
        return nullptr;
      }
    }

    // Step 3.d. Set namespace to ModuleNamespaceCreate(module,
    //           unambiguousNames).
    ns = ModuleNamespaceCreate(cx, module, &unambiguousNames);
  }

  // Step 4. Return namespace.
  return ns;
}

static bool IsResolvedBinding(JSContext* cx, Handle<Value> resolution) {
  MOZ_ASSERT(resolution.isObjectOrNull() ||
             resolution.toString() == cx->names().ambiguous);
  return resolution.isObject();
}

static void InitNamespaceBinding(JSContext* cx,
                                 Handle<ModuleEnvironmentObject*> env,
                                 Handle<JSAtom*> name,
                                 Handle<ModuleNamespaceObject*> ns) {
  // The property already exists in the evironment but is not writable, so set
  // the slot directly.
  RootedId id(cx, AtomToId(name));
  mozilla::Maybe<PropertyInfo> prop = env->lookup(cx, id);
  MOZ_ASSERT(prop.isSome());
  env->setSlot(prop->slot(), ObjectValue(*ns));
}

struct AtomComparator {
  bool operator()(JSAtom* a, JSAtom* b, bool* lessOrEqualp) {
    int32_t result = CompareStrings(a, b);
    *lessOrEqualp = (result <= 0);
    return true;
  }
};

// https://tc39.es/ecma262/#sec-modulenamespacecreate
// ES2023 10.4.6.12 ModuleNamespaceCreate
static ModuleNamespaceObject* ModuleNamespaceCreate(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<UniquePtr<ExportNameVector>> exports) {
  // Step 1. Assert: module.[[Namespace]] is empty.
  MOZ_ASSERT(!module->namespace_());

  // Step 6. Let sortedExports be a List whose elements are the elements of
  //         exports ordered as if an Array of the same values had been sorted
  //         using %Array.prototype.sort% using undefined as comparefn.
  ExportNameVector scratch;
  if (!scratch.resize(exports->length())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  MOZ_ALWAYS_TRUE(MergeSort(exports->begin(), exports->length(),
                            scratch.begin(), AtomComparator()));

  // Steps 2 - 5.
  Rooted<ModuleNamespaceObject*> ns(
      cx, ModuleObject::createNamespace(cx, module, exports));
  if (!ns) {
    return nullptr;
  }

  // Pre-compute all binding mappings now instead of on each access.
  // See:
  // https://tc39.es/ecma262/#sec-module-namespace-exotic-objects-get-p-receiver
  // ES2023 10.4.6.8 Module Namespace Exotic Object [[Get]]
  Rooted<JSAtom*> name(cx);
  Rooted<Value> resolution(cx);
  Rooted<ResolvedBindingObject*> binding(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<ModuleNamespaceObject*> importedNamespace(cx);
  Rooted<JSAtom*> bindingName(cx);
  for (JSAtom* atom : ns->exports()) {
    name = atom;

    if (!ModuleResolveExport(cx, module, name, &resolution)) {
      return nullptr;
    }

    MOZ_ASSERT(IsResolvedBinding(cx, resolution));
    binding = &resolution.toObject().as<ResolvedBindingObject>();
    importedModule = binding->module();
    bindingName = binding->bindingName();

    if (bindingName == cx->names().star_namespace_star_) {
      importedNamespace = GetOrCreateModuleNamespace(cx, importedModule);
      if (!importedNamespace) {
        return nullptr;
      }

      // The spec uses an immutable binding here but we have already generated
      // bytecode for an indirect binding. Instead, use an indirect binding to
      // "*namespace*" slot of the target environment.
      Rooted<ModuleEnvironmentObject*> env(
          cx, &importedModule->initialEnvironment());
      InitNamespaceBinding(cx, env, bindingName, importedNamespace);
    }

    if (!ns->addBinding(cx, name, importedModule, bindingName)) {
      return nullptr;
    }
  }

  // Step 10. Return M.
  return ns;
}

void ModuleErrorInfo::setImportedModule(JSContext* cx,
                                        ModuleObject* importedModule) {
  imported = importedModule->filename();
}

void ModuleErrorInfo::setCircularImport(JSContext* cx,
                                        ModuleObject* importedModule) {
  setImportedModule(cx, importedModule);
  isCircular = true;
}

void ModuleErrorInfo::setForAmbiguousImport(JSContext* cx,
                                            ModuleObject* importedModule,
                                            ModuleObject* module1,
                                            ModuleObject* module2) {
  setImportedModule(cx, importedModule);
  entry1 = module1->filename();
  entry2 = module2->filename();
}

static void CreateErrorNumberMessageUTF8(JSContext* cx, unsigned errorNumber,
                                         JSErrorReport* reportOut, ...) {
  va_list ap;
  va_start(ap, reportOut);
  AutoReportFrontendContext fc(cx);
  if (!ExpandErrorArgumentsVA(&fc, GetErrorMessage, nullptr, errorNumber,
                              ArgumentsAreUTF8, reportOut, ap)) {
    ReportOutOfMemory(cx);
    return;
  }

  va_end(ap);
}

static void ThrowResolutionError(JSContext* cx, Handle<ModuleObject*> module,
                                 Handle<Value> resolution, Handle<JSAtom*> name,
                                 ModuleErrorInfo* errorInfo) {
  MOZ_ASSERT(errorInfo);
  auto chars = StringToNewUTF8CharsZ(cx, *name);
  if (!chars) {
    ReportOutOfMemory(cx);
    return;
  }

  bool isAmbiguous = resolution == StringValue(cx->names().ambiguous);

  unsigned errorNumber;
  if (errorInfo->isCircular) {
    errorNumber = JSMSG_MODULE_CIRCULAR_IMPORT;
  } else if (isAmbiguous) {
    errorNumber = JSMSG_MODULE_AMBIGUOUS;
  } else {
    errorNumber = JSMSG_MODULE_NO_EXPORT;
  }

  JSErrorReport report;
  report.isWarning_ = false;
  report.errorNumber = errorNumber;

  if (errorNumber == JSMSG_MODULE_AMBIGUOUS) {
    CreateErrorNumberMessageUTF8(cx, errorNumber, &report, errorInfo->imported,
                                 chars.get(), errorInfo->entry1,
                                 errorInfo->entry2);
  } else {
    CreateErrorNumberMessageUTF8(cx, errorNumber, &report, errorInfo->imported,
                                 chars.get());
  }

  Rooted<JSString*> message(cx, report.newMessageString(cx));
  if (!message) {
    ReportOutOfMemory(cx);
    return;
  }

  const char* file = module->filename();
  RootedString filename(
      cx, JS_NewStringCopyUTF8Z(cx, JS::ConstUTF8CharsZ(file, strlen(file))));
  if (!filename) {
    ReportOutOfMemory(cx);
    return;
  }

  RootedValue error(cx);
  if (!JS::CreateError(cx, JSEXN_SYNTAXERR, nullptr, filename,
                       errorInfo->lineNumber, errorInfo->columnNumber, nullptr,
                       message, JS::NothingHandleValue, &error)) {
    ReportOutOfMemory(cx);
    return;
  }

  cx->setPendingException(error, nullptr);
}

// https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment
// ES2023 16.2.1.6.4 InitializeEnvironment
static bool ModuleInitializeEnvironment(JSContext* cx,
                                        Handle<ModuleObject*> module) {
  MOZ_ASSERT(module->status() == ModuleStatus::Linking);

  // Step 1. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  Rooted<JSAtom*> exportName(cx);
  Rooted<Value> resolution(cx);
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 1.a. Assert: e.[[ExportName]] is not null.
    MOZ_ASSERT(e.exportName());

    // Step 1.b. Let resolution be ? module.ResolveExport(e.[[ExportName]]).
    exportName = e.exportName();
    ModuleErrorInfo errorInfo{e.lineNumber(), e.columnNumber()};
    if (!ModuleResolveExport(cx, module, exportName, &resolution, &errorInfo)) {
      return false;
    }

    // Step 1.c. If resolution is either null or AMBIGUOUS, throw a SyntaxError
    //           exception.
    if (!IsResolvedBinding(cx, resolution)) {
      ThrowResolutionError(cx, module, resolution, exportName, &errorInfo);
      return false;
    }
  }

  // Step 5. Let env be NewModuleEnvironment(realm.[[GlobalEnv]]).
  // Step 6. Set module.[[Environment]] to env.
  // Note that we have already created the environment by this point.
  Rooted<ModuleEnvironmentObject*> env(cx, &module->initialEnvironment());

  // Step 7. For each ImportEntry Record in of module.[[ImportEntries]], do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> importName(cx);
  Rooted<JSAtom*> localName(cx);
  Rooted<ModuleObject*> sourceModule(cx);
  Rooted<JSAtom*> bindingName(cx);
  for (const ImportEntry& in : module->importEntries()) {
    // Step 7.a. Let importedModule be ! GetImportedModule(module,
    //           in.[[ModuleRequest]]).
    moduleRequest = in.moduleRequest();
    importedModule = GetImportedModule(cx, module, moduleRequest);
    if (!importedModule) {
      return false;
    }
    MOZ_ASSERT(importedModule->status() >= ModuleStatus::Linking);

    localName = in.localName();
    importName = in.importName();

    // Step 7.c. If in.[[ImportName]] is namespace-object, then:
    if (!importName) {
      // Step 7.c.i. Let namespace be ? GetModuleNamespace(importedModule).
      Rooted<ModuleNamespaceObject*> ns(
          cx, GetOrCreateModuleNamespace(cx, importedModule));
      if (!ns) {
        return false;
      }

      // Step 7.c.ii. Perform ! env.CreateImmutableBinding(in.[[LocalName]],
      // true). This happens when the environment is created.

      // Step 7.c.iii. Perform ! env.InitializeBinding(in.[[LocalName]],
      // namespace).
      InitNamespaceBinding(cx, env, localName, ns);
    } else {
      // Step 7.d. Else:
      // Step 7.d.i. Let resolution be ?
      // importedModule.ResolveExport(in.[[ImportName]]).
      ModuleErrorInfo errorInfo{in.lineNumber(), in.columnNumber()};
      if (!ModuleResolveExport(cx, importedModule, importName, &resolution,
                               &errorInfo)) {
        return false;
      }

      // Step 7.d.ii. If resolution is null or ambiguous, throw a SyntaxError
      //              exception.
      if (!IsResolvedBinding(cx, resolution)) {
        ThrowResolutionError(cx, module, resolution, importName, &errorInfo);
        return false;
      }

      auto* binding = &resolution.toObject().as<ResolvedBindingObject>();
      sourceModule = binding->module();
      bindingName = binding->bindingName();

      // Step 7.d.iii. If resolution.[[BindingName]] is namespace, then:
      if (bindingName == cx->names().star_namespace_star_) {
        // Step 7.d.iii.1. Let namespace be ?
        //                 GetModuleNamespace(resolution.[[Module]]).
        Rooted<ModuleNamespaceObject*> ns(
            cx, GetOrCreateModuleNamespace(cx, sourceModule));
        if (!ns) {
          return false;
        }

        // Step 7.d.iii.2. Perform !
        //                 env.CreateImmutableBinding(in.[[LocalName]], true).
        // Step 7.d.iii.3. Perform ! env.InitializeBinding(in.[[LocalName]],
        //                 namespace).
        //
        // This should be InitNamespaceBinding, but we have already generated
        // bytecode assuming an indirect binding. Instead, ensure a special
        // "*namespace*"" binding exists on the target module's environment. We
        // then generate an indirect binding to this synthetic binding.
        Rooted<ModuleEnvironmentObject*> sourceEnv(
            cx, &sourceModule->initialEnvironment());
        InitNamespaceBinding(cx, sourceEnv, bindingName, ns);
        if (!env->createImportBinding(cx, localName, sourceModule,
                                      bindingName)) {
          return false;
        }
      } else {
        // Step 7.d.iv. Else:
        // Step 7.d.iv.1. 1. Perform env.CreateImportBinding(in.[[LocalName]],
        //                   resolution.[[Module]], resolution.[[BindingName]]).
        if (!env->createImportBinding(cx, localName, sourceModule,
                                      bindingName)) {
          return false;
        }
      }
    }
  }

  // Steps 8-26.
  //
  // Some of these do not need to happen for practical purposes. For steps
  // 21-23, the bindings that can be handled in a similar way to regulars
  // scripts are done separately. Function Declarations are special due to
  // hoisting and are handled within this function. See ModuleScope and
  // ModuleEnvironmentObject for further details.

  // Step 24. For each element d of lexDeclarations, do:
  // Step 24.a. For each element dn of the BoundNames of d, do:
  // Step 24.a.iii. If d is a FunctionDeclaration, a GeneratorDeclaration, an
  //                AsyncFunctionDeclaration, or an AsyncGeneratorDeclaration,
  //                then:
  // Step 24.a.iii.1 Let fo be InstantiateFunctionObject of d with arguments env
  //                 and privateEnv.
  // Step 24.a.iii.2. Perform ! env.InitializeBinding(dn, fo).
  return ModuleObject::instantiateFunctionDeclarations(cx, module);
}

static bool FailWithUnsupportedAttributeException(
    JSContext* cx, Handle<GraphLoadingStateRecordObject*> state,
    Handle<ModuleRequestObject*> moduleRequest) {
  UniqueChars printableKey = AtomToPrintableString(
      cx, moduleRequest->getFirstUnsupportedAttributeKey());
  JS_ReportErrorNumberASCII(
      cx, GetErrorMessage, nullptr,
      JSMSG_IMPORT_ATTRIBUTES_STATIC_IMPORT_UNSUPPORTED_ATTRIBUTE,
      printableKey ? printableKey.get() : "");

  JS::ExceptionStack exnStack(cx);
  if (!JS::StealPendingExceptionStack(cx, &exnStack)) {
    return false;
  }

  ContinueModuleLoading(cx, state, nullptr, exnStack.exception());
  return true;
}

// https://tc39.es/ecma262/#sec-InnerModuleLoading
// InnerModuleLoading ( state, module )
static bool InnerModuleLoading(JSContext* cx,
                               Handle<GraphLoadingStateRecordObject*> state,
                               Handle<ModuleObject*> module) {
  MOZ_ASSERT(state);
  MOZ_ASSERT(module);

  // Step 1. Assert: state.[[IsLoading]] is true.
  MOZ_ASSERT(state->isLoading());

  // Step 2. If module is a Cyclic Module Record, module.[[Status]] is new, and
  // state.[[Visited]] does not contain module, then
  if (module->hasCyclicModuleFields() &&
      module->status() == ModuleStatus::New && !state->visited().has(module)) {
    // Step 2.a. Append module to state.[[Visited]].
    if (!state->visited().putNew(module)) {
      ReportOutOfMemory(cx);
      return false;
    }

    // Step 2.b. Let requestedModulesCount be the number of elements in
    //           module.[[RequestedModules]].
    size_t requestedModulesCount = module->requestedModules().Length();

    // Step 2.c. Set state.[[PendingModulesCount]] to
    //           state.[[PendingModulesCount]] + requestedModulesCount.
    uint32_t count = state->pendingModulesCount() + requestedModulesCount;
    state->setPendingModulesCount(count);

    // Step 2.d. For each String required of module.[[RequestedModules]], do
    Rooted<ModuleRequestObject*> moduleRequest(cx);
    Rooted<ModuleObject*> recordModule(cx);
    Rooted<JSAtom*> invalidKey(cx);
    for (const RequestedModule& request : module->requestedModules()) {
      moduleRequest = request.moduleRequest();

      // https://tc39.es/proposal-import-attributes/#sec-InnerModuleLoading
      if (moduleRequest->hasFirstUnsupportedAttributeKey()) {
        if (!FailWithUnsupportedAttributeException(cx, state, moduleRequest)) {
          return false;
        }
      } else if (auto record = module->loadedModules().lookup(moduleRequest)) {
        // Step 2.d.i. If module.[[LoadedModules]] contains a Record whose
        //             [[Specifier]] is required, then
        // Step 2.d.i.1. Let record be that Record.
        // Step 2.d.i.2. Perform InnerModuleLoading(state, record.[[Module]]).
        recordModule = record->value();
        if (!InnerModuleLoading(cx, state, recordModule)) {
          return false;
        }
      } else {
        // Step 2.d.ii. Else,
        // Step 2.d.ii.1. Perform HostLoadImportedModule(module, required,
        //                state.[[HostDefined]], state).
        Rooted<JSScript*> referrer(cx, module->script());
        Rooted<Value> hostDefined(cx, state->hostDefined());
        Rooted<Value> payload(cx, ObjectValue(*state));
        if (!HostLoadImportedModule(cx, referrer, moduleRequest, hostDefined,
                                    payload)) {
          return false;
        }
      }

      // Step 2.d.iii. If state.[[IsLoading]] is false, return unused.
      if (!state->isLoading()) {
        return true;
      }
    }
  }

  // Step 3. Assert: state.[[PendingModulesCount]] ≥ 1.
  MOZ_ASSERT(state->pendingModulesCount() >= 1);

  // Step 4. Set state.[[PendingModulesCount]] to
  //         state.[[PendingModulesCount]] - 1.
  uint32_t count = state->pendingModulesCount() - 1;
  state->setPendingModulesCount(count);

  // Step 5. If state.[[PendingModulesCount]] = 0, then
  if (state->pendingModulesCount() == 0) {
    // Step 5.a. Set state.[[IsLoading]] to false.
    state->setIsLoading(false);

    // Step 5.b. For each Cyclic Module Record loaded of state.[[Visited]], do
    for (auto iter = state->visited().iter(); !iter.done(); iter.next()) {
      auto& loaded = iter.get();
      // Step 5.b.i. If loaded.[[Status]] is new, set loaded.[[Status]] to
      // unlinked.
      if (loaded->status() == ModuleStatus::New) {
        loaded->setStatus(ModuleStatus::Unlinked);
      }
    }

    // Step 5.c. Perform ! Call(state.[[PromiseCapability]].[[Resolve]],
    //                          undefined, « undefined »).
    RootedValue hostDefined(cx, state->hostDefined());
    if (!state->resolved(cx, hostDefined)) {
      return false;
    }
  }

  // Step 6. Return unused.
  return true;
}

// https://tc39.es/ecma262/#sec-ContinueModuleLoading
// ContinueModuleLoading ( state, moduleCompletion )
static bool ContinueModuleLoading(JSContext* cx,
                                  Handle<GraphLoadingStateRecordObject*> state,
                                  Handle<ModuleObject*> moduleCompletion,
                                  Handle<Value> error) {
  MOZ_ASSERT_IF(moduleCompletion, error.isUndefined());

  // Step 1. If state.[[IsLoading]] is false, return unused.
  if (!state->isLoading()) {
    return true;
  }

  // Step 2. If moduleCompletion is a normal completion, then
  if (moduleCompletion) {
    // Step 2.a. Perform InnerModuleLoading(state, moduleCompletion.[[Value]]).
    return InnerModuleLoading(cx, state, moduleCompletion);
  }

  // Step 3. Else,
  // Step 3.a. Set state.[[IsLoading]] to false.
  state->setIsLoading(false);

  // Step 3.b. Perform ! Call(state.[[PromiseCapability]].[[Reject]],
  // undefined, « moduleCompletion.[[Value]] »).
  RootedValue hostDefined(cx, state->hostDefined());
  return state->rejected(cx, hostDefined, error);
}

// https://tc39.es/ecma262/#sec-LoadRequestedModules
bool js::LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                              Handle<Value> hostDefined,
                              JS::LoadModuleResolvedCallback resolved,
                              JS::LoadModuleRejectedCallback rejected) {
  if (module->hasSyntheticModuleFields()) {
    // Step 1. Return ! PromiseResolve(%Promise%, undefined).
    return resolved(cx, hostDefined);
  }

  // Step 1. If hostDefined is not present, let hostDefined be empty.
  // Step 2. Let pc be ! NewPromiseCapability(%Promise%).
  // Note: For implementation we use callbacks to notify the results.

  // Step 3. Let state be the GraphLoadingState Record { [[IsLoading]]: true,
  //         [[PendingModulesCount]]: 1, [[Visited]]: « »,
  //         [[PromiseCapability]]: pc, [[HostDefined]]: hostDefined }.
  Rooted<GraphLoadingStateRecordObject*> state(
      cx, GraphLoadingStateRecordObject::create(cx, true, 1, resolved, rejected,
                                                hostDefined));
  if (!state) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Perform InnerModuleLoading(state, module).
  return InnerModuleLoading(cx, state, module);
}

bool js::LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                              Handle<Value> hostDefined,
                              MutableHandle<JSObject*> promiseOut) {
  // Step 1. If hostDefined is not present, let hostDefined be empty.
  // Step 2. Let pc be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> pc(cx, CreatePromiseObjectForAsync(cx));
  if (!pc) {
    ReportOutOfMemory(cx);
    return false;
  }

  if (module->hasSyntheticModuleFields()) {
    // Step 1. Return ! PromiseResolve(%Promise%, undefined).
    promiseOut.set(pc);
    return AsyncFunctionReturned(cx, pc, UndefinedHandleValue);
  }

  // Step 3. Let state be the GraphLoadingState Record { [[IsLoading]]: true,
  //         [[PendingModulesCount]]: 1, [[Visited]]: « »,
  //         [[PromiseCapability]]: pc, [[HostDefined]]: hostDefined }.
  Rooted<GraphLoadingStateRecordObject*> state(
      cx, GraphLoadingStateRecordObject::create(cx, true, 1, pc, hostDefined));
  if (!state) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Perform InnerModuleLoading(state, module).
  if (!InnerModuleLoading(cx, state, module)) {
    return false;
  }

  // Step 5. Return pc.[[Promise]].
  promiseOut.set(pc);
  return true;
}

// https://tc39.es/ecma262/#sec-moduledeclarationlinking
// ES2023 16.2.1.5.1 Link
static bool ModuleLink(JSContext* cx, Handle<ModuleObject*> module) {
  if (!module->hasCyclicModuleFields()) {
    return true;
  }

  // Step 1. Assert: module.[[Status]] is one of unlinked, linked,
  //         evaluating-async, or evaluated.
  ModuleStatus status = module->status();
  if (status == ModuleStatus::New || status == ModuleStatus::Linking ||
      status == ModuleStatus::Evaluating) {
    ThrowUnexpectedModuleStatus(cx, status);
    return false;
  }

  // Step 2. Let stack be a new empty List.
  Rooted<ModuleVector> stack(cx);

  // Step 3. Let result be Completion(InnerModuleLinking(module, stack, 0)).
  size_t ignored;
  bool ok = InnerModuleLinking(cx, module, &stack, 0, &ignored);

  // Step 4. If result is an abrupt completion, then:
  if (!ok) {
    // Step 4.a. For each Cyclic Module Record m of stack, do:
    for (ModuleObject* m : stack) {
      // Step 4.a.i. Assert: m.[[Status]] is linking.
      MOZ_ASSERT(m->status() == ModuleStatus::Linking);
      // Step 4.a.ii. Set m.[[Status]] to unlinked.
      m->setStatus(ModuleStatus::Unlinked);
      m->clearDfsIndexes();
    }

    // Step 4.b. Assert: module.[[Status]] is unlinked.
    MOZ_ASSERT(module->status() == ModuleStatus::Unlinked);

    // Step 4.c.
    return false;
  }

  // Step 5. Assert: module.[[Status]] is linked, evaluating-async, or
  //         evaluated.
  MOZ_ASSERT(module->status() == ModuleStatus::Linked ||
             module->status() == ModuleStatus::EvaluatingAsync ||
             module->status() == ModuleStatus::Evaluated);

  // Step 6. Assert: stack is empty.
  MOZ_ASSERT(stack.empty());

  // Step 7. Return unused.
  return true;
}

// https://tc39.es/ecma262/#sec-InnerModuleLinking
// ES2023 16.2.1.5.1.1 InnerModuleLinking
static bool InnerModuleLinking(JSContext* cx, Handle<ModuleObject*> module,
                               MutableHandle<ModuleVector> stack, size_t index,
                               size_t* indexOut) {
  // Step 1. If module is not a Cyclic Module Record, then
  if (!module->hasCyclicModuleFields()) {
    // Step 1.a. Perform ? module.Link(). (Skipped)
    // Step 2.b. Return index.
    *indexOut = index;
    return true;
  }

  // Step 2. If module.[[Status]] is linking, linked, evaluating-async, or
  //         evaluated, then:
  if (module->status() == ModuleStatus::Linking ||
      module->status() == ModuleStatus::Linked ||
      module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    // Step 2.a. Return index.
    *indexOut = index;
    return true;
  }

  // Step 3. Assert: module.[[Status]] is unlinked.
  if (module->status() != ModuleStatus::Unlinked) {
    ThrowUnexpectedModuleStatus(cx, module->status());
    return false;
  }

  // Step 8. Append module to stack.
  // Do this before changing the status so that we can recover on failure.
  if (!stack.append(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Set module.[[Status]] to linking.
  module->setStatus(ModuleStatus::Linking);

  // Step 5. Set module.[[DFSIndex]] to index.
  module->setDfsIndex(index);

  // Step 6. Set module.[[DFSAncestorIndex]] to index.
  module->setDfsAncestorIndex(index);

  // Step 7. Set index to index + 1.
  index++;

  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 9. For each String required that is an element of
  //         module.[[RequestedModules]], do:
  Rooted<ModuleRequestObject*> required(cx);
  Rooted<ModuleObject*> requiredModule(cx);
  for (const RequestedModule& request : module->requestedModules()) {
    // Step 9.a. Let requiredModule be ? GetImportedModule(module, required).
    required = request.moduleRequest();
    requiredModule = GetImportedModule(cx, module, required);
    if (!requiredModule) {
      return false;
    }
    MOZ_ASSERT(requiredModule->status() >= ModuleStatus::Unlinked);

    // Step 9.b. Set index to ? InnerModuleLinking(requiredModule, stack,
    //           index).
    if (!InnerModuleLinking(cx, requiredModule, stack, index, &index)) {
      return false;
    }

    // Step 9.c. If requiredModule is a Cyclic Module Record, then:
    if (requiredModule->hasCyclicModuleFields()) {
      // Step 9.c.i. Assert: requiredModule.[[Status]] is either linking,
      // linked,
      //             evaluating-async, or evaluated.
      MOZ_ASSERT(requiredModule->status() == ModuleStatus::Linking ||
                 requiredModule->status() == ModuleStatus::Linked ||
                 requiredModule->status() == ModuleStatus::EvaluatingAsync ||
                 requiredModule->status() == ModuleStatus::Evaluated);

      // Step 9.c.ii. Assert: requiredModule.[[Status]] is linking if and only
      // if
      //              requiredModule is in stack.
      MOZ_ASSERT((requiredModule->status() == ModuleStatus::Linking) ==
                 ContainsElement(stack, requiredModule));

      // Step 9.c.iii. If requiredModule.[[Status]] is linking, then:
      if (requiredModule->status() == ModuleStatus::Linking) {
        // Step 9.c.iii.1. Set module.[[DFSAncestorIndex]] to
        //                 min(module.[[DFSAncestorIndex]],
        //                 requiredModule.[[DFSAncestorIndex]]).
        module->setDfsAncestorIndex(std::min(
            module->dfsAncestorIndex(), requiredModule->dfsAncestorIndex()));
      }
    }
  }

  // Step 10. Perform ? module.InitializeEnvironment().
  if (!ModuleInitializeEnvironment(cx, module)) {
    return false;
  }

  // Step 11. Assert: module occurs exactly once in stack.
  MOZ_ASSERT(CountElements(stack, module) == 1);

  // Step 12. Assert: module.[[DFSAncestorIndex]] <= module.[[DFSIndex]].
  MOZ_ASSERT(module->dfsAncestorIndex() <= module->dfsIndex());

  // Step 13. If module.[[DFSAncestorIndex]] = module.[[DFSIndex]], then
  if (module->dfsAncestorIndex() == module->dfsIndex()) {
    // Step 13.a.
    bool done = false;

    // Step 13.b. Repeat, while done is false:
    while (!done) {
      // Step 13.b.i. Let requiredModule be the last element in stack.
      // Step 13.b.ii. Remove the last element of stack.
      requiredModule = stack.popCopy();

      // Step 13.b.iv. Set requiredModule.[[Status]] to linked.
      requiredModule->setStatus(ModuleStatus::Linked);

      // Step 13.b.v. If requiredModule and module are the same Module Record,
      //              set done to true.
      done = requiredModule == module;
    }
  }

  // Step 14. Return index.
  *indexOut = index;
  return true;
}

static bool SyntheticModuleEvaluate(JSContext* cx,
                                    Handle<ModuleObject*> moduleArg,
                                    MutableHandle<Value> rval) {
  // Steps 1-12 happen elsewhere in the engine.

  // Step 13. Let pc be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> resultPromise(cx, CreatePromiseObjectForAsync(cx));
  if (!resultPromise) {
    return false;
  }

  // Since the only synthetic modules we support are JSON modules, result is
  // always |undefined|.

  // Step 14. IfAbruptRejectPromise(result, pc) (Skipped)

  // 15. Perform ! pc.[[Resolve]](result).
  if (!AsyncFunctionReturned(cx, resultPromise, JS::UndefinedHandleValue)) {
    return false;
  }

  // 16. Return pc.[[Promise]].
  rval.set(ObjectValue(*resultPromise));
  return true;
}

// https://tc39.es/ecma262/#sec-moduleevaluation
// ES2023 16.2.1.5.2 Evaluate
static bool ModuleEvaluate(JSContext* cx, Handle<ModuleObject*> moduleArg,
                           MutableHandle<Value> result) {
  Rooted<ModuleObject*> module(cx, moduleArg);

  // Step 2. Assert: module.[[Status]] is linked, evaluating-async, or
  //         evaluated.
  ModuleStatus status = module->status();
  if (status != ModuleStatus::Linked &&
      status != ModuleStatus::EvaluatingAsync &&
      status != ModuleStatus::Evaluated) {
    ThrowUnexpectedModuleStatus(cx, status);
    return false;
  }

  // Note: we return early in the error case, as the spec assumes we can get the
  // cycle root of |module| which may not be available.
  if (module->hadEvaluationError()) {
    Rooted<PromiseObject*> capability(cx);
    if (!module->hasTopLevelCapability()) {
      capability = ModuleObject::createTopLevelCapability(cx, module);
      if (!capability) {
        return false;
      }

      Rooted<Value> error(cx, module->evaluationError());
      if (!ModuleObject::topLevelCapabilityReject(cx, module, error)) {
        return false;
      }
    }

    capability = module->topLevelCapability();
    MOZ_ASSERT(JS::GetPromiseState(capability) == JS::PromiseState::Rejected);
    MOZ_ASSERT(JS::GetPromiseResult(capability) == module->evaluationError());
    result.set(ObjectValue(*capability));
    return true;
  }

  // Step 3. If module.[[Status]] is evaluating-async or evaluated, set module
  //         to module.[[CycleRoot]].
  if (module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    module = module->getCycleRoot();
  }

  // Step 4. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 4.a. Return module.[[TopLevelCapability]].[[Promise]].
    result.set(ObjectValue(*module->topLevelCapability()));
    return true;
  }

  // Step 5. Let stack be a new empty List.
  Rooted<ModuleVector> stack(cx);

  // Step 6. Let capability be ! NewPromiseCapability(%Promise%).
  // Step 7. Set module.[[TopLevelCapability]] to capability.
  Rooted<PromiseObject*> capability(
      cx, ModuleObject::createTopLevelCapability(cx, module));
  if (!capability) {
    return false;
  }

  // Step 8. Let result be Completion(InnerModuleEvaluation(module, stack, 0)).
  size_t ignored;
  bool ok = InnerModuleEvaluation(cx, module, &stack, 0, &ignored);

  // Step 9. f result is an abrupt completion, then:
  if (!ok) {
    // Attempt to take any pending exception, but make sure we still handle
    // uncatchable exceptions.
    Rooted<Value> error(cx);
    if (cx->isExceptionPending()) {
      (void)cx->getPendingException(&error);
      cx->clearPendingException();
    }

    // Step 9.a. For each Cyclic Module Record m of stack, do
    for (ModuleObject* m : stack) {
      // Step 9.a.i. Assert: m.[[Status]] is evaluating.
      MOZ_ASSERT(m->status() == ModuleStatus::Evaluating);

      // Step 9.a.ii. Set m.[[Status]] to evaluated.
      // Step 9.a.iii. Set m.[[EvaluationError]] to result.
      m->setEvaluationError(error);
    }

    // Handle OOM when appending to the stack or over-recursion errors.
    if (stack.empty() && !module->hadEvaluationError()) {
      module->setEvaluationError(error);
    }

    // Step 9.b. Assert: module.[[Status]] is evaluated.
    MOZ_ASSERT(module->status() == ModuleStatus::Evaluated);

    // Step 9.c. Assert: module.[[EvaluationError]] is result.
    MOZ_ASSERT(module->evaluationError() == error);

    // Step 9.d. Perform ! Call(capability.[[Reject]], undefined,
    //           result.[[Value]]).
    if (!ModuleObject::topLevelCapabilityReject(cx, module, error)) {
      return false;
    }
  } else {
    // Step 10. Else:
    // Step 10.a. Assert: module.[[Status]] is evaluating-async or evaluated.
    MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync ||
               module->status() == ModuleStatus::Evaluated);

    // Step 10.b. Assert: module.[[EvaluationError]] is empty.
    MOZ_ASSERT(!module->hadEvaluationError());

    // Step 10.c. If module.[[AsyncEvaluation]] is false, then:
    if (module->status() == ModuleStatus::Evaluated) {
      // Step 10.c.ii. Perform ! Call(capability.[[Resolve]], undefined,
      //               undefined).
      if (!ModuleObject::topLevelCapabilityResolve(cx, module)) {
        return false;
      }
    }

    // Step 10.d. Assert: stack is empty.
    MOZ_ASSERT(stack.empty());
  }

  // Step 11. Return capability.[[Promise]].
  result.set(ObjectValue(*capability));
  return true;
}

// https://tc39.es/ecma262/#sec-innermoduleevaluation
// 16.2.1.5.2.1 InnerModuleEvaluation
static bool InnerModuleEvaluation(JSContext* cx, Handle<ModuleObject*> module,
                                  MutableHandle<ModuleVector> stack,
                                  size_t index, size_t* indexOut) {
  // Step 1: If module is not a Cyclic Module Record, then
  if (!module->hasCyclicModuleFields()) {
    // Step 1.a. Let promise be ! module.Evaluate(). (Skipped)
    // Step 1.b. Assert: promise.[[PromiseState]] is not pending. (Skipped)
    // Step 1.c. If promise.[[PromiseState]] is rejected, then (Skipped)
    //   Step 1.c.i Return ThrowCompletion(promise.[[PromiseResult]]). (Skipped)
    // Step 1.d. Return index.
    *indexOut = index;
    return true;
  }

  // Step 2. If module.[[Status]] is evaluating-async or evaluated, then:
  if (module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    // Step 2.a. If module.[[EvaluationError]] is empty, return index.
    if (!module->hadEvaluationError()) {
      *indexOut = index;
      return true;
    }

    // Step 2.b. Otherwise, return ? module.[[EvaluationError]].
    Rooted<Value> error(cx, module->evaluationError());
    cx->setPendingException(error, ShouldCaptureStack::Maybe);
    return false;
  }

  // Step 3. If module.[[Status]] is evaluating, return index.
  if (module->status() == ModuleStatus::Evaluating) {
    *indexOut = index;
    return true;
  }

  // Step 4. Assert: module.[[Status]] is linked.
  MOZ_ASSERT(module->status() == ModuleStatus::Linked);

  // Step 10. Append module to stack.
  // Do this before changing the status so that we can recover on failure.
  if (!stack.append(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. Set module.[[Status]] to evaluating.
  module->setStatus(ModuleStatus::Evaluating);

  // Step 6. Set module.[[DFSIndex]] to index.
  module->setDfsIndex(index);

  // Step 7. Set module.[[DFSAncestorIndex]] to index.
  module->setDfsAncestorIndex(index);

  // Step 8. Set module.[[PendingAsyncDependencies]] to 0.
  module->setPendingAsyncDependencies(0);

  // Step 9. Set index to index + 1.
  index++;

  // Step 11. For each String required of module.[[RequestedModules]], do:
  Rooted<ModuleRequestObject*> required(cx);
  Rooted<ModuleObject*> requiredModule(cx);
  for (const RequestedModule& request : module->requestedModules()) {
    // Step 11.a. Let requiredModule be ! GetImportedModule(module,
    //            required).
    // Step 11.b. NOTE: Link must be completed successfully prior to invoking
    //            this method, so every requested module is guaranteed to
    //            resolve successfully.
    required = request.moduleRequest();
    requiredModule = GetImportedModule(cx, module, required);
    if (!requiredModule) {
      return false;
    }
    MOZ_ASSERT(requiredModule->status() >= ModuleStatus::Linked);

    // Step 11.c. Set index to ? InnerModuleEvaluation(requiredModule, stack,
    //            index).
    if (!InnerModuleEvaluation(cx, requiredModule, stack, index, &index)) {
      return false;
    }

    // Step 11.d. If requiredModule is a Cyclic Module Record, then:
    if (requiredModule->hasCyclicModuleFields()) {
      // Step 11.d.i. Assert: requiredModule.[[Status]] is either evaluating,
      //              evaluating-async, or evaluated.
      MOZ_ASSERT(requiredModule->status() == ModuleStatus::Evaluating ||
                 requiredModule->status() == ModuleStatus::EvaluatingAsync ||
                 requiredModule->status() == ModuleStatus::Evaluated);

      // Step 11.d.ii. Assert: requiredModule.[[Status]] is evaluating if and
      //               only if requiredModule is in stack.
      MOZ_ASSERT((requiredModule->status() == ModuleStatus::Evaluating) ==
                 ContainsElement(stack, requiredModule));

      // Step 11.d.iii. If requiredModule.[[Status]] is evaluating, then:
      if (requiredModule->status() == ModuleStatus::Evaluating) {
        // Step 11.d.iii.1. Set module.[[DFSAncestorIndex]] to
        //                  min(module.[[DFSAncestorIndex]],
        //                  requiredModule.[[DFSAncestorIndex]]).
        module->setDfsAncestorIndex(std::min(
            module->dfsAncestorIndex(), requiredModule->dfsAncestorIndex()));
      } else {
        // Step 11.d.iv. Else:
        // Step 11.d.iv.1. Set requiredModule to requiredModule.[[CycleRoot]].
        requiredModule = requiredModule->getCycleRoot();

        // Step 11.d.iv.2. Assert: requiredModule.[[Status]] is evaluating-async
        //                 or evaluated.
        MOZ_ASSERT(requiredModule->status() >= ModuleStatus::EvaluatingAsync ||
                   requiredModule->status() == ModuleStatus::Evaluated);

        // Step 11.d.iv.3. If requiredModule.[[EvaluationError]] is not empty,
        //                 return ? requiredModule.[[EvaluationError]].
        if (requiredModule->hadEvaluationError()) {
          Rooted<Value> error(cx, requiredModule->evaluationError());
          cx->setPendingException(error, ShouldCaptureStack::Maybe);
          return false;
        }
      }

      // Step 11.d.v. If requiredModule.[[AsyncEvaluation]] is true, then:
      if (requiredModule->isAsyncEvaluating() &&
          requiredModule->status() != ModuleStatus::Evaluated) {
        // Step 11.d.v.2. Append module to
        // requiredModule.[[AsyncParentModules]].
        if (!ModuleObject::appendAsyncParentModule(cx, requiredModule,
                                                   module)) {
          return false;
        }

        // Step 11.d.v.1. Set module.[[PendingAsyncDependencies]] to
        //                module.[[PendingAsyncDependencies]] + 1.
        module->setPendingAsyncDependencies(module->pendingAsyncDependencies() +
                                            1);
      }
    }
  }

  // Step 12. If module.[[PendingAsyncDependencies]] > 0 or module.[[HasTLA]] is
  //          true, then:
  if (module->pendingAsyncDependencies() > 0 || module->hasTopLevelAwait()) {
    // Step 12.a. Assert: module.[[AsyncEvaluation]] is false and was never
    //            previously set to true.
    MOZ_ASSERT(!module->isAsyncEvaluating());

    // Step 12.b. Set module.[[AsyncEvaluation]] to true.
    // Step 12.c. NOTE: The order in which module records have their
    //            [[AsyncEvaluation]] fields transition to true is
    //            significant. (See 16.2.1.5.2.4.)
    module->setAsyncEvaluating();

    // Step 12.d. If module.[[PendingAsyncDependencies]] is 0, perform
    //            ExecuteAsyncModule(module).
    if (module->pendingAsyncDependencies() == 0) {
      if (!ExecuteAsyncModule(cx, module)) {
        return false;
      }
    }
  } else {
    // Step 13. Otherwise, perform ? module.ExecuteModule().
    if (!ModuleObject::execute(cx, module)) {
      return false;
    }
  }

  // Step 14. Assert: module occurs exactly once in stack.
  MOZ_ASSERT(CountElements(stack, module) == 1);

  // Step 15. Assert: module.[[DFSAncestorIndex]] <= module.[[DFSIndex]].
  MOZ_ASSERT(module->dfsAncestorIndex() <= module->dfsIndex());

  // Step 16. If module.[[DFSAncestorIndex]] = module.[[DFSIndex]], then:
  if (module->dfsAncestorIndex() == module->dfsIndex()) {
    // Step 16.a. Let done be false.
    bool done = false;

    // Step 16.b. Repeat, while done is false:
    while (!done) {
      // Step 16.b.i. Let requiredModule be the last element in stack.
      // Step 16.b.ii. Remove the last element of stack.
      requiredModule = stack.popCopy();

      // Step 16.b.iv. If requiredModule.[[AsyncEvaluation]] is false, set
      //               requiredModule.[[Status]] to evaluated.
      if (!requiredModule->isAsyncEvaluating()) {
        requiredModule->setStatus(ModuleStatus::Evaluated);
      } else {
        // Step 16.b.v. Otherwise, set requiredModule.[[Status]] to
        //              evaluating-async.
        requiredModule->setStatus(ModuleStatus::EvaluatingAsync);
      }

      // Step 16.b.vi. If requiredModule and module are the same Module Record,
      //               set done to true.
      done = requiredModule == module;

      // Step 16.b.vii. Set requiredModule.[[CycleRoot]] to module.
      requiredModule->setCycleRoot(module);
    }
  }

  // Step 17. Return index.
  *indexOut = index;
  return true;
}

// https://tc39.es/ecma262/#sec-execute-async-module
// ES2023 16.2.1.5.2.2 ExecuteAsyncModule
static bool ExecuteAsyncModule(JSContext* cx, Handle<ModuleObject*> module) {
  // Step 1. Assert: module.[[Status]] is evaluating or evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::Evaluating ||
             module->status() == ModuleStatus::EvaluatingAsync);

  // Step 2. Assert: module.[[HasTLA]] is true.
  MOZ_ASSERT(module->hasTopLevelAwait());

  // Steps 3 - 8 are performed by the AsyncAwait opcode.

  // Step 9. Perform ! module.ExecuteModule(capability).
  // Step 10. Return unused.
  return ModuleObject::execute(cx, module);
}

// https://tc39.es/ecma262/#sec-gather-available-ancestors
// ES2023 16.2.1.5.2.3 GatherAvailableAncestors
static bool GatherAvailableModuleAncestors(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleVector> execList) {
  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 1. For each Cyclic Module Record m of module.[[AsyncParentModules]],
  //         do:
  Rooted<ListObject*> asyncParentModules(cx, module->asyncParentModules());
  Rooted<ModuleObject*> m(cx);
  for (uint32_t i = 0; i != asyncParentModules->length(); i++) {
    m = &asyncParentModules->getDenseElement(i).toObject().as<ModuleObject>();

    // Step 1.a. If execList does not contain m and
    //           m.[[CycleRoot]].[[EvaluationError]] is empty, then:
    //
    // Note: we also check whether m.[[EvaluationError]] is empty since an error
    // in synchronous execution can prevent the CycleRoot field from being set.
    if (!m->hadEvaluationError() && !m->getCycleRoot()->hadEvaluationError() &&
        !ContainsElement(execList, m)) {
      // Step 1.a.i. Assert: m.[[Status]] is evaluating-async.
      MOZ_ASSERT(m->status() == ModuleStatus::EvaluatingAsync);

      // Step 1.a.ii. Assert: m.[[EvaluationError]] is empty.
      MOZ_ASSERT(!m->hadEvaluationError());

      // Step 1.a.iii. Assert: m.[[AsyncEvaluation]] is true.
      MOZ_ASSERT(m->isAsyncEvaluating());

      // Step 1.a.iv. Assert: m.[[PendingAsyncDependencies]] > 0.
      MOZ_ASSERT(m->pendingAsyncDependencies() > 0);

      // Step 1.a.v. Set m.[[PendingAsyncDependencies]] to
      // m.[[PendingAsyncDependencies]] - 1.
      m->setPendingAsyncDependencies(m->pendingAsyncDependencies() - 1);

      // Step 1.a.vi. If m.[[PendingAsyncDependencies]] = 0, then:
      if (m->pendingAsyncDependencies() == 0) {
        // Step 1.a.vi.1. Append m to execList.
        if (!execList.append(m)) {
          return false;
        }

        // Step 1.a.vi.2. If m.[[HasTLA]] is false, perform
        //                GatherAvailableAncestors(m, execList).
        if (!m->hasTopLevelAwait() &&
            !GatherAvailableModuleAncestors(cx, m, execList)) {
          return false;
        }
      }
    }
  }

  // Step 2. Return unused.
  return true;
}

struct EvalOrderComparator {
  bool operator()(ModuleObject* a, ModuleObject* b, bool* lessOrEqualp) {
    int32_t result = int32_t(a->getAsyncEvaluatingPostOrder()) -
                     int32_t(b->getAsyncEvaluatingPostOrder());
    *lessOrEqualp = (result <= 0);
    return true;
  }
};

static void RejectExecutionWithPendingException(JSContext* cx,
                                                Handle<ModuleObject*> module) {
  // If there is no exception pending then we have been interrupted or have
  // OOM'd and all bets are off. We reject the execution by throwing
  // undefined. Not much more we can do.
  RootedValue exception(cx);
  if (cx->isExceptionPending()) {
    (void)cx->getPendingException(&exception);
  }
  cx->clearPendingException();
  AsyncModuleExecutionRejected(cx, module, exception);
}

// https://tc39.es/ecma262/#sec-async-module-execution-fulfilled
// ES2023 16.2.1.5.2.4 AsyncModuleExecutionFulfilled
void js::AsyncModuleExecutionFulfilled(JSContext* cx,
                                       Handle<ModuleObject*> module) {
  // Step 1. If module.[[Status]] is evaluated, then:
  if (module->status() == ModuleStatus::Evaluated) {
    // Step 1.a. Assert: module.[[EvaluationError]] is not empty.
    MOZ_ASSERT(module->hadEvaluationError());

    // Step 1.b. Return unused.
    return;
  }

  // Step 2. Assert: module.[[Status]] is evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 3. Assert: module.[[AsyncEvaluation]] is true.
  MOZ_ASSERT(module->isAsyncEvaluating());

  // Step 4. Assert: module.[[EvaluationError]] is empty.
  MOZ_ASSERT(!module->hadEvaluationError());

  // The following steps are performed in a different order from the
  // spec. Gather available module ancestors before mutating the module object
  // as this can fail in our implementation.

  // Step 8. Let execList be a new empty List.
  Rooted<ModuleVector> execList(cx);

  // Step 9. Perform GatherAvailableAncestors(module, execList).
  if (!GatherAvailableModuleAncestors(cx, module, &execList)) {
    RejectExecutionWithPendingException(cx, module);
    return;
  }

  // Step 10. Let sortedExecList be a List whose elements are the elements of
  //          execList, in the order in which they had their [[AsyncEvaluation]]
  //          fields set to true in InnerModuleEvaluation.

  Rooted<ModuleVector> scratch(cx);
  if (!scratch.resize(execList.length())) {
    ReportOutOfMemory(cx);
    RejectExecutionWithPendingException(cx, module);
    return;
  }

  MOZ_ALWAYS_TRUE(MergeSort(execList.begin(), execList.length(),
                            scratch.begin(), EvalOrderComparator()));

  // Step 11. Assert: All elements of sortedExecList have their
  //          [[AsyncEvaluation]] field set to true,
  //          [[PendingAsyncDependencies]] field set to 0, and
  //          [[EvaluationError]] field set to empty.
#ifdef DEBUG
  for (ModuleObject* m : execList) {
    MOZ_ASSERT(m->isAsyncEvaluating());
    MOZ_ASSERT(m->pendingAsyncDependencies() == 0);
    MOZ_ASSERT(!m->hadEvaluationError());
  }
#endif

  // Return to original order of steps.

  ModuleObject::onTopLevelEvaluationFinished(module);

  // Step 6. Set module.[[Status]] to evaluated.
  module->setStatus(ModuleStatus::Evaluated);
  module->clearAsyncEvaluatingPostOrder();

  // Step 7. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 7.a. Assert: module.[[CycleRoot]] is module.
    MOZ_ASSERT(module->getCycleRoot() == module);

    // Step 7.b. Perform ! Call(module.[[TopLevelCapability]].[[Resolve]],
    //           undefined, undefined).
    if (!ModuleObject::topLevelCapabilityResolve(cx, module)) {
      // If Resolve fails, there's nothing more we can do here.
      cx->clearPendingException();
    }
  }

  // Step 12. For each Cyclic Module Record m of sortedExecList, do:
  Rooted<ModuleObject*> m(cx);
  for (ModuleObject* obj : execList) {
    m = obj;

    // Step 12.a. If m.[[Status]] is evaluated, then:
    if (m->status() == ModuleStatus::Evaluated) {
      // Step 12.a.i. Assert: m.[[EvaluationError]] is not empty.
      MOZ_ASSERT(m->hadEvaluationError());
    } else if (m->hasTopLevelAwait()) {
      // Step 12.b. Else if m.[[HasTLA]] is true, then:
      // Step 12.b.i. Perform ExecuteAsyncModule(m).
      if (!ExecuteAsyncModule(cx, m)) {
        MOZ_ASSERT(cx->isThrowingOutOfMemory() || cx->isThrowingOverRecursed());
        cx->clearPendingException();
      }
    } else {
      // Step 12.c. Else:
      // Step 12.c.i. Let result be m.ExecuteModule().
      bool ok = ModuleObject::execute(cx, m);

      // Step 12.c.ii. If result is an abrupt completion, then:
      if (!ok) {
        // Step 12.c.ii.1. Perform AsyncModuleExecutionRejected(m,
        //                 result.[[Value]]).
        RejectExecutionWithPendingException(cx, m);
      } else {
        // Step 12.c.iii. Else:
        // Step 12.c.iii.1. Set m.[[Status]] to evaluated.
        m->setStatus(ModuleStatus::Evaluated);
        m->clearAsyncEvaluatingPostOrder();

        // Step 12.c.iii.2. If m.[[TopLevelCapability]] is not empty, then:
        if (m->hasTopLevelCapability()) {
          // Step 12.c.iii.2.a. Assert: m.[[CycleRoot]] is m.
          MOZ_ASSERT(m->getCycleRoot() == m);

          // Step 12.c.iii.2.b. Perform !
          //                    Call(m.[[TopLevelCapability]].[[Resolve]],
          //                    undefined, undefined).
          if (!ModuleObject::topLevelCapabilityResolve(cx, m)) {
            // If Resolve fails, there's nothing more we can do here.
            cx->clearPendingException();
          }
        }
      }
    }
  }

  // Step 13. Return unused.
}

// https://tc39.es/ecma262/#sec-async-module-execution-rejected
// ES2023 16.2.1.5.2.5 AsyncModuleExecutionRejected
void js::AsyncModuleExecutionRejected(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      HandleValue error) {
  // Step 1. If module.[[Status]] is evaluated, then:
  if (module->status() == ModuleStatus::Evaluated) {
    // Step 1.a. Assert: module.[[EvaluationError]] is not empty
    MOZ_ASSERT(module->hadEvaluationError());

    // Step 1.b. Return unused.
    return;
  }

  // Step 2. Assert: module.[[Status]] is evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 3. Assert: module.[[AsyncEvaluation]] is true.
  MOZ_ASSERT(module->isAsyncEvaluating());

  // Step 4. 4. Assert: module.[[EvaluationError]] is empty.
  MOZ_ASSERT(!module->hadEvaluationError());

  ModuleObject::onTopLevelEvaluationFinished(module);

  // Step 5. Set module.[[EvaluationError]] to ThrowCompletion(error).
  module->setEvaluationError(error);

  // Step 6. Set module.[[Status]] to evaluated.
  MOZ_ASSERT(module->status() == ModuleStatus::Evaluated);

  module->clearAsyncEvaluatingPostOrder();

  // Step 7. For each Cyclic Module Record m of module.[[AsyncParentModules]],
  //         do:
  Rooted<ListObject*> parents(cx, module->asyncParentModules());
  Rooted<ModuleObject*> parent(cx);
  for (uint32_t i = 0; i < parents->length(); i++) {
    parent = &parents->get(i).toObject().as<ModuleObject>();

    // Step 7.a. Perform AsyncModuleExecutionRejected(m, error).
    AsyncModuleExecutionRejected(cx, parent, error);
  }

  // Step 8. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 8.a. Assert: module.[[CycleRoot]] is module.
    MOZ_ASSERT(module->getCycleRoot() == module);

    // Step 8.b. Perform ! Call(module.[[TopLevelCapability]].[[Reject]],
    //           undefined, error).
    if (!ModuleObject::topLevelCapabilityReject(cx, module, error)) {
      // If Reject fails, there's nothing more we can do here.
      cx->clearPendingException();
    }
  }

  // Step 9. Return unused.
}

// https://tc39.es/proposal-import-attributes/#sec-evaluate-import-call
// NOTE: The caller needs to handle the promise.
static bool EvaluateDynamicImportOptions(
    JSContext* cx, HandleValue optionsArg,
    MutableHandle<ImportAttributeVector> attributesArrayArg) {
  // Step 11. If options is not undefined, then
  if (optionsArg.isUndefined()) {
    return true;
  }

  // Step 11.a. If options is not an Object, then
  if (!optionsArg.isObject()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_EXPECTED_TYPE, "import",
        "object or undefined", InformalValueTypeName(optionsArg));
    return false;
  }

  RootedObject attributesWrapperObject(cx, &optionsArg.toObject());
  RootedValue attributesValue(cx);

  // Step 11.b. Let attributesObj be Completion(Get(options, "with")).
  RootedId withId(cx, NameToId(cx->names().with));
  if (!GetProperty(cx, attributesWrapperObject, attributesWrapperObject, withId,
                   &attributesValue)) {
    return false;
  }

  // Step 11.e. If attributesObj is not undefined, then
  if (attributesValue.isUndefined()) {
    return true;
  }

  // Step 11.e.i. If attributesObj is not an Object, then
  if (!attributesValue.isObject()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_EXPECTED_TYPE, "import",
        "object or undefined", InformalValueTypeName(attributesValue));
    return false;
  }

  // Step 11.e.ii. Let entries be
  // Completion(EnumerableOwnProperties(attributesObj, key+value)).
  RootedObject attributesObject(cx, &attributesValue.toObject());
  RootedIdVector attributes(cx);
  if (!GetPropertyKeys(cx, attributesObject, JSITER_OWNONLY, &attributes)) {
    return false;
  }

  uint32_t numberOfAttributes = attributes.length();
  if (numberOfAttributes == 0) {
    return true;
  }

  // Step 10 (reordered). Let attributes be a new empty List.
  if (!attributesArrayArg.reserve(numberOfAttributes)) {
    ReportOutOfMemory(cx);
    return false;
  }

  size_t numberOfValidAttributes = 0;

  // Step 11.e.iv. For each element entry of entries, do
  RootedId key(cx);
  RootedValue value(cx);
  Rooted<JSAtom*> keyAtom(cx);
  Rooted<JSString*> valueString(cx);
  for (size_t i = 0; i < numberOfAttributes; i++) {
    // Step 11.e.ii.iv.1. Let key be ! Get(entry, "0").
    key = attributes[i];

    // Step 11.e.ii.iv.2. Let value be ! Get(entry, "1").
    if (!GetProperty(cx, attributesObject, attributesObject, key, &value)) {
      return false;
    }

    // Step 11.e.ii.iv.3. If key is a String, then
    if (key.isString()) {
      // Step 11.f (reordered). If AllImportAttributesSupported(attributes) is
      // false, then
      //
      // Note: This should be driven by a host hook
      // (HostGetSupportedImportAttributes), however the infrastructure of said
      // host hook is deeply unclear, and so right now embedders will not have
      // the ability to alter or extend the set of supported attributes.
      // See https://bugzilla.mozilla.org/show_bug.cgi?id=1840723.
      bool supported = key.isAtom(cx->names().type);
      if (!supported) {
        UniqueChars printableKey = AtomToPrintableString(cx, key.toAtom());
        if (!printableKey) {
          return false;
        }
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr,
            JSMSG_IMPORT_ATTRIBUTES_DYNAMIC_IMPORT_UNSUPPORTED_ATTRIBUTE,
            printableKey.get());
        return false;
      }

      // Step 10.d.v.3.a. If value is not a String, then
      if (!value.isString()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_NOT_EXPECTED_TYPE, "import", "string",
                                  InformalValueTypeName(value));
        return false;
      }

      // Step 10.d.v.3.b. Append the ImportAttribute Record { [[Key]]: key,
      // [[Value]]: value } to attributes.
      keyAtom = key.toAtom();
      valueString = value.toString();
      attributesArrayArg.infallibleEmplaceBack(keyAtom, valueString);
      ++numberOfValidAttributes;
    }
  }

  if (numberOfValidAttributes == 0) {
    return true;
  }

  // Step 10.g (skipped). Sort attributes according to the lexicographic order
  // of their [[Key]] fields, treating the value of each such field as a
  // sequence of UTF-16 code unit values.
  //
  // We only support "type", so we can ignore this.

  return true;
}

// https://tc39.es/ecma262/#sec-evaluate-import-call
//
// ShadowRealmImportValue duplicates some of this, so be sure to keep these in
// sync.
JSObject* js::StartDynamicModuleImport(JSContext* cx, HandleScript script,
                                       HandleValue specifierArg,
                                       HandleValue optionsArg) {
  // Step 7. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  if (!promise) {
    return nullptr;
  }

  if (!TryStartDynamicModuleImport(cx, script, specifierArg, optionsArg,
                                   promise)) {
    if (!RejectPromiseWithPendingError(cx, promise.as<PromiseObject>())) {
      return nullptr;
    }
  }

  return promise;
}

// https://tc39.es/ecma262/#sec-evaluate-import-call continued.
static bool TryStartDynamicModuleImport(JSContext* cx, HandleScript script,
                                        HandleValue specifierArg,
                                        HandleValue optionsArg,
                                        HandleObject promise) {
  RootedString specifier(cx, ToString(cx, specifierArg));
  if (!specifier) {
    return false;
  }

  Rooted<JSAtom*> specifierAtom(cx, AtomizeString(cx, specifier));
  if (!specifierAtom) {
    return false;
  }

  Rooted<ImportAttributeVector> attributes(cx);
  if (!EvaluateDynamicImportOptions(cx, optionsArg, &attributes)) {
    return false;
  }

  // Step 12. Let moduleRequest be a new ModuleRequest Record { [[Specifier]]:
  //          specifierString, [[Attributes]]: attributes }.
  RootedObject moduleRequest(
      cx, ModuleRequestObject::create(cx, specifierAtom, attributes));
  if (!moduleRequest) {
    return false;
  }

  // Step 13. Perform HostLoadImportedModule(referrer, moduleRequest, empty,
  //          promiseCapability).
  RootedValue payload(cx, ObjectValue(*promise));
  (void)HostLoadImportedModule(cx, script, moduleRequest,
                               JS::UndefinedHandleValue, payload);

  return true;
}

static bool OnRootModuleRejected(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue error = args.get(0);

  ReportExceptionClosure reportExn(error);
  PrepareScriptEnvironmentAndInvoke(cx, cx->global(), reportExn);

  args.rval().setUndefined();
  return true;
};

bool js::OnModuleEvaluationFailure(JSContext* cx,
                                   HandleObject evaluationPromise,
                                   JS::ModuleErrorBehaviour errorBehaviour) {
  if (evaluationPromise == nullptr) {
    return false;
  }

  // To allow module evaluation to happen synchronously throw the error
  // immediately. This assumes that any error will already have caused the
  // promise to be rejected, and doesn't support top-level await.
  if (errorBehaviour == JS::ThrowModuleErrorsSync) {
    JS::PromiseState state = JS::GetPromiseState(evaluationPromise);
    MOZ_DIAGNOSTIC_ASSERT(state == JS::PromiseState::Rejected ||
                          state == JS::PromiseState::Fulfilled);

    JS::SetSettledPromiseIsHandled(cx, evaluationPromise);
    if (state == JS::PromiseState::Fulfilled) {
      return true;
    }

    RootedValue error(cx, JS::GetPromiseResult(evaluationPromise));
    JS_SetPendingException(cx, error);
    return false;
  }

  RootedFunction onRejected(
      cx, NewHandler(cx, OnRootModuleRejected, evaluationPromise));
  if (!onRejected) {
    return false;
  }

  return JS::AddPromiseReactions(cx, evaluationPromise, nullptr, onRejected);
}

// This is used for |fulfilledClosure| and |rejectedClosure| in
// https://tc39.es/ecma262/#sec-ContinueDynamicImport
//
// It is used to marshal some arguments and pass them through to the promise
// resolve and reject callbacks. It holds a reference to the referencing private
// to keep it alive until it is needed.
//
// TODO: The |referrer| field is used to keep the importing script alive while
// the import operation is happening. It is possible that this is no longer
// required.
class DynamicImportContextObject : public NativeObject {
 public:
  enum { ReferrerSlot = 0, PromiseSlot, ModuleSlot, SlotCount };

  static const JSClass class_;

  [[nodiscard]] static DynamicImportContextObject* create(
      JSContext* cx, Handle<JSScript*> referrer, Handle<PromiseObject*> promise,
      Handle<ModuleObject*> module);

  JSScript* referrer() const;
  PromiseObject* promise() const;
  ModuleObject* module() const;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

/* static */
const JSClass DynamicImportContextObject::class_ = {
    "DynamicImportContextObject",
    JSCLASS_HAS_RESERVED_SLOTS(DynamicImportContextObject::SlotCount)};

/* static */
DynamicImportContextObject* DynamicImportContextObject::create(
    JSContext* cx, Handle<JSScript*> referrer, Handle<PromiseObject*> promise,
    Handle<ModuleObject*> module) {
  Rooted<DynamicImportContextObject*> self(
      cx, NewObjectWithGivenProto<DynamicImportContextObject>(cx, nullptr));
  if (!self) {
    return nullptr;
  }

  if (referrer) {
    self->initReservedSlot(ReferrerSlot, PrivateGCThingValue(referrer));
  }
  self->initReservedSlot(PromiseSlot, ObjectValue(*promise));
  self->initReservedSlot(ModuleSlot, ObjectValue(*module));
  return self;
}

JSScript* DynamicImportContextObject::referrer() const {
  Value value = getReservedSlot(ReferrerSlot);
  if (value.isUndefined()) {
    return nullptr;
  }

  return static_cast<JSScript*>(value.toGCThing());
}

PromiseObject* DynamicImportContextObject::promise() const {
  Value value = getReservedSlot(PromiseSlot);
  if (value.isUndefined()) {
    return nullptr;
  }

  return &value.toObject().as<PromiseObject>();
}

ModuleObject* DynamicImportContextObject::module() const {
  Value value = getReservedSlot(ModuleSlot);
  if (value.isUndefined()) {
    return nullptr;
  }

  return &value.toObject().as<ModuleObject>();
}

// https://tc39.es/ecma262/#sec-ContinueDynamicImport
/* static */
bool ContinueDynamicImport(JSContext* cx, Handle<JSScript*> referrer,
                           Handle<JSObject*> moduleRequest,
                           Handle<PromiseObject*> promiseCapability,
                           Handle<ModuleObject*> module, bool usePromise) {
  MOZ_ASSERT(module);

  // Step 1, 2: Already handled in FinishLoadingImportedModuleFailed functions.

  // Step 6. Let linkAndEvaluateClosure be a new Abstract Closure with no
  // parameters that captures module, promiseCapability, and onRejected...
  Rooted<DynamicImportContextObject*> context(
      cx, DynamicImportContextObject::create(cx, referrer, promiseCapability,
                                             module));
  if (!context) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Our implementation provides an option for synchronous completion for
  // environments where we can't use promises.
  if (!usePromise) {
    return LinkAndEvaluateDynamicImport(cx, context);
  }

  // Step 3: The module dependencies has been loaded in the host layer, so we
  // only need to do _linkAndEvaluate_ part defined in the spec. Create a
  // promise that we'll resolve immediately.
  JS::Rooted<PromiseObject*> loadPromise(cx, CreatePromiseObjectForAsync(cx));
  if (!loadPromise) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Step 7. Let linkAndEvaluate be
  //         CreateBuiltinFunction(linkAndEvaluateClosure, 0, "", []).
  Rooted<JSFunction*> linkAndEvaluate(cx);
  linkAndEvaluate = js::NewFunctionWithReserved(
      cx, LinkAndEvaluateDynamicImport, 0, 0, "resolved");
  if (!linkAndEvaluate) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Step 8. Perform PerformPromiseThen(loadPromise, linkAndEvaluate,
  // onRejected).
  js::SetFunctionNativeReserved(linkAndEvaluate, 0, ObjectValue(*context));
  JS::AddPromiseReactions(cx, loadPromise, linkAndEvaluate, nullptr);
  return AsyncFunctionReturned(cx, loadPromise, UndefinedHandleValue);
}

// static
bool LinkAndEvaluateDynamicImport(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Value value = js::GetFunctionNativeReserved(&args.callee(), 0);
  Rooted<DynamicImportContextObject*> context(cx);
  context = &value.toObject().as<DynamicImportContextObject>();
  return LinkAndEvaluateDynamicImport(cx, context);
}

// https://tc39.es/ecma262/#sec-ContinueDynamicImport
static bool LinkAndEvaluateDynamicImport(
    JSContext* cx, Handle<DynamicImportContextObject*> context) {
  MOZ_ASSERT(context);
  Rooted<ModuleObject*> module(cx, context->module());
  Rooted<PromiseObject*> promise(cx, context->promise());

  // Step 6.a. Let link be Completion(module.Link()).
  if (!JS::ModuleLink(cx, module)) {
    //   b. If link is an abrupt completion, then
    //      i. Perform ! Call(promiseCapability.[[Reject]], undefined, [
    //         link.[[Value]] ]).
    //      ii. Return unused.
    return RejectPromiseWithPendingError(cx, promise);
  }
  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  // Step 6.c. Let evaluatePromise be module.Evaluate().
  JS::Rooted<JS::Value> rval(cx);
  mozilla::DebugOnly<bool> ok = JS::ModuleEvaluate(cx, module, &rval);
  MOZ_ASSERT_IF(ok, !JS_IsExceptionPending(cx));
  if (!rval.isObject()) {
    // If we do not have an evaluation promise or a module request for the
    // module, we can assume that evaluation has failed or been interrupted and
    // can reject the dynamic module.
    return RejectPromiseWithPendingError(cx, promise);
  }

  JS::Rooted<JSObject*> evaluatePromise(cx, &rval.toObject());
  MOZ_ASSERT(evaluatePromise->is<PromiseObject>());

  // Step 6.e. Let onFulfilled be CreateBuiltinFunction(fulfilledClosure, 0, "",
  //           []).
  RootedValue contextValue(cx, ObjectValue(*context));
  RootedFunction onFulfilled(cx);
  onFulfilled = NewHandlerWithExtraValue(cx, DynamicImportResolved, promise,
                                         contextValue);
  if (!onFulfilled) {
    return false;
  }

  // Step 5. Let onRejected be CreateBuiltinFunction(rejectedClosure, 1, "",
  //         []).
  RootedFunction onRejected(cx);
  onRejected = NewHandlerWithExtraValue(cx, DynamicImportRejected, promise,
                                        contextValue);
  if (!onRejected) {
    return false;
  }

  // Step 6.f. Perform PerformPromiseThen(evaluatePromise, onFulfilled,
  //           onRejected).
  // Step 6.g. Return unused.
  return JS::AddPromiseReactionsIgnoringUnhandledRejection(
      cx, evaluatePromise, onFulfilled, onRejected);
}

// This performs the steps for |fulfilledClosure| from
// https://tc39.es/ecma262/#sec-ContinueDynamicImport step 6.d.
//
// With adjustment for Top-level await:
// https://GitHub.com/tc39/proposal-dynamic-import/pull/71/files
static bool DynamicImportResolved(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  MOZ_ASSERT(args.get(0).isUndefined());

  Rooted<DynamicImportContextObject*> context(
      cx, ExtraFromHandler<DynamicImportContextObject>(args));

  Rooted<PromiseObject*> promise(cx, TargetFromHandler<PromiseObject>(args));

  Rooted<ModuleObject*> module(cx, context->module());
  if (module->status() != ModuleStatus::EvaluatingAsync &&
      module->status() != ModuleStatus::Evaluated) {
    JS_ReportErrorASCII(
        cx, "Unevaluated or errored module returned by module resolve hook");
    return RejectPromiseWithPendingError(cx, promise);
  }

  // This is called when |evaluationPromise| is resolved, step 6.f.
  MOZ_ASSERT_IF(module->hasCyclicModuleFields(),
                module->getCycleRoot()
                        ->topLevelCapability()
                        ->as<PromiseObject>()
                        .state() == JS::PromiseState::Fulfilled);

  // Step 6.d.i. Let namespace be GetModuleNamespace(module).
  RootedObject ns(cx, GetOrCreateModuleNamespace(cx, module));
  if (!ns) {
    return RejectPromiseWithPendingError(cx, promise);
  }

  // Step 6.d.ii. Perform ! Call(promiseCapability.[[Resolve]], undefined, [
  //              namespace ]).
  RootedValue value(cx, ObjectValue(*ns));
  if (!PromiseObject::resolve(cx, promise, value)) {
    return false;
  }

  // Step 6.d.iii. Return NormalCompletion(undefined).
  args.rval().setUndefined();
  return true;
};

// This performs the steps for |rejectedClosure| from
// https://tc39.es/ecma262/#sec-ContinueDynamicImport step 4.
static bool DynamicImportRejected(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue error = args.get(0);

  Rooted<DynamicImportContextObject*> context(
      cx, ExtraFromHandler<DynamicImportContextObject>(args));

  Rooted<PromiseObject*> promise(cx, TargetFromHandler<PromiseObject>(args));

  // Step 4.a. Perform ! Call(promiseCapability.[[Reject]], undefined, [ reason
  // ]).
  if (!PromiseObject::reject(cx, promise, error)) {
    return false;
  }

  // Step 4.b. Return NormalCompletion(undefined).
  args.rval().setUndefined();
  return true;
}
