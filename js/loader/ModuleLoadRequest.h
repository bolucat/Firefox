/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_ModuleLoadRequest_h
#define js_loader_ModuleLoadRequest_h

#include "LoadContextBase.h"
#include "ScriptLoadRequest.h"
#include "ModuleLoaderBase.h"
#include "mozilla/Assertions.h"
#include "js/RootingAPI.h"
#include "js/Value.h"
#include "nsURIHashKey.h"
#include "nsTHashtable.h"

namespace JS::loader {

class LoadedScript;
class ModuleScript;
class ModuleLoaderBase;

// A load request for a module, created for every top level module script and
// every module import.  Load request can share an ModuleScript if there are
// multiple imports of the same module.

class ModuleLoadRequest final : public ScriptLoadRequest {
  ~ModuleLoadRequest() {
    MOZ_ASSERT(!mReferrerScript);
    MOZ_ASSERT(!mModuleRequestObj);
    MOZ_ASSERT(mPayload.isUndefined());
  }

  ModuleLoadRequest(const ModuleLoadRequest& aOther) = delete;
  ModuleLoadRequest(ModuleLoadRequest&& aOther) = delete;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(ModuleLoadRequest,
                                                         ScriptLoadRequest)
  using SRIMetadata = mozilla::dom::SRIMetadata;

  enum class Kind {
    // Top-level modules, not imported statically or dynamically..
    TopLevel,

    // Modules imported statically with `import` declarations.
    StaticImport,

    // Modules imported dynamically with dynamic `import()`.
    // This is actually also a top-level module, but this should be used for
    // dynamic imports.
    DynamicImport,
  };

  ModuleLoadRequest(nsIURI* aURI, ModuleType aModuleType,
                    mozilla::dom::ReferrerPolicy aReferrerPolicy,
                    ScriptFetchOptions* aFetchOptions,
                    const SRIMetadata& aIntegrity, nsIURI* aReferrer,
                    LoadContextBase* aContext, Kind aKind,
                    ModuleLoaderBase* aLoader, ModuleLoadRequest* aRootModule);

  bool IsTopLevel() const override { return mIsTopLevel; }

  bool IsDynamicImport() const { return mIsDynamicImport; }

  bool IsErrored() const;

  nsIGlobalObject* GetGlobalObject();

  void SetReady() override;
  void Cancel() override;

  void SetDynamicImport(LoadedScript* aReferencingScript,
                        Handle<JSObject*> aModuleRequestObj,
                        Handle<JSObject*> aPromise);
  void ClearDynamicImport();

  void ModuleLoaded();
  void ModuleErrored();
  void LoadFailed();

  ModuleLoadRequest* GetRootModule() {
    if (!mRootModule) {
      return this;
    }
    return mRootModule;
  }

  void MarkModuleForBytecodeEncoding() { MarkForBytecodeEncoding(); }

  // Convenience methods to call into the module loader for this request.

  void CancelDynamicImport(nsresult aResult) {
    MOZ_ASSERT(IsDynamicImport());
    mLoader->CancelDynamicImport(this, aResult);
  }
#ifdef DEBUG
  bool IsRegisteredDynamicImport() const {
    return IsDynamicImport() && mLoader->HasDynamicImport(this);
  }
#endif
  nsresult StartModuleLoad() { return mLoader->StartModuleLoad(this); }
  nsresult RestartModuleLoad() { return mLoader->RestartModuleLoad(this); }
  nsresult OnFetchComplete(nsresult aRv) {
    return mLoader->OnFetchComplete(this, aRv);
  }
  bool InstantiateModuleGraph() {
    return mLoader->InstantiateModuleGraph(this);
  }
  nsresult EvaluateModule() { return mLoader->EvaluateModule(this); }
  void StartDynamicImport() { mLoader->StartDynamicImport(this); }
  void ProcessDynamicImport() { mLoader->ProcessDynamicImport(this); }

  void LoadFinished();

  void UpdateReferrerPolicy(mozilla::dom::ReferrerPolicy aReferrerPolicy) {
    mReferrerPolicy = aReferrerPolicy;
  }

  // Is this a request for a top level module script or an import?
  const bool mIsTopLevel;

  // Type of module (JavaScript, JSON)
  const ModuleType mModuleType;

  // Is this the top level request for a dynamic module import?
  const bool mIsDynamicImport;

  // Pointer to the script loader, used to trigger actions when the module load
  // finishes.
  RefPtr<ModuleLoaderBase> mLoader;

  // Pointer to the top level module of this module graph, nullptr if this is a
  // top level module
  RefPtr<ModuleLoadRequest> mRootModule;

  // Set to a module script object after a successful load or nullptr on
  // failure.
  RefPtr<ModuleScript> mModuleScript;

  Heap<JSScript*> mReferrerScript;
  Heap<JSObject*> mModuleRequestObj;
  Heap<Value> mPayload;
};

}  // namespace JS::loader

#endif  // js_loader_ModuleLoadRequest_h
