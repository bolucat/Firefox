/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that the request for a domain that is not found shows
 * correctly.
 */

add_task(async function () {
  const URL = "https://not-existed.com/";
  const { monitor } = await initNetMonitor(URL, {
    requestCount: 1,
    waitForLoad: false,
  });
  info("Starting test... ");

  const { document, store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");

  store.dispatch(Actions.batchEnable(false));

  const wait = waitForNetworkEvents(monitor, 1);
  reloadBrowser({ waitForLoad: false });
  await wait;

  const firstItem = document.querySelectorAll(".request-list-item")[0];

  info("Wait for content for the transfered column to be updated");
  const transferredValue = await waitFor(() => {
    const value = firstItem.querySelector(
      ".requests-list-transferred"
    ).innerText;
    return value.includes("NS_ERROR") ? value : false;
  });

  is(
    firstItem.querySelector(".requests-list-url").innerText,
    URL,
    "The url in the displayed request is correct"
  );
  is(
    transferredValue,
    "NS_ERROR_UNKNOWN_HOST",
    "The error in the displayed request is correct"
  );

  await teardown(monitor);
});
