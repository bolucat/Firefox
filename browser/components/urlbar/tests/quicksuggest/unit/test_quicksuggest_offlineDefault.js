/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that Suggest is enabled by default for appropriate locales and disabled
// by default everywhere else. (When Suggest is enabled by default, "offline"
// suggestions are enabled but Merino is not, hence "offline" default.)

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  Preferences: "resource://gre/modules/Preferences.sys.mjs",
});

const EXPECTED_EU_PREFS = {
  "quicksuggest.enabled": true,
  "quicksuggest.dataCollection.enabled": false,
  "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
  "suggest.quicksuggest.nonsponsored": true,
  "suggest.quicksuggest.sponsored": true,
  "addons.featureGate": false,
  "amp.featureGate": false,
  "importantDates.featureGate": true,
  "mdn.featureGate": false,
  "weather.featureGate": true,
  "wikipedia.featureGate": false,
  "yelp.featureGate": false,
};

// All the prefs that are initialized when Suggest is enabled, per region.
const EXPECTED_PREFS_BY_REGION = {
  DE: EXPECTED_EU_PREFS,
  FR: EXPECTED_EU_PREFS,
  GB: {
    "quicksuggest.enabled": true,
    "quicksuggest.dataCollection.enabled": false,
    "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.OFFLINE_ONLY,
    "suggest.quicksuggest.nonsponsored": true,
    "suggest.quicksuggest.sponsored": true,
    "addons.featureGate": false,
    "amp.featureGate": true,
    "importantDates.featureGate": true,
    "mdn.featureGate": false,
    "weather.featureGate": true,
    "wikipedia.featureGate": true,
    "yelp.featureGate": false,
  },
  IT: EXPECTED_EU_PREFS,
  US: {
    "quicksuggest.enabled": true,
    "quicksuggest.dataCollection.enabled": false,
    "quicksuggest.settingsUi": QuickSuggest.SETTINGS_UI.FULL,
    "suggest.quicksuggest.nonsponsored": true,
    "suggest.quicksuggest.sponsored": true,
    "addons.featureGate": true,
    "amp.featureGate": true,
    "importantDates.featureGate": true,
    "mdn.featureGate": true,
    "weather.featureGate": true,
    "wikipedia.featureGate": true,
    "yelp.featureGate": true,
  },
};

// Expected prefs when Suggest is disabled.
const EXPECTED_PREFS_DISABLED = {
  "quicksuggest.enabled": false,
  "quicksuggest.dataCollection.enabled": false,
  "quicksuggest.settingsUi": 0,
  "suggest.quicksuggest.nonsponsored": false,
  "suggest.quicksuggest.sponsored": false,
  "addons.featureGate": false,
  "amp.featureGate": false,
  "importantDates.featureGate": false,
  "mdn.featureGate": false,
  "weather.featureGate": false,
  "wikipedia.featureGate": false,
  "yelp.featureGate": false,
};

add_setup(async () => {
  await UrlbarTestUtils.initNimbusFeature();
});

add_task(async function test() {
  let tests = [
    // Suggest should be enabled
    { region: "DE", locale: "de", expectSuggestToBeEnabled: true },
    { region: "FR", locale: "fr", expectSuggestToBeEnabled: true },
    { region: "GB", locale: "en-US", expectSuggestToBeEnabled: true },
    { region: "GB", locale: "en-CA", expectSuggestToBeEnabled: true },
    { region: "GB", locale: "en-GB", expectSuggestToBeEnabled: true },
    { region: "IT", locale: "it", expectSuggestToBeEnabled: true },
    { region: "US", locale: "en-US", expectSuggestToBeEnabled: true },
    { region: "US", locale: "en-CA", expectSuggestToBeEnabled: true },
    { region: "US", locale: "en-GB", expectSuggestToBeEnabled: true },

    // Suggest should be disabled
    { region: "CA", locale: "en-US", expectSuggestToBeEnabled: false },
    { region: "CA", locale: "en-CA", expectSuggestToBeEnabled: false },
    { region: "DE", locale: "en-US", expectSuggestToBeEnabled: false },
    { region: "FR", locale: "en-US", expectSuggestToBeEnabled: false },
    { region: "GB", locale: "de", expectSuggestToBeEnabled: false },
    { region: "IT", locale: "en-US", expectSuggestToBeEnabled: false },
    { region: "US", locale: "de", expectSuggestToBeEnabled: false },
  ];
  for (let { locale, region, expectSuggestToBeEnabled } of tests) {
    await doTest({ locale, region, expectSuggestToBeEnabled });
  }
});

/**
 * Sets the app's locale and region, reinitializes Suggest, and asserts that the
 * pref values are correct.
 *
 * @param {object} options
 *   Options object.
 * @param {string} options.locale
 *   The locale to simulate.
 * @param {string} options.region
 *   The "home" region to simulate.
 * @param {boolean} options.expectSuggestToBeEnabled
 *   Whether Suggest is expected to be enabled by default for the given locale
 *   and region.
 */
async function doTest({ locale, region, expectSuggestToBeEnabled }) {
  let expectedPrefs = EXPECTED_PREFS_DISABLED;
  if (expectSuggestToBeEnabled) {
    expectedPrefs = EXPECTED_PREFS_BY_REGION[region];
    Assert.ok(
      expectedPrefs,
      "EXPECTED_PREFS_BY_REGION should have an entry for region since expectSuggestToBeEnabled is true, region=" +
        region
    );
  }

  let defaults = new Preferences({
    branch: "browser.urlbar.",
    defaultBranch: true,
  });

  // Setup: Clear any user values and save original default-branch values.
  let originalDefaults = {};
  for (let name of Object.keys(expectedPrefs)) {
    Services.prefs.clearUserPref("browser.urlbar." + name);
    originalDefaults[name] = defaults.get(name);
  }

  // Set the region and locale, call the function, check the pref values.
  await QuickSuggestTestUtils.withLocales({
    homeRegion: region,
    locales: [locale],
    callback: async () => {
      await QuickSuggest._test_reinit();

      for (let [name, value] of Object.entries(expectedPrefs)) {
        // Check the default-branch value.
        Assert.strictEqual(
          defaults.get(name),
          value,
          `Default pref value for ${name}, locale ${locale}, region ${region}`
        );

        // For good measure, also check the return value of `UrlbarPrefs.get`
        // since we use it everywhere. The value should be the same as the
        // default-branch value.
        UrlbarPrefs.get(
          name,
          value,
          `UrlbarPrefs.get() value for ${name}, locale ${locale}, region ${region}`
        );
      }
    },
  });

  // Teardown: Restore original default-branch values for the next task.
  for (let [name, originalDefault] of Object.entries(originalDefaults)) {
    if (originalDefault === undefined) {
      Services.prefs.deleteBranch("browser.urlbar." + name);
    } else {
      defaults.set(name, originalDefault);
    }
  }
}
