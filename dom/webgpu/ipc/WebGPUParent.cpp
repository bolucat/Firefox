/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGPUParent.h"

#include <unordered_set>

#include "ExternalTexture.h"
#include "mozilla/PodOperations.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/FileHandleWrapper.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/WebRenderImageHost.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/webgpu/SharedTexture.h"
#include "mozilla/webgpu/ffi/wgpu.h"

#if defined(XP_WIN)
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/webgpu/SharedTextureD3D11.h"
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/webgpu/SharedTextureDMABuf.h"
#endif

#if defined(XP_MACOSX)
#  include "mozilla/webgpu/SharedTextureMacIOSurface.h"
#endif

namespace mozilla::webgpu {

const uint64_t POLL_TIME_MS = 100;

static mozilla::LazyLogModule sLogger("WebGPU");

namespace ffi {

extern bool wgpu_server_use_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  return parent->UseSharedTextureForSwapChain(aSwapChainId);
}

extern void wgpu_server_disable_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  parent->DisableSharedTextureForSwapChain(aSwapChainId);
}

extern bool wgpu_server_ensure_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId,
    WGPUDeviceId aDeviceId, WGPUTextureId aTextureId, uint32_t aWidth,
    uint32_t aHeight, struct WGPUTextureFormat aFormat,
    WGPUTextureUsages aUsage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  return parent->EnsureSharedTextureForSwapChain(
      aSwapChainId, aDeviceId, aTextureId, aWidth, aHeight, aFormat, aUsage);
}

extern void wgpu_server_ensure_shared_texture_for_readback(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId,
    WGPUDeviceId aDeviceId, WGPUTextureId aTextureId, uint32_t aWidth,
    uint32_t aHeight, struct WGPUTextureFormat aFormat,
    WGPUTextureUsages aUsage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  parent->EnsureSharedTextureForReadBackPresent(
      aSwapChainId, aDeviceId, aTextureId, aWidth, aHeight, aFormat, aUsage);
}

#ifdef XP_WIN
extern void* wgpu_server_get_shared_texture_handle(WGPUWebGPUParentPtr aParent,
                                                   WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto* textureD3D11 = texture->AsSharedTextureD3D11();
  if (!textureD3D11) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }
  void* sharedHandle = textureD3D11->GetSharedTextureHandle();
  if (!sharedHandle) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "Failed to get shared handle";
    return nullptr;
  }

  return sharedHandle;
}
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
extern int32_t wgpu_server_get_dma_buf_fd(WGPUWebGPUParentPtr aParent,
                                          WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return -1;
  }

  auto* textureDMABuf = texture->AsSharedTextureDMABuf();
  if (!textureDMABuf) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return -1;
  }
  auto fd = textureDMABuf->CloneDmaBufFd();
  // fd should be closed by the caller.
  return fd.release();
}
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
extern const WGPUVkImageHandle* wgpu_server_get_vk_image_handle(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto* textureDMABuf = texture->AsSharedTextureDMABuf();
  if (!textureDMABuf) {
    return nullptr;
  }
  return textureDMABuf->GetHandle();
}
#endif

#if defined(XP_MACOSX)
extern uint32_t wgpu_server_get_external_io_surface_id(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return 0;
  }

  auto* textureIOSurface = texture->AsSharedTextureMacIOSurface();
  if (!textureIOSurface) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return 0;
  }
  return textureIOSurface->GetIOSurfaceId();
}
#endif

extern void wgpu_server_remove_shared_texture(WGPUWebGPUParentPtr aParent,
                                              WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->RemoveSharedTexture(aId);
}

extern ffi::WGPUExternalTextureDescriptorFromSource
wgpu_parent_external_texture_source_get_external_texture_descriptor(
    void* aParent, WGPUExternalTextureSourceId aId,
    ffi::WGPUPredefinedColorSpace aDestColorSpace) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  const auto& source = parent->GetExternalTextureSource(aId);
  return source.GetExternalTextureDescriptor(aDestColorSpace);
}

extern void wgpu_parent_destroy_external_texture_source(
    WGPUWebGPUParentPtr aParent, WGPUExternalTextureSourceId aId) {
  auto* const parent = static_cast<WebGPUParent*>(aParent);
  parent->DestroyExternalTextureSource(aId);
}

extern void wgpu_parent_drop_external_texture_source(
    WGPUWebGPUParentPtr aParent, WGPUExternalTextureSourceId aId) {
  auto* const parent = static_cast<WebGPUParent*>(aParent);
  parent->DropExternalTextureSource(aId);
}

extern void wgpu_server_dealloc_buffer_shmem(WGPUWebGPUParentPtr aParent,
                                             WGPUBufferId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->DeallocBufferShmem(aId);
}

extern void wgpu_server_pre_device_drop(WGPUWebGPUParentPtr aParent,
                                        WGPUDeviceId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->PreDeviceDrop(aId);
}

extern void wgpu_server_set_buffer_map_data(
    WGPUWebGPUParentPtr aParent, WGPUDeviceId aDeviceId, WGPUBufferId aBufferId,
    bool aHasMapFlags, uint64_t aMappedOffset, uint64_t aMappedSize,
    uintptr_t aShmemIndex) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto mapping = std::move(parent->mTempMappings.ElementAt(aShmemIndex));
  MOZ_ASSERT(mapping.isSome());

  auto data = WebGPUParent::BufferMapData{
      std::move(*mapping), aHasMapFlags, aMappedOffset, aMappedSize, aDeviceId,
  };

  parent->mSharedMemoryMap.insert({aBufferId, std::move(data)});
}

extern void wgpu_server_device_push_error_scope(WGPUWebGPUParentPtr aParent,
                                                WGPUDeviceId aDeviceId,
                                                uint8_t aFilter) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->DevicePushErrorScope(aDeviceId, (dom::GPUErrorFilter)aFilter);
}

extern void wgpu_server_device_pop_error_scope(WGPUWebGPUParentPtr aParent,
                                               WGPUDeviceId aDeviceId,
                                               uint8_t* aOutType,
                                               nsCString* aOutMessage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto result = parent->DevicePopErrorScope(aDeviceId);
  *aOutType = (uint8_t)result.resultType;
  *aOutMessage = std::move(result.message);
}

extern void wgpu_parent_buffer_unmap(WGPUWebGPUParentPtr aParent,
                                     WGPUDeviceId aDeviceId,
                                     WGPUBufferId aBufferId, bool aFlush) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->BufferUnmap(aDeviceId, aBufferId, aFlush);
}

extern void wgpu_parent_queue_submit(
    WGPUWebGPUParentPtr aParent, WGPUDeviceId aDeviceId, WGPUQueueId aQueueId,
    const WGPUCommandBufferId* aCommandBufferIds,
    uintptr_t aCommandBufferIdsLength, const WGPUTextureId* aTextureIds,
    uintptr_t aTextureIdsLength) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto command_buffers = Span(aCommandBufferIds, aCommandBufferIdsLength);
  auto textures = Span(aTextureIds, aTextureIdsLength);
  parent->QueueSubmit(aDeviceId, aQueueId, command_buffers, textures);
}

extern void wgpu_parent_create_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUDeviceId aDeviceId, WGPUQueueId aQueueId,
    int32_t aWidth, int32_t aHeight, WGPUSurfaceFormat aFormat,
    const WGPUBufferId* aBufferIds, uintptr_t aBufferIdsLength,
    WGPURemoteTextureOwnerId aRemoteTextureOwnerId,
    bool aUseSharedTextureInSwapChain) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto buffer_ids_span = Span(aBufferIds, aBufferIdsLength);
  auto buffer_ids = nsTArray<RawId>(aBufferIdsLength);
  for (const RawId id : buffer_ids_span) {
    buffer_ids.AppendElement(id);
  }
  auto size = gfx::IntSize(aWidth, aHeight);
  auto format = gfx::SurfaceFormat(aFormat);
  auto desc = layers::RGBDescriptor(size, format);
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->DeviceCreateSwapChain(aDeviceId, aQueueId, desc, buffer_ids, owner,
                                aUseSharedTextureInSwapChain);
}

extern void wgpu_parent_swap_chain_present(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aTextureId,
    WGPUCommandEncoderId aCommandEncoderId,
    WGPUCommandBufferId aCommandBufferId, WGPURemoteTextureId aRemoteTextureId,
    WGPURemoteTextureOwnerId aRemoteTextureOwnerId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto remote_texture = layers::RemoteTextureId{aRemoteTextureId};
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->SwapChainPresent(aTextureId, aCommandEncoderId, aCommandBufferId,
                           remote_texture, owner);
}

extern void wgpu_parent_swap_chain_drop(
    WGPUWebGPUParentPtr aParent, WGPURemoteTextureOwnerId aRemoteTextureOwnerId,
    WGPURemoteTextureTxnType aTxnType, WGPURemoteTextureTxnId aTxnId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->SwapChainDrop(owner, aTxnType, aTxnId);
}

#ifdef XP_WIN
extern void wgpu_parent_get_compositor_device_luid(
    struct WGPUFfiLUID* aOutLuid) {
  auto luid = WebGPUParent::GetCompositorDeviceLuid();
  if (luid.isSome()) {
    *aOutLuid = luid.extract();
  } else {
    aOutLuid = nullptr;
  }
}
#endif

extern void wgpu_parent_post_request_device(WGPUWebGPUParentPtr aParent,
                                            WGPUDeviceId aDeviceId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->PostAdapterRequestDevice(aDeviceId);
}

extern ffi::WGPUBufferMapClosure wgpu_parent_build_buffer_map_closure(
    WGPUWebGPUParentPtr aParent, RawId aDeviceId, RawId aBufferId,
    ffi::WGPUHostMap aMode, uint64_t aOffset, uint64_t aSize) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  std::unique_ptr<WebGPUParent::MapRequest> request(
      new WebGPUParent::MapRequest{parent, aDeviceId, aBufferId, aMode, aOffset,
                                   aSize});

  ffi::WGPUBufferMapClosure closure = {
      &WebGPUParent::MapCallback,
      reinterpret_cast<uint8_t*>(request.release())};

  return closure;
}

extern ffi::WGPUSubmittedWorkDoneClosure
wgpu_parent_build_submitted_work_done_closure(WGPUWebGPUParentPtr aParent,
                                              WGPUQueueId aQueueId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  std::unique_ptr<WebGPUParent::OnSubmittedWorkDoneRequest> request(
      new WebGPUParent::OnSubmittedWorkDoneRequest{parent, aQueueId});

  ffi::WGPUSubmittedWorkDoneClosure closure = {
      &WebGPUParent::OnSubmittedWorkDoneCallback,
      reinterpret_cast<uint8_t*>(request.release())};

  return closure;
}

extern void wgpu_parent_handle_error(WGPUWebGPUParentPtr aParent,
                                     WGPUDeviceId aDeviceId,
                                     WGPUErrorBufferType aTy,
                                     const nsCString* aMessage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  dom::GPUErrorFilter ty;
  switch (aTy) {
    case ffi::WGPUErrorBufferType_Internal:
      ty = dom::GPUErrorFilter::Internal;
      break;
    case ffi::WGPUErrorBufferType_Validation:
      ty = dom::GPUErrorFilter::Validation;
      break;
    case ffi::WGPUErrorBufferType_OutOfMemory:
      ty = dom::GPUErrorFilter::Out_of_memory;
      break;
    default:
      MOZ_CRASH("invalid `ErrorBufferType`");
  }

  parent->ReportError(aDeviceId, ty, *aMessage);
}

extern void wgpu_parent_send_server_message(WGPUWebGPUParentPtr aParent,
                                            struct WGPUByteBuf* aMessage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto* message = FromFFI(aMessage);
  if (!parent->SendServerMessage(std::move(*message))) {
    NS_ERROR("SendServerMessage failed");
  }
}

}  // namespace ffi

ErrorBuffer::ErrorBuffer() { mMessageUtf8[0] = 0; }

ErrorBuffer::~ErrorBuffer() { MOZ_ASSERT(!mAwaitingGetError); }

ffi::WGPUErrorBuffer ErrorBuffer::ToFFI() {
  mAwaitingGetError = true;
  ffi::WGPUErrorBuffer errorBuf = {&mType, mMessageUtf8, BUFFER_SIZE,
                                   &mDeviceId};
  return errorBuf;
}

ffi::WGPUErrorBufferType ErrorBuffer::GetType() { return mType; }

Maybe<dom::GPUErrorFilter> ErrorBuffer::ErrorTypeToFilterType(
    ffi::WGPUErrorBufferType aType) {
  switch (aType) {
    case ffi::WGPUErrorBufferType_None:
    case ffi::WGPUErrorBufferType_DeviceLost:
      return {};
    case ffi::WGPUErrorBufferType_Internal:
      return Some(dom::GPUErrorFilter::Internal);
    case ffi::WGPUErrorBufferType_Validation:
      return Some(dom::GPUErrorFilter::Validation);
    case ffi::WGPUErrorBufferType_OutOfMemory:
      return Some(dom::GPUErrorFilter::Out_of_memory);
    case ffi::WGPUErrorBufferType_Sentinel:
      break;
  }

  MOZ_CRASH("invalid `ErrorBufferType`");
}

Maybe<ErrorBuffer::Error> ErrorBuffer::GetError() {
  mAwaitingGetError = false;
  if (mType == ffi::WGPUErrorBufferType_DeviceLost) {
    // This error is for a lost device, so we return an Error struct
    // with the isDeviceLost bool set to true. It doesn't matter what
    // GPUErrorFilter type we use, so we just use Validation. The error
    // will not be reported.
    return Some(Error{dom::GPUErrorFilter::Validation, true,
                      nsCString{mMessageUtf8}, mDeviceId});
  }
  auto filterType = ErrorTypeToFilterType(mType);
  if (!filterType) {
    return {};
  }
  return Some(Error{*filterType, false, nsCString{mMessageUtf8}, mDeviceId});
}

void ErrorBuffer::CoerceValidationToInternal() {
  if (mType == ffi::WGPUErrorBufferType_Validation) {
    mType = ffi::WGPUErrorBufferType_Internal;
  }
}

struct PendingSwapChainDrop {
  layers::RemoteTextureTxnType mTxnType;
  layers::RemoteTextureTxnId mTxnId;
};

class PresentationData {
  NS_INLINE_DECL_REFCOUNTING(PresentationData);

 public:
  WeakPtr<WebGPUParent> mParent;
  bool mUseSharedTextureInSwapChain;
  const RawId mDeviceId;
  const RawId mQueueId;
  Maybe<RawId> mLastSubmittedTextureId;
  const layers::RGBDescriptor mDesc;

  uint64_t mSubmissionIndex = 0;

  std::deque<std::shared_ptr<SharedTexture>> mRecycledSharedTextures;

  std::unordered_set<layers::RemoteTextureId, layers::RemoteTextureId::HashFn>
      mWaitingReadbackTexturesForPresent;
  Maybe<PendingSwapChainDrop> mPendingSwapChainDrop;

  const uint32_t mSourcePitch;
  std::vector<RawId> mUnassignedBufferIds;
  std::vector<RawId> mAvailableBufferIds;
  std::vector<RawId> mQueuedBufferIds;

  bool mReadbackSnapshotCallbackCalled = false;

  PresentationData(WebGPUParent* aParent, bool aUseSharedTextureInSwapChain,
                   RawId aDeviceId, RawId aQueueId,
                   const layers::RGBDescriptor& aDesc, uint32_t aSourcePitch,
                   const nsTArray<RawId>& aBufferIds)
      : mParent(aParent),
        mUseSharedTextureInSwapChain(aUseSharedTextureInSwapChain),
        mDeviceId(aDeviceId),
        mQueueId(aQueueId),
        mDesc(aDesc),
        mSourcePitch(aSourcePitch) {
    MOZ_COUNT_CTOR(PresentationData);

    for (const RawId id : aBufferIds) {
      mUnassignedBufferIds.push_back(id);
    }
  }

 private:
  ~PresentationData() { MOZ_COUNT_DTOR(PresentationData); }
};

WebGPUParent::WebGPUParent() : mContext(ffi::wgpu_server_new(this)) {
  mTimer.Start(base::TimeDelta::FromMilliseconds(POLL_TIME_MS), this,
               &WebGPUParent::MaintainDevices);
}

WebGPUParent::~WebGPUParent() {}

void WebGPUParent::MaintainDevices() {
  ffi::wgpu_server_poll_all_devices(mContext.get(), false);
}

void WebGPUParent::LoseDevice(const RawId aDeviceId, uint8_t aReason,
                              const nsACString& aMessage) {
  if (mActiveDeviceIds.Contains(aDeviceId)) {
    mActiveDeviceIds.Remove(aDeviceId);
  }
  // Check to see if we've already sent a DeviceLost message to aDeviceId.
  if (mLostDeviceIds.Contains(aDeviceId)) {
    return;
  }

  // If the connection has been dropped, there is nobody to receive
  // the DeviceLost message anyway.
  if (!CanSend()) {
    return;
  }

  if (!SendDeviceLost(aDeviceId, aReason, aMessage)) {
    NS_ERROR("SendDeviceLost failed");
    return;
  }

  mLostDeviceIds.Insert(aDeviceId);
}

bool WebGPUParent::ForwardError(ErrorBuffer& aError) {
  if (auto error = aError.GetError()) {
    // If this is a error has isDeviceLost true, then instead of reporting
    // the error, we swallow it and call LoseDevice if we have an
    // aDeviceID. This is to comply with the spec declaration in
    // https://gpuweb.github.io/gpuweb/#lose-the-device
    // "No errors are generated after device loss."
    if (error->isDeviceLost) {
      if (error->deviceId) {
        LoseDevice(error->deviceId,
                   static_cast<uint8_t>(dom::GPUDeviceLostReason::Unknown),
                   error->message);
      }
    } else {
      ReportError(error->deviceId, error->type, error->message);
    }
    return true;
  }
  return false;
}

// Generate an error on the Device timeline of aDeviceId.
// aMessage is interpreted as UTF-8.
void WebGPUParent::ReportError(RawId aDeviceId, const GPUErrorFilter aType,
                               const nsCString& aMessage) {
  // find the appropriate error scope
  if (aDeviceId) {
    const auto& itr = mErrorScopeStackByDevice.find(aDeviceId);
    if (itr != mErrorScopeStackByDevice.end()) {
      auto& stack = itr->second;
      for (auto& scope : Reversed(stack)) {
        if (scope.filter != aType) {
          continue;
        }
        if (!scope.firstMessage) {
          scope.firstMessage = Some(aMessage);
        }
        return;
      }
    }
  }
  // No error scope found, so fall back to the uncaptured error handler
  if (!SendUncapturedError(aDeviceId, aMessage)) {
    NS_ERROR("SendDeviceUncapturedError failed");
  }
}

struct OnDeviceLostRequest {
  WeakPtr<WebGPUParent> mParent;
  RawId mDeviceId;
};

static void DeviceLostCleanupCallback(uint8_t* aUserData) {
  auto req = std::unique_ptr<OnDeviceLostRequest>(
      reinterpret_cast<OnDeviceLostRequest*>(aUserData));
}

/* static */ void WebGPUParent::DeviceLostCallback(uint8_t* aUserData,
                                                   uint8_t aReason,
                                                   const char* aMessage) {
  auto req = std::unique_ptr<OnDeviceLostRequest>(
      reinterpret_cast<OnDeviceLostRequest*>(aUserData));
  if (!req->mParent) {
    // Parent is dead, never mind.
    return;
  }

  RawId deviceId = req->mDeviceId;

  // NOTE: Based on `u8` discriminant values provided for `DeviceLostReason` in
  // `wgpu_bindings`.
  uint8_t reason;
  switch (aReason) {
    case 0:
      reason = static_cast<uint8_t>(dom::GPUDeviceLostReason::Unknown);
      break;
    case 1:
      reason = static_cast<uint8_t>(dom::GPUDeviceLostReason::Destroyed);
      break;
    default:
      MOZ_CRASH_UNSAFE_PRINTF(
          "invalid `aReason` from device lost callback: %hhu", aReason);
      break;
  }
  nsAutoCString message(aMessage);
  req->mParent->LoseDevice(deviceId, reason, message);

  auto it = req->mParent->mDeviceFenceHandles.find(deviceId);
  if (it != req->mParent->mDeviceFenceHandles.end()) {
    req->mParent->mDeviceFenceHandles.erase(it);
  }
}

void WebGPUParent::PostAdapterRequestDevice(RawId aDeviceId) {
  mErrorScopeStackByDevice.insert({aDeviceId, {}});

  std::unique_ptr<OnDeviceLostRequest> request(
      new OnDeviceLostRequest{this, aDeviceId});
  ffi::WGPUDeviceLostClosure closure = {
      &DeviceLostCallback, &DeviceLostCleanupCallback,
      reinterpret_cast<uint8_t*>(request.release())};
  ffi::wgpu_server_set_device_lost_callback(mContext.get(), aDeviceId, closure);

#if defined(XP_WIN)
  HANDLE handle =
      wgpu_server_get_device_fence_handle(mContext.get(), aDeviceId);
  if (handle) {
    RefPtr<gfx::FileHandleWrapper> fenceHandle =
        new gfx::FileHandleWrapper(UniqueFileHandle(handle));
    mDeviceFenceHandles.emplace(aDeviceId, std::move(fenceHandle));
  }
#endif

  MOZ_ASSERT(!mActiveDeviceIds.Contains(aDeviceId));
  mActiveDeviceIds.Insert(aDeviceId);
}

void WebGPUParent::PreDeviceDrop(RawId aDeviceId) {
  if (mActiveDeviceIds.Contains(aDeviceId)) {
    mActiveDeviceIds.Remove(aDeviceId);
  }
  mErrorScopeStackByDevice.erase(aDeviceId);
  mLostDeviceIds.Remove(aDeviceId);
}

WebGPUParent::BufferMapData* WebGPUParent::GetBufferMapData(RawId aBufferId) {
  const auto iter = mSharedMemoryMap.find(aBufferId);
  if (iter == mSharedMemoryMap.end()) {
    return nullptr;
  }

  return &iter->second;
}

static const char* MapStatusString(ffi::WGPUBufferMapAsyncStatus status) {
  switch (status) {
    case ffi::WGPUBufferMapAsyncStatus_Success:
      return "Success";
    case ffi::WGPUBufferMapAsyncStatus_AlreadyMapped:
      return "Already mapped";
    case ffi::WGPUBufferMapAsyncStatus_MapAlreadyPending:
      return "Map is already pending";
    case ffi::WGPUBufferMapAsyncStatus_ContextLost:
      return "Context lost";
    case ffi::WGPUBufferMapAsyncStatus_Invalid:
      return "Invalid buffer";
    case ffi::WGPUBufferMapAsyncStatus_InvalidRange:
      return "Invalid range";
    case ffi::WGPUBufferMapAsyncStatus_InvalidAlignment:
      return "Invalid alignment";
    case ffi::WGPUBufferMapAsyncStatus_InvalidUsageFlags:
      return "Invalid usage flags";
    case ffi::WGPUBufferMapAsyncStatus_Error:
      return "Map failed";
    case ffi::WGPUBufferMapAsyncStatus_Sentinel:  // For -Wswitch
      break;
  }

  MOZ_CRASH("Bad ffi::WGPUBufferMapAsyncStatus");
}

void WebGPUParent::MapCallback(uint8_t* aUserData,
                               ffi::WGPUBufferMapAsyncStatus aStatus) {
  auto req =
      std::unique_ptr<MapRequest>(reinterpret_cast<MapRequest*>(aUserData));

  if (!req->mParent) {
    return;
  }
  if (!req->mParent->CanSend()) {
    return;
  }

  ipc::ByteBuf bb;

  if (aStatus != ffi::WGPUBufferMapAsyncStatus_Success) {
    // A buffer map operation that fails with a DeviceError gets
    // mapped to the ContextLost status. If we have this status, we
    // need to lose the device.
    if (aStatus == ffi::WGPUBufferMapAsyncStatus_ContextLost) {
      req->mParent->LoseDevice(
          req->mDeviceId,
          static_cast<uint8_t>(dom::GPUDeviceLostReason::Unknown),
          nsPrintfCString("Buffer %" PRIu64 " invalid", req->mBufferId));
    }
    auto error = nsPrintfCString("Mapping WebGPU buffer failed: %s",
                                 MapStatusString(aStatus));

    ffi::wgpu_server_pack_buffer_map_error(req->mBufferId, &error, ToFFI(&bb));
  } else {
    auto* mapData = req->mParent->GetBufferMapData(req->mBufferId);
    MOZ_RELEASE_ASSERT(mapData);

    auto size = req->mSize;
    auto offset = req->mOffset;

    if (req->mHostMap == ffi::WGPUHostMap_Read && size > 0) {
      ErrorBuffer error;
      const auto src = ffi::wgpu_server_buffer_get_mapped_range(
          req->mParent->GetContext(), mapData->mDeviceId, req->mBufferId,
          offset, size, error.ToFFI());

      MOZ_RELEASE_ASSERT(!error.GetError());

      MOZ_RELEASE_ASSERT(mapData->mShmem.Size() >= offset + size);
      if (src.ptr != nullptr && src.length >= size) {
        auto dst = mapData->mShmem.DataAsSpan<uint8_t>().Subspan(offset, size);
        memcpy(dst.data(), src.ptr, size);
      }
    }

    bool is_writable = req->mHostMap == ffi::WGPUHostMap_Write;
    ffi::wgpu_server_pack_buffer_map_success(req->mBufferId, is_writable,
                                             offset, size, ToFFI(&bb));

    mapData->mMappedOffset = offset;
    mapData->mMappedSize = size;
  }

  if (!req->mParent->SendServerMessage(std::move(bb))) {
    NS_ERROR("SendServerMessage failed");
  }
}

void WebGPUParent::BufferUnmap(RawId aDeviceId, RawId aBufferId, bool aFlush) {
  MOZ_LOG(sLogger, LogLevel::Info,
          ("RecvBufferUnmap %" PRIu64 " flush=%d\n", aBufferId, aFlush));

  auto* mapData = GetBufferMapData(aBufferId);

  if (mapData && aFlush) {
    uint64_t offset = mapData->mMappedOffset;
    uint64_t size = mapData->mMappedSize;

    ErrorBuffer getRangeError;
    const auto mapped = ffi::wgpu_server_buffer_get_mapped_range(
        mContext.get(), aDeviceId, aBufferId, offset, size,
        getRangeError.ToFFI());
    ForwardError(getRangeError);

    if (mapped.ptr != nullptr && mapped.length >= size) {
      auto shmSize = mapData->mShmem.Size();
      MOZ_RELEASE_ASSERT(offset <= shmSize);
      MOZ_RELEASE_ASSERT(size <= shmSize - offset);

      auto src = mapData->mShmem.DataAsSpan<uint8_t>().Subspan(offset, size);
      memcpy(mapped.ptr, src.data(), size);
    }

    mapData->mMappedOffset = 0;
    mapData->mMappedSize = 0;
  }

  ErrorBuffer unmapError;
  ffi::wgpu_server_buffer_unmap(mContext.get(), aDeviceId, aBufferId,
                                unmapError.ToFFI());
  ForwardError(unmapError);

  if (mapData && !mapData->mHasMapFlags) {
    // We get here if the buffer was mapped at creation without map flags.
    // We don't need the shared memory anymore.
    DeallocBufferShmem(aBufferId);
  }
}

void WebGPUParent::DeallocBufferShmem(RawId aBufferId) {
  const auto iter = mSharedMemoryMap.find(aBufferId);
  if (iter != mSharedMemoryMap.end()) {
    mSharedMemoryMap.erase(iter);
  }
}

void WebGPUParent::RemoveSharedTexture(RawId aTextureId) {
  auto it = mSharedTextures.find(aTextureId);
  if (it != mSharedTextures.end()) {
    mSharedTextures.erase(it);
  }
}

const ExternalTextureSourceHost& WebGPUParent::GetExternalTextureSource(
    ffi::WGPUExternalTextureSourceId aId) const {
  return mExternalTextureSources.at(aId);
}

void WebGPUParent::DestroyExternalTextureSource(RawId aSourceId) {
  auto it = mExternalTextureSources.find(aSourceId);
  if (it != mExternalTextureSources.end()) {
    for (const auto textureId : it->second.TextureIds()) {
      ffi::wgpu_server_texture_destroy(mContext.get(), textureId);
    }
  }
}

void WebGPUParent::DropExternalTextureSource(RawId aSourceId) {
  auto it = mExternalTextureSources.find(aSourceId);
  if (it != mExternalTextureSources.end()) {
    for (const auto viewId : it->second.ViewIds()) {
      ffi::wgpu_server_texture_view_drop(mContext.get(), viewId);
    }
    for (const auto textureId : it->second.TextureIds()) {
      ffi::wgpu_server_texture_drop(mContext.get(), textureId);
    }
    mExternalTextureSources.erase(it);
  }
}

void WebGPUParent::QueueSubmit(RawId aQueueId, RawId aDeviceId,
                               Span<const RawId> aCommandBuffers,
                               Span<const RawId> aTextureIds) {
  for (const auto& textureId : aTextureIds) {
    auto it = mSharedTextures.find(textureId);
    if (it != mSharedTextures.end()) {
      auto& sharedTexture = it->second;
      sharedTexture->onBeforeQueueSubmit(aQueueId);
    }
  }

  ErrorBuffer error;
  auto index = ffi::wgpu_server_queue_submit(
      mContext.get(), aDeviceId, aQueueId,
      {aCommandBuffers.Elements(), aCommandBuffers.Length()}, error.ToFFI());
  // Check if index is valid. 0 means error.
  if (index != 0) {
    for (const auto& textureId : aTextureIds) {
      auto it = mSharedTextures.find(textureId);
      if (it != mSharedTextures.end()) {
        auto& sharedTexture = it->second;

        sharedTexture->SetSubmissionIndex(index);
        // Update mLastSubmittedTextureId
        auto ownerId = sharedTexture->GetOwnerId();
        const auto& lookup = mPresentationDataMap.find(ownerId);
        if (lookup != mPresentationDataMap.end()) {
          RefPtr<PresentationData> data = lookup->second.get();
          data->mLastSubmittedTextureId = Some(textureId);
        }
      }
    }
  }
  ForwardError(error);
}

void WebGPUParent::OnSubmittedWorkDoneCallback(uint8_t* userdata) {
  auto req = std::unique_ptr<OnSubmittedWorkDoneRequest>(
      reinterpret_cast<OnSubmittedWorkDoneRequest*>(userdata));
  if (!req->mParent) {
    return;
  }
  if (!req->mParent->CanSend()) {
    return;
  }

  ipc::ByteBuf bb;
  ffi::wgpu_server_pack_work_done(ToFFI(&bb), req->mQueueId);
  if (!req->mParent->SendServerMessage(std::move(bb))) {
    NS_ERROR("SendServerMessage failed");
  }
}

// TODO: proper destruction

void WebGPUParent::DeviceCreateSwapChain(
    RawId aDeviceId, RawId aQueueId, const RGBDescriptor& aDesc,
    const nsTArray<RawId>& aBufferIds,
    const layers::RemoteTextureOwnerId& aOwnerId,
    bool aUseSharedTextureInSwapChain) {
  switch (aDesc.format()) {
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid surface format!");
      return;
  }

  const auto bufferStrideWithMask =
      Device::BufferStrideWithMask(aDesc.size(), aDesc.format());
  if (!bufferStrideWithMask.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Invalid width / buffer stride!");
    return;
  }

  constexpr uint32_t kBufferAlignmentMask = 0xff;
  const uint32_t bufferStride =
      bufferStrideWithMask.value() & ~kBufferAlignmentMask;

  const auto rows = CheckedInt<uint32_t>(aDesc.size().height);
  if (!rows.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Invalid height!");
    return;
  }

  if (!mRemoteTextureOwner) {
    mRemoteTextureOwner =
        MakeRefPtr<layers::RemoteTextureOwnerClient>(OtherPid());
  }
  mRemoteTextureOwner->RegisterTextureOwner(aOwnerId);

  auto data = MakeRefPtr<PresentationData>(this, aUseSharedTextureInSwapChain,
                                           aDeviceId, aQueueId, aDesc,
                                           bufferStride, aBufferIds);
  if (!mPresentationDataMap.emplace(aOwnerId, data).second) {
    NS_ERROR("External image is already registered as WebGPU canvas!");
  }
}

struct ReadbackPresentRequest {
  ReadbackPresentRequest(
      const ffi::WGPUGlobal* aContext, RefPtr<PresentationData>& aData,
      RefPtr<layers::RemoteTextureOwnerClient>& aRemoteTextureOwner,
      const layers::RemoteTextureId aTextureId,
      const layers::RemoteTextureOwnerId aOwnerId)
      : mContext(aContext),
        mData(aData),
        mRemoteTextureOwner(aRemoteTextureOwner),
        mTextureId(aTextureId),
        mOwnerId(aOwnerId) {}

  const ffi::WGPUGlobal* mContext;
  RefPtr<PresentationData> mData;
  RefPtr<layers::RemoteTextureOwnerClient> mRemoteTextureOwner;
  const layers::RemoteTextureId mTextureId;
  const layers::RemoteTextureOwnerId mOwnerId;
};

static void ReadbackPresentCallback(uint8_t* userdata,
                                    ffi::WGPUBufferMapAsyncStatus status) {
  UniquePtr<ReadbackPresentRequest> req(
      reinterpret_cast<ReadbackPresentRequest*>(userdata));

  const auto onExit = mozilla::MakeScopeExit([&]() {
    auto& waitingTextures = req->mData->mWaitingReadbackTexturesForPresent;
    auto it = waitingTextures.find(req->mTextureId);
    MOZ_ASSERT(it != waitingTextures.end());
    if (it != waitingTextures.end()) {
      waitingTextures.erase(it);
    }
    if (req->mData->mPendingSwapChainDrop.isSome() && waitingTextures.empty()) {
      if (req->mData->mParent) {
        auto& pendingDrop = req->mData->mPendingSwapChainDrop.ref();
        req->mData->mParent->SwapChainDrop(req->mOwnerId, pendingDrop.mTxnType,
                                           pendingDrop.mTxnId);
        req->mData->mPendingSwapChainDrop = Nothing();
      }
    }
  });

  if (!req->mRemoteTextureOwner->IsRegistered(req->mOwnerId)) {
    // SwapChain is already Destroyed
    return;
  }

  RefPtr<PresentationData> data = req->mData;
  // get the buffer ID
  RawId bufferId;
  {
    bufferId = data->mQueuedBufferIds.back();
    data->mQueuedBufferIds.pop_back();
  }

  // Ensure we'll make the bufferId available for reuse
  data->mAvailableBufferIds.push_back(bufferId);

  MOZ_LOG(sLogger, LogLevel::Info,
          ("ReadbackPresentCallback for buffer %" PRIu64 " status=%d\n",
           bufferId, status));
  // copy the data
  if (status == ffi::WGPUBufferMapAsyncStatus_Success) {
    const auto bufferSize = data->mDesc.size().height * data->mSourcePitch;
    ErrorBuffer getRangeError;
    const auto mapped = ffi::wgpu_server_buffer_get_mapped_range(
        req->mContext, data->mDeviceId, bufferId, 0, bufferSize,
        getRangeError.ToFFI());
    getRangeError.CoerceValidationToInternal();
    if (req->mData->mParent) {
      req->mData->mParent->ForwardError(getRangeError);
    }
    if (auto innerError = getRangeError.GetError()) {
      MOZ_LOG(sLogger, LogLevel::Info,
              ("WebGPU present: buffer get_mapped_range for internal "
               "presentation readback failed: %s\n",
               innerError->message.get()));
      return;
    }

    MOZ_RELEASE_ASSERT(mapped.length >= bufferSize);
    auto textureData =
        req->mRemoteTextureOwner->CreateOrRecycleBufferTextureData(
            data->mDesc.size(), data->mDesc.format(), req->mOwnerId);
    if (!textureData) {
      gfxCriticalNoteOnce << "Failed to allocate BufferTextureData";
      return;
    }
    layers::MappedTextureData mappedData;
    if (textureData && textureData->BorrowMappedData(mappedData)) {
      uint8_t* src = mapped.ptr;
      uint8_t* dst = mappedData.data;
      for (auto row = 0; row < data->mDesc.size().height; ++row) {
        memcpy(dst, src, mappedData.stride);
        dst += mappedData.stride;
        src += data->mSourcePitch;
      }
      req->mRemoteTextureOwner->PushTexture(req->mTextureId, req->mOwnerId,
                                            std::move(textureData));
    } else {
      NS_WARNING("WebGPU present skipped: the swapchain is resized!");
    }
    ErrorBuffer unmapError;
    wgpu_server_buffer_unmap(req->mContext, data->mDeviceId, bufferId,
                             unmapError.ToFFI());
    unmapError.CoerceValidationToInternal();
    if (req->mData->mParent) {
      req->mData->mParent->ForwardError(unmapError);
    }
    if (auto innerError = unmapError.GetError()) {
      MOZ_LOG(sLogger, LogLevel::Info,
              ("WebGPU present: buffer unmap for internal presentation "
               "readback failed: %s\n",
               innerError->message.get()));
    }
  } else {
    // TODO: better handle errors
    NS_WARNING("WebGPU frame mapping failed!");
  }
}

struct ReadbackSnapshotRequest {
  ReadbackSnapshotRequest(const ffi::WGPUGlobal* aContext,
                          RefPtr<PresentationData>& aData,
                          ffi::WGPUBufferId aBufferId,
                          const ipc::Shmem& aDestShmem)
      : mContext(aContext),
        mData(aData),
        mBufferId(aBufferId),
        mDestShmem(aDestShmem) {}

  const ffi::WGPUGlobal* mContext;
  RefPtr<PresentationData> mData;
  const ffi::WGPUBufferId mBufferId;
  const ipc::Shmem& mDestShmem;
};

static void ReadbackSnapshotCallback(uint8_t* userdata,
                                     ffi::WGPUBufferMapAsyncStatus status) {
  UniquePtr<ReadbackSnapshotRequest> req(
      reinterpret_cast<ReadbackSnapshotRequest*>(userdata));

  RefPtr<PresentationData> data = req->mData;
  data->mReadbackSnapshotCallbackCalled = true;

  // Ensure we'll make the bufferId available for reuse
  data->mAvailableBufferIds.push_back(req->mBufferId);

  MOZ_LOG(sLogger, LogLevel::Info,
          ("ReadbackSnapshotCallback for buffer %" PRIu64 " status=%d\n",
           req->mBufferId, status));
  if (status != ffi::WGPUBufferMapAsyncStatus_Success) {
    return;
  }
  // copy the data
  const auto bufferSize = data->mDesc.size().height * data->mSourcePitch;
  ErrorBuffer getRangeError;
  const auto mapped = ffi::wgpu_server_buffer_get_mapped_range(
      req->mContext, data->mDeviceId, req->mBufferId, 0, bufferSize,
      getRangeError.ToFFI());
  getRangeError.CoerceValidationToInternal();
  if (req->mData->mParent) {
    req->mData->mParent->ForwardError(getRangeError);
  }
  if (auto innerError = getRangeError.GetError()) {
    MOZ_LOG(sLogger, LogLevel::Info,
            ("WebGPU present: buffer get_mapped_range for internal "
             "presentation readback failed: %s\n",
             innerError->message.get()));
    return;
  }

  MOZ_RELEASE_ASSERT(mapped.length >= bufferSize);

  uint8_t* src = mapped.ptr;
  uint8_t* dst = req->mDestShmem.get<uint8_t>();
  const uint32_t stride = layers::ImageDataSerializer::ComputeRGBStride(
      gfx::SurfaceFormat::B8G8R8A8, data->mDesc.size().width);

  for (auto row = 0; row < data->mDesc.size().height; ++row) {
    memcpy(dst, src, stride);
    src += data->mSourcePitch;
    dst += stride;
  }

  ErrorBuffer unmapError;
  wgpu_server_buffer_unmap(req->mContext, data->mDeviceId, req->mBufferId,
                           unmapError.ToFFI());
  unmapError.CoerceValidationToInternal();
  if (req->mData->mParent) {
    req->mData->mParent->ForwardError(unmapError);
  }
  if (auto innerError = unmapError.GetError()) {
    MOZ_LOG(sLogger, LogLevel::Info,
            ("WebGPU snapshot: buffer unmap for internal presentation "
             "readback failed: %s\n",
             innerError->message.get()));
  }
}

ipc::IPCResult WebGPUParent::GetFrontBufferSnapshot(
    IProtocol* aProtocol, const layers::RemoteTextureOwnerId& aOwnerId,
    const RawId& aCommandEncoderId, const RawId& aCommandBufferId,
    Maybe<Shmem>& aShmem, gfx::IntSize& aSize, uint32_t& aByteStride) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return IPC_OK();
  }

  RefPtr<PresentationData> data = lookup->second.get();
  data->mReadbackSnapshotCallbackCalled = false;
  aSize = data->mDesc.size();
  uint32_t stride = layers::ImageDataSerializer::ComputeRGBStride(
      data->mDesc.format(), aSize.width);
  aByteStride = stride;
  uint32_t len = data->mDesc.size().height * stride;
  Shmem shmem;
  if (!AllocShmem(len, &shmem)) {
    return IPC_OK();
  }

  if (data->mLastSubmittedTextureId.isNothing()) {
    return IPC_OK();
  }

  auto it = mSharedTextures.find(data->mLastSubmittedTextureId.ref());
  // Shared texture is already invalid and posted to RemoteTextureMap
  if (it == mSharedTextures.end()) {
    if (!mRemoteTextureOwner || !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return IPC_OK();
    }
    if (!data->mUseSharedTextureInSwapChain) {
      ffi::wgpu_server_device_poll(mContext.get(), data->mDeviceId, true);
    }
    mRemoteTextureOwner->GetLatestBufferSnapshot(aOwnerId, shmem, aSize);
    aShmem.emplace(std::move(shmem));
    return IPC_OK();
  }

  // Readback synchronously

  RawId bufferId = 0;
  const auto& size = data->mDesc.size();
  const auto bufferSize = data->mDesc.size().height * data->mSourcePitch;

  // step 1: find an available staging buffer, or create one
  {
    if (!data->mAvailableBufferIds.empty()) {
      bufferId = data->mAvailableBufferIds.back();
      data->mAvailableBufferIds.pop_back();
    } else if (!data->mUnassignedBufferIds.empty()) {
      bufferId = data->mUnassignedBufferIds.back();
      data->mUnassignedBufferIds.pop_back();

      ffi::WGPUBufferUsages usage =
          WGPUBufferUsages_COPY_DST | WGPUBufferUsages_MAP_READ;

      ErrorBuffer error;
      ffi::wgpu_server_device_create_buffer(mContext.get(), data->mDeviceId,
                                            bufferId, nullptr, bufferSize,
                                            usage, false, error.ToFFI());
      if (ForwardError(error)) {
        return IPC_OK();
      }
    } else {
      bufferId = 0;
    }
  }

  MOZ_LOG(sLogger, LogLevel::Info,
          ("GetFrontBufferSnapshot with buffer %" PRIu64 "\n", bufferId));
  if (!bufferId) {
    // TODO: add a warning - no buffer are available!
    return IPC_OK();
  }

  // step 3: submit a copy command for the frame
  ffi::WGPUCommandEncoderDescriptor encoderDesc = {};
  {
    ErrorBuffer error;
    ffi::wgpu_server_device_create_encoder(mContext.get(), data->mDeviceId,
                                           &encoderDesc, aCommandEncoderId,
                                           error.ToFFI());
    if (ForwardError(error)) {
      return IPC_OK();
    }
  }

  if (data->mLastSubmittedTextureId.isNothing()) {
    return IPC_OK();
  }

  const ffi::WGPUTexelCopyTextureInfo texView = {
      data->mLastSubmittedTextureId.ref(),
  };
  const ffi::WGPUTexelCopyBufferLayout bufLayout = {
      0,
      &data->mSourcePitch,
      nullptr,
  };
  const ffi::WGPUExtent3d extent = {
      static_cast<uint32_t>(size.width),
      static_cast<uint32_t>(size.height),
      1,
  };

  {
    ErrorBuffer error;
    ffi::wgpu_server_encoder_copy_texture_to_buffer(
        mContext.get(), data->mDeviceId, aCommandEncoderId, &texView, bufferId,
        &bufLayout, &extent, error.ToFFI());
    if (ForwardError(error)) {
      return IPC_OK();
    }
  }
  ffi::WGPUCommandBufferDescriptor commandDesc = {};
  {
    ErrorBuffer error;
    ffi::wgpu_server_encoder_finish(mContext.get(), data->mDeviceId,
                                    aCommandEncoderId, aCommandBufferId,
                                    &commandDesc, error.ToFFI());
    if (ForwardError(error)) {
      ffi::wgpu_server_command_encoder_drop(mContext.get(), aCommandEncoderId);
      ffi::wgpu_server_command_buffer_drop(mContext.get(), aCommandBufferId);
      return IPC_OK();
    }
  }

  {
    ErrorBuffer error;
    ffi::wgpu_server_queue_submit(mContext.get(), data->mDeviceId,
                                  data->mQueueId, {&aCommandBufferId, 1},
                                  error.ToFFI());
    ffi::wgpu_server_command_encoder_drop(mContext.get(), aCommandEncoderId);
    ffi::wgpu_server_command_buffer_drop(mContext.get(), aCommandBufferId);
    if (ForwardError(error)) {
      return IPC_OK();
    }
  }

  auto snapshotRequest = MakeUnique<ReadbackSnapshotRequest>(
      mContext.get(), data, bufferId, shmem);

  ffi::WGPUBufferMapClosure closure = {
      &ReadbackSnapshotCallback,
      reinterpret_cast<uint8_t*>(snapshotRequest.release())};

  ErrorBuffer error;
  ffi::wgpu_server_buffer_map(mContext.get(), data->mDeviceId, bufferId, 0,
                              bufferSize, ffi::WGPUHostMap_Read, closure,
                              error.ToFFI());
  if (ForwardError(error)) {
    return IPC_OK();
  }

  // Callback should be called during the poll.
  ffi::wgpu_server_poll_all_devices(mContext.get(), true);

  // Check if ReadbackSnapshotCallback is called.
  MOZ_RELEASE_ASSERT(data->mReadbackSnapshotCallbackCalled == true);

  aShmem.emplace(std::move(shmem));
  return IPC_OK();
}

void WebGPUParent::PostSharedTexture(
    const std::shared_ptr<SharedTexture>&& aSharedTexture,
    const layers::RemoteTextureId aRemoteTextureId,
    const layers::RemoteTextureOwnerId aOwnerId) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end() || !mRemoteTextureOwner ||
      !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
    NS_WARNING("WebGPU presenting on a destroyed swap chain!");
    return;
  }

  const auto surfaceFormat = gfx::SurfaceFormat::B8G8R8A8;
  const auto size = aSharedTexture->GetSize();

  RefPtr<PresentationData> data = lookup->second.get();

  Maybe<layers::SurfaceDescriptor> desc = aSharedTexture->ToSurfaceDescriptor();
  if (!desc) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  mRemoteTextureOwner->PushTexture(aRemoteTextureId, aOwnerId, aSharedTexture,
                                   size, surfaceFormat, *desc);

  auto recycledTexture = mRemoteTextureOwner->GetRecycledSharedTexture(
      size, surfaceFormat, desc->type(), aOwnerId);
  if (recycledTexture) {
    recycledTexture->CleanForRecycling();
    data->mRecycledSharedTextures.push_back(recycledTexture);
  }
}

RefPtr<gfx::FileHandleWrapper> WebGPUParent::GetDeviceFenceHandle(
    const RawId aDeviceId) {
  auto it = mDeviceFenceHandles.find(aDeviceId);
  if (it == mDeviceFenceHandles.end()) {
    return nullptr;
  }
  return it->second;
}

void WebGPUParent::SwapChainPresent(
    RawId aTextureId, RawId aCommandEncoderId, RawId aCommandBufferId,
    const layers::RemoteTextureId& aRemoteTextureId,
    const layers::RemoteTextureOwnerId& aOwnerId) {
  // step 0: get the data associated with the swapchain
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end() || !mRemoteTextureOwner ||
      !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
    NS_WARNING("WebGPU presenting on a destroyed swap chain!");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  if (data->mUseSharedTextureInSwapChain) {
    auto it = mSharedTextures.find(aTextureId);
    if (it == mSharedTextures.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
    std::shared_ptr<SharedTexture> sharedTexture = it->second;
    mSharedTextures.erase(it);

    MOZ_ASSERT(sharedTexture->GetOwnerId() == aOwnerId);

    PostSharedTexture(std::move(sharedTexture), aRemoteTextureId, aOwnerId);
    return;
  }

  RawId bufferId = 0;
  const auto& size = data->mDesc.size();
  const auto bufferSize = data->mDesc.size().height * data->mSourcePitch;

  // step 1: find an available staging buffer, or create one
  {
    if (!data->mAvailableBufferIds.empty()) {
      bufferId = data->mAvailableBufferIds.back();
      data->mAvailableBufferIds.pop_back();
    } else if (!data->mUnassignedBufferIds.empty()) {
      bufferId = data->mUnassignedBufferIds.back();
      data->mUnassignedBufferIds.pop_back();

      ffi::WGPUBufferUsages usage =
          WGPUBufferUsages_COPY_DST | WGPUBufferUsages_MAP_READ;

      ErrorBuffer error;
      ffi::wgpu_server_device_create_buffer(mContext.get(), data->mDeviceId,
                                            bufferId, nullptr, bufferSize,
                                            usage, false, error.ToFFI());
      if (ForwardError(error)) {
        return;
      }
    } else {
      bufferId = 0;
    }

    if (bufferId) {
      data->mQueuedBufferIds.insert(data->mQueuedBufferIds.begin(), bufferId);
    }
  }

  MOZ_LOG(sLogger, LogLevel::Info,
          ("RecvSwapChainPresent with buffer %" PRIu64 "\n", bufferId));
  if (!bufferId) {
    // TODO: add a warning - no buffer are available!
    return;
  }

  // step 3: submit a copy command for the frame
  ffi::WGPUCommandEncoderDescriptor encoderDesc = {};
  {
    ErrorBuffer error;
    ffi::wgpu_server_device_create_encoder(mContext.get(), data->mDeviceId,
                                           &encoderDesc, aCommandEncoderId,
                                           error.ToFFI());
    if (ForwardError(error)) {
      return;
    }
  }

  const ffi::WGPUTexelCopyTextureInfo texView = {
      aTextureId,
  };
  const ffi::WGPUTexelCopyBufferLayout bufLayout = {
      0,
      &data->mSourcePitch,
      nullptr,
  };
  const ffi::WGPUExtent3d extent = {
      static_cast<uint32_t>(size.width),
      static_cast<uint32_t>(size.height),
      1,
  };

  {
    ErrorBuffer error;
    ffi::wgpu_server_encoder_copy_texture_to_buffer(
        mContext.get(), data->mDeviceId, aCommandEncoderId, &texView, bufferId,
        &bufLayout, &extent, error.ToFFI());
    if (ForwardError(error)) {
      return;
    }
  }
  ffi::WGPUCommandBufferDescriptor commandDesc = {};
  {
    ErrorBuffer error;
    ffi::wgpu_server_encoder_finish(mContext.get(), data->mDeviceId,
                                    aCommandEncoderId, aCommandBufferId,
                                    &commandDesc, error.ToFFI());
    if (ForwardError(error)) {
      ffi::wgpu_server_command_encoder_drop(mContext.get(), aCommandEncoderId);
      ffi::wgpu_server_command_buffer_drop(mContext.get(), aCommandBufferId);
      return;
    }
  }

  {
    ErrorBuffer error;
    ffi::wgpu_server_queue_submit(mContext.get(), data->mDeviceId,
                                  data->mQueueId, {&aCommandBufferId, 1},
                                  error.ToFFI());
    ffi::wgpu_server_command_encoder_drop(mContext.get(), aCommandEncoderId);
    ffi::wgpu_server_command_buffer_drop(mContext.get(), aCommandBufferId);
    if (ForwardError(error)) {
      return;
    }
  }

  auto& waitingTextures = data->mWaitingReadbackTexturesForPresent;
  auto it = waitingTextures.find(aRemoteTextureId);
  MOZ_ASSERT(it == waitingTextures.end());
  if (it == waitingTextures.end()) {
    waitingTextures.emplace(aRemoteTextureId);
  }

  // step 4: request the pixels to be copied into the shared texture
  // TODO: this isn't strictly necessary. When WR wants to Lock() the external
  // texture,
  // we can just give it the contents of the last mapped buffer instead of the
  // copy.
  auto presentRequest = MakeUnique<ReadbackPresentRequest>(
      mContext.get(), data, mRemoteTextureOwner, aRemoteTextureId, aOwnerId);

  ffi::WGPUBufferMapClosure closure = {
      &ReadbackPresentCallback,
      reinterpret_cast<uint8_t*>(presentRequest.release())};

  ErrorBuffer error;
  ffi::wgpu_server_buffer_map(mContext.get(), data->mDeviceId, bufferId, 0,
                              bufferSize, ffi::WGPUHostMap_Read, closure,
                              error.ToFFI());
  if (ForwardError(error)) {
    return;
  }
}

void WebGPUParent::SwapChainDrop(const layers::RemoteTextureOwnerId& aOwnerId,
                                 layers::RemoteTextureTxnType aTxnType,
                                 layers::RemoteTextureTxnId aTxnId) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  MOZ_ASSERT(lookup != mPresentationDataMap.end());
  if (lookup == mPresentationDataMap.end()) {
    NS_WARNING("WebGPU presenting on a destroyed swap chain!");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  auto waitingCount = data->mWaitingReadbackTexturesForPresent.size();
  if (waitingCount > 0) {
    // Defer SwapChainDrop until readback complete
    data->mPendingSwapChainDrop = Some(PendingSwapChainDrop{aTxnType, aTxnId});
    return;
  }

  if (mRemoteTextureOwner) {
    if (aTxnType && aTxnId) {
      mRemoteTextureOwner->WaitForTxn(aOwnerId, aTxnType, aTxnId);
    }
    mRemoteTextureOwner->UnregisterTextureOwner(aOwnerId);
  }

  mPresentationDataMap.erase(lookup);

  for (const auto bid : data->mAvailableBufferIds) {
    ffi::wgpu_server_buffer_drop(mContext.get(), bid);
  }
  for (const auto bid : data->mQueuedBufferIds) {
    ffi::wgpu_server_buffer_drop(mContext.get(), bid);
  }
}

void WebGPUParent::ActorDestroy(ActorDestroyReason aWhy) {
  mTimer.Stop();
  mPresentationDataMap.clear();
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->UnregisterAllTextureOwners();
    mRemoteTextureOwner = nullptr;
  }
  mActiveDeviceIds.Clear();
  ffi::wgpu_server_poll_all_devices(mContext.get(), true);
  mContext = nullptr;
}

ipc::IPCResult WebGPUParent::RecvMessages(
    uint32_t nrOfMessages, ipc::ByteBuf&& aSerializedMessages,
    nsTArray<ipc::ByteBuf>&& aDataBuffers,
    nsTArray<MutableSharedMemoryHandle>&& aShmems) {
  MOZ_ASSERT(mTempMappings.IsEmpty());

  mTempMappings.SetCapacity(aShmems.Length());

  nsTArray<ffi::WGPUFfiSlice_u8> shmem_mappings(aShmems.Length());

  for (const auto& shmem : aShmems) {
    auto mapping = shmem.Map();

    auto* ptr = mapping.DataAs<uint8_t>();
    auto len = mapping.Size();
    ffi::WGPUFfiSlice_u8 byte_slice{ptr, len};
    shmem_mappings.AppendElement(std::move(byte_slice));

    // `aShmem` may be an invalid handle, however this will simply result in an
    // invalid mapping with 0 size, which we use safely.
    mTempMappings.AppendElement(Some(std::move(mapping)));
  }

  ffi::WGPUFfiSlice_ByteBuf data_buffers{ToFFI(aDataBuffers.Elements()),
                                         aDataBuffers.Length()};

  ffi::WGPUFfiSlice_FfiSlice_u8 shmem_mapping_slices{shmem_mappings.Elements(),
                                                     shmem_mappings.Length()};

  ffi::wgpu_server_messages(mContext.get(), nrOfMessages,
                            ToFFI(&aSerializedMessages), data_buffers,
                            shmem_mapping_slices);

  mTempMappings.Clear();

  return IPC_OK();
}

ipc::IPCResult WebGPUParent::RecvCreateExternalTextureSource(
    RawId aDeviceId, RawId aQueueId, RawId aExternalTextureSourceId,
    const ExternalTextureSourceDescriptor& aDesc) {
  MOZ_RELEASE_ASSERT(mExternalTextureSources.find(aExternalTextureSourceId) ==
                     mExternalTextureSources.end());
  mExternalTextureSources.emplace(
      aExternalTextureSourceId,
      ExternalTextureSourceHost::Create(this, aDeviceId, aQueueId, aDesc));

  return IPC_OK();
}

void WebGPUParent::DevicePushErrorScope(RawId aDeviceId,
                                        const dom::GPUErrorFilter aFilter) {
  const auto& itr = mErrorScopeStackByDevice.find(aDeviceId);
  if (itr == mErrorScopeStackByDevice.end()) {
    // Content can cause this simply by destroying a device and then
    // calling `pushErrorScope`.
    return;
  }
  auto& stack = itr->second;

  // Let's prevent `while (true) { pushErrorScope(); }`.
  constexpr size_t MAX_ERROR_SCOPE_STACK_SIZE = 1'000'000;
  if (stack.size() >= MAX_ERROR_SCOPE_STACK_SIZE) {
    nsPrintfCString m("pushErrorScope: Hit MAX_ERROR_SCOPE_STACK_SIZE of %zu",
                      MAX_ERROR_SCOPE_STACK_SIZE);
    ReportError(aDeviceId, dom::GPUErrorFilter::Out_of_memory, m);
    return;
  }

  const auto newScope = ErrorScope{aFilter};
  stack.push_back(newScope);
}

PopErrorScopeResult WebGPUParent::DevicePopErrorScope(RawId aDeviceId) {
  const auto popResult = [&]() {
    const auto& itr = mErrorScopeStackByDevice.find(aDeviceId);
    if (itr == mErrorScopeStackByDevice.end()) {
      // Content can cause this simply by destroying a device and then
      // calling `popErrorScope`.
      return PopErrorScopeResult{PopErrorScopeResultType::DeviceLost};
    }

    auto& stack = itr->second;
    if (!stack.size()) {
      // Content can cause this simply by calling `popErrorScope` when
      // there is no error scope pushed.
      return PopErrorScopeResult{PopErrorScopeResultType::ThrowOperationError,
                                 "popErrorScope on empty stack"_ns};
    }

    const auto& scope = stack.back();
    const auto popLater = MakeScopeExit([&]() { stack.pop_back(); });

    auto ret = PopErrorScopeResult{PopErrorScopeResultType::NoError};
    if (scope.firstMessage) {
      ret.message = *scope.firstMessage;
      switch (scope.filter) {
        case dom::GPUErrorFilter::Validation:
          ret.resultType = PopErrorScopeResultType::ValidationError;
          break;
        case dom::GPUErrorFilter::Out_of_memory:
          ret.resultType = PopErrorScopeResultType::OutOfMemory;
          break;
        case dom::GPUErrorFilter::Internal:
          ret.resultType = PopErrorScopeResultType::InternalError;
          break;
      }
    }
    return ret;
  }();
  return popResult;
}

bool WebGPUParent::UseSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  return data->mUseSharedTextureInSwapChain;
}

void WebGPUParent::DisableSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  if (data->mUseSharedTextureInSwapChain) {
    gfxCriticalNote << "Disable SharedTexture for SwapChain:  "
                    << aSwapChainId._0;
  }

  data->mUseSharedTextureInSwapChain = false;
}

bool WebGPUParent::EnsureSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    struct ffi::WGPUTextureFormat aFormat, ffi::WGPUTextureUsages aUsage) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  RefPtr<PresentationData> data = lookup->second.get();
  if (!data->mUseSharedTextureInSwapChain) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  // Recycled SharedTexture if it exists.
  if (!data->mRecycledSharedTextures.empty()) {
    std::shared_ptr<SharedTexture> texture =
        data->mRecycledSharedTextures.front();
    // Check if the texture is recyclable.
    if (texture->mWidth == aWidth && texture->mHeight == aHeight &&
        texture->mFormat.tag == aFormat.tag && texture->mUsage == aUsage) {
      texture->SetOwnerId(ownerId);
      data->mRecycledSharedTextures.pop_front();
      mSharedTextures.emplace(aTextureId, texture);
      return true;
    }
    data->mRecycledSharedTextures.clear();
  }

  auto sharedTexture = CreateSharedTexture(ownerId, aDeviceId, aTextureId,
                                           aWidth, aHeight, aFormat, aUsage);
  return static_cast<bool>(sharedTexture);
}

void WebGPUParent::EnsureSharedTextureForReadBackPresent(
    ffi::WGPUSwapChainId aSwapChainId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    struct ffi::WGPUTextureFormat aFormat, ffi::WGPUTextureUsages aUsage) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();
  if (data->mUseSharedTextureInSwapChain) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  UniquePtr<SharedTexture> texture =
      SharedTextureReadBackPresent::Create(aWidth, aHeight, aFormat, aUsage);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  texture->SetOwnerId(ownerId);
  std::shared_ptr<SharedTexture> shared(texture.release());
  mSharedTextures[aTextureId] = shared;
}

std::shared_ptr<SharedTexture> WebGPUParent::CreateSharedTexture(
    const layers::RemoteTextureOwnerId& aOwnerId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    ffi::WGPUTextureUsages aUsage) {
  MOZ_RELEASE_ASSERT(mSharedTextures.find(aTextureId) == mSharedTextures.end());

  UniquePtr<SharedTexture> texture =
      SharedTexture::Create(this, aDeviceId, aWidth, aHeight, aFormat, aUsage);
  if (!texture) {
    return nullptr;
  }

  texture->SetOwnerId(aOwnerId);
  std::shared_ptr<SharedTexture> shared(texture.release());
  mSharedTextures.emplace(aTextureId, shared);

  return shared;
}

std::shared_ptr<SharedTexture> WebGPUParent::GetSharedTexture(
    ffi::WGPUTextureId aId) {
  auto it = mSharedTextures.find(aId);
  if (it == mSharedTextures.end()) {
    return nullptr;
  }
  return it->second;
}

#if defined(XP_WIN)
/* static */
Maybe<ffi::WGPUFfiLUID> WebGPUParent::GetCompositorDeviceLuid() {
  const RefPtr<ID3D11Device> d3d11Device =
      gfx::DeviceManagerDx::Get()->GetCompositorDevice();
  if (!d3d11Device) {
    gfxCriticalNoteOnce << "CompositorDevice does not exist";
    return Nothing();
  }

  RefPtr<IDXGIDevice> dxgiDevice;
  d3d11Device->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIAdapter> dxgiAdapter;
  dxgiDevice->GetAdapter(getter_AddRefs(dxgiAdapter));

  DXGI_ADAPTER_DESC desc;
  if (FAILED(dxgiAdapter->GetDesc(&desc))) {
    gfxCriticalNoteOnce << "Failed to get DXGI_ADAPTER_DESC";
    return Nothing();
  }

  return Some(
      ffi::WGPUFfiLUID{desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart});
}
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
VkImageHandle::~VkImageHandle() {
  if (!mParent) {
    return;
  }
  auto* context = mParent->GetContext();
  if (context && mParent->IsDeviceActive(mDeviceId) && mVkImageHandle) {
    wgpu_vkimage_destroy(context, mDeviceId, mVkImageHandle);
  }
  wgpu_vkimage_delete(mVkImageHandle);
}

VkSemaphoreHandle::~VkSemaphoreHandle() {
  if (!mParent) {
    return;
  }
  auto* context = mParent->GetContext();
  if (context && mParent->IsDeviceActive(mDeviceId) && mVkSemaphoreHandle) {
    wgpu_vksemaphore_destroy(context, mDeviceId, mVkSemaphoreHandle);
  }
  wgpu_vksemaphore_delete(mVkSemaphoreHandle);
}
#endif

}  // namespace mozilla::webgpu
