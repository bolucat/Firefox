/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteMediaDataEncoderParent.h"

#include "ImageContainer.h"
#include "PEMFactory.h"
#include "RemoteMediaManagerParent.h"
#include "mozilla/dom/WebCodecsUtils.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#define LOG_INTERNAL(level, fmt, ...)                              \
  MOZ_LOG_FMT(sPEMLog, LogLevel::level,                            \
              "[RemoteMediaDataEncoderParent] {}: " fmt, __func__, \
              __VA_ARGS__)
#define LOGE(fmt, ...) LOG_INTERNAL(Error, fmt, __VA_ARGS__)
#define LOGW(fmt, ...) LOG_INTERNAL(Warning, fmt, __VA_ARGS__)
#define LOGD(fmt, ...) LOG_INTERNAL(Debug, fmt, __VA_ARGS__)
#define LOGV(fmt, ...) LOG_INTERNAL(Verbose, fmt, __VA_ARGS__)

#define LOGE_IF(condition, fmt, ...) \
  do {                               \
    if (condition) {                 \
      LOGE(fmt, __VA_ARGS__);        \
    }                                \
  } while (0)

#define AUTO_MARKER(var, postfix) \
  AutoWebCodecsMarker var("RemoteMediaDataEncoderParent", postfix);

RemoteMediaDataEncoderParent::RemoteMediaDataEncoderParent(
    const EncoderConfig& aConfig)
    : ShmemRecycleAllocator(this),
      mBufferRecycleBin(new layers::BufferRecycleBin()),
      mConfig(aConfig) {}

RemoteMediaDataEncoderParent::~RemoteMediaDataEncoderParent() = default;

IPCResult RemoteMediaDataEncoderParent::RecvConstruct(
    ConstructResolver&& aResolver) {
  if (mEncoder) {
    aResolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
    return IPC_OK();
  }

  RefPtr<TaskQueue> taskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_ENCODER),
                        "RemoteMediaDataEncoderParent");

  auto factory = MakeRefPtr<PEMFactory>();
  factory->CreateEncoderAsync(mConfig, taskQueue)
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, resolver = std::move(aResolver)](
                 PlatformEncoderModule::CreateEncoderPromise::
                     ResolveOrRejectValue&& aValue) {
               if (aValue.IsReject()) {
                 resolver(aValue.RejectValue());
                 return;
               }

               if (self->mEncoder) {
                 resolver(MediaResult(NS_ERROR_ALREADY_INITIALIZED, __func__));
                 return;
               }

               self->mEncoder = std::move(aValue.ResolveValue());
               resolver(MediaResult(NS_OK));
             });
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvInit(InitResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  mEncoder->Init()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [encoder = RefPtr{mEncoder}, resolver = std::move(aResolver)](
          MediaDataEncoder::InitPromise::ResolveOrRejectValue&& aValue) {
        if (aValue.IsReject()) {
          resolver(aValue.RejectValue());
          return;
        }

        nsCString hardwareReason;
        bool hardware = encoder->IsHardwareAccelerated(hardwareReason);
        resolver(EncodeInitCompletionIPDL{encoder->GetDescriptionName(),
                                          hardware, std::move(hardwareReason)});
      });
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvEncode(
    const EncodedInputIPDL& aData, EncodeResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  nsTArray<RefPtr<MediaData>> frames;

  if (mConfig.IsAudio() &&
      aData.type() == EncodedInputIPDL::TArrayOfRemoteAudioData) {
    auto remoteAudioArray = aData.get_ArrayOfRemoteAudioData();
    if (remoteAudioArray->IsEmpty()) {
      LOGE("[{}] no audio frames received", fmt::ptr(this));
      aResolver(MediaResult(NS_ERROR_INVALID_ARG, __func__));
      return IPC_OK();
    }

    LOGV("[{}] recv {} audio frames", fmt::ptr(this),
         remoteAudioArray->Count());
    for (size_t i = 0; i < remoteAudioArray->Count(); i++) {
      frames.AppendElement(
          remoteAudioArray->ElementAt(i).downcast<MediaData>());
    }
  } else if (mConfig.IsVideo() &&
             aData.type() == EncodedInputIPDL::TArrayOfRemoteVideoData) {
    auto remoteVideoArray = aData.get_ArrayOfRemoteVideoData();
    if (remoteVideoArray->Array().IsEmpty()) {
      LOGE("[{}] no video frames received", fmt::ptr(this));
      aResolver(MediaResult(NS_ERROR_INVALID_ARG, __func__));
      return IPC_OK();
    }

    LOGV("[{}] recv {} video frames", fmt::ptr(this),
         remoteVideoArray->Array().Length());
    for (size_t i = 0; i < remoteVideoArray->Array().Length(); i++) {
      RefPtr<MediaData> frame;
      auto data = std::move(remoteVideoArray->Array().ElementAt(i));
      if (!data.image().IsEmpty()) {
        AUTO_MARKER(marker, ".RecvEncode.TransferToImage");
        RefPtr<layers::Image> image =
            data.image().TransferToImage(mBufferRecycleBin);
        marker.End();
        LOGE_IF(!image, "[{}] failed to get image from video frame at index {}",
                fmt::ptr(this), i);
        if (image) {
          frame = VideoData::CreateFromImage(
                      data.display(), data.base().offset(), data.base().time(),
                      data.base().duration(), image, data.base().keyframe(),
                      data.base().timecode())
                      .downcast<MediaData>();
        }
      } else {
        LOGW("[{}] empty image in video frame at index {}", fmt::ptr(this), i);
        frame = MakeRefPtr<NullData>(data.base().offset(), data.base().time(),
                                     data.base().duration());
      }

      if (NS_WARN_IF(!frame)) {
        LOGE("[{}] failed to create video frame", fmt::ptr(this));
        aResolver(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
        return IPC_OK();
      }

      frames.AppendElement(std::move(frame));
    }
  } else {
    LOGE("[{}] invalid input data type", fmt::ptr(this));
    aResolver(MediaResult(NS_ERROR_INVALID_ARG, __func__));
    return IPC_OK();
  }

  LOGV("[{}] encoding {} frames", fmt::ptr(this), frames.Length());
  mEncoder->Encode(std::move(frames))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, resolver = std::move(aResolver)](
              MediaDataEncoder::EncodePromise::ResolveOrRejectValue&& aValue) {
            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
              return;
            }

            auto ticket = MakeRefPtr<ShmemRecycleTicket>();
            auto samples = MakeRefPtr<ArrayOfRemoteMediaRawData>();
            if (!samples->Fill(aValue.ResolveValue(), [&](size_t aSize) {
                  return self->AllocateBuffer(aSize, ticket);
                })) {
              self->ReleaseTicket(ticket);
              resolver(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
              return;
            }

            uint32_t ticketId = ++self->mTicketCounter;
            self->mTickets[ticketId] = std::move(ticket);
            resolver(EncodeCompletionIPDL(samples, ticketId));
          });
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvReconfigure(
    EncoderConfigurationChangeList* aConfigurationChanges,
    ReconfigureResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  mEncoder->Reconfigure(aConfigurationChanges)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [encoder = RefPtr{mEncoder}, resolver = std::move(aResolver)](
              MediaDataEncoder::ReconfigurationPromise::ResolveOrRejectValue&&
                  aValue) {
            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
              return;
            }

            resolver(MediaResult(NS_OK));
          });
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvDrain(DrainResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(MediaResult(NS_ERROR_ABORT, __func__));
    return IPC_OK();
  }

  mEncoder->Drain()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, resolver = std::move(aResolver)](
          MediaDataEncoder::EncodePromise::ResolveOrRejectValue&& aValue) {
        if (aValue.IsReject()) {
          resolver(aValue.RejectValue());
          return;
        }

        auto ticket = MakeRefPtr<ShmemRecycleTicket>();
        auto samples = MakeRefPtr<ArrayOfRemoteMediaRawData>();
        if (!samples->Fill(aValue.ResolveValue(), [&](size_t aSize) {
              return self->AllocateBuffer(aSize, ticket);
            })) {
          self->ReleaseTicket(ticket);
          resolver(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
          return;
        }

        uint32_t ticketId = ++self->mTicketCounter;
        self->mTickets[ticketId] = std::move(ticket);
        resolver(EncodeCompletionIPDL(samples, ticketId));
      });
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvReleaseTicket(
    const uint32_t& aTicketId) {
  auto i = mTickets.find(aTicketId);
  if (i != mTickets.end()) {
    ReleaseTicket(i->second.get());
    mTickets.erase(i);
  }
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvShutdown(
    ShutdownResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(false);
    return IPC_OK();
  }

  mEncoder->Shutdown()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [resolver = std::move(aResolver)](
          const ShutdownPromise::ResolveOrRejectValue& aValue) {
        resolver(aValue.IsResolve());
      });
  mEncoder = nullptr;
  return IPC_OK();
}

IPCResult RemoteMediaDataEncoderParent::RecvSetBitrate(
    const uint32_t& aBitrate, SetBitrateResolver&& aResolver) {
  if (!mEncoder) {
    aResolver(NS_ERROR_ABORT);
    return IPC_OK();
  }

  mEncoder->SetBitrate(aBitrate)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [encoder = RefPtr{mEncoder}, resolver = std::move(aResolver)](
          GenericPromise::ResolveOrRejectValue&& aValue) {
        resolver(aValue.IsResolve() ? NS_OK : aValue.RejectValue());
      });
  return IPC_OK();
}

void RemoteMediaDataEncoderParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (mEncoder) {
    mEncoder->Shutdown();
    mEncoder = nullptr;
  }

  for (auto& i : mTickets) {
    ReleaseTicket(i.second.get());
  }
  mTickets.clear();

  CleanupShmemRecycleAllocator();
}

#undef LOGE
#undef LOGW
#undef LOGD
#undef LOGV
#undef LOGE_IF
#undef LOG_INTERNAL
#undef AUTO_MARKER

}  // namespace mozilla
