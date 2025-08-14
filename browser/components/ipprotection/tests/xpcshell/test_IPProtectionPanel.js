/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);
const { IPProtectionPanel } = ChromeUtils.importESModule(
  "resource:///modules/ipprotection/IPProtectionPanel.sys.mjs"
);
const { IPProtectionService } = ChromeUtils.importESModule(
  "resource:///modules/ipprotection/IPProtectionService.sys.mjs"
);

class FakeIPProtectionPanelElement {
  constructor() {
    this.state = {
      isSignedIn: false,
      isProtectionEnabled: false,
      protectionEnabledSince: null,
    };
    this.isConnected = false;
  }

  requestUpdate() {
    /* NOOP */
  }

  closest() {
    return {
      state: "open",
    };
  }
}

/**
 * Tests that we can set a state and pass it to a fake element.
 */
add_task(async function test_setState() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;

  ipProtectionPanel.state = {};
  fakeElement.state = {};

  ipProtectionPanel.setState({
    foo: "bar",
  });

  Assert.deepEqual(
    ipProtectionPanel.state,
    { foo: "bar" },
    "The state should be set on the IPProtectionPanel instance"
  );

  Assert.deepEqual(
    fakeElement.state,
    {},
    "The state should not be set on the fake element, as it is not connected"
  );

  fakeElement.isConnected = true;

  ipProtectionPanel.setState({
    isFoo: true,
  });

  Assert.deepEqual(
    ipProtectionPanel.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the IPProtectionPanel instance"
  );

  Assert.deepEqual(
    fakeElement.state,
    { foo: "bar", isFoo: true },
    "The state should be set on the fake element"
  );
});

/**
 * Tests that the whole state will be updated when calling updateState directly.
 */
add_task(async function test_updateState() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;

  ipProtectionPanel.state = {};
  fakeElement.state = {};

  ipProtectionPanel.setState({
    foo: "bar",
  });

  Assert.deepEqual(
    fakeElement.state,
    {},
    "The state should not be set on the fake element, as it is not connected"
  );

  fakeElement.isConnected = true;
  ipProtectionPanel.updateState();

  Assert.deepEqual(
    fakeElement.state,
    { foo: "bar" },
    "The state should be set on the fake element"
  );
});

/**
 * Tests that IPProtectionService signed-in status events updates the state.
 */
add_task(async function test_IPProtectionPanel_signedIn() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_SIGNED_IN,
  });

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;
  fakeElement.isConnected = true;

  let signedInEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:SignedIn"
  );

  IPProtectionService.updateSignInStatus();

  await signedInEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isSignedIn,
    true,
    "isSignedIn should be true in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isSignedIn,
    true,
    "isSignedIn should be true in the fake elements state"
  );

  sandbox.restore();
});

/**
 * Tests that IPProtectionService signed-out status events updates the state.
 */
add_task(async function test_IPProtectionPanel_signedOut() {
  let sandbox = sinon.createSandbox();
  sandbox.stub(UIState, "get").returns({
    status: UIState.STATUS_NOT_CONFIGURED,
  });

  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;
  fakeElement.isConnected = true;

  IPProtectionService.isSignedIn = true;
  ipProtectionPanel.setState({
    isSignedIn: true,
  });
  ipProtectionPanel.updateState();

  let signedOutEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:SignedOut"
  );

  IPProtectionService.updateSignInStatus();

  await signedOutEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isSignedIn,
    false,
    "isSignedIn should be true in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isSignedIn,
    false,
    "isSignedIn should be true in the fake elements state"
  );

  sandbox.restore();
});

/**
 * Tests that start and stopping the IPProtectionService updates the state.
 */
add_task(async function test_IPProtectionPanel_started_stopped() {
  let ipProtectionPanel = new IPProtectionPanel();
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;
  fakeElement.isConnected = true;

  // Set to signed in
  IPProtectionService.isSignedIn = true;
  ipProtectionPanel.setState({
    isSignedIn: true,
  });
  ipProtectionPanel.updateState();

  let startedEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:Started"
  );

  IPProtectionService.start();

  await startedEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isProtectionEnabled,
    true,
    "isProtectionEnabled should be true in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isProtectionEnabled,
    true,
    "isProtectionEnabled should be true in the fake elements state"
  );

  let stoppedEventPromise = waitForEvent(
    IPProtectionService,
    "IPProtectionService:Stopped"
  );

  IPProtectionService.stop();

  await stoppedEventPromise;

  Assert.equal(
    ipProtectionPanel.state.isProtectionEnabled,
    false,
    "isProtectionEnabled should be false in the IPProtectionPanel state"
  );

  Assert.equal(
    fakeElement.state.isProtectionEnabled,
    false,
    "isProtectionEnabled should be false in the fake elements state"
  );
});

/**
 * Tests that the variant argument is set in the state.
 */
add_task(async function test_IPProtectionPanel_variant() {
  let ipProtectionPanel = new IPProtectionPanel(null, "alpha");
  let fakeElement = new FakeIPProtectionPanelElement();
  ipProtectionPanel.panel = fakeElement;

  Assert.equal(
    ipProtectionPanel.state.variant,
    "alpha",
    "variant should be set in the IPProtectionPanel state"
  );

  fakeElement.isConnected = true;
  ipProtectionPanel.updateState();

  Assert.equal(
    fakeElement.state.variant,
    "alpha",
    "variant should be set in the fake elements state"
  );
});
