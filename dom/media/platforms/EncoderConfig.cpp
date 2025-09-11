/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EncoderConfig.h"

#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "VPXDecoder.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageUtils.h"

namespace mozilla {

nsCString EncoderConfig::ToString() const {
  nsCString rv(EnumValueToString(mCodec));
  rv.AppendLiteral(mBitrateMode == BitrateMode::Constant ? " (CBR)" : " (VBR)");
  rv.AppendPrintf("%" PRIu32 "bps", mBitrate);
  if (mUsage == Usage::Realtime) {
    rv.AppendLiteral(", realtime");
  } else {
    rv.AppendLiteral(", record");
  }
  if (IsVideo()) {
    rv.AppendPrintf(" [%dx%d]", mSize.Width(), mSize.Height());
    if (mHardwarePreference == HardwarePreference::RequireHardware) {
      rv.AppendLiteral(", hw required");
    } else if (mHardwarePreference == HardwarePreference::RequireSoftware) {
      rv.AppendLiteral(", sw required");
    } else {
      rv.AppendLiteral(", hw: no preference");
    }
    rv.AppendPrintf(", %s", mFormat.ToString().get());
    if (mScalabilityMode == ScalabilityMode::L1T2) {
      rv.AppendLiteral(", L1T2");
    } else if (mScalabilityMode == ScalabilityMode::L1T3) {
      rv.AppendLiteral(", L1T3");
    }
    rv.AppendPrintf(", %" PRIu8 " fps", mFramerate);
    rv.AppendPrintf(", kf interval: %zu", mKeyframeInterval);
  } else {
    MOZ_ASSERT(IsAudio());
    rv.AppendPrintf(", ch: %" PRIu32 ", %" PRIu32 "Hz", mNumberOfChannels,
                    mSampleRate);
  }
  const char* specificStr = "";
  if (mCodecSpecific.is<void_t>()) {
    specificStr = "o";
  } else if (mCodecSpecific.is<H264Specific>()) {
    specificStr = " H264";
  } else if (mCodecSpecific.is<OpusSpecific>()) {
    specificStr = " Opus";
  } else if (mCodecSpecific.is<VP8Specific>()) {
    specificStr = " VP8";
  } else if (mCodecSpecific.is<VP9Specific>()) {
    specificStr = " VP9";
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected codec specific type");
    specificStr = " unknown";
  }
  rv.AppendPrintf(" (w/%s codec specific)", specificStr);
  return rv;
};

const char* ColorRangeToString(const gfx::ColorRange& aColorRange) {
  switch (aColorRange) {
    case gfx::ColorRange::FULL:
      return "FULL";
    case gfx::ColorRange::LIMITED:
      return "LIMITED";
  }
  MOZ_ASSERT_UNREACHABLE("unknown ColorRange");
  return "unknown";
}

const char* YUVColorSpaceToString(const gfx::YUVColorSpace& aYUVColorSpace) {
  switch (aYUVColorSpace) {
    case gfx::YUVColorSpace::BT601:
      return "BT601";
    case gfx::YUVColorSpace::BT709:
      return "BT709";
    case gfx::YUVColorSpace::BT2020:
      return "BT2020";
    case gfx::YUVColorSpace::Identity:
      return "Identity";
  }
  MOZ_ASSERT_UNREACHABLE("unknown YUVColorSpace");
  return "unknown";
}

const char* ColorSpace2ToString(const gfx::ColorSpace2& aColorSpace2) {
  switch (aColorSpace2) {
    case gfx::ColorSpace2::Display:
      return "Display";
    case gfx::ColorSpace2::SRGB:
      return "SRGB";
    case gfx::ColorSpace2::DISPLAY_P3:
      return "DISPLAY_P3";
    case gfx::ColorSpace2::BT601_525:
      return "BT601_525";
    case gfx::ColorSpace2::BT709:
      return "BT709";
    case gfx::ColorSpace2::BT2020:
      return "BT2020";
  }
  MOZ_ASSERT_UNREACHABLE("unknown ColorSpace2");
  return "unknown";
}

const char* TransferFunctionToString(
    const gfx::TransferFunction& aTransferFunction) {
  switch (aTransferFunction) {
    case gfx::TransferFunction::BT709:
      return "BT709";
    case gfx::TransferFunction::SRGB:
      return "SRGB";
    case gfx::TransferFunction::PQ:
      return "PQ";
    case gfx::TransferFunction::HLG:
      return "HLG";
  }
  MOZ_ASSERT_UNREACHABLE("unknown TransferFunction");
  return "unknown";
}

nsCString EncoderConfig::VideoColorSpace::ToString() const {
  nsCString ret;
  ret.AppendFmt(
      "VideoColorSpace: [range: {}, matrix: {}, primaries: {}, transfer: {}]",
      mRange ? ColorRangeToString(mRange.value()) : "none",
      mMatrix ? YUVColorSpaceToString(mMatrix.value()) : "none",
      mPrimaries ? ColorSpace2ToString(mPrimaries.value()) : "none",
      mTransferFunction ? TransferFunctionToString(mTransferFunction.value())
                        : "none");
  return ret;
}

nsCString EncoderConfig::SampleFormat::ToString() const {
  return nsPrintfCString("SampleFormat - [PixelFormat: %s, %s]",
                         dom::GetEnumString(mPixelFormat).get(),
                         mColorSpace.ToString().get());
}

Result<EncoderConfig::SampleFormat, MediaResult>
EncoderConfig::SampleFormat::FromImage(layers::Image* aImage) {
  if (!aImage) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "No image"));
  }

  const dom::ImageUtils imageUtils(aImage);
  Maybe<dom::ImageBitmapFormat> format = imageUtils.GetFormat();
  if (format.isNothing()) {
    return Err(
        MediaResult(NS_ERROR_NOT_IMPLEMENTED,
                    nsPrintfCString("unsupported image format: %d",
                                    static_cast<int>(aImage->GetFormat()))));
  }

  if (layers::PlanarYCbCrImage* image = aImage->AsPlanarYCbCrImage()) {
    if (const layers::PlanarYCbCrImage::Data* yuv = image->GetData()) {
      return EncoderConfig::SampleFormat(
          format.ref(), EncoderConfig::VideoColorSpace(
                            yuv->mColorRange, yuv->mYUVColorSpace,
                            yuv->mColorPrimaries, yuv->mTransferFunction));
    }
    return Err(MediaResult(NS_ERROR_UNEXPECTED,
                           "failed to get YUV data from a YUV image"));
  }

  return EncoderConfig::SampleFormat(format.ref());
}

}  // namespace mozilla
