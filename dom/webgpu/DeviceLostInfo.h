/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_DeviceLostInfo_H_
#define GPU_DeviceLostInfo_H_

#include "ObjectModel.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "nsWrapperCache.h"

namespace mozilla::webgpu {
class Device;

class DeviceLostInfo final : public nsWrapperCache {
 public:
  GPU_DECL_CYCLE_COLLECTION(DeviceLostInfo)
  GPU_DECL_JS_WRAP(DeviceLostInfo)

  DeviceLostInfo(nsIGlobalObject* const aGlobal,
                 dom::GPUDeviceLostReason aReason, const nsAString& aMessage)
      : mGlobal(aGlobal), mReason(aReason), mMessage(aMessage) {}

 private:
  virtual ~DeviceLostInfo() = default;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  const dom::GPUDeviceLostReason mReason;
  const nsAutoString mMessage;

 public:
  dom::GPUDeviceLostReason Reason() { return mReason; }

  void GetMessage(nsAString& aValue) const { aValue = mMessage; }

  nsIGlobalObject* GetParentObject() const { return mGlobal; }
};

}  // namespace mozilla::webgpu

#endif  // GPU_DeviceLostInfo_H_
