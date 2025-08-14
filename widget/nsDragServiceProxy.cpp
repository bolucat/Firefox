/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDragServiceProxy.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/widget/WidgetLogging.h"
#include "nsContentUtils.h"

using mozilla::CSSIntRegion;
using mozilla::LayoutDeviceIntRect;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::dom::BrowserChild;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::SourceSurface;
using mozilla::gfx::SurfaceFormat;
using mozilla::ipc::Shmem;

#define LOGD DRAGSERVICE_LOGD
#define LOGI DRAGSERVICE_LOGI
#define LOGE DRAGSERVICE_LOGE

nsDragServiceProxy::nsDragServiceProxy() {
  LOGD("[%p] %s", this, __FUNCTION__);
}

nsDragSessionProxy::nsDragSessionProxy() {
  LOGD("[%p] %s", this, __FUNCTION__);
}

nsDragServiceProxy::~nsDragServiceProxy() {
  LOGD("[%p] %s", this, __FUNCTION__);
}

nsDragSessionProxy::~nsDragSessionProxy() {
  LOGD("[%p] %s", this, __FUNCTION__);
}

already_AddRefed<nsIDragSession> nsDragServiceProxy::CreateDragSession() {
  RefPtr<nsIDragSession> session = new nsDragSessionProxy();
  return session.forget();
}

nsresult nsDragSessionProxy::InvokeDragSession(
    nsIWidget* aWidget, nsINode* aDOMNode, nsIPrincipal* aPrincipal,
    nsIPolicyContainer* aPolicyContainer,
    nsICookieJarSettings* aCookieJarSettings, nsIArray* aTransferableArray,
    uint32_t aActionType, nsContentPolicyType aContentPolicyType) {
  BrowserChild* sourceBrowser = aWidget->GetOwningBrowserChild();
  LOGI("[%p] %s | aWidget: %p | sourceBrowser: %p | sourceSession: %p", this,
       __FUNCTION__, aWidget, sourceBrowser,
       sourceBrowser ? RefPtr(sourceBrowser->GetDragSession()).get() : nullptr);
  NS_ENSURE_TRUE(sourceBrowser, NS_ERROR_INVALID_ARG);
  [[maybe_unused]] RefPtr<nsIDragSession> sourceSession =
      sourceBrowser->GetDragSession();
  MOZ_ASSERT(!sourceSession);
  MOZ_ALWAYS_SUCCEEDS(
      sourceBrowser->GetWeakReference(getter_AddRefs(mSourceBrowser)));
  sourceBrowser->SetDragSession(this);
  nsresult rv = nsBaseDragSession::InvokeDragSession(
      aWidget, aDOMNode, aPrincipal, aPolicyContainer, aCookieJarSettings,
      aTransferableArray, aActionType, aContentPolicyType);
  return rv;
}

nsresult nsDragSessionProxy::InvokeDragSessionImpl(
    nsIWidget* aWidget, nsIArray* aArrayTransferables,
    const Maybe<CSSIntRegion>& aRegion, uint32_t aActionType) {
  LOGD("[%p] %s | aWidget: %p | aActionType: %u", this, __FUNCTION__, aWidget,
       aActionType);
  NS_ENSURE_STATE(mSourceDocument->GetDocShell());
  BrowserChild* child = BrowserChild::GetFrom(mSourceDocument->GetDocShell());
  NS_ENSURE_STATE(child);
  nsTArray<mozilla::dom::IPCTransferableData> transferables;
  nsContentUtils::TransferablesToIPCTransferableDatas(
      aArrayTransferables, transferables, false, nullptr);

  nsCOMPtr<nsIPrincipal> principal;
  if (mSourceNode) {
    principal = mSourceNode->NodePrincipal();
  }

  nsCOMPtr<nsIPolicyContainer> policyContainer;
  if (mSourceDocument) {
    policyContainer = mSourceDocument->GetPolicyContainer();
    // XXX why do we need this here? Shouldn't they be set properly in
    // nsBaseDragService already?
    mSourceWindowContext = mSourceDocument->GetWindowContext();
    mSourceTopWindowContext = mSourceWindowContext
                                  ? mSourceWindowContext->TopWindowContext()
                                  : nullptr;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  cookieJarSettings = mSourceDocument->CookieJarSettings();
  mozilla::net::CookieJarSettingsArgs csArgs;
  mozilla::net::CookieJarSettings::Cast(cookieJarSettings)->Serialize(csArgs);

  LayoutDeviceIntRect dragRect;
  if (mHasImage || mSelection) {
    nsPresContext* pc;
    RefPtr<SourceSurface> surface;
    DrawDrag(mSourceNode, aRegion, mScreenPosition, &dragRect, &surface, &pc);

    if (surface) {
      RefPtr<DataSourceSurface> dataSurface = surface->GetDataSurface();
      if (dataSurface) {
        size_t length;
        int32_t stride;
        auto surfaceData =
            nsContentUtils::GetSurfaceData(*dataSurface, &length, &stride);
        if (surfaceData.isNothing()) {
          NS_WARNING("Failed to create shared memory for drag session.");
          return NS_ERROR_FAILURE;
        }

        LOGI("[%p] %s | sending PBrowser::InvokeDragSession with image data",
             this, __FUNCTION__);
        mozilla::Unused << child->SendInvokeDragSession(
            std::move(transferables), aActionType, std::move(surfaceData),
            stride, dataSurface->GetFormat(), dragRect, principal,
            policyContainer, csArgs, mSourceWindowContext,
            mSourceTopWindowContext);
        return NS_OK;
      }
    }
  }

  LOGI("[%p] %s | sending PBrowser::InvokeDragSession without image data", this,
       __FUNCTION__);
  mozilla::Unused << child->SendInvokeDragSession(
      std::move(transferables), aActionType, Nothing(), 0,
      static_cast<SurfaceFormat>(0), dragRect, principal, policyContainer,
      csArgs, mSourceWindowContext, mSourceTopWindowContext);
  return NS_OK;
}

nsIDragSession* nsDragServiceProxy::StartDragSession(
    nsISupports* aWidgetProvider) {
  nsIWidget* widget = GetWidgetFromWidgetProvider(aWidgetProvider);
  NS_ENSURE_TRUE(widget, nullptr);
  BrowserChild* targetBrowser = widget->GetOwningBrowserChild();
  NS_ENSURE_TRUE(targetBrowser, nullptr);
  RefPtr<nsIDragSession> session = targetBrowser->GetDragSession();
  if (session) {
    // session already exists on the browser
    return session;
  }

  session = CreateDragSession();
  MOZ_ASSERT(session);
  static_cast<nsDragSessionProxy*>(session.get())->SetDragTarget(targetBrowser);
  targetBrowser->SetDragSession(session);
  LOGI(
      "[%p] %s | widget: %p | targetBrowser: %p | session: %p | Created drag "
      "session",
      this, __FUNCTION__, widget, targetBrowser, session.get());
  return session;
}

NS_IMETHODIMP
nsDragServiceProxy::GetCurrentSession(nsISupports* aWidgetProvider,
                                      nsIDragSession** aSession) {
  if (!aSession) {
    return NS_ERROR_INVALID_ARG;
  }
  *aSession = nullptr;

  nsIWidget* widget = GetWidgetFromWidgetProvider(aWidgetProvider);
  NS_ENSURE_TRUE(widget, NS_ERROR_INVALID_ARG);
  BrowserChild* browser = widget->GetOwningBrowserChild();
  NS_ENSURE_TRUE(browser, NS_ERROR_INVALID_ARG);
  RefPtr<nsIDragSession> session = browser->GetDragSession();

  if (!mSuppressLevel && session) {
    session.forget(aSession);
  }

  return NS_OK;
}

void nsDragSessionProxy::SetDragTarget(BrowserChild* aTarget) {
  if (!aTarget) {
    if (mTargetBrowser) {
      nsCOMPtr<BrowserChild> targetBC = do_QueryReferent(mTargetBrowser);
      MOZ_ASSERT(targetBC);
      if (targetBC) {
        targetBC->SetDragSession(nullptr);
      }
      mTargetBrowser = nullptr;
    }
    return;
  }
  [[maybe_unused]] RefPtr<nsIDragSession> session = aTarget->GetDragSession();
  MOZ_ASSERT(!session);
  MOZ_ALWAYS_SUCCEEDS(
      aTarget->GetWeakReference(getter_AddRefs(mTargetBrowser)));
}

nsresult nsDragSessionProxy::EndDragSessionImpl(bool aDoneDrag,
                                                uint32_t aKeyModifiers) {
  // End the drag session before removing it from its BrowserChild(s).  This
  // leaves the drag session in place while EndDragSessionImpl sends dragend.
  nsresult rv = nsBaseDragSession::EndDragSessionImpl(aDoneDrag, aKeyModifiers);

  if (mSourceBrowser) {
    nsCOMPtr<BrowserChild> sourceBC = do_QueryReferent(mSourceBrowser);
    MOZ_ASSERT(sourceBC);
    [[maybe_unused]] RefPtr<nsIDragSession> session =
        sourceBC->GetDragSession();
    MOZ_ASSERT(session == this);
    sourceBC->SetDragSession(nullptr);
    mSourceBrowser = nullptr;
  }

  SetDragTarget(nullptr);
  return rv;
}
