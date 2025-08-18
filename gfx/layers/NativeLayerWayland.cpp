/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
  TODO:
  - Better layers update mechanism - update only in changed layes and updated
    properties.
  - Create cache of mapped layers?
  - Fix messages from SurfacePoolWayland() mPendingEntries num xxx
    mPoolSizeLimit 25 Are we leaking pending entries?
  - Implemented screenshotter
  - Presentation feedback
  - Fullscreen - handle differently
  - Attach dmabuf feedback to dmabuf surfaces to get formats for direct scanout
  - Don't use for tooltips/small menus etc.

  Testing:
    Mochitest test speeds
    Fractional Scale
    SW/HW rendering + VSync
*/

#include "mozilla/layers/NativeLayerWayland.h"

#include <dlfcn.h>
#include <utility>
#include <algorithm>

#include "gfxUtils.h"
#include "nsGtkUtils.h"
#include "GLContextProvider.h"
#include "GLBlitHelper.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/RenderDMABUFTextureHost.h"
#include "mozilla/widget/WaylandSurface.h"
#include "mozilla/StaticPrefs_widget.h"

#ifdef MOZ_LOGGING
#  undef LOG
#  undef LOGVERBOSE
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
#  include "Units.h"
extern mozilla::LazyLogModule gWidgetCompositorLog;
#  define LOG(str, ...)                                     \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGVERBOSE(str, ...)                                \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Verbose, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGS(str, ...)                                    \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Debug, \
            (str, ##__VA_ARGS__))
#else
#  define LOG(args)
#endif /* MOZ_LOGGING */

using namespace mozilla;
using namespace mozilla::widget;

namespace mozilla::layers {

using gfx::BackendType;
using gfx::DrawTarget;
using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;
using gfx::Point;
using gfx::Rect;
using gfx::SamplingFilter;
using gfx::Size;

#ifdef MOZ_LOGGING
nsAutoCString NativeLayerRootWayland::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("W[%p]R[%p]", mLoggingWidget, this);
  return tag;
}

nsAutoCString NativeLayerWayland::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("W[%p]R[%p]L[%p]", mRootLayer->GetLoggingWidget(),
                   mRootLayer.get(), this);
  return tag;
}
#endif

/* static */
already_AddRefed<NativeLayerRootWayland> NativeLayerRootWayland::Create(
    RefPtr<WaylandSurface> aWaylandSurface) {
  return MakeAndAddRef<NativeLayerRootWayland>(std::move(aWaylandSurface));
}

void NativeLayerRootWayland::Init() {
  mTmpBuffer = widget::WaylandBufferSHM::Create(LayoutDeviceIntSize(1, 1));

  // Get DRM format for surfaces created by GBM.
  if (!gfx::gfxVars::UseDMABufSurfaceExport()) {
    RefPtr<DMABufFormats> formats = WaylandDisplayGet()->GetDMABufFormats();
    if (formats) {
      mDRMFormat = formats->GetFormat(GBM_FORMAT_ARGB8888,
                                      /* aScanoutFormat */ true);
    }
    if (!mDRMFormat) {
      mDRMFormat = new DRMFormat(GBM_FORMAT_ARGB8888);
    }
  }

  // Unmap all layers if nsWindow is unmapped
  WaylandSurfaceLock lock(mRootSurface);
  mRootSurface->SetUnmapCallbackLocked(
      lock, [this, self = RefPtr{this}]() -> void {
        LOG("NativeLayerRootWayland Unmap callback");
        WaylandSurfaceLock lock(mRootSurface);
        for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
          if (layer->IsMapped()) {
            layer->Unmap();
            layer->MainThreadUnmap();
          }
        }
      });

  mRootSurface->SetGdkCommitCallbackLocked(
      lock, [this, self = RefPtr{this}]() -> void {
        LOGVERBOSE("GdkCommitCallback()");
        // Try to update on main thread if we
        // need it
        UpdateLayersOnMainThread();
      });

  // Propagate frame callback state (enabled/disabled) to all layers
  // to save resources.
  mRootSurface->SetFrameCallbackStateHandlerLocked(
      lock, [this, self = RefPtr{this}](bool aState) -> void {
        LOGVERBOSE("FrameCallbackStateHandler()");
        mRootSurface->AssertCurrentThreadOwnsMutex();
        for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
          layer->SetFrameCallbackState(aState);
        }
      });

  // Get the best DMABuf format for root wl_surface. We use the same
  // for child surfaces as we expect them to share the same window/monitor.
  //
  // Using suboptimal format doesn't cause any functional/visual issue
  // but may lead to worse performance as Wayland compositor may need
  // to convert it for direct scanout.
  //
  // TODO: Recreate (Unmap/Map and Dispose buffers) child surfaces
  // if there's format table refresh.
  //
  // Use on nightly only as it's not implemented yet by compositors
  // to get scanout formats for non-fullscreen surfaces.
#ifdef NIGHTLY_BUILD
  if (!gfx::gfxVars::UseDMABufSurfaceExport() &&
      StaticPrefs::widget_dmabuf_feedback_enabled_AtStartup()) {
    mRootSurface->EnableDMABufFormatsLocked(lock, [this, self = RefPtr{this}](
                                                      DMABufFormats* aFormats) {
      if (DRMFormat* format = aFormats->GetFormat(GBM_FORMAT_ARGB8888,
                                                  /* aScanoutFormat */ true)) {
        LOG("NativeLayerRootWayland DMABuf format refresh: we have scanout "
            "format.");
        mDRMFormat = format;
        return;
      }
      if (DRMFormat* format = aFormats->GetFormat(GBM_FORMAT_ARGB8888,
                                                  /* aScanoutFormat */ false)) {
        LOG("NativeLayerRootWayland DMABuf format refresh: missing scanout "
            "format, use generic one.");
        mDRMFormat = format;
        return;
      }
      LOG("NativeLayerRootWayland DMABuf format refresh: missing DRM "
          "format!");
    });
  }
#endif
}

void NativeLayerRootWayland::Shutdown() {
  LOG("NativeLayerRootWayland::Shutdown()");
  AssertIsOnMainThread();

  UpdateLayersOnMainThread();

  {
    WaylandSurfaceLock lock(mRootSurface);
    if (mRootSurface->IsMapped()) {
      mRootSurface->RemoveAttachedBufferLocked(lock);
    }
    mRootSurface->ClearUnmapCallbackLocked(lock);
    mRootSurface->ClearGdkCommitCallbackLocked(lock);
    mRootSurface->DisableDMABufFormatsLocked(lock);
  }

  mRootSurface = nullptr;
  mTmpBuffer = nullptr;
  mDRMFormat = nullptr;
}

NativeLayerRootWayland::NativeLayerRootWayland(
    RefPtr<WaylandSurface> aWaylandSurface)
    : mRootSurface(aWaylandSurface) {
#ifdef MOZ_LOGGING
  mLoggingWidget = mRootSurface->GetLoggingWidget();
  mRootSurface->SetLoggingWidget(this);
  LOG("NativeLayerRootWayland::NativeLayerRootWayland() nsWindow [%p] mapped "
      "%d",
      mLoggingWidget, mRootSurface->IsMapped());
#endif
  if (!WaylandSurface::IsOpaqueRegionEnabled()) {
    NS_WARNING(
        "Wayland opaque region disabled, expect poor rendering performance!");
  }
}

NativeLayerRootWayland::~NativeLayerRootWayland() {
  LOG("NativeLayerRootWayland::~NativeLayerRootWayland()");
  MOZ_DIAGNOSTIC_ASSERT(
      !mRootSurface,
      "NativeLayerRootWayland destroyed without Shutdown() call!");
}

#ifdef MOZ_LOGGING
void* NativeLayerRootWayland::GetLoggingWidget() const {
  return mLoggingWidget;
}
#endif

// Create layer for rendering to layer/surface so get blank one from
// surface pool.
already_AddRefed<NativeLayer> NativeLayerRootWayland::CreateLayer(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  LOG("NativeLayerRootWayland::CreateLayer() [%d x %d] nsWindow [%p] opaque %d",
      aSize.width, aSize.height, GetLoggingWidget(), aIsOpaque);
  return MakeAndAddRef<NativeLayerWaylandRender>(
      this, aSize, aIsOpaque, aSurfacePoolHandle->AsSurfacePoolHandleWayland());
}

already_AddRefed<NativeLayer>
NativeLayerRootWayland::CreateLayerForExternalTexture(bool aIsOpaque) {
  LOG("NativeLayerRootWayland::CreateLayerForExternalTexture() nsWindow [%p] "
      "opaque %d",
      GetLoggingWidget(), aIsOpaque);
  return MakeAndAddRef<NativeLayerWaylandExternal>(this, aIsOpaque);
}

void NativeLayerRootWayland::AppendLayer(NativeLayer* aLayer) {
  MOZ_CRASH("NativeLayerRootWayland::AppendLayer() not implemented.");
}

void NativeLayerRootWayland::RemoveLayer(NativeLayer* aLayer) {
  MOZ_CRASH("NativeLayerRootWayland::RemoveLayer() not implemented.");
}

bool NativeLayerRootWayland::IsEmptyLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  return mSublayers.IsEmpty();
}

void NativeLayerRootWayland::ClearLayersLocked(
    const widget::WaylandSurfaceLock& aProofOfLock) {
  LOG("NativeLayerRootWayland::ClearLayersLocked() layers num [%d]",
      (int)mRemovedSublayers.Length());
  for (const RefPtr<NativeLayerWayland>& layer : mRemovedSublayers) {
    LOG("  Unmap removed child layer [%p]", layer.get());
    layer->Unmap();
  }
  mMainThreadUpdateSublayers.AppendElements(std::move(mRemovedSublayers));
  RequestUpdateOnMainThreadLocked(aProofOfLock);
}

void NativeLayerRootWayland::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  // Removing all layers can destroy us so hold ref
  RefPtr<NativeLayerRoot> kungfuDeathGrip = this;

  WaylandSurfaceLock lock(mRootSurface);

  // Take shortcut if all layers are removed
  if (aLayers.IsEmpty()) {
    mRemovedSublayers.AppendElements(std::move(mSublayers));
    ClearLayersLocked(lock);
    return;
  }

  nsTArray<RefPtr<NativeLayerWayland>> newLayers(aLayers.Length());
  for (const RefPtr<NativeLayer>& sublayer : aLayers) {
    RefPtr<NativeLayerWayland> layer = sublayer->AsNativeLayerWayland();
    layer->MarkClear();
    newLayers.AppendElement(std::move(layer));
  }

  if (newLayers == mSublayers) {
    return;
  }

  LOG("NativeLayerRootWayland::SetLayers(), old layers num %d new layers num "
      "%d",
      (int)mSublayers.Length(), (int)aLayers.Length());

  // newLayers (aLayers) is a mix of old (already used) and new layers.
  // We need to go through recent layers and remove the ones missing
  // in new layers.
  for (const RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layer->MarkRemoved();
  }
  for (const RefPtr<NativeLayerWayland>& layer : newLayers) {
    layer->MarkAdded();
  }

  for (const RefPtr<NativeLayerWayland>& layer : mSublayers) {
    if (layer->IsRemoved()) {
      LOG("  Unmap removed child layer [%p]", layer.get());
      mRemovedSublayers.AppendElement(layer);
    }
  }

  // Map newly added layers only if root surface itself is mapped.
  // We lock it to make sure root surface stays mapped.
  lock.RequestForceCommit();

  if (mRootSurface->IsMapped()) {
    for (const RefPtr<NativeLayerWayland>& layer : newLayers) {
      if (layer->IsNew()) {
        LOG("  Map new child layer [%p]", layer.get());
        if (!layer->Map(&lock)) {
          continue;
        }
        if (layer->IsOpaque() && WaylandSurface::IsOpaqueRegionEnabled()) {
          LOG("  adding new opaque layer [%p]", layer.get());
          mMainThreadUpdateSublayers.AppendElement(layer);
        }
      }
    }
  }

  mSublayers = std::move(newLayers);
  mRootMutatedStackingOrder = true;

  mRootAllLayersRendered = false;
  mRootSurface->SetCommitStateLocked(lock, mRootAllLayersRendered);

  // We need to process a part of map event on main thread as we use Gdk
  // code there. Ask for the processing now.
  RequestUpdateOnMainThreadLocked(lock);
}

// Update layers on main thread. Missing the main thread update is not critical
// but may lead to worse performance as we tell Gdk to skip compositing opaque
// surfaces.
void NativeLayerRootWayland::UpdateLayersOnMainThread() {
  AssertIsOnMainThread();

  // We're called after Shutdown so do nothing.
  if (!mRootSurface) {
    return;
  }

  LOG("NativeLayerRootWayland::UpdateLayersOnMainThread()");
  WaylandSurfaceLock lock(mRootSurface);
  for (const RefPtr<NativeLayerWayland>& layer : mMainThreadUpdateSublayers) {
    LOGVERBOSE("NativeLayerRootWayland::UpdateLayersOnMainThread() [%p]",
               layer.get());
    layer->UpdateOnMainThread();
  }
  mMainThreadUpdateSublayers.Clear();
  mMainThreadUpdateQueued = false;
}

void NativeLayerRootWayland::RequestUpdateOnMainThreadLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  if (!mMainThreadUpdateSublayers.Length() || mMainThreadUpdateQueued) {
    return;
  }
  mMainThreadUpdateQueued = true;

  LOG("NativeLayerRootWayland::RequestUpdateOnMainThreadLocked()");
  nsCOMPtr<nsIRunnable> updateLayersRunnable = NewRunnableMethod<>(
      "layers::NativeLayerRootWayland::UpdateLayersOnMainThread", this,
      &NativeLayerRootWayland::UpdateLayersOnMainThread);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThreadQueue(
      updateLayersRunnable.forget(), EventQueuePriority::Normal));
}

#ifdef MOZ_LOGGING
void NativeLayerRootWayland::LogStatsLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  if (!MOZ_LOG_TEST(gWidgetCompositorLog, mozilla::LogLevel::Verbose)) {
    return;
  }

  int layersNum = 0;
  int layersMapped = 0;
  int layersMappedOpaque = 0;
  int layersMappedOpaqueSet = 0;
  int layersBufferAttached = 0;
  int layersVisible = 0;
  int layersRendered = 0;
  int layersRenderedLastCycle = 0;

  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layersNum++;
    if (layer->IsMapped()) {
      layersMapped++;
    }
    if (layer->GetWaylandSurface()->HasBufferAttached()) {
      layersBufferAttached++;
    }
    if (layer->IsMapped() && layer->IsOpaque()) {
      layersMappedOpaque++;
      if (layer->GetWaylandSurface()->IsOpaqueSurfaceHandlerSet()) {
        layersMappedOpaqueSet++;
      }
    }
    if (layer->State()->mIsVisible) {
      layersVisible++;
    }
    if (layer->State()->mIsRendered) {
      layersRendered++;
    }
    if (layer->State()->mRenderedLastCycle) {
      layersRenderedLastCycle++;
    }
  }
  LOGVERBOSE(
      "Rendering stats: all rendered [%d] layers [%d] mapped [%d] attached "
      "[%d] visible [%d] "
      "rendered [%d] last [%d] opaque [%d] opaque set [%d] fullscreen [%d]",
      mRootAllLayersRendered, layersNum, layersMapped, layersBufferAttached,
      layersVisible, layersRendered, layersRenderedLastCycle,
      layersMappedOpaque, layersMappedOpaqueSet, mIsFullscreen);
}
#endif

bool NativeLayerRootWayland::CommitToScreen() {
  WaylandSurfaceLock lock(mRootSurface);

  mFrameInProcess = false;

  if (!mRootSurface->IsMapped()) {
    // TODO: Register frame callback to paint again? Are we hidden?
    LOG("NativeLayerRootWayland::CommitToScreen() root surface is not mapped");
    return false;
  }

  LOG("NativeLayerRootWayland::CommitToScreen()");

  // Attach empty tmp buffer to root layer (nsWindow).
  // We need to have any content to attach child layers to it.
  if (!mRootSurface->HasBufferAttached()) {
    mRootSurface->AttachLocked(lock, mTmpBuffer);
    mRootSurface->ClearOpaqueRegionLocked(lock);
  }

  // Try to map all missing surfaces
  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    if (!layer->IsMapped()) {
      if (!layer->Map(&lock)) {
        LOGVERBOSE(
            "NativeLayerRootWayland::CommitToScreen() failed to map layer [%p]",
            layer.get());
        continue;
      }
      if (layer->IsOpaque() && WaylandSurface::IsOpaqueRegionEnabled()) {
        mMainThreadUpdateSublayers.AppendElement(layer);
      }
      mRootMutatedStackingOrder = true;
    }
  }

  if (mRootMutatedStackingOrder) {
    RequestUpdateOnMainThreadLocked(lock);
  }

  const double scale = mRootSurface->GetScaleSafe();
  mRootAllLayersRendered = true;
  for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
    layer->RenderLayer(scale);
    if (layer->State()->mMutatedStackingOrder) {
      mRootMutatedStackingOrder = true;
    }
    if (layer->State()->mIsVisible && !layer->State()->mIsRendered) {
      LOG("NativeLayerRootWayland::CommitToScreen() layer [%p] is not rendered",
          layer.get());
      mRootAllLayersRendered = false;
    }
  }

  if (mRootMutatedStackingOrder) {
    LOGVERBOSE(
        "NativeLayerRootWayland::CommitToScreen(): changed stacking order");
    NativeLayerWayland* previousWaylandSurface = nullptr;
    for (RefPtr<NativeLayerWayland>& layer : mSublayers) {
      if (layer->State()->mIsVisible) {
        MOZ_DIAGNOSTIC_ASSERT(layer->IsMapped());
        if (previousWaylandSurface) {
          layer->PlaceAbove(previousWaylandSurface);
        }
        previousWaylandSurface = layer;
      }
      layer->State()->mMutatedStackingOrder = false;
    }
    mRootMutatedStackingOrder = false;
  }

  LOGVERBOSE("NativeLayerRootWayland::CommitToScreen(): %s root commit",
             mRootAllLayersRendered ? "enabled" : "disabled");
  mRootSurface->SetCommitStateLocked(lock, mRootAllLayersRendered);

#ifdef MOZ_LOGGING
  LogStatsLocked(lock);
#endif

  // Commit all layers changes now so we can unmap removed layers without
  // flickering.
  lock.Commit();

  if (mRootAllLayersRendered && !mRemovedSublayers.IsEmpty()) {
    ClearLayersLocked(lock);
  }

  return true;
}

// Ready-to-paint signal from root or child surfaces. Route it to
// root WaylandSurface (owned by nsWindow) where it's used to fire VSync.
void NativeLayerRootWayland::FrameCallbackHandler(uint32_t aTime) {
  {
    // Child layer wl_subsurface already requested next frame callback
    // and we need to commit to root surface too as we're in
    // wl_subsurface synced mode.
    WaylandSurfaceLock lock(mRootSurface);
  }

  if (aTime <= mLastFrameCallbackTime) {
    LOGVERBOSE(
        "NativeLayerRootWayland::FrameCallbackHandler() ignoring redundant "
        "callback %d",
        aTime);
    return;
  }
  mLastFrameCallbackTime = aTime;

  LOGVERBOSE("NativeLayerRootWayland::FrameCallbackHandler() time %d", aTime);
  mRootSurface->FrameCallbackHandler(nullptr, aTime,
                                     /* aRoutedFromChildSurface */ true);
}

// We don't need to lock access to GdkWindow() as we process all Gdk/Gtk
// events on main thread only.
GdkWindow* NativeLayerRootWayland::GetGdkWindow() const {
  AssertIsOnMainThread();
  return mRootSurface->GetGdkWindow();
}

// Try to match stored wl_buffer with provided DMABufSurface or create
// a new one.
RefPtr<WaylandBuffer> NativeLayerRootWayland::BorrowExternalBuffer(
    RefPtr<DMABufSurface> aDMABufSurface) {
  LOG("NativeLayerRootWayland::BorrowExternalBuffer() WaylandSurface [%p] UID "
      "%d PID %d mExternalBuffers num %d",
      aDMABufSurface.get(), aDMABufSurface->GetUID(), aDMABufSurface->GetPID(),
      (int)mExternalBuffers.Length());

  RefPtr waylandBuffer =
      widget::WaylandBufferDMABUF::CreateExternal(aDMABufSurface);
  for (auto& b : mExternalBuffers) {
    if (b.Matches(aDMABufSurface)) {
      LOG("NativeLayerRootWayland::BorrowExternalBuffer() wl_buffer matches, "
          "recycling");
      waylandBuffer->SetExternalWLBuffer(b.GetWLBuffer());
      return waylandBuffer.forget();
    }
  }

  wl_buffer* wlbuffer = waylandBuffer->CreateWlBuffer();
  if (!wlbuffer) {
    return nullptr;
  }

  LOG("NativeLayerRootWayland::BorrowExternalBuffer() adding new wl_buffer");
  waylandBuffer->SetExternalWLBuffer(wlbuffer);
  mExternalBuffers.EmplaceBack(aDMABufSurface, wlbuffer);
  return waylandBuffer.forget();
}

NativeLayerWayland::NativeLayerWayland(NativeLayerRootWayland* aRootLayer,
                                       const IntSize& aSize, bool aIsOpaque)
    : mRootLayer(aRootLayer), mIsOpaque(aIsOpaque), mSize(aSize) {
  mSurface = new WaylandSurface(mRootLayer->GetRootWaylandSurface(), mSize);
#ifdef MOZ_LOGGING
  mSurface->SetLoggingWidget(this);
#endif
  LOG("NativeLayerWayland::NativeLayerWayland() WaylandSurface [%p] size [%d, "
      "%d] opaque %d",
      mSurface.get(), mSize.width, mSize.height, aIsOpaque);

  mState.mMutatedStackingOrder = true;
  mState.mMutatedPlacement = true;
}

NativeLayerWayland::~NativeLayerWayland() {
  LOG("NativeLayerWayland::~NativeLayerWayland() IsMapped %d",
      mSurface->IsMapped());
  MOZ_RELEASE_ASSERT(!mSurface->IsMapped(), "Releasing mapped surface!");
}

bool NativeLayerWayland::IsMapped() { return mSurface->IsMapped(); }

void NativeLayerWayland::SetSurfaceIsFlipped(bool aIsFlipped) {
  WaylandSurfaceLock lock(mSurface);
  if (aIsFlipped != mSurfaceIsFlipped) {
    mSurfaceIsFlipped = aIsFlipped;
    mState.mMutatedPlacement = true;
  }
}

bool NativeLayerWayland::SurfaceIsFlipped() {
  WaylandSurfaceLock lock(mSurface);
  return mSurfaceIsFlipped;
}

IntSize NativeLayerWayland::GetSize() {
  WaylandSurfaceLock lock(mSurface);
  return mSize;
}

void NativeLayerWayland::SetPosition(const IntPoint& aPosition) {
  WaylandSurfaceLock lock(mSurface);
  if (aPosition != mPosition) {
    LOG("NativeLayerWayland::SetPosition() [%d, %d]", (int)aPosition.x,
        (int)aPosition.y);
    mPosition = aPosition;
    mState.mMutatedPlacement = true;
  }
}

IntPoint NativeLayerWayland::GetPosition() {
  WaylandSurfaceLock lock(mSurface);
  return mPosition;
}

void NativeLayerWayland::PlaceAbove(NativeLayerWayland* aLowerLayer) {
  WaylandSurfaceLock lock(mSurface);
  WaylandSurfaceLock lowerSurfacelock(aLowerLayer->mSurface);

  MOZ_DIAGNOSTIC_ASSERT(IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(aLowerLayer->IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(this != aLowerLayer);

  mSurface->PlaceAboveLocked(lock, lowerSurfacelock);
  mState.mMutatedStackingOrder = true;
}

void NativeLayerWayland::SetTransform(const Matrix4x4& aTransform) {
  WaylandSurfaceLock lock(mSurface);
  MOZ_DIAGNOSTIC_ASSERT(aTransform.IsRectilinear());
  if (aTransform != mTransform) {
    mTransform = aTransform;
    mState.mMutatedPlacement = true;
  }
}

void NativeLayerWayland::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  WaylandSurfaceLock lock(mSurface);
  if (aSamplingFilter != mSamplingFilter) {
    mSamplingFilter = aSamplingFilter;
  }
}

Matrix4x4 NativeLayerWayland::GetTransform() {
  WaylandSurfaceLock lock(mSurface);
  return mTransform;
}

IntRect NativeLayerWayland::GetRect() {
  WaylandSurfaceLock lock(mSurface);
  return IntRect(mPosition, mSize);
}

// TODO: remove lock?
bool NativeLayerWayland::IsOpaque() {
  WaylandSurfaceLock lock(mSurface);
  return mIsOpaque;
}

void NativeLayerWayland::SetClipRect(const Maybe<IntRect>& aClipRect) {
  WaylandSurfaceLock lock(mSurface);
  if (aClipRect != mClipRect) {
#if MOZ_LOGGING
    if (aClipRect) {
      gfx::IntRect rect(aClipRect.value());
      LOG("NativeLayerWaylandRender::SetClipRect() [%d,%d] -> [%d x %d]",
          rect.x, rect.y, rect.width, rect.height);
    }
#endif
    mClipRect = aClipRect;
    mState.mMutatedPlacement = true;
  }
}

Maybe<IntRect> NativeLayerWayland::ClipRect() {
  WaylandSurfaceLock lock(mSurface);
  return mClipRect;
}

void NativeLayerWayland::SetRoundedClipRect(
    const Maybe<gfx::RoundedRect>& aClip) {
  WaylandSurfaceLock lock(mSurface);
  if (aClip != mRoundedClipRect) {
    // TODO(gw): Support rounded clips on wayland
    mRoundedClipRect = aClip;
  }
}

Maybe<gfx::RoundedRect> NativeLayerWayland::RoundedClipRect() {
  WaylandSurfaceLock lock(mSurface);
  return mRoundedClipRect;
}

IntRect NativeLayerWayland::CurrentSurfaceDisplayRect() {
  WaylandSurfaceLock lock(mSurface);
  return mDisplayRect;
}

void NativeLayerWayland::SetScalelocked(
    const widget::WaylandSurfaceLock& aProofOfLock, double aScale) {
  MOZ_DIAGNOSTIC_ASSERT(aScale > 0);
  if (aScale != mScale) {
    mScale = aScale;
    mState.mMutatedPlacement = true;
  }
}

void NativeLayerWayland::UpdateLayerPlacementLocked(
    const widget::WaylandSurfaceLock& aProofOfLock) {
  MOZ_DIAGNOSTIC_ASSERT(IsMapped());

  if (!mState.mMutatedPlacement) {
    return;
  }
  mState.mMutatedPlacement = false;

  LOGVERBOSE("NativeLayerWayland::UpdateLayerPlacementLocked()");

  MOZ_RELEASE_ASSERT(mTransform.Is2D());
  auto transform2D = mTransform.As2D();

  Rect surfaceRectClipped = Rect(0, 0, (float)mSize.width, (float)mSize.height);
  surfaceRectClipped = surfaceRectClipped.Intersect(Rect(mDisplayRect));

  transform2D.PostTranslate((float)mPosition.x, (float)mPosition.y);
  surfaceRectClipped = transform2D.TransformBounds(surfaceRectClipped);

  if (mClipRect) {
    surfaceRectClipped = surfaceRectClipped.Intersect(Rect(mClipRect.value()));
  }

  const bool visible = !surfaceRectClipped.IsEmpty();
  if (mState.mIsVisible != visible) {
    mState.mIsVisible = visible;
    mState.mMutatedVisibility = true;
    mState.mMutatedStackingOrder = true;
    if (!mState.mIsVisible) {
      LOGVERBOSE("NativeLayerWayland become hidden");
      mSurface->RemoveAttachedBufferLocked(aProofOfLock);
      return;
    }
    LOGVERBOSE("NativeLayerWayland become visible");
  }

  mSurface->SetTransformFlippedLocked(aProofOfLock, transform2D._11 < 0.0,
                                      transform2D._22 < 0.0);
  auto unscaledRect =
      gfx::RoundedToInt(surfaceRectClipped / UnknownScaleFactor(mScale));
  mSurface->MoveLocked(aProofOfLock, unscaledRect.TopLeft());
  mSurface->SetViewPortDestLocked(aProofOfLock, unscaledRect.Size());

  auto transform2DInversed = transform2D.Inverse();
  Rect bufferClip = transform2DInversed.TransformBounds(surfaceRectClipped);
  mSurface->SetViewPortSourceRectLocked(
      aProofOfLock,
      bufferClip.Intersect(Rect(0, 0, mSize.width, mSize.height)));
}

void NativeLayerWayland::RenderLayer(double aScale) {
  WaylandSurfaceLock lock(mSurface);

  LOG("NativeLayerWayland::RenderLayer()");

  SetScalelocked(lock, aScale);
  UpdateLayerPlacementLocked(lock);

  mState.mRenderedLastCycle = false;

  // Don't operate over hidden layers
  if (!mState.mIsVisible) {
    LOG("NativeLayerWayland::RenderLayer() quit, not visible");
    return;
  }

  // Return if front buffer didn't changed (or changed area is empty)
  // and there isn't any visibility change.
  if (!IsFrontBufferChanged() && !mState.mMutatedVisibility) {
    LOG("NativeLayerWayland::RenderLayer() quit "
        "IsFrontBufferChanged [%d] "
        "mState.mMutatedVisibility [%d] rendered [%d]",
        IsFrontBufferChanged(), mState.mMutatedVisibility, mState.mIsRendered);
    return;
  }

  if (!mFrontBuffer) {
    LOG("NativeLayerWayland::RenderLayer() - missing front buffer!");
    return;
  }

  mState.mIsRendered = mState.mRenderedLastCycle =
      CommitFrontBufferToScreenLocked(lock);

  mState.mMutatedFrontBuffer = false;
  mState.mMutatedVisibility = false;

  if (mState.mIsVisible) {
    MOZ_DIAGNOSTIC_ASSERT(mSurface->HasBufferAttached());
  }

  LOG("NativeLayerWayland::RenderLayer(): rendered [%d]", mState.mIsRendered);
}

bool NativeLayerWayland::Map(WaylandSurfaceLock* aParentWaylandSurfaceLock) {
  WaylandSurfaceLock surfaceLock(mSurface);

  if (mNeedsMainThreadUpdate == MainThreadUpdate::Unmap) {
    LOG("NativeLayerWayland::Map() waiting to MainThreadUpdate::Unmap");
    return false;
  }

  LOG("NativeLayerWayland::Map() parent %p", mRootLayer.get());

  MOZ_DIAGNOSTIC_ASSERT(!mSurface->IsMapped());
  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate != MainThreadUpdate::Map);

  if (!mSurface->MapLocked(surfaceLock, aParentWaylandSurfaceLock,
                           gfx::IntPoint(0, 0))) {
    gfxCriticalError() << "NativeLayerWayland::Map() failed!";
    return false;
  }
  mSurface->DisableUserInputLocked(surfaceLock);
  mSurface->CreateViewportLocked(surfaceLock,
                                 /* aFollowsSizeChanges */ false);

  // Route frame-to-paint (frame callback) from child layer to root layer
  // where it's passed to Vsync.
  //
  // aTime param is used to identify duplicate events.
  //
  mSurface->SetFrameCallbackLocked(
      surfaceLock,
      [this, self = RefPtr{this}](wl_callback* aCallback,
                                  uint32_t aTime) -> void {
        LOGVERBOSE("NativeLayerWayland::FrameCallbackHandler() time %d", aTime);
        mRootLayer->FrameCallbackHandler(aTime);
      },
      /* aEmulateFrameCallback */ true);

  if (mIsHDR) {
    mSurface->EnableColorManagementLocked(surfaceLock);
  }

  if (auto* external = AsNativeLayerWaylandExternal()) {
    if (RefPtr surface = external->GetSurface()) {
      if (auto* surfaceYUV = surface->GetAsDMABufSurfaceYUV()) {
        mSurface->SetColorRepresentationLocked(surfaceLock,
                                               surfaceYUV->GetYUVColorSpace(),
                                               surfaceYUV->IsFullRange());
      }
    }
  }

  mNeedsMainThreadUpdate = MainThreadUpdate::Map;
  mState.mMutatedStackingOrder = true;
  mState.mMutatedVisibility = true;
  mState.mMutatedPlacement = true;
  mState.mIsRendered = false;
  return true;
}

void NativeLayerWayland::SetFrameCallbackState(bool aState) {
  LOGVERBOSE("NativeLayerWayland::SetFrameCallbackState() %d", aState);
  WaylandSurfaceLock lock(mSurface);
  mSurface->SetFrameCallbackStateLocked(lock, aState);
}

void NativeLayerWayland::MainThreadMap() {
  AssertIsOnMainThread();
  MOZ_DIAGNOSTIC_ASSERT(IsOpaque());
  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate == MainThreadUpdate::Map);

  WaylandSurfaceLock lock(mSurface);
  if (!mSurface->IsOpaqueSurfaceHandlerSet()) {
    // Don't register commit handler, we do it for all surfaces at
    // GdkCommitCallback() handler.
    mSurface->AddOpaqueSurfaceHandlerLocked(lock, mRootLayer->GetGdkWindow(),
                                            /* aRegisterCommitHandler */ false);
    mSurface->SetOpaqueLocked(lock);
    mNeedsMainThreadUpdate = MainThreadUpdate::None;
  }
}

void NativeLayerWayland::Unmap() {
  WaylandSurfaceLock surfaceLock(mSurface);

  if (!mSurface->IsMapped()) {
    return;
  }

  LOG("NativeLayerWayland::Unmap()");

  mSurface->UnmapLocked(surfaceLock);
  // Clear reference to this added at NativeLayerWayland::Map() by
  // callback handler.
  mSurface->ClearFrameCallbackHandlerLocked(surfaceLock);
  mState.mMutatedStackingOrder = true;
  mState.mMutatedVisibility = true;
  mState.mIsRendered = false;
  mState.mIsVisible = false;
  mNeedsMainThreadUpdate = MainThreadUpdate::Unmap;
}

void NativeLayerWayland::MainThreadUnmap() {
  WaylandSurfaceLock lock(mSurface);

  MOZ_DIAGNOSTIC_ASSERT(mNeedsMainThreadUpdate == MainThreadUpdate::Unmap);
  AssertIsOnMainThread();

  if (mSurface->IsPendingGdkCleanup()) {
    mSurface->GdkCleanUpLocked(lock);
    // TODO: Do we need to clear opaque region?
  }
  mNeedsMainThreadUpdate = MainThreadUpdate::None;
}

void NativeLayerWayland::UpdateOnMainThread() {
  AssertIsOnMainThread();
  if (mNeedsMainThreadUpdate == MainThreadUpdate::None) {
    return;
  }
  if (mNeedsMainThreadUpdate == MainThreadUpdate::Map) {
    MainThreadMap();
  } else {
    MainThreadUnmap();
  }
}

void NativeLayerWayland::DiscardBackbuffers() {
  WaylandSurfaceLock lock(mSurface);
  DiscardBackbuffersLocked(lock);
}

void NativeLayerWayland::ForceCommit() {
  WaylandSurfaceLock lock(mSurface);
  if (mSurface->IsMapped()) {
    mSurface->CommitLocked(lock, /* force commit */ true);
  }
}

NativeLayerWaylandRender::NativeLayerWaylandRender(
    NativeLayerRootWayland* aRootLayer, const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandleWayland* aSurfacePoolHandle)
    : NativeLayerWayland(aRootLayer, aSize, aIsOpaque),
      mSurfacePoolHandle(aSurfacePoolHandle) {
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

void NativeLayerWaylandRender::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MOZ_RELEASE_ASSERT(
      false,
      "NativeLayerWaylandRender::AttachExternalImage() not implemented.");
}

bool NativeLayerWaylandRender::IsFrontBufferChanged() {
  return mState.mMutatedFrontBuffer && !mDirtyRegion.IsEmpty();
}

RefPtr<DrawTarget> NativeLayerWaylandRender::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    BackendType aBackendType) {
  LOG("NativeLayerWaylandRender::NextSurfaceAsDrawTarget()");

  WaylandSurfaceLock lock(mSurface);

  if (!mDisplayRect.IsEqualEdges(aDisplayRect)) {
    mDisplayRect = aDisplayRect;
    mState.mMutatedPlacement = true;
  }
  mDirtyRegion = aUpdateRegion;

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsAttached()) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): use front buffer "
        "for rendering");
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): use progress "
        "buffer for rendering");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        mSize, mRootLayer->GetDRMFormat());
    if (mFrontBuffer) {
      LOGVERBOSE(
          "NativeLayerWaylandRender::NextSurfaceAsDrawTarget(): read-back from "
          "front buffer");
      ReadBackFrontBuffer(lock);
      mSurfacePoolHandle->ReturnBufferToPool(mFrontBuffer);
      mFrontBuffer = nullptr;
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return nullptr;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer->IsAttached(),
                        "Reusing attached buffer!");

  return mInProgressBuffer->Lock();
}

Maybe<GLuint> NativeLayerWaylandRender::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  LOG("NativeLayerWaylandRender::NextSurfaceAsFramebuffer()");

  WaylandSurfaceLock lock(mSurface);

  if (!mDisplayRect.IsEqualEdges(aDisplayRect)) {
    mDisplayRect = aDisplayRect;
    mState.mMutatedPlacement = true;
  }
  mDirtyRegion = IntRegion(aUpdateRegion);

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer && !mFrontBuffer->IsAttached()) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): use front "
        "buffer for rendering");
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): use progress "
        "buffer for rendering");
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        mSize, mRootLayer->GetDRMFormat());
  }

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return Nothing();
  }

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer->IsAttached(),
                        "Reusing attached buffer!");

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mSurfacePoolHandle->GetFramebufferForBuffer(
      mInProgressBuffer, aNeedsDepth);
  MOZ_RELEASE_ASSERT(fbo, "GetFramebufferForBuffer failed.");

  if (mFrontBuffer) {
    LOGVERBOSE(
        "NativeLayerWaylandRender::NextSurfaceAsFramebuffer(): read-back from "
        "front buffer");
    ReadBackFrontBuffer(lock);
    mSurfacePoolHandle->ReturnBufferToPool(mFrontBuffer);
    mFrontBuffer = nullptr;
  }

  return fbo;
}

// Front buffer is still used by compositor so we can't paint into it.
// Read it back to progress buffer and paint next frame to progress buffer.
void NativeLayerWaylandRender::ReadBackFrontBuffer(
    const WaylandSurfaceLock& aProofOfLock) {
  IntRegion copyRegion = IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  LOG("NativeLayerWaylandRender::ReadBackFrontBuffer()");

  if (!copyRegion.IsEmpty()) {
    if (mSurfacePoolHandle->gl()) {
      mSurfacePoolHandle->gl()->MakeCurrent();
      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        Maybe<GLuint> sourceFB =
            mSurfacePoolHandle->GetFramebufferForBuffer(mFrontBuffer, false);
        Maybe<GLuint> destFB = mSurfacePoolHandle->GetFramebufferForBuffer(
            mInProgressBuffer, false);
        MOZ_RELEASE_ASSERT(sourceFB && destFB);
        mSurfacePoolHandle->gl()->BlitHelper()->BlitFramebufferToFramebuffer(
            sourceFB.value(), destFB.value(), r, r, LOCAL_GL_NEAREST);
      }
    } else {
      RefPtr<gfx::DataSourceSurface> dataSourceSurface =
          gfx::CreateDataSourceSurfaceFromData(
              mSize, mFrontBuffer->GetSurfaceFormat(),
              (const uint8_t*)mFrontBuffer->GetImageData(),
              mSize.width * BytesPerPixel(mFrontBuffer->GetSurfaceFormat()));
      RefPtr<DrawTarget> dt = mInProgressBuffer->Lock();

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        IntRect r = iter.Get();
        dt->CopySurface(dataSourceSurface, r, IntPoint(r.x, r.y));
      }
    }
  }
}

bool NativeLayerWaylandRender::CommitFrontBufferToScreenLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  // Don't operate over hidden layers
  LOG("NativeLayerWaylandRender::CommitFrontBufferToScreenLocked()");

  if (mState.mMutatedVisibility) {
    mSurface->InvalidateLocked(aProofOfLock);
  } else {
    mSurface->InvalidateRegionLocked(aProofOfLock, mDirtyRegion);
  }
  mDirtyRegion.SetEmpty();

  auto* buffer = mFrontBuffer->AsWaylandBufferDMABUF();
  if (buffer) {
    buffer->GetSurface()->FenceWait();
  }

  mSurface->AttachLocked(aProofOfLock, mFrontBuffer);
  return true;
}

void NativeLayerWaylandRender::NotifySurfaceReady() {
  LOG("NativeLayerWaylandRender::NotifySurfaceReady()");

  WaylandSurfaceLock lock(mSurface);

  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mInProgressBuffer);

  mFrontBuffer = std::move(mInProgressBuffer);
  if (mSurfacePoolHandle->gl()) {
    auto* buffer = mFrontBuffer->AsWaylandBufferDMABUF();
    if (buffer) {
      buffer->GetSurface()->FenceSet();
    }
    mSurfacePoolHandle->gl()->FlushIfHeavyGLCallsSinceLastFlush();
  }

  mState.mMutatedFrontBuffer = true;
}

void NativeLayerWaylandRender::DiscardBackbuffersLocked(
    const WaylandSurfaceLock& aProofOfLock, bool aForce) {
  LOGVERBOSE(
      "NativeLayerWaylandRender::DiscardBackbuffersLocked() force %d progress "
      "%p front %p",
      aForce, mInProgressBuffer.get(), mFrontBuffer.get());
  if (mInProgressBuffer && (!mInProgressBuffer->IsAttached() || aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(mInProgressBuffer);
    mInProgressBuffer = nullptr;
  }
  if (mFrontBuffer && (!mFrontBuffer->IsAttached() || aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(mFrontBuffer);
    mFrontBuffer = nullptr;
  }
}

NativeLayerWaylandRender::~NativeLayerWaylandRender() {
  LOG("NativeLayerWaylandRender::~NativeLayerWaylandRender()");
  WaylandSurfaceLock lock(mSurface);
  DiscardBackbuffersLocked(lock, /* aForce */ true);
}

RefPtr<DMABufSurface> NativeLayerWaylandExternal::GetSurface() {
  return mTextureHost ? mTextureHost->GetSurface() : nullptr;
}

NativeLayerWaylandExternal::NativeLayerWaylandExternal(
    NativeLayerRootWayland* aRootLayer, bool aIsOpaque)
    : NativeLayerWayland(aRootLayer, IntSize(), aIsOpaque) {}

void NativeLayerWaylandExternal::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  WaylandSurfaceLock lock(mSurface);

  wr::RenderDMABUFTextureHost* texture =
      aExternalImage->AsRenderDMABUFTextureHost();
  MOZ_DIAGNOSTIC_ASSERT(texture);
  if (!texture) {
    LOG("NativeLayerWayland::AttachExternalImage() failed.");
    gfxCriticalNoteOnce << "ExternalImage is not RenderDMABUFTextureHost";
    return;
  }

  if (mSize != texture->GetSize(0)) {
    mSize = texture->GetSize(0);
    mDisplayRect = IntRect(IntPoint{}, mSize);
    mState.mMutatedPlacement = true;
  }

  mState.mMutatedFrontBuffer =
      (!mTextureHost || mTextureHost->GetSurface() != texture->GetSurface());
  if (!mState.mMutatedFrontBuffer) {
    return;
  }
  mTextureHost = texture;

  auto surface = mTextureHost->GetSurface();
  mIsHDR = surface->IsHDRSurface();

  LOG("NativeLayerWaylandExternal::AttachExternalImage() host [%p] "
      "DMABufSurface [%p] DMABuf UID %d [%d x %d] HDR %d Opaque %d recycle "
      "%d",
      mTextureHost.get(), mTextureHost->GetSurface().get(),
      mTextureHost->GetSurface()->GetUID(), mSize.width, mSize.height, mIsHDR,
      mIsOpaque, surface->CanRecycle());

  mFrontBuffer = surface->CanRecycle()
                     ? mRootLayer->BorrowExternalBuffer(surface)
                     : widget::WaylandBufferDMABUF::CreateExternal(surface);
}

void NativeLayerWaylandExternal::DiscardBackbuffersLocked(
    const WaylandSurfaceLock& aProofOfLock, bool aForce) {
  LOG("NativeLayerWaylandRender::DiscardBackbuffersLocked()");

  // Buffers attached to compositor are still tracked by WaylandSurface
  // so we can release reference here.
  mTextureHost = nullptr;
  mFrontBuffer = nullptr;
}

RefPtr<DrawTarget> NativeLayerWaylandExternal::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    BackendType aBackendType) {
  MOZ_RELEASE_ASSERT(
      false,
      "NativeLayerWaylandExternal::NextSurfaceAsDrawTarget() not implemented!");
  return nullptr;
}

Maybe<GLuint> NativeLayerWaylandExternal::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MOZ_RELEASE_ASSERT(false,
                     "NativeLayerWaylandExternal::NextSurfaceAsFramebuffer() "
                     "not implemented!");
  return Nothing();
}

bool NativeLayerWaylandExternal::IsFrontBufferChanged() {
  return mState.mMutatedFrontBuffer;
}

bool NativeLayerWaylandExternal::CommitFrontBufferToScreenLocked(
    const WaylandSurfaceLock& aProofOfLock) {
  LOG("NativeLayerWaylandExternal::CommitFrontBufferToScreenLocked()");
  mSurface->InvalidateLocked(aProofOfLock);
  mSurface->AttachLocked(aProofOfLock, mFrontBuffer);
  return true;
}

NativeLayerWaylandExternal::~NativeLayerWaylandExternal() {
  LOG("NativeLayerWaylandExternal::~NativeLayerWaylandExternal()");
}

}  // namespace mozilla::layers
