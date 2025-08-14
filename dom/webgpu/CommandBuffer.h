/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_CommandBuffer_H_
#define GPU_CommandBuffer_H_

#include "ObjectModel.h"
#include "mozilla/Span.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "nsTArrayForwardDeclare.h"
#include "nsWrapperCache.h"

namespace mozilla::webgpu {

class CanvasContext;
class CommandEncoder;
class Device;
class ExternalTexture;

class CommandBuffer final : public ObjectBase, public ChildOf<Device> {
 public:
  GPU_DECL_CYCLE_COLLECTION(CommandBuffer)
  GPU_DECL_JS_WRAP(CommandBuffer)

  CommandBuffer(Device* const aParent, WebGPUChild* const aBridge, RawId aId,
                nsTArray<WeakPtr<CanvasContext>>&& aPresentationContexts,
                nsTArray<RefPtr<ExternalTexture>>&& aExternalTextures);

  Span<const RefPtr<ExternalTexture>> GetExternalTextures() const {
    return mExternalTextures;
  }

  Maybe<RawId> Commit();

 private:
  CommandBuffer() = delete;
  ~CommandBuffer();
  void Cleanup();

  const RawId mId;
  RefPtr<WebGPUChild> mBridge;
  const nsTArray<WeakPtr<CanvasContext>> mPresentationContexts;

  // List of external textures used in this command buffer.
  nsTArray<RefPtr<ExternalTexture>> mExternalTextures;
};

}  // namespace mozilla::webgpu

#endif  // GPU_CommandBuffer_H_
