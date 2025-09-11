/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Borrowed from browser_PanelMultiView_keyboard.js
async function expectFocusAfterKey(aKey, aFocus) {
  let res = aKey.match(/^(Shift\+)?(.+)$/);
  let shift = Boolean(res[1]);
  let key;
  if (res[2].length == 1) {
    key = res[2]; // Character.
  } else {
    key = "KEY_" + res[2]; // Tab, ArrowRight, etc.
  }
  info("Waiting for focus on " + aFocus.id);
  let focused = BrowserTestUtils.waitForEvent(aFocus, "focus");
  EventUtils.synthesizeKey(key, { shiftKey: shift });
  await focused;
  ok(true, aFocus.id + " focused after " + aKey + " pressed");
}

/**
 * Tests that the panel can be navigated with Tab and Arrow keys.
 */
add_task(async function test_keyboard_navigation_in_panel() {
  let content = await openPanel({
    isSignedOut: false,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection-content component should be present"
  );

  await expectFocusAfterKey("Tab", content.connectionToggleEl);
  await expectFocusAfterKey("Tab", content.upgradeEl.querySelector("a"));
  await expectFocusAfterKey(
    "Tab",
    content.upgradeEl.querySelector("#upgrade-vpn-button")
  );
  await expectFocusAfterKey("Tab", content.headerEl.helpButtonEl);
  // Loop back around
  await expectFocusAfterKey("Tab", content.connectionToggleEl);

  await expectFocusAfterKey("ArrowDown", content.upgradeEl.querySelector("a"));
  await expectFocusAfterKey(
    "ArrowDown",
    content.upgradeEl.querySelector("#upgrade-vpn-button")
  );
  await expectFocusAfterKey("ArrowDown", content.headerEl.helpButtonEl);
  // Loop back around
  await expectFocusAfterKey("ArrowDown", content.connectionToggleEl);

  // Loop backwards
  await expectFocusAfterKey("Shift+Tab", content.headerEl.helpButtonEl);

  await closePanel();
});
