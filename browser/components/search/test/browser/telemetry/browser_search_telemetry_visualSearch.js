/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Telemetry test for the visual search content context menu item, the one
// labeled "Search Image with {engine}". Also checks that the search config
// property `excludePartnerCodeFromTelemetry` is respected when recording
// telemetry.

ChromeUtils.defineESModuleGetters(this, {
  EngineURL: "moz-src:///toolkit/components/search/SearchEngine.sys.mjs",
});

// Expected source and action recorded by `BrowserSearchTelemetry`.
const EXPECTED_TELEMETRY_SOURCE = "contextmenu_visual";
const EXPECTED_TELEMETRY_ACTION = "search";

const CONTEXT_MENU_ID = "contentAreaContextMenu";
const VISUAL_SEARCH_MENUITEM_ID = "context-visual-search";

const TEST_PAGE_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/browser_contentContextMenu.xhtml";

// The URL of the primary image in the test page.
const IMAGE_URL =
  "http://mochi.test:8888/browser/browser/components/search/test/browser/ctxmenu-image.png";

const ENGINE_ID = "visual-search";
const ENGINE_NAME = "Visual Search Engine";
const ENGINE_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.org"
  ) + "searchTelemetry.html";

const NONCONFIG_ENGINE_NAME = "Nonconfig Visual Search Engine";

const SEARCH_CONFIG = [
  {
    recordType: "engine",
    identifier: ENGINE_ID,
    base: {
      name: ENGINE_NAME,
      // Make sure the engine has a partner code so we can verify it's excluded
      // from telemetry due to `excludePartnerCodeFromTelemetry` below.
      partnerCode: "test-partner-code",
      urls: {
        visualSearch: {
          base: ENGINE_URL,
          params: [
            {
              name: "mode",
              value: "visual",
            },
            {
              name: "abc",
              value: "ff",
            },
          ],
          searchTermParamName: "url",
          excludePartnerCodeFromTelemetry: true,
        },
      },
    },
    variants: [
      {
        environment: { allRegionsAndLocales: true },
      },
    ],
  },
  {
    recordType: "defaultEngines",
    globalDefault: ENGINE_ID,
    specificDefaults: [],
  },
  {
    recordType: "engineOrders",
    orders: [],
  },
];

const TEST_PROVIDER_INFO = [
  {
    telemetryId: "example-visual",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetry\.html/,
    queryParamNames: ["url"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    adServerAttributes: [],
    extraAdServersRegexps: [/^https:\/\/example\.com\/ad2?/],
    searchMode: {
      mode: "image_search",
    },
    components: [
      {
        type: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        default: true,
      },
    ],
  },
];

SearchTestUtils.init(this);
SearchUITestUtils.init(this);

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["browser.search.visualSearch.featureGate", true],
    ],
  });

  // Install the primary visual search engine via the search config.
  SearchSERPTelemetry.overrideSearchTelemetryForTests(TEST_PROVIDER_INFO);
  await SearchTestUtils.updateRemoteSettingsConfig(SEARCH_CONFIG);
  await waitForIdle();

  let engine = await Services.search.getDefault();
  Assert.equal(
    engine.id,
    ENGINE_ID,
    "Sanity check: The visual search engine should be default"
  );
  Assert.equal(
    engine.partnerCode,
    "test-partner-code",
    "Sanity check: The visual search engine should have a partner code"
  );

  // Install another engine such that it's not a config engine but has a visual
  // search URL. Currently there's no better way to do this than to modify its
  // URLs after the fact. This must happen after setting the config above
  // because that causes all engines to be reloaded.
  await SearchTestUtils.installSearchExtension({
    name: NONCONFIG_ENGINE_NAME,
    search_url: "https://example.com/nonconfig-engine",
    search_url_get_params: "q={searchTerms}",
  });
  let nonconfigEngine = Services.search.getEngineByName(NONCONFIG_ENGINE_NAME);
  nonconfigEngine.wrappedJSObject._urls.push(
    new EngineURL({
      type: SearchUtils.URL_TYPE.VISUAL_SEARCH,
      template: "https://example.com/nonconfig-engine-visual",
    })
  );

  // Enable local telemetry recording for the duration of the tests.
  let canRecord = Services.telemetry.canRecordExtended;
  Services.telemetry.canRecordExtended = true;

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE_URL
  );
  registerCleanupFunction(() => {
    Services.telemetry.canRecordExtended = canRecord;
    SearchSERPTelemetry.overrideSearchTelemetryForTests();
    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function nonPrivateWindow() {
  Services.fog.testResetFOG();
  TelemetryTestUtils.getAndClearKeyedHistogram("SEARCH_COUNTS");

  await openMenuAndClickItem();

  // `sap.impression_counts` labeled counter
  Assert.equal(
    Glean.sapImpressionCounts.contextmenuVisual[ENGINE_ID].testGetValue(),
    1,
    "contextmenuVisual should be recorded once for metric impressionCounts"
  );

  // `sap.counts` event and legacy `SEARCH_COUNTS` histogram
  await SearchUITestUtils.assertSAPTelemetry({
    // The partner code should be excluded.
    partnerCode: null,
    // `SEARCH_COUNTS` should not be updated.
    expectLegacyTelemetry: false,
    engineId: ENGINE_ID,
    engineName: ENGINE_NAME,
    source: EXPECTED_TELEMETRY_SOURCE,
    count: 1,
  });

  Assert.equal(
    Glean.browserSearchContent.contextmenuVisual[
      "example-visual:tagged:ff"
    ].testGetValue(),
    1,
    "Should have recorded a browser.search.content entry"
  );

  assertSERPTelemetry([
    {
      impression: {
        provider: "example-visual",
        tagged: "true",
        partner_code: "ff",
        search_mode: "image_search",
        source: "contextmenu_visual",
        is_shopping_page: "false",
        is_private: "false",
        shopping_tab_displayed: "false",
        is_signed_in: "false",
      },
      abandonment: {
        reason: SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE,
      },
    },
  ]);

  // `browser.engagement.navigation` labeled counter
  Assert.equal(
    Glean.browserEngagementNavigation.contextmenuVisual[
      EXPECTED_TELEMETRY_ACTION
    ].testGetValue(),
    1,
    "contextmenuVisual should be recorded once for metric browserEngagementNavigation"
  );
});

add_task(async function privateWindow_countsEnabled() {
  await doPrivateWindowTest(true);
});

add_task(async function privateWindow_countsDisabled() {
  await doPrivateWindowTest(false);
});

async function doPrivateWindowTest(shouldRecordCounts) {
  Services.fog.testResetFOG();
  TelemetryTestUtils.getAndClearKeyedHistogram("SEARCH_COUNTS");

  await SpecialPowers.pushPrefEnv({
    // This pref is the opposite of what it seems like it should be.
    set: [["browser.engagement.search_counts.pbm", !shouldRecordCounts]],
  });

  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await BrowserTestUtils.openNewForegroundTab(win.gBrowser, TEST_PAGE_URL);

  await openMenuAndClickItem({ win, shouldRecordCounts });

  let expectedCount = shouldRecordCounts ? 1 : null;

  // `sap.impression_counts` labeled counter
  Assert.equal(
    Glean.sapImpressionCounts.contextmenuVisual[ENGINE_ID].testGetValue(),
    expectedCount,
    "contextmenuVisual should be recorded as expected for metric impressionCounts"
  );

  // `sap.counts` event and legacy `SEARCH_COUNTS` histogram
  if (expectedCount) {
    await SearchUITestUtils.assertSAPTelemetry({
      // The partner code should be excluded.
      partnerCode: null,
      // `SEARCH_COUNTS` should not be updated.
      expectLegacyTelemetry: false,
      engineId: ENGINE_ID,
      engineName: ENGINE_NAME,
      source: EXPECTED_TELEMETRY_SOURCE,
      count: expectedCount,
    });

    Assert.equal(
      Glean.browserSearchContent.contextmenuVisual[
        "example-visual:tagged:ff"
      ].testGetValue(),
      1,
      "Should have recorded a browser.search.content entry"
    );

    assertSERPTelemetry([
      {
        impression: {
          provider: "example-visual",
          tagged: "true",
          partner_code: "ff",
          search_mode: "image_search",
          source: "contextmenu_visual",
          is_shopping_page: "false",
          is_private: "true",
          shopping_tab_displayed: "false",
          is_signed_in: "false",
        },
        abandonment: {
          reason: SearchSERPTelemetryUtils.ABANDONMENTS.TAB_CLOSE,
        },
      },
    ]);
  } else {
    Assert.equal(
      Glean.sap.counts.testGetValue(),
      null,
      "No sap.counts events should be recorded"
    );
    Assert.equal(
      Glean.browserSearchContent.contextmenuVisual[
        "example-visual:tagged:ff"
      ].testGetValue(),
      null,
      "No browser.search.content event should be recorded"
    );
    assertSERPTelemetry([]);
  }

  // browser.engagement.navigation labeled counter
  Assert.equal(
    Glean.browserEngagementNavigation.contextmenuVisual[
      EXPECTED_TELEMETRY_ACTION
    ].testGetValue(),
    expectedCount,
    "contextmenuVisual should be recorded as expected for metric browserEngagementNavigation"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
}

// For nonconfig engines with visual search URLs, the `sap.impression_counts`
// labeled counter should be recorded with "none" as the engine name.
add_task(async function nonconfigEngine() {
  Services.fog.testResetFOG();

  let engine = Services.search.getEngineByName(NONCONFIG_ENGINE_NAME);
  Assert.ok(
    engine.wrappedJSObject.getURLOfType(SearchUtils.URL_TYPE.VISUAL_SEARCH),
    "Sanity check: Nonconfig engine has a visual search URL"
  );

  // Make the nonconfig engine the default so that it handles visual searches.
  let previousEngine = await Services.search.getDefault();
  await Services.search.setDefault(
    engine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );

  await openAndCheckMenu({
    shouldBeShown: true,
    expectedEngineNameInLabel: NONCONFIG_ENGINE_NAME,
  });
  await closeMenu();

  Assert.equal(
    Glean.sapImpressionCounts.contextmenuVisual.none.testGetValue(),
    1,
    "impressionCounts.contextmenuVisual should be recorded once with name 'none'"
  );
  Assert.equal(
    Glean.sapImpressionCounts.contextmenuVisual[engine.id].testGetValue(),
    null,
    "impressionCounts.contextmenuVisual should not be recorded with the engine ID"
  );

  await Services.search.setDefault(
    previousEngine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
});

async function openMenuAndClickItem({
  expectedEngineNameInLabel = ENGINE_NAME,
  expectedBaseUrl = ENGINE_URL,
  win = window,
  shouldRecordCounts = true,
} = {}) {
  let testTab = win.gBrowser.selectedTab;

  let pageImpression = shouldRecordCounts ? waitForPageWithImpression() : null;

  let { menu, item } = await openAndCheckMenu({
    win,
    expectedEngineNameInLabel,
    shouldBeShown: true,
  });

  // Click the visual search menuitem and wait for the SERP to load.
  let loadPromise = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    expectedBaseUrl +
      "?mode=visual&abc=ff&url=" +
      encodeURIComponent(IMAGE_URL),
    true
  );
  menu.activateItem(item);

  info("Waiting for visual search SERP to load");
  let serpTab = await loadPromise;
  info("Visual search SERP loaded");

  BrowserTestUtils.removeTab(serpTab);
  BrowserTestUtils.switchTab(win.gBrowser, testTab);

  info("Awaiting impression");
  await pageImpression;
  info("Impression received");
}

async function openAndCheckMenu({
  shouldBeShown,
  expectedEngineNameInLabel,
  win = window,
  selector = "#image",
}) {
  let selectorMatches = await SpecialPowers.spawn(
    win.gBrowser.selectedBrowser,
    [selector],
    async function (sel) {
      return !!content.document.querySelector(sel);
    }
  );
  Assert.ok(
    selectorMatches,
    "Sanity check: selector should match an element in the page: " + selector
  );

  let menu = win.document.getElementById(CONTEXT_MENU_ID);
  let popupPromise = BrowserTestUtils.waitForEvent(menu, "popupshown");

  info("Opening context menu");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    selector,
    { type: "contextmenu", button: 2 },
    win.gBrowser.selectedBrowser
  );

  info("Waiting for context menu to open");
  await popupPromise;
  info("Context menu opened");

  let item = win.document.getElementById(VISUAL_SEARCH_MENUITEM_ID);
  Assert.ok(item, "The visual search menuitem should exist");
  Assert.equal(
    item.hidden,
    !shouldBeShown,
    "The visual search menuitem should be shown as expected"
  );
  if (shouldBeShown) {
    let expectedLabel = `Search Image with ${expectedEngineNameInLabel}`;
    await TestUtils.waitForCondition(
      () => item.label == expectedLabel,
      "Waiting for expected label to be set on item: " + expectedLabel
    );
    Assert.equal(
      item.label,
      expectedLabel,
      "The visual search menuitem should have the expected label"
    );
  }

  return { menu, item };
}

async function closeMenu({ win = window } = {}) {
  let menu = win.document.getElementById(CONTEXT_MENU_ID);
  let popupPromise = BrowserTestUtils.waitForEvent(menu, "popuphidden");

  info("Closing context menu");
  menu.hidePopup();

  info("Waiting for context menu to close");
  await popupPromise;
  info("Context menu closed");
}
