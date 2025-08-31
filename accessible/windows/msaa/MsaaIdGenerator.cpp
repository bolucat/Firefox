/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MsaaIdGenerator.h"

#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Unused.h"
#include "MsaaAccessible.h"
#include "nsAccessibilityService.h"

namespace mozilla {
namespace a11y {

uint32_t MsaaIdGenerator::GetID() {
  if (!mGetIDCalled) {
    mGetIDCalled = true;
    // this is a static instance, so capturing this here is safe.
    RunOnShutdown([this] {
      if (mReleaseIDTimer) {
        mReleaseIDTimer->Cancel();
        ReleasePendingIDs();
      }
    });
  }
  uint32_t id = mIDSet.GetID();
  MOZ_ASSERT(id <= ((1UL << kNumFullIDBits) - 1UL));
  return ~id;
}

void MsaaIdGenerator::ReleasePendingIDs() {
  for (auto id : mIDsToRelease) {
    mIDSet.ReleaseID(~id);
  }
  mIDsToRelease.Clear();
  mReleaseIDTimer = nullptr;
}

void MsaaIdGenerator::ReleasePendingIdsCallback(nsITimer* aTimer,
                                                void* aClosure) {
  MsaaIdGenerator* gen = (MsaaIdGenerator*)aClosure;
  gen->ReleasePendingIDs();
}

bool MsaaIdGenerator::ReleaseID(uint32_t aID) {
  MOZ_ASSERT(aID != MsaaAccessible::kNoID);
  // Releasing an id means it can be reused. Reusing ids too quickly can
  // cause problems for clients which process events asynchronously.
  // Therefore, we release ids after a short delay. This doesn't seem to be
  // necessary when the cache is disabled, perhaps because the COM runtime
  // holds references to our objects for longer.
  if (nsAccessibilityService::IsShutdown()) {
    // If accessibility is shut down, no more Accessibles will be created.
    // Also, if the service is shut down, it's possible XPCOM is also shutting
    // down, in which case timers won't work. Thus, we release the id
    // immediately.
    mIDSet.ReleaseID(~aID);
    return true;
  }
  const uint32_t kReleaseDelay = 1000;
  mIDsToRelease.AppendElement(aID);
  if (!mReleaseIDTimer) {
    mReleaseIDTimer = NS_NewTimer();
  }
  // mReleaseIDTimer is cancelled on shutdown and this is a static instance,
  // so capturing this here is safe.
  mReleaseIDTimer->InitWithNamedFuncCallback(
      ReleasePendingIdsCallback, this, kReleaseDelay, nsITimer::TYPE_ONE_SHOT,
      "a11y::MsaaIdGenerator::ReleaseIDDelayed"_ns);

  return true;
}

void MsaaIdGenerator::ReleaseID(NotNull<MsaaAccessible*> aMsaaAcc) {
  ReleaseID(aMsaaAcc->GetExistingID());
}

}  // namespace a11y
}  // namespace mozilla
