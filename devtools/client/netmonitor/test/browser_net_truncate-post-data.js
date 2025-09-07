/* Any copyright is dedicated to the Public Domain.
  http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Get the default limit
const defaultRequestBodyLimit = Services.prefs.getIntPref(
  "devtools.netmonitor.requestBodyLimit"
);

/**
 * Bug 1986196 -
 * Verifies that requests with large post data are not truncated if
 * devtools.netmonitor.requestBodyLimit is 0.
 */
add_task(async function () {
  await pushPref("devtools.netmonitor.requestBodyLimit", 0);

  await checkPostDataRequest(false);
});

/**
 * Bug 1542172 -
 * Verifies that requests with large post data which are over
 * the limit are truncated and an error is displayed.
 */
add_task(async function () {
  await pushPref("devtools.netmonitor.requestBodyLimit", 1000);

  await checkPostDataRequest(true);
});

/**
 * Bug 1542172 -
 * Verifies that requests with large post data which are within the limit
 * are not truncated and no error is displayed.
 */
add_task(async function () {
  // Set a limit over the size of the post data which is 2 * defaultRequestBodyLimit
  await pushPref(
    "devtools.netmonitor.requestBodyLimit",
    defaultRequestBodyLimit * 3
  );
  await checkPostDataRequest(false);
});

async function checkPostDataRequest(expectErrorDisplay) {
  const { monitor, tab } = await initNetMonitor(POST_JSON_URL, {
    requestCount: 1,
  });

  info("Starting test... ");

  const {
    L10N,
  } = require("resource://devtools/client/netmonitor/src/utils/l10n.js");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  store.dispatch(Actions.batchEnable(false));

  requestLongerTimeout(2);

  info("Perform requests");
  await performRequestsAndWait(monitor, tab);

  await waitUntil(() => document.querySelector(".request-list-item"));
  const item = document.querySelectorAll(".request-list-item")[0];
  await waitUntil(() => item.querySelector(".requests-list-type").title);

  // Make sure the header and editor are loaded
  const waitHeader = waitForDOM(document, "#request-panel .data-header");
  const waitSourceEditor = waitForDOM(
    document,
    "#request-panel .CodeMirror.cm-s-mozilla"
  );

  store.dispatch(Actions.toggleNetworkDetails());
  clickOnSidebarTab(document, "request");

  await Promise.all([waitHeader, waitSourceEditor]);

  const tabpanel = document.querySelector("#request-panel");
  is(
    !!tabpanel.querySelector(".request-error-header"),
    expectErrorDisplay,
    "The request error header doesn't have the intended visibility."
  );
  if (expectErrorDisplay) {
    is(
      tabpanel.querySelector(".request-error-header").textContent,
      "Request has been truncated",
      "The error message shown is incorrect"
    );
  }
  const jsonView = tabpanel.querySelector(".data-label") || {};
  is(
    jsonView.textContent === L10N.getStr("jsonScopeName"),
    false,
    "The params json view doesn't have the intended visibility."
  );
  is(
    tabpanel.querySelector("PRE") === null,
    false,
    "The Request Payload has the intended visibility."
  );

  await pushPref(
    "devtools.netmonitor.requestBodyLimit",
    defaultRequestBodyLimit
  );
  return teardown(monitor);
}

async function performRequestsAndWait(monitor, tab) {
  const wait = waitForNetworkEvents(monitor, 1);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
    content.wrappedJSObject.performLargePostDataRequest();
  });
  await wait;
}
