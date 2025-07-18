/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

const { Preferences } = ChromeUtils.importESModule(
  "resource://gre/modules/Preferences.sys.mjs"
);
const { SearchSettings } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchSettings.sys.mjs"
);
const { updateAppInfo, getAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);
const { FileTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/FileTestUtils.sys.mjs"
);
const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);
ChromeUtils.defineESModuleGetters(lazy, {
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
});
const { EnterprisePolicyTesting } = ChromeUtils.importESModule(
  "resource://testing-common/EnterprisePolicyTesting.sys.mjs"
);
const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);

updateAppInfo({
  name: "XPCShell",
  ID: "xpcshell@tests.mozilla.org",
  version: "48",
  platformVersion: "48",
});

// This initializes the policy engine for xpcshell tests
let policies = Cc["@mozilla.org/enterprisepolicies;1"].getService(
  Ci.nsIObserver
);
policies.observe(null, "policies-startup", null);

SearchSettings.SETTINGS_INVALIDATION_DELAY = 100;

async function setupPolicyEngineWithJson(json, customSchema) {
  if (typeof json != "object") {
    let filePath = do_get_file(json ? json : "non-existing-file.json").path;
    return EnterprisePolicyTesting.setupPolicyEngineWithJson(
      filePath,
      customSchema
    );
  }
  return EnterprisePolicyTesting.setupPolicyEngineWithJson(json, customSchema);
}

/**
 * Loads a new enterprise policy, and re-initialise the search service
 * with the new policy. Also waits for the search service to write the settings
 * file to disk.
 *
 * @param {object} json
 *   The enterprise policy to use.
 * @param {object} customSchema
 *   A custom schema to use to validate the enterprise policy.
 */
async function setupPolicyEngineWithJsonWithSearch(json, customSchema) {
  Services.search.wrappedJSObject.reset();
  if (typeof json != "object") {
    let filePath = do_get_file(json ? json : "non-existing-file.json").path;
    await EnterprisePolicyTesting.setupPolicyEngineWithJson(
      filePath,
      customSchema
    );
  } else {
    await EnterprisePolicyTesting.setupPolicyEngineWithJson(json, customSchema);
  }
  let settingsWritten = lazy.SearchTestUtils.promiseSearchNotification(
    "write-settings-to-disk-complete"
  );
  await Services.search.init();
  await settingsWritten;
}

function checkLockedPref(prefName, prefValue) {
  equal(
    Preferences.locked(prefName),
    true,
    `Pref ${prefName} is correctly locked`
  );
  strictEqual(
    Preferences.get(prefName),
    prefValue,
    `Pref ${prefName} has the correct value`
  );
}

function checkUnlockedPref(prefName, prefValue) {
  equal(
    Preferences.locked(prefName),
    false,
    `Pref ${prefName} is correctly unlocked`
  );
  strictEqual(
    Preferences.get(prefName),
    prefValue,
    `Pref ${prefName} has the correct value`
  );
}

function checkUserPref(prefName, prefValue) {
  strictEqual(
    Preferences.get(prefName),
    prefValue,
    `Pref ${prefName} has the correct value`
  );
}

function checkClearPref(prefName) {
  equal(
    Services.prefs.prefHasUserValue(prefName),
    false,
    `Pref ${prefName} has no user value`
  );
}

function checkDefaultPref(prefName, prefValue) {
  let defaultPrefBranch = Services.prefs.getDefaultBranch("");
  let prefType = defaultPrefBranch.getPrefType(prefName);
  notEqual(
    prefType,
    Services.prefs.PREF_INVALID,
    `Pref ${prefName} is set on the default branch`
  );
  strictEqual(
    Preferences.get(prefName),
    prefValue,
    `Pref ${prefName} has the correct value`
  );
}

function checkUnsetPref(prefName) {
  let defaultPrefBranch = Services.prefs.getDefaultBranch("");
  let prefType = defaultPrefBranch.getPrefType(prefName);
  equal(
    prefType,
    Services.prefs.PREF_INVALID,
    `Pref ${prefName} is not set on the default branch`
  );
}

async function assertManagementAPIInstallType(addonId, expectedInstallType) {
  const addon = await AddonManager.getAddonByID(addonId);
  const expectInstalledByPolicy = expectedInstallType === "admin";
  equal(
    addon.isInstalledByEnterprisePolicy,
    expectInstalledByPolicy,
    `Addon should ${
      expectInstalledByPolicy ? "be" : "NOT be"
    } marked as installed by enterprise policy`
  );
  const policy = WebExtensionPolicy.getByID(addonId);
  const pageURL = policy.extension.baseURI.resolve(
    "_generated_background_page.html"
  );
  const page = await ExtensionTestUtils.loadContentPage(pageURL);
  const { id, installType } = await page.spawn([], async () => {
    const res = await this.content.wrappedJSObject.browser.management.getSelf();
    return { id: res.id, installType: res.installType };
  });
  await page.close();
  Assert.equal(id, addonId, "Got results for the expected addon id");
  Assert.equal(
    installType,
    expectedInstallType,
    "Got the expected installType on policy installed extension"
  );
}
