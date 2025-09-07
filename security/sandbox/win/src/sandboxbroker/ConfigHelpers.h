/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CONFIGHELPERS_H_
#define SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CONFIGHELPERS_H_

#include <minwindef.h>

#include "mozilla/Attributes.h"
#include "nsStringFwd.h"
#include "sandbox/win/src/sandbox_policy.h"

namespace sandbox {
class TargetPolicy;
}  // namespace sandbox

namespace mozilla::sandboxing {

// Used to track size of config and fail if rule will breach the maximum set.
class SizeTrackingConfig {
 public:
  explicit SizeTrackingConfig(sandbox::TargetConfig* aConfig,
                              int32_t aStoragePages);

  virtual sandbox::ResultCode AllowFileAccess(sandbox::FileSemantics aSemantics,
                                              const wchar_t* aPattern);

 private:
  sandbox::TargetConfig* mConfig;
  int32_t mRemainingSize;
};

MOZ_RAII class UserFontConfigHelper final {
 public:
  UserFontConfigHelper(const wchar_t* aUserFontKeyPath,
                       const nsString& aWinUserProfile,
                       const nsString& aLocalAppData);
  ~UserFontConfigHelper();

  void AddRules(sandboxing::SizeTrackingConfig& aPolicy) const;

  UserFontConfigHelper(const UserFontConfigHelper&) = delete;
  UserFontConfigHelper& operator=(const UserFontConfigHelper&) = delete;

 private:
  const nsString& mWinUserProfile;
  const nsString& mLocalAppData;
  HKEY mUserFontKey = nullptr;
};

}  // namespace mozilla::sandboxing

#endif  // SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CONFIGHELPERS_H_
