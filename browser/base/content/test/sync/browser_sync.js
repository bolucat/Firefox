/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/browser/components/profiles/tests/browser/head.js",
  this
);

const { FX_RELAY_OAUTH_CLIENT_ID } = ChromeUtils.importESModule(
  "resource://gre/modules/FxAccountsCommon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  CustomizableUITestUtils:
    "resource://testing-common/CustomizableUITestUtils.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
});

let gCUITestUtils = new CustomizableUITestUtils(window);

add_setup(async function () {
  // gSync.init() is called in a requestIdleCallback. Force its initialization.
  gSync.init();
  // This preference gets set the very first time that the FxA menu gets opened,
  // which can cause a state write to occur, which can confuse this test, since
  // when in the signed-out state, we need to set the state _before_ opening
  // the FxA menu (since the panel cannot be opened) in the signed out state.
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.toolbar.accessed", true]],
  });
});

add_task(async function test_ui_state_notification_calls_updateAllUI() {
  let called = false;
  let updateAllUI = gSync.updateAllUI;
  gSync.updateAllUI = () => {
    called = true;
  };

  Services.obs.notifyObservers(null, UIState.ON_UPDATE);
  ok(called);

  gSync.updateAllUI = updateAllUI;
});

add_task(async function test_navBar_button_visibility() {
  const button = document.getElementById("fxa-toolbar-menu-button");
  ok(button.closest("#nav-bar"), "button is in the #nav-bar");

  const state = {
    status: UIState.STATUS_NOT_CONFIGURED,
    syncEnabled: true,
  };
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_NOT_CONFIGURED"
  );

  state.email = "foo@bar.com";
  state.status = UIState.STATUS_NOT_VERIFIED;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_NOT_VERIFIED"
  );

  state.status = UIState.STATUS_LOGIN_FAILED;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_LOGIN_FAILED"
  );

  state.status = UIState.STATUS_SIGNED_IN;
  gSync.updateAllUI(state);
  ok(
    BrowserTestUtils.isVisible(button),
    "Check button visibility with STATUS_SIGNED_IN"
  );

  state.syncEnabled = false;
  gSync.updateAllUI(state);
  is(
    BrowserTestUtils.isVisible(button),
    true,
    "Check button visibility when signed in, but sync disabled"
  );
});

add_task(async function test_overflow_navBar_button_visibility() {
  const button = document.getElementById("fxa-toolbar-menu-button");

  let overflowPanel = document.getElementById("widget-overflow");
  overflowPanel.setAttribute("animate", "false");
  let navbar = document.getElementById(CustomizableUI.AREA_NAVBAR);
  let originalWindowWidth = window.outerWidth;

  registerCleanupFunction(function () {
    overflowPanel.removeAttribute("animate");
    window.resizeTo(originalWindowWidth, window.outerHeight);
    return TestUtils.waitForCondition(
      () => !navbar.hasAttribute("overflowing")
    );
  });

  window.resizeTo(450, window.outerHeight);

  await TestUtils.waitForCondition(() => navbar.hasAttribute("overflowing"));
  ok(navbar.hasAttribute("overflowing"), "Should have an overflowing toolbar.");

  let chevron = document.getElementById("nav-bar-overflow-button");
  let shownPanelPromise = BrowserTestUtils.waitForEvent(
    overflowPanel,
    "popupshown"
  );
  chevron.click();
  await shownPanelPromise;

  ok(button, "fxa-toolbar-menu-button was found");

  const state = {
    status: UIState.STATUS_NOT_CONFIGURED,
    syncEnabled: true,
  };
  gSync.updateAllUI(state);

  ok(
    BrowserTestUtils.isVisible(button),
    "Button should still be visable even if user sync not configured"
  );

  let hidePanelPromise = BrowserTestUtils.waitForEvent(
    overflowPanel,
    "popuphidden"
  );
  chevron.click();
  await hidePanelPromise;
});

add_task(async function setupForPanelTests() {
  /* Proton hides the FxA toolbar button when in the nav-bar and unconfigured.
     To test the panel in all states, we move it to the tabstrip toolbar where
     it will always be visible.
   */
  CustomizableUI.addWidgetToArea(
    "fxa-toolbar-menu-button",
    CustomizableUI.AREA_TABSTRIP
  );

  // make sure it gets put back at the end of the tests
  registerCleanupFunction(() => {
    CustomizableUI.addWidgetToArea(
      "fxa-toolbar-menu-button",
      CustomizableUI.AREA_NAVBAR
    );
  });
});

add_task(async function test_ui_state_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // Setup profiles db
  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.enabled", true]],
  });
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  ok(
    BrowserTestUtils.isVisible(
      document.getElementById("fxa-menu-header-title")
    ),
    "expected toolbar to be visible after opening"
  );
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-connect-device-button",
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: [],
  });

  await checkProfilesButtons(
    document.getElementById("fxa-manage-account-button"),
    true
  );

  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Ensure the profiles menuitem is not shown in the FxA panel when it is
  // displayed as a subpanel of the app menu.
  let appMenuFxAStatusButton = document.getElementById("appMenu-fxa-label2");
  appMenuFxAStatusButton.click();
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  await BrowserTestUtils.waitForEvent(fxaView, "ViewShown");

  // Verify the manage button is shown, just as a basic check.
  let manageButton = fxaView.querySelector("#fxa-manage-account-button");
  ok(
    BrowserTestUtils.isVisible(manageButton),
    "expected manage button to be visible after opening"
  );
  let profilesButton = fxaView.querySelector(
    "PanelUI-fxa-menu-profiles-button"
  );
  ok(!profilesButton, "expected profiles button to not be present");

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_syncing_panel_closed() {
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");
});

add_task(async function test_ui_state_syncing_panel_open() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkSyncNowButtons(false);

  state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");

  await closeFxaPanel();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_ui_state_panel_open_after_syncing() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: true,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkSyncNowButtons(true);

  // Be good citizens and remove the "syncing" state.
  gSync.updateAllUI({
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    lastSync: new Date(),
    syncing: false,
  });
  // Because we switch from syncing to non-syncing, and there's a timeout involved.
  await promiseObserver("test:browser-sync:activity-stop");

  await closeFxaPanel();
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_ui_state_unconfigured() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // Setup profiles db
  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.enabled", true]],
  });
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");

  let state = {
    status: UIState.STATUS_NOT_CONFIGURED,
  };

  gSync.updateAllUI(state);

  checkMenuBarItem("sync-setup");

  checkFxAAvatar("not_configured");

  let signedOffLabel = gSync.fluentStrings.formatValueSync(
    "appmenu-fxa-signed-in-label"
  );

  await openMainPanel();

  checkPanelUIStatusBar({
    description: signedOffLabel,
    titleHidden: true,
    hideFxAText: false,
  });
  await closeTabAndMainPanel();

  await openFxaPanel();

  await checkProfilesButtons(
    document.getElementById("PanelUI-signedin-panel"),
    false
  );

  await closeFxaPanel();
});

add_task(async function test_ui_state_signed_in() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "Foo Bar",
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-connect-device-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: ["PanelUI-fxa-menu-setup-sync-container"],
  });
  checkFxAAvatar("signedin");
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_signed_in_no_display_name() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "foo@bar.com",
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-connect-device-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: ["PanelUI-fxa-menu-setup-sync-container"],
  });
  checkFxAAvatar("signedin");
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "foo@bar.com",
    titleHidden: true,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_unverified() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_NOT_VERIFIED,
    email: "foo@bar.com",
    syncing: false,
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  const expectedLabel = gSync.fluentStrings.formatValueSync(
    "account-finish-account-setup"
  );

  checkMenuBarItem("sync-unverifieditem");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: state.email,
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: ["PanelUI-fxa-menu-connect-device-button"],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: ["PanelUI-fxa-menu-setup-sync-container"],
  });
  checkFxAAvatar("unverified");
  await closeFxaPanel();
  await openMainPanel();

  checkPanelUIStatusBar({
    description: state.email,
    title: expectedLabel,
    titleHidden: false,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_ui_state_loginFailed() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  let state = {
    status: UIState.STATUS_LOGIN_FAILED,
    email: "foo@bar.com",
    displayName: "Foo Bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  const expectedLabel = gSync.fluentStrings.formatValueSync(
    "account-disconnected2"
  );

  checkMenuBarItem("sync-reauthitem");
  checkPanelHeader();
  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-account-signout-button",
    ],
    disabledItems: ["PanelUI-fxa-menu-connect-device-button"],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: ["PanelUI-fxa-menu-setup-sync-container"],
  });
  checkFxAAvatar("login-failed");
  await closeFxaPanel();
  await openMainPanel();

  checkPanelUIStatusBar({
    description: state.displayName,
    title: expectedLabel,
    titleHidden: false,
    hideFxAText: true,
  });

  await closeTabAndMainPanel();
});

add_task(async function test_app_menu_fxa_disabled() {
  const newWin = await BrowserTestUtils.openNewBrowserWindow();

  Services.prefs.setBoolPref("identity.fxaccounts.enabled", true);
  newWin.gSync.onFxaDisabled();

  let menuButton = newWin.document.getElementById("PanelUI-menu-button");
  menuButton.click();
  await BrowserTestUtils.waitForEvent(newWin.PanelUI.mainView, "ViewShown");

  [...newWin.document.querySelectorAll(".sync-ui-item")].forEach(
    e => (e.hidden = false)
  );

  let hidden = BrowserTestUtils.waitForEvent(
    newWin.document,
    "popuphidden",
    true
  );
  newWin.PanelUI.hide();
  await hidden;
  await BrowserTestUtils.closeWindow(newWin);
});

add_task(async function test_history_menu_fxa_disabled() {
  if (AppConstants.platform === "macosx") {
    info(
      "skipping test because the history menu can't be opened in tests on mac"
    );
    return;
  }

  const newWin = await BrowserTestUtils.openNewBrowserWindow();

  Services.prefs.setBoolPref("identity.fxaccounts.enabled", true);
  newWin.gSync.onFxaDisabled();

  const historyMenubarItem = window.document.getElementById("history-menu");
  const historyMenu = window.document.getElementById("historyMenuPopup");
  const syncedTabsItem = historyMenu.querySelector("#sync-tabs-menuitem");
  const menuShown = BrowserTestUtils.waitForEvent(historyMenu, "popupshown");
  historyMenubarItem.openMenu(true);
  await menuShown;

  Assert.equal(
    syncedTabsItem.hidden,
    true,
    "Synced Tabs item should not be displayed when FxAccounts is disabled"
  );
  const menuHidden = BrowserTestUtils.waitForEvent(historyMenu, "popuphidden");
  historyMenu.hidePopup();
  await menuHidden;
  await BrowserTestUtils.closeWindow(newWin);
});

// If the PXI experiment is enabled, we need to ensure we can see the CTAs when signed out
add_task(async function test_experiment_ui_state_unconfigured() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // The experiment enables this bool, found in FeatureManifest.yaml
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );
  let state = {
    status: UIState.STATUS_NOT_CONFIGURED,
  };

  gSync.updateAllUI(state);

  checkMenuBarItem("sync-setup");

  checkFxAAvatar("not_configured");

  let expectedLabel = gSync.fluentStrings.formatValueSync(
    "synced-tabs-fxa-sign-in"
  );

  let expectedDescriptionLabel = gSync.fluentStrings.formatValueSync(
    "fxa-menu-sync-description"
  );

  await openMainPanel();

  checkFxaToolbarButtonPanel({
    headerTitle: expectedLabel,
    headerDescription: expectedDescriptionLabel,
    enabledItems: [
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: [],
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

// Ensure we can see the regular signed in flow + the extra PXI CTAs when
// the experiment is enabled
add_task(async function test_experiment_ui_state_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  // The experiment enables this bool, found in FeatureManifest.yaml
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    true
  );

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  ok(
    BrowserTestUtils.isVisible(
      document.getElementById("fxa-menu-header-title")
    ),
    "expected toolbar to be visible after opening"
  );
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-connect-device-button",
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-relay-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: ["PanelUI-fxa-menu-setup-sync-container"],
    visibleItems: [],
  });
  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

add_task(async function test_new_sync_setup_ui() {
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: false,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
  };

  gSync.updateAllUI(state);

  await openFxaPanel();

  checkMenuBarItem("sync-enable");
  checkPanelHeader();

  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: "Foo Bar",
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-menu-connect-device-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
    ],
    visibleItems: [
      "PanelUI-fxa-menu-setup-sync-container",
      "PanelUI-fxa-menu-connect-device-button",
    ],
  });

  await closeFxaPanel();

  // We need to reset the panel back to hidden since in the code we flip between the old and new sync setup ids
  // so subsequent tests will fail if checking this new container
  let newSyncSetup = document.getElementById(
    "PanelUI-fxa-menu-setup-sync-container"
  );
  newSyncSetup.setAttribute("hidden", true);
});

// Ensure we can see the new "My services" section if the user has enabled relay on their account
add_task(async function test_ui_my_services_signedin() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");

  const relativeDateAnchor = new Date();
  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };

  const origRelativeTimeFormat = gSync.relativeTimeFormat;
  gSync.relativeTimeFormat = {
    formatBestUnit(date) {
      return origRelativeTimeFormat.formatBestUnit(date, {
        now: relativeDateAnchor,
      });
    },
  };

  gSync.updateAllUI(state);

  // pretend that the user has relay enabled
  gSync._attachedClients = [
    {
      id: FX_RELAY_OAUTH_CLIENT_ID,
    },
  ];

  await openFxaPanel();

  checkMenuBarItem("sync-syncnowitem");
  checkPanelHeader();
  ok(
    BrowserTestUtils.isVisible(
      document.getElementById("fxa-menu-header-title")
    ),
    "expected toolbar to be visible after opening"
  );
  checkFxaToolbarButtonPanel({
    headerTitle: "Manage account",
    headerDescription: state.displayName,
    enabledItems: [
      "PanelUI-fxa-menu-sendtab-button",
      "PanelUI-fxa-menu-connect-device-button",
      "PanelUI-fxa-menu-syncnow-button",
      "PanelUI-fxa-menu-sync-prefs-button",
      "PanelUI-fxa-menu-account-signout-button",
      "PanelUI-fxa-cta-menu",
      "PanelUI-fxa-menu-monitor-button",
      "PanelUI-fxa-menu-vpn-button",
    ],
    disabledItems: [],
    hiddenItems: [
      "PanelUI-fxa-menu-setup-sync-container",
      "PanelUI-fxa-menu-relay-button", // the relay button in the "other protections" side should be hidden
    ],
    visibleItems: [],
  });
  checkFxAAvatar("signedin");
  gSync.relativeTimeFormat = origRelativeTimeFormat;
  await closeFxaPanel();

  await openMainPanel();

  checkPanelUIStatusBar({
    description: "Foo Bar",
    titleHidden: true,
    hideFxAText: true,
  });

  // Revert the pref at the end of the test
  Services.prefs.setBoolPref(
    "identity.fxaccounts.toolbar.pxiToolbarEnabled",
    false
  );
  await closeTabAndMainPanel();
});

add_task(async function test_experiment_signin_button_signed_out() {
  // Enroll in Nimbus experiment
  await ExperimentAPI.ready();
  let cleanupNimbus = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: NimbusFeatures.expandSignInButton.featureId,
    value: {
      ctaCopyVariant: "fxa-avatar-sign-in",
    },
  });

  // Set UI state to STATUS_NOT_CONFIGURED (signed out)
  let state = { status: UIState.STATUS_NOT_CONFIGURED };
  gSync.updateAllUI(state);

  const fxaAvatarLabel = document.getElementById("fxa-avatar-label");
  ok(fxaAvatarLabel, "Avatar label element should exist");
  is(
    fxaAvatarLabel.hidden,
    false,
    "Avatar label should be visible when Nimbus experiment is enabled"
  );

  const expectedLabel =
    gSync.fluentStrings.formatValueSync("fxa-avatar-sign-in");

  is(
    fxaAvatarLabel.getAttribute("value"),
    expectedLabel,
    `Avatar label should have the expected localized value: ${expectedLabel}`
  );

  // Clean up experiment
  await cleanupNimbus();

  // Reset UI state after experiment cleanup
  gSync.updateAllUI(state);

  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should be hidden after Nimbus experiment is cleaned up"
  );
});

add_task(async function test_experiment_signin_button_signed_in() {
  // Enroll in Nimbus experiment
  await ExperimentAPI.ready();
  let cleanupNimbus = await NimbusTestUtils.enrollWithFeatureConfig({
    featureId: NimbusFeatures.expandSignInButton.featureId,
    value: {
      ctaCopyVariant: "fxa-avatar-sign-in",
    },
  });

  let state = {
    status: UIState.STATUS_SIGNED_IN,
    syncEnabled: true,
    email: "foo@bar.com",
    displayName: "Foo Bar",
    avatarURL: "https://foo.bar",
    lastSync: new Date(),
    syncing: false,
  };
  gSync.updateAllUI(state);

  const fxaAvatarLabel = document.getElementById("fxa-avatar-label");
  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should never be visible when signed in"
  );

  // Clean up experiment
  await cleanupNimbus();

  // Reset UI state after experiment cleanup
  gSync.updateAllUI(state);

  is(
    fxaAvatarLabel.hidden,
    true,
    "Avatar label should still be hidden when signed in without experiment"
  );
});

function checkPanelUIStatusBar({
  description,
  title,
  titleHidden,
  hideFxAText,
}) {
  checkAppMenuFxAText(hideFxAText);
  let appMenuHeaderTitle = PanelMultiView.getViewNode(
    document,
    "appMenu-header-title"
  );
  let appMenuHeaderDescription = PanelMultiView.getViewNode(
    document,
    "appMenu-header-description"
  );
  is(
    appMenuHeaderDescription.value,
    description,
    "app menu description has correct value"
  );
  is(appMenuHeaderTitle.hidden, titleHidden, "title has correct hidden status");
  if (!titleHidden) {
    is(appMenuHeaderTitle.value, title, "title has correct value");
  }
}

function checkMenuBarItem(expectedShownItemId) {
  checkItemsVisibilities(
    [
      "sync-setup",
      "sync-enable",
      "sync-syncnowitem",
      "sync-reauthitem",
      "sync-unverifieditem",
    ],
    expectedShownItemId
  );
}

function checkPanelHeader() {
  let fxaPanelView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  is(
    fxaPanelView.getAttribute("title"),
    gSync.fluentStrings.formatValueSync("appmenu-account-header"),
    "Panel title is correct"
  );
}

function checkSyncNowButtons(syncing, tooltip = null) {
  const syncButtons = document.querySelectorAll(".syncNowBtn");

  for (const syncButton of syncButtons) {
    is(
      syncButton.getAttribute("syncstatus"),
      syncing ? "active" : null,
      "button active has the right value"
    );
    if (tooltip) {
      is(
        syncButton.getAttribute("tooltiptext"),
        tooltip,
        "button tooltiptext is set to the right value"
      );
    }
  }

  const syncLabels = document.querySelectorAll(".syncnow-label");

  for (const syncLabel of syncLabels) {
    if (syncing) {
      is(
        syncLabel.getAttribute("data-l10n-id"),
        syncLabel.getAttribute("syncing-data-l10n-id"),
        "label is set to the right value"
      );
    } else {
      is(
        syncLabel.getAttribute("data-l10n-id"),
        syncLabel.getAttribute("sync-now-data-l10n-id"),
        "label is set to the right value"
      );
    }
  }
}

async function checkFxaToolbarButtonPanel({
  headerTitle,
  headerDescription,
  enabledItems,
  disabledItems,
  hiddenItems,
  visibleItems,
}) {
  is(
    document.getElementById("fxa-menu-header-title").value,
    headerTitle,
    "has correct title"
  );
  is(
    document.getElementById("fxa-menu-header-description").value,
    headerDescription,
    "has correct description"
  );

  for (const id of enabledItems) {
    const el = document.getElementById(id);
    is(el.hasAttribute("disabled"), false, id + " is enabled");
  }

  for (const id of disabledItems) {
    const el = document.getElementById(id);
    is(el.getAttribute("disabled"), "true", id + " is disabled");
  }

  for (const id of hiddenItems) {
    const el = document.getElementById(id);
    ok(el.hasAttribute("hidden"), id + " is hidden");
  }

  for (const id of visibleItems) {
    const el = document.getElementById(id);
    ok(isElementVisible(el), `${id} is visible`);
  }
}

function isElementVisible(el) {
  if (!el) {
    return false;
  }
  let style = window.getComputedStyle(el);
  // The “hidden” property on the element itself
  // might not exist or might be false, so we also
  // check that the computed style is not hiding it
  // (display: none or visibility: hidden).
  return (
    !el.hidden && style.display !== "none" && style.visibility !== "hidden"
  );
}

async function checkProfilesButtons(
  previousElementSibling,
  separatorVisible = false
) {
  const profilesButton = document.getElementById(
    "PanelUI-fxa-menu-profiles-button"
  );
  const emptyProfilesButton = document.getElementById(
    "PanelUI-fxa-menu-empty-profiles-button"
  );
  const profilesSeparator = document.getElementById(
    "PanelUI-fxa-menu-profiles-separator"
  );

  ok(
    (profilesButton.hidden || emptyProfilesButton.hidden) &&
      !(profilesButton.hidden && emptyProfilesButton.hidden),
    "Only one of the profiles button is visible"
  );

  is(
    !profilesSeparator.hidden,
    separatorVisible,
    "The profile separator is visible"
  );

  is(
    previousElementSibling,
    emptyProfilesButton.previousElementSibling,
    "The profiles button is displayed after " +
      emptyProfilesButton.previousElementSibling.id
  );
}

async function checkFxABadged() {
  const button = document.getElementById("fxa-toolbar-menu-button");
  await BrowserTestUtils.waitForCondition(() => {
    return button.querySelector("label.feature-callout");
  });
  const badge = button.querySelector("label.feature-callout");
  ok(badge, "expected feature-callout style badge");
  ok(BrowserTestUtils.isVisible(badge), "expected the badge to be visible");
}

// fxaStatus is one of 'not_configured', 'unverified', 'login-failed', or 'signedin'.
function checkFxAAvatar(fxaStatus) {
  // Unhide the panel so computed styles can be read
  document.querySelector("#appMenu-popup").hidden = false;

  const avatarContainers = [document.getElementById("fxa-avatar-image")];
  for (const avatar of avatarContainers) {
    const avatarURL = getComputedStyle(avatar).listStyleImage;
    const expected = {
      not_configured: 'url("chrome://browser/skin/fxa/avatar-empty.svg")',
      unverified: 'url("chrome://browser/skin/fxa/avatar.svg")',
      signedin: 'url("chrome://browser/skin/fxa/avatar.svg")',
      "login-failed": 'url("chrome://browser/skin/fxa/avatar.svg")',
    };
    Assert.equal(
      avatarURL,
      expected[fxaStatus],
      `expected avatar URL to be ${expected[fxaStatus]}, got ${avatarURL}`
    );
  }
}

function checkAppMenuFxAText(hideStatus) {
  let fxaText = document.getElementById("appMenu-fxa-text");
  let isHidden = fxaText.hidden || fxaText.style.visibility == "collapse";
  Assert.equal(isHidden, hideStatus, "FxA text has correct hidden state");
}

// Only one item visible at a time.
function checkItemsVisibilities(itemsIds, expectedShownItemId) {
  for (let id of itemsIds) {
    if (id == expectedShownItemId) {
      ok(
        !document.getElementById(id).hidden,
        "menuitem " + id + " should be visible"
      );
    } else {
      ok(
        document.getElementById(id).hidden,
        "menuitem " + id + " should be hidden"
      );
    }
  }
}

function promiseObserver(topic) {
  return new Promise(resolve => {
    let obs = (aSubject, aTopic) => {
      Services.obs.removeObserver(obs, aTopic);
      resolve(aSubject);
    };
    Services.obs.addObserver(obs, topic);
  });
}

async function openTabAndFxaPanel() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "https://example.com/");
  await openFxaPanel();
}

async function openFxaPanel() {
  let fxaButton = document.getElementById("fxa-toolbar-menu-button");
  fxaButton.click();

  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  await BrowserTestUtils.waitForEvent(fxaView, "ViewShown");
}

async function closeFxaPanel() {
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  fxaView.closest("panel").hidePopup();
  await hidden;
}

async function openMainPanel() {
  let menuButton = document.getElementById("PanelUI-menu-button");
  menuButton.click();
  await BrowserTestUtils.waitForEvent(window.PanelUI.mainView, "ViewShown");
}

async function closeTabAndMainPanel() {
  await gCUITestUtils.hideMainMenu();

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
}
