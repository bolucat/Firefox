/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { InfoBar } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/InfoBar.sys.mjs"
);
const { CFRMessageProvider } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/CFRMessageProvider.sys.mjs"
);
const { ASRouter } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouter.sys.mjs"
);
const { SpecialMessageActions } = ChromeUtils.importESModule(
  "resource://messaging-system/lib/SpecialMessageActions.sys.mjs"
);

// Helper to record impressions in ASRouter state when dispatching an IMPRESSION
// action
function recordImpression(action) {
  if (action.type === "IMPRESSION") {
    let imps = ASRouter.state.messageImpressions || {};
    const { id } = action.data;
    ASRouter.state.messageImpressions = {
      ...imps,
      [id]: [...(imps[id] || []), {}],
    };
  }
}

// Clean up any prefs written via SET_PREF actions
registerCleanupFunction(() => {
  const prefsToClear = [
    "messaging-system-action.embedded-link-sma",
    "messaging-system-action.impressionActionTest",
    "messaging-system-action.onceTest",
    "messaging-system-action.multi.once",
    "messaging-system-action.multi.every",
  ];
  for (let name of prefsToClear) {
    try {
      Services.prefs.clearUserPref(name);
    } catch (e) {
      info(`${name} pref wasn't set.`);
    }
  }
});

add_task(async function show_and_send_telemetry() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    {
      ...message,
      content: {
        priority: window.gNotificationBox.PRIORITY_WARNING_HIGH,
        ...message.content,
      },
    },
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // This is the call to increment impressions for frequency capping
  Assert.equal(dispatchStub.firstCall.args[0].type, "IMPRESSION");
  Assert.equal(dispatchStub.firstCall.args[0].data.id, message.id);
  // This is the telemetry ping
  Assert.equal(dispatchStub.secondCall.args[0].data.event, "IMPRESSION");
  Assert.equal(dispatchStub.secondCall.args[0].data.message_id, message.id);
  Assert.equal(
    infobar.notification.priority,
    window.gNotificationBox.PRIORITY_WARNING_HIGH,
    "Has the priority level set in the message definition"
  );

  let primaryBtn = infobar.notification.buttonContainer.querySelector(
    ".notification-button.primary"
  );

  Assert.ok(primaryBtn, "Has a primary button");
  primaryBtn.click();

  Assert.equal(dispatchStub.callCount, 4, "Called again with CLICK + removed");
  Assert.equal(dispatchStub.thirdCall.args[0].type, "USER_ACTION");
  Assert.equal(
    dispatchStub.lastCall.args[0].data.event,
    "CLICK_PRIMARY_BUTTON"
  );

  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for notification to be dismissed by primary btn click."
  );
});

add_task(async function dismiss_telemetry() {
  let message = {
    ...(await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    ),
  };
  message.content.type = "tab";

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  // Remove any IMPRESSION pings
  dispatchStub.reset();

  infobar.notification.closeButton.click();

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `removed` event"
  );

  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );

  // Remove DISMISSED ping
  dispatchStub.reset();

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  await BrowserTestUtils.waitForCondition(
    () => dispatchStub.callCount > 0,
    "Wait for impression ping"
  );

  // Remove IMPRESSION ping
  dispatchStub.reset();
  BrowserTestUtils.removeTab(tab);

  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Set to null by `disconnect` event"
  );

  // Called by closing the tab and triggering "disconnect"
  Assert.equal(dispatchStub.callCount, 1, "Only called once");
  Assert.equal(
    dispatchStub.firstCall.args[0].data.event,
    "DISMISSED",
    "Called with dismissed"
  );
});

add_task(async function prevent_multiple_messages() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );

  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");

  // Try to stack 2 notifications
  await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(dispatchStub.callCount, 2, "Impression count did not increase");

  // Dismiss the first notification
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");

  // Reset impressions count
  dispatchStub.reset();
  // Try show the message again
  infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );
  Assert.ok(InfoBar._activeInfobar, "activeInfobar is set");
  Assert.equal(dispatchStub.callCount, 2, "Called twice with IMPRESSION");
  // Dismiss the notification again
  infobar.notification.closeButton.click();
  Assert.equal(InfoBar._activeInfobar, null, "Cleared the active notification");
});

add_task(async function default_dismissable_button_shows() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message, "Found the message");

  // Use the base message which has no dismissable property by default.
  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.ok(
    infobar.notification.closeButton,
    "Default message should display a close button"
  );

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => infobar.notification === null,
    "Wait for default message notification to be dismissed."
  );
});

add_task(
  async function non_dismissable_notification_does_not_show_close_button() {
    let baseMessage = (await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    );
    Assert.ok(baseMessage, "Found the base message");

    let message = {
      ...baseMessage,
      content: {
        ...baseMessage.content,
        dismissable: false,
      },
    };

    // Add a footer button we can close the infobar with
    message.content.buttons.push({
      label: "Cancel",
      action: {
        type: "CANCEL",
      },
    });

    let dispatchStub = sinon.stub();
    let infobar = await InfoBar.showInfoBarMessage(
      BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
      message,
      dispatchStub
    );

    Assert.ok(
      !infobar.notification.closeButton,
      "Non-dismissable message should not display a close button"
    );

    let cancelButton = infobar.notification.querySelector(
      ".footer-button:not(.primary)"
    );

    Assert.ok(cancelButton, "Non-primary footer button exists");

    cancelButton.click();
    await BrowserTestUtils.waitForCondition(
      () => infobar.notification === null,
      "Wait for default message notification to close."
    );
  }
);

function getMeaningfulNodes(infobar) {
  return [...infobar.notification.messageText.childNodes].filter(
    n =>
      n.nodeType === Node.ELEMENT_NODE ||
      (n.nodeType === Node.TEXT_NODE && n.textContent.trim())
  );
}

async function showInfobar(text, box, browser) {
  let msg = {
    id: "Test Infobar",
    content: {
      text,
      type: "global",
      priority: box.PRIORITY_INFO_LOW,
      buttons: [{ label: "Close", action: { type: "CANCEL" } }],
    },
  };
  let stub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(browser, msg, stub);
  return { infobar, stub };
}

add_task(async function test_formatMessageConfig_single_string() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  let { infobar } = await showInfobar("Just a plain string", box, browser);
  const nodes = getMeaningfulNodes(infobar);

  Assert.equal(nodes.length, 1, "One meaningful node for single string");
  Assert.equal(nodes[0].nodeType, Node.TEXT_NODE, "That node is a text node");
  Assert.equal(nodes[0].textContent.trim(), "Just a plain string");

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_formatMessageConfig_array() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  let parts = [
    "A",
    { raw: "B" },
    { string_id: "launch-on-login-infobar-message" },
    { href: "https://x.test/", raw: "LINK" },
    "Z",
  ];
  let { infobar } = await showInfobar(parts, box, browser);
  const nodes = getMeaningfulNodes(infobar);

  Assert.equal(nodes.length, parts.length, "One node per array part");
  Assert.equal(nodes[0].textContent, "A", "Plain text");
  Assert.equal(nodes[1].textContent, "B", "Raw text");
  Assert.equal(nodes[2].localName, "remote-text", "L10n element");
  Assert.equal(
    nodes[2].getAttribute("fluent-remote-id"),
    "launch-on-login-infobar-message",
    "Fluent ID"
  );
  const [, , , a] = nodes;
  Assert.equal(a.localName, "a", "It's a link");
  Assert.equal(a.getAttribute("href"), "https://x.test/", "hred preserved");
  Assert.equal(a.textContent, "LINK", "Link text");
  Assert.equal(nodes[4].textContent, "Z", "Trailing text");

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_specialMessageAction_onLinkClick() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  let handleStub = sinon.stub(SpecialMessageActions, "handleAction");

  const parts = [
    "Click ",
    { raw: "here", href: "https://example.com/foo", where: "tab" },
    " to continue",
  ];
  let { infobar } = await showInfobar(parts, box, browser);

  let link = infobar.notification.messageText.querySelector("a[href]");
  Assert.ok(link, "Found the link");
  EventUtils.synthesizeMouseAtCenter(link, {}, browser.ownerGlobal);

  Assert.equal(handleStub.callCount, 1, "handleAction was invoked once");
  let [actionArg, browserArg] = handleStub.firstCall.args;
  Assert.deepEqual(
    actionArg,
    {
      type: "OPEN_URL",
      data: { args: "https://example.com/foo", where: "tab" },
    },
    "Passed correct action to handleAction"
  );
  Assert.equal(
    browserArg,
    browser,
    "Passed the selectedBrowser to handleAction"
  );

  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);

  handleStub.restore();
});

add_task(async function test_showInfoBarMessage_skipsPrivateWindow() {
  const { PrivateBrowsingUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
  );
  sinon.stub(PrivateBrowsingUtils, "isWindowPrivate").returns(true);

  let browser = BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser;
  let dispatch = sinon.stub();

  let result = await InfoBar.showInfoBarMessage(
    browser,
    {
      id: "Private Win Test",
      content: { type: "global", buttons: [], text: "t", dismissable: true },
    },
    dispatch
  );

  Assert.equal(result, null);
  Assert.equal(dispatch.callCount, 0);

  // Cleanup
  sinon.restore();
});

add_task(async function test_non_dismissable_button_action() {
  let baseMessage = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(baseMessage, "Found the base message");

  let message = {
    ...baseMessage,
    content: {
      ...baseMessage.content,
      type: "global",
      dismissable: true,
      buttons: [
        {
          label: "Secondary button",
          action: {
            type: "OPEN_URL",
            data: { args: "https://example.com/foo", where: "tab" },
            dismiss: false,
          },
        },
      ],
    },
  };

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  let button = infobar.notification.querySelector(".notification-button");
  Assert.ok(button, "Found the button");
  Assert.ok(
    dispatchStub.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match({
          event: "IMPRESSION",
          message_id: message.id,
        }),
      })
    ),
    "Dispatched telemetry IMPRESSION ping"
  );

  button.click();

  Assert.ok(
    infobar.notification,
    "Infobar was not dismissed after clicking the button"
  );
  Assert.ok(
    dispatchStub.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match.has("event", "CLICK_SECONDARY_BUTTON"),
      })
    ),
    "Dispatched telemetry CLICK_SECONDARY_BUTTON"
  );

  // Clean up
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

// Default experience
add_task(async function test_dismissable_button_action() {
  let baseMessage = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(baseMessage, "Found the base message");

  let message = {
    ...baseMessage,
    content: {
      ...baseMessage.content,
      type: "global",
      dismissable: true,
      buttons: [
        {
          label: "Secondary button",
          action: {
            type: "OPEN_URL",
            data: { args: "https://example.com/bar", where: "tab" },
            // dismiss is omitted here to test default case
          },
        },
      ],
    },
  };

  let dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  let button = infobar.notification.querySelector(".notification-button");
  Assert.ok(button, "Found the button");
  Assert.ok(
    dispatchStub.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match({
          event: "IMPRESSION",
          message_id: message.id,
        }),
      })
    ),
    "Dispatched telemetry IMPRESSION ping"
  );

  button.click();
  Assert.ok(
    dispatchStub.calledWith(
      sinon.match({
        type: "INFOBAR_TELEMETRY",
        data: sinon.match.has("event", "CLICK_SECONDARY_BUTTON"),
      })
    ),
    "Dispatched telemetry CLICK_SECONDARY_BUTTON"
  );

  // Wait for the notification to be removed
  await BrowserTestUtils.waitForCondition(() => !infobar.notification);

  Assert.ok(!infobar.notification, "Infobar was dismissed after button click");
});

add_task(async function clear_activeInfobar_on_window_close() {
  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message.id, "Found the message");

  let dispatchStub = sinon.stub();
  let testWin = await BrowserTestUtils.openNewBrowserWindow();
  let testBrowser = testWin.gBrowser.selectedBrowser;

  await InfoBar.showInfoBarMessage(testBrowser, message, dispatchStub);
  Assert.ok(
    InfoBar._activeInfobar,
    "InfoBar._activeInfobar should be set after showing infobar"
  );

  await BrowserTestUtils.closeWindow(testWin);
  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "InfoBar._activeInfobar should be cleared when the window unloads"
  );
  testWin.close();
  sinon.restore();
});

add_task(async function test_buildMessageFragment_withInlineAnchors() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const doc = browser.ownerGlobal.document;

  // Stub out the Fluent call to return a string with an inline anchor
  const { RemoteL10n } = ChromeUtils.importESModule(
    "resource:///modules/asrouter/RemoteL10n.sys.mjs"
  );
  sinon
    .stub(RemoteL10n, "formatLocalizableText")
    .resolves('<a data-l10n-name="foo">Click Here</a>');
  let handleStub = sinon.stub(SpecialMessageActions, "handleAction");

  const linkUrl = "https://example.com/";
  const message = {
    id: "TEST_INLINE_ANCHOR",
    content: {
      type: "global",
      priority: win.gNotificationBox.PRIORITY_INFO_LOW,
      text: { string_id: "test-id", args: { where: "tabshifted" } },
      linkUrls: { foo: linkUrl },
      buttons: [{ label: "Close", action: { type: "CANCEL" } }],
    },
  };
  const dispatchStub = sinon.stub();
  const infobar = await InfoBar.showInfoBarMessage(
    browser,
    message,
    dispatchStub
  );

  const container = doc.getElementById("infobar-link-templates");
  Assert.ok(container, "Link template container in doc");
  const template = container.querySelector('a[data-l10n-name="foo"]');
  Assert.equal(
    template.href,
    linkUrl,
    "Template anchor carries the correct href"
  );

  const rendered = infobar.notification.messageText.querySelector(
    'a[data-l10n-name="foo"]'
  );
  Assert.ok(rendered, "Rendered anchor is present in infobar");
  Assert.equal(rendered.href, linkUrl, "Rendered anchor has correct href");
  Assert.equal(
    rendered.textContent,
    "Click Here",
    "Rendered anchor text matches"
  );

  rendered.click();
  Assert.equal(handleStub.callCount, 1, "handleAction called once");
  let [actionArg] = handleStub.firstCall.args;
  Assert.equal(
    actionArg.data.where,
    message.content.text.args.where,
    "handleAction receives correct where value"
  );

  // Cleanup
  container.remove();
  RemoteL10n.formatLocalizableText.restore();
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
  sinon.restore();
});

add_task(async function test_buildMessageFragment_withoutInlineAnchors() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;
  const doc = browser.ownerGlobal.document;

  // Stub Fluent to return plain text (no <a data-l10n-name>)
  const { RemoteL10n } = ChromeUtils.importESModule(
    "resource:///modules/asrouter/RemoteL10n.sys.mjs"
  );
  sinon.stub(RemoteL10n, "formatLocalizableText").resolves("Just plain text");

  let { infobar } = await showInfobar(
    { string_id: "existing-user-tou-message" },
    box,
    browser
  );

  Assert.ok(
    !doc.getElementById("infobar-link-templates"),
    "No link-template container created for a string without anchors"
  );

  const nodes = getMeaningfulNodes(infobar);
  Assert.equal(nodes.length, 1, "One meaningful node for string_id fallback");
  const [remoteTextEl] = nodes;
  Assert.equal(
    remoteTextEl.localName,
    "remote-text",
    "Renders via <remote-text>"
  );
  Assert.equal(
    remoteTextEl.getAttribute("fluent-remote-id"),
    "existing-user-tou-message",
    "Has the correct fluent-remote-id"
  );

  // Cleanup
  RemoteL10n.formatLocalizableText.restore();
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
  sinon.restore();
});

add_task(
  async function test_buildMessageFragment_withInlineAnchor_openUrlAndSMA() {
    const win = BrowserWindowTracker.getTopWindow();
    const browser = win.gBrowser.selectedBrowser;

    const { RemoteL10n } = ChromeUtils.importESModule(
      "resource:///modules/asrouter/RemoteL10n.sys.mjs"
    );
    sinon
      .stub(RemoteL10n, "formatLocalizableText")
      .resolves('<a data-l10n-name="foo">Click Me</a>');

    const handleStub = sinon.stub(SpecialMessageActions, "handleAction");

    const testUrl = "https://example.com/";
    const customAction = {
      type: "SET_PREF",
      data: { pref: { name: "embedded-link-sma", value: true } },
    };
    const message = {
      id: "TEST_INLINE_OPEN_URL_AND_ACTION",
      content: {
        type: "global",
        priority: win.gNotificationBox.PRIORITY_INFO_LOW,
        text: { string_id: "test" },
        linkUrls: { foo: testUrl },
        linkActions: { foo: customAction },
        buttons: [{ label: "Close", action: { type: "CANCEL" } }],
      },
    };

    const dispatchStub = sinon.stub();
    const infobar = await InfoBar.showInfoBarMessage(
      browser,
      message,
      dispatchStub
    );

    const rendered = infobar.notification.messageText.querySelector(
      'a[data-l10n-name="foo"]'
    );
    Assert.ok(rendered, "Rendered inline anchor present");
    Assert.equal(rendered.href, testUrl, "Rendered href is correct");

    EventUtils.synthesizeMouseAtCenter(rendered, {}, browser.ownerGlobal);

    Assert.equal(handleStub.callCount, 2, "handleAction called twice");

    const [openUrlAction, openUrlBrowser] = handleStub.firstCall.args;
    Assert.deepEqual(
      openUrlAction,
      { type: "OPEN_URL", data: { args: testUrl, where: "tab" } },
      "First call opens the URL"
    );
    Assert.equal(openUrlBrowser, browser, "OPEN_URL got the correct browser");

    const [customArg, customBrowser] = handleStub.secondCall.args;
    Assert.deepEqual(
      customArg,
      customAction,
      "Second call invokes the custom action"
    );
    Assert.equal(
      customBrowser,
      browser,
      "Custom action got the correct browser"
    );

    // Cleanup
    RemoteL10n.formatLocalizableText.restore();
    handleStub.restore();
    infobar.notification.closeButton.click();
    await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
  }
);

add_task(async function test_configurable_background_color() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;

  // Pick an arbitrary color string that supports and light and dark mode
  const customBg = "light-dark(rgb(255, 255, 255), rgb(0, 0, 0))";

  // Build a minimal “infobar” message with the style override
  const msg = {
    id: "TEST_CONFIG_BG",
    content: {
      type: "global",
      priority: box.PRIORITY_INFO_LOW,
      text: "BG Test",
      buttons: [{ label: "Close", action: { type: "CANCEL" } }],
      style: {
        "background-color": customBg,
      },
    },
  };

  let dispatch = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(browser, msg, dispatch);
  Assert.ok(infobar.notification, "Got a notification");

  let node = infobar.notification;
  let bg = browser.ownerGlobal
    .getComputedStyle(node)
    .getPropertyValue("background-color");

  // Confirm either light or dark mode background was applied
  Assert.ok(
    bg === "rgb(255, 255, 255)" || bg === "rgb(0, 0, 0)",
    `background-color was applied (${bg})`
  );

  // Cleanup
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_configurable_font_size() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const box = win.gNotificationBox;
  const customSize = "24px";

  const msg = {
    id: "TEST_CONFIG_FS",
    content: {
      type: "global",
      priority: box.PRIORITY_INFO_LOW,
      text: "FS Test",
      buttons: [{ label: "Close", action: { type: "CANCEL" } }],
      style: {
        "font-size": customSize,
      },
    },
  };

  let dispatch = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(browser, msg, dispatch);
  Assert.ok(infobar.notification, "Got a notification");

  let node = infobar.notification;
  let fs = browser.ownerGlobal
    .getComputedStyle(node)
    .getPropertyValue("font-size");

  Assert.equal(fs, customSize, `font-size was applied (${fs})`);

  // Cleanup
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(() => !InfoBar._activeInfobar);
});

add_task(async function test_infobar_css_background_fallback_var() {
  const win = BrowserWindowTracker.getTopWindow();
  const browser = win.gBrowser.selectedBrowser;
  const dispatch = sinon.stub();

  // Show an infobar so its shadowRoot + stylesheet are initialized
  let infobar = await InfoBar.showInfoBarMessage(
    browser,
    {
      id: "TEST_BG_FALLBACK",
      content: {
        type: "global",
        style: {
          "background-color": "light-dark(white, black)",
        },
        text: "CSS fallback test",
        buttons: [{ label: "Close", action: { type: "CANCEL" } }],
      },
    },
    dispatch
  );
  Assert.ok(infobar.notification, "Got a notification");

  const link = infobar.notification.renderRoot.querySelector(
    'link[href$="/elements/infobar.css"]'
  );
  Assert.ok(link, "Found the infobar.css link in shadowRoot");
  const { sheet } = link;
  Assert.ok(sheet, "infobar.css stylesheet loaded");

  // Look for @media not (prefers-contrast) > :host(.infobar) rule
  let found = false;
  for (const rule of sheet.cssRules) {
    if (
      CSSMediaRule.isInstance(rule) &&
      rule.conditionText.trim() === "not (prefers-contrast)"
    ) {
      for (const inner of rule.cssRules) {
        if (inner.selectorText === ":host(.infobar)") {
          const bg = inner.style.getPropertyValue("background-color");
          if (
            bg.includes(
              "--info-bar-background-color-configurable, var(--info-bar-background-color)"
            )
          ) {
            found = true;
          }
          break;
        }
      }
      break;
    }
  }

  Assert.ok(
    found,
    "infobar.css uses fallback `var(--info-bar-background-color-configurable, var(--info-bar-background-color))` in not(prefers-contrast)"
  );

  // Cleanup
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for infobar to close"
  );
});

add_task(async function test_impression_action_handling() {
  const handleStub = sinon.stub(SpecialMessageActions, "handleAction");

  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message, "Found base message");

  message.content.impression_action = {
    type: "SET_PREF",
    data: {
      pref: { name: "impressionActionTest", value: true },
    },
  };

  const dispatchStub = sinon.stub();
  let infobar = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(
    handleStub.callCount,
    1,
    "handleAction called once on impression"
  );

  const [actionArg] = handleStub.firstCall.args;
  Assert.equal(actionArg.type, "SET_PREF", "Correct action type dispatched");
  Assert.ok(actionArg.data.onImpression, "onImpression flag set to true");
  Assert.deepEqual(
    actionArg.data.pref,
    { name: "impressionActionTest", value: true },
    "Original pref data preserved"
  );

  // Cleanup
  infobar.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for infobar to close"
  );
  handleStub.restore();
});

add_task(
  async function test_impression_action_once_allowed_only_on_first_impression() {
    const handleStub = sinon.stub(SpecialMessageActions, "handleAction");

    let message = (await CFRMessageProvider.getMessages()).find(
      m => m.id === "INFOBAR_ACTION_86"
    );
    Assert.ok(message, "Found base message");

    message.content.impression_action = {
      type: "SET_PREF",
      data: { pref: { name: "onceTest", value: true } },
      once: true,
    };

    // Ensure ASRouter has no prior impressions recorded for this message
    await ASRouter.resetMessageState();

    let dispatchStub = sinon.stub();
    dispatchStub.callsFake(recordImpression);

    let infobar = await InfoBar.showInfoBarMessage(
      BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
      message,
      dispatchStub
    );

    Assert.equal(
      handleStub.callCount,
      1,
      "handleAction called once on first impression when once is true"
    );

    handleStub.resetHistory();

    infobar.notification.closeButton.click();
    await BrowserTestUtils.waitForCondition(
      () => !InfoBar._activeInfobar,
      "Wait for infobar to close"
    );

    let infobar2 = await InfoBar.showInfoBarMessage(
      BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
      message,
      dispatchStub
    );

    Assert.equal(
      handleStub.callCount,
      0,
      "handleAction suppressed on subsequent impressions when once is true"
    );

    // Cleanup
    infobar2.notification.closeButton.click();
    await BrowserTestUtils.waitForCondition(
      () => !InfoBar._activeInfobar,
      "Wait for infobar to close"
    );
    handleStub.restore();
  }
);

add_task(async function test_impression_action_multi_action_once_and_every() {
  const handleStub = sinon.stub(SpecialMessageActions, "handleAction");

  let message = (await CFRMessageProvider.getMessages()).find(
    m => m.id === "INFOBAR_ACTION_86"
  );
  Assert.ok(message, "Found base message");

  message.content.impression_action = {
    type: "MULTI_ACTION",
    data: {
      actions: [
        {
          type: "SET_PREF",
          data: { pref: { name: "multi.once", value: true } },
          once: true,
        },
        {
          type: "SET_PREF",
          data: { pref: { name: "multi.every", value: true } },
        },
      ],
    },
  };

  await ASRouter.resetMessageState();

  let dispatchStub = sinon.stub();
  dispatchStub.callsFake(recordImpression);

  let infobar1 = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(
    handleStub.callCount,
    2,
    "handleAction called twice on first impression (once and every)"
  );

  infobar1.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for first infobar to close"
  );

  let infobar2 = await InfoBar.showInfoBarMessage(
    BrowserWindowTracker.getTopWindow().gBrowser.selectedBrowser,
    message,
    dispatchStub
  );

  Assert.equal(
    handleStub.callCount,
    3,
    "only the non-once action fires on second impression"
  );

  infobar2.notification.closeButton.click();
  await BrowserTestUtils.waitForCondition(
    () => !InfoBar._activeInfobar,
    "Wait for second infobar to close"
  );

  handleStub.restore();
});
