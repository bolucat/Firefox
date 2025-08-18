/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SponsorProtection:
    "moz-src:///browser/components/newtab/SponsorProtection.sys.mjs",
});

// Test whether a visit information is annotated correctly when picking a result.

if (AppConstants.platform === "macosx") {
  requestLongerTimeout(2);
}

const FRECENCY = {
  ORGANIC: 2000,
  SPONSORED: -1,
  BOOKMARKED: 2075,
  SEARCHED: 100,
};

const {
  VISIT_SOURCE_ORGANIC,
  VISIT_SOURCE_SPONSORED,
  VISIT_SOURCE_BOOKMARKED,
  VISIT_SOURCE_SEARCHED,
} = PlacesUtils.history;

/**
 * To be used before checking database contents when they depend on a visit
 * being added to History.
 *
 * @param {string} href the page to await notifications for.
 */
async function waitForVisitNotification(href) {
  await PlacesTestUtils.waitForNotification("page-visited", events =>
    events.some(e => e.url === href)
  );
}

async function assertDatabase({ targetURL, expected }) {
  const frecency = await PlacesTestUtils.getDatabaseValue(
    "moz_places",
    "frecency",
    { url: targetURL }
  );
  Assert.equal(frecency, expected.frecency, "Frecency is correct");

  const placesId = await PlacesTestUtils.getDatabaseValue("moz_places", "id", {
    url: targetURL,
  });
  const expectedTriggeringPlaceId = expected.triggerURL
    ? await PlacesTestUtils.getDatabaseValue("moz_places", "id", {
        url: expected.triggerURL,
      })
    : null;
  const db = await PlacesUtils.promiseDBConnection();
  const rows = await db.execute(
    "SELECT source, triggeringPlaceId FROM moz_historyvisits WHERE place_id = :place_id AND source = :source",
    {
      place_id: placesId,
      source: expected.source,
    }
  );
  Assert.equal(rows.length, 1);
  Assert.equal(
    rows[0].getResultByName("triggeringPlaceId"),
    expectedTriggeringPlaceId,
    `The triggeringPlaceId in database is correct for ${targetURL}`
  );
}

function registerProvider(payload) {
  const provider = new UrlbarTestUtils.TestProvider({
    results: [
      new UrlbarResult(
        UrlbarUtils.RESULT_TYPE.URL,
        UrlbarUtils.RESULT_SOURCE.SEARCH,
        ...UrlbarResult.payloadAndSimpleHighlights([], {
          ...payload,
        })
      ),
    ],
    priority: Infinity,
  });
  UrlbarProvidersManager.registerProvider(provider);
  return provider;
}

async function pickResult({ input, payloadURL, redirectTo }) {
  const destinationURL = redirectTo || payloadURL;
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: input,
    fireInputEvent: true,
  });

  const result = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(result.url, payloadURL);
  UrlbarTestUtils.setSelectedRowIndex(window, 0);

  info("Show result and wait for loading");
  const onLoad = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    destinationURL
  );
  EventUtils.synthesizeKey("KEY_Enter");
  await onLoad;
}

add_setup(async function () {
  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
  });
});

add_task(async function basic() {
  const testData = [
    {
      description: "Sponsored result",
      input: "exa",
      payload: {
        url: "https://example.com/",
        isSponsored: true,
      },
      expected: {
        source: VISIT_SOURCE_SPONSORED,
        frecency: FRECENCY.SPONSORED,
      },
    },
    {
      description: "Bookmarked result",
      input: "exa",
      payload: {
        url: "https://example.com/",
      },
      bookmarks: [
        {
          parentGuid: PlacesUtils.bookmarks.toolbarGuid,
          url: Services.io.newURI("https://example.com/"),
          title: "test bookmark",
        },
      ],
      expected: {
        source: VISIT_SOURCE_BOOKMARKED,
        frecency: FRECENCY.BOOKMARKED,
      },
    },
    {
      description: "Sponsored and bookmarked result",
      input: "exa",
      payload: {
        url: "https://example.com/",
        isSponsored: true,
      },
      bookmarks: [
        {
          parentGuid: PlacesUtils.bookmarks.toolbarGuid,
          url: Services.io.newURI("https://example.com/"),
          title: "test bookmark",
        },
      ],
      expected: {
        source: VISIT_SOURCE_SPONSORED,
        frecency: FRECENCY.BOOKMARKED,
      },
    },
    {
      description: "Organic result",
      input: "exa",
      payload: {
        url: "https://example.com/",
      },
      expected: {
        source: VISIT_SOURCE_ORGANIC,
        frecency: FRECENCY.ORGANIC,
      },
    },
  ];

  for (const { description, input, payload, bookmarks, expected } of testData) {
    info(description);
    const provider = registerProvider(payload);

    for (const bookmark of bookmarks || []) {
      await PlacesUtils.bookmarks.insert(bookmark);
    }

    await BrowserTestUtils.withNewTab("about:blank", async browser => {
      info("Pick result");
      let promiseVisited = waitForVisitNotification(payload.url);
      await pickResult({ input, payloadURL: payload.url });
      await promiseVisited;
      info("Check database");
      await assertDatabase({ targetURL: payload.url, expected });
      Assert.ok(
        !SponsorProtection.isProtectedBrowser(browser),
        "Navigations from the URL bar do not cause sponsor protection at this time."
      );
    });

    UrlbarProvidersManager.unregisterProvider(provider);
    await PlacesUtils.history.clear();
    await PlacesUtils.bookmarks.eraseEverything();
  }
});

add_task(async function redirection() {
  const redirectTo = "https://example.com/";
  const payload = {
    url: "https://example.com/browser/browser/components/urlbar/tests/browser/redirect_to.sjs?/",
    isSponsored: true,
  };
  const input = "exa";
  const provider = registerProvider(payload);

  await BrowserTestUtils.withNewTab("about:home", async () => {
    info("Pick result");
    let promises = [
      waitForVisitNotification(payload.url),
      waitForVisitNotification(redirectTo),
    ];
    await pickResult({ input, payloadURL: payload.url, redirectTo });
    await Promise.all(promises);

    info("Check database");
    await assertDatabase({
      targetURL: payload.url,
      expected: {
        source: VISIT_SOURCE_SPONSORED,
        frecency: FRECENCY.SPONSORED,
      },
    });
    await assertDatabase({
      targetURL: redirectTo,
      expected: {
        source: VISIT_SOURCE_SPONSORED,
        triggerURL: payload.url,
        frecency: FRECENCY.SPONSORED,
      },
    });
  });

  await PlacesUtils.history.clear();
  await PlacesUtils.bookmarks.eraseEverything();
  UrlbarProvidersManager.unregisterProvider(provider);
});

add_task(async function search() {
  const originalDefaultEngine = await Services.search.getDefault();
  await SearchTestUtils.installSearchExtension({
    name: "test engine",
    keyword: "@test",
  });

  const testData = [
    {
      description: "Searched result",
      input: "@test abc",
      resultURL: "https://example.com/?q=abc",
      expected: {
        source: VISIT_SOURCE_SEARCHED,
        frecency: FRECENCY.SEARCHED,
      },
    },
    {
      description: "Searched bookmarked result",
      input: "@test abc",
      resultURL: "https://example.com/?q=abc",
      bookmarks: [
        {
          parentGuid: PlacesUtils.bookmarks.toolbarGuid,
          url: Services.io.newURI("https://example.com/?q=abc"),
          title: "test bookmark",
        },
      ],
      expected: {
        source: VISIT_SOURCE_BOOKMARKED,
        frecency: FRECENCY.BOOKMARKED,
      },
    },
  ];

  for (const {
    description,
    input,
    resultURL,
    bookmarks,
    expected,
  } of testData) {
    info(description);
    await BrowserTestUtils.withNewTab("about:blank", async () => {
      for (const bookmark of bookmarks || []) {
        await PlacesUtils.bookmarks.insert(bookmark);
      }

      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: input,
      });
      const onLoad = BrowserTestUtils.browserLoaded(
        gBrowser.selectedBrowser,
        false,
        resultURL
      );
      let promiseVisited = waitForVisitNotification(resultURL);
      EventUtils.synthesizeKey("KEY_Enter");
      await onLoad;
      await promiseVisited;
      await assertDatabase({ targetURL: resultURL, expected });

      // Open another URL to check whther the source is not inherited.
      const payload = { url: "https://example.com/" };
      const provider = registerProvider(payload);
      promiseVisited = waitForVisitNotification(payload.url);
      await pickResult({ input, payloadURL: payload.url });
      await promiseVisited;
      await assertDatabase({
        targetURL: payload.url,
        expected: {
          source: VISIT_SOURCE_ORGANIC,
          frecency: FRECENCY.ORGANIC,
        },
      });
      UrlbarProvidersManager.unregisterProvider(provider);

      await PlacesUtils.history.clear();
      await PlacesUtils.bookmarks.eraseEverything();
    });
  }

  await Services.search.setDefault(
    originalDefaultEngine,
    Ci.nsISearchService.CHANGE_REASON_UNKNOWN
  );
});
