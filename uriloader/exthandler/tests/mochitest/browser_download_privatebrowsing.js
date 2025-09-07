/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests that downloads started from a private window by clicking on a link end
 * up in the global list of private downloads (see bug 1367581).
 */

"use strict";

const { Downloads } = ChromeUtils.importESModule(
  "resource://gre/modules/Downloads.sys.mjs"
);
const { DownloadPaths } = ChromeUtils.importESModule(
  "resource://gre/modules/DownloadPaths.sys.mjs"
);
const { FileTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/FileTestUtils.sys.mjs"
);
const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

add_task(async function test_setup() {
  // Save downloads to disk without showing the dialog.
  let cid = MockRegistrar.register("@mozilla.org/helperapplauncherdialog;1", {
    QueryInterface: ChromeUtils.generateQI(["nsIHelperAppLauncherDialog"]),
    show(launcher) {
      launcher.promptForSaveDestination();
    },
    promptForSaveToFileAsync(launcher) {
      // The dialog should create the empty placeholder file.
      let file = FileTestUtils.getTempFile();
      file.create(Ci.nsIFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
      launcher.saveDestinationAvailable(file);
    },
  });
  registerCleanupFunction(() => {
    MockRegistrar.unregister(cid);
  });
});

add_task(async function test_download_privatebrowsing() {
  let privateList = await Downloads.getList(Downloads.PRIVATE);
  let publicList = await Downloads.getList(Downloads.PUBLIC);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  try {
    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      `data:text/html,<a download href="data:text/plain,">download</a>`
    );

    let promiseNextPrivateDownload = new Promise(resolve => {
      privateList.addView({
        onDownloadAdded(download) {
          privateList.removeView(this);
          resolve(download);
        },
      });
    });

    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    // Wait for the download to finish so the file can be safely deleted later.
    let download = await promiseNextPrivateDownload;
    await download.whenSucceeded();

    // Clean up after checking that there are no new public downloads either.
    let publicDownloads = await publicList.getAll();
    Assert.equal(publicDownloads.length, 0);
    await privateList.removeFinished();
  } finally {
    await BrowserTestUtils.closeWindow(win);
  }
});

add_task(async function test_download_auto_open_private() {
  let privateList = await Downloads.getList(Downloads.PRIVATE);

  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.download.enableDeletePrivate", true],
      ["browser.download.deletePrivate", false],
    ],
  });

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let downloadedFilePaths = [];
  try {
    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      `data:text/html,<a download href="data:application/pdf,test">download</a>`
    );

    let promiseNextPrivateDownload = new Promise(resolve => {
      privateList.addView({
        onDownloadAdded(download) {
          privateList.removeView(this);
          resolve(download);
        },
      });
    });

    let newTabPromise = BrowserTestUtils.waitForNewTab(win.gBrowser);
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    await (await promiseNextPrivateDownload).whenSucceeded();
    await newTabPromise;
  } catch (ex) {
    Assert.ok(false, `Caught unexpected exception: ${ex}`);
  } finally {
    for (let download of privateList._downloads) {
      downloadedFilePaths.push(download.target.path);
    }
    await BrowserTestUtils.closeWindow(win);
  }

  Assert.notEqual(
    downloadedFilePaths.length,
    0,
    "List of private download paths should be populated"
  );
  for (let path of downloadedFilePaths) {
    Assert.ok(await IOUtils.exists(path), "Download exists at " + path);
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_download_privatebrowsing() {
  let privateList = await Downloads.getList(Downloads.PRIVATE);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let downloadedFilePaths = [];
  try {
    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      `data:text/html,<a download href="data:text/plain,">download</a>`
    );

    let promiseNextPrivateDownload = new Promise(resolve => {
      privateList.addView({
        onDownloadAdded(download) {
          privateList.removeView(this);
          resolve(download);
        },
      });
    });

    // Test files downloaded before the pref is set
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    // Wait for the download to finish
    let downloadBeforePref = await promiseNextPrivateDownload;
    await downloadBeforePref.whenSucceeded();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.download.enableDeletePrivate", true],
        ["browser.download.deletePrivate", true],
      ],
    });

    // Test files downloaded after the pref is set
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    // Wait for the download to finish
    let downloadAfterPref = await promiseNextPrivateDownload;
    await downloadAfterPref.whenSucceeded();
  } finally {
    let privateListBefore = await Downloads.getList(Downloads.PRIVATE);
    for (let download of privateListBefore._downloads) {
      downloadedFilePaths.push(download.target.path);
    }
    await BrowserTestUtils.closeWindow(win);
  }

  Assert.notEqual(
    downloadedFilePaths.length,
    0,
    "List of private download paths should be populated"
  );
  for (let path of downloadedFilePaths) {
    Assert.ok(!(await IOUtils.exists(path)), "Download exists at " + path);
  }
});

add_task(async function test_do_not_delete_download_privatebrowsing() {
  let privateList = await Downloads.getList(Downloads.PRIVATE);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let downloadedFilePaths = [];
  try {
    let tab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      `data:text/html,<a download href="data:text/plain,">download</a>`
    );

    let promiseNextPrivateDownload = new Promise(resolve => {
      privateList.addView({
        onDownloadAdded(download) {
          privateList.removeView(this);
          resolve(download);
        },
      });
    });

    // Test files downloaded before the pref is set
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    // Wait for the download to finish
    let downloadBeforePref = await promiseNextPrivateDownload;
    await downloadBeforePref.whenSucceeded();

    await SpecialPowers.pushPrefEnv({
      set: [
        ["browser.download.enableDeletePrivate", true],
        ["browser.download.deletePrivate", false],
      ],
    });

    // Test files downloaded after the pref is set
    await SpecialPowers.spawn(tab.linkedBrowser, [], async function () {
      content.document.querySelector("a").click();
    });

    // Wait for the download to finish
    let downloadAfterPref = await promiseNextPrivateDownload;
    await downloadAfterPref.whenSucceeded();
  } finally {
    let privateListBefore = await Downloads.getList(Downloads.PRIVATE);
    for (let download of privateListBefore._downloads) {
      downloadedFilePaths.push(download.target.path);
    }
    await BrowserTestUtils.closeWindow(win);
  }

  Assert.notEqual(
    downloadedFilePaths.length,
    0,
    "List of private download paths should be populated"
  );
  for (let path of downloadedFilePaths) {
    Assert.ok(await IOUtils.exists(path), "Download exists at " + path);
  }
});
