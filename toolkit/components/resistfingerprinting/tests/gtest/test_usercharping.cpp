/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/gtest/nsUserCharacteristics.h"

#include "mozilla/glean/GleanPings.h"
#include "mozilla/glean/ResistfingerprintingMetrics.h"

using namespace mozilla;

const auto* const kUUIDPref =
    "toolkit.telemetry.user_characteristics_ping.uuid";

TEST(ResistFingerprinting, UserCharacteristics_Simple)
{
  mozilla::glean::characteristics::max_touch_points.Set(7);

  ASSERT_TRUE(mozilla::glean_pings::UserCharacteristics.TestSubmission(
      [](const nsACString& aReason) {
        ASSERT_EQ(
            7, mozilla::glean::characteristics::max_touch_points.TestGetValue()
                   .unwrap()
                   .ref());
      },
      []() { mozilla::glean_pings::UserCharacteristics.Submit(); }));
}

TEST(ResistFingerprinting, UserCharacteristics_Complex)
{
  nsUserCharacteristics::PopulateDataAndEventuallySubmit(
      /* aUpdatePref = */ false, /* aTesting = */ true);

  ASSERT_TRUE(mozilla::glean_pings::UserCharacteristics.TestSubmission(
      [](const nsACString& aReason) {
        ASSERT_STRNE("", mozilla::glean::characteristics::client_identifier
                             .TestGetValue()
                             .unwrap()
                             .value()
                             .get());

        nsCString fullUuidStr;
        Preferences::GetCString(kUUIDPref, fullUuidStr);

        // Remove the '{' and '}'
        nsAutoCString uuidString;
        uuidString = Substring(fullUuidStr, 1, NSID_LENGTH - 3);

        ASSERT_STREQ(
            uuidString.get(),
            mozilla::glean::characteristics::client_identifier.TestGetValue()
                .unwrap()
                .value()
                .get());
        ASSERT_EQ(
            testing::MaxTouchPoints(),
            mozilla::glean::characteristics::max_touch_points.TestGetValue()
                .unwrap()
                .ref());
      },
      []() { nsUserCharacteristics::SubmitPing(); }));
}

TEST(ResistFingerprinting, UserCharacteristics_ClearPref)
{
  nsCString originalUUID;

  ASSERT_TRUE(mozilla::glean_pings::UserCharacteristics.TestSubmission(
      [&originalUUID](const nsACString& aReason) {
        originalUUID =
            mozilla::glean::characteristics::client_identifier.TestGetValue()
                .unwrap()
                .value()
                .get();
        ASSERT_STRNE("", mozilla::glean::characteristics::client_identifier
                             .TestGetValue()
                             .unwrap()
                             .value()
                             .get());

        nsCString fullUuidStr;
        Preferences::GetCString(kUUIDPref, fullUuidStr);

        // Remove the '{' and '}'
        nsAutoCString uuidString;
        uuidString = Substring(fullUuidStr, 1, NSID_LENGTH - 3);

        ASSERT_STREQ(
            uuidString.get(),
            mozilla::glean::characteristics::client_identifier.TestGetValue()
                .unwrap()
                .value()
                .get());
      },
      []() {
        nsUserCharacteristics::PopulateDataAndEventuallySubmit(
            /* aUpdatePref = */ false, /* aTesting = */ true);
        nsUserCharacteristics::SubmitPing();
      }));

  auto original_value =
      Preferences::GetBool("datareporting.healthreport.uploadEnabled");
  Preferences::SetBool("datareporting.healthreport.uploadEnabled", true);
  Preferences::SetBool("datareporting.healthreport.uploadEnabled", false);

  ASSERT_TRUE(mozilla::glean_pings::UserCharacteristics.TestSubmission(
      [](const nsACString& aReason) {
        // Assert that the pref is blank
        nsAutoCString uuidValue;
        Preferences::GetCString(kUUIDPref, uuidValue);
        ASSERT_STREQ("", uuidValue.get());
      },
      []() { nsUserCharacteristics::SubmitPing(); }));

  Preferences::SetBool("datareporting.healthreport.uploadEnabled", true);
  ASSERT_TRUE(mozilla::glean_pings::UserCharacteristics.TestSubmission(
      [&originalUUID](const nsACString& aReason) {
        // Assert that the new UUID is different from the old one
        ASSERT_STRNE(
            originalUUID.get(),
            mozilla::glean::characteristics::client_identifier.TestGetValue()
                .unwrap()
                .value()
                .get());

        // Assert that the pref is not blank
        nsAutoCString uuidValue;
        Preferences::GetCString(kUUIDPref, uuidValue);
        ASSERT_STRNE("", uuidValue.get());
      },
      []() {
        nsUserCharacteristics::PopulateDataAndEventuallySubmit(
            /* aUpdatePref = */ false, /* aTesting = */ true);
        nsUserCharacteristics::SubmitPing();
      }));

  Preferences::SetBool("datareporting.healthreport.uploadEnabled",
                       original_value);
}

const auto* const kLastVersionPref =
    "toolkit.telemetry.user_characteristics_ping.last_version_sent";
const auto* const kCurrentVersionPref =
    "toolkit.telemetry.user_characteristics_ping.current_version";
const auto* const kOptOutPref =
    "toolkit.telemetry.user_characteristics_ping.opt-out";
const auto* const kSendOncePref =
    "toolkit.telemetry.user_characteristics_ping.send-once";
const auto* const kResistFingerprintingPref = "privacy.resistFingerprinting";
const auto* const kResistFingerprintingPrefPBMode =
    "privacy.resistFingerprinting.pbmode";
const auto* const kFingerprintingProtectionOverrides =
    "privacy.fingerprintingProtection.overrides";
const auto* const kBaselineFPPOverridesPref =
    "privacy.baselineFingerprintingProtection.overrides";

TEST(ResistFingerprinting, UserCharacteristics_ShouldSubmit)
{
  // Test nsUserCharacteristics::ShouldSubmit()

  // Make sure kCurrentVersionPref > kLastVersionPref and kCurrentVersionPref !=
  // 0
  Preferences::SetInt(kCurrentVersionPref, 1);
  Preferences::SetInt(kLastVersionPref, 0);

  // Verify ShouldSubmit returns true when kCurrentVersionPref >
  // kLastVersionPref
  ASSERT_TRUE(nsUserCharacteristics::ShouldSubmit());

  // Verify opting-out works
  Preferences::SetBool(kOptOutPref, true);
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());

  Preferences::SetBool(kOptOutPref, false);
  ASSERT_TRUE(nsUserCharacteristics::ShouldSubmit());

  // Verify ShouldSubmit returns false when kCurrentVersionPref = 0
  Preferences::SetInt(kCurrentVersionPref, 0);
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());

  // Verify sending a ping once regardless of version works
  Preferences::SetBool(kSendOncePref, true);
  ASSERT_TRUE(nsUserCharacteristics::ShouldSubmit());
  Preferences::SetInt(kCurrentVersionPref, 1);

  // Verify precedence
  Preferences::SetBool(kOptOutPref, true);
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());
  Preferences::SetBool(kOptOutPref, false);

  // Verify resistFingerprinting prevents submission
  Preferences::SetBool(kResistFingerprintingPref, true);
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());
  Preferences::SetBool(kResistFingerprintingPref, false);

  Preferences::SetBool(kResistFingerprintingPrefPBMode, true);
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());
  Preferences::SetBool(kResistFingerprintingPrefPBMode, false);

  // Verify non-empty fingerprintingProtection overrides prevent submission
  Preferences::SetCString(kFingerprintingProtectionOverrides, "test");
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());
  Preferences::ClearUser(kFingerprintingProtectionOverrides);
  ASSERT_TRUE(nsUserCharacteristics::ShouldSubmit());

  // Verify non-empty baselineFPP overrides prevent submission
  Preferences::SetCString(kBaselineFPPOverridesPref, "test");
  ASSERT_FALSE(nsUserCharacteristics::ShouldSubmit());
  Preferences::ClearUser(kBaselineFPPOverridesPref);
  ASSERT_TRUE(nsUserCharacteristics::ShouldSubmit());
}
