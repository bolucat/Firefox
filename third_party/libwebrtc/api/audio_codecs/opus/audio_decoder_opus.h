/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_AUDIO_CODECS_OPUS_AUDIO_DECODER_OPUS_H_
#define API_AUDIO_CODECS_OPUS_AUDIO_DECODER_OPUS_H_

#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "api/audio_codecs/audio_codec_pair_id.h"
#include "api/audio_codecs/audio_decoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

// Opus decoder API for use as a template parameter to
// CreateAudioDecoderFactory<...>().
struct RTC_EXPORT AudioDecoderOpus {
  struct Config {
    bool IsOk() const;  // Checks if the values are currently OK.
    int sample_rate_hz = 48000;
    int num_channels = 1;
  };
  static absl::optional<Config> SdpToConfig(const SdpAudioFormat& audio_format);
  static void AppendSupportedDecoders(std::vector<AudioCodecSpec>* specs);

  [[deprecated("bugs.webrtc.org/356878416")]]  //
  static std::unique_ptr<AudioDecoder>
  MakeAudioDecoder(
      Config config,
      absl::optional<AudioCodecPairId> codec_pair_id = absl::nullopt,
      const FieldTrialsView* field_trials = nullptr);

  static std::unique_ptr<AudioDecoder> MakeAudioDecoder(const Environment& env,
                                                        Config config);
};

}  // namespace webrtc

#endif  // API_AUDIO_CODECS_OPUS_AUDIO_DECODER_OPUS_H_
