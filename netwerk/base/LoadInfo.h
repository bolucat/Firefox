/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_LoadInfo_h
#define mozilla_LoadInfo_h

#include "mozilla/dom/FeaturePolicy.h"
#include "mozilla/dom/UserNavigationInvolvement.h"
#include "nsIInterceptionInfo.h"
#include "nsILoadInfo.h"
#include "nsIPrincipal.h"
#include "nsIWeakReferenceUtils.h"  // for nsWeakPtr
#include "nsIURI.h"
#include "nsContentUtils.h"
#include "nsString.h"
#include "nsTArray.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Result.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"

class nsDocShell;
class nsICookieJarSettings;
class nsIPolicyContainer;
class nsINode;
class nsPIDOMWindowOuter;

namespace mozilla {

namespace dom {
class PerformanceStorage;
class XMLHttpRequestMainThread;
class CanonicalBrowsingContext;
class WindowGlobalParent;
}  // namespace dom

namespace net {
class EarlyHintPreloader;
class LoadInfoArgs;
class LoadInfo;
}  // namespace net

namespace ipc {
// we have to forward declare that function so we can use it as a friend.
nsresult LoadInfoArgsToLoadInfo(const mozilla::net::LoadInfoArgs& aLoadInfoArgs,
                                const nsACString& aOriginRemoteType,
                                nsINode* aCspToInheritLoadingContext,
                                net::LoadInfo** outLoadInfo);

}  // namespace ipc

#define LOADINFO_DUMMY_SETTER(type, name)

#define LOADINFO_FOR_EACH_FIELD(GETTER, SETTER)                                \
  /*     Type  Name                  LoadInfoArgs Name     Default         */  \
  GETTER(bool, BlockAllMixedContent, blockAllMixedContent, false)              \
                                                                               \
  GETTER(bool, UpgradeInsecureRequests, upgradeInsecureRequests, false)        \
                                                                               \
  GETTER(bool, BrowserUpgradeInsecureRequests, browserUpgradeInsecureRequests, \
         false)                                                                \
                                                                               \
  GETTER(bool, BrowserDidUpgradeInsecureRequests,                              \
         browserDidUpgradeInsecureRequests, false)                             \
  SETTER(bool, BrowserDidUpgradeInsecureRequests)                              \
                                                                               \
  GETTER(bool, BrowserWouldUpgradeInsecureRequests,                            \
         browserWouldUpgradeInsecureRequests, false)                           \
                                                                               \
  GETTER(bool, ForceAllowDataURI, forceAllowDataURI, false)                    \
  SETTER(bool, ForceAllowDataURI)                                              \
                                                                               \
  GETTER(bool, AllowInsecureRedirectToDataURI, allowInsecureRedirectToDataURI, \
         false)                                                                \
  SETTER(bool, AllowInsecureRedirectToDataURI)                                 \
                                                                               \
  GETTER(bool, SkipContentPolicyCheckForWebRequest,                            \
         skipContentPolicyCheckForWebRequest, false)                           \
  SETTER(bool, SkipContentPolicyCheckForWebRequest)                            \
                                                                               \
  GETTER(bool, OriginalFrameSrcLoad, originalFrameSrcLoad, false)              \
  SETTER(bool, OriginalFrameSrcLoad)                                           \
                                                                               \
  GETTER(bool, ForceInheritPrincipalDropped, forceInheritPrincipalDropped,     \
         false)                                                                \
                                                                               \
  GETTER(uint64_t, InnerWindowID, innerWindowID, 0)                            \
                                                                               \
  GETTER(uint64_t, BrowsingContextID, browsingContextID, 0)                    \
                                                                               \
  GETTER(uint64_t, FrameBrowsingContextID, frameBrowsingContextID, 0)          \
                                                                               \
  GETTER(bool, IsOn3PCBExceptionList, isOn3PCBExceptionList, false)            \
  SETTER(bool, IsOn3PCBExceptionList)                                          \
                                                                               \
  GETTER(bool, IsFormSubmission, isFormSubmission, false)                      \
  SETTER(bool, IsFormSubmission)                                               \
                                                                               \
  GETTER(bool, IsGETRequest, isGETRequest, true)                               \
  SETTER(bool, IsGETRequest)                                                   \
                                                                               \
  GETTER(bool, SendCSPViolationEvents, sendCSPViolationEvents, true)           \
  SETTER(bool, SendCSPViolationEvents)                                         \
                                                                               \
  GETTER(uint32_t, RequestBlockingReason, requestBlockingReason,               \
         BLOCKING_REASON_NONE)                                                 \
  SETTER(uint32_t, RequestBlockingReason)                                      \
                                                                               \
  GETTER(bool, ForcePreflight, forcePreflight, false)                          \
                                                                               \
  GETTER(bool, IsPreflight, isPreflight, false)                                \
                                                                               \
  GETTER(bool, ServiceWorkerTaintingSynthesized,                               \
         serviceWorkerTaintingSynthesized, false)                              \
                                                                               \
  GETTER(bool, DocumentHasUserInteracted, documentHasUserInteracted, false)    \
  SETTER(bool, DocumentHasUserInteracted)                                      \
                                                                               \
  GETTER(bool, AllowListFutureDocumentsCreatedFromThisRedirectChain,           \
         allowListFutureDocumentsCreatedFromThisRedirectChain, false)          \
  SETTER(bool, AllowListFutureDocumentsCreatedFromThisRedirectChain)           \
                                                                               \
  GETTER(bool, NeedForCheckingAntiTrackingHeuristic,                           \
         needForCheckingAntiTrackingHeuristic, false)                          \
  SETTER(bool, NeedForCheckingAntiTrackingHeuristic)                           \
                                                                               \
  GETTER(bool, SkipContentSniffing, skipContentSniffing, false)                \
  SETTER(bool, SkipContentSniffing)                                            \
                                                                               \
  GETTER(uint32_t, HttpsOnlyStatus, httpsOnlyStatus,                           \
         nsILoadInfo::HTTPS_ONLY_UNINITIALIZED)                                \
  SETTER(uint32_t, HttpsOnlyStatus)                                            \
                                                                               \
  GETTER(bool, HstsStatus, httpsOnlyStatus, false)                             \
  SETTER(bool, HstsStatus)                                                     \
                                                                               \
  GETTER(bool, HasValidUserGestureActivation, hasValidUserGestureActivation,   \
         false)                                                                \
  SETTER(bool, HasValidUserGestureActivation)                                  \
                                                                               \
  GETTER(bool, TextDirectiveUserActivation, textDirectiveUserActivation,       \
         false)                                                                \
  SETTER(bool, TextDirectiveUserActivation)                                    \
                                                                               \
  GETTER(bool, AllowDeprecatedSystemRequests, allowDeprecatedSystemRequests,   \
         false)                                                                \
  SETTER(bool, AllowDeprecatedSystemRequests)                                  \
                                                                               \
  GETTER(bool, IsInDevToolsContext, isInDevToolsContext, false)                \
  SETTER(bool, IsInDevToolsContext)                                            \
                                                                               \
  GETTER(bool, ParserCreatedScript, parserCreatedScript, false)                \
  SETTER(bool, ParserCreatedScript)                                            \
                                                                               \
  GETTER(Maybe<dom::RequestMode>, RequestMode, requestMode, Nothing())         \
  SETTER(Maybe<dom::RequestMode>, RequestMode)                                 \
                                                                               \
  GETTER(bool, IsMetaRefresh, isMetaRefresh, false)                            \
  SETTER(bool, IsMetaRefresh)                                                  \
                                                                               \
  GETTER(bool, IsFromProcessingFrameAttributes,                                \
         isFromProcessingFrameAttributes, false)                               \
                                                                               \
  GETTER(bool, IsMediaRequest, isMediaRequest, false)                          \
  SETTER(bool, IsMediaRequest)                                                 \
                                                                               \
  GETTER(bool, IsMediaInitialRequest, isMediaInitialRequest, false)            \
  SETTER(bool, IsMediaInitialRequest)                                          \
                                                                               \
  GETTER(bool, IsFromObjectOrEmbed, isFromObjectOrEmbed, false)                \
  SETTER(bool, IsFromObjectOrEmbed)

// Heads-up: LoadInfoToLoadInfoArgs still needs to be manually updated.

namespace net {
using RedirectHistoryArray = nsTArray<nsCOMPtr<nsIRedirectHistoryEntry>>;

/**
 * Class that provides an nsILoadInfo implementation.
 */
class LoadInfo final : public nsILoadInfo {
  template <typename T, typename... Args>
  friend already_AddRefed<T> mozilla::MakeAndAddRef(Args&&... aArgs);

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSILOADINFO

  // Currently used for most load types, but prefer the specialized
  // factories below when possible. aLoadingPrincipal MUST NOT BE NULL.
  static mozilla::Result<already_AddRefed<LoadInfo>, nsresult> Create(
      nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
      nsINode* aLoadingContext, nsSecurityFlags aSecurityFlags,
      nsContentPolicyType aContentPolicyType,
      const Maybe<mozilla::dom::ClientInfo>& aLoadingClientInfo =
          Maybe<mozilla::dom::ClientInfo>(),
      const Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController =
          Maybe<mozilla::dom::ServiceWorkerDescriptor>(),
      uint32_t aSandboxFlags = 0);

  // Used for TYPE_DOCUMENT load.
  static already_AddRefed<LoadInfo> CreateForDocument(
      dom::CanonicalBrowsingContext* aBrowsingContext, nsIURI* aURI,
      nsIPrincipal* aTriggeringPrincipal,
      const nsACString& aTriggeringRemoteType,
      const OriginAttributes& aOriginAttributes, nsSecurityFlags aSecurityFlags,
      uint32_t aSandboxFlags);

  // Used for TYPE_FRAME or TYPE_IFRAME load.
  static already_AddRefed<LoadInfo> CreateForFrame(
      dom::CanonicalBrowsingContext* aBrowsingContext,
      nsIPrincipal* aTriggeringPrincipal,
      const nsACString& aTriggeringRemoteType, nsSecurityFlags aSecurityFlags,
      uint32_t aSandboxFlags);

  // Use for non-{TYPE_DOCUMENT|TYPE_FRAME|TYPE_IFRAME} load.
  static already_AddRefed<LoadInfo> CreateForNonDocument(
      dom::WindowGlobalParent* aParentWGP, nsIPrincipal* aTriggeringPrincipal,
      nsContentPolicyType aContentPolicyType, nsSecurityFlags aSecurityFlags,
      uint32_t aSandboxFlags);

  // Constructor used for TYPE_DOCUMENT loads which have a different
  // loadingContext than other loads. This ContextForTopLevelLoad is
  // only used for content policy checks.
  LoadInfo(nsPIDOMWindowOuter* aOuterWindow, nsIURI* aURI,
           nsIPrincipal* aTriggeringPrincipal,
           nsISupports* aContextForTopLevelLoad, nsSecurityFlags aSecurityFlags,
           uint32_t aSandboxFlags);

 private:
  // Use factory function Create.
  // aLoadingPrincipal MUST NOT BE NULL.
  LoadInfo(nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
           nsINode* aLoadingContext, nsSecurityFlags aSecurityFlags,
           nsContentPolicyType aContentPolicyType,
           const Maybe<mozilla::dom::ClientInfo>& aLoadingClientInfo,
           const Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
           uint32_t aSandboxFlags);

  // Use factory function CreateForDocument
  // Used for TYPE_DOCUMENT load.
  LoadInfo(dom::CanonicalBrowsingContext* aBrowsingContext, nsIURI* aURI,
           nsIPrincipal* aTriggeringPrincipal,
           const nsACString& aTriggeringRemoteType,
           const OriginAttributes& aOriginAttributes,
           nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags);

  // Use factory function CreateForFrame
  // Used for TYPE_FRAME or TYPE_IFRAME load.
  LoadInfo(dom::CanonicalBrowsingContext* aBrowsingContext,
           nsIPrincipal* aTriggeringPrincipal,
           const nsACString& aTriggeringRemoteType,
           nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags);

  // Used for loads initiated by DocumentLoadListener that are not
  // TYPE_DOCUMENT | TYPE_FRAME | TYPE_FRAME.
  LoadInfo(dom::WindowGlobalParent* aParentWGP,
           nsIPrincipal* aTriggeringPrincipal,
           const nsACString& aTriggeringRemoteType,
           nsContentPolicyType aContentPolicyType,
           nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags);

 public:
  // Compute a list of ancestor principals and BrowsingContext IDs.
  // See methods AncestorPrincipals and AncestorBrowsingContextIDs
  // in nsILoadInfo.idl for details.
  static void ComputeAncestors(
      dom::CanonicalBrowsingContext* aBC,
      nsTArray<nsCOMPtr<nsIPrincipal>>& aAncestorPrincipals,
      nsTArray<uint64_t>& aBrowsingContextIDs);

  // create an exact copy of the loadinfo
  already_AddRefed<nsILoadInfo> Clone() const;

  // hands off!!! don't use CloneWithNewSecFlags unless you know
  // exactly what you are doing - it should only be used within
  // nsBaseChannel::Redirect()
  already_AddRefed<nsILoadInfo> CloneWithNewSecFlags(
      nsSecurityFlags aSecurityFlags) const;
  // creates a copy of the loadinfo which is appropriate to use for a
  // separate request. I.e. not for a redirect or an inner channel, but
  // when a separate request is made with the same security properties.
  already_AddRefed<nsILoadInfo> CloneForNewRequest() const;

  // The `nsContentPolicyType GetExternalContentPolicyType()` version
  // in the base class is hidden by the implementation of
  // `GetExternalContentPolicyType(nsContentPolicyType* aResult)` in
  // LoadInfo.cpp. Explicit mark it visible.
  using nsILoadInfo::GetExternalContentPolicyType;

  void SetIsPreflight();
  void SetUpgradeInsecureRequests(bool aValue);
  void SetBrowserUpgradeInsecureRequests();
  void SetBrowserWouldUpgradeInsecureRequests();
  void SetIsFromProcessingFrameAttributes();

  // Hands off from the cspToInherit functionality!
  //
  // For navigations, GetCSPToInherit returns what the spec calls the
  // "request's client's global object's CSP list", or more precisely
  // a snapshot of it taken when the navigation starts.  For
  // navigations that need to inherit their CSP, this is the right CSP
  // to use for the new document.  We need a way to transfer the CSP
  // from the docshell (where the navigation starts) to the point where
  // the new document is created and decides whether to inherit its
  // CSP, and this is the mechanism we use for that.
  //
  // For example:
  // A document with a CSP triggers a new top-level data: URI load.
  // We pass the CSP of the document that triggered the load all the
  // way to docshell. Within docshell we call SetCSPToInherit() on the
  // loadinfo. Within Document::InitCSP() we check if the newly created
  // document needs to inherit the CSP. If so, we call
  // GetCSPToInherit() and set the inherited CSP as the CSP for the new
  // document. Please note that any additonal Meta CSP in that document
  // will be merged into that CSP. Any subresource loads within that
  // document subesquently will receive the correct CSP by querying
  // loadinfo->GetCsp() from that point on.
  void SetPolicyContainerToInherit(
      nsIPolicyContainer* aPolicyContainerToInherit);

  bool HasIsThirdPartyContextToTopWindowSet() {
    return mIsThirdPartyContextToTopWindow.isSome();
  }
  void ClearIsThirdPartyContextToTopWindow() {
    mIsThirdPartyContextToTopWindow.reset();
  }

  void SetContinerFeaturePolicy(
      const Maybe<dom::FeaturePolicyInfo>& aContainerFeaturePolicy) {
    mContainerFeaturePolicyInfo = aContainerFeaturePolicy;
  }

#ifdef DEBUG
  void MarkOverriddenFingerprintingSettingsAsSet() {
    mOverriddenFingerprintingSettingsIsSet = true;
  }
#endif

 private:
  // Private constructor that is only allowed to be called from within
  // mozilla::ipc::LoadInfoArgsToLoadInfo declared as friends undeneath.
  // In e10s we can not serialize nsINode, hence we store the innerWindowID.
  // Please note that aRedirectChain uses swapElements.
  LoadInfo(nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
           nsIPrincipal* aPrincipalToInherit, nsIPrincipal* aTopLevelPrincipal,
           nsIURI* aResultPrincipalURI,
           nsICookieJarSettings* aCookieJarSettings,
           nsIPolicyContainer* aPolicyContainerToInherit,
           const nsACString& aTriggeringRemoteType,
           const nsID& aSandboxedNullPrincipalID,
           const Maybe<mozilla::dom::ClientInfo>& aClientInfo,
           const Maybe<mozilla::dom::ClientInfo>& aReservedClientInfo,
           const Maybe<mozilla::dom::ClientInfo>& aInitialClientInfo,
           const Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
           nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags,
           uint32_t aTriggeringSandboxFlags, uint64_t aTriggeringWindowId,
           bool aTriggeringStorageAccess,
           uint32_t aTriggeringFirstPartyClassificationFlags,
           uint32_t aTriggeringThirdPartyClassificationFlags,
           nsContentPolicyType aContentPolicyType, LoadTainting aTainting,

#define DEFINE_PARAMETER(type, name, _n, _d) type a##name,
           LOADINFO_FOR_EACH_FIELD(DEFINE_PARAMETER, LOADINFO_DUMMY_SETTER)
#undef DEFINE_PARAMETER

               bool aInitialSecurityCheckDone,
           bool aIsThirdPartyContext,
           const Maybe<bool>& aIsThirdPartyContextToTopWindow,
           const OriginAttributes& aOriginAttributes,
           RedirectHistoryArray&& aRedirectChainIncludingInternalRedirects,
           RedirectHistoryArray&& aRedirectChain,
           nsTArray<nsCOMPtr<nsIPrincipal>>&& aAncestorPrincipals,
           const nsTArray<uint64_t>& aAncestorBrowsingContextIDs,
           const nsTArray<nsCString>& aCorsUnsafeHeaders,
           bool aLoadTriggeredFromExternal, const nsAString& aCspNonce,
           const nsAString& aIntegrityMetadata, bool aIsSameDocumentNavigation,
           nsILoadInfo::StoragePermissionState aStoragePermission,
           nsILoadInfo::IPAddressSpace aParentIPAddressSpace,
           nsILoadInfo::IPAddressSpace aIPAddressSpace,
           const Maybe<RFPTargetSet>& aOverriddenFingerprintingSettings,
           nsINode* aLoadingContext,
           nsILoadInfo::CrossOriginEmbedderPolicy aLoadingEmbedderPolicy,
           bool aIsOriginTrialCoepCredentiallessEnabledForTopLevel,
           nsIURI* aUnstrippedURI, nsIInterceptionInfo* aInterceptionInfo,
           bool aHasInjectedCookieForCookieBannerHandling,
           nsILoadInfo::SchemelessInputType aSchemelessInput,
           nsILoadInfo::HTTPSUpgradeTelemetryType aHttpsUpgradeTelemetry,
           bool aIsNewWindowTarget,
           dom::UserNavigationInvolvement aUserNavigationInvolvement);

  LoadInfo(const LoadInfo& rhs);

  NS_IMETHOD GetRedirects(JSContext* aCx,
                          JS::MutableHandle<JS::Value> aRedirects,
                          const RedirectHistoryArray& aArra);

  friend nsresult mozilla::ipc::LoadInfoArgsToLoadInfo(
      const mozilla::net::LoadInfoArgs& aLoadInfoArgs,
      const nsACString& aOriginRemoteType, nsINode* aCspToInheritLoadingContext,
      net::LoadInfo** outLoadInfo);

  ~LoadInfo();

  void ComputeIsThirdPartyContext(nsPIDOMWindowOuter* aOuterWindow);
  void ComputeIsThirdPartyContext(dom::WindowGlobalParent* aGlobal);

  bool IsDocumentMissingClientInfo();

  // This function is the *only* function which can change the securityflags
  // of a loadinfo. It only exists because of the XHR code. Don't call it
  // from anywhere else!
  void SetIncludeCookiesSecFlag();
  friend class mozilla::dom::XMLHttpRequestMainThread;

  // nsDocShell::OpenInitializedChannel and EarlyHintPreloader::OpenChannel
  // needs to update the loadInfo with the correct browsingContext.
  friend class ::nsDocShell;
  friend class mozilla::net::EarlyHintPreloader;
  void UpdateBrowsingContextID(uint64_t aBrowsingContextID) {
    mBrowsingContextID = aBrowsingContextID;
  }
  void UpdateFrameBrowsingContextID(uint64_t aFrameBrowsingContextID) {
    mFrameBrowsingContextID = aFrameBrowsingContextID;
  }

  void UpdateParentAddressSpaceInfo();
  MOZ_NEVER_INLINE void ReleaseMembers();

  // if you add a member, please also update the copy constructor and consider
  // if it should be merged from parent channel through
  // ParentLoadInfoForwarderArgs.
  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;
  nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;
  nsCOMPtr<nsIPrincipal> mPrincipalToInherit;
  nsCOMPtr<nsIPrincipal> mTopLevelPrincipal;
  nsCOMPtr<nsIURI> mResultPrincipalURI;
  nsCOMPtr<nsIURI> mChannelCreationOriginalURI;
  nsCOMPtr<nsICSPEventListener> mCSPEventListener;
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;
  nsCOMPtr<nsIPolicyContainer> mPolicyContainerToInherit;
  Maybe<dom::FeaturePolicyInfo> mContainerFeaturePolicyInfo;
  nsCString mTriggeringRemoteType;
  nsID mSandboxedNullPrincipalID;

  Maybe<mozilla::dom::ClientInfo> mClientInfo;
  UniquePtr<mozilla::dom::ClientSource> mReservedClientSource;
  Maybe<mozilla::dom::ClientInfo> mReservedClientInfo;
  Maybe<mozilla::dom::ClientInfo> mInitialClientInfo;
  Maybe<mozilla::dom::ServiceWorkerDescriptor> mController;
  RefPtr<mozilla::dom::PerformanceStorage> mPerformanceStorage;

  nsWeakPtr mLoadingContext;
  nsWeakPtr mContextForTopLevelLoad;
  nsSecurityFlags mSecurityFlags;
  uint32_t mSandboxFlags;
  uint32_t mTriggeringSandboxFlags = 0;
  uint64_t mTriggeringWindowId = 0;
  bool mTriggeringStorageAccess = false;
  uint32_t mTriggeringFirstPartyClassificationFlags = 0;
  uint32_t mTriggeringThirdPartyClassificationFlags = 0;
  nsContentPolicyType mInternalContentPolicyType;
  LoadTainting mTainting = LoadTainting::Basic;

#define DEFINE_FIELD(type, name, _, default_init) type m##name = default_init;
  LOADINFO_FOR_EACH_FIELD(DEFINE_FIELD, LOADINFO_DUMMY_SETTER)
#undef DEFINE_FIELD

  uint64_t mWorkerAssociatedBrowsingContextID = 0;
  bool mInitialSecurityCheckDone = false;
  // NB: TYPE_DOCUMENT implies !third-party.
  bool mIsThirdPartyContext = false;
  Maybe<bool> mIsThirdPartyContextToTopWindow;
  OriginAttributes mOriginAttributes;
  RedirectHistoryArray mRedirectChainIncludingInternalRedirects;
  RedirectHistoryArray mRedirectChain;
  nsTArray<nsCOMPtr<nsIPrincipal>> mAncestorPrincipals;
  nsTArray<uint64_t> mAncestorBrowsingContextIDs;
  nsTArray<nsCString> mCorsUnsafeHeaders;
  bool mLoadTriggeredFromExternal = false;
  nsString mCspNonce;
  nsString mIntegrityMetadata;
  bool mIsSameDocumentNavigation = false;
  bool mIsUserTriggeredSave = false;
  nsILoadInfo::StoragePermissionState mStoragePermission =
      nsILoadInfo::NoStoragePermission;
  // IP Address space of the parent browsing context.
  nsILoadInfo::IPAddressSpace mParentIPAddressSpace = nsILoadInfo::Unknown;
  nsILoadInfo::IPAddressSpace mIPAddressSpace = nsILoadInfo::Unknown;

  Maybe<RFPTargetSet> mOverriddenFingerprintingSettings;
#ifdef DEBUG
  // A boolean used to ensure the mOverriddenFingerprintingSettings is set
  // before use it.
  bool mOverriddenFingerprintingSettingsIsSet = false;
#endif

  // The cross origin embedder policy that the loading need to respect.
  // If the value is nsILoadInfo::EMBEDDER_POLICY_REQUIRE_CORP, CORP checking
  // must be performed for the loading.
  // See https://wicg.github.io/cross-origin-embedder-policy/#corp-check.
  nsILoadInfo::CrossOriginEmbedderPolicy mLoadingEmbedderPolicy =
      nsILoadInfo::EMBEDDER_POLICY_NULL;

  bool mIsOriginTrialCoepCredentiallessEnabledForTopLevel = false;

  nsCOMPtr<nsIURI> mUnstrippedURI;

  nsCOMPtr<nsIInterceptionInfo> mInterceptionInfo;

  bool mHasInjectedCookieForCookieBannerHandling = false;
  nsILoadInfo::SchemelessInputType mSchemelessInput =
      nsILoadInfo::SchemelessInputTypeUnset;

  nsILoadInfo::HTTPSUpgradeTelemetryType mHttpsUpgradeTelemetry =
      nsILoadInfo::NOT_INITIALIZED;

  dom::UserNavigationInvolvement mUserNavigationInvolvement =
      dom::UserNavigationInvolvement::None;

  bool mIsNewWindowTarget = false;
  bool mSkipHTTPSUpgrade = false;
};
// This is exposed solely for testing purposes and should not be used outside of
// LoadInfo
already_AddRefed<nsIPrincipal> CreateTruncatedPrincipal(nsIPrincipal*);

}  // namespace net

}  // namespace mozilla

#endif  // mozilla_LoadInfo_h
