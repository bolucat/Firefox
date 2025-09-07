/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <string>
#include <windows.h>

#include "nsLiteralString.h"
#include "sandbox/win/src/sandbox.h"
#include "sandbox/win/src/app_container.h"
#include "sandbox/win/src/policy_engine_opcodes.h"
#include "../src/sandboxbroker/ConfigHelpers.h"

using namespace sandbox;
using mozilla::sandboxing::UserFontConfigHelper;
using ::testing::Eq;
using ::testing::Expectation;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::StrictMock;

static const nsLiteralString sWinUserProfile = uR"(C:\Users\Moz User)"_ns;
static const nsLiteralString sLocalAppData =
    uR"(C:\Users\Moz User\AppData\Local)"_ns;
static const wchar_t* sWinUserFonts =
    LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\*)";
static const wchar_t* sTestRegKey = LR"(Software\MozFontsPathsTest)";
static const wchar_t* sTestFailRegKey = LR"(Software\MozFontsPathsTestFail)";

namespace mozilla {

class MockConfig : public TargetConfig {
 public:
  MOCK_METHOD(ResultCode, AllowFileAccess,
              (FileSemantics semantics, const wchar_t* pattern), (override));

  // Remaining methods should not be called during tests.
  MOCK_METHOD(bool, IsConfigured, (), (const, override));
  MOCK_METHOD(ResultCode, SetTokenLevel,
              (TokenLevel initial, TokenLevel lockdown), (override));
  MOCK_METHOD(TokenLevel, GetInitialTokenLevel, (), (const, override));
  MOCK_METHOD(TokenLevel, GetLockdownTokenLevel, (), (const, override));
  MOCK_METHOD(void, SetDoNotUseRestrictingSIDs, (), (override));
  MOCK_METHOD(bool, GetUseRestrictingSIDs, (), (override));
  MOCK_METHOD(void, SetForceKnownDllLoadingFallback, (), (override));
  MOCK_METHOD(ResultCode, SetJobLevel,
              (JobLevel job_level, uint32_t ui_exceptions), (override));
  MOCK_METHOD(JobLevel, GetJobLevel, (), (const, override));
  MOCK_METHOD(void, SetJobMemoryLimit, (size_t memory_limit), (override));
  MOCK_METHOD(ResultCode, AllowNamedPipes, (const wchar_t* pattern),
              (override));
  MOCK_METHOD(ResultCode, AllowRegistryRead, (const wchar_t* pattern),
              (override));
  MOCK_METHOD(ResultCode, AllowExtraDlls, (const wchar_t* pattern), (override));
  MOCK_METHOD(ResultCode, SetFakeGdiInit, (), (override));
  MOCK_METHOD(ResultCode, AllowLineBreaking, (), (override));
  MOCK_METHOD(void, AddDllToUnload, (const wchar_t* dll_name), (override));
  MOCK_METHOD(ResultCode, SetIntegrityLevel, (IntegrityLevel level),
              (override));
  MOCK_METHOD(IntegrityLevel, GetIntegrityLevel, (), (const, override));
  MOCK_METHOD(void, SetDelayedIntegrityLevel, (IntegrityLevel level),
              (override));
  MOCK_METHOD(ResultCode, SetLowBox, (const wchar_t* sid), (override));
  MOCK_METHOD(ResultCode, SetProcessMitigations, (MitigationFlags flags),
              (override));
  MOCK_METHOD(MitigationFlags, GetProcessMitigations, (), (override));
  MOCK_METHOD(ResultCode, SetDelayedProcessMitigations, (MitigationFlags flags),
              (override));
  MOCK_METHOD(MitigationFlags, GetDelayedProcessMitigations, (),
              (const, override));
  MOCK_METHOD(void, AddRestrictingRandomSid, (), (override));
  MOCK_METHOD(void, SetLockdownDefaultDacl, (), (override));
  MOCK_METHOD(ResultCode, AddAppContainerProfile,
              (const wchar_t* package_name, bool create_profile), (override));
  MOCK_METHOD(scoped_refptr<AppContainer>, GetAppContainer, (), (override));
  MOCK_METHOD(ResultCode, AddKernelObjectToClose,
              (const wchar_t* handle_type, const wchar_t* handle_name),
              (override));
  MOCK_METHOD(ResultCode, SetDisconnectCsrss, (), (override));
  MOCK_METHOD(void, SetDesktop, (Desktop desktop), (override));
  MOCK_METHOD(void, SetFilterEnvironment, (bool filter), (override));
  MOCK_METHOD(bool, GetEnvironmentFiltered, (), (override));
  MOCK_METHOD(void, SetZeroAppShim, (), (override));
};

#define EXPECT_READONLY_EQ(aRulePath)                                     \
  EXPECT_CALL(mConfig, AllowFileAccess(Eq(FileSemantics::kAllowReadonly), \
                                       StrEq(aRulePath)))

#define EXPECT_READONLY_STARTS(aRulePath)                                 \
  EXPECT_CALL(mConfig, AllowFileAccess(Eq(FileSemantics::kAllowReadonly), \
                                       StartsWith(aRulePath)))

class UserFontConfigHelperTest : public testing::Test {
 protected:
  // We always expect the Windows User font dir rule to be added.
  UserFontConfigHelperTest()
      : mWinUserFontCall(EXPECT_READONLY_EQ(sWinUserFonts)) {
    ::RegCreateKeyExW(HKEY_CURRENT_USER, sTestRegKey, 0, nullptr,
                      REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
                      &mTestUserFontKey, nullptr);
  }

  ~UserFontConfigHelperTest() {
    if (mTestUserFontKey) {
      ::RegCloseKey(mTestUserFontKey);
    }
    ::RegDeleteKeyW(HKEY_CURRENT_USER, sTestRegKey);
  }

  void SetUpPaths(const std::vector<std::wstring_view>& aFontPaths) {
    for (size_t i = 0; i < aFontPaths.size(); ++i) {
      const auto* pathBytes =
          reinterpret_cast<const BYTE*>(aFontPaths[i].data());
      size_t sizeInBytes = (aFontPaths[i].length() + 1) * sizeof(wchar_t);
      ::RegSetValueExW(mTestUserFontKey, std::to_wstring(i).c_str(), 0, REG_SZ,
                       pathBytes, sizeInBytes);
    }
  }

  void CreateHelperAndCallAddRules() {
    UserFontConfigHelper policyHelper(sTestRegKey, sWinUserProfile,
                                      sLocalAppData);
    // Only allow one page to test
    sandboxing::SizeTrackingConfig trackingPolicy(&mConfig, 1);
    policyHelper.AddRules(trackingPolicy);
  }

  // StrictMock because we only expect AllowFileAccess to be called.
  StrictMock<MockConfig> mConfig;
  const Expectation mWinUserFontCall;
  HKEY mTestUserFontKey = nullptr;
};

TEST_F(UserFontConfigHelperTest, WindowsDirRProgramDatauleAddedOnKeyFailure) {
  // Create helper with incorrect key name.
  UserFontConfigHelper policyHelper(sTestFailRegKey, sWinUserProfile,
                                    sLocalAppData);
  sandboxing::SizeTrackingConfig trackingPolicy(&mConfig, 1);
  policyHelper.AddRules(trackingPolicy);
}

TEST_F(UserFontConfigHelperTest, PathsInsideUsersDirAdded) {
  SetUpPaths({LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)"});

  // We expect the windows user font rule to be added first.
  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathsInsideUsersDirAddedIgnoringCase) {
  SetUpPaths({LR"(C:\users\moz uSER\Fonts\FontFile1.ttf)"});

  EXPECT_READONLY_EQ(LR"(C:\users\moz uSER\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathsOutsideUsersDirNotAdded) {
  SetUpPaths({LR"(C:\ProgramData\Fonts\FontFile1.ttf)",
              LR"(C:\programdata\Fonts\FontFile2.ttf)"});

  EXPECT_READONLY_EQ(LR"(C:\ProgramData\Fonts\FontFile1.ttf)").Times(0);
  EXPECT_READONLY_EQ(LR"(C:\programdata\Fonts\FontFile2.ttf)").Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, MultipleFontsInAndOutside) {
  SetUpPaths({
      LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)",
      LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)",
      LR"(C:\Users\Moz User\Fonts\FontFile3.ttf)",
      LR"(C:\ProgramData\Fonts\FontFile1.ttf)",
      LR"(C:\ProgramData\Fonts\FontFile2.ttf)",
  });

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);
  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile2.ttf)")
      .After(mWinUserFontCall);
  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile3.ttf)")
      .After(mWinUserFontCall);
  EXPECT_READONLY_EQ(LR"(C:\ProgramData\Fonts\FontFile1.ttf)").Times(0);
  EXPECT_READONLY_EQ(LR"(C:\ProgramData\Fonts\FontFile2.ttf)").Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, NonStringValueIsIgnored) {
  DWORD regValue = 42;
  ::RegSetValueExW(mTestUserFontKey, L"Liff", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&regValue), sizeof(regValue));
  wchar_t multiPath[] = LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)";
  ::RegSetValueExW(mTestUserFontKey, L"MultiStr", 0, REG_MULTI_SZ,
                   reinterpret_cast<const BYTE*>(multiPath), sizeof(multiPath));
  ::RegSetValueExW(mTestUserFontKey, L"ExpandStr", 0, REG_EXPAND_SZ,
                   reinterpret_cast<const BYTE*>(multiPath), sizeof(multiPath));

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)").Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathNotNullTerminated) {
  // If you just miss the null in the size it stills gets stored with it, so you
  // have to make the next character non-null.
  wchar_t fontPath[] = LR"(C:\Users\Moz User\Fonts\FontFile1.ttfx)";
  ::RegSetValueExW(mTestUserFontKey, L"NoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath),
                   sizeof(fontPath) - (2 * sizeof(wchar_t)));

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz User\Fonts\FontFile1.ttf)")
      .After(mWinUserFontCall);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathEmpty) {
  wchar_t fontPath[] = L"";
  ::RegSetValueExW(mTestUserFontKey, L"EmptyNoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath), sizeof(fontPath));

  EXPECT_READONLY_EQ(fontPath).Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathEmptyNotNullTerminated) {
  // If you just miss the null in the size it stills gets stored with it, so you
  // have to make the next character non-null.
  wchar_t fontPath[] = L"F";
  ::RegSetValueExW(mTestUserFontKey, L"EmptyNoNull", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(fontPath), 0);

  EXPECT_READONLY_EQ(L"").Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, DirsAreIgnored) {
  SetUpPaths({LR"(C:\Users\Moz User\Fonts\)"});

  EXPECT_READONLY_EQ(LR"(C:\Users\Moz Us]er\Fonts\)").Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathsInWindowsUsersFontDirNotAdded) {
  SetUpPaths({
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\FontFile1.ttf)",
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\Sub\FontFile2.ttf)",
  });

  EXPECT_READONLY_EQ(
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\FontFile1.ttf)")
      .Times(0);
  EXPECT_READONLY_EQ(
      LR"(C:\Users\Moz User\AppData\Local\Microsoft\Windows\Fonts\Sub\FontFile2.ttf)")
      .Times(0);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest,
       PathsInWindowsUsersFontDirNotAddedIgnoringCase) {
  SetUpPaths({
      LR"(c:\Users\mOZ user\aPPdATA\Local\microsoft\wINDows\Fonts\FontFile1.ttf)",
      LR"(c:\uSERS\moz user\aPPdATA\lOCAL\MICRosoft\WindOWS\fONTS\Sub\FontFile2.ttf)",
  });

  EXPECT_READONLY_EQ(
      LR"(c:\Users\mOZ user\aPPdATA\Local\microsoft\wINDows\Fonts\FontFile1.ttf)")
      .Times(0);
  EXPECT_READONLY_EQ(
      LR"(c:\uSERS\moz user\aPPdATA\lOCAL\MICRosoft\WindOWS\fONTS\Sub\FontFile2.ttf)")
      .Times(0);

  CreateHelperAndCallAddRules();
}

std::wstring MakeLongFontPath(const wchar_t* aPrefix, const wchar_t* aSuffix) {
  static size_t sReqPathLen = []() {
    // Bytes taken up by the Windows user font path rule.
    size_t winUserFontSpace =
        (12 * sizeof(sandbox::PolicyOpcode)) +
        ((wcslen(sWinUserFonts) + 4) * sizeof(wchar_t) * 4);

    // The test fixture allows for one page.
    size_t remainingSpace = 4096 - winUserFontSpace;

    // We want 3 paths to be too big, so divide by 3 and reverse the formula.
    size_t spacePerFontPath = remainingSpace / 3;
    size_t reqFontLen =
        ((spacePerFontPath - (12 * sizeof(sandbox::PolicyOpcode))) /
         (4 * sizeof(wchar_t))) -
        4;

    // Add on 1 to make it too long for 3 paths.
    return reqFontLen + 1;
  }();

  std::wstring longFontPath(aPrefix);
  longFontPath.append(sReqPathLen - longFontPath.length() - wcslen(aSuffix),
                      'F');
  longFontPath.append(aSuffix);
  return longFontPath;
}

TEST_F(UserFontConfigHelperTest, PathsTooLongForStorage) {
  // These font paths take up enough storage such that, with the Windows user
  // font dir rule, only two will fit in the available 4K of storage. Note that
  // we can't guarantee the order they are returned from the registry.
  auto path1 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"1");
  auto path2 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"2");
  auto path3 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"3");
  SetUpPaths({
      path1,
      path2,
      path3,
  });

  path1.pop_back();
  EXPECT_READONLY_STARTS(path1).Times(2).After(mWinUserFontCall);

  CreateHelperAndCallAddRules();
}

TEST_F(UserFontConfigHelperTest, PathsTooLongOneOutsideUserProfile) {
  // These font paths take up enough storage such that, with the Windows user
  // font dir rule, only two will fit in the available 4K of storage.
  // However one is outside the user profile, so we can be certain about which
  // rules should be added.
  auto path1 = MakeLongFontPath(LR"(C:\ProgramData\)", L"1");
  auto path2 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"2");
  auto path3 = MakeLongFontPath(LR"(C:\Users\Moz User\)", L"3");
  SetUpPaths({
      path1,
      path2,
      path3,
  });

  EXPECT_READONLY_EQ(path1).Times(0);
  EXPECT_READONLY_EQ(path2).After(mWinUserFontCall);
  EXPECT_READONLY_EQ(path3).After(mWinUserFontCall);

  CreateHelperAndCallAddRules();
}

}  // namespace mozilla
