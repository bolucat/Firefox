/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_enabletrackingprotection_strict() {
  await BrowserTestUtils.withNewTab(
    "about:preferences#privacy",
    async browser => {
      let strictRadio = browser.contentDocument.getElementById("strictRadio");
      is(strictRadio.selected, true, "Strict checkbox should be selected");
      is(strictRadio.disabled, true, "Strict checkbox should be disabled");
    }
  );
});

add_task(async function test_enabletrackingprotection_strict_preferences() {
  is(
    Services.prefs.prefIsLocked("privacy.trackingprotection.enabled"),
    true,
    "Preference should be locked"
  );
  is(
    Services.prefs.getBoolPref("privacy.trackingprotection.enabled"),
    true,
    "Preference should be true"
  );
  is(
    Services.prefs.prefIsLocked(
      "privacy.trackingprotection.allow_list.baseline.enabled"
    ),
    false,
    "Preference should NOT be locked"
  );
  is(
    Services.prefs.prefIsLocked(
      "privacy.trackingprotection.allow_list.convenience.enabled"
    ),
    false,
    "Preference should NOT be locked"
  );
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.baseline.enabled"
    ),
    true,
    "Default value for privacy.trackingprotection.allow_list.baseline.enabled should be true"
  );
  is(
    Services.prefs.getBoolPref(
      "privacy.trackingprotection.allow_list.convenience.enabled"
    ),
    false,
    "Default value for privacy.trackingprotection.allow_list.convenience.enabled should be false"
  );
});
