/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConfigHelpers.h"

#include <windows.h>

#include "mozilla/Logging.h"
#include "nsStringFwd.h"
#include "nsUnicharUtils.h"
#include "sandbox/win/src/policy_engine_opcodes.h"

namespace mozilla {

extern LazyLogModule sSandboxBrokerLog;

#define LOG_E(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Error, (__VA_ARGS__))
#define LOG_W(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Warning, (__VA_ARGS__))

namespace sandboxing {

SizeTrackingConfig::SizeTrackingConfig(sandbox::TargetConfig* aConfig,
                                       int32_t aStoragePages)
    : mConfig(aConfig) {
  MOZ_ASSERT(mConfig);

  // The calculation at the start of sandbox_policy_base.cc allows for 14 pages.
  MOZ_ASSERT(aStoragePages <= 14);

  constexpr int32_t kOneMemPage = 4096;
  mRemainingSize = kOneMemPage * aStoragePages;
}

sandbox::ResultCode SizeTrackingConfig::AllowFileAccess(
    sandbox::FileSemantics aSemantics, const wchar_t* aPattern) {
  // This calculation doesn't allow for wild-cards, pipes or things that have an
  // an NT prefix, but in our use cases this would result in an overestimate, so
  // that is fine for our purposes. Wild-cards mid pattern would be undersized,
  // because of extra opcodes, but we don't have any rules like these.

  // Add 4 to length to allow for \??\ NT prefix added to most rules. The
  // pattern is stored with a length and so the null-terminator is not stored.
  auto patternRuleSize = (wcslen(aPattern) + 4) * sizeof(wchar_t);

  // Each brokered function has a copy of the pattern and a number of opcodes
  // depending on aSemantics. This is generally 1 opcode for the string match
  // and 1 for the action (ASK_BROKER) opcode added when Done is called on the
  // rule. For kAllowReadonly access and disposition checks are also added for
  // create and open making 4 opcodes in total.
  int32_t requiredSize;
  constexpr auto opcodeSize = sizeof(sandbox::PolicyOpcode);
  switch (aSemantics) {
    case sandbox::FileSemantics::kAllowAny:
      // create, open, query, query_full and rename brokered with 2 opcodes.
      requiredSize = (patternRuleSize * 5) + (opcodeSize * 10);
      break;
    case sandbox::FileSemantics::kAllowReadonly:
      // create and open brokered with 4 opcodes
      // query and query_full brokered with 2 opcodes
      requiredSize = (patternRuleSize * 4) + (opcodeSize * 12);
      break;
    case sandbox::FileSemantics::kAllowQuery:
      // query and query_full brokered with 2 opcodes
      requiredSize = (patternRuleSize * 2) + (opcodeSize * 4);
      break;
    default:
      MOZ_CRASH("Unknown FileSemantics");
  }

  if (requiredSize > mRemainingSize) {
    return sandbox::SBOX_ERROR_NO_SPACE;
  }

  mRemainingSize -= requiredSize;
  return mConfig->AllowFileAccess(aSemantics, aPattern);
}

UserFontConfigHelper::UserFontConfigHelper(const wchar_t* aUserFontKeyPath,
                                           const nsString& aWinUserProfile,
                                           const nsString& aLocalAppData)
    : mWinUserProfile(aWinUserProfile), mLocalAppData(aLocalAppData) {
  LSTATUS lStatus = ::RegOpenKeyExW(HKEY_CURRENT_USER, aUserFontKeyPath, 0,
                                    KEY_QUERY_VALUE, &mUserFontKey);
  if (lStatus != ERROR_SUCCESS) {
    // Ensure that mUserFontKey is null on failure.
    mUserFontKey = nullptr;
  }
}

UserFontConfigHelper::~UserFontConfigHelper() {
  if (mUserFontKey) {
    ::RegCloseKey(mUserFontKey);
  }
}

void UserFontConfigHelper::AddRules(SizeTrackingConfig& aConfig) const {
  // Always add rule to allow access to windows user specific fonts dir first.
  nsAutoString windowsUserFontDir(mLocalAppData);
  windowsUserFontDir += uR"(\Microsoft\Windows\Fonts\*)"_ns;
  auto result = aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                        windowsUserFontDir.getW());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add Windows user font dir policy rule.");
    LOG_E("Failed (ResultCode %d) to add read access to: %S", result,
          windowsUserFontDir.getW());
  }

  // We failed to open the registry key, we can't do any more.
  if (!mUserFontKey) {
    return;
  }

  // We don't want the wild-card for comparisons.
  windowsUserFontDir.SetLength(windowsUserFontDir.Length() - 1);

  // Windows user's profile dir with trailing slash for comparisons.
  nsAutoString winUserProfile(mWinUserProfile);
  winUserProfile += L'\\';

  for (DWORD valueIndex = 0; /* break on ERROR_NO_MORE_ITEMS */; ++valueIndex) {
    DWORD keyType;
    wchar_t name[1024];
    wchar_t data[2048];
    auto* dataAsBytes = reinterpret_cast<LPBYTE>(data);
    DWORD nameLength = std::size(name);
    // Pass 1 less wchar_t worth, in case we have to add a null.
    DWORD dataSizeInBytes = sizeof(data) - sizeof(wchar_t);
    LSTATUS lStatus =
        ::RegEnumValueW(mUserFontKey, valueIndex, name, &nameLength, NULL,
                        &keyType, dataAsBytes, &dataSizeInBytes);
    if (lStatus == ERROR_NO_MORE_ITEMS) {
      break;
    }

    // Skip if we failed to retrieve the value.
    if (lStatus != ERROR_SUCCESS) {
      continue;
    }

    // Only strings are used, REG_EXPAND_SZ is not recognized by Fonts panel.
    if (keyType != REG_SZ) {
      continue;
    }

    auto dataSizeInWChars = dataSizeInBytes / sizeof(wchar_t);

    // We test data[dataSizeInWChars - 1] below and might test again after
    // decrementing it, so ensure we have at least 2 wchar_ts. A valid font path
    // couldn't be this short anyway.
    if (dataSizeInWChars < 2) {
      continue;
    }

    // Size might include a null.
    if (data[dataSizeInWChars - 1] == L'\0') {
      --dataSizeInWChars;
    } else {
      // Ensure null terminated.
      data[dataSizeInWChars] = L'\0';
    }

    // Should be path to font file so reject directories.
    if (data[dataSizeInWChars - 1] == L'\\') {
      continue;
    }

    // Skip if not in user's dir.
    if (dataSizeInWChars < winUserProfile.Length() ||
        !winUserProfile.Equals(
            nsDependentSubstring(data, winUserProfile.Length()),
            nsCaseInsensitiveStringComparator)) {
      continue;
    }

    // Skip if in windows user font dir.
    if (dataSizeInWChars > windowsUserFontDir.Length() &&
        windowsUserFontDir.Equals(
            nsDependentSubstring(data, windowsUserFontDir.Length()),
            nsCaseInsensitiveStringComparator)) {
      continue;
    }

    result =
        aConfig.AllowFileAccess(sandbox::FileSemantics::kAllowReadonly, data);
    if (result != sandbox::SBOX_ALL_OK) {
      NS_WARNING("Failed to add specific user font policy rule.");
      LOG_W("Failed (ResultCode %d) to add read access to: %S", result, data);
      if (result == sandbox::SBOX_ERROR_NO_SPACE) {
        return;
      }
    }
  }
}

}  // namespace sandboxing

}  // namespace mozilla
