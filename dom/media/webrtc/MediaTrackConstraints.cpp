/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaTrackConstraints.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "mozilla/MediaManager.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"

#ifdef MOZ_WEBRTC
namespace mozilla {
extern LazyLogModule gMediaManagerLog;
}
#else
static mozilla::LazyLogModule gMediaManagerLog("MediaManager");
#endif
#define LOG(...) MOZ_LOG(gMediaManagerLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

using dom::CallerType;
using dom::VideoResizeModeEnum;

template <class ValueType>
template <class ConstrainRange>
void NormalizedConstraintSet::Range<ValueType>::SetFrom(
    const ConstrainRange& aOther) {
  if (aOther.mIdeal.WasPassed()) {
    mIdeal.emplace(aOther.mIdeal.Value());
  }
  if (aOther.mExact.WasPassed()) {
    mMin = aOther.mExact.Value();
    mMax = aOther.mExact.Value();
  } else {
    if (aOther.mMin.WasPassed()) {
      mMin = aOther.mMin.Value();
    }
    if (aOther.mMax.WasPassed()) {
      mMax = aOther.mMax.Value();
    }
  }
}

// The Range code works surprisingly well for bool, except when averaging
// ideals.
template <>
bool NormalizedConstraintSet::Range<bool>::Merge(const Range& aOther) {
  if (!Intersects(aOther)) {
    return false;
  }
  Intersect(aOther);

  // To avoid "unsafe use of type 'bool'", we keep counter in mMergeDenominator
  uint32_t counter = mMergeDenominator >> 16;
  uint32_t denominator = mMergeDenominator & 0xffff;

  if (aOther.mIdeal.isSome()) {
    if (mIdeal.isNothing()) {
      mIdeal.emplace(aOther.Get(false));
      counter = aOther.Get(false);
      denominator = 1;
    } else {
      if (!denominator) {
        counter = Get(false);
        denominator = 1;
      }
      counter += aOther.Get(false);
      denominator++;
    }
  }
  mMergeDenominator = ((counter & 0xffff) << 16) + (denominator & 0xffff);
  return true;
}

template <>
void NormalizedConstraintSet::Range<bool>::FinalizeMerge() {
  if (mMergeDenominator) {
    uint32_t counter = mMergeDenominator >> 16;
    uint32_t denominator = mMergeDenominator & 0xffff;

    *mIdeal = !!(counter / denominator);
    mMergeDenominator = 0;
  }
}

NormalizedConstraintSet::LongRange::LongRange(
    const nsCString& aName,
    const dom::Optional<dom::OwningLongOrConstrainLongRange>& aOther,
    bool advanced)
    : Range<int32_t>(aName,
                     1 + INT32_MIN,  // +1 avoids Windows compiler bug
                     INT32_MAX) {
  if (!aOther.WasPassed()) {
    return;
  }
  const auto& other = aOther.Value();
  if (other.IsLong()) {
    if (advanced) {
      mMin = mMax = other.GetAsLong();
    } else {
      mIdeal.emplace(other.GetAsLong());
    }
  } else {
    SetFrom(other.GetAsConstrainLongRange());
  }
}

NormalizedConstraintSet::LongLongRange::LongLongRange(
    const nsCString& aName, const dom::Optional<int64_t>& aOther)
    : Range<int64_t>(aName,
                     1 + INT64_MIN,  // +1 avoids Windows compiler bug
                     INT64_MAX) {
  if (aOther.WasPassed()) {
    mIdeal.emplace(aOther.Value());
  }
}

NormalizedConstraintSet::DoubleRange::DoubleRange(
    const nsCString& aName,
    const dom::Optional<dom::OwningDoubleOrConstrainDoubleRange>& aOther,
    bool advanced)
    : Range<double>(aName, -std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()) {
  if (!aOther.WasPassed()) {
    return;
  }
  const auto& other = aOther.Value();
  if (other.IsDouble()) {
    if (advanced) {
      mMin = mMax = other.GetAsDouble();
    } else {
      mIdeal.emplace(other.GetAsDouble());
    }
  } else {
    SetFrom(other.GetAsConstrainDoubleRange());
  }
}

NormalizedConstraintSet::BooleanRange::BooleanRange(
    const nsCString& aName,
    const dom::Optional<dom::OwningBooleanOrConstrainBooleanParameters>& aOther,
    bool advanced)
    : Range<bool>(aName, false, true) {
  if (!aOther.WasPassed()) {
    return;
  }
  const auto& other = aOther.Value();
  if (other.IsBoolean()) {
    if (advanced) {
      mMin = mMax = other.GetAsBoolean();
    } else {
      mIdeal.emplace(other.GetAsBoolean());
    }
  } else {
    const auto& r = other.GetAsConstrainBooleanParameters();
    if (r.mIdeal.WasPassed()) {
      mIdeal.emplace(r.mIdeal.Value());
    }
    if (r.mExact.WasPassed()) {
      mMin = r.mExact.Value();
      mMax = r.mExact.Value();
    }
  }
}

NormalizedConstraintSet::StringRange::StringRange(
    const nsCString& aName,
    const dom::Optional<
        dom::OwningStringOrStringSequenceOrConstrainDOMStringParameters>&
        aOther,
    bool advanced)
    : BaseRange(aName) {
  if (!aOther.WasPassed()) {
    return;
  }
  const auto& other = aOther.Value();
  if (other.IsString()) {
    if (advanced) {
      mExact.insert(other.GetAsString());
    } else {
      mIdeal.insert(other.GetAsString());
    }
  } else if (other.IsStringSequence()) {
    if (advanced) {
      mExact.clear();
      for (const auto& str : other.GetAsStringSequence()) {
        mExact.insert(str);
      }
    } else {
      mIdeal.clear();
      for (const auto& str : other.GetAsStringSequence()) {
        mIdeal.insert(str);
      }
    }
  } else {
    SetFrom(other.GetAsConstrainDOMStringParameters());
  }
}

void NormalizedConstraintSet::StringRange::SetFrom(
    const dom::ConstrainDOMStringParameters& aOther) {
  if (aOther.mIdeal.WasPassed()) {
    mIdeal.clear();
    if (aOther.mIdeal.Value().IsString()) {
      mIdeal.insert(aOther.mIdeal.Value().GetAsString());
    } else {
      for (const auto& str : aOther.mIdeal.Value().GetAsStringSequence()) {
        mIdeal.insert(str);
      }
    }
  }
  if (aOther.mExact.WasPassed()) {
    mExact.clear();
    if (aOther.mExact.Value().IsString()) {
      mExact.insert(aOther.mExact.Value().GetAsString());
    } else {
      for (const auto& str : aOther.mExact.Value().GetAsStringSequence()) {
        mExact.insert(str);
      }
    }
  }
}

auto NormalizedConstraintSet::StringRange::Clamp(const ValueType& n) const
    -> ValueType {
  if (mExact.empty()) {
    return n;
  }
  ValueType result;
  for (const auto& entry : n) {
    if (mExact.find(entry) != mExact.end()) {
      result.insert(entry);
    }
  }
  return result;
}

bool NormalizedConstraintSet::StringRange::Intersects(
    const StringRange& aOther) const {
  if (mExact.empty() || aOther.mExact.empty()) {
    return true;
  }

  ValueType intersection;
  set_intersection(mExact.begin(), mExact.end(), aOther.mExact.begin(),
                   aOther.mExact.end(),
                   std::inserter(intersection, intersection.begin()));
  return !intersection.empty();
}

void NormalizedConstraintSet::StringRange::Intersect(
    const StringRange& aOther) {
  if (aOther.mExact.empty()) {
    return;
  }

  ValueType intersection;
  set_intersection(mExact.begin(), mExact.end(), aOther.mExact.begin(),
                   aOther.mExact.end(),
                   std::inserter(intersection, intersection.begin()));
  mExact = intersection;
}

bool NormalizedConstraintSet::StringRange::Merge(const StringRange& aOther) {
  if (!Intersects(aOther)) {
    return false;
  }
  Intersect(aOther);

  ValueType unioned;
  set_union(mIdeal.begin(), mIdeal.end(), aOther.mIdeal.begin(),
            aOther.mIdeal.end(), std::inserter(unioned, unioned.begin()));
  mIdeal = unioned;
  return true;
}

NormalizedConstraints::NormalizedConstraints(
    const dom::MediaTrackConstraints& aOther)
    : NormalizedConstraintSet(aOther, false) {
  if (aOther.mAdvanced.WasPassed()) {
    for (const auto& entry : aOther.mAdvanced.Value()) {
      mAdvanced.push_back(NormalizedConstraintSet(entry, true));
    }
  }
}

FlattenedConstraints::FlattenedConstraints(const NormalizedConstraints& aOther)
    : NormalizedConstraintSet(aOther) {
  for (const auto& set : aOther.mAdvanced) {
    // Must only apply compatible i.e. inherently non-overconstraining sets
    // This rule is pretty much why this code is centralized here.
    if (mWidth.Intersects(set.mWidth) && mHeight.Intersects(set.mHeight) &&
        mFrameRate.Intersects(set.mFrameRate)) {
      mWidth.Intersect(set.mWidth);
      mHeight.Intersect(set.mHeight);
      mFrameRate.Intersect(set.mFrameRate);
    }
    if (mEchoCancellation.Intersects(set.mEchoCancellation)) {
      mEchoCancellation.Intersect(set.mEchoCancellation);
    }
    if (mNoiseSuppression.Intersects(set.mNoiseSuppression)) {
      mNoiseSuppression.Intersect(set.mNoiseSuppression);
    }
    if (mAutoGainControl.Intersects(set.mAutoGainControl)) {
      mAutoGainControl.Intersect(set.mAutoGainControl);
    }
    if (mChannelCount.Intersects(set.mChannelCount)) {
      mChannelCount.Intersect(set.mChannelCount);
    }
  }
}

// MediaEngine helper
//
// The full algorithm for all devices.
//
// Fitness distance returned as integer math * 1000. Infinity = UINT32_MAX

// First, all devices have a minimum distance based on their deviceId.
// If you have no other constraints, use this one. Reused by all device types.

/* static */
bool MediaConstraintsHelper::SomeSettingsFit(
    const NormalizedConstraints& aConstraints, const MediaEnginePrefs& aPrefs,
    const nsTArray<RefPtr<LocalMediaDevice>>& aDevices) {
  nsTArray<const NormalizedConstraintSet*> sets;
  sets.AppendElement(&aConstraints);

  MOZ_ASSERT(!aDevices.IsEmpty());
  for (const auto& device : aDevices) {
    auto distance =
        device->GetBestFitnessDistance(sets, aPrefs, CallerType::NonSystem);
    if (distance != UINT32_MAX) {
      return true;
    }
  }
  return false;
}

// Fitness distance returned as integer math * 1000. Infinity = UINT32_MAX

/* static */
uint32_t MediaConstraintsHelper::FitnessDistance(
    const Maybe<nsString>& aN,
    const NormalizedConstraintSet::StringRange& aParams) {
  if (!aParams.mExact.empty() &&
      (aN.isNothing() || aParams.mExact.find(*aN) == aParams.mExact.end())) {
    return UINT32_MAX;
  }
  if (!aParams.mIdeal.empty() &&
      (aN.isNothing() || aParams.mIdeal.find(*aN) == aParams.mIdeal.end())) {
    return 1000;
  }
  return 0;
}

/* static */ const char* MediaConstraintsHelper::SelectSettings(
    const NormalizedConstraints& aConstraints, const MediaEnginePrefs& aPrefs,
    nsTArray<RefPtr<LocalMediaDevice>>& aDevices, CallerType aCallerType) {
  const auto& c = aConstraints;
  LogConstraints(c);

  if (!aDevices.IsEmpty() &&
      aDevices[0]->Kind() == dom::MediaDeviceKind::Videoinput &&
      aPrefs.mResizeModeEnabled) {
    // Check invalid exact resizeMode constraint (not a device property)
    nsString none =
        NS_ConvertASCIItoUTF16(dom::GetEnumString(VideoResizeModeEnum::None));
    nsString crop = NS_ConvertASCIItoUTF16(
        dom::GetEnumString(VideoResizeModeEnum::Crop_and_scale));
    if (FitnessDistance(Some(none), c.mResizeMode) == UINT32_MAX &&
        FitnessDistance(Some(crop), c.mResizeMode) == UINT32_MAX) {
      return "resizeMode";
    }
  }

  // First apply top-level constraints.

  // Stack constraintSets that pass, starting with the required one, because the
  // whole stack must be re-satisfied each time a capability-set is ruled out
  // (this avoids storing state or pushing algorithm into the lower-level code).
  nsTArray<RefPtr<LocalMediaDevice>> unsatisfactory;
  nsTArray<const NormalizedConstraintSet*> aggregateConstraints;
  aggregateConstraints.AppendElement(&c);

  std::multimap<uint32_t, RefPtr<LocalMediaDevice>> ordered;

  for (uint32_t i = 0; i < aDevices.Length();) {
    uint32_t distance = aDevices[i]->GetBestFitnessDistance(
        aggregateConstraints, aPrefs, aCallerType);
    if (distance == UINT32_MAX) {
      unsatisfactory.AppendElement(std::move(aDevices[i]));
      aDevices.RemoveElementAt(i);
    } else {
      ordered.insert(std::make_pair(distance, aDevices[i]));
      ++i;
    }
  }
  if (aDevices.IsEmpty()) {
    return FindBadConstraint(c, aPrefs, unsatisfactory);
  }

  // Order devices by shortest distance
  for (auto& ordinal : ordered) {
    aDevices.RemoveElement(ordinal.second);
    aDevices.AppendElement(ordinal.second);
  }

  // Then apply advanced constraints.

  for (const auto& advanced : c.mAdvanced) {
    aggregateConstraints.AppendElement(&advanced);
    nsTArray<RefPtr<LocalMediaDevice>> rejects;
    for (uint32_t j = 0; j < aDevices.Length();) {
      uint32_t distance = aDevices[j]->GetBestFitnessDistance(
          aggregateConstraints, aPrefs, aCallerType);
      if (distance == UINT32_MAX) {
        rejects.AppendElement(std::move(aDevices[j]));
        aDevices.RemoveElementAt(j);
      } else {
        ++j;
      }
    }
    if (aDevices.IsEmpty()) {
      aDevices.AppendElements(std::move(rejects));
      aggregateConstraints.RemoveLastElement();
    }
  }
  return nullptr;
}

/* static */ const char* MediaConstraintsHelper::FindBadConstraint(
    const NormalizedConstraints& aConstraints, const MediaEnginePrefs& aPrefs,
    const nsTArray<RefPtr<LocalMediaDevice>>& aDevices) {
  // The spec says to report a constraint that satisfies NONE
  // of the sources. Unfortunately, this is a bit laborious to find out, and
  // requires updating as new constraints are added!
  const auto& c = aConstraints;

  if (aDevices.IsEmpty() ||
      !SomeSettingsFit(NormalizedConstraints(), aPrefs, aDevices)) {
    return "";
  }
  {
    NormalizedConstraints fresh;
    fresh.mDeviceId = c.mDeviceId;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "deviceId";
    }
  }
  {
    NormalizedConstraints fresh;
    fresh.mGroupId = c.mGroupId;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "groupId";
    }
  }
  {
    NormalizedConstraints fresh;
    fresh.mWidth = c.mWidth;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "width";
    }
  }
  {
    NormalizedConstraints fresh;
    fresh.mHeight = c.mHeight;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "height";
    }
  }
  {
    NormalizedConstraints fresh;
    fresh.mFrameRate = c.mFrameRate;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "frameRate";
    }
  }
  {
    NormalizedConstraints fresh;
    fresh.mFacingMode = c.mFacingMode;
    if (!SomeSettingsFit(fresh, aPrefs, aDevices)) {
      return "facingMode";
    }
  }
  return "";
}

/* static */
const char* MediaConstraintsHelper::FindBadConstraint(
    const NormalizedConstraints& aConstraints, const MediaEnginePrefs& aPrefs,
    const MediaDevice* aMediaDevice) {
  NormalizedConstraints c(aConstraints);
  NormalizedConstraints empty;
  c.mDeviceId = empty.mDeviceId;
  c.mGroupId = empty.mGroupId;
  AutoTArray<RefPtr<LocalMediaDevice>, 1> devices;
  devices.EmplaceBack(
      new LocalMediaDevice(aMediaDevice, u""_ns, u""_ns, u""_ns));
  return FindBadConstraint(c, aPrefs, devices);
}

static void LogConstraintStringRange(
    const NormalizedConstraintSet::StringRange& aRange) {
  if (aRange.mExact.size() <= 1 && aRange.mIdeal.size() <= 1) {
    LOG("  %s: { exact: [%s], ideal: [%s] }", aRange.mName.get(),
        (aRange.mExact.empty()
             ? ""
             : NS_ConvertUTF16toUTF8(*aRange.mExact.begin()).get()),
        (aRange.mIdeal.empty()
             ? ""
             : NS_ConvertUTF16toUTF8(*aRange.mIdeal.begin()).get()));
  } else {
    LOG("  %s: { exact: [", aRange.mName.get());
    for (const auto& entry : aRange.mExact) {
      LOG("      %s,", NS_ConvertUTF16toUTF8(entry).get());
    }
    LOG("    ], ideal: [");
    for (const auto& entry : aRange.mIdeal) {
      LOG("      %s,", NS_ConvertUTF16toUTF8(entry).get());
    }
    LOG("    ]}");
  }
}

template <typename T>
static void LogConstraintRange(
    const NormalizedConstraintSet::Range<T>& aRange) {
  if (aRange.mIdeal.isSome()) {
    LOG("  %s: { min: %d, max: %d, ideal: %d }", aRange.mName.get(),
        aRange.mMin, aRange.mMax, aRange.mIdeal.valueOr(0));
  } else {
    LOG("  %s: { min: %d, max: %d }", aRange.mName.get(), aRange.mMin,
        aRange.mMax);
  }
}

template <>
void LogConstraintRange(const NormalizedConstraintSet::Range<double>& aRange) {
  if (aRange.mIdeal.isSome()) {
    LOG("  %s: { min: %f, max: %f, ideal: %f }", aRange.mName.get(),
        aRange.mMin, aRange.mMax, aRange.mIdeal.valueOr(0));
  } else {
    LOG("  %s: { min: %f, max: %f }", aRange.mName.get(), aRange.mMin,
        aRange.mMax);
  }
}

/* static */
void MediaConstraintsHelper::LogConstraints(
    const NormalizedConstraintSet& aConstraints) {
  const auto& c = aConstraints;
  LOG("Constraints: {");
  LOG("%s", [&]() {
    LogConstraintRange(c.mWidth);
    LogConstraintRange(c.mHeight);
    LogConstraintRange(c.mFrameRate);
    LogConstraintStringRange(c.mMediaSource);
    LogConstraintStringRange(c.mFacingMode);
    LogConstraintStringRange(c.mResizeMode);
    LogConstraintStringRange(c.mDeviceId);
    LogConstraintStringRange(c.mGroupId);
    LogConstraintRange(c.mEchoCancellation);
    LogConstraintRange(c.mAutoGainControl);
    LogConstraintRange(c.mNoiseSuppression);
    LogConstraintRange(c.mChannelCount);
    return "}";
  }());
}

/* static */
Maybe<VideoResizeModeEnum> MediaConstraintsHelper::GetResizeMode(
    const NormalizedConstraintSet& aConstraints,
    const MediaEnginePrefs& aPrefs) {
  if (!aPrefs.mResizeModeEnabled) {
    return Nothing();
  }
  auto defaultResizeMode = aPrefs.mResizeMode;
  nsString defaultResizeModeString =
      NS_ConvertASCIItoUTF16(dom::GetEnumString(defaultResizeMode));
  uint32_t distanceToDefault = MediaConstraintsHelper::FitnessDistance(
      Some(defaultResizeModeString), aConstraints.mResizeMode);
  if (distanceToDefault == 0) {
    return Some(defaultResizeMode);
  }
  VideoResizeModeEnum otherResizeMode =
      (defaultResizeMode == VideoResizeModeEnum::None)
          ? VideoResizeModeEnum::Crop_and_scale
          : VideoResizeModeEnum::None;
  nsString otherResizeModeString =
      NS_ConvertASCIItoUTF16(dom::GetEnumString(otherResizeMode));
  uint32_t distanceToOther = MediaConstraintsHelper::FitnessDistance(
      Some(otherResizeModeString), aConstraints.mResizeMode);
  return Some((distanceToDefault <= distanceToOther) ? defaultResizeMode
                                                     : otherResizeMode);
}

}  // namespace mozilla
