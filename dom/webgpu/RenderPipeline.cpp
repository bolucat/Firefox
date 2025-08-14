/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderPipeline.h"

#include "Device.h"
#include "ipc/WebGPUChild.h"
#include "mozilla/dom/WebGPUBinding.h"

namespace mozilla::webgpu {

GPU_IMPL_CYCLE_COLLECTION(RenderPipeline, mParent)
GPU_IMPL_JS_WRAP(RenderPipeline)

RenderPipeline::RenderPipeline(Device* const aParent, RawId aId)
    : ChildOf(aParent), mId(aId) {
  MOZ_RELEASE_ASSERT(aId);
}

RenderPipeline::~RenderPipeline() { Cleanup(); }

void RenderPipeline::Cleanup() {
  if (!mValid) {
    return;
  }
  mValid = false;

  auto bridge = mParent->GetBridge();
  if (!bridge) {
    return;
  }

  ffi::wgpu_client_drop_render_pipeline(bridge->GetClient(), mId);
}

already_AddRefed<BindGroupLayout> RenderPipeline::GetBindGroupLayout(
    uint32_t aIndex) const {
  auto bridge = mParent->GetBridge();

  const RawId bglId = ffi::wgpu_client_render_pipeline_get_bind_group_layout(
      bridge->GetClient(), mParent->GetId(), mId, aIndex);

  RefPtr<BindGroupLayout> object = new BindGroupLayout(mParent, bglId);
  return object.forget();
}

}  // namespace mozilla::webgpu
