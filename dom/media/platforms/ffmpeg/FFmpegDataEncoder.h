/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGDATAENCODER_H_
#define DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGDATAENCODER_H_

#include "FFmpegLibWrapper.h"
#include "PlatformEncoderModule.h"
#include "SimpleMap.h"
#include "mozilla/ThreadSafety.h"

// This must be the last header included
#include "FFmpegLibs.h"

namespace mozilla {

template <int V>
AVCodecID GetFFmpegEncoderCodecId(CodecType aCodec);

template <>
AVCodecID GetFFmpegEncoderCodecId<LIBAV_VER>(CodecType aCodec);

template <int V>
class FFmpegDataEncoder : public MediaDataEncoder {};

template <>
class FFmpegDataEncoder<LIBAV_VER> : public MediaDataEncoder {
  using DurationMap = SimpleMap<int64_t, int64_t, ThreadSafePolicy>;

 public:
  static AVCodec* FindSoftwareEncoder(const FFmpegLibWrapper* aLib,
                                      AVCodecID aCodecId);
#ifdef MOZ_USE_HWDECODE
  static AVCodec* FindHardwareEncoder(const FFmpegLibWrapper* aLib,
                                      AVCodecID aCodecId);
#endif

  FFmpegDataEncoder(const FFmpegLibWrapper* aLib, AVCodecID aCodecID,
                    const RefPtr<TaskQueue>& aTaskQueue,
                    const EncoderConfig& aConfig);

  /* MediaDataEncoder Methods */
  // All methods run on the task queue, except for GetDescriptionName.
  RefPtr<InitPromise> Init() override = 0;  // Implemented in the sub-classes.
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<EncodePromise> Encode(nsTArray<RefPtr<MediaData>>&& aSamples) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitRate) override;

 protected:
  Result<AVCodecContext*, MediaResult> AllocateCodecContext(bool aHardware);

  // This method copies data from an AVPacket into a newly created MediaRawData.
  // It should serve as the initial step in implementing ToMediaRawData.
  static Result<RefPtr<MediaRawData>, MediaResult> CreateMediaRawData(
      AVPacket* aPacket);

  // Methods only called on mTaskQueue.
  RefPtr<EncodePromise> ProcessEncode(nsTArray<RefPtr<MediaData>>&& aSamples);
  RefPtr<ReconfigurationPromise> ProcessReconfigure(
      const RefPtr<const EncoderConfigurationChangeList>&
          aConfigurationChanges);
  RefPtr<EncodePromise> ProcessDrain();
  RefPtr<ShutdownPromise> ProcessShutdown();
  // Initialize the audio or video-specific members of an encoder instance.
  virtual MediaResult InitEncoder() = 0;

  void SetContextBitrate();

  void ShutdownInternal();
  int OpenCodecContext(const AVCodec* aCodec, AVDictionary** aOptions)
      MOZ_EXCLUDES(sMutex);
  void ReleaseCodecContext() MOZ_EXCLUDES(sMutex);
  bool PrepareFrame();
  void DestroyFrame();
#if LIBAVCODEC_VERSION_MAJOR >= 58
  virtual Result<EncodedData, MediaResult> EncodeInputWithModernAPIs(
      RefPtr<const MediaData> aSample) = 0;
  Result<EncodedData, MediaResult> EncodeWithModernAPIs();
  virtual Result<EncodedData, MediaResult> DrainWithModernAPIs();
#endif
  // Convert an AVPacket to a MediaRawData. This can return nullptr if a packet
  // has been processed by the encoder, but is not to be returned to the caller,
  // because DTX is enabled.
  virtual Result<RefPtr<MediaRawData>, MediaResult> ToMediaRawData(
      AVPacket* aPacket) = 0;
  virtual Result<already_AddRefed<MediaByteBuffer>, MediaResult> GetExtraData(
      AVPacket* aPacket) = 0;

  // This refers to a static FFmpegLibWrapper, so raw pointer is adequate.
  const FFmpegLibWrapper* mLib;
  const AVCodecID mCodecID;
  const RefPtr<TaskQueue> mTaskQueue;

  // set in constructor, modified when parameters change
  EncoderConfig mConfig;

  // mTaskQueue only.
  nsCString mCodecName;
  AVCodecContext* mCodecContext;
  AVFrame* mFrame;
  DurationMap mDurationMap;

  // Provide critical-section for open/close mCodecContext.
  static StaticMutex sMutex;
  const bool mVideoCodec;
};

}  // namespace mozilla

#endif /* DOM_MEDIA_PLATFORMS_FFMPEG_FFMPEGDATAENCODER_H_ */
