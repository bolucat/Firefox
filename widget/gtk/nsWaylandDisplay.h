/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __MOZ_WAYLAND_DISPLAY_H__
#define __MOZ_WAYLAND_DISPLAY_H__

#include "DMABufDevice.h"

#include "mozilla/widget/mozwayland.h"
#include "mozilla/widget/fractional-scale-v1-client-protocol.h"
#include "mozilla/widget/idle-inhibit-unstable-v1-client-protocol.h"
#include "mozilla/widget/kde-appmenu-client-protocol.h"
#include "mozilla/widget/linux-dmabuf-unstable-v1-client-protocol.h"
#include "mozilla/widget/pointer-constraints-unstable-v1-client-protocol.h"
#include "mozilla/widget/pointer-gestures-unstable-v1-client-protocol.h"
#include "mozilla/widget/relative-pointer-unstable-v1-client-protocol.h"
#include "mozilla/widget/viewporter-client-protocol.h"
#include "mozilla/widget/xdg-activation-v1-client-protocol.h"
#include "mozilla/widget/xdg-output-unstable-v1-client-protocol.h"
#include "mozilla/widget/color-management-v1-client-protocol.h"
#include "mozilla/widget/color-representation-v1-client-protocol.h"
#include "mozilla/widget/xdg-shell-client-protocol.h"
#include "mozilla/widget/xx-pip-v1-client-protocol.h"

#include <gbm.h>

using GdkMonitor = struct _GdkMonitor;

namespace mozilla::widget {

constexpr const int sColorTransfersNum =
    WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG + 1;
constexpr const int sColorPrimariesNum =
    WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB + 1;

class DMABufFormats;

// Our general connection to Wayland display server,
// holds our display connection and runs event loop.
// We have a global nsWaylandDisplay object for each thread.
class nsWaylandDisplay {
 public:
  // Create nsWaylandDisplay object on top of native Wayland wl_display
  // connection.
  explicit nsWaylandDisplay(wl_display* aDisplay);

  static uint32_t GetLastEventSerial();
  wl_display* GetDisplay() { return mDisplay; };
  wl_compositor* GetCompositor() { return mCompositor; };
  wl_subcompositor* GetSubcompositor() { return mSubcompositor; };
  wl_shm* GetShm() { return mShm; };
  zwp_idle_inhibit_manager_v1* GetIdleInhibitManager() {
    return mIdleInhibitManager;
  }
  wp_viewporter* GetViewporter() { return mViewporter; };
  zwp_relative_pointer_manager_v1* GetRelativePointerManager() {
    return mRelativePointerManager;
  }
  zwp_pointer_constraints_v1* GetPointerConstraints() {
    return mPointerConstraints;
  }
  zwp_linux_dmabuf_v1* GetDmabuf() { return mDmabuf; };
  xdg_activation_v1* GetXdgActivation() { return mXdgActivation; };
  org_kde_kwin_appmenu_manager* GetAppMenuManager() { return mAppMenuManager; }
  wp_fractional_scale_manager_v1* GetFractionalScaleManager() {
    return mFractionalScaleManager;
  }
  bool IsPrimarySelectionEnabled() { return mIsPrimarySelectionEnabled; }

  wl_pointer* GetPointer() { return mPointer; }
  void SetPointer(wl_pointer* aPointer);
  void RemovePointer();

  void SetShm(wl_shm* aShm);

  void SetKeyboard(wl_keyboard* aKeyboard);
  wl_keyboard* GetKeyboard() { return mKeyboard; }
  void ClearKeyboard();

  void SetSeat(wl_seat* aSeat, int aSeatId);
  wl_seat* GetSeat() { return mSeat; }
  void RemoveSeat(int aSeatId);

  void SetCompositor(wl_compositor* aCompositor);
  void SetSubcompositor(wl_subcompositor* aSubcompositor);
  void SetDataDeviceManager(wl_data_device_manager* aDataDeviceManager);
  void SetIdleInhibitManager(zwp_idle_inhibit_manager_v1* aIdleInhibitManager);
  void SetViewporter(wp_viewporter* aViewporter);
  void SetRelativePointerManager(
      zwp_relative_pointer_manager_v1* aRelativePointerManager);
  void SetPointerConstraints(zwp_pointer_constraints_v1* aPointerConstraints);
  void SetPointerGestures(zwp_pointer_gestures_v1* aPointerGestures);
  void SetDmabuf(zwp_linux_dmabuf_v1* aDmabufFeedback, int aVersion);
  void SetXdgActivation(xdg_activation_v1* aXdgActivation);
  void SetAppMenuManager(org_kde_kwin_appmenu_manager* appMenuManager);
  void SetFractionalScaleManager(wp_fractional_scale_manager_v1* aManager) {
    mFractionalScaleManager = aManager;
  }
  void EnablePrimarySelection() { mIsPrimarySelectionEnabled = true; }

  void SetColorManager(wp_color_manager_v1* aColorManager);
  wp_color_manager_v1* GetColorManager() const { return mColorManager; }
  void SetColorRepresentationManager(
      wp_color_representation_manager_v1* aColorRepresentationManager);
  wp_color_representation_manager_v1* GetColorRepresentationManager() const {
    return mColorRepresentationManager;
  }
  void SetPipShell(xx_pip_shell_v1* aShell) { mPipShell = aShell; }
  xx_pip_shell_v1* GetPipShell() const { return mPipShell; }
  void SetXdgWm(xdg_wm_base* aWmBase) { mWmBase = aWmBase; }
  xdg_wm_base* GetXdgWm() const { return mWmBase; }
  void SetCMSupportedFeature(uint32_t aFeature);
  void SetCMSupportedTFNamed(uint32_t aTF);
  void SetCMSupportedPrimariesNamed(uint32_t aPrimaries);
  bool IsHDREnabled() const {
    return mColorManagerSupportedFeature.mParametric;
  }
  void SetSupportedCoefficientsAndRanges(uint32_t aCoefficients,
                                         uint32_t aRange);
  uint32_t GetColorRange(uint32_t aCoefficients, bool aFullRange);
  RefPtr<DMABufFormats> GetDMABufFormats() const { return mFormats; }
  bool HasDMABufFeedback() const { return mDmabufIsFeedback; }
  void EnsureDMABufFormats();

  static void AsyncRoundtripCallback(void* aData, wl_callback* aCallback,
                                     uint32_t aTime);
  void RequestAsyncRoundtrip();
  void WaitForAsyncRoundtrips();

  ~nsWaylandDisplay();

 private:
  PRThread* mThreadId = nullptr;
  wl_registry* mRegistry = nullptr;
  wl_display* mDisplay = nullptr;
  wl_compositor* mCompositor = nullptr;
  wl_subcompositor* mSubcompositor = nullptr;
  wl_shm* mShm = nullptr;
  wl_seat* mSeat = nullptr;
  int mSeatId = -1;
  wl_keyboard* mKeyboard = nullptr;
  wl_pointer* mPointer = nullptr;
  zwp_idle_inhibit_manager_v1* mIdleInhibitManager = nullptr;
  zwp_relative_pointer_manager_v1* mRelativePointerManager = nullptr;
  zwp_pointer_constraints_v1* mPointerConstraints = nullptr;
  zwp_pointer_gestures_v1* mPointerGestures = nullptr;
  zwp_pointer_gesture_hold_v1* mPointerGestureHold = nullptr;
  wp_viewporter* mViewporter = nullptr;
  bool mDmabufIsFeedback = false;
  zwp_linux_dmabuf_v1* mDmabuf = nullptr;
  xdg_activation_v1* mXdgActivation = nullptr;
  org_kde_kwin_appmenu_manager* mAppMenuManager = nullptr;
  wp_fractional_scale_manager_v1* mFractionalScaleManager = nullptr;
  wp_color_manager_v1* mColorManager = nullptr;
  wp_color_representation_manager_v1* mColorRepresentationManager = nullptr;
  xx_pip_shell_v1* mPipShell = nullptr;
  xdg_wm_base* mWmBase = nullptr;
  RefPtr<DMABufFormats> mFormats;
  GList* mAsyncRoundtrips = nullptr;

  struct ColorManagerSupportedFeature {
    bool mICC = false;
    bool mParametric = false;
    bool mPrimaries = false;
    bool mFTPower = false;
    bool mLuminances = false;
    bool mDisplayPrimaries = false;
  } mColorManagerSupportedFeature;

  int mSupportedTransfer[sColorTransfersNum] = {};
  int mSupportedPrimaries[sColorPrimariesNum] = {};

  constexpr static int sSupportedRangeFull = 1;
  constexpr static int sSupportedRangeLimited = 2;
  constexpr static int sSupportedRangeBoth = 3;
  constexpr static int sSupportedRangesNum =
      WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_ICTCP + 1;
  uint32_t mSupportedRanges[sSupportedRangesNum] = {};

  bool mExplicitSync = false;
  bool mIsPrimarySelectionEnabled = false;
};

wl_display* WaylandDisplayGetWLDisplay();
nsWaylandDisplay* WaylandDisplayGet();
void WaylandDisplayRelease();
void WlCompositorCrashHandler();

}  // namespace mozilla::widget

template <class T>
static inline T* WaylandRegistryBind(struct wl_registry* wl_registry,
                                     uint32_t name,
                                     const struct wl_interface* interface,
                                     uint32_t version) {
  struct wl_proxy* id;

  // When libwayland-client does not provide this symbol, it will be
  // linked to the fallback in libmozwayland, which returns NULL.
  id = wl_proxy_marshal_constructor_versioned(
      (struct wl_proxy*)wl_registry, WL_REGISTRY_BIND, interface, version, name,
      interface->name, version, nullptr);

  if (id == nullptr) {
    id = wl_proxy_marshal_constructor((struct wl_proxy*)wl_registry,
                                      WL_REGISTRY_BIND, interface, name,
                                      interface->name, version, nullptr);
  }

  return reinterpret_cast<T*>(id);
}

#endif  // __MOZ_WAYLAND_DISPLAY_H__
