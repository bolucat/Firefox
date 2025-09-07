/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SearchSERPTelemetry:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  UrlbarSearchUtils: "resource:///modules/UrlbarSearchUtils.sys.mjs",
});

/**
 * @import {SearchEngine} from "moz-src:///toolkit/components/search/SearchEngine.sys.mjs"
 */

/**
 * This class handles saving search telemetry related to the url bar,
 * search bar and other areas as per the sources above.
 */
class BrowserSearchTelemetryHandler {
  /**
   * A map of known search origins. The values of this map should be used for all
   * current telemetry, except for sap.deprecatedCounts.
   *
   * The keys of this map are used in the calling code to recordSearch, and in
   * the sap.deprecatedCounts labelled counter (and the mirrored SEARCH_COUNTS
   * histogram).
   *
   * When legacy telemetry stops being reported, we should remove this map, and
   * update the callers to use the values directly. We might still want to keep
   * a list of valid sources, to help ensure that telemetry reporting is updated
   * correctly if new sources are added.
   */
  KNOWN_SEARCH_SOURCES = new Map([
    ["abouthome", "about_home"],
    ["contextmenu", "contextmenu"],
    ["contextmenu_visual", "contextmenu_visual"],
    ["newtab", "about_newtab"],
    ["searchbar", "searchbar"],
    ["system", "system"],
    ["urlbar", "urlbar"],
    ["urlbar-handoff", "urlbar_handoff"],
    ["urlbar-persisted", "urlbar_persisted"],
    ["urlbar-searchmode", "urlbar_searchmode"],
    ["webextension", "webextension"],
  ]);

  /**
   * Determines if we should record a search for this browser instance.
   * Private Browsing mode is normally skipped.
   *
   * @param {XULBrowserElement} browser
   *   The browser where the search was loaded.
   * @returns {boolean}
   *   True if the search should be recorded, false otherwise.
   */
  shouldRecordSearchCount(browser) {
    return (
      !lazy.PrivateBrowsingUtils.isWindowPrivate(browser.ownerGlobal) ||
      !Services.prefs.getBoolPref("browser.engagement.search_counts.pbm", false)
    );
  }

  /**
   * Records the method by which the user selected a result from the searchbar.
   *
   * @param {Event} event
   *        The event that triggered the selection.
   * @param {number} index
   *        The index that the user chose in the popup, or -1 if there wasn't a
   *        selection.
   */
  recordSearchSuggestionSelectionMethod(event, index) {
    // command events are from the one-off context menu. Treat them as clicks.
    // Note that we only care about MouseEvent subclasses here when the
    // event type is "click", or else the subclasses are associated with
    // non-click interactions.
    let isClick =
      event &&
      (ChromeUtils.getClassName(event) == "MouseEvent" ||
        event.type == "click" ||
        event.type == "command");
    let category;
    if (isClick) {
      category = "click";
    } else if (index >= 0) {
      category = "enterSelection";
    } else {
      category = "enter";
    }

    Glean.searchbar.selectedResultMethod[category].add(1);
  }

  /**
   * Records entry into the Urlbar's search mode.
   *
   * Telemetry records only which search mode is entered and how it was entered.
   * It does not record anything pertaining to searches made within search mode.
   *
   * @param {object} searchMode
   *   A search mode object. See UrlbarInput.setSearchMode documentation for
   *   details.
   */
  recordSearchMode(searchMode) {
    // Search mode preview is not search mode. Recording it would just create
    // noise.
    if (searchMode.isPreview) {
      return;
    }

    let label = lazy.UrlbarSearchUtils.getSearchModeScalarKey(searchMode);
    let name = searchMode.entry.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
    Glean.urlbarSearchmode[name]?.[label].add(1);
  }

  /**
   * The main entry point for recording search related Telemetry. This includes
   * search counts and engagement measurements.
   *
   * Telemetry records only search counts per engine and action origin, but
   * nothing pertaining to the search contents themselves.
   *
   * @param {XULBrowserElement} browser
   *        The browser where the search originated.
   * @param {nsISearchEngine} engine
   *        The engine handling the search.
   * @param {string} source
   *        Where the search originated from. See KNOWN_SEARCH_SOURCES for allowed
   *        values.
   * @param {object} [details] Options object.
   * @param {boolean} [details.isOneOff=false]
   *        true if this event was generated by a one-off search.
   * @param {boolean} [details.isSuggestion=false]
   *        true if this event was generated by a suggested search.
   * @param {boolean} [details.isFormHistory=false]
   *        true if this event was generated by a form history result.
   * @param {string} [details.alias=null]
   *        The search engine alias used in the search, if any.
   * @param {string} [details.newtabSessionId=undefined]
   *        The newtab session that prompted this search, if any.
   * @param {string} [details.searchUrlType=undefined]
   *        A `SearchUtils.URL_TYPE` value that indicates the type of search.
   *        Defaults to `SearchUtils.URL_TYPE.SEARCH`, a plain old search.
   * @throws if source is not in the known sources list.
   */
  recordSearch(browser, engine, source, details = {}) {
    if (engine.clickUrl) {
      this.#reportSearchInGlean(engine.clickUrl);
    }

    try {
      if (!this.shouldRecordSearchCount(browser)) {
        return;
      }
      if (!this.KNOWN_SEARCH_SOURCES.has(source)) {
        console.error("Unknown source for search: ", source);
        return;
      }

      if (source != "contextmenu_visual") {
        const countIdPrefix = `${engine.telemetryId}.`;
        const countIdSource = countIdPrefix + source;

        // NOTE: When removing the sap.deprecatedCounts telemetry, see the note
        // above KNOWN_SEARCH_SOURCES.
        if (
          details.alias &&
          engine.isConfigEngine &&
          engine.aliases.includes(details.alias)
        ) {
          // This is a keyword search using a config engine.
          // Record the source as "alias", not "urlbar".
          Glean.sap.deprecatedCounts[countIdPrefix + "alias"].add();
        } else {
          Glean.sap.deprecatedCounts[countIdSource].add();
        }
      }

      // When an engine is overridden by a third party, then we report the
      // override and skip reporting the partner code, since we don't have
      // a requirement to report the partner code in that case.
      let isOverridden = !!engine.overriddenById;

      let searchUrlType =
        details.searchUrlType ?? lazy.SearchUtils.URL_TYPE.SEARCH;

      let unwrappedEngine = /** @type {SearchEngine} */ (
        engine.wrappedJSObject
      );

      // Strict equality is used because we want to only match against the
      // empty string and not other values. We would have `engine.partnerCode`
      // return `undefined`, but the XPCOM interfaces force us to return an
      // empty string.
      let reportPartnerCode =
        !isOverridden &&
        engine.partnerCode !== "" &&
        !unwrappedEngine.getURLOfType(searchUrlType)
          ?.excludePartnerCodeFromTelemetry;

      Glean.sap.counts.record({
        source: this.KNOWN_SEARCH_SOURCES.get(source),
        provider_id: engine.isConfigEngine ? engine.id : "other",
        provider_name: engine.name,
        // If no code is reported, we must returned undefined, Glean will then
        // not report the field.
        partner_code: reportPartnerCode ? engine.partnerCode : undefined,
        overridden_by_third_party: isOverridden.toString(),
      });

      // Dispatch the search signal to other handlers.
      switch (source) {
        case "urlbar":
        case "searchbar":
        case "urlbar-searchmode":
        case "urlbar-persisted":
        case "urlbar-handoff":
          this._handleSearchAndUrlbar(browser, engine, source, details);
          break;
        case "abouthome":
        case "newtab":
          this._recordSearch(browser, engine, source, "enter");
          break;
        default:
          this._recordSearch(browser, engine, source);
          break;
      }
      if (["urlbar-handoff", "abouthome", "newtab"].includes(source)) {
        Glean.newtabSearch.issued.record({
          newtab_visit_id: details.newtabSessionId,
          search_access_point: this.KNOWN_SEARCH_SOURCES.get(source),
          telemetry_id: engine.telemetryId,
        });
        lazy.SearchSERPTelemetry.recordBrowserNewtabSession(
          browser,
          details.newtabSessionId
        );
      }
    } catch (ex) {
      // Catch any errors here, so that search actions are not broken if
      // telemetry is broken for some reason.
      console.error(ex);
    }
  }

  /**
   * Records visits to a search engine's search form.
   *
   * @param {nsISearchEngine} engine
   *   The engine whose search form is being visited.
   * @param {string} source
   *   Where the search form was opened from.
   *   This can be "urlbar" or "searchbar".
   */
  recordSearchForm(engine, source) {
    Glean.sap.searchFormCounts.record({
      source,
      provider_id: engine.isConfigEngine ? engine.id : "other",
    });
  }

  /**
   * Records an impression of a search access point.
   *
   * @param {XULBrowserElement} browser
   *   The browser associated with the SAP.
   * @param {nsISearchEngine|null} engine
   *   The engine handling the search, or null if this doesn't apply to the SAP
   *   (e.g., the engine isn't known or selected yet). The counter's label will
   *   be `engine.id` if `engine` is a non-null, app-provided engine. Otherwise
   *   the label will be "none".
   * @param {string} source
   *   The name of the SAP. See `KNOWN_SEARCH_SOURCES` for allowed values.
   */
  recordSapImpression(browser, engine, source) {
    if (!this.shouldRecordSearchCount(browser)) {
      return;
    }
    if (!this.KNOWN_SEARCH_SOURCES.has(source)) {
      console.error("Unknown source for SAP impression:", source);
      return;
    }

    let scalarSource = this.KNOWN_SEARCH_SOURCES.get(source);
    let name = scalarSource.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
    let label = engine?.isConfigEngine ? engine.id : "none";
    Glean.sapImpressionCounts[name][label].add(1);
  }

  /**
   * This function handles the "urlbar", "urlbar-oneoff", "searchbar" and
   * "searchbar-oneoff" sources.
   *
   * @param {XULBrowserElement} browser
   *   The browser where the search originated.
   * @param {nsISearchEngine} engine
   *   The engine handling the search.
   * @param {string} source
   *   Where the search originated from.
   * @param {object} details
   *   See {@link BrowserSearchTelemetryHandler.recordSearch}
   */
  _handleSearchAndUrlbar(browser, engine, source, details) {
    const isOneOff = !!details.isOneOff;
    let action = "enter";
    if (isOneOff) {
      action = "oneoff";
    } else if (details.isFormHistory) {
      action = "formhistory";
    } else if (details.isSuggestion) {
      action = "suggestion";
    } else if (details.alias) {
      action = "alias";
    }

    this._recordSearch(browser, engine, source, action);
  }

  _recordSearch(browser, engine, source, action = null) {
    let scalarSource = this.KNOWN_SEARCH_SOURCES.get(source);
    lazy.SearchSERPTelemetry.recordBrowserSource(browser, scalarSource);

    let label = action ? "search_" + action : "search";
    let name = scalarSource.replace(/_([a-z])/g, (m, p) => p.toUpperCase());
    Glean.browserEngagementNavigation[name][label].add(1);
  }

  /**
   * Records the search in Glean for contextual services.
   *
   * @param {string} reportingUrl
   *   The url to be sent to contextual services.
   */
  async #reportSearchInGlean(reportingUrl) {
    let defaultValuesByGleanKey = {
      contextId: await lazy.ContextId.request(),
    };

    let sendGleanPing = valuesByGleanKey => {
      valuesByGleanKey = { ...defaultValuesByGleanKey, ...valuesByGleanKey };
      for (let [gleanKey, value] of Object.entries(valuesByGleanKey)) {
        let glean = Glean.searchWith[gleanKey];
        if (value !== undefined && value !== "") {
          glean.set(value);
        }
      }
      GleanPings.searchWith.submit();
    };

    sendGleanPing({
      reportingUrl,
    });
  }
}

export var BrowserSearchTelemetry = new BrowserSearchTelemetryHandler();
