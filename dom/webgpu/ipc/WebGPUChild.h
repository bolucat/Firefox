/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGPU_CHILD_H_
#define WEBGPU_CHILD_H_

#include <deque>
#include <unordered_map>

#include "mozilla/MozPromise.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/webgpu/Adapter.h"
#include "mozilla/webgpu/Device.h"
#include "mozilla/webgpu/Instance.h"
#include "mozilla/webgpu/PWebGPUChild.h"
#include "mozilla/webgpu/SupportedFeatures.h"
#include "mozilla/webgpu/SupportedLimits.h"
#include "mozilla/webgpu/ffi/wgpu.h"

namespace mozilla {
namespace dom {
struct GPURequestAdapterOptions;
}  // namespace dom
namespace layers {
class CompositorBridgeChild;
}  // namespace layers
namespace webgpu {
namespace ffi {
struct WGPUClient;
struct WGPULimits;
struct WGPUTextureViewDescriptor;
}  // namespace ffi

using AdapterPromise =
    MozPromise<ipc::ByteBuf, Maybe<ipc::ResponseRejectReason>, true>;
using PipelinePromise = MozPromise<RawId, ipc::ResponseRejectReason, true>;
using DevicePromise = MozPromise<bool, ipc::ResponseRejectReason, true>;

ffi::WGPUByteBuf* ToFFI(ipc::ByteBuf* x);

class WebGPUChild final : public PWebGPUChild, public SupportsWeakPtr {
 public:
  friend class layers::CompositorBridgeChild;

  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(WebGPUChild)
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING_INHERITED(WebGPUChild)

 public:
  explicit WebGPUChild();

  RawId RenderBundleEncoderFinish(ffi::WGPURenderBundleEncoder& aEncoder,
                                  RawId aDeviceId,
                                  const dom::GPURenderBundleDescriptor& aDesc);
  RawId RenderBundleEncoderFinishError(RawId aDeviceId, const nsString& aLabel);

  ffi::WGPUClient* GetClient() const { return mClient.get(); }

  void SwapChainPresent(RawId aTextureId,
                        const RemoteTextureId& aRemoteTextureId,
                        const RemoteTextureOwnerId& aOwnerId);

  void RegisterDevice(Device* const aDevice);
  void UnregisterDevice(RawId aDeviceId);

  void QueueSubmit(RawId aSelfId, RawId aDeviceId,
                   nsTArray<RawId>& aCommandBuffers);
  void NotifyWaitForSubmit(RawId aTextureId);

  static void JsWarning(nsIGlobalObject* aGlobal, const nsACString& aMessage);

 private:
  virtual ~WebGPUChild();

  UniquePtr<ffi::WGPUClient> const mClient;
  std::unordered_map<RawId, RefPtr<Device>> mDeviceMap;
  nsTArray<RawId> mSwapChainTexturesWaitingForSubmit;

  bool mScheduledFlushQueuedMessages = false;
  void ScheduledFlushQueuedMessages();
  nsTArray<ipc::ByteBuf> mQueuedDataBuffers;
  nsTArray<ipc::MutableSharedMemoryHandle> mQueuedHandles;
  void ClearActorState();

 public:
  ipc::IPCResult RecvServerMessage(const ipc::ByteBuf& aByteBuf);
  ipc::IPCResult RecvUncapturedError(RawId aDeviceId,
                                     const nsACString& aMessage);
  ipc::IPCResult RecvDeviceLost(RawId aDeviceId, uint8_t aReason,
                                const nsACString& aMessage);

  size_t QueueDataBuffer(ipc::ByteBuf&& bb);
  size_t QueueShmemHandle(ipc::MutableSharedMemoryHandle&& handle);
  void ScheduleFlushQueuedMessages();
  void FlushQueuedMessages();

  void ActorDestroy(ActorDestroyReason) override;

  struct PendingRequestAdapterPromise {
    RefPtr<dom::Promise> promise;
    RefPtr<Instance> instance;
    RawId adapter_id;
  };

  std::deque<PendingRequestAdapterPromise> mPendingRequestAdapterPromises;

  struct PendingRequestDevicePromise {
    RefPtr<dom::Promise> promise;
    RawId device_id;
    RawId queue_id;
    nsString label;
    RefPtr<Adapter> adapter;
    RefPtr<SupportedFeatures> features;
    RefPtr<SupportedLimits> limits;
    RefPtr<AdapterInfo> adapter_info;
    RefPtr<dom::Promise> lost_promise;
  };

  std::deque<PendingRequestDevicePromise> mPendingRequestDevicePromises;

  std::unordered_map<RawId, RefPtr<dom::Promise>> mPendingDeviceLostPromises;

  struct PendingPopErrorScopePromise {
    RefPtr<dom::Promise> promise;
    RefPtr<Device> device;
  };

  std::deque<PendingPopErrorScopePromise> mPendingPopErrorScopePromises;

  struct PendingCreatePipelinePromise {
    RefPtr<dom::Promise> promise;
    RefPtr<Device> device;
    bool is_render_pipeline;
    RawId pipeline_id;
    nsString label;
  };

  std::deque<PendingCreatePipelinePromise> mPendingCreatePipelinePromises;

  struct PendingCreateShaderModulePromise {
    RefPtr<dom::Promise> promise;
    RefPtr<Device> device;
    RefPtr<ShaderModule> shader_module;
  };

  std::deque<PendingCreateShaderModulePromise>
      mPendingCreateShaderModulePromises;

  struct PendingBufferMapPromise {
    RefPtr<dom::Promise> promise;
    RefPtr<Buffer> buffer;
  };

  std::unordered_map<RawId, std::deque<PendingBufferMapPromise>>
      mPendingBufferMapPromises;

  // Pending submitted work done promises for each queue. We must track these
  // separately for each queue because there are guarantees about the order
  // different queues will complete their work in. For each queue individually
  // we know these will be resolved FIFO.
  std::unordered_map<ffi::WGPUQueueId, std::deque<RefPtr<dom::Promise>>>
      mPendingOnSubmittedWorkDonePromises;
};

}  // namespace webgpu
}  // namespace mozilla

#endif  // WEBGPU_CHILD_H_
