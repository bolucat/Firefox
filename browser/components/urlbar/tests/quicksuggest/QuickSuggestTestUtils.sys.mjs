/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-disable jsdoc/require-param */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AmpSuggestions: "resource:///modules/urlbar/private/AmpSuggestions.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTestUtils: "resource://testing-common/NimbusTestUtils.sys.mjs",
  QuickSuggest: "resource:///modules/QuickSuggest.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettingsServer:
    "resource://testing-common/RemoteSettingsServer.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SharedRemoteSettingsService:
    "resource://gre/modules/RustSharedRemoteSettingsService.sys.mjs",
  Suggestion:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
  YelpSubjectType:
    "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustSuggest.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

/**
 * @import {Assert} from "resource://testing-common/Assert.sys.mjs"
 */

let gTestScope;

// Test utils singletons need special handling. Since they are uninitialized in
// cleanup functions, they must be re-initialized on each new test. That does
// not happen automatically inside system modules like this one because system
// module lifetimes are the app's lifetime, unlike individual browser chrome and
// xpcshell tests.
Object.defineProperty(lazy, "UrlbarTestUtils", {
  get: () => {
    // eslint-disable-next-line mozilla/valid-lazy
    if (!lazy._UrlbarTestUtils) {
      const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
        "resource://testing-common/UrlbarTestUtils.sys.mjs"
      );
      module.init(gTestScope);
      gTestScope.registerCleanupFunction(() => {
        // Make sure the utils are re-initialized during the next test.
        // eslint-disable-next-line mozilla/valid-lazy
        lazy._UrlbarTestUtils = null;
      });
      // eslint-disable-next-line mozilla/valid-lazy
      lazy._UrlbarTestUtils = module;
    }
    // eslint-disable-next-line mozilla/valid-lazy
    return lazy._UrlbarTestUtils;
  },
});

// Test utils singletons need special handling. Since they are uninitialized in
// cleanup functions, they must be re-initialized on each new test. That does
// not happen automatically inside system modules like this one because system
// module lifetimes are the app's lifetime, unlike individual browser chrome and
// xpcshell tests.
Object.defineProperty(lazy, "MerinoTestUtils", {
  get: () => {
    // eslint-disable-next-line mozilla/valid-lazy
    if (!lazy._MerinoTestUtils) {
      const { MerinoTestUtils: module } = ChromeUtils.importESModule(
        "resource://testing-common/MerinoTestUtils.sys.mjs"
      );
      module.init(gTestScope);
      gTestScope.registerCleanupFunction(() => {
        // Make sure the utils are re-initialized during the next test.
        // eslint-disable-next-line mozilla/valid-lazy
        lazy._MerinoTestUtils = null;
      });
      // eslint-disable-next-line mozilla/valid-lazy
      lazy._MerinoTestUtils = module;
    }
    // eslint-disable-next-line mozilla/valid-lazy
    return lazy._MerinoTestUtils;
  },
});

// TODO bug 1881409: Previously this was an empty object, but the Rust backend
// seems to persist old config after ingesting an empty config object.
const DEFAULT_CONFIG = {
  // Zero means there is no cap, the same as if this wasn't specified at all.
  show_less_frequently_cap: 0,
};

// The following properties and methods are copied from the test scope to the
// test utils object so they can be easily accessed. Be careful about assuming a
// particular property will be defined because depending on the scope -- browser
// test or xpcshell test -- some may not be.
const TEST_SCOPE_PROPERTIES = [
  "Assert",
  "EventUtils",
  "info",
  "registerCleanupFunction",
];

/** @typedef {() => Promise<void>} cleanupFunctionType */

/**
 * Test utils for quick suggest.
 */
class _QuickSuggestTestUtils {
  /** @type {Assert} */
  Assert = undefined;

  /** @type {object} */
  EventUtils = undefined;

  /** @type {(message:string) => void} */
  info = undefined;

  /** @type {(cleanupFn: cleanupFunctionType) => void} */
  registerCleanupFunction = undefined;

  /**
   * Initializes the utils.
   *
   * @param {object} scope
   *   The global JS scope where tests are being run. This allows the instance
   *   to access test helpers like `Assert` that are available in the scope.
   */
  init(scope) {
    if (!scope) {
      throw new Error("QuickSuggestTestUtils() must be called with a scope");
    }
    gTestScope = scope;
    for (let p of TEST_SCOPE_PROPERTIES) {
      this[p] = scope[p];
    }
    // If you add other properties to `this`, null them in `uninit()`.

    scope.registerCleanupFunction?.(() => this.uninit());
  }

  /**
   * Uninitializes the utils. If they were created with a test scope that
   * defines `registerCleanupFunction()`, you don't need to call this yourself
   * because it will automatically be called as a cleanup function. Otherwise
   * you'll need to call this.
   */
  uninit() {
    gTestScope = null;
    for (let p of TEST_SCOPE_PROPERTIES) {
      this[p] = null;
    }
  }

  get RS_COLLECTION() {
    return {
      AMP: "quicksuggest-amp",
      OTHER: "quicksuggest-other",
    };
  }

  get RS_TYPE() {
    return {
      AMP: "amp",
      WIKIPEDIA: "wikipedia",
    };
  }

  get DEFAULT_CONFIG() {
    // Return a clone so callers can modify it.
    return Cu.cloneInto(DEFAULT_CONFIG, this);
  }

  /**
   * Sets up local remote settings and Merino servers, registers test
   * suggestions, and initializes Suggest.
   *
   * @param {object} [options]
   *   Options object
   * @param {Array} [options.remoteSettingsRecords]
   *   Array of remote settings records. Each item in this array should be a
   *   realistic remote settings record with some exceptions as noted below.
   *   For details see `RemoteSettingsServer.addRecords()`.
   *     - `record.attachment` - Optional. This should be the attachment itself
   *       and not its metadata. It should be a JSONable object.
   *     - `record.collection` - Optional. The name of the RS collection that
   *       the record should be added to. Defaults to "quicksuggest-other".
   * @param {Array} [options.merinoSuggestions]
   *   Array of Merino suggestion objects. If given, this function will start
   *   the mock Merino server and set `quicksuggest.dataCollection.enabled` to
   *   true so that `UrlbarProviderQuickSuggest` will fetch suggestions from it.
   *   Otherwise Merino will not serve suggestions, but you can still set up
   *   Merino without using this function by using `MerinoTestUtils` directly.
   * @param {object} [options.config]
   *   The Suggest configuration object. This should not be the full remote
   *   settings record; only pass the object that should be set to the nested
   *   `configuration` object inside the record.
   * @param {Array} [options.prefs]
   *   An array of Suggest-related prefs to set. This is useful because setting
   *   some prefs, like feature gates, can cause Suggest to sync from remote
   *   settings; this function will set them, wait for sync to finish, and clear
   *   them when the cleanup function is called. Each item in this array should
   *   itself be a two-element array `[prefName, prefValue]` similar to the
   *   `set` array passed to `SpecialPowers.pushPrefEnv()`, except here pref
   *   names are relative to `browser.urlbar`.
   * @returns {Promise<(() => void) | (() => Promise<void>)>}
   *   An async cleanup function. This function is automatically registered as a
   *   cleanup function, so you only need to call it if your test needs to clean
   *   up Suggest before it ends, for example if you have a small number of
   *   tasks that need Suggest and it's not enabled throughout your test. The
   *   cleanup function is idempotent so there's no harm in calling it more than
   *   once. Be sure to `await` it.
   */
  async ensureQuickSuggestInit({
    remoteSettingsRecords = [],
    merinoSuggestions = null,
    config = DEFAULT_CONFIG,
    prefs = [],
  } = {}) {
    this.#log("ensureQuickSuggestInit", "Started");

    this.#log("ensureQuickSuggestInit", "Awaiting ExperimentAPI.init");
    const initializedExperimentAPI = await lazy.ExperimentAPI.init();

    this.#log("ensureQuickSuggestInit", "Awaiting ExperimentAPI.ready");
    await lazy.ExperimentAPI.ready();

    // Set up the local remote settings server.
    this.#log("ensureQuickSuggestInit", "Preparing remote settings server");
    if (!this.#remoteSettingsServer) {
      this.#remoteSettingsServer = new lazy.RemoteSettingsServer();
    }

    this.#remoteSettingsServer.removeRecords();
    for (let [collection, records] of this.#recordsByCollection(
      remoteSettingsRecords
    )) {
      await this.#remoteSettingsServer.addRecords({ collection, records });
    }
    await this.#remoteSettingsServer.addRecords({
      collection: this.RS_COLLECTION.OTHER,
      records: [{ type: "configuration", configuration: config }],
    });

    this.#log("ensureQuickSuggestInit", "Starting remote settings server");
    await this.#remoteSettingsServer.start();
    this.#log("ensureQuickSuggestInit", "Remote settings server started");

    // Init Suggest and force the region to US and the locale to en-US, which
    // will cause Suggest to be enabled along with all suggestion types that are
    // enabled in the US by default. Do this after setting up remote settings
    // because the Rust backend will immediately try to sync.
    this.#log(
      "ensureQuickSuggestInit",
      "Calling QuickSuggest.init() and setting prefs"
    );
    await lazy.QuickSuggest.init({ region: "US", locale: "en-US" });

    // Set prefs requested by the caller.
    for (let [name, value] of prefs) {
      lazy.UrlbarPrefs.set(name, value);
    }

    // Tell the Rust backend to use the local remote setting server.
    lazy.SharedRemoteSettingsService.updateServer({
      url: this.#remoteSettingsServer.url.toString(),
      bucketName: "main",
    });
    await lazy.QuickSuggest.rustBackend._test_setRemoteSettingsService(
      lazy.SharedRemoteSettingsService.rustService()
    );

    // Wait for the Rust backend to finish syncing.
    await this.forceSync();

    // Set up Merino. This can happen any time relative to Suggest init.
    if (merinoSuggestions) {
      this.#log("ensureQuickSuggestInit", "Setting up Merino server");
      await lazy.MerinoTestUtils.server.start();
      lazy.MerinoTestUtils.server.response.body.suggestions = merinoSuggestions;
      lazy.UrlbarPrefs.set("quicksuggest.dataCollection.enabled", true);
      this.#log("ensureQuickSuggestInit", "Done setting up Merino server");
    }

    let cleanupCalled = false;
    let cleanup = async () => {
      if (!cleanupCalled) {
        cleanupCalled = true;
        await this.#uninitQuickSuggest(prefs, !!merinoSuggestions);

        if (initializedExperimentAPI) {
          // Only reset if we're in an xpcshell-test and actually initialized
          // the ExperimentAPI.
          lazy.ExperimentAPI._resetForTests();
        }
      }
    };
    this.registerCleanupFunction?.(cleanup);

    this.#log("ensureQuickSuggestInit", "Done");
    return cleanup;
  }

  async #uninitQuickSuggest(prefs, clearDataCollectionEnabled) {
    this.#log("#uninitQuickSuggest", "Started");

    // Reset prefs, which can cause the Rust backend to start syncing. Wait for
    // it to finish.
    for (let [name] of prefs) {
      lazy.UrlbarPrefs.clear(name);
    }
    await this.forceSync();

    this.#log("#uninitQuickSuggest", "Stopping remote settings server");
    await this.#remoteSettingsServer.stop();

    if (clearDataCollectionEnabled) {
      lazy.UrlbarPrefs.clear("quicksuggest.dataCollection.enabled");
    }

    await lazy.QuickSuggest.rustBackend._test_setRemoteSettingsService(null);

    this.#log("#uninitQuickSuggest", "Done");
  }

  /**
   * Removes all records from the local remote settings server and adds a new
   * batch of records.
   *
   * @param {Array} records
   *   Array of remote settings records. See `ensureQuickSuggestInit()`.
   * @param {object} [options]
   *   Options object.
   * @param {boolean} [options.forceSync]
   *   Whether to force Suggest to sync after updating the records.
   */
  async setRemoteSettingsRecords(records, { forceSync = true } = {}) {
    this.#log("setRemoteSettingsRecords", "Started");

    this.#remoteSettingsServer.removeRecords();
    for (let [collection, recs] of this.#recordsByCollection(records)) {
      await this.#remoteSettingsServer.addRecords({
        collection,
        records: recs,
      });
    }

    if (forceSync) {
      this.#log("setRemoteSettingsRecords", "Forcing sync");
      await this.forceSync();
    }
    this.#log("setRemoteSettingsRecords", "Done");
  }

  /**
   * Sets the quick suggest configuration. You should call this again with
   * `DEFAULT_CONFIG` before your test finishes. See also `withConfig()`.
   *
   * @param {object} config
   *   The quick suggest configuration object. This should not be the full
   *   remote settings record; only pass the object that should be set to the
   *   `configuration` nested object inside the record.
   */
  async setConfig(config) {
    this.#log("setConfig", "Started");
    let type = "configuration";
    this.#remoteSettingsServer.removeRecords({ type });
    await this.#remoteSettingsServer.addRecords({
      collection: this.RS_COLLECTION.OTHER,
      records: [{ type, configuration: config }],
    });
    this.#log("setConfig", "Forcing sync");
    await this.forceSync();
    this.#log("setConfig", "Done");
  }

  /**
   * Forces Suggest to sync with remote settings. This can be used to ensure
   * Suggest has finished all sync activity.
   */
  async forceSync() {
    this.#log("forceSync", "Started");
    if (lazy.QuickSuggest.rustBackend.isEnabled) {
      this.#log("forceSync", "Syncing Rust backend");
      await lazy.QuickSuggest.rustBackend._test_ingest();
      this.#log("forceSync", "Done syncing Rust backend");
    }
    this.#log("forceSync", "Done");
  }

  /**
   * Sets the quick suggest configuration, calls your callback, and restores the
   * previous configuration.
   *
   * @param {object} options
   *   The options object.
   * @param {object} options.config
   *   The configuration that should be used with the callback
   * @param {Function} options.callback
   *   Will be called with the configuration applied
   *
   * @see {@link setConfig}
   */
  async withConfig({ config, callback }) {
    let original = lazy.QuickSuggest.config;
    await this.setConfig(config);
    await callback();
    await this.setConfig(original);
  }

  /**
   * Returns an AMP (sponsored) suggestion suitable for storing in a remote
   * settings attachment.
   *
   * @returns {object}
   *   An AMP suggestion for storing in remote settings.
   */
  ampRemoteSettings({
    keywords = ["amp"],
    full_keywords = keywords.map(kw => [kw, 1]),
    url = "https://example.com/amp",
    title = "Amp Suggestion",
    score = 0.3,
  } = {}) {
    return {
      keywords,
      full_keywords,
      url,
      title,
      score,
      id: 1,
      click_url: "https://example.com/amp-click",
      impression_url: "https://example.com/amp-impression",
      advertiser: "Amp",
      iab_category: "22 - Shopping",
      icon: "1234",
    };
  }

  /**
   * Returns an expected AMP (sponsored) result that can be passed to
   * `check_results()` in xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  ampResult({
    source = "rust",
    provider = "Amp",
    keyword = "amp",
    fullKeyword = keyword,
    title = "Amp Suggestion",
    url = "https://example.com/amp",
    originalUrl = url,
    icon = null,
    iconBlob = null,
    impressionUrl = "https://example.com/amp-impression",
    clickUrl = "https://example.com/amp-click",
    blockId = 1,
    advertiser = "Amp",
    iabCategory = "22 - Shopping",
    suggestedIndex = 0,
    isSuggestedIndexRelativeToGroup = true,
    isBestMatch = false,
    requestId = undefined,
    descriptionL10n = { id: "urlbar-result-action-sponsored" },
    categories = [],
  } = {}) {
    let result = {
      suggestedIndex,
      isSuggestedIndexRelativeToGroup,
      isBestMatch,
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      heuristic: false,
      payload: {
        title,
        url,
        originalUrl,
        requestId,
        source,
        provider,
        displayUrl: url.replace(/^https:\/\//, ""),
        isSponsored: true,
        qsSuggestion: fullKeyword ?? keyword,
        sponsoredImpressionUrl: impressionUrl,
        sponsoredClickUrl: clickUrl,
        sponsoredBlockId: blockId,
        sponsoredAdvertiser: advertiser,
        sponsoredIabCategory: iabCategory,
        isBlockable: true,
        isManageable: true,
        telemetryType: "adm_sponsored",
      },
    };

    if (descriptionL10n) {
      result.payload.descriptionL10n = descriptionL10n;
    }

    if (result.payload.source == "rust") {
      result.payload.iconBlob = iconBlob;
      result.payload.suggestionObject = new lazy.Suggestion.Amp({
        title,
        url,
        rawUrl: originalUrl,
        icon: null,
        iconMimetype: null,
        fullKeyword,
        blockId,
        advertiser,
        iabCategory,
        categories,
        impressionUrl,
        clickUrl,
        rawClickUrl: clickUrl,
        score: 0.3,
        ftsMatchInfo: null,
      });
    } else {
      result.payload.icon = icon;
    }

    return result;
  }

  /**
   * Returns a Wikipedia (non-sponsored) suggestion suitable for storing in a
   * remote settings attachment.
   *
   * @returns {object}
   *   A Wikipedia suggestion for storing in remote settings.
   */
  wikipediaRemoteSettings({
    keywords = ["wikipedia"],
    url = "https://example.com/wikipedia",
    title = "Wikipedia Suggestion",
    score = 0.2,
  } = {}) {
    return {
      keywords,
      url,
      title,
      score,
      id: 2,
      click_url: "https://example.com/wikipedia-click",
      impression_url: "https://example.com/wikipedia-impression",
      advertiser: "Wikipedia",
      iab_category: "5 - Education",
      icon: "1234",
    };
  }

  /**
   * Returns an expected Wikipedia result that can be passed to
   * `check_results()` in xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  wikipediaResult({
    source = "rust",
    provider = "Wikipedia",
    keyword = "wikipedia",
    fullKeyword = keyword,
    title = "Wikipedia Suggestion",
    url = "https://example.com/wikipedia",
    icon = null,
    iconBlob = null,
    suggestedIndex = -1,
    isSuggestedIndexRelativeToGroup = true,
    telemetryType = "adm_nonsponsored",
  } = {}) {
    let result = {
      suggestedIndex,
      isSuggestedIndexRelativeToGroup,
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      heuristic: false,
      payload: {
        title,
        url,
        icon,
        iconBlob,
        source,
        provider,
        telemetryType,
        displayUrl: url.replace(/^https:\/\//, ""),
        isSponsored: false,
        qsSuggestion: fullKeyword ?? keyword,
        isBlockable: true,
        isManageable: true,
      },
    };

    if (source == "rust") {
      result.payload.suggestionObject = new lazy.Suggestion.Wikipedia({
        title,
        url,
        icon: null,
        iconMimeType: null,
        fullKeyword,
      });
    }

    return result;
  }

  /**
   * Returns an AMO (addons) suggestion suitable for storing in a remote
   * settings attachment.
   *
   * @returns {object}
   *   An AMO suggestion for storing in remote settings.
   */
  amoRemoteSettings({
    keywords = ["amo"],
    url = "https://example.com/amo",
    title = "Amo Suggestion",
    score = 0.2,
  } = {}) {
    return {
      keywords,
      url,
      title,
      score,
      guid: "amo-suggestion@example.com",
      icon: "https://example.com/addon.svg",
      rating: "4.7",
      description: "Addon with score",
      number_of_ratings: 1256,
    };
  }

  /**
   * Returns a remote settings weather record.
   *
   * @returns {object}
   *   A weather record for storing in remote settings.
   */
  weatherRecord({
    keywords = ["weather"],
    min_keyword_length = undefined,
    score = 0.29,
  } = {}) {
    return {
      type: "weather",
      attachment: {
        keywords,
        min_keyword_length,
        score,
      },
    };
  }

  /**
   * Returns remote settings records containing geonames populated with some
   * cities.
   *
   * @returns {Array}
   *   One or more geonames records for storing in remote settings.
   */
  geonamesRecords() {
    let geonames = [
      // United States
      {
        id: 6252001,
        name: "United States",
        feature_class: "A",
        feature_code: "PCLI",
        country: "US",
        admin1: "00",
        population: 327167434,
        latitude: "39.76",
        longitude: "-98.5",
      },
      // Waterloo, AL
      {
        id: 4096497,
        name: "Waterloo",
        feature_class: "P",
        feature_code: "PPL",
        country: "US",
        admin1: "AL",
        admin2: "077",
        population: 200,
        latitude: "34.91814",
        longitude: "-88.0642",
      },
      // AL
      {
        id: 4829764,
        name: "Alabama",
        feature_class: "A",
        feature_code: "ADM1",
        country: "US",
        admin1: "AL",
        population: 4530315,
        latitude: "32.75041",
        longitude: "-86.75026",
      },
      // Waterloo, IA
      {
        id: 4880889,
        name: "Waterloo",
        feature_class: "P",
        feature_code: "PPLA2",
        country: "US",
        admin1: "IA",
        admin2: "013",
        admin3: "94597",
        population: 68460,
        latitude: "42.49276",
        longitude: "-92.34296",
      },
      // IA
      {
        id: 4862182,
        name: "Iowa",
        feature_class: "A",
        feature_code: "ADM1",
        country: "US",
        admin1: "IA",
        population: 2955010,
        latitude: "42.00027",
        longitude: "-93.50049",
      },

      // Made-up cities with the same name in the US and CA. The CA city has a
      // larger population.
      {
        id: 100,
        name: "US CA City",
        feature_class: "P",
        feature_code: "PPL",
        country: "US",
        admin1: "IA",
        population: 1,
        latitude: "38.06084",
        longitude: "-97.92977",
      },
      {
        id: 101,
        name: "US CA City",
        feature_class: "P",
        feature_code: "PPL",
        country: "CA",
        admin1: "08",
        population: 2,
        latitude: "45.50884",
        longitude: "-73.58781",
      },

      // Made-up cities that are only ~1.5 km apart.
      {
        id: 102,
        name: "Twin City A",
        feature_class: "P",
        feature_code: "PPL",
        country: "US",
        admin1: "GA",
        population: 1,
        latitude: "33.748889",
        longitude: "-84.39",
      },
      {
        id: 103,
        name: "Twin City B",
        feature_class: "P",
        feature_code: "PPL",
        country: "US",
        admin1: "GA",
        population: 2,
        latitude: "33.76",
        longitude: "-84.4",
      },

      // Tokyo
      {
        id: 1850147,
        name: "Tokyo",
        feature_class: "P",
        feature_code: "PPLC",
        country: "JP",
        admin1: "Tokyo-to",
        population: 9733276,
        latitude: "35.6895",
        longitude: "139.69171",
      },

      // UK
      {
        id: 2635167,
        name: "United Kingdom of Great Britain and Northern Ireland",
        feature_class: "A",
        feature_code: "PCLI",
        country: "GB",
        admin1: "00",
        population: 66488991,
        latitude: "54.75844",
        longitude: "-2.69531",
      },
      // England
      {
        id: 6269131,
        name: "England",
        feature_class: "A",
        feature_code: "ADM1",
        country: "GB",
        admin1: "ENG",
        population: 57106398,
        latitude: "52.16045",
        longitude: "-0.70312",
      },
      // Liverpool (metropolitan borough, admin2 for Liverpool city)
      {
        id: 3333167,
        name: "Liverpool",
        feature_class: "A",
        feature_code: "ADM2",
        country: "GB",
        admin1: "ENG",
        admin2: "H8",
        population: 484578,
        latitude: "53.41667",
        longitude: "-2.91667",
      },
      // Liverpool (city)
      {
        id: 2644210,
        name: "Liverpool",
        feature_class: "P",
        feature_code: "PPLA2",
        country: "GB",
        admin1: "ENG",
        admin2: "H8",
        population: 864122,
        latitude: "53.41058",
        longitude: "-2.97794",
      },
    ];

    return [
      {
        type: "geonames-2",
        attachment: geonames,
      },
    ];
  }

  /**
   * Returns remote settings records containing geonames alternates (alternate
   * names) populated with some names.
   *
   * @returns {Array}
   *   One or more geonames alternates records for storing in remote settings.
   */
  geonamesAlternatesRecords() {
    return [
      {
        type: "geonames-alternates",
        attachment: [
          {
            language: "abbr",
            alternates_by_geoname_id: [
              [4829764, ["AL"]],
              [4862182, ["IA"]],
              [2635167, ["UK"]],
            ],
          },
        ],
      },
      {
        type: "geonames-alternates",
        attachment: [
          {
            language: "en",
            alternates_by_geoname_id: [
              [
                2635167,
                [
                  {
                    name: "United Kingdom",
                    is_preferred: true,
                    is_short: true,
                  },
                ],
              ],
            ],
          },
        ],
      },
    ];
  }

  /**
   * Returns an expected AMO (addons) result that can be passed to
   * `check_results()` in xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  amoResult({
    source = "rust",
    provider = "Amo",
    title = "Amo Suggestion",
    description = "Amo description",
    url = "https://example.com/amo",
    originalUrl = "https://example.com/amo",
    icon = null,
    setUtmParams = true,
  }) {
    if (setUtmParams) {
      let parsedUrl = new URL(url);
      parsedUrl.searchParams.set("utm_medium", "firefox-desktop");
      parsedUrl.searchParams.set("utm_source", "firefox-suggest");
      url = parsedUrl.href;
    }

    let result = {
      isBestMatch: true,
      suggestedIndex: 1,
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      heuristic: false,
      payload: {
        source,
        provider,
        title,
        description,
        url,
        originalUrl,
        icon,
        displayUrl: url.replace(/^https:\/\//, ""),
        isSponsored: false,
        shouldShowUrl: true,
        bottomTextL10n: { id: "firefox-suggest-addons-recommended" },
        helpUrl: lazy.QuickSuggest.HELP_URL,
        telemetryType: "amo",
      },
    };

    if (source == "rust") {
      result.payload.suggestionObject = new lazy.Suggestion.Amo({
        title,
        url: originalUrl,
        iconUrl: icon,
        description,
        rating: "4.7",
        numberOfRatings: 1,
        guid: "amo-suggestion@example.com",
        score: 0.2,
      });
    }

    return result;
  }

  /**
   * Returns an expected MDN result that can be passed to `check_results()` in
   * xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  mdnResult({ url, title, description }) {
    let finalUrl = new URL(url);
    finalUrl.searchParams.set("utm_medium", "firefox-desktop");
    finalUrl.searchParams.set("utm_source", "firefox-suggest");
    finalUrl.searchParams.set(
      "utm_campaign",
      "firefox-mdn-web-docs-suggestion-experiment"
    );
    finalUrl.searchParams.set("utm_content", "treatment");

    return {
      isBestMatch: true,
      suggestedIndex: 1,
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.OTHER_NETWORK,
      heuristic: false,
      payload: {
        telemetryType: "mdn",
        title,
        url: finalUrl.href,
        originalUrl: url,
        displayUrl: finalUrl.href.replace(/^https:\/\//, ""),
        isSponsored: false,
        description,
        icon: "chrome://global/skin/icons/mdn.svg",
        shouldShowUrl: true,
        bottomTextL10n: { id: "firefox-suggest-mdn-bottom-text" },
        source: "rust",
        provider: "Mdn",
        suggestionObject: new lazy.Suggestion.Mdn({
          title,
          url,
          description,
          score: 0.2,
        }),
      },
    };
  }

  /**
   * Returns an expected Yelp result that can be passed to `check_results()` in
   * xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  yelpResult({
    url,
    title = undefined,
    titleL10n = undefined,
    source = "rust",
    provider = "Yelp",
    isTopPick = false,
    // The default Yelp suggestedIndex is 0, unlike most other Suggest
    // suggestion types, which use -1.
    suggestedIndex = 0,
    isSuggestedIndexRelativeToGroup = true,
    originalUrl = undefined,
    suggestedType = lazy.YelpSubjectType.SERVICE,
  }) {
    const utmParameters = "&utm_medium=partner&utm_source=mozilla";

    originalUrl ??= url;
    originalUrl = new URL(originalUrl);
    originalUrl.searchParams.delete("find_loc");
    originalUrl = originalUrl.toString();

    url += utmParameters;

    if (isTopPick) {
      suggestedIndex = 1;
      isSuggestedIndexRelativeToGroup = false;
    }

    let result = {
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      isBestMatch: !!isTopPick,
      suggestedIndex,
      isSuggestedIndexRelativeToGroup,
      heuristic: false,
      payload: {
        source,
        provider,
        telemetryType: "yelp",
        bottomTextL10n: { id: "firefox-suggest-yelp-bottom-text" },
        url,
        originalUrl,
        title,
        titleL10n,
        icon: null,
        isSponsored: true,
      },
    };

    if (source == "rust") {
      result.payload.suggestionObject = new lazy.Suggestion.Yelp({
        url: originalUrl,
        // `title` will be undefined if the caller passed in `titleL10n`
        // instead, but the Rust suggestion must be created with a string title.
        // The Rust suggestion title doesn't actually matter since no test
        // relies on it directly or indirectly. Pick an arbitrary string, and
        // make it distinctive so it's easier to track down bugs in case it does
        // start to matter at some point.
        title: title ?? "<QuickSuggestTestUtils Yelp suggestion>",
        icon: null,
        iconMimeType: null,
        score: 0.2,
        hasLocationSign: false,
        subjectExactMatch: false,
        subjectType: suggestedType,
        locationParam: "find_loc",
      });
    }

    return result;
  }

  /**
   * Returns an expected weather result that can be passed to `check_results()`
   * in xpcshell tests.
   *
   * @returns {object}
   *   An object that can be passed to `check_results()`.
   */
  weatherResult({
    source = "rust",
    provider = "Weather",
    titleL10n = undefined,
    temperatureUnit = undefined,
  } = {}) {
    if (!temperatureUnit) {
      temperatureUnit =
        Services.locale.regionalPrefsLocales[0] == "en-US" ? "f" : "c";
    }

    if (!titleL10n) {
      titleL10n = {
        id: "urlbar-result-weather-title",
        args: {
          city: lazy.MerinoTestUtils.WEATHER_SUGGESTION.city_name,
          region: lazy.MerinoTestUtils.WEATHER_SUGGESTION.region_code,
        },
      };
    }
    titleL10n = {
      ...titleL10n,
      args: {
        ...titleL10n.args,
        temperature:
          lazy.MerinoTestUtils.WEATHER_SUGGESTION.current_conditions
            .temperature[temperatureUnit],
        unit: temperatureUnit.toUpperCase(),
      },
      parseMarkup: true,
      cacheable: true,
      excludeArgsFromCacheKey: true,
    };

    return {
      type: lazy.UrlbarUtils.RESULT_TYPE.URL,
      source: lazy.UrlbarUtils.RESULT_SOURCE.SEARCH,
      heuristic: false,
      suggestedIndex: 1,
      isRichSuggestion: true,
      richSuggestionIconVariation: "6",
      payload: {
        titleL10n,
        url: lazy.MerinoTestUtils.WEATHER_SUGGESTION.url,
        bottomTextL10n: {
          id: "urlbar-result-weather-provider-sponsored",
          args: { provider: "AccuWeather®" },
          cacheable: true,
        },
        source,
        provider,
        isSponsored: true,
        telemetryType: "weather",
        helpUrl: lazy.QuickSuggest.HELP_URL,
      },
    };
  }

  /**
   * Asserts a result is a quick suggest result.
   *
   * @param {object} options
   *   The options object.
   * @param {string} options.url
   *   The expected URL. At least one of `url` and `originalUrl` must be given.
   * @param {string} options.originalUrl
   *   The expected original URL (the URL with an unreplaced timestamp
   *   template). At least one of `url` and `originalUrl` must be given.
   * @param {object} options.window
   *   The window that should be used for this assertion
   * @param {number} [options.index]
   *   The expected index of the quick suggest result. Pass -1 to use the index
   *   of the last result.
   * @param {boolean} [options.isSponsored]
   *   Whether the result is expected to be sponsored.
   * @param {boolean} [options.isBestMatch]
   *   Whether the result is expected to be a best match.
   * @param {boolean} [options.isManageable]
   *   Whether the result is expected to show Manage result menu item.
   * @param {boolean} [options.hasSponsoredLabel]
   *   Whether the result is expected to show the "Sponsored" label below the
   *   title.
   * @returns {Promise<object>}
   *   The quick suggest result.
   */
  async assertIsQuickSuggest({
    url,
    originalUrl,
    window,
    index = -1,
    isSponsored = true,
    isBestMatch = false,
    isManageable = true,
    hasSponsoredLabel = isSponsored || isBestMatch,
  }) {
    this.Assert.ok(
      url || originalUrl,
      "At least one of url and originalUrl is specified"
    );

    if (index < 0) {
      let resultCount = lazy.UrlbarTestUtils.getResultCount(window);
      if (isBestMatch) {
        index = 1;
        this.Assert.greater(
          resultCount,
          1,
          "Sanity check: Result count should be > 1"
        );
      } else {
        index = resultCount - 1;
        this.Assert.greater(
          resultCount,
          0,
          "Sanity check: Result count should be > 0"
        );
      }
    }

    let details = await lazy.UrlbarTestUtils.getDetailsOfResultAt(
      window,
      index
    );
    let { result } = details;

    this.#log(
      "assertIsQuickSuggest",
      `Checking actual result at index ${index}: ` + JSON.stringify(result)
    );

    this.Assert.equal(
      result.providerName,
      "UrlbarProviderQuickSuggest",
      "Result provider name is UrlbarProviderQuickSuggest"
    );
    this.Assert.equal(details.type, lazy.UrlbarUtils.RESULT_TYPE.URL);
    this.Assert.equal(details.isSponsored, isSponsored, "Result isSponsored");
    if (url) {
      this.Assert.equal(details.url, url, "Result URL");
    }
    if (originalUrl) {
      this.Assert.equal(
        result.payload.originalUrl,
        originalUrl,
        "Result original URL"
      );
    }

    this.Assert.equal(!!result.isBestMatch, isBestMatch, "Result isBestMatch");

    let { row } = details.element;

    let sponsoredElement = row._elements.get("description");
    if (hasSponsoredLabel) {
      this.Assert.ok(sponsoredElement, "Result sponsored label element exists");
      this.Assert.equal(
        sponsoredElement.textContent,
        isSponsored ? "Sponsored" : "",
        "Result sponsored label"
      );
    } else {
      this.Assert.ok(
        !sponsoredElement?.textContent,
        "Result sponsored label element should not exist"
      );
    }

    this.Assert.equal(
      result.payload.isManageable,
      isManageable,
      "Result isManageable"
    );

    if (!isManageable) {
      this.Assert.equal(
        result.payload.helpUrl,
        lazy.QuickSuggest.HELP_URL,
        "Result helpURL"
      );
    }

    this.Assert.ok(
      row._buttons.get("result-menu"),
      "The menu button should be present"
    );

    return details;
  }

  /**
   * Asserts a result is not a quick suggest result.
   *
   * @param {object} window
   *   The window that should be used for this assertion
   * @param {number} index
   *   The index of the result.
   */
  async assertIsNotQuickSuggest(window, index) {
    let details = await lazy.UrlbarTestUtils.getDetailsOfResultAt(
      window,
      index
    );
    this.Assert.notEqual(
      details.result.providerName,
      "UrlbarProviderQuickSuggest",
      `Result at index ${index} is not provided by UrlbarProviderQuickSuggest`
    );
  }

  /**
   * Asserts that none of the results are quick suggest results.
   *
   * @param {object} window
   *   The window that should be used for this assertion
   */
  async assertNoQuickSuggestResults(window) {
    for (let i = 0; i < lazy.UrlbarTestUtils.getResultCount(window); i++) {
      await this.assertIsNotQuickSuggest(window, i);
    }
  }

  /**
   * Asserts that URLs in a result's payload have the timestamp template
   * substring replaced with real timestamps.
   *
   * @param {UrlbarResult} result The results to check
   * @param {object} urls
   *   An object that contains the expected payload properties with template
   *   substrings. For example:
   *   ```js
   *   {
   *     url: "https://example.com/foo-%YYYYMMDDHH%",
   *     sponsoredClickUrl: "https://example.com/bar-%YYYYMMDDHH%",
   *   }
   *   ```
   */
  assertTimestampsReplaced(result, urls) {
    let { TIMESTAMP_TEMPLATE, TIMESTAMP_LENGTH } = lazy.AmpSuggestions;

    // Parse the timestamp strings from each payload property and save them in
    // `urls[key].timestamp`.
    urls = { ...urls };
    for (let [key, url] of Object.entries(urls)) {
      let index = url.indexOf(TIMESTAMP_TEMPLATE);
      this.Assert.ok(
        index >= 0,
        `Timestamp template ${TIMESTAMP_TEMPLATE} is in URL ${url} for key ${key}`
      );
      let value = result.payload[key];
      this.Assert.ok(value, "Key is in result payload: " + key);
      let timestamp = value.substring(index, index + TIMESTAMP_LENGTH);

      // Set `urls[key]` to an object that's helpful in the logged info message
      // below.
      urls[key] = { url, value, timestamp };
    }

    this.#log(
      "assertTimestampsReplaced",
      "Parsed timestamps: " + JSON.stringify(urls)
    );

    // Make a set of unique timestamp strings. There should only be one.
    let { timestamp } = Object.values(urls)[0];
    this.Assert.deepEqual(
      [...new Set(Object.values(urls).map(o => o.timestamp))],
      [timestamp],
      "There's only one unique timestamp string"
    );

    // Parse the parts of the timestamp string.
    let year = timestamp.slice(0, -6);
    let month = timestamp.slice(-6, -4);
    let day = timestamp.slice(-4, -2);
    let hour = timestamp.slice(-2);
    let date = new Date(year, month - 1, day, hour);

    // The timestamp should be no more than two hours in the past. Typically it
    // will be the same as the current hour, but since its resolution is in
    // terms of hours and it's possible the test may have crossed over into a
    // new hour as it was running, allow for the previous hour.
    this.Assert.less(
      Date.now() - 2 * 60 * 60 * 1000,
      date.getTime(),
      "Timestamp is within the past two hours"
    );
  }

  /**
   * Calls a callback while enrolled in a mock Nimbus experiment. The experiment
   * is automatically unenrolled and cleaned up after the callback returns.
   *
   * @param {object} options
   *   Options for the mock experiment.
   * @param {Function} options.callback
   *   The callback to call while enrolled in the mock experiment.
   * @param {object} options.valueOverrides
   *   Values for feature variables.
   */
  async withExperiment({ callback, ...options }) {
    let doExperimentCleanup = await this.enrollExperiment(options);
    await callback();
    await doExperimentCleanup();
  }

  /**
   * Enrolls in a mock Nimbus experiment.
   *
   * @param {object} options
   *   Options for the mock experiment.
   * @param {object} [options.valueOverrides]
   *   Values for feature variables.
   * @returns {Promise<Function>}
   *   The experiment cleanup function (async).
   */
  async enrollExperiment({ valueOverrides = {} }) {
    this.#log("enrollExperiment", "Awaiting ExperimentAPI.ready");
    await lazy.ExperimentAPI.ready();

    let doExperimentCleanup =
      await lazy.NimbusTestUtils.enrollWithFeatureConfig({
        enabled: true,
        featureId: "urlbar",
        value: valueOverrides,
      });

    return async () => {
      this.#log("enrollExperiment.cleanup", "Awaiting experiment cleanup");
      await doExperimentCleanup();
    };
  }

  /**
   * Sets the app's home region and locales, calls your callback, and resets
   * the region and locales.
   *
   * @param {object} options
   *   Options object.
   * @param {Array} options.locales
   *   An array of locale strings. The entire array will be set as the available
   *   locales, and the first locale in the array will be set as the requested
   *   locale.
   * @param {Function} options.callback
   *  The callback to be called with the {@link locales} set. This function can
   *  be async.
   * @param {string} options.homeRegion
   *   The home region to set, an all-caps country code, e.g., "US", "CA", "DE".
   *   Leave undefined to skip setting a region.
   */
  async withLocales({ locales, callback, homeRegion = undefined }) {
    let promiseChanges = async desiredLocales => {
      this.#log(
        "withLocales",
        "Changing locales from " +
          JSON.stringify(Services.locale.requestedLocales) +
          " to " +
          JSON.stringify(desiredLocales)
      );

      if (desiredLocales[0] == Services.locale.requestedLocales[0]) {
        // Nothing happens when the locale doesn't actually change.
        this.#log("withLocales", "Locale is already " + desiredLocales[0]);
        return;
      }

      this.#log("withLocales", "Waiting for intl:requested-locales-changed");
      await lazy.TestUtils.topicObserved("intl:requested-locales-changed");
      this.#log("withLocales", "Observed intl:requested-locales-changed");

      // Wait for the search service to reload engines. Otherwise tests can fail
      // in strange ways due to internal search service state during shutdown.
      // It won't always reload engines but it's hard to tell in advance when it
      // won't, so also set a timeout.
      this.#log("withLocales", "Waiting for TOPIC_SEARCH_SERVICE");
      await Promise.race([
        lazy.TestUtils.topicObserved(
          lazy.SearchUtils.TOPIC_SEARCH_SERVICE,
          (subject, data) => {
            this.#log(
              "withLocales",
              "Observed TOPIC_SEARCH_SERVICE with data: " + data
            );
            return data == "engines-reloaded";
          }
        ),
        new Promise(resolve => {
          lazy.setTimeout(() => {
            this.#log(
              "withLocales",
              "Timed out waiting for TOPIC_SEARCH_SERVICE"
            );
            resolve();
          }, 2000);
        }),
      ]);

      this.#log("withLocales", "Done waiting for locale changes");
    };

    let originalHome = lazy.Region.home;
    if (homeRegion) {
      lazy.Region._setHomeRegion(homeRegion, true);
    }

    let available = Services.locale.availableLocales;
    let requested = Services.locale.requestedLocales;

    let newRequested = locales.slice(0, 1);
    let promise = promiseChanges(newRequested);
    Services.locale.availableLocales = locales;
    Services.locale.requestedLocales = newRequested;
    await promise;

    this.Assert.equal(
      Services.locale.appLocaleAsBCP47,
      locales[0],
      "App locale is now " + locales[0]
    );

    await callback();

    if (homeRegion) {
      lazy.Region._setHomeRegion(originalHome, true);
    }

    promise = promiseChanges(requested);
    Services.locale.availableLocales = available;
    Services.locale.requestedLocales = requested;
    await promise;
  }

  #log(fnName, msg) {
    this.info?.(`QuickSuggestTestUtils.${fnName} ${msg}`);
  }

  #recordsByCollection(records) {
    // Make a Map from collection name to the array of records that should be
    // added to that collection.
    let recordsByCollection = records.reduce((memo, record) => {
      let collection = record.collection || this.RS_COLLECTION.OTHER;
      let recs = memo.get(collection);
      if (!recs) {
        recs = [];
        memo.set(collection, recs);
      }
      recs.push(record);
      return memo;
    }, new Map());

    // Make sure the two main collections, "quicksuggest-amp" and
    // "quicksuggest-other", are present. Otherwise the Rust component will log
    // 404 errors because it expects them to exist. The errors are harmless but
    // annoying.
    for (let collection of Object.values(this.RS_COLLECTION)) {
      if (!recordsByCollection.has(collection)) {
        recordsByCollection.set(collection, []);
      }
    }

    return recordsByCollection;
  }

  #remoteSettingsServer;
}

export var QuickSuggestTestUtils = new _QuickSuggestTestUtils();
