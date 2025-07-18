/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozJSModuleLoader_h
#define mozJSModuleLoader_h

#include "SyncModuleLoader.h"
#include "mozilla/Attributes.h"  // MOZ_STACK_CLASS
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/FileLocation.h"
#include "mozilla/Maybe.h"   // mozilla::Maybe
#include "mozilla/RefPtr.h"  // RefPtr, mozilla::StaticRefPtr
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadLocal.h"  // MOZ_THREAD_LOCAL
#include "nsIURI.h"
#include "nsClassHashtable.h"
#include "jsapi.h"
#include "js/CompileOptions.h"
#include "js/experimental/JSStencil.h"

#include "xpcpublic.h"

class nsIFile;
class ModuleLoaderInfo;

namespace mozilla {
class ScriptPreloader;
}  // namespace mozilla

namespace JS::loader {
class ModuleLoadRequest;
}  // namespace JS::loader

#if defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION) || defined(DEBUG)
#  define STARTUP_RECORDER_ENABLED
#endif

namespace mozilla::loader {

class NonSharedGlobalSyncModuleLoaderScope;

}  // namespace mozilla::loader

class mozJSModuleLoader final {
 public:
  NS_INLINE_DECL_REFCOUNTING(mozJSModuleLoader);

  // Returns the list of all ESMs.
  nsresult GetLoadedESModules(nsTArray<nsCString>& aLoadedModules);

  nsresult GetModuleImportStack(const nsACString& aLocation,
                                nsACString& aRetval);

  void FindTargetObject(JSContext* aCx, JS::MutableHandleObject aTargetObject);

  static void InitStatics();
  static void UnloadLoaders();
  static void ShutdownLoaders();

  static mozJSModuleLoader* Get() {
    MOZ_ASSERT(sSelf, "Should have already created the module loader");
    return sSelf;
  }

  JSObject* GetSharedGlobal() {
    MOZ_ASSERT(mLoaderGlobal);
    return mLoaderGlobal;
  }

 private:
  void InitSharedGlobal(JSContext* aCx);

  void InitSyncModuleLoaderForGlobal(nsIGlobalObject* aGlobal);
  void DisconnectSyncModuleLoaderFromGlobal();

  friend class mozilla::loader::NonSharedGlobalSyncModuleLoaderScope;

 public:
  static mozJSModuleLoader* GetDevToolsLoader() { return sDevToolsLoader; }
  static mozJSModuleLoader* GetOrCreateDevToolsLoader(JSContext* aCx);

  // Synchronously load an ES6 module and all its dependencies.
  nsresult ImportESModule(JSContext* aCx, const nsACString& aResourceURI,
                          JS::MutableHandleObject aModuleNamespace);

#ifdef STARTUP_RECORDER_ENABLED
  void RecordImportStack(JSContext* aCx,
                         JS::loader::ModuleLoadRequest* aRequest);
#endif

  nsresult IsESModuleLoaded(const nsACString& aResourceURI, bool* aRetval);
  bool IsLoaderGlobal(JSObject* aObj) { return mLoaderGlobal == aObj; }
  bool IsDevToolsLoader() const { return this == sDevToolsLoader; }

  static bool IsSharedSystemGlobal(nsIGlobalObject* aGlobal);
  static bool IsDevToolsLoaderGlobal(nsIGlobalObject* aGlobal);

  // Public methods for use from SyncModuleLoader.
  static nsresult LoadSingleModuleScript(
      mozilla::loader::SyncModuleLoader* aModuleLoader, JSContext* aCx,
      JS::loader::ModuleLoadRequest* aRequest,
      JS::MutableHandleScript aScriptOut);

 private:
  static nsresult ReadScriptOnMainThread(JSContext* aCx,
                                         const nsCString& aLocation,
                                         nsCString& aData);
  static nsresult LoadSingleModuleScriptOnWorker(
      mozilla::loader::SyncModuleLoader* aModuleLoader, JSContext* aCx,
      JS::loader::ModuleLoadRequest* aRequest,
      JS::MutableHandleScript aScriptOut);

 public:
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

  bool DefineJSServices(JSContext* aCx, JS::Handle<JSObject*> aGlobal);

 protected:
  mozJSModuleLoader();
  ~mozJSModuleLoader();

  friend class XPCJSRuntime;

 private:
  static mozilla::StaticRefPtr<mozJSModuleLoader> sSelf;
  static mozilla::StaticRefPtr<mozJSModuleLoader> sDevToolsLoader;

  void Unload();
  void UnloadModules();

  void CreateLoaderGlobal(JSContext* aCx, const nsACString& aLocation,
                          JS::MutableHandleObject aGlobal);
  void CreateDevToolsLoaderGlobal(JSContext* aCx, const nsACString& aLocation,
                                  JS::MutableHandleObject aGlobal);

  bool CreateJSServices(JSContext* aCx);

  static nsresult GetSourceFile(nsIURI* aResolvedURI, nsIFile** aSourceFileOut);

  static bool LocationIsRealFile(nsIURI* aURI);

  static void SetModuleOptions(JS::CompileOptions& aOptions);

  // Get the script for a given location, either from a cached stencil or by
  // compiling it from source.
  static nsresult GetScriptForLocation(JSContext* aCx, ModuleLoaderInfo& aInfo,
                                       nsIFile* aModuleFile, bool aUseMemMap,
                                       JS::MutableHandleScript aScriptOut,
                                       char** aLocationOut = nullptr);

  static JSScript* InstantiateStencil(JSContext* aCx, JS::Stencil* aStencil);

  class ModuleEntry {
   public:
    explicit ModuleEntry(JS::RootingContext* aRootingCx)
        : obj(aRootingCx), exports(aRootingCx), thisObjectKey(aRootingCx) {
      location = nullptr;
    }

    ~ModuleEntry() { Clear(); }

    void Clear() {
      if (obj) {
        if (JS_HasExtensibleLexicalEnvironment(obj)) {
          JS::RootedObject lexicalEnv(mozilla::dom::RootingCx(),
                                      JS_ExtensibleLexicalEnvironment(obj));
          JS_SetAllNonReservedSlotsToUndefined(lexicalEnv);
        }
        JS_SetAllNonReservedSlotsToUndefined(obj);
        obj = nullptr;
        thisObjectKey = nullptr;
      }

      if (location) {
        free(location);
      }

      obj = nullptr;
      thisObjectKey = nullptr;
      location = nullptr;
    }

    size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

    JS::PersistentRootedObject obj;
    JS::PersistentRootedObject exports;
    JS::PersistentRootedScript thisObjectKey;
    char* location;
    nsCString resolvedURL;
  };

#ifdef STARTUP_RECORDER_ENABLED
  nsTHashMap<nsCStringHashKey, nsCString> mImportStacks;
#endif

  bool mInitialized;
  bool mIsUnloaded = false;
#ifdef DEBUG
  bool mIsInitializingLoaderGlobal = false;
#endif
  JS::PersistentRooted<JSObject*> mLoaderGlobal;
  JS::PersistentRooted<JSObject*> mServicesObj;

  RefPtr<mozilla::loader::SyncModuleLoader> mModuleLoader;
};

namespace mozilla::loader {

// Automatically allocate and initialize a sync module loader for given
// non-shared global, and override the module loader for the global with sync
// module loader.
//
// This is not re-entrant, and the consumer must check IsActive method before
// allocating this on the stack.
//
// The consumer should ensure the target global's module loader has no
// ongoing fetching modules (ModuleLoaderBase::HasFetchingModules).
// If there's any fetching modules, the consumer should wait for them before
// allocating this class on the stack.
//
// The consumer should also verify that the target global has module loader,
// as a part of the above step.
//
// The loader returned by ActiveLoader can be reused only when
// ActiveLoader's global matches the global the consumer wants to use.
class MOZ_STACK_CLASS NonSharedGlobalSyncModuleLoaderScope {
 public:
  NonSharedGlobalSyncModuleLoaderScope(JSContext* aCx,
                                       nsIGlobalObject* aGlobal);
  ~NonSharedGlobalSyncModuleLoaderScope();

  // After successfully importing a module graph, move all imported modules to
  // the target global's module loader.
  void Finish();

  // Returns true if another instance of NonSharedGlobalSyncModuleLoaderScope
  // is on stack.
  static bool IsActive();

  static mozJSModuleLoader* ActiveLoader();

  static void InitStatics();

 private:
  RefPtr<mozJSModuleLoader> mLoader;

  // Reference to thread-local module loader on the stack.
  // This is used by another sync module load during a sync module load is
  // ongoing.
  static MOZ_THREAD_LOCAL(mozJSModuleLoader*) sTlsActiveLoader;

  // The module loader of the target global.
  RefPtr<JS::loader::ModuleLoaderBase> mAsyncModuleLoader;

  mozilla::Maybe<JS::loader::AutoOverrideModuleLoader> mMaybeOverride;
};

}  // namespace mozilla::loader

#endif  // mozJSModuleLoader_h
