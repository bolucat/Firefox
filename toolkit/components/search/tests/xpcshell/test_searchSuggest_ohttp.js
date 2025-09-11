/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Testing search suggestions from SearchSuggestionController.sys.mjs.
 */

"use strict";

const { SearchSuggestionController } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs"
);
const { ObliviousHTTP } = ChromeUtils.importESModule(
  "resource://gre/modules/ObliviousHTTP.sys.mjs"
);

const ENGINE_ID = "suggestions-engine-test";

const CONFIG = [
  {
    identifier: ENGINE_ID,
    base: {
      name: "other",
      urls: {
        suggestions: {
          base: "https://example.com",
          params: [
            {
              name: "parameter",
              value: "14235",
            },
          ],
          searchTermParamName: "suggest",
        },
      },
    },
  },
];

add_setup(async function () {
  Services.fog.initializeFOG();
  Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
  Services.prefs.setCharPref(
    "browser.urlbar.merino.ohttpConfigURL",
    "https://example.com/config"
  );
  Services.prefs.setCharPref(
    "browser.urlbar.merino.ohttpRelayURL",
    "https://example.com/relay"
  );
  Services.prefs.setBoolPref("browser.search.suggest.ohttp.featureGate", true);

  SearchTestUtils.setRemoteSettingsConfig(CONFIG);
  await Services.search.init();

  SearchSuggestionController.oHTTPEngineId = CONFIG[0].identifier;

  sinon.stub(ObliviousHTTP, "getOHTTPConfig").resolves({});
  sinon.stub(ObliviousHTTP, "ohttpRequest").callsFake(() => {});
});

add_task(async function simple_remote_results_merino() {
  const suggestions = ["Mozilla", "modern", "mom"];

  ObliviousHTTP.ohttpRequest.callsFake(() => {
    return {
      status: 200,
      json: async () =>
        Promise.resolve({
          suggestions: [
            {
              title: "",
              url: "https://merino.services.mozilla.com",
              provider: "google_suggest",
              is_sponsored: false,
              score: 1,
              custom_details: {
                google_suggest: {
                  suggestions: ["mo", suggestions],
                },
              },
            },
          ],
        }),
      ok: true,
    };
  });

  let expectedParams = {
    q: "mo",
    providers: "google_suggest",
    google_suggest_params: new URLSearchParams([
      ["parameter", 14235],
      ["suggest", "mo"],
    ]),
  };

  // Now do the actual request.
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: Services.search.defaultEngine,
  });
  Assert.equal(result.term, "mo", "Should have the term matching the query");
  Assert.equal(result.local.length, 0, "Should have no local suggestions");
  Assert.deepEqual(
    result.remote.map(r => r.value),
    suggestions,
    "Should have the expected remote suggestions"
  );

  Assert.greater(
    Glean.search.suggestionsLatency[ENGINE_ID].testGetValue().sum,
    0,
    "Should have recorded latency."
  );

  Assert.equal(
    ObliviousHTTP.ohttpRequest.callCount,
    1,
    "Should have requested via OHTTP once"
  );
  let args = ObliviousHTTP.ohttpRequest.firstCall.args;
  Assert.deepEqual(
    args[0],
    "https://example.com/relay",
    "Should have called the Relay URL"
  );
  let url = new URL(args[2]);
  Assert.deepEqual(
    url.origin + url.pathname,
    Services.prefs.getCharPref("browser.urlbar.merino.endpointURL"),
    "Should have the correct URL base"
  );
  for (let [param, value] of Object.entries(expectedParams)) {
    if (URLSearchParams.isInstance(value)) {
      Assert.equal(
        url.searchParams.get(param),
        value.toString(),
        `Should have set the correct value for ${param}`
      );
    } else {
      Assert.equal(
        url.searchParams.get(param),
        value,
        `Should have set the correct value for ${param}`
      );
    }
  }
});

add_task(async function simple_merino_empty_result() {
  // Tests the case when Merino returns an empty response, e.g. due to an error
  // there may be no suggestions returned.

  ObliviousHTTP.ohttpRequest.resetHistory();

  consoleAllowList = consoleAllowList.concat([
    "SearchSuggestionController found an unexpected string value",
  ]);

  ObliviousHTTP.ohttpRequest.callsFake(() => {
    return {
      status: 200,
      json: async () =>
        Promise.resolve({
          suggestions: [],
        }),
      ok: true,
    };
  });

  let expectedParams = {
    q: "mo",
    providers: "google_suggest",
    google_suggest_params: new URLSearchParams([
      ["parameter", 14235],
      ["suggest", "mo"],
    ]),
  };

  // Now do the actual request.
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: Services.search.defaultEngine,
  });
  Assert.equal(result.term, "mo", "Should have the term matching the query");
  Assert.equal(result.local.length, 0, "Should have no local suggestions");
  Assert.deepEqual(
    result.remote.map(r => r.value),
    [],
    "Should have no remote suggestions"
  );

  Assert.greater(
    Glean.search.suggestionsLatency[ENGINE_ID].testGetValue().sum,
    0,
    "Should have recorded latency."
  );

  Assert.equal(
    ObliviousHTTP.ohttpRequest.callCount,
    1,
    "Should have requested via OHTTP once"
  );
  let args = ObliviousHTTP.ohttpRequest.firstCall.args;
  Assert.deepEqual(
    args[0],
    "https://example.com/relay",
    "Should have called the Relay URL"
  );
  let url = new URL(args[2]);
  Assert.deepEqual(
    url.origin + url.pathname,
    Services.prefs.getCharPref("browser.urlbar.merino.endpointURL"),
    "Should have the correct URL base"
  );
  for (let [param, value] of Object.entries(expectedParams)) {
    if (URLSearchParams.isInstance(value)) {
      Assert.equal(
        url.searchParams.get(param),
        value.toString(),
        `Should have set the correct value for ${param}`
      );
    } else {
      Assert.equal(
        url.searchParams.get(param),
        value,
        `Should have set the correct value for ${param}`
      );
    }
  }
});
