/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Texture.h"

#include "TextureView.h"
#include "Utility.h"
#include "ipc/WebGPUChild.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/webgpu/CanvasContext.h"
#include "mozilla/webgpu/WebGPUTypes.h"
#include "mozilla/webgpu/ffi/wgpu.h"

namespace mozilla::webgpu {

GPU_IMPL_CYCLE_COLLECTION(Texture, mParent)
GPU_IMPL_JS_WRAP(Texture)

static Maybe<uint8_t> GetBytesPerBlockSingleAspect(
    dom::GPUTextureFormat aFormat) {
  auto format = ConvertTextureFormat(aFormat);
  ffi::WGPUTextureAspect aspect = {ffi::WGPUTextureAspect_All};
  ffi::WGPUTextureFormatBlockInfo info = {};
  bool valid = ffi::wgpu_texture_format_get_block_info(format, aspect, &info);
  if (!valid) {
    // The above function returns false if the texture has multiple aspects like
    // depth and stencil.
    return Nothing();
  }

  return Some((uint8_t)info.copy_size);
}

Texture::Texture(Device* const aParent, RawId aId,
                 const dom::GPUTextureDescriptor& aDesc)
    : ObjectBase(aParent->GetChild(), aId, ffi::wgpu_client_drop_texture),
      ChildOf(aParent),
      mFormat(aDesc.mFormat),
      mBytesPerBlock(GetBytesPerBlockSingleAspect(aDesc.mFormat)),
      mSize(ConvertExtent(aDesc.mSize)),
      mMipLevelCount(aDesc.mMipLevelCount),
      mSampleCount(aDesc.mSampleCount),
      mDimension(aDesc.mDimension),
      mUsage(aDesc.mUsage) {}

Texture::~Texture() = default;

already_AddRefed<TextureView> Texture::CreateView(
    const dom::GPUTextureViewDescriptor& aDesc) {
  ffi::WGPUTextureViewDescriptor desc = {};

  webgpu::StringHelper label(aDesc.mLabel);
  desc.label = label.Get();

  ffi::WGPUTextureFormat format = {ffi::WGPUTextureFormat_Sentinel};
  if (aDesc.mFormat.WasPassed()) {
    format = ConvertTextureFormat(aDesc.mFormat.Value());
    desc.format = &format;
  }
  ffi::WGPUTextureViewDimension dimension =
      ffi::WGPUTextureViewDimension_Sentinel;
  if (aDesc.mDimension.WasPassed()) {
    dimension = ffi::WGPUTextureViewDimension(aDesc.mDimension.Value());
    desc.dimension = &dimension;
  }

  // Ideally we'd just do something like "aDesc.mMipLevelCount.ptrOr(nullptr)"
  // but dom::Optional does not seem to have very many nice things.
  uint32_t mipCount =
      aDesc.mMipLevelCount.WasPassed() ? aDesc.mMipLevelCount.Value() : 0;
  uint32_t layerCount =
      aDesc.mArrayLayerCount.WasPassed() ? aDesc.mArrayLayerCount.Value() : 0;

  desc.aspect = ffi::WGPUTextureAspect(aDesc.mAspect);
  desc.base_mip_level = aDesc.mBaseMipLevel;
  desc.mip_level_count = aDesc.mMipLevelCount.WasPassed() ? &mipCount : nullptr;
  desc.base_array_layer = aDesc.mBaseArrayLayer;
  desc.array_layer_count =
      aDesc.mArrayLayerCount.WasPassed() ? &layerCount : nullptr;

  RawId id = ffi::wgpu_client_create_texture_view(GetClient(), mParent->GetId(),
                                                  GetId(), &desc);

  RefPtr<TextureView> view = new TextureView(this, id);
  view->SetLabel(aDesc.mLabel);
  return view.forget();
}

void Texture::Destroy() {
  ffi::wgpu_client_destroy_texture(GetClient(), GetId());
}
}  // namespace mozilla::webgpu
