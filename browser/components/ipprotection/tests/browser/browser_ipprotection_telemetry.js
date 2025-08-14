/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionService:
    "resource:///modules/ipprotection/IPProtectionService.sys.mjs",
});

async function resetStateToObj(content, originalState) {
  content.state = originalState;
  content.requestUpdate();
  await content.updateComplete;
}

/**
 * Tests that the toggled event is recorded when the VPN
 * is turned on or off
 */
add_task(async function user_toggle_on_and_off() {
  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added to the navbar"
  );

  lazy.IPProtectionService.isSignedIn = true;

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  let panelInitPromise = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:Init"
  );
  button.click();
  await Promise.all([panelShownPromise, panelInitPromise]);

  let panelView = PanelMultiView.getViewNode(
    document,
    IPProtectionWidget.PANEL_ID
  );

  let content = panelView.querySelector(IPProtectionPanel.CONTENT_TAGNAME);

  Assert.ok(content, "Panel content should be present");

  content.state.isSignedIn = true;
  content.requestUpdate();
  await content.updateComplete;

  let toggle = content.connectionToggleEl;
  Assert.ok(toggle, "Status card connection toggle should be present");

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();
  let vpnOnPromise = BrowserTestUtils.waitForEvent(
    lazy.IPProtectionService,
    "IPProtectionService:Started"
  );
  // Toggle the VPN on
  toggle.click();
  await vpnOnPromise;
  let toggledEvents = Glean.ipprotection.toggled.testGetValue();
  Assert.equal(toggledEvents.length, 1, "should have recorded a toggle");
  Assert.equal(toggledEvents[0].category, "ipprotection");
  Assert.equal(toggledEvents[0].name, "toggled");
  Assert.equal(toggledEvents[0].extra.enabled, "true");
  Assert.equal(toggledEvents[0].extra.userAction, "true");

  let vpnOffPromise = BrowserTestUtils.waitForEvent(
    lazy.IPProtectionService,
    "IPProtectionService:Stopped"
  );
  // Toggle the VPN off
  toggle.click();
  await vpnOffPromise;
  toggledEvents = Glean.ipprotection.toggled.testGetValue();
  Assert.equal(toggledEvents.length, 2, "should have recorded a second toggle");
  Assert.equal(toggledEvents[1].category, "ipprotection");
  Assert.equal(toggledEvents[1].name, "toggled");
  Assert.equal(toggledEvents[1].extra.enabled, "false");
  Assert.equal(toggledEvents[1].extra.userAction, "true");

  Services.fog.testResetFOG();

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;
});

/**
 * Tests that the toggled event is recorded when the VPN
 * turns off at browser shutdown
 */
add_task(async function toggle_off_on_shutdown() {
  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added to the navbar"
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  let panelInitPromise = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:Init"
  );
  button.click();
  await Promise.all([panelShownPromise, panelInitPromise]);

  let panelView = PanelMultiView.getViewNode(
    document,
    IPProtectionWidget.PANEL_ID
  );

  let content = panelView.querySelector(IPProtectionPanel.CONTENT_TAGNAME);
  Assert.ok(content, "Panel content should be present");
  content.state.isSignedIn = true;
  content.requestUpdate();
  await content.updateComplete;
  lazy.IPProtectionService.isSignedIn = true;

  let toggle = content.connectionToggleEl;
  Assert.ok(toggle, "Status card connection toggle should be present");

  Services.fog.testResetFOG();

  let vpnOnPromise = BrowserTestUtils.waitForEvent(
    lazy.IPProtectionService,
    "IPProtectionService:Started"
  );
  // Toggle the VPN on
  toggle.click();
  await vpnOnPromise;
  let toggledEvents = Glean.ipprotection.toggled.testGetValue();
  Assert.equal(toggledEvents.length, 1, "should have recorded a toggle");
  Assert.equal(toggledEvents[0].category, "ipprotection");
  Assert.equal(toggledEvents[0].name, "toggled");
  Assert.equal(toggledEvents[0].extra.enabled, "true");
  Assert.equal(toggledEvents[0].extra.userAction, "true");

  // Simulate closing the window
  lazy.IPProtectionService.uninit(true);
  toggledEvents = Glean.ipprotection.toggled.testGetValue();
  Assert.equal(toggledEvents.length, 2, "should have recorded a second toggle");
  Assert.equal(toggledEvents[1].category, "ipprotection");
  Assert.equal(toggledEvents[1].name, "toggled");
  Assert.equal(toggledEvents[1].extra.enabled, "false");
  Assert.equal(toggledEvents[1].extra.userAction, "false");

  Services.fog.testResetFOG();
  // Re-initialize to avoid breaking tests that follow
  lazy.IPProtectionService.init();
  let widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(widget),
    "IP Protection widget should be added back to the navbar"
  );
});

/**
 * Tests that the click upgrade button event is recorded when CTA is clicked
 */
add_task(async function click_upgrade_button() {
  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added to the navbar"
  );

  lazy.IPProtectionService.isSignedIn = true;

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  let panelInitPromise = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:Init"
  );
  button.click();
  await Promise.all([panelShownPromise, panelInitPromise]);

  let panelView = PanelMultiView.getViewNode(
    document,
    IPProtectionWidget.PANEL_ID
  );

  let content = panelView.querySelector(IPProtectionPanel.CONTENT_TAGNAME);

  Assert.ok(content, "Panel content should be present");

  content.state.isSignedIn = true;
  content.requestUpdate();
  await content.updateComplete;

  let upgradeButton = content.upgradeEl.querySelector("#upgrade-vpn-button");

  Services.fog.testResetFOG();
  let newTabPromise = BrowserTestUtils.waitForNewTab(gBrowser);
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  upgradeButton.click();
  let newTab = await newTabPromise;
  await panelHiddenPromise;

  let upgradeEvent = Glean.ipprotection.clickUpgradeButton.testGetValue();
  Assert.equal(upgradeEvent.length, 1, "should have recorded a toggle");

  Services.fog.testResetFOG();

  BrowserTestUtils.removeTab(newTab);
});
