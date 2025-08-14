/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerRootRemoteMacParent_h
#define mozilla_layers_NativeLayerRootRemoteMacParent_h

#include "mozilla/layers/NativeLayerCA.h"
#include "mozilla/layers/NativeLayerRemoteParent.h"

namespace mozilla {
namespace layers {

// NativeLayerRootRemoteMacParent is a macOS-specific receiver class for
// NativeLayerRootRemoteMacChild. It interprets the incoming messages and
// sends them on to the "real" NativeLayerRoot object in the parent
// process.
class NativeLayerRootRemoteMacParent final : public NativeLayerRemoteParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NativeLayerRootRemoteMacParent,
                                        override)

  explicit NativeLayerRootRemoteMacParent(
      RefPtr<NativeLayerRootCA> aRealNativeLayerRoot);

  mozilla::ipc::IPCResult RecvCommitNativeLayerCommands(
      nsTArray<NativeLayerCommand>&& aCommands) override;

 protected:
  ~NativeLayerRootRemoteMacParent() = default;

  void HandleCreateLayer(uint64_t aID, IntSize aSize, bool aOpaque);
  void HandleCreateLayerForExternalTexture(uint64_t aID, bool aOpaque);
  void HandleCreateLayerForColor(uint64_t aID, DeviceColor aColor);
  void HandleLayerDestroyed(uint64_t aID);
  void HandleSetLayers(const nsTArray<uint64_t>& aIDs);
  void HandleLayerInfo(uint64_t aID, uint32_t aSurfaceID, bool aIsDRM,
                       bool aIsHDR, IntPoint aPosition, IntSize aSize,
                       IntRect aDisplayRect, Maybe<IntRect> aClipRect,
                       Maybe<RoundedRect> aRoundedClipRect,
                       Matrix4x4 aTransform, int8_t aSamplingFilter,
                       bool aSurfaceIsFlipped);

  RefPtr<NativeLayerRootCA> mRealNativeLayerRoot;
  nsTHashMap<uint64_t, RefPtr<NativeLayer>> mKnownLayers;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerRootRemoteMacParent_h
