/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MFTEncoder.h"

#include <comdef.h>

#include "WMFUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/WindowsProcessMitigations.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "mozilla/mscom/COMWrappers.h"
#include "mozilla/mscom/Utils.h"

using Microsoft::WRL::ComPtr;

// Missing from MinGW.
#ifndef CODECAPI_AVEncAdaptiveMode
#  define STATIC_CODECAPI_AVEncAdaptiveMode \
    0x4419b185, 0xda1f, 0x4f53, 0xbc, 0x76, 0x9, 0x7d, 0xc, 0x1e, 0xfb, 0x1e
DEFINE_CODECAPI_GUID(AVEncAdaptiveMode, "4419b185-da1f-4f53-bc76-097d0c1efb1e",
                     0x4419b185, 0xda1f, 0x4f53, 0xbc, 0x76, 0x9, 0x7d, 0xc,
                     0x1e, 0xfb, 0x1e)
#  define CODECAPI_AVEncAdaptiveMode \
    DEFINE_CODECAPI_GUIDNAMED(AVEncAdaptiveMode)
#endif
#ifndef MF_E_NO_EVENTS_AVAILABLE
#  define MF_E_NO_EVENTS_AVAILABLE _HRESULT_TYPEDEF_(0xC00D3E80L)
#endif

#define MFT_LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(mozilla::sPEMLog, LogLevel::level, (msg, ##__VA_ARGS__))

#define MFT_ENC_LOG(level, msg, ...)                                    \
  MFT_LOG_INTERNAL(level, "MFTEncoder(0x%p)::%s: " msg, this, __func__, \
                   ##__VA_ARGS__)
#define MFT_ENC_SLOG(level, msg, ...) \
  MFT_LOG_INTERNAL(level, "MFTEncoder::%s: " msg, __func__, ##__VA_ARGS__)

#define MFT_ENC_LOGD(msg, ...) MFT_ENC_LOG(Debug, msg, ##__VA_ARGS__)
#define MFT_ENC_LOGE(msg, ...) MFT_ENC_LOG(Error, msg, ##__VA_ARGS__)
#define MFT_ENC_LOGW(msg, ...) MFT_ENC_LOG(Warning, msg, ##__VA_ARGS__)
#define MFT_ENC_LOGV(msg, ...) MFT_ENC_LOG(Verbose, msg, ##__VA_ARGS__)

#define MFT_ENC_SLOGD(msg, ...) MFT_ENC_SLOG(Debug, msg, ##__VA_ARGS__)
#define MFT_ENC_SLOGE(msg, ...) MFT_ENC_SLOG(Error, msg, ##__VA_ARGS__)
#define MFT_ENC_SLOGW(msg, ...) MFT_ENC_SLOG(Warning, msg, ##__VA_ARGS__)
#define MFT_ENC_SLOGV(msg, ...) MFT_ENC_SLOG(Verbose, msg, ##__VA_ARGS__)

#undef MFT_RETURN_IF_FAILED_IMPL
#define MFT_RETURN_IF_FAILED_IMPL(x, log_macro)                            \
  do {                                                                     \
    HRESULT rv = x;                                                        \
    if (MOZ_UNLIKELY(FAILED(rv))) {                                        \
      _com_error error(rv);                                                \
      log_macro("(" #x ") failed, rv=%lx(%ls)", rv, error.ErrorMessage()); \
      return rv;                                                           \
    }                                                                      \
  } while (false)

#undef MFT_RETURN_IF_FAILED
#define MFT_RETURN_IF_FAILED(x) MFT_RETURN_IF_FAILED_IMPL(x, MFT_ENC_LOGE)

#undef MFT_RETURN_IF_FAILED_S
#define MFT_RETURN_IF_FAILED_S(x) MFT_RETURN_IF_FAILED_IMPL(x, MFT_ENC_SLOGE)

#undef MFT_RETURN_VALUE_IF_FAILED_IMPL
#define MFT_RETURN_VALUE_IF_FAILED_IMPL(x, ret, log_macro)                 \
  do {                                                                     \
    HRESULT rv = x;                                                        \
    if (MOZ_UNLIKELY(FAILED(rv))) {                                        \
      _com_error error(rv);                                                \
      log_macro("(" #x ") failed, rv=%lx(%ls)", rv, error.ErrorMessage()); \
      return ret;                                                          \
    }                                                                      \
  } while (false)

#undef MFT_RETURN_VALUE_IF_FAILED
#define MFT_RETURN_VALUE_IF_FAILED(x, r) \
  MFT_RETURN_VALUE_IF_FAILED_IMPL(x, r, MFT_ENC_LOGE)

#undef MFT_RETURN_VALUE_IF_FAILED_S
#define MFT_RETURN_VALUE_IF_FAILED_S(x, r) \
  MFT_RETURN_VALUE_IF_FAILED_IMPL(x, r, MFT_ENC_SLOGE)

#undef MFT_RETURN_ERROR_IF_FAILED_IMPL
#define MFT_RETURN_ERROR_IF_FAILED_IMPL(x, log_macro)                      \
  do {                                                                     \
    HRESULT rv = x;                                                        \
    if (MOZ_UNLIKELY(FAILED(rv))) {                                        \
      _com_error error(rv);                                                \
      log_macro("(" #x ") failed, rv=%lx(%ls)", rv, error.ErrorMessage()); \
      return Err(rv);                                                      \
    }                                                                      \
  } while (false)

#undef MFT_RETURN_ERROR_IF_FAILED
#define MFT_RETURN_ERROR_IF_FAILED(x) \
  MFT_RETURN_ERROR_IF_FAILED_IMPL(x, MFT_ENC_LOGE)

#undef MFT_RETURN_ERROR_IF_FAILED_S
#define MFT_RETURN_ERROR_IF_FAILED_S(x) \
  MFT_RETURN_ERROR_IF_FAILED_IMPL(x, MFT_ENC_SLOGE)

#define AUTO_MFTENCODER_MARKER(desc) AUTO_WEBCODECS_MARKER("MFTEncoder", desc);

namespace mozilla {
extern LazyLogModule sPEMLog;

static const char* ErrorStr(HRESULT hr) {
  switch (hr) {
    case S_OK:
      return "OK";
    case MF_E_INVALIDMEDIATYPE:
      return "INVALIDMEDIATYPE";
    case MF_E_INVALIDSTREAMNUMBER:
      return "INVALIDSTREAMNUMBER";
    case MF_E_INVALIDTYPE:
      return "INVALIDTYPE";
    case MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING:
      return "TRANSFORM_PROCESSING";
    case MF_E_TRANSFORM_ASYNC_LOCKED:
      return "TRANSFORM_ASYNC_LOCKED";
    case MF_E_TRANSFORM_NEED_MORE_INPUT:
      return "TRANSFORM_NEED_MORE_INPUT";
    case MF_E_TRANSFORM_STREAM_CHANGE:
      return "TRANSFORM_STREAM_CHANGE";
    case MF_E_TRANSFORM_TYPE_NOT_SET:
      return "TRANSFORM_TYPE_NO_SET";
    case MF_E_UNSUPPORTED_D3D_TYPE:
      return "UNSUPPORTED_D3D_TYPE";
    case E_INVALIDARG:
      return "INVALIDARG";
    case MF_E_MULTIPLE_SUBSCRIBERS:
      return "MULTIPLE_SUBSCRIBERS";
    case MF_E_NO_EVENTS_AVAILABLE:
      return "NO_EVENTS_AVAILABLE";
    case MF_E_NO_SAMPLE_DURATION:
      return "NO_SAMPLE_DURATION";
    case MF_E_NO_SAMPLE_TIMESTAMP:
      return "NO_SAMPLE_TIMESTAMP";
    case MF_E_NOTACCEPTING:
      return "NOTACCEPTING";
    case MF_E_ATTRIBUTENOTFOUND:
      return "NOTFOUND";
    case MF_E_BUFFERTOOSMALL:
      return "BUFFERTOOSMALL";
    case E_NOTIMPL:
      return "NOTIMPL";
    default:
      return "OTHER";
  }
}

static const char* MediaEventTypeStr(MediaEventType aType) {
#define ENUM_TO_STR(enumVal) \
  case enumVal:              \
    return #enumVal
  switch (aType) {
    ENUM_TO_STR(MEUnknown);
    ENUM_TO_STR(METransformUnknown);
    ENUM_TO_STR(METransformNeedInput);
    ENUM_TO_STR(METransformHaveOutput);
    ENUM_TO_STR(METransformDrainComplete);
    ENUM_TO_STR(METransformMarker);
    ENUM_TO_STR(METransformInputStreamStateChanged);
    default:
      break;
  }
  return "Unknown MediaEventType";

#undef ENUM_TO_STR
}

static nsCString ErrorMessage(HRESULT hr) {
  nsCString msg(ErrorStr(hr));
  _com_error err(hr);
  msg.AppendFmt(" ({})", NS_ConvertUTF16toUTF8(err.ErrorMessage()).get());
  return msg;
}

static const char* CodecStr(const GUID& aGUID) {
  if (IsEqualGUID(aGUID, MFVideoFormat_H264)) {
    return "H.264";
  } else if (IsEqualGUID(aGUID, MFVideoFormat_VP80)) {
    return "VP8";
  } else if (IsEqualGUID(aGUID, MFVideoFormat_VP90)) {
    return "VP9";
  } else {
    return "Unsupported codec";
  }
}

static Result<nsCString, HRESULT> GetStringFromAttributes(
    IMFAttributes* aAttributes, REFGUID aGuidKey) {
  UINT32 len = 0;
  MFT_RETURN_ERROR_IF_FAILED_S(aAttributes->GetStringLength(aGuidKey, &len));

  nsCString str;
  if (len > 0) {
    ++len;  // '\0'.
    WCHAR buffer[len];
    MFT_RETURN_ERROR_IF_FAILED_S(
        aAttributes->GetString(aGuidKey, buffer, len, nullptr));
    str.Append(NS_ConvertUTF16toUTF8(buffer));
  }

  return str;
}

static Result<nsCString, HRESULT> GetFriendlyName(IMFActivate* aActivate) {
  return GetStringFromAttributes(aActivate, MFT_FRIENDLY_NAME_Attribute)
      .map([](const nsCString& aName) {
        return aName.IsEmpty() ? "Unknown MFT"_ns : aName;
      });
}

static Result<MFTEncoder::Factory::Provider, HRESULT> GetHardwareVendor(
    IMFActivate* aActivate) {
  nsCString vendor = MOZ_TRY(GetStringFromAttributes(
      aActivate, MFT_ENUM_HARDWARE_VENDOR_ID_Attribute));

  if (vendor == "VEN_1002"_ns) {
    return MFTEncoder::Factory::Provider::HW_AMD;
  } else if (vendor == "VEN_10DE"_ns) {
    return MFTEncoder::Factory::Provider::HW_NVIDIA;
  } else if (vendor == "VEN_8086"_ns) {
    return MFTEncoder::Factory::Provider::HW_Intel;
  } else if (vendor == "VEN_QCOM"_ns) {
    return MFTEncoder::Factory::Provider::HW_Qualcomm;
  }

  MFT_ENC_SLOGD("Undefined hardware vendor id: %s", vendor.get());
  return MFTEncoder::Factory::Provider::HW_Unknown;
}

static Result<nsTArray<ComPtr<IMFActivate>>, HRESULT> EnumMFT(
    GUID aCategory, UINT32 aFlags, const MFT_REGISTER_TYPE_INFO* aInType,
    const MFT_REGISTER_TYPE_INFO* aOutType) {
  nsTArray<ComPtr<IMFActivate>> activates;

  IMFActivate** enumerated;
  UINT32 num = 0;
  MFT_RETURN_ERROR_IF_FAILED_S(
      wmf::MFTEnumEx(aCategory, aFlags, aInType, aOutType, &enumerated, &num));
  for (UINT32 i = 0; i < num; ++i) {
    activates.AppendElement(ComPtr<IMFActivate>(enumerated[i]));
    // MFTEnumEx increments the reference count for each IMFActivate; decrement
    // here so ComPtr manages the lifetime correctly
    enumerated[i]->Release();
  }
  if (enumerated) {
    mscom::wrapped::CoTaskMemFree(enumerated);
  }
  return activates;
}

MFTEncoder::Factory::Factory(Provider aProvider,
                             ComPtr<IMFActivate>&& aActivate)
    : mProvider(aProvider), mActivate(std::move(aActivate)) {
  mName = mozilla::GetFriendlyName(mActivate.Get()).unwrapOr("Unknown"_ns);
}

MFTEncoder::Factory::~Factory() { Shutdown(); }

HRESULT MFTEncoder::Factory::Shutdown() {
  HRESULT hr = S_OK;
  if (mActivate) {
    MFT_ENC_LOGE("Shutdown %s encoder %s",
                 MFTEncoder::Factory::EnumValueToString(mProvider),
                 mName.get());
    // Release MFT resources via activation object.
    hr = mActivate->ShutdownObject();
    if (FAILED(hr)) {
      MFT_ENC_LOGE("Failed to shutdown MFT: %s", ErrorStr(hr));
    }
  }
  mActivate.Reset();
  mName.Truncate();
  return hr;
}

static nsTArray<MFTEncoder::Factory> IntoFactories(
    nsTArray<ComPtr<IMFActivate>>&& aActivates, bool aIsHardware) {
  nsTArray<MFTEncoder::Factory> factories;
  for (auto& activate : aActivates) {
    if (activate) {
      MFTEncoder::Factory::Provider provider =
          aIsHardware ? GetHardwareVendor(activate.Get())
                            .unwrapOr(MFTEncoder::Factory::Provider::HW_Unknown)
                      : MFTEncoder::Factory::Provider::SW;
      factories.AppendElement(
          MFTEncoder::Factory(provider, std::move(activate)));
    }
  }
  return factories;
}

static nsTArray<MFTEncoder::Factory> EnumEncoders(
    const GUID& aSubtype, const MFTEncoder::HWPreference aHWPreference) {
  MFT_REGISTER_TYPE_INFO inType = {.guidMajorType = MFMediaType_Video,
                                   .guidSubtype = MFVideoFormat_NV12};
  MFT_REGISTER_TYPE_INFO outType = {.guidMajorType = MFMediaType_Video,
                                    .guidSubtype = aSubtype};

  auto log = [&](const nsTArray<MFTEncoder::Factory>& aActivates) {
    for (const auto& activate : aActivates) {
      MFT_ENC_SLOGD("Found %s encoders: %s",
                    MFTEncoder::Factory::EnumValueToString(activate.mProvider),
                    activate.mName.get());
    }
  };

  nsTArray<MFTEncoder::Factory> swFactories;
  nsTArray<MFTEncoder::Factory> hwFactories;

  if (aHWPreference != MFTEncoder::HWPreference::SoftwareOnly) {
    // Some HW encoders use DXGI API and crash when locked down.
    // TODO: move HW encoding out of content process (bug 1754531).
    if (IsWin32kLockedDown()) {
      MFT_ENC_SLOGD("Don't use HW encoder when win32k locked down.");
    } else {
      auto r = EnumMFT(MFT_CATEGORY_VIDEO_ENCODER,
                       MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                       &inType, &outType);
      if (r.isErr()) {
        MFT_ENC_SLOGE("enumerate HW encoder for %s: error=%s",
                      CodecStr(aSubtype), ErrorMessage(r.unwrapErr()).get());
      } else {
        hwFactories.AppendElements(
            IntoFactories(r.unwrap(), true /* aIsHardware */));
        log(hwFactories);
      }
    }
  }

  if (aHWPreference != MFTEncoder::HWPreference::HardwareOnly) {
    auto r = EnumMFT(MFT_CATEGORY_VIDEO_ENCODER,
                     MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                         MFT_ENUM_FLAG_SORTANDFILTER,
                     &inType, &outType);
    if (r.isErr()) {
      MFT_ENC_SLOGE("enumerate SW encoder for %s: error=%s", CodecStr(aSubtype),
                    ErrorMessage(r.unwrapErr()).get());
    } else {
      swFactories.AppendElements(
          IntoFactories(r.unwrap(), false /* aIsHardware */));
      log(swFactories);
    }
  }

  nsTArray<MFTEncoder::Factory> factories;

  switch (aHWPreference) {
    case MFTEncoder::HWPreference::HardwareOnly:
      return hwFactories;
    case MFTEncoder::HWPreference::SoftwareOnly:
      return swFactories;
    case MFTEncoder::HWPreference::PreferHardware:
      factories.AppendElements(std::move(hwFactories));
      factories.AppendElements(std::move(swFactories));
      break;
    case MFTEncoder::HWPreference::PreferSoftware:
      factories.AppendElements(std::move(swFactories));
      factories.AppendElements(std::move(hwFactories));
      break;
  }

  return factories;
}

static void PopulateEncoderInfo(const GUID& aSubtype,
                                nsTArray<MFTEncoder::Info>& aInfos) {
  nsTArray<MFTEncoder::Factory> factories =
      EnumEncoders(aSubtype, MFTEncoder::HWPreference::PreferHardware);
  for (const auto& factory : factories) {
    MFTEncoder::Info info = {.mSubtype = aSubtype, .mName = factory.mName};
    aInfos.AppendElement(info);
    MFT_ENC_SLOGD("<ENC> [%s] %s\n", CodecStr(aSubtype), info.mName.Data());
  }
}

Maybe<MFTEncoder::Info> MFTEncoder::GetInfo(const GUID& aSubtype) {
  nsTArray<Info>& infos = Infos();

  for (auto i : infos) {
    if (IsEqualGUID(aSubtype, i.mSubtype)) {
      return Some(i);
    }
  }
  return Nothing();
}

nsCString MFTEncoder::GetFriendlyName(const GUID& aSubtype) {
  Maybe<Info> info = GetInfo(aSubtype);

  return info ? info.ref().mName : "???"_ns;
}

// Called only once by Infos().
nsTArray<MFTEncoder::Info> MFTEncoder::Enumerate() {
  nsTArray<Info> infos;

  if (!wmf::MediaFoundationInitializer::HasInitialized()) {
    MFT_ENC_SLOGE("cannot init Media Foundation");
    return infos;
  }

  PopulateEncoderInfo(MFVideoFormat_H264, infos);
  PopulateEncoderInfo(MFVideoFormat_VP90, infos);
  PopulateEncoderInfo(MFVideoFormat_VP80, infos);

  return infos;
}

nsTArray<MFTEncoder::Info>& MFTEncoder::Infos() {
  static nsTArray<Info> infos = Enumerate();
  return infos;
}

static Result<Ok, nsCString> IsSupported(
    const MFTEncoder::Factory& aFactory, const GUID& aSubtype,
    const gfx::IntSize& aFrameSize,
    const EncoderConfig::CodecSpecific& aCodecSpecific) {
  bool isH264HighProfile = IsEqualGUID(aSubtype, MFVideoFormat_H264) &&
                           aCodecSpecific.is<H264Specific>() &&
                           aCodecSpecific.as<H264Specific>().mProfile ==
                               H264_PROFILE::H264_PROFILE_HIGH;
  // This is an empirically safe limit.
  bool isFrameSizeGreaterThan4K =
      aFrameSize.width > 3840 || aFrameSize.height > 2160;

  // For Intel and AMD hardware encoders, initializing the H.264 High profile
  // with large frame sizes such as 7680×4320 may cause SetOutputType to fail or
  // prevent the encoder from producing output.
  if (aFactory.mProvider != MFTEncoder::Factory::Provider::SW &&
      isH264HighProfile && isFrameSizeGreaterThan4K) {
    return Err(nsFmtCString(
        FMT_STRING(
            "{} encoder {} does not support H.264 high profile for 4K+ video"),
        MFTEncoder::Factory::EnumValueToString(aFactory.mProvider),
        aFactory.mName.get()));
  }
  // TODO: Check the SVC support from different HW encoders.
  return Ok();
}

HRESULT MFTEncoder::Create(const GUID& aSubtype, const gfx::IntSize& aFrameSize,
                           const EncoderConfig::CodecSpecific& aCodecSpecific) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(!mEncoder);

  AUTO_MFTENCODER_MARKER("::Create");

  auto cleanup = MakeScopeExit([&] {
    mEncoder = nullptr;
    mFactory.reset();
    mConfig = nullptr;
  });

  nsTArray<MFTEncoder::Factory> factories =
      EnumEncoders(aSubtype, mHWPreference);
  for (auto& f : factories) {
    MOZ_ASSERT(f);
    if (auto r = IsSupported(f, aSubtype, aFrameSize, aCodecSpecific);
        r.isErr()) {
      nsCString errorMsg = r.unwrapErr();
      MFT_ENC_LOGE("Skip %s encoder %s for %s: %s",
                   MFTEncoder::Factory::EnumValueToString(f.mProvider),
                   f.mName.get(), CodecStr(aSubtype), errorMsg.get());
      continue;
    }

    RefPtr<IMFTransform> encoder;
    // Create the MFT activation object.
    HRESULT hr = f.mActivate->ActivateObject(
        IID_PPV_ARGS(static_cast<IMFTransform**>(getter_AddRefs(encoder))));
    if (SUCCEEDED(hr) && encoder) {
      MFT_ENC_LOGD("%s for %s is activated", f.mName.get(), CodecStr(aSubtype));
      mFactory.emplace(std::move(f));
      mEncoder = std::move(encoder);
      break;
    }
    _com_error error(hr);
    MFT_ENC_LOGE("ActivateObject %s error = 0x%lX, %ls", f.mName.get(), hr,
                 error.ErrorMessage());
  }

  if (!mFactory || !mEncoder) {
    MFT_ENC_LOGE("Failed to create MFT for %s", CodecStr(aSubtype));
    return E_FAIL;
  }

  RefPtr<ICodecAPI> config;
  // Avoid IID_PPV_ARGS() here for MingGW fails to declare UUID for ICodecAPI.
  MFT_RETURN_IF_FAILED(
      mEncoder->QueryInterface(IID_ICodecAPI, getter_AddRefs(config)));
  mConfig = std::move(config);

  SetState(State::Initializing);
  cleanup.release();
  return S_OK;
}

HRESULT
MFTEncoder::Destroy() {
  if (!mEncoder) {
    return S_OK;
  }

  MaybeResolveOrRejectAnyPendingPromise(MediaResult(
      NS_ERROR_DOM_MEDIA_CANCELED, RESULT_DETAIL("Canceled by Destroy")));
  mPendingError = NS_OK;

  mAsyncEventSource = nullptr;
  mEncoder = nullptr;
  mConfig = nullptr;
  HRESULT hr = mFactory ? S_OK : mFactory->Shutdown();
  mFactory.reset();
  // TODO: If Factory::Shutdown() fails and the encoder is not reusable, set the
  // state to error.
  SetState(State::Uninited);

  return hr;
}

HRESULT
MFTEncoder::SetMediaTypes(IMFMediaType* aInputType, IMFMediaType* aOutputType) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(aInputType && aOutputType);
  MOZ_ASSERT(mFactory);
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Initializing);

  AUTO_MFTENCODER_MARKER("::SetMediaTypes");

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });

  AsyncMFTResult asyncMFT = AttemptEnableAsync();
  if (asyncMFT.isErr()) {
    HRESULT hr = asyncMFT.inspectErr();
    MFT_ENC_LOGE("AttemptEnableAsync error: %s", ErrorMessage(hr).get());
    return hr;
  }
  bool isAsync = asyncMFT.unwrap();
  MFT_ENC_LOGD("%s encoder %s is %s",
               MFTEncoder::Factory::EnumValueToString(mFactory->mProvider),
               mFactory->mName.get(), isAsync ? "asynchronous" : "synchronous");

  MFT_RETURN_IF_FAILED(GetStreamIDs());

  // Always set encoder output type before input.
  MFT_RETURN_IF_FAILED(
      mEncoder->SetOutputType(mOutputStreamID, aOutputType, 0));

  if (MatchInputSubtype(aInputType) == GUID_NULL) {
    MFT_ENC_LOGE("Input type does not match encoder input subtype");
    return MF_E_INVALIDMEDIATYPE;
  }

  MFT_RETURN_IF_FAILED(mEncoder->SetInputType(mInputStreamID, aInputType, 0));

  MFT_RETURN_IF_FAILED(
      mEncoder->GetInputStreamInfo(mInputStreamID, &mInputStreamInfo));

  MFT_RETURN_IF_FAILED(
      mEncoder->GetOutputStreamInfo(mInputStreamID, &mOutputStreamInfo));

  mOutputStreamProvidesSample =
      IsFlagSet(mOutputStreamInfo.dwFlags, MFT_OUTPUT_STREAM_PROVIDES_SAMPLES);

  if (isAsync) {
    MFT_ENC_LOGD("Setting event source w/%s callback", mIsRealtime ? "" : "o");
    RefPtr<IMFMediaEventGenerator> source;
    MFT_RETURN_IF_FAILED(mEncoder->QueryInterface(IID_PPV_ARGS(
        static_cast<IMFMediaEventGenerator**>(getter_AddRefs(source)))));
    // TODO: Consider always using MFTEventSource with callbacks if it does not
    // introduce performance regressions for overall video encoding duration.
    if (mIsRealtime) {
      mAsyncEventSource = MakeRefPtr<MFTEventSource>(this, source.forget());
      mAsyncEventSource->BeginEventListening();
    } else {
      mAsyncEventSource = MakeRefPtr<MFTEventSource>(source.forget());
    }
  }

  MFT_RETURN_IF_FAILED(SendMFTMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));

  MFT_RETURN_IF_FAILED(SendMFTMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));

  SetState(State::Inited);
  exitWithError.release();
  mNumNeedInput = 0;
  return S_OK;
}

// Async MFT won't work without unlocking. See
// https://docs.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts#unlocking-asynchronous-mfts
MFTEncoder::AsyncMFTResult MFTEncoder::AttemptEnableAsync() {
  ComPtr<IMFAttributes> attributes = nullptr;
  HRESULT hr = mEncoder->GetAttributes(&attributes);
  if (FAILED(hr)) {
    MFT_ENC_LOGE("Encoder->GetAttribute error");
    return AsyncMFTResult(hr);
  }

  // Retrieve `MF_TRANSFORM_ASYNC` using `MFGetAttributeUINT32` rather than
  // `attributes->GetUINT32`, since `MF_TRANSFORM_ASYNC` may not be present in
  // the attributes.
  bool async =
      MFGetAttributeUINT32(attributes.Get(), MF_TRANSFORM_ASYNC, FALSE) == TRUE;
  if (!async) {
    MFT_ENC_LOGD("Encoder is not async");
    return AsyncMFTResult(false);
  }

  hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
  if (FAILED(hr)) {
    MFT_ENC_LOGE("SetUINT32 async unlock error");
    return AsyncMFTResult(hr);
  }

  return AsyncMFTResult(true);
}

HRESULT MFTEncoder::GetStreamIDs() {
  DWORD numIns;
  DWORD numOuts;
  MFT_RETURN_IF_FAILED(mEncoder->GetStreamCount(&numIns, &numOuts));
  MFT_ENC_LOGD("input stream count: %lu, output stream count: %lu", numIns,
               numOuts);
  if (numIns < 1 || numOuts < 1) {
    MFT_ENC_LOGE("stream count error");
    return MF_E_INVALIDSTREAMNUMBER;
  }

  DWORD inIDs[numIns];
  DWORD outIDs[numOuts];
  HRESULT hr = mEncoder->GetStreamIDs(numIns, inIDs, numOuts, outIDs);
  if (SUCCEEDED(hr)) {
    mInputStreamID = inIDs[0];
    mOutputStreamID = outIDs[0];
  } else if (hr == E_NOTIMPL) {
    mInputStreamID = 0;
    mOutputStreamID = 0;
  } else {
    MFT_ENC_LOGE("failed to get stream IDs: %s", ErrorMessage(hr).get());
    return hr;
  }
  MFT_ENC_LOGD("input stream ID: %lu, output stream ID: %lu", mInputStreamID,
               mOutputStreamID);
  return S_OK;
}

GUID MFTEncoder::MatchInputSubtype(IMFMediaType* aInputType) {
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(aInputType);

  GUID desired = GUID_NULL;
  MFT_RETURN_VALUE_IF_FAILED(aInputType->GetGUID(MF_MT_SUBTYPE, &desired),
                             GUID_NULL);
  MOZ_ASSERT(desired != GUID_NULL);

  DWORD i = 0;
  RefPtr<IMFMediaType> inputType;
  GUID preferred = GUID_NULL;
  while (true) {
    HRESULT hr = mEncoder->GetInputAvailableType(mInputStreamID, i,
                                                 getter_AddRefs(inputType));
    if (hr == MF_E_NO_MORE_TYPES) {
      break;
    }
    if (FAILED(hr)) {
      MFT_ENC_LOGE("GetInputAvailableType error: %s", ErrorMessage(hr).get());
      return GUID_NULL;
    }

    GUID sub = GUID_NULL;
    MFT_RETURN_VALUE_IF_FAILED(inputType->GetGUID(MF_MT_SUBTYPE, &sub),
                               GUID_NULL);

    if (IsEqualGUID(desired, sub)) {
      preferred = desired;
      break;
    }
    ++i;
  }

  return IsEqualGUID(preferred, desired) ? preferred : GUID_NULL;
}

HRESULT
MFTEncoder::SendMFTMessage(MFT_MESSAGE_TYPE aMsg, ULONG_PTR aData) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  return mEncoder->ProcessMessage(aMsg, aData);
}

HRESULT MFTEncoder::SetModes(const EncoderConfig& aConfig) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mConfig);
  MOZ_ASSERT(mState == State::Initializing);

  AUTO_MFTENCODER_MARKER("::SetModes");

  VARIANT var;
  var.vt = VT_UI4;
  switch (aConfig.mBitrateMode) {
    case BitrateMode::Constant:
      var.ulVal = eAVEncCommonRateControlMode_CBR;
      break;
    case BitrateMode::Variable:
      if (aConfig.mCodec == CodecType::VP8 ||
          aConfig.mCodec == CodecType::VP9) {
        MFT_ENC_LOGE(
            "Overriding requested VRB bitrate mode, forcing CBR for VP8/VP9 "
            "encoding.");
        var.ulVal = eAVEncCommonRateControlMode_CBR;
      } else {
        var.ulVal = eAVEncCommonRateControlMode_PeakConstrainedVBR;
      }
      break;
  }
  MFT_RETURN_IF_FAILED(
      mConfig->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var));

  if (aConfig.mBitrate) {
    var.ulVal = aConfig.mBitrate;
    MFT_RETURN_IF_FAILED(
        mConfig->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var));
  }

  switch (aConfig.mScalabilityMode) {
    case ScalabilityMode::None:
      var.ulVal = 1;
      break;
    case ScalabilityMode::L1T2:
      var.ulVal = 2;
      break;
    case ScalabilityMode::L1T3:
      var.ulVal = 3;
      break;
  }

  // TODO check this and replace it with mFactory->mProvider
  bool isIntel = false;
  if (aConfig.mScalabilityMode != ScalabilityMode::None || isIntel) {
    MFT_RETURN_IF_FAILED(
        mConfig->SetValue(&CODECAPI_AVEncVideoTemporalLayerCount, &var));
  }

  if (SUCCEEDED(mConfig->IsModifiable(&CODECAPI_AVEncAdaptiveMode))) {
    var.ulVal = eAVEncAdaptiveMode_Resolution;
    MFT_RETURN_IF_FAILED(mConfig->SetValue(&CODECAPI_AVEncAdaptiveMode, &var));
  }

  if (SUCCEEDED(mConfig->IsModifiable(&CODECAPI_AVLowLatencyMode))) {
    var.vt = VT_BOOL;
    var.boolVal =
        aConfig.mUsage == Usage::Realtime ? VARIANT_TRUE : VARIANT_FALSE;
    MFT_RETURN_IF_FAILED(mConfig->SetValue(&CODECAPI_AVLowLatencyMode, &var));
  }

  mIsRealtime = aConfig.mUsage == Usage::Realtime;

  return S_OK;
}

HRESULT
MFTEncoder::SetBitrate(UINT32 aBitsPerSec) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mConfig);

  VARIANT var = {.vt = VT_UI4, .ulVal = aBitsPerSec};
  return mConfig->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var);
}

bool MFTEncoder::IsHardwareAccelerated() const {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  return mFactory && mFactory->mProvider != MFTEncoder::Factory::Provider::SW;
}

template <typename T, typename E, bool IsExclusive = true>
static auto ResultToPromise(Result<T, E>&& aResult) {
  if (aResult.isErr()) {
    return MozPromise<T, E, IsExclusive>::CreateAndReject(aResult.unwrapErr(),
                                                          __func__);
  }
  return MozPromise<T, E, IsExclusive>::CreateAndResolve(aResult.unwrap(),
                                                         __func__);
};

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::Encode(InputSample&& aInput) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  if (!IsAsync()) {
    return ResultToPromise(EncodeSync(std::move(aInput)));
  }
  if (!mIsRealtime) {
    return ResultToPromise(EncodeAsync(std::move(aInput)));
  }
  return EncodeWithAsyncCallback(std::move(aInput));
}

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::Drain() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  if (!IsAsync()) {
    return ResultToPromise(DrainSync());
  }
  if (!mIsRealtime) {
    return ResultToPromise(DrainAsync());
  }
  return DrainWithAsyncCallback();
}

static HRESULT CreateSample(RefPtr<IMFSample>* aOutSample, DWORD aSize,
                            DWORD aAlignment) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  RefPtr<IMFSample> sample;
  MFT_RETURN_IF_FAILED_S(wmf::MFCreateSample(getter_AddRefs(sample)));

  RefPtr<IMFMediaBuffer> buffer;
  MFT_RETURN_IF_FAILED_S(wmf::MFCreateAlignedMemoryBuffer(
      aSize, aAlignment, getter_AddRefs(buffer)));

  MFT_RETURN_IF_FAILED_S(sample->AddBuffer(buffer));

  *aOutSample = sample.forget();

  return S_OK;
}

HRESULT
MFTEncoder::CreateInputSample(RefPtr<IMFSample>* aSample, size_t aSize) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  return CreateSample(
      aSample, aSize,
      mInputStreamInfo.cbAlignment > 0 ? mInputStreamInfo.cbAlignment - 1 : 0);
}

Result<MFTEncoder::EncodedData, MediaResult> MFTEncoder::EncodeSync(
    InputSample&& aInput) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(State::Encoding);

  // Follow steps in
  // https://learn.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
  HRESULT hr = ProcessInput(std::move(aInput));
  if (FAILED(hr)) {
    return Err(MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("ProcessInput error: %s", ErrorMessage(hr).get())));
  }

  DWORD flags = 0;
  hr = mEncoder->GetOutputStatus(&flags);
  if (FAILED(hr) && hr != E_NOTIMPL) {
    return Err(MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("GetOutputStatus error: %s", ErrorMessage(hr).get())));
  }

  if (hr == S_OK && !(flags & MFT_OUTPUT_STATUS_SAMPLE_READY)) {
    exitWithError.release();
    SetState(State::Inited);
    return EncodedData{};
  }

  MOZ_ASSERT(hr == E_NOTIMPL ||
             (hr == S_OK && (flags & MFT_OUTPUT_STATUS_SAMPLE_READY)));
  EncodedData outputs = MOZ_TRY(PullOutputs().mapErr([](HRESULT e) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("PullOutputs error: %s", ErrorMessage(e).get()));
  }));
  exitWithError.release();
  SetState(State::Inited);
  return outputs;
}

Result<MFTEncoder::EncodedData, MediaResult> MFTEncoder::DrainSync() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(State::Draining);

  // Follow step 7 in
  // https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
  HRESULT hr = SendMFTMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
  if (FAILED(hr)) {
    return Err(MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("SendMFTMessage MFT_MESSAGE_COMMAND_DRAIN error: %s",
                      ErrorMessage(hr).get())));
  }

  EncodedData outputs = MOZ_TRY(PullOutputs().mapErr([](HRESULT e) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("PullOutputs error: %s", ErrorMessage(e).get()));
  }));
  exitWithError.release();
  SetState(State::Inited);
  return outputs;
}

Result<MFTEncoder::EncodedData, HRESULT> MFTEncoder::PullOutputs() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  EncodedData outputs;
  MPEGHeader header;
  while (true) {
    auto r = GetOutputOrNewHeader();
    if (r.isErr()) {
      HRESULT e = r.unwrapErr();
      if (e == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        MFT_ENC_LOGD("Need more inputs");
        // Step 4 or 8 in
        // https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model#process-data
        break;
      }
      MFT_ENC_LOGE("GetOutputOrNewHeader failed: %s", ErrorMessage(e).get());
      return Err(e);
    }

    OutputResult result = r.unwrap();
    if (result.IsHeader()) {
      header = result.TakeHeader();
      MFT_ENC_LOGD(
          "Obtained new MPEG header, attempting to retrieve output again");
      continue;
    }

    MOZ_ASSERT(result.IsSample());
    outputs.AppendElement(OutputSample{.mSample = result.TakeSample()});
    if (!header.IsEmpty()) {
      outputs.LastElement().mHeader = std::move(header);
    }
  }

  MFT_ENC_LOGV("%zu outputs pulled", outputs.Length());
  return outputs;
}

Result<MFTEncoder::EncodedData, MediaResult> MFTEncoder::EncodeAsync(
    InputSample&& aInput) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(State::Encoding);

  mPendingInputs.push_back(std::move(aInput));
  auto r = MOZ_TRY(ProcessInput().mapErr([](HRESULT hr) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("ProcessInput error: %s", ErrorMessage(hr).get()));
  }));
  MFT_ENC_LOGV("input processed: %s", MFTEncoder::EnumValueToString(r));

  // If the underlying system signaled that more input is needed, continue
  // processing inputs until either no more input is required or there are no
  // pending inputs left.
  MOZ_TRY(ProcessPendingEvents().mapErr([](HRESULT hr) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("ProcessPendingEvents error: %s",
                                     ErrorMessage(hr).get()));
  }));
  MOZ_ASSERT(mNumNeedInput == 0 || mPendingInputs.empty());

  exitWithError.release();
  SetState(State::Inited);
  EncodedData outputs = std::move(mOutputs);
  return outputs;
}

Result<MFTEncoder::EncodedData, MediaResult> MFTEncoder::DrainAsync() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(mPendingInputs.empty() ? State::Draining : State::PreDraining);

  // Ensure all pending inputs are processed before initiating the drain. If any
  // pending inputs remain, the input-needed count must be zero; otherwise, they
  // would have been processed in Encode().
  MOZ_ASSERT_IF(!mPendingInputs.empty(), mNumNeedInput == 0);
  while (!mPendingInputs.empty()) {
    MFT_ENC_LOGV("Pending inputs: %zu, inputs needed: %zu",
                 mPendingInputs.size(), mNumNeedInput);
    // Prompt the MFT to process pending inputs or collect any pending outputs,
    // which may allow more inputs to be accepted.
    MOZ_TRY(ProcessPendingEvents().mapErr([](HRESULT hr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("ProcessPendingEvents error: %s",
                                       ErrorMessage(hr).get()));
    }));
  }

  if (mState == State::PreDraining) {
    SetState(State::Draining);
  }

  HRESULT hr = SendMFTMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
  if (FAILED(hr)) {
    return Err(MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("SendMFTMessage MFT_MESSAGE_COMMAND_DRAIN error: %s",
                      ErrorMessage(hr).get())));
  }

  ProcessedResults results;
  do {
    results = MOZ_TRY(ProcessPendingEvents().mapErr([](HRESULT hr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         RESULT_DETAIL("ProcessPendingEvents error: %s",
                                       ErrorMessage(hr).get()));
    }));
  } while (!results.contains(ProcessedResult::DrainComplete));

  exitWithError.release();
  SetState(State::Inited);
  EncodedData outputs = std::move(mOutputs);
  return outputs;
}

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::EncodeWithAsyncCallback(
    InputSample&& aInput) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mEncodePromise.IsEmpty());
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(State::Encoding);

  mPendingInputs.push_back(std::move(aInput));
  auto inputProcessed = ProcessInput();
  if (inputProcessed.isErr()) {
    return EncodePromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            RESULT_DETAIL("ProcessInput error: %s",
                          ErrorMessage(inputProcessed.unwrapErr()).get())),
        __func__);
  }
  MFT_ENC_LOGV("input processed: %s",
               MFTEncoder::EnumValueToString(inputProcessed.inspect()));

  RefPtr<MFTEncoder::EncodePromise> p = mEncodePromise.Ensure(__func__);
  exitWithError.release();

  // TODO: Calculate time duration based on frame rate instead of a fixed value.
  auto timerResult = NS_NewTimerWithCallback(
      [self = RefPtr{this}](nsITimer* aTimer) {
        if (!self->mEncoder) {
          MFT_ENC_SLOGW(
              "Timer callback aborted: encoder has already been shut down");
          return;
        }

        MFT_ENC_SLOGV("Timer callback: resolving pending encode promise");
        self->MaybeResolveOrRejectEncodePromise();
      },
      TimeDuration::FromMilliseconds(20), nsITimer::TYPE_ONE_SHOT,
      "EncodingProgressChecker", GetCurrentSerialEventTarget());
  if (timerResult.isErr()) {
    MFT_ENC_LOGE(
        "Failed to set an encoding progress checker. Resolve encode promise "
        "directly");
    MaybeResolveOrRejectEncodePromise();
    return p;
  }

  mTimer = timerResult.unwrap();
  return p;
}

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::DrainWithAsyncCallback() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  return PrepareForDrain()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}](MFTEncoder::EncodedData&& aOutput) {
        MFT_ENC_SLOGV("All pending inputs are processed, now starts draining");
        self->mOutputs.AppendElements(std::move(aOutput));
        return self->StartDraining();
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        MFT_ENC_SLOGE("PrepareForDrain failed: %s", aError.Description().get());
        return EncodePromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::PrepareForDrain() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mPreDrainPromise.IsEmpty());
  MOZ_ASSERT(mState == State::Inited);

  SetState(State::PreDraining);
  MFT_ENC_LOGV("Pending inputs: %zu, inputs needed: %zu", mPendingInputs.size(),
               mNumNeedInput);

  if (mPendingInputs.empty()) {
    MFT_ENC_LOGV("No pending inputs, leave %s state immediately",
                 EnumValueToString(mState));
    SetState(State::Inited);
    return EncodePromise::CreateAndResolve(std::move(mOutputs), __func__);
  }

  MOZ_ASSERT(mNumNeedInput == 0);
  MFT_ENC_LOGV("Waiting for %zu pending inputs to be processed",
               mPendingInputs.size());

  return mPreDrainPromise.Ensure(__func__);
}

RefPtr<MFTEncoder::EncodePromise> MFTEncoder::StartDraining() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mDrainPromise.IsEmpty());
  MOZ_ASSERT(mPendingInputs.empty());
  MOZ_ASSERT(mState == State::Inited);

  auto exitWithError = MakeScopeExit([&] { SetState(State::Error); });
  SetState(State::Draining);

  HRESULT r = SendMFTMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
  if (FAILED(r)) {
    return EncodePromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("SendMFTMessage COMMAND_DRAIN failed: %s",
                                  ErrorMessage(r).get())),
        __func__);
  }

  RefPtr<MFTEncoder::EncodePromise> p = mDrainPromise.Ensure(__func__);
  exitWithError.release();
  return p;
}

void MFTEncoder::EventHandler(MediaEventType aEventType, HRESULT aStatus) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  MFT_ENC_LOGV("[state: %s] Get event: %s, status: %s",
               EnumValueToString(mState), MediaEventTypeStr(aEventType),
               ErrorMessage(aStatus).get());

  if (!mAsyncEventSource) {
    MFT_ENC_LOGW("Async event source is not initialized or destroyed");
    return;
  }

  MOZ_ASSERT(mState != State::Uninited);

  auto errorHandler = [&](MediaResult&& aError) {
    MFT_ENC_LOGE("%s", aError.Message().get());
    mPendingError = aError;
    switch (mState) {
      case State::Encoding:
        MaybeResolveOrRejectEncodePromise();
        break;
      case State::Draining:
        MaybeResolveOrRejectDrainPromise();
        break;
      case State::PreDraining:
        MaybeResolveOrRejectPreDrainPromise();
        break;
      default:
        MFT_ENC_LOGW("Received error in state %s", EnumValueToString(mState));
    }
  };

  if (FAILED(aStatus)) {
    errorHandler(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Received error status: %s for event %s",
                                  ErrorMessage(aStatus).get(),
                                  MediaEventTypeStr(aEventType))));
    return;
  }

  auto processed = ProcessEvent(aEventType);
  if (processed.isErr()) {
    HRESULT hr = processed.unwrapErr();
    errorHandler(MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("ProcessEvent error: %s for event %s",
                      ErrorMessage(hr).get(), MediaEventTypeStr(aEventType))));
    return;
  }

  ProcessedResult result = processed.unwrap();
  MFT_ENC_LOGV(
      "%s processed: %s\n\tpending inputs: %zu\n\tinput needed: %zu\n\tpending "
      "outputs: %zu",
      MediaEventTypeStr(aEventType), MFTEncoder::EnumValueToString(result),
      mPendingInputs.size(), mNumNeedInput, mOutputs.Length());
  switch (result) {
    case ProcessedResult::AllAvailableInputsProcessed:
      // Since mNumNeedInput was incremented in ProcessInput(), a result
      // indicating no input was processed means there were not enough pending
      // inputs in the queue.
      MOZ_ASSERT(mPendingInputs.empty());
      // If EventHandler is in the PreDraining state here, it means there were
      // pending inputs to process before draining started. Processing those
      // inputs should have produced InputProcessed results, and the state
      // should have transitioned out of PreDraining. Therefore, we should not
      // still be in PreDraining at this point.
      MOZ_ASSERT(mState != State::PreDraining);
      [[fallthrough]];
    case ProcessedResult::InputProcessed:
      if (mState == State::Encoding) {
        // In realtime mode, we could resolve the encode promise only upon
        // receiving an output. However, since the performance gain is minor,
        // it's not worth risking a scenario where the encode promise is
        // resolved by the timer callback if no output is produced in time.
        MaybeResolveOrRejectEncodePromise();
      } else if (mState == State::PreDraining) {
        if (mPendingInputs.empty()) {
          MaybeResolveOrRejectPreDrainPromise();
        }
      }
      break;
    case ProcessedResult::OutputYielded:
      if (mState == State::Encoding) {
        MaybeResolveOrRejectEncodePromise();
      }
      break;
    case ProcessedResult::DrainComplete:
      MOZ_ASSERT(mState == State::Draining);
      MaybeResolveOrRejectDrainPromise();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Unexpected ProcessedResult value in EventHandler");
  }

  mAsyncEventSource->BeginEventListening();
}

void MFTEncoder::MaybeResolveOrRejectEncodePromise() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  if (mEncodePromise.IsEmpty()) {
    MFT_ENC_LOGV("[%s] No encode promise to resolve or reject",
                 EnumValueToString(mState));
    return;
  }

  MOZ_ASSERT(mState == State::Encoding);

  MFT_ENC_LOGV("Resolving (%zu outputs ) or rejecting encode promise (%s)",
               mOutputs.Length(),
               NS_FAILED(mPendingError.Code())
                   ? mPendingError.Description().get()
                   : "no error");

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
    MFT_ENC_LOGV("Encode timer cancelled");
  }

  if (NS_FAILED(mPendingError.Code())) {
    SetState(State::Error);
    mEncodePromise.Reject(mPendingError, __func__);
    mPendingError = NS_OK;
    return;
  }

  mEncodePromise.Resolve(std::move(mOutputs), __func__);
  SetState(State::Inited);
}

void MFTEncoder::MaybeResolveOrRejectDrainPromise() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  if (mDrainPromise.IsEmpty()) {
    MFT_ENC_LOGV("[%s] No drain promise to resolve or reject",
                 EnumValueToString(mState));
    return;
  }

  MOZ_ASSERT(mState == State::Draining);

  MFT_ENC_LOGV("Resolving (%zu outputs ) or rejecting drain promise (%s)",
               mOutputs.Length(),
               NS_FAILED(mPendingError.Code())
                   ? mPendingError.Description().get()
                   : "no error");

  if (NS_FAILED(mPendingError.Code())) {
    SetState(State::Error);
    mDrainPromise.Reject(mPendingError, __func__);
    mPendingError = NS_OK;
    return;
  }

  mDrainPromise.Resolve(std::move(mOutputs), __func__);
  SetState(State::Inited);
}

void MFTEncoder::MaybeResolveOrRejectPreDrainPromise() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  if (mPreDrainPromise.IsEmpty()) {
    MFT_ENC_LOGV("[%s] No pre-drain promise to resolve or reject",
                 EnumValueToString(mState));
    return;
  }

  MOZ_ASSERT(mState == State::PreDraining);

  MFT_ENC_LOGV("Resolving pre-drain promise (%zu outputs ) or rejecting (%s)",
               mOutputs.Length(),
               NS_FAILED(mPendingError.Code())
                   ? mPendingError.Description().get()
                   : "no error");

  if (NS_FAILED(mPendingError.Code())) {
    SetState(State::Error);
    mPreDrainPromise.Reject(mPendingError, __func__);
    mPendingError = NS_OK;
    return;
  }

  MOZ_ASSERT(mPendingInputs.empty());
  mPreDrainPromise.Resolve(std::move(mOutputs), __func__);
  SetState(State::Inited);
}

void MFTEncoder::MaybeResolveOrRejectAnyPendingPromise(
    const MediaResult& aResult) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  if (NS_FAILED(aResult.Code())) {
    MFT_ENC_LOGW(
        "[%s] Rejecting pending promises with error: %s (previous pending "
        "error: %s)",
        EnumValueToString(mState), aResult.Description().get(),
        mPendingError.Description().get());
    mPendingError = aResult;
  }

  MaybeResolveOrRejectEncodePromise();
  MaybeResolveOrRejectPreDrainPromise();
  MaybeResolveOrRejectDrainPromise();
}

Result<MFTEncoder::ProcessedResults, HRESULT>
MFTEncoder::ProcessPendingEvents() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mAsyncEventSource);

  ProcessedResults results;
  while (true) {
    auto got = GetPendingEvent();
    if (got.isErr()) {
      HRESULT hr = got.unwrapErr();
      if (hr == MF_E_NO_EVENTS_AVAILABLE) {
        MFT_ENC_LOGV("No more pending events");
        break;
      }
      MFT_ENC_LOGE("GetPendingEvent error: %s", ErrorMessage(hr).get());
      return Err(hr);
    }

    MediaEventType event = got.unwrap();
    MFT_ENC_LOGV("Processing pending event: %s", MediaEventTypeStr(event));
    ProcessedResult result = MOZ_TRY(ProcessEvent(event));
    MFT_ENC_LOGV("event processed: %s", MFTEncoder::EnumValueToString(result));
    results += result;
  }

  return results;
}

Result<MFTEncoder::ProcessedResult, HRESULT> MFTEncoder::ProcessEvent(
    MediaEventType aType) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  switch (aType) {
    case METransformNeedInput:
      ++mNumNeedInput;
      return ProcessInput();
    case METransformHaveOutput:
      return ProcessOutput();
    case METransformDrainComplete:
      return ProcessDrainComplete();
    default:
      MFT_ENC_LOGE("Unsupported event type: %s", MediaEventTypeStr(aType));
      break;
  }
  return Err(E_UNEXPECTED);
}

Result<MFTEncoder::ProcessedResult, HRESULT> MFTEncoder::ProcessInput() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  MFT_ENC_LOGV("Inputs needed: %zu, pending inputs: %zu", mNumNeedInput,
               mPendingInputs.size());
  if (mNumNeedInput == 0 || mPendingInputs.empty()) {
    return ProcessedResult::AllAvailableInputsProcessed;
  }

  auto input = mPendingInputs.front();
  mPendingInputs.pop_front();
  MFT_RETURN_ERROR_IF_FAILED(ProcessInput(std::move(input)));
  --mNumNeedInput;

  return ProcessedResult::InputProcessed;
}

Result<MFTEncoder::ProcessedResult, HRESULT> MFTEncoder::ProcessOutput() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  OutputResult result = MOZ_TRY(GetOutputOrNewHeader());
  if (result.IsHeader()) {
    mOutputHeader = result.TakeHeader();
    MFT_ENC_LOGD("Got new MPEG header, size: %zu", mOutputHeader.Length());
    return ProcessedResult::OutputYielded;
  }

  MOZ_ASSERT(result.IsSample());
  mOutputs.AppendElement(OutputSample{.mSample = result.TakeSample()});
  if (!mOutputHeader.IsEmpty()) {
    mOutputs.LastElement().mHeader = std::move(mOutputHeader);
  }
  return ProcessedResult::OutputYielded;
}

Result<MFTEncoder::ProcessedResult, HRESULT>
MFTEncoder::ProcessDrainComplete() {
  // After draining is complete, the MFT will not emit another
  // METransformNeedInput event until it receives an
  // MFT_MESSAGE_NOTIFY_START_OF_STREAM message.
  MFT_RETURN_ERROR_IF_FAILED(
      SendMFTMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
  MFT_ENC_LOGV("Drain complete, resetting inputs needed(%zu) to 0",
               mNumNeedInput);
  mNumNeedInput = 0;
  return ProcessedResult::DrainComplete;
}

Result<MediaEventType, HRESULT> MFTEncoder::GetPendingEvent() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mAsyncEventSource);
  MOZ_ASSERT(!mIsRealtime);
  return mAsyncEventSource->GetEvent(MF_EVENT_FLAG_NO_WAIT);
}

Result<MFTEncoder::OutputResult, HRESULT> MFTEncoder::GetOutputOrNewHeader() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  RefPtr<IMFSample> sample;
  DWORD status = 0;
  DWORD bufStatus = 0;

  HRESULT hr = ProcessOutput(sample, status, bufStatus);
  MFT_ENC_LOGV(
      "output processed: %s, status: 0x%lx, output buffer status: 0x%lx",
      ErrorMessage(hr).get(), status, bufStatus);

  if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
    if (bufStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE) {
      MFT_ENC_LOGW("output buffer format changed, updating output type");
      MFT_RETURN_ERROR_IF_FAILED(UpdateOutputType());
      return OutputResult(MOZ_TRY(GetMPEGSequenceHeader()));
    }
    // TODO: We should query for updated stream identifiers here. For now,
    // handle this as an error.
    return Err(hr);
  }

  if (FAILED(hr)) {
    return Err(hr);
  }

  MOZ_ASSERT(sample);
  return OutputResult(sample.forget());
}

HRESULT MFTEncoder::UpdateOutputType() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);
  // Per Microsoft's documentation:
  // https://docs.microsoft.com/en-us/windows/win32/medfound/handling-stream-changes#output-type
  RefPtr<IMFMediaType> outputType;
  MFT_RETURN_IF_FAILED(mEncoder->GetOutputAvailableType(
      mOutputStreamID, 0, getter_AddRefs(outputType)));
  MFT_RETURN_IF_FAILED(mEncoder->SetOutputType(mOutputStreamID, outputType, 0));
  MFT_ENC_LOGW("stream format has been renegotiated for output stream %lu",
               mOutputStreamID);
  return S_OK;
}

HRESULT MFTEncoder::ProcessOutput(RefPtr<IMFSample>& aSample,
                                  DWORD& aOutputStatus, DWORD& aBufferStatus) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  MFT_OUTPUT_DATA_BUFFER output = {.dwStreamID = mOutputStreamID,
                                   .pSample = nullptr,
                                   .dwStatus = 0,
                                   .pEvents = nullptr};
  RefPtr<IMFSample> sample;
  if (!mOutputStreamProvidesSample) {
    MFT_RETURN_IF_FAILED(CreateSample(&sample, mOutputStreamInfo.cbSize,
                                      mOutputStreamInfo.cbAlignment > 1
                                          ? mOutputStreamInfo.cbAlignment - 1
                                          : 0));
    output.pSample = sample;
  }

  HRESULT hr = mEncoder->ProcessOutput(0, 1, &output, &aOutputStatus);
  aBufferStatus = output.dwStatus;
  if (output.pEvents) {
    MFT_ENC_LOGW("Discarding events from ProcessOutput");
    output.pEvents->Release();
    output.pEvents = nullptr;
  }

  if (FAILED(hr)) {
    return hr;
  }

  aSample = output.pSample;
  if (mOutputStreamProvidesSample) {
    // Release MFT provided sample.
    output.pSample->Release();
    output.pSample = nullptr;
  }

  return hr;
}

HRESULT MFTEncoder::ProcessInput(InputSample&& aInput) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  MFT_RETURN_IF_FAILED(
      mEncoder->ProcessInput(mInputStreamID, aInput.mSample, 0));
  if (aInput.mKeyFrameRequested) {
    VARIANT v = {.vt = VT_UI4, .ulVal = 1};
    mConfig->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &v);
  }
  return S_OK;
}

Result<nsTArray<UINT8>, HRESULT> MFTEncoder::GetMPEGSequenceHeader() {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());
  MOZ_ASSERT(mEncoder);

  RefPtr<IMFMediaType> outputType;
  MFT_RETURN_ERROR_IF_FAILED(mEncoder->GetOutputCurrentType(
      mOutputStreamID, getter_AddRefs(outputType)));
  UINT32 length = 0;
  HRESULT hr = outputType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &length);
  if (hr == MF_E_ATTRIBUTENOTFOUND) {
    MFT_ENC_LOGW("GetBlobSize MF_MT_MPEG_SEQUENCE_HEADER: not found");
    return nsTArray<UINT8>();
  }
  if (FAILED(hr)) {
    MFT_ENC_LOGE("GetBlobSize MF_MT_MPEG_SEQUENCE_HEADER error: %s",
                 ErrorMessage(hr).get());
    return Err(hr);
  }
  if (length == 0) {
    MFT_ENC_LOGW("GetBlobSize MF_MT_MPEG_SEQUENCE_HEADER: no header");
    return nsTArray<UINT8>();
  }
  MFT_ENC_LOGD("GetBlobSize MF_MT_MPEG_SEQUENCE_HEADER: %u", length);

  nsTArray<UINT8> header;
  header.SetCapacity(length);
  hr = outputType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.Elements(),
                           length, nullptr);
  header.SetLength(SUCCEEDED(hr) ? length : 0);

  return header;
}

void MFTEncoder::SetState(State aState) {
  MOZ_ASSERT(mscom::IsCurrentThreadMTA());

  MFT_ENC_LOGD("SetState: %s -> %s", EnumValueToString(mState),
               EnumValueToString(aState));
  mState = aState;
}

#define MFT_EVTSRC_LOG(level, msg, ...)                                     \
  MFT_LOG_INTERNAL(level, "MFTEventSource(0x%p)::%s: " msg, this, __func__, \
                   ##__VA_ARGS__)
#define MFT_EVTSRC_SLOG(level, msg, ...) \
  MFT_LOG_INTERNAL(level, "MFTEventSource::%s: " msg, __func__, ##__VA_ARGS__)

#define MFT_EVTSRC_LOGD(msg, ...) MFT_EVTSRC_LOG(Debug, msg, ##__VA_ARGS__)
#define MFT_EVTSRC_LOGE(msg, ...) MFT_EVTSRC_LOG(Error, msg, ##__VA_ARGS__)
#define MFT_EVTSRC_LOGW(msg, ...) MFT_EVTSRC_LOG(Warning, msg, ##__VA_ARGS__)
#define MFT_EVTSRC_LOGV(msg, ...) MFT_EVTSRC_LOG(Verbose, msg, ##__VA_ARGS__)

#define MFT_EVTSRC_SLOGW(msg, ...) MFT_EVTSRC_SLOG(Warning, msg, ##__VA_ARGS__)

#define MFT_EVTSRC_RETURN_IF_FAILED(x) \
  MFT_RETURN_IF_FAILED_IMPL(x, MFT_EVTSRC_LOGE)
#define MFT_EVTSRC_RETURN_ERROR_IF_FAILED(x) \
  MFT_RETURN_ERROR_IF_FAILED_IMPL(x, MFT_EVTSRC_LOGE)

MFTEventSource::MFTEventSource(
    nsISerialEventTarget* aEncoderThread, MFTEncoder* aEncoder,
    already_AddRefed<IMFMediaEventGenerator> aEventGenerator)
    : mId(GenerateId()),
      mEncoderThread(aEncoderThread),
      mEncoder(aEncoder),
      mEventGenerator(aEventGenerator, "MFTEventSource::mEventGenerator") {
  MOZ_ASSERT(mEncoderThread);
  auto g = mEventGenerator.Lock();
  MOZ_ASSERT(!!g.ref());
  MFT_EVTSRC_LOGD("(id %zu) created", mId);
}

MFTEventSource::~MFTEventSource() {
  MFT_EVTSRC_LOGD("(id %zu) destroyed", mId);
  auto g = mEventGenerator.Lock();
  *g = nullptr;
}

Result<MediaEventType, HRESULT> MFTEventSource::GetEvent(DWORD aFlags) {
  MOZ_ASSERT(mEncoderThread->IsOnCurrentThread());
  MOZ_ASSERT(!CanForwardEvents());

  HRESULT hr = S_OK;
  RefPtr<IMFMediaEvent> event;
  {
    auto g = mEventGenerator.Lock();
    hr = g.ref()->GetEvent(aFlags, getter_AddRefs(event));
  }
  if (FAILED(hr)) {
    if (hr == MF_E_NO_EVENTS_AVAILABLE) {
      MFT_EVTSRC_LOGV("GetEvent: %s", ErrorMessage(hr).get());
    } else {
      MFT_EVTSRC_LOGE("GetEvent error: %s", ErrorMessage(hr).get());
    }
    return Err(hr);
  }
  MediaEventType type = MEUnknown;
  MFT_EVTSRC_RETURN_ERROR_IF_FAILED(event->GetType(&type));
  return type;
}

HRESULT MFTEventSource::BeginEventListening() {
  MOZ_ASSERT(mEncoderThread->IsOnCurrentThread());
  MOZ_ASSERT(CanForwardEvents());

  MFT_EVTSRC_LOGV("(id %zu) starts waiting for event", mId);
  HRESULT hr = S_OK;
  {
    auto g = mEventGenerator.Lock();
    hr = g.ref()->BeginGetEvent(this, nullptr);
  }
  return hr;
}

STDMETHODIMP MFTEventSource::GetParameters(DWORD* aFlags, DWORD* aQueue) {
  MOZ_ASSERT(aFlags);
  MOZ_ASSERT(aQueue);
  *aFlags = MFASYNC_FAST_IO_PROCESSING_CALLBACK;
  *aQueue = MFASYNC_CALLBACK_QUEUE_TIMER;
  return S_OK;
}

STDMETHODIMP MFTEventSource::Invoke(IMFAsyncResult* aResult) {
  RefPtr<IMFMediaEvent> event;
  {
    auto g = mEventGenerator.Lock();
    MFT_EVTSRC_RETURN_IF_FAILED(
        g.ref()->EndGetEvent(aResult, getter_AddRefs(event)));
  }

  MediaEventType type = MEUnknown;
  MFT_EVTSRC_RETURN_IF_FAILED(event->GetType(&type));

  MFT_EVTSRC_LOGV("(id %zu) received event: %s", mId, MediaEventTypeStr(type));

  HRESULT status = S_OK;
  MFT_EVTSRC_RETURN_IF_FAILED(event->GetStatus(&status));

  mEncoderThread->Dispatch(
      NS_NewRunnableFunction(__func__,
                             [type, status, id = mId, encoder = mEncoder]() {
                               if (!encoder->mAsyncEventSource ||
                                   encoder->mAsyncEventSource->mId != id) {
                                 MFT_EVTSRC_SLOGW(
                                     "Event %s from source %zu is stale",
                                     MediaEventTypeStr(type), id);
                                 return;
                               }
                               encoder->EventHandler(type, status);
                             }),
      NS_DISPATCH_NORMAL);

  return status;
}

STDMETHODIMP MFTEventSource::QueryInterface(REFIID aIID, void** aPPV) {
  const IID IID_IMFAsyncCallback = __uuidof(IMFAsyncCallback);
  if (aIID == IID_IUnknown || aIID == IID_IMFAsyncCallback) {
    *aPPV = static_cast<IMFAsyncCallback*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

#undef MFT_EVTSRC_LOG
#undef MFT_EVTSRC_SLOG
#undef MFT_EVTSRC_LOGE
#undef MFT_EVTSRC_RETURN_IF_FAILED
#undef MFT_EVTSRC_RETURN_ERROR_IF_FAILED

}  // namespace mozilla

#undef MFT_ENC_SLOGE
#undef MFT_ENC_SLOGD
#undef MFT_ENC_LOGE
#undef MFT_ENC_LOGW
#undef MFT_ENC_LOGV
#undef MFT_ENC_LOGD
#undef MFT_RETURN_IF_FAILED
#undef MFT_RETURN_IF_FAILED_S
#undef MFT_RETURN_VALUE_IF_FAILED
#undef MFT_RETURN_VALUE_IF_FAILED_S
#undef MFT_RETURN_ERROR_IF_FAILED
#undef MFT_RETURN_ERROR_IF_FAILED_S
#undef MFT_RETURN_IF_FAILED_IMPL
#undef MFT_RETURN_VALUE_IF_FAILED_IMPL
#undef MFT_RETURN_ERROR_IF_FAILED_IMPL
#undef MFT_ENC_LOG
#undef MFT_ENC_SLOG
#undef MFT_LOG_INTERNAL
#undef AUTO_MFTENCODER_MARKER
