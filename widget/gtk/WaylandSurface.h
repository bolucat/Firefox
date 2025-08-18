/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __MOZ_WAYLAND_SURFACE_H__
#define __MOZ_WAYLAND_SURFACE_H__

#include "nsWaylandDisplay.h"
#include "mozilla/Mutex.h"
#include "mozilla/Atomics.h"
#include "WaylandSurfaceLock.h"
#include "mozilla/GRefPtr.h"

/* Workaround for bug at wayland-util.h,
 * present in wayland-devel < 1.12
 */
struct wl_surface;
struct wl_subsurface;
struct wl_egl_window;

class MessageLoop;

namespace mozilla::widget {

class WaylandBuffer;
class BufferTransaction;

// WaylandSurface is a wrapper for Wayland rendering target
// which is wl_surface / wl_subsurface.
class WaylandSurface final {
  friend WaylandSurfaceLock;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WaylandSurface);

  WaylandSurface(RefPtr<WaylandSurface> aParent, gfx::IntSize aSize);

#ifdef MOZ_LOGGING
  nsAutoCString GetDebugTag() const;
  void* GetLoggingWidget() const { return mLoggingWidget; };
  void SetLoggingWidget(void* aWidget) { mLoggingWidget = aWidget; }
#endif

  void ReadyToDrawFrameCallbackHandler(struct wl_callback* aCallback);
  void AddOrFireReadyToDrawCallback(const std::function<void(void)>& aDrawCB);
  void ClearReadyToDrawCallbacks();

  void FrameCallbackHandler(struct wl_callback* aCallback, uint32_t aTime,
                            bool aRoutedFromChildSurface);

  // Run frame callback repeatedly. Callback is removed on Unmap.
  // If aEmulateFrameCallback is set to true and WaylandSurface is mapped and
  // ready to draw and we don't have buffer attached yet,
  // fire aFrameCallbackHandler without frame callback from
  // compositor in sFrameCheckTimeoutMs.
  void SetFrameCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(wl_callback*, uint32_t)>& aFrameCallbackHandler,
      bool aEmulateFrameCallback = false);

  // Clears frame callback handler. It's used if frame callback handler
  // contains strong reference to WaylandSurface class owner
  // which we want to clear.
  void ClearFrameCallbackHandlerLocked(const WaylandSurfaceLock& aProofOfLock);

  // Enable/Disable any frame callback emission (includes emulated ones).
  void SetFrameCallbackStateLocked(const WaylandSurfaceLock& aProofOfLock,
                                   bool aEnabled);
  void SetFrameCallbackStateHandlerLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(bool)>& aFrameCallbackStateHandler);

  // Create and resize EGL window.
  // GetEGLWindow() takes unscaled window size as we derive size from GdkWindow.
  // It's scaled internally by WaylandSurface fractional scale.
  wl_egl_window* GetEGLWindow(nsIntSize aUnscaledSize);
  // SetEGLWindowSize() takes scaled size - it's called from rendering code
  // which uses scaled sizes.
  bool SetEGLWindowSize(nsIntSize aScaledSize);
  bool HasEGLWindow() const { return !!mEGLWindow; }

  // Read to draw means we got frame callback from parent surface
  // where we attached to.
  bool IsReadyToDraw() const { return mIsReadyToDraw; }
  // Mapped means we have all internals created.
  bool IsMapped() const { return mIsMapped; }
  // Indicate that Wayland surface uses Gdk resources which
  // need to be released on main thread by GdkCleanUpLocked().
  // It may be called after Unmap() to make sure
  // Gtk resources are not allocated again.
  bool IsPendingGdkCleanup() const { return mIsPendingGdkCleanup; }

  bool IsOpaqueSurfaceHandlerSet() const { return mIsOpaqueSurfaceHandlerSet; }

  bool HasBufferAttached() const { return mBufferAttached; }

  // Mapped as direct surface of MozContainer
  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 wl_surface* aParentWLSurface,
                 gfx::IntPoint aSubsurfacePosition);
  // Mapped as child of WaylandSurface (used by layers)
  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 WaylandSurfaceLock* aParentWaylandSurfaceLock,
                 gfx::IntPoint aSubsurfacePosition);
  // Unmap surface which hides it
  void UnmapLocked(WaylandSurfaceLock& aSurfaceLock);

  // Clean up Gdk resources, on main thread only
  void GdkCleanUpLocked(const WaylandSurfaceLock& aProofOfLock);

  // Allow to register and run unmap callback.
  // Unmap callback needs to be called *before* UnmapLocked() call
  // on main thread.
  void SetUnmapCallbackLocked(const WaylandSurfaceLock& aProofOfLock,
                              const std::function<void(void)>& aUnmapCB);
  void ClearUnmapCallbackLocked(const WaylandSurfaceLock& aProofOfLock);

  void RunUnmapCallback();

  // Create Viewport to manage surface transformations.
  // aFollowsSizeChanges if set, Viewport destination size
  // is updated according to buffer size.
  bool CreateViewportLocked(const WaylandSurfaceLock& aProofOfLock,
                            bool aFollowsSizeChanges);

  void AddReadyToDrawCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(void)>& aInitialDrawCB);

  // Attach WaylandBuffer which shows WaylandBuffer content
  // on screen.
  bool AttachLocked(const WaylandSurfaceLock& aSurfaceLock,
                    RefPtr<WaylandBuffer> aBuffer);
  bool IsBufferAttached(WaylandBuffer* aBuffer);

  // If there's any WaylandBuffer recently attached, detach it.
  // It makes the WaylandSurface invisible and it doesn't have any
  // content.
  void RemoveAttachedBufferLocked(const WaylandSurfaceLock& aProofOfLock);

  // Remove deleted transaction from WaylandSurface, it may release
  // referenced WaylandBuffer.
  void RemoveTransactionLocked(const WaylandSurfaceLock& aSurfaceLock,
                               RefPtr<BufferTransaction> aTransaction);

  // CommitLocked() is needed to call after some of *Locked() method
  // to submit the action to Wayland compositor by wl_surface_commit().

  // It's possible to stack more *Locked() methods
  // together and do commit after the last one to do the changes in atomic way.

  // Need of commit is tracked by mSurfaceNeedsCommit flag and
  // if it's set, CommitLocked() is called when WaylandSurfaceLock is destroyed
  // and WaylandSurface is unlocked.
  void CommitLocked(const WaylandSurfaceLock& aProofOfLock,
                    bool aForceCommit = false, bool aForceDisplayFlush = false);

  void EnableDMABufFormatsLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(DMABufFormats*)>& aFormatRefreshCB);
  void DisableDMABufFormatsLocked(const WaylandSurfaceLock& aProofOfLock);

  // Place this WaylandSurface above aLowerSurface
  void PlaceAboveLocked(const WaylandSurfaceLock& aProofOfLock,
                        WaylandSurfaceLock& aLowerSurfaceLock);
  void MoveLocked(const WaylandSurfaceLock& aProofOfLock,
                  gfx::IntPoint aPosition);
  void SetViewPortSourceRectLocked(const WaylandSurfaceLock& aProofOfLock,
                                   gfx::Rect aRect);
  void SetViewPortDestLocked(const WaylandSurfaceLock& aProofOfLock,
                             gfx::IntSize aDestSize);
  void SetTransformFlippedLocked(const WaylandSurfaceLock& aProofOfLock,
                                 bool aFlippedX, bool aFlippedY);

  void SetOpaqueRegion(const gfx::IntRegion& aRegion);
  void SetOpaqueRegionLocked(const WaylandSurfaceLock& aProofOfLock,
                             const gfx::IntRegion& aRegion);
  void SetOpaqueLocked(const WaylandSurfaceLock& aProofOfLock);
  void ClearOpaqueRegionLocked(const WaylandSurfaceLock& aProofOfLock);

  bool DisableUserInputLocked(const WaylandSurfaceLock& aProofOfLock);
  void InvalidateRegionLocked(const WaylandSurfaceLock& aProofOfLock,
                              const gfx::IntRegion& aInvalidRegion);
  void InvalidateLocked(const WaylandSurfaceLock& aProofOfLock);

  bool EnableFractionalScaleLocked(
      const WaylandSurfaceLock& aProofOfLock,
      std::function<void(void)> aFractionalScaleCallback, bool aManageViewport);
  bool EnableCeiledScaleLocked(const WaylandSurfaceLock& aProofOfLock);

  bool IsFractionalScaleLocked(const WaylandSurfaceLock& aProofOfLock) const {
    return mScaleType == ScaleType::Disabled;
  }
  bool IsCeiledScaleLocked(const WaylandSurfaceLock& aProofOfLock) const {
    return mScaleType == ScaleType::Ceiled;
  }
  bool IsScaleEnabledLocked(const WaylandSurfaceLock& aProofOfLock) const {
    return mScaleType != ScaleType::Disabled;
  }

  // Returns scale as float point number. If WaylandSurface is not mapped,
  // return fractional scale of parent surface.
  // Returns sNoScale is we can't get it.
  static constexpr const double sNoScale = -1;
  double GetScale();

  // The same as GetScale() but returns monitor scale if window scale is
  // missing.
  double GetScaleSafe();

  // Called when screen ceiled scale changed or set initial scale before we map
  // and paint the surface.
  void SetCeiledScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                            int aScreenCeiledScale);

  // Called by wayland compositor when fractional scale is changed.
  static void FractionalScaleHandler(void* data,
                                     struct wp_fractional_scale_v1* info,
                                     uint32_t wire_scale);

  static void AfterPaintHandler(GdkFrameClock* aClock, void* aData);

  // See https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/3111 why we use it.
  // If child surface covers whole area of parent surface and it's opaque,
  // parent surface will not get any events (frame callbacks) from compositor
  // as it's considered as invisible.
  //
  // Firefox uses the parent wl_surface (owned by GdkWindow) to get input
  // events. Without gdk_wayland_window_add_frame_callback_surface() call,
  // Gdk is not getting any events from compostor and we're frozen.
  //
  // So gdk_wayland_window_add_frame_callback_surface() registers wl_surface
  // owned by WaylandSurface to GtkWindow and requests frame callback
  // for it. Such frame callback is then routed to GdkWindow and it's used
  // to fire events like native GdkWindow ones.
  //
  // To make sure WaylandSurface's wl_surface frame callback is generated,
  // we need to commit the wl_surface regularly as Gdk registers frame callback
  // for it at on_frame_clock_after_paint() event of GdkWindow.
  bool AddOpaqueSurfaceHandlerLocked(const WaylandSurfaceLock& aProofOfLock,
                                     GdkWindow* aGdkWindow,
                                     bool aRegisterCommitHandler);
  bool RemoveOpaqueSurfaceHandlerLocked(const WaylandSurfaceLock& aProofOfLock);

  // Additional callback to call from on_frame_clock_after_paint()
  // and before this wl_surface is commited.
  // It can be used to update subsurfaces from main thread.
  void SetGdkCommitCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(void)>& aGdkCommitCB);
  void ClearGdkCommitCallbackLocked(const WaylandSurfaceLock& aProofOfLock);

  RefPtr<DMABufFormats> GetDMABufFormats() const { return mFormats; }

  GdkWindow* GetGdkWindow() const;

  static bool IsOpaqueRegionEnabled();

  void SetParentLocked(const WaylandSurfaceLock& aProofOfLock,
                       RefPtr<WaylandSurface> aParent);

  bool EnableColorManagementLocked(const WaylandSurfaceLock& aProofOfLock);
  void SetColorRepresentationLocked(const WaylandSurfaceLock& aProofOfLock,
                                    mozilla::gfx::YUVColorSpace aColorSpace,
                                    bool aFullRange);

  static void ImageDescriptionFailed(
      void* aData, struct wp_image_description_v1* aImageDescription,
      uint32_t aCause, const char* aMsg);
  static void ImageDescriptionReady(
      void* aData, struct wp_image_description_v1* aImageDescription,
      uint32_t aIdentity);

  void AssertCurrentThreadOwnsMutex();

  void ForceCommit() { mSurfaceNeedsCommit = true; }
  void SetCommitStateLocked(const WaylandSurfaceLock& aProofOfLock,
                            bool aCommitAllowed) {
    mCommitAllowed = aCommitAllowed;
  }

 private:
  ~WaylandSurface();

  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 wl_surface* aParentWLSurface,
                 WaylandSurfaceLock* aParentWaylandSurfaceLock,
                 gfx::IntPoint aSubsurfacePosition, bool aSubsurfaceDesync,
                 bool aUseReadyToDrawCallback = true);

  void SetSizeLocked(const WaylandSurfaceLock& aProofOfLock,
                     gfx::IntSize aSizeScaled, gfx::IntSize aUnscaledSize);

  wl_surface* Lock(WaylandSurfaceLock* aWaylandSurfaceLock);
  void Unlock(struct wl_surface** aSurface,
              WaylandSurfaceLock* aWaylandSurfaceLock);
  void Commit(WaylandSurfaceLock* aProofOfLock, bool aForceCommit,
              bool aForceDisplayFlush);

  BufferTransaction* GetNextTransactionLocked(
      const WaylandSurfaceLock& aSurfaceLock, WaylandBuffer* aBuffer);
  // Force release/detele all transactions and wl_buffers attached to them.
  void ReleaseAllWaylandTransactionsLocked(WaylandSurfaceLock& aSurfaceLock);

  void RequestFrameCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  void ClearFrameCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  bool HasEmulatedFrameCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock) const;

  void ClearReadyToDrawCallbacksLocked(const WaylandSurfaceLock& aProofOfLock);

  void ClearScaleLocked(const WaylandSurfaceLock& aProofOfLock);

  // Weak ref to owning widget (nsWindow or NativeLayerWayland),
  // used for diagnostics/logging only.
  void* mLoggingWidget = nullptr;

  // WaylandSurface mapped - we have valid wl_surface where we can paint to.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsMapped{false};

  // Wayland shows only subsurfaces of visible parent surfaces.
  // mIsReadyToDraw means our parent wl_surface has content so
  // this WaylandSurface can be visible on screen and get get frame callback.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsReadyToDraw{false};

  // We used Gdk functions which needs clean up in main thread.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsPendingGdkCleanup{false};

  std::function<void(void)> mGdkCommitCallback;
  std::function<void(void)> mUnmapCallback;

  // Scaled surface size, ceiled or fractional.
  // This reflects real surface size which we paint.
  gfx::IntSize mSizeScaled;

  // Parent GdkWindow where we paint to, directly or via subsurface.
  RefPtr<GdkWindow> mGdkWindow;

  // Parent wl_surface owned by mGdkWindow. It's used when we're attached
  // directly to MozContainer.
  wl_surface* mParentSurface = nullptr;

  // Parent WaylandSurface.
  //
  // Layer rendering (compositor) uses mSurface directly attached to
  // wl_surface owned by mParent.
  //
  // For non-compositing rendering (old) mParent is WaylandSurface
  // owned by parent nsWindow.
  RefPtr<WaylandSurface> mParent;

  // wl_surface setup/states
  wl_surface* mSurface = nullptr;
  mozilla::Atomic<bool, mozilla::Relaxed> mSurfaceNeedsCommit{false};
  bool mCommitAllowed = true;

  // When subsurface is desynced, we need to commit to parent surface
  // to see the change in subsurface (this one).
  // In such case we set mSurfaceNeedsCommit to parent for it.
  bool mSubsurfaceDesync = true;

  wl_subsurface* mSubsurface = nullptr;
  gfx::IntPoint mSubsurfacePosition{-1, -1};

  // Wayland buffers recently attached to this surface or held by
  // Wayland compositor.
  // There may be more than one buffer attached, for instance if
  // previous buffer is hold by compositor. We need to keep
  // there buffers live until compositor notify us that we
  // can release them.
  AutoTArray<RefPtr<BufferTransaction>, 3> mBufferTransactions;
  uintptr_t mLatestAttachedBuffer = 0;

  // Indicates mSurface has buffer attached so we can attach subsurface
  // to it and expect to get frame callbacks from Wayland compositor.
  // We set it at AttachLocked() or when we get first frame callback
  // (when EGL is used).
  mozilla::Atomic<bool, mozilla::Relaxed> mBufferAttached{false};

  mozilla::Atomic<wl_egl_window*, mozilla::Relaxed> mEGLWindow{nullptr};

  bool mViewportFollowsSizeChanges = true;
  wp_viewport* mViewport = nullptr;
  gfx::Rect mViewportSourceRect{-1, -1, -1, -1};
  gfx::IntSize mViewportDestinationSize{-1, -1};

  // Surface flip state on X/Y asix
  bool mBufferTransformFlippedX = false;
  bool mBufferTransformFlippedY = false;

  // Frame callback registered to parent surface. When we get it we know
  // parent surface is ready and we can paint.
  wl_callback* mReadyToDrawFrameCallback = nullptr;
  std::vector<std::function<void(void)>> mReadyToDrawCallbacks;

  // Frame callbacks of this surface
  wl_callback* mFrameCallback = nullptr;

  struct FrameCallback {
    std::function<void(wl_callback*, uint32_t)> mCb = nullptr;
    bool mEmulated = false;
    bool IsSet() const { return !!mCb; }
  };

  bool mFrameCallbackEnabled = true;
  std::function<void(bool)> mFrameCallbackStateHandler = nullptr;

  // Frame callback handler called every frame
  FrameCallback mFrameCallbackHandler;

  // WaylandSurface is used from Compositor/Rendering/Main threads.
  mozilla::Mutex mMutex{"WaylandSurface"};
  WaylandSurfaceLock* mSurfaceLock = nullptr;

  // We may mark part of mSurface as opaque (non-transparent) if it's supported
  // by Gtk which allows compositor to skip painting of covered parts.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsOpaqueSurfaceHandlerSet{false};
  gulong mGdkAfterPaintId = 0;
  static bool sIsOpaqueRegionEnabled;
  static void (*sGdkWaylandWindowAddCallbackSurface)(GdkWindow*,
                                                     struct wl_surface*);
  static void (*sGdkWaylandWindowRemoveCallbackSurface)(GdkWindow*,
                                                        struct wl_surface*);
  guint mEmulatedFrameCallbackTimerID = 0;
  constexpr static int sEmulatedFrameCallbackTimeoutMs = (int)(1000.0 / 60.0);

  // We use two scale systems in Firefox/Wayland. Ceiled (integer) scale and
  // fractional scale. Ceiled scale is easy to implement but comes with
  // rendering overhead while fractional rendering paints buffers with exact
  // scale.
  //
  // Fractional scale is used as rendering optimization.
  // For instance if 225% scale is used, ceiled scale is 3
  // and fractional 2.20.
  //
  // If we paint content with ceiled scale 3 and desktop uses scale 225%,
  // Wayland compositor downscales buffer to 2.20 on rendering
  // but we paint more pixels than neccessary (so we use name ceiled).
  //
  // Scale is used by wp_viewport. If a surface has a surface-local size
  // of 100 px by 50 px and wishes to submit buffers with a scale of 1.5,
  // then a buffer of 150px by 75 px should be used and the wp_viewport
  // destination rectangle should be 100 px by 50 px.
  // The wl_surface buffer scale should remain set to 1.
  //
  // For scale 2 (200%) we use surface size 200 x 100 px and set
  // viewport size to 100 x 50 px.
  //
  // We're getting fractional scale number with a small delay from
  // wp_fractional_scale_v1 after fist commit to surface.
  // Meanwhile we can use ceiled scale number instead of fractional one or
  // get fractional scale from parent window (if there's any).
  //
  enum class ScaleType {
    Disabled,
    Ceiled,
    Fractional,
  };

  ScaleType mScaleType = ScaleType::Disabled;

  // mScreenScale is set from main thread only but read from
  // different threads.
  mozilla::Atomic<double, mozilla::Relaxed> mScreenScale{sNoScale};

  wp_fractional_scale_v1* mFractionalScaleListener = nullptr;

  // mFractionalScaleCallback is called from
  // wp_fractional_scale_v1_add_listener when scale is changed.
  std::function<void(void)> mFractionalScaleCallback = []() {};

  bool mUseDMABufFormats = false;
  // Wayland display notifies us when available DRM formats are are changed.
  // For instance if wl_surface becomes fullscreen we may get DRM formats
  // for direct scanout.
  std::function<void(DMABufFormats*)> mDMABufFormatRefreshCallback;
  RefPtr<DMABufFormats> mFormats;

  // HDR support
  bool mHDRSet = false;
  wp_color_management_surface_v1* mColorSurface = nullptr;
  wp_color_representation_surface_v1* mColorRepresentationSurface = nullptr;
  wp_image_description_v1* mImageDescription = nullptr;
};

}  // namespace mozilla::widget

#endif /* __MOZ_WAYLAND_SURFACE_H__ */
