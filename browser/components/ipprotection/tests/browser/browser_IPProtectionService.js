/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);

const { ERRORS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

// Don't add an experiment so we can test adding and removing it.
DEFAULT_EXPERIMENT = null;

/**
 * Tests getting eligibility from a Nimbus experiment and
 * creating and destroying the widget.
 */
add_task(async function test_IPProtectionService_updateEligibility() {
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });
  Assert.ok(IPProtectionService.isEligible, "Should be in the experiment");
  let buttonOn = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(buttonOn),
    "IP Protection widget should be added to the navbar"
  );
  await cleanupAlpha();

  let cleanupControl = await setupExperiment({
    enabled: true,
    variant: "control",
  });
  Assert.ok(!IPProtectionService.isEligible, "Should not be in the experiment");
  let buttonOff = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    !buttonOff,
    "IP Protection widget should not be added to the navbar"
  );
  await cleanupControl();
});

/**
 * Tests a user who was previously enrolled will be shown the widget.
 */
add_task(async function test_IPProtectionService_updateEnrollment() {
  setupService({
    isSignedIn: true,
    isEnrolled: true,
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  // hasEnrolled / isEnrolled is async so wait for widget.
  await waitForWidgetAdded();

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added to the navbar"
  );

  cleanupService();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests a user in the experiment can enroll with Guardian on sign-in.
 */
add_task(async function test_IPProtectionService_enroll() {
  setupService({
    isEnrolled: false,
    canEnroll: true,
  });

  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  await waitForWidgetAdded();

  setupService({
    isSignedIn: true,
  });

  await IPProtectionService.updateSignInStatus();

  Assert.ok(IPProtectionService.isEnrolled, "User should now be enrolled");

  cleanupService();
  await cleanupAlpha();
});

/**
 * Tests a user who has signed in is enrolled when enrolled in the experiment.
 * This state is only likely when testing the experiment.
 */
add_task(
  async function test_IPProtectionService_enroll_when_enrolled_in_experiment() {
    setupService({
      isEnrolled: false,
      isSignedIn: true,
      canEnroll: true,
    });

    let cleanupAlpha = await setupExperiment({
      enabled: true,
      variant: "alpha",
    });

    await waitForWidgetAdded();

    await IPProtectionService.updateSignInStatus();

    Assert.ok(IPProtectionService.isEnrolled, "User should now be enrolled");

    cleanupService();
    await cleanupAlpha();
  }
);

/**
 *  Tests the entitlement updates when in the experiment.
 */
add_task(
  async function test_IPProtectionService_updateEntitlement_in_experiment() {
    setupService({
      isEnrolled: false,
      isSignedIn: true,
      canEnroll: true,
    });

    let cleanupAlpha = await setupExperiment({
      enabled: true,
      variant: "alpha",
    });

    await waitForWidgetAdded();

    await IPProtectionService.updateSignInStatus();

    Assert.equal(
      IPProtectionService.isEntitled,
      true,
      "Entitlement set the user as entitled"
    );

    cleanupService();
    await cleanupAlpha();
  }
);

/**
 * Tests the entitlement updates when not in the experiment.
 */
add_task(async function test_IPProtectionService_updateEntitlement() {
  setupService({
    isSignedIn: true,
    isEnrolled: true,
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  await waitForWidgetAdded();

  Assert.equal(
    IPProtectionService.isEntitled,
    true,
    "Entitlement set the user as entitled"
  );

  cleanupService();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests a user with a valid proxy pass and can start the proxy.
 */
add_task(async function test_IPProtectionService_proxyPass() {
  setupService({
    isSignedIn: true,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  IPProtectionService.isSignedIn = false;
  await IPProtectionService.updateSignInStatus();

  Assert.ok(IPProtectionService.isEnrolled, "User should be enrolled");
  Assert.equal(IPProtectionService.isEntitled, true, "User should be entitled");

  let content = await openPanel();

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.connectionToggleEl,
    "Status card connection toggle should be present"
  );

  let startedEventPromise = BrowserTestUtils.waitForEvent(
    IPProtectionService,
    "IPProtectionService:Started"
  );
  content.connectionToggleEl.click();

  await startedEventPromise;

  Assert.ok(IPProtectionService.hasProxyPass, "User has a proxyPass");

  await closePanel();
  await cleanupAlpha();
  cleanupService();
});

add_task(async function test_ipprotection_ready() {
  setupService({
    isSignedIn: true,
    isEnrolled: true,
  });

  const sandbox = sinon.createSandbox();
  const receivedTrigger = new Promise(resolve => {
    sandbox.stub(ASRouter, "sendTriggerMessage").callsFake(({ id }) => {
      if (id === "ipProtectionReady") {
        resolve(true);
      }
    });
  });

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  let ipProtectionReadyTrigger = await receivedTrigger;
  Assert.ok(ipProtectionReadyTrigger, "ipProtectionReady trigger sent");

  sandbox.restore();
  cleanupService();
});

/**
 * Tests showing an error and dismissing it on panel close.
 */
add_task(async function test_IPProtectionService_pass_errors() {
  setupService({
    isSignedIn: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  await IPProtectionService.updateSignInStatus();

  let content = await openPanel();

  let messageBarLoadedPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.shadowRoot.querySelector("ipprotection-message-bar")
  );
  // Mock a failure
  IPProtectionService.isEntitled = false;

  content.connectionToggleEl.click();

  Assert.ok(!IPProtectionService.isActive, "Proxy is not active");

  await messageBarLoadedPromise;

  let messageBar = content.shadowRoot.querySelector("ipprotection-message-bar");

  Assert.ok(!content.connectionToggleEl.pressed, "Toggle is off");
  Assert.ok(messageBar, "Message bar should be present");
  Assert.equal(
    content.state.error,
    ERRORS.GENERIC,
    "Should have a generic error"
  );

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    button.classList.contains("ipprotection-error"),
    "Toolbar icon should show the error status"
  );

  await closePanel();

  Assert.equal(content.state.error, "", "Should have no error");

  await cleanupAlpha();
  cleanupService();
});

/**
 * Tests retry after an error.
 */
add_task(async function test_IPProtectionService_retry_errors() {
  setupService({
    isSignedIn: true,
    isEnrolled: false,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  await IPProtectionService.updateSignInStatus();

  let content = await openPanel();

  // Mock a failure
  IPProtectionService.isEnrolled = false;
  IPProtectionService.hasError = true;

  let startedEventPromise = BrowserTestUtils.waitForEvent(
    IPProtectionService,
    "IPProtectionService:Started"
  );
  content.connectionToggleEl.click();

  await startedEventPromise;

  Assert.ok(IPProtectionService.isActive, "Proxy is active");
  Assert.ok(IPProtectionService.isEnrolled, "User is now enrolled");
  Assert.ok(!IPProtectionService.hasError, "There is no longer an error");

  IPProtectionService.stop();

  await closePanel();
  await cleanupAlpha();
  cleanupService();
});

/**
 * Tests the proxy is stopped if user signs out with it active.
 */
add_task(async function test_IPProtectionService_stop_on_signout() {
  setupService({
    isSignedIn: true,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  await IPProtectionService.updateSignInStatus();

  let content = await openPanel();

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.connectionToggleEl,
    "Status card connection toggle should be present"
  );

  let startedEventPromise = BrowserTestUtils.waitForEvent(
    IPProtectionService,
    "IPProtectionService:Started"
  );
  content.connectionToggleEl.click();

  await startedEventPromise;

  Assert.ok(IPProtectionService.isActive, "Proxy is active");

  let vpnOffPromise = BrowserTestUtils.waitForEvent(
    IPProtectionService,
    "IPProtectionService:Stopped"
  );

  setupService({
    isSignedIn: false,
  });
  let signedOut = IPProtectionService.updateSignInStatus();
  await Promise.all([signedOut, vpnOffPromise]);

  Assert.ok(!IPProtectionService.isActive, "Proxy has stopped");

  await closePanel();
  await cleanupAlpha();
  cleanupService();
});

function waitForTabReloaded(tab) {
  return new Promise(resolve => {
    gBrowser.addTabsProgressListener({
      async onLocationChange(aBrowser) {
        if (tab.linkedBrowser == aBrowser) {
          gBrowser.removeTabsProgressListener(this);
          await Promise.resolve();
          resolve();
        }
      },
    });
  });
}

/**
 * Tests a user start or stopping the proxy reloads the current tab.
 */
add_task(async function test_IPProtectionService_reload() {
  setupService({
    isSignedIn: true,
    canEnroll: true,
  });
  let cleanupAlpha = await setupExperiment({ enabled: true, variant: "alpha" });

  IPProtectionService.isSignedIn = false;
  await IPProtectionService.updateSignInStatus();

  Assert.ok(IPProtectionService.isEnrolled, "User should be enrolled");
  Assert.equal(IPProtectionService.isEntitled, true, "User should be entitled");

  let content = await openPanel();

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.connectionToggleEl,
    "Status card connection toggle should be present"
  );

  let tabReloaded = waitForTabReloaded(gBrowser.selectedTab);
  content.connectionToggleEl.click();
  await tabReloaded;

  Assert.ok(IPProtectionService.isActive, "Proxy is active");

  tabReloaded = waitForTabReloaded(gBrowser.selectedTab);
  content.connectionToggleEl.click();
  await tabReloaded;

  Assert.ok(!IPProtectionService.isActive, "Proxy is not active");

  await closePanel();
  await cleanupAlpha();
  cleanupService();
});
