/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports a provider that offers search history suggestions
 * based on embeddings and semantic search techniques using semantic
 * history
 */

import {
  UrlbarProvider,
  UrlbarUtils,
} from "resource:///modules/UrlbarUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  EnrollmentType: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProviderOpenTabs: "resource:///modules/UrlbarProviderOpenTabs.sys.mjs",
  UrlbarResult: "resource:///modules/UrlbarResult.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", function () {
  return UrlbarUtils.getLogger({ prefix: "SemanticHistorySearch" });
});

/**
 * Lazily creates (on first call) and returns the
 * {@link PlacesSemanticHistoryManager} instance backing this provider.
 */
ChromeUtils.defineLazyGetter(lazy, "semanticManager", function () {
  let { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
    "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
  );
  const distanceThreshold = Services.prefs.getFloatPref(
    "places.semanticHistory.distanceThreshold",
    0.6
  );
  return getPlacesSemanticHistoryManager({
    embeddingSize: 384,
    rowLimit: 10000,
    samplingAttrib: "frecency",
    changeThresholdCount: 3,
    distanceThreshold,
  });
});

/**
 * @typedef {ReturnType<import("resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs").getPlacesSemanticHistoryManager>} PlacesSemanticHistoryManager
 */

/**
 * Class representing the Semantic History Search provider for the URL bar.
 *
 * This provider queries a semantic database created using history.
 * It performs semantic search using embeddings generated
 * by an ML model and retrieves results ranked by cosine similarity to the
 * query's embedding.
 *
 * @class
 */
export class UrlbarProviderSemanticHistorySearch extends UrlbarProvider {
  /** @type {boolean} */
  static #exposureRecorded;

  /**
   * Provides a shared instance of the semantic manager, so that other consumers
   * won't wrongly initialize it with different parameters.
   *
   * @returns {PlacesSemanticHistoryManager}
   *   The semantic manager instance used by this provider.
   */
  static get semanticManager() {
    return lazy.semanticManager;
  }

  /**
   * @returns {Values<typeof UrlbarUtils.PROVIDER_TYPE>}
   */
  get type() {
    return UrlbarUtils.PROVIDER_TYPE.PROFILE;
  }

  /**
   * Determines if the provider is active for the given query context.
   *
   * @param {object} queryContext
   *   The context of the query, including the search string.
   */
  async isActive(queryContext) {
    const minSearchStringLength = lazy.UrlbarPrefs.get(
      "suggest.semanticHistory.minLength"
    );
    if (
      lazy.UrlbarPrefs.get("suggest.history") &&
      queryContext.searchString.length >= minSearchStringLength &&
      (!queryContext.searchMode ||
        queryContext.searchMode.source == UrlbarUtils.RESULT_SOURCE.HISTORY)
    ) {
      if (lazy.semanticManager.canUseSemanticSearch) {
        // Proceed only if a sufficient number of history entries have
        // embeddings calculated.
        return lazy.semanticManager.hasSufficientEntriesForSearching();
      }
    }
    return false;
  }

  /**
   * Starts a semantic search query.
   *
   * @param {object} queryContext
   *   The query context, including the search string.
   * @param {Function} addCallback
   *   Callback to add results to the URL bar.
   */
  async startQuery(queryContext, addCallback) {
    let instance = this.queryInstance;
    let resultObject = await lazy.semanticManager.infer(queryContext);
    this.#maybeRecordExposure();
    let results = resultObject.results;
    if (!results || instance != this.queryInstance) {
      return;
    }

    let openTabs = lazy.UrlbarProviderOpenTabs.getOpenTabUrls(
      queryContext.isPrivate
    );
    for (let res of results) {
      if (
        !this.#addAsSwitchToTab(
          openTabs.get(res.url),
          queryContext,
          res,
          addCallback
        )
      ) {
        const result = new lazy.UrlbarResult(
          UrlbarUtils.RESULT_TYPE.URL,
          UrlbarUtils.RESULT_SOURCE.HISTORY,
          ...lazy.UrlbarResult.payloadAndSimpleHighlights(queryContext.tokens, {
            title: [res.title, UrlbarUtils.HIGHLIGHT.NONE],
            url: [res.url, UrlbarUtils.HIGHLIGHT.NONE],
            icon: UrlbarUtils.getIconForUrl(res.url),
            isBlockable: true,
            blockL10n: { id: "urlbar-result-menu-remove-from-history" },
            helpUrl:
              Services.urlFormatter.formatURLPref("app.support.baseURL") +
              "awesome-bar-result-menu",
            frecency: res.frecency,
          })
        );
        addCallback(this, result);
      }
    }
  }

  /**
   * Check if the url is open in tabs, and adds one or multiple switch to tab
   * results if so.
   *
   * @param {Set<[number, string]>|undefined} openTabs
   *  Tabs open for the result URL, may be undefined.
   * @param {object} queryContext
   *  The query context, including the search string.
   * @param {object} res
   * The result object containing the URL.
   * @param {Function} addCallback
   *  Callback to add results to the URL bar.
   * @returns {boolean} True if a switch to tab result was added.
   */
  #addAsSwitchToTab(openTabs, queryContext, res, addCallback) {
    if (!openTabs?.size) {
      return false;
    }

    let userContextId =
      lazy.UrlbarProviderOpenTabs.getUserContextIdForOpenPagesTable(
        queryContext.userContextId,
        queryContext.isPrivate
      );

    let added = false;
    for (let [tabUserContextId, tabGroupId] of openTabs) {
      // Don't return a switch to tab result for the current page.
      if (
        res.url == queryContext.currentPage &&
        userContextId == tabUserContextId &&
        queryContext.tabGroup === tabGroupId
      ) {
        continue;
      }
      // Respect the switchTabs.searchAllContainers pref.
      if (
        !lazy.UrlbarPrefs.get("switchTabs.searchAllContainers") &&
        tabUserContextId != userContextId
      ) {
        continue;
      }
      let payload = lazy.UrlbarResult.payloadAndSimpleHighlights(
        queryContext.tokens,
        {
          url: [res.url, UrlbarUtils.HIGHLIGHT.NONE],
          title: [res.title, UrlbarUtils.HIGHLIGHT.NONE],
          icon: UrlbarUtils.getIconForUrl(res.url),
          userContextId: tabUserContextId,
          tabGroup: tabGroupId,
          lastVisit: res.lastVisit,
        }
      );
      if (lazy.UrlbarPrefs.get("secondaryActions.switchToTab")) {
        payload[0].action =
          UrlbarUtils.createTabSwitchSecondaryAction(tabUserContextId);
      }
      let result = new lazy.UrlbarResult(
        UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
        UrlbarUtils.RESULT_SOURCE.TABS,
        ...payload
      );
      addCallback(this, result);
      added = true;
    }
    return added;
  }

  /**
   * Records an exposure event for the semantic-history feature-gate, but
   * **only once per profile**.  Subsequent calls are ignored.
   */
  #maybeRecordExposure() {
    // Skip if we already recorded or if the gate is manually turned off.
    if (UrlbarProviderSemanticHistorySearch.#exposureRecorded) {
      return;
    }

    // Look up our enrollment (experiment or rollout). If no slug, we’re not enrolled.
    let metadata =
      lazy.NimbusFeatures.urlbar.getEnrollmentMetadata(
        lazy.EnrollmentType.EXPERIMENT
      ) ||
      lazy.NimbusFeatures.urlbar.getEnrollmentMetadata(
        lazy.EnrollmentType.ROLLOUT
      );
    if (!metadata?.slug) {
      // Not part of any semantic-history experiment/rollout → nothing to record
      return;
    }

    try {
      // Actually send it once with the slug.
      lazy.NimbusFeatures.urlbar.recordExposureEvent({
        once: true,
        slug: metadata.slug,
      });
      UrlbarProviderSemanticHistorySearch.#exposureRecorded = true;
      lazy.logger.debug(
        `Nimbus exposure event sent (semanticHistory: ${metadata.slug}).`
      );
    } catch (ex) {
      lazy.logger.warn("Unable to record semantic-history exposure event:", ex);
    }
  }

  /**
   * Gets the priority of this provider relative to other providers.
   *
   * @returns {number} The priority of this provider.
   */
  getPriority() {
    return 0;
  }

  onEngagement(queryContext, controller, details) {
    let { result } = details;
    if (details.selType == "dismiss") {
      // Remove browsing history entries from Places.
      lazy.PlacesUtils.history.remove(result.payload.url).catch(console.error);
      controller.removeResult(result);
    }
  }
}
