/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

AddonTestUtils.initMochitest(this);

// ----------------------------------------------------------------------------
// Checks that a chained redirect through a data URI and javascript is blocked

function setup_redirect(aSettings) {
  var url = TESTROOT + "redirect.sjs?mode=setup";
  for (var name in aSettings) {
    url += "&" + name + "=" + encodeURIComponent(aSettings[name]);
  }

  var req = new XMLHttpRequest();
  req.open("GET", url, false);
  req.send(null);
}

function test() {
  waitForExplicitFinish();
  SpecialPowers.pushPrefEnv(
    {
      set: [
        ["network.allow_redirect_to_data", true],
        ["security.data_uri.block_toplevel_data_uri_navigations", false],
        // Relax the user input requirements while running this test.
        ["xpinstall.userActivation.required", false],
        // Bug 721336 - Use sync XHR system requests
        ["network.xhr.block_sync_system_requests", false],
      ],
    },
    runTest
  );
}

function runTest() {
  Harness.installOriginBlockedCallback = install_blocked;
  Harness.installsCompletedCallback = finish_test;
  Harness.setup();

  setup_redirect({
    Location:
      "data:text/html,<script>window.location.href='" +
      TESTROOT +
      "amosigned.xpi'</script>",
  });

  BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TESTROOT + "redirect.sjs?mode=redirect"
  );
}

function install_blocked(installInfo) {
  is(
    installInfo.installs.length,
    1,
    "Got one AddonInstall instance as expected"
  );
  AddonTestUtils.checkInstallInfo(installInfo.installs[0], {
    method: "link",
    source: "unknown",
    sourceURL: /moz-nullprincipal:\{.*\}/,
  });
}

function finish_test(count) {
  is(count, 0, "No add-ons should have been installed");
  PermissionTestUtils.remove("http://example.com", "install");

  gBrowser.removeCurrentTab();
  Harness.finish();
  finish();
}
