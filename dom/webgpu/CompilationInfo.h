/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GPU_CompilationInfo_H_
#define GPU_CompilationInfo_H_

#include "CompilationMessage.h"
#include "ObjectModel.h"
#include "nsWrapperCache.h"

namespace mozilla::webgpu {
class ShaderModule;

class CompilationInfo final : public nsWrapperCache, public ChildOf<Device> {
 public:
  GPU_DECL_CYCLE_COLLECTION(CompilationInfo)
  GPU_DECL_JS_WRAP(CompilationInfo)

  explicit CompilationInfo(Device* const aParent);

  void SetMessages(
      nsTArray<mozilla::webgpu::WebGPUCompilationMessage>& aMessages);

  void GetMessages(
      nsTArray<RefPtr<mozilla::webgpu::CompilationMessage>>& aMessages);

 private:
  virtual ~CompilationInfo();

  nsTArray<RefPtr<mozilla::webgpu::CompilationMessage>> mMessages;
};

}  // namespace mozilla::webgpu

#endif  // GPU_CompilationInfo_H_
