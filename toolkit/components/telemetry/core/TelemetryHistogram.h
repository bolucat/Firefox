/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TelemetryHistogram_h__
#define TelemetryHistogram_h__

#include "mozilla/TelemetryComms.h"
#include "mozilla/TelemetryHistogramEnums.h"
#include "mozilla/TelemetryProcessEnums.h"
#include "nsXULAppAPI.h"
#include "TelemetryCommon.h"

// This module is internal to Telemetry.  It encapsulates Telemetry's
// histogram accumulation and storage logic.  It should only be used by
// Telemetry.cpp.  These functions should not be used anywhere else.
// For the public interface to Telemetry functionality, see Telemetry.h.

namespace TelemetryHistogram {

void InitializeGlobalState(bool canRecordBase, bool canRecordExtended);
void DeInitializeGlobalState();
#ifdef DEBUG
bool GlobalStateHasBeenInitialized();
#endif

bool CanRecordBase();
void SetCanRecordBase(bool b);
bool CanRecordExtended();
void SetCanRecordExtended(bool b);

void InitHistogramRecordingEnabled();

void Accumulate(mozilla::Telemetry::HistogramID aHistogram, uint32_t aSample);
void Accumulate(mozilla::Telemetry::HistogramID aHistogram,
                const nsTArray<uint32_t>& aSamples);
void Accumulate(mozilla::Telemetry::HistogramID aID, const nsCString& aKey,
                uint32_t aSample);

void AccumulateCategorical(mozilla::Telemetry::HistogramID aId,
                           const nsCString& aLabel);
void AccumulateCategorical(mozilla::Telemetry::HistogramID aId,
                           const nsCString& aKey, const nsCString& aLabel);

void AccumulateChild(
    mozilla::Telemetry::ProcessID aProcessType,
    const nsTArray<mozilla::Telemetry::HistogramAccumulation>& aAccumulations);
void AccumulateChildKeyed(
    mozilla::Telemetry::ProcessID aProcessType,
    const nsTArray<mozilla::Telemetry::KeyedHistogramAccumulation>&
        aAccumulations);

/**
 * Append the list of registered stores to the given set.
 */
nsresult GetAllStores(mozilla::Telemetry::Common::StringHashSet& set);

nsresult GetCategoricalHistogramLabels(JSContext* aCx,
                                       JS::MutableHandle<JS::Value> aResult);

nsresult GetHistogramById(const nsACString& name, JSContext* cx,
                          JS::MutableHandle<JS::Value> ret);

nsresult GetKeyedHistogramById(const nsACString& name, JSContext* cx,
                               JS::MutableHandle<JS::Value> ret);

const char* GetHistogramName(mozilla::Telemetry::HistogramID id);

uint8_t GetHistogramType(mozilla::Telemetry::HistogramID id);

nsresult CreateHistogramSnapshots(JSContext* aCx,
                                  JS::MutableHandle<JS::Value> aResult,
                                  const nsACString& aStore,
                                  unsigned int aDataset, bool aClearSubsession,
                                  bool aFilterTest = false);

nsresult GetKeyedHistogramSnapshots(JSContext* aCx,
                                    JS::MutableHandle<JS::Value> aResult,
                                    const nsACString& aStore,
                                    unsigned int aDataset,
                                    bool aClearSubsession,
                                    bool aFilterTest = false);

size_t GetHistogramSizesOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf);

}  // namespace TelemetryHistogram

#endif  // TelemetryHistogram_h__
