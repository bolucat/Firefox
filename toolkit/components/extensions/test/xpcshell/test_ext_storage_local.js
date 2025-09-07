/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionStorageIDB: "resource://gre/modules/ExtensionStorageIDB.sys.mjs",
});

AddonTestUtils.init(this);

add_task(async function setup() {
  await ExtensionTestUtils.startAddonManager();
});

add_task(async function test_local_cache_invalidation() {
  function background(checkGet) {
    browser.test.onMessage.addListener(async msg => {
      if (msg === "set-initial") {
        await browser.storage.local.set({
          "test-prop1": "value1",
          "test-prop2": "value2",
        });
        browser.test.sendMessage("set-initial-done");
      } else if (msg === "check") {
        await checkGet("local", "test-prop1", "value1");
        await checkGet("local", "test-prop2", "value2");
        browser.test.sendMessage("check-done");
      }
    });

    browser.test.sendMessage("ready");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["storage"],
    },
    background: `(${background})(${checkGetImpl})`,
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  extension.sendMessage("set-initial");
  await extension.awaitMessage("set-initial-done");

  Services.obs.notifyObservers(null, "extension-invalidate-storage-cache");

  extension.sendMessage("check");
  await extension.awaitMessage("check-done");

  await extension.unload();
});

add_task(function test_storage_local_file_backend() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, false]], () =>
    test_background_page_storage("local")
  );
});

add_task(function test_storage_local_idb_backend() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, true]], () =>
    test_background_page_storage("local")
  );
});

add_task(function test_storage_local_onChanged_event_page() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, true]], () =>
    test_storage_change_event_page("local")
  );
});

add_task(async function test_storage_local_empty_events() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, true]], () =>
    test_storage_empty_events("local")
  );
});

add_task(async function test_storage_local_idb_get_bytes_in_use() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, true]], () =>
    test_get_bytes_in_use("local", true)
  );
});

add_task(async function test_storage_local_file_get_bytes_in_use() {
  return runWithPrefs([[ExtensionStorageIDB.BACKEND_ENABLED_PREF, false]], () =>
    test_get_bytes_in_use("local", false)
  );
});
