/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  Interactions: "moz-src:///browser/components/places/Interactions.sys.mjs",
  ProviderSemanticHistorySearch:
    "resource:///modules/UrlbarProviderSemanticHistorySearch.sys.mjs",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.sys.mjs",
  UrlbarProvidersManager: "resource:///modules/UrlbarProvidersManager.sys.mjs",
  UrlbarTokenizer: "resource:///modules/UrlbarTokenizer.sys.mjs",
  UrlbarUtils: "resource:///modules/UrlbarUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.UrlbarUtils.getLogger({ prefix: "Controller" })
);

const NOTIFICATIONS = {
  QUERY_STARTED: "onQueryStarted",
  QUERY_RESULTS: "onQueryResults",
  QUERY_RESULT_REMOVED: "onQueryResultRemoved",
  QUERY_CANCELLED: "onQueryCancelled",
  QUERY_FINISHED: "onQueryFinished",
  VIEW_OPEN: "onViewOpen",
  VIEW_CLOSE: "onViewClose",
};

/**
 * The address bar controller handles queries from the address bar, obtains
 * results and returns them to the UI for display.
 *
 * Listeners may be added to listen for the results. They may support the
 * following methods which may be called when a query is run:
 *
 * - onQueryStarted(queryContext)
 * - onQueryResults(queryContext)
 * - onQueryCancelled(queryContext)
 * - onQueryFinished(queryContext)
 * - onQueryResultRemoved(index)
 * - onViewOpen()
 * - onViewClose()
 */
export class UrlbarController {
  /**
   * Initialises the class. The manager may be overridden here, this is for
   * test purposes.
   *
   * @param {object} options
   *   The initial options for UrlbarController.
   * @param {UrlbarInput} options.input
   *   The input this controller is operating with.
   * @param {object} [options.manager]
   *   Optional fake providers manager to override the built-in providers manager.
   *   Intended for use in unit tests only.
   */
  constructor(options = {}) {
    if (!options.input) {
      throw new Error("Missing options: input");
    }
    if (!options.input.window) {
      throw new Error("input is missing 'window' property.");
    }
    if (
      !options.input.window.location ||
      options.input.window.location.href != AppConstants.BROWSER_CHROME_URL
    ) {
      throw new Error("input.window should be an actual browser window.");
    }
    if (!("isPrivate" in options.input)) {
      throw new Error("input.isPrivate must be set.");
    }

    this.input = options.input;
    this.browserWindow = options.input.window;

    this.manager = options.manager || lazy.UrlbarProvidersManager;

    this._listeners = new Set();
    this._userSelectionBehavior = "none";

    this.engagementEvent = new TelemetryEvent(
      this,
      options.eventTelemetryCategory
    );
  }

  get NOTIFICATIONS() {
    return NOTIFICATIONS;
  }

  /**
   * Hooks up the controller with a view.
   *
   * @param {UrlbarView} view
   *   The UrlbarView instance associated with this controller.
   */
  setView(view) {
    this.view = view;
  }

  /**
   * Takes a query context and starts the query based on the user input.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   * @returns {Promise<UrlbarQueryContext>}
   *   The updated query context.
   */
  async startQuery(queryContext) {
    // Cancel any running query.
    this.cancelQuery();

    // Wrap the external queryContext, to track a unique object, in case
    // the external consumer reuses the same context multiple times.
    // This also allows to add properties without polluting the context.
    // Note this can't be null-ed or deleted once a query is done, because it's
    // used by #dismissSelectedResult and handleKeyNavigation, that can run after
    // a query is cancelled or finished.
    let contextWrapper = (this._lastQueryContextWrapper = { queryContext });

    queryContext.lastResultCount = 0;
    queryContext.firstTimerId =
      Glean.urlbar.autocompleteFirstResultTime.start();
    queryContext.sixthTimerId =
      Glean.urlbar.autocompleteSixthResultTime.start();

    // For proper functionality we must ensure this notification is fired
    // synchronously, as soon as startQuery is invoked, but after any
    // notifications related to the previous query.
    this.notify(NOTIFICATIONS.QUERY_STARTED, queryContext);
    await this.manager.startQuery(queryContext, this);

    // If the query has been cancelled, onQueryFinished was notified already.
    // Note this._lastQueryContextWrapper may have changed in the meanwhile.
    if (
      contextWrapper === this._lastQueryContextWrapper &&
      !contextWrapper.done
    ) {
      contextWrapper.done = true;
      // TODO (Bug 1549936) this is necessary to avoid leaks in PB tests.
      this.manager.cancelQuery(queryContext);
      this.notify(NOTIFICATIONS.QUERY_FINISHED, queryContext);
    }

    return queryContext;
  }

  /**
   * Cancels an in-progress query. Note, queries may continue running if they
   * can't be cancelled.
   */
  cancelQuery() {
    // If the query finished already, don't handle cancel.
    if (!this._lastQueryContextWrapper || this._lastQueryContextWrapper.done) {
      return;
    }

    this._lastQueryContextWrapper.done = true;

    let { queryContext } = this._lastQueryContextWrapper;

    Glean.urlbar.autocompleteFirstResultTime.cancel(queryContext.firstTimerId);
    queryContext.firstTimerId = 0;
    Glean.urlbar.autocompleteSixthResultTime.cancel(queryContext.sixthTimerId);
    queryContext.sixthTimerId = 0;

    this.manager.cancelQuery(queryContext);
    this.notify(NOTIFICATIONS.QUERY_CANCELLED, queryContext);
    this.notify(NOTIFICATIONS.QUERY_FINISHED, queryContext);
  }

  /**
   * Receives results from a query.
   *
   * @param {UrlbarQueryContext} queryContext The query details.
   */
  receiveResults(queryContext) {
    if (queryContext.lastResultCount < 1 && queryContext.results.length >= 1) {
      Glean.urlbar.autocompleteFirstResultTime.stopAndAccumulate(
        queryContext.firstTimerId
      );
      queryContext.firstTimerId = 0;
    }
    if (queryContext.lastResultCount < 6 && queryContext.results.length >= 6) {
      Glean.urlbar.autocompleteSixthResultTime.stopAndAccumulate(
        queryContext.sixthTimerId
      );
      queryContext.sixthTimerId = 0;
    }

    if (queryContext.firstResultChanged) {
      // Notify the input so it can make adjustments based on the first result.
      if (this.input.onFirstResult(queryContext.results[0])) {
        // The input canceled the query and started a new one.
        return;
      }

      // The first time we receive results try to connect to the heuristic
      // result.
      this.speculativeConnect(
        queryContext.results[0],
        queryContext,
        "resultsadded"
      );
    }

    this.notify(NOTIFICATIONS.QUERY_RESULTS, queryContext);
    // Update lastResultCount after notifying, so the view can use it.
    queryContext.lastResultCount = queryContext.results.length;
  }

  /**
   * Adds a listener for Urlbar result notifications.
   *
   * @param {object} listener The listener to add.
   * @throws {TypeError} Throws if the listener is not an object.
   */
  addListener(listener) {
    if (!listener || typeof listener != "object") {
      throw new TypeError("Expected listener to be an object");
    }
    this._listeners.add(listener);
  }

  /**
   * Removes a listener for Urlbar result notifications.
   *
   * @param {object} listener The listener to remove.
   */
  removeListener(listener) {
    this._listeners.delete(listener);
  }

  /**
   * Checks whether a keyboard event that would normally open the view should
   * instead be handled natively by the input field.
   * On certain platforms, the up and down keys can be used to move the caret,
   * in which case we only want to open the view if the caret is at the
   * start or end of the input.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   * @returns {boolean}
   *   Returns true if the event should move the caret instead of opening the
   *   view.
   */
  keyEventMovesCaret(event) {
    if (this.view.isOpen) {
      return false;
    }
    if (AppConstants.platform != "macosx" && AppConstants.platform != "linux") {
      return false;
    }
    let isArrowUp = event.keyCode == KeyEvent.DOM_VK_UP;
    let isArrowDown = event.keyCode == KeyEvent.DOM_VK_DOWN;
    if (!isArrowUp && !isArrowDown) {
      return false;
    }
    let start = this.input.selectionStart;
    let end = this.input.selectionEnd;
    if (
      end != start ||
      (isArrowUp && start > 0) ||
      (isArrowDown && end < this.input.value.length)
    ) {
      return true;
    }
    return false;
  }

  /**
   * Receives keyboard events from the input and handles those that should
   * navigate within the view or pick the currently selected item.
   *
   * @param {KeyboardEvent} event
   *   The DOM KeyboardEvent.
   * @param {boolean} executeAction
   *   Whether the event should actually execute the associated action, or just
   *   be managed (at a preventDefault() level). This is used when the event
   *   will be deferred by the event bufferer, but preventDefault() and friends
   *   should still happen synchronously.
   */
  // eslint-disable-next-line complexity
  handleKeyNavigation(event, executeAction = true) {
    const isMac = AppConstants.platform == "macosx";
    // Handle readline/emacs-style navigation bindings on Mac.
    if (
      isMac &&
      this.view.isOpen &&
      event.ctrlKey &&
      (event.key == "n" || event.key == "p")
    ) {
      if (executeAction) {
        this.view.selectBy(1, { reverse: event.key == "p" });
      }
      event.preventDefault();
      return;
    }

    if (this.view.isOpen && executeAction && this._lastQueryContextWrapper) {
      // In native inputs on most platforms, Shift+Up/Down moves the caret to the
      // start/end of the input and changes its selection, so in that case defer
      // handling to the input instead of changing the view's selection.
      if (
        event.shiftKey &&
        (event.keyCode === KeyEvent.DOM_VK_UP ||
          event.keyCode === KeyEvent.DOM_VK_DOWN)
      ) {
        return;
      }

      let { queryContext } = this._lastQueryContextWrapper;
      let handled = this.view.oneOffSearchButtons.handleKeyDown(
        event,
        this.view.visibleRowCount,
        this.view.allowEmptySelection,
        queryContext.searchString
      );
      if (handled) {
        return;
      }
    }

    switch (event.keyCode) {
      case KeyEvent.DOM_VK_ESCAPE:
        if (executeAction) {
          if (this.view.isOpen) {
            this.view.close();
          } else if (
            lazy.UrlbarPrefs.get("focusContentDocumentOnEsc") &&
            !this.input.searchMode &&
            (this.input.getAttribute("pageproxystate") == "valid" ||
              (this.input.value == "" &&
                this.browserWindow.isBlankPageURL(
                  this.browserWindow.gBrowser.currentURI.spec
                )))
          ) {
            this.browserWindow.gBrowser.selectedBrowser.focus();
          } else {
            this.input.handleRevert();
          }
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_SPACE:
        if (!this.view.shouldSpaceActivateSelectedElement()) {
          break;
        }
      // Fall through, we want the SPACE key to activate this element.
      case KeyEvent.DOM_VK_RETURN:
        lazy.logger.debug(`Enter pressed${executeAction ? "" : " delayed"}`);
        if (executeAction) {
          this.input.handleCommand(event);
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_TAB: {
        if (!this.view.visibleRowCount) {
          // Leave it to the default behaviour if there are not results.
          break;
        }

        // Change the tab behavior when urlbar view is open.
        if (
          lazy.UrlbarPrefs.get("scotchBonnet.enableOverride") &&
          this.view.isOpen &&
          !event.ctrlKey &&
          !event.altKey
        ) {
          if (
            (event.shiftKey &&
              this.view.selectedElement ==
                this.view.getFirstSelectableElement()) ||
            (!event.shiftKey &&
              this.view.selectedElement == this.view.getLastSelectableElement())
          ) {
            // If pressing tab + shift when the first or pressing tab when last
            // element has been selected, move the focus to the Unified Search
            // Button. Then make urlbar results selectable by tab + shift.
            event.preventDefault();
            this.view.selectedRowIndex = -1;
            this.#focusOnUnifiedSearchButton();
            break;
          } else if (
            !this.view.selectedElement &&
            this.input.focusedViaMousedown
          ) {
            if (event.shiftKey) {
              this.#focusOnUnifiedSearchButton();
            } else {
              this.view.selectBy(1, {
                userPressedTab: true,
              });
            }
            event.preventDefault();
            break;
          }
        }

        // It's always possible to tab through results when the urlbar was
        // focused with the mouse or has a search string, or when the view
        // already has a selection.
        // We allow tabbing without a search string when in search mode preview,
        // since that means the user has interacted with the Urlbar since
        // opening it.
        // When there's no search string and no view selection, we want to focus
        // the next toolbar item instead, for accessibility reasons.
        let allowTabbingThroughResults =
          this.input.focusedViaMousedown ||
          this.input.searchMode?.isPreview ||
          this.input.searchMode?.source ==
            lazy.UrlbarUtils.RESULT_SOURCE.ACTIONS ||
          this.view.selectedElement ||
          (this.input.value &&
            this.input.getAttribute("pageproxystate") != "valid");
        if (
          // Even if the view is closed, we may be waiting results, and in
          // such a case we don't want to tab out of the urlbar.
          (this.view.isOpen || !executeAction) &&
          !event.ctrlKey &&
          !event.altKey &&
          allowTabbingThroughResults
        ) {
          if (executeAction) {
            this.userSelectionBehavior = "tab";
            this.view.selectBy(1, {
              reverse: event.shiftKey,
              userPressedTab: true,
            });
          }
          event.preventDefault();
        }
        break;
      }
      case KeyEvent.DOM_VK_PAGE_DOWN:
      case KeyEvent.DOM_VK_PAGE_UP:
        if (event.ctrlKey) {
          break;
        }
      // eslint-disable-next-lined no-fallthrough
      case KeyEvent.DOM_VK_DOWN:
      case KeyEvent.DOM_VK_UP:
        if (event.altKey) {
          break;
        }
        if (this.view.isOpen) {
          if (executeAction) {
            this.userSelectionBehavior = "arrow";
            this.view.selectBy(
              event.keyCode == KeyEvent.DOM_VK_PAGE_DOWN ||
                event.keyCode == KeyEvent.DOM_VK_PAGE_UP
                ? lazy.UrlbarUtils.PAGE_UP_DOWN_DELTA
                : 1,
              {
                reverse:
                  event.keyCode == KeyEvent.DOM_VK_UP ||
                  event.keyCode == KeyEvent.DOM_VK_PAGE_UP,
              }
            );
          }
        } else {
          if (this.keyEventMovesCaret(event)) {
            break;
          }
          if (executeAction) {
            this.userSelectionBehavior = "arrow";
            this.input.startQuery({
              searchString: this.input.value,
              event,
            });
          }
        }
        event.preventDefault();
        break;
      case KeyEvent.DOM_VK_RIGHT:
      case KeyEvent.DOM_VK_END:
        this.input.maybeConfirmSearchModeFromResult({
          entry: "typed",
          startQuery: true,
        });
      // Fall through.
      case KeyEvent.DOM_VK_LEFT:
      case KeyEvent.DOM_VK_HOME:
        this.view.removeAccessibleFocus();
        break;
      case KeyEvent.DOM_VK_BACK_SPACE:
        if (
          this.input.searchMode &&
          this.input.selectionStart == 0 &&
          this.input.selectionEnd == 0 &&
          !event.shiftKey
        ) {
          this.input.searchMode = null;
          this.input.view.oneOffSearchButtons.selectedButton = null;
          this.input.startQuery({
            allowAutofill: false,
            event,
          });
        }
      // Fall through.
      case KeyEvent.DOM_VK_DELETE:
        if (!this.view.isOpen) {
          break;
        }
        if (event.shiftKey) {
          if (!executeAction || this.#dismissSelectedResult(event)) {
            event.preventDefault();
          }
        } else if (executeAction) {
          this.userSelectionBehavior = "none";
        }
        break;
    }
  }

  /**
   * Tries to initialize a speculative connection on a result.
   * Speculative connections are only supported for a subset of all the results.
   *
   * Speculative connect to:
   *  - Search engine heuristic results
   *  - autofill results
   *  - http/https results
   *
   * @param {UrlbarResult} result The result to speculative connect to.
   * @param {UrlbarQueryContext} context The queryContext
   * @param {string} reason Reason for the speculative connect request.
   */
  speculativeConnect(result, context, reason) {
    // Never speculative connect in private contexts.
    if (!this.input || context.isPrivate || !context.results.length) {
      return;
    }

    switch (reason) {
      case "resultsadded": {
        // We should connect to an heuristic result, if it exists.
        if (
          (result == context.results[0] && result.heuristic) ||
          result.autofill
        ) {
          if (result.type == lazy.UrlbarUtils.RESULT_TYPE.SEARCH) {
            // Speculative connect only if search suggestions are enabled.
            if (
              lazy.UrlbarPrefs.get("suggest.searches") &&
              lazy.UrlbarPrefs.get("browser.search.suggest.enabled")
            ) {
              let engine = Services.search.getEngineByName(
                result.payload.engine
              );
              lazy.UrlbarUtils.setupSpeculativeConnection(
                engine,
                this.browserWindow
              );
            }
          } else if (result.autofill) {
            const { url } = lazy.UrlbarUtils.getUrlFromResult(result);
            if (!url) {
              return;
            }

            lazy.UrlbarUtils.setupSpeculativeConnection(
              url,
              this.browserWindow
            );
          }
        }
        return;
      }
      case "mousedown": {
        const { url } = lazy.UrlbarUtils.getUrlFromResult(result);
        if (!url) {
          return;
        }

        // On mousedown, connect only to http/https urls.
        if (url.startsWith("http")) {
          lazy.UrlbarUtils.setupSpeculativeConnection(url, this.browserWindow);
        }
        return;
      }
      default: {
        throw new Error("Invalid speculative connection reason");
      }
    }
  }

  /**
   * Stores the selection behavior that the user has used to select a result.
   *
   * @param {"arrow"|"tab"|"none"} behavior
   *   The behavior the user used.
   */
  set userSelectionBehavior(behavior) {
    // Don't change the behavior to arrow if tab has already been recorded,
    // as we want to know that the tab was used first.
    if (behavior == "arrow" && this._userSelectionBehavior == "tab") {
      return;
    }
    this._userSelectionBehavior = behavior;
  }

  /**
   * Triggers a "dismiss" engagement for the selected result if one is selected
   * and it's not the heuristic. Providers that can respond to dismissals of
   * their results should implement `onEngagement()`, handle the
   * dismissal, and call `controller.removeResult()`.
   *
   * @param {Event} event
   *   The event that triggered dismissal.
   * @returns {boolean}
   *   Whether providers were notified about the engagement. Providers will not
   *   be notified if there is no selected result or the selected result is the
   *   heuristic, since the heuristic result cannot be dismissed.
   */
  #dismissSelectedResult(event) {
    if (!this._lastQueryContextWrapper) {
      console.error("Cannot dismiss selected result, last query not present");
      return false;
    }
    let { queryContext } = this._lastQueryContextWrapper;

    let { selectedElement } = this.input.view;
    if (selectedElement?.classList.contains("urlbarView-button")) {
      // For results with buttons, delete them only when the main part of the
      // row is selected, not a button.
      return false;
    }

    let result = this.input.view.selectedResult;
    if (!result || result.heuristic) {
      return false;
    }

    this.engagementEvent.record(event, {
      result,
      selType: "dismiss",
      searchString: queryContext.searchString,
    });

    return true;
  }

  /**
   * Removes a result from the current query context and notifies listeners.
   * Heuristic results cannot be removed.
   *
   * @param {UrlbarResult} result
   *   The result to remove.
   */
  removeResult(result) {
    if (!result || result.heuristic) {
      return;
    }

    if (!this._lastQueryContextWrapper) {
      console.error("Cannot remove result, last query not present");
      return;
    }
    let { queryContext } = this._lastQueryContextWrapper;

    let index = queryContext.results.indexOf(result);
    if (index < 0) {
      console.error("Failed to find the selected result in the results");
      return;
    }

    queryContext.results.splice(index, 1);
    this.notify(NOTIFICATIONS.QUERY_RESULT_REMOVED, index);
  }

  /**
   * Set the query context cache.
   *
   * @param {UrlbarQueryContext} queryContext the object to cache.
   */
  setLastQueryContextCache(queryContext) {
    this._lastQueryContextWrapper = { queryContext };
  }

  /**
   * Clear the previous query context cache.
   */
  clearLastQueryContextCache() {
    this._lastQueryContextWrapper = null;
  }

  /**
   * Notifies listeners of results.
   *
   * @param {string} name Name of the notification.
   * @param {object} params Parameters to pass with the notification.
   */
  notify(name, ...params) {
    for (let listener of this._listeners) {
      // Can't use "in" because some tests proxify these.
      if (typeof listener[name] != "undefined") {
        try {
          listener[name](...params);
        } catch (ex) {
          console.error(ex);
        }
      }
    }
  }

  #focusOnUnifiedSearchButton() {
    this.input.setUnifiedSearchButtonAvailability(true);

    const switcher = this.input.querySelector(".searchmode-switcher");
    // Set tabindex to be focusable.
    switcher.setAttribute("tabindex", "-1");
    // Remove blur listener to avoid closing urlbar view panel.
    this.input.removeEventListener("blur", this.input);
    // Move the focus.
    switcher.focus();
    // Restore all.
    this.input.addEventListener("blur", this.input);
    switcher.addEventListener(
      "blur",
      e => {
        switcher.removeAttribute("tabindex");
        if (
          this.input.hasAttribute("focused") &&
          !e.relatedTarget?.closest("#urlbar")
        ) {
          // If the focus is not back to urlbar, fire blur event explicitly to
          // clear the urlbar. Because the input field has been losing an
          // opportunity to lose the focus since we removed blur listener once.
          this.input.inputField.dispatchEvent(
            new FocusEvent("blur", {
              relatedTarget: e.relatedTarget,
            })
          );
        }
      },
      { once: true }
    );
  }
}

/**
 * Tracks and records telemetry events for the given category, if provided,
 * otherwise it's a no-op.
 * It is currently designed around the "urlbar" category, even if it can
 * potentially be extended to other categories.
 * To record an event, invoke start() with a starting event, then either
 * invoke record() with a final event, or discard() to drop the recording.
 *
 * @see Events.yaml
 */
class TelemetryEvent {
  constructor(controller, category) {
    this._controller = controller;
    this._category = category;
    lazy.UrlbarPrefs.addObserver(this);
    this.#readPingPrefs();
    this._lastSearchDetailsForDisableSuggestTracking = null;
  }

  /**
   * Start measuring the elapsed time from a user-generated event.
   * After this has been invoked, any subsequent calls to start() are ignored,
   * until either record() or discard() are invoked. Thus, it is safe to keep
   * invoking this on every input event as the user is typing, for example.
   *
   * @param {event} event A DOM event.
   * @param {UrlbarQueryContext} queryContext A queryContext.
   * @param {string} [searchString] Pass a search string related to the event if
   *        you have one.  The event by itself sometimes isn't enough to
   *        determine the telemetry details we should record.
   * @throws This should never throw, or it may break the urlbar.
   * @see {@link https://firefox-source-docs.mozilla.org/browser/urlbar/telemetry.html}
   */
  start(event, queryContext, searchString = null) {
    if (this._startEventInfo) {
      if (this._startEventInfo.interactionType == "topsites") {
        // If the most recent event came from opening the results pane with an
        // empty string replace the interactionType (that would be "topsites")
        // with one for the current event to better measure the user flow.
        this._startEventInfo.interactionType = this._getStartInteractionType(
          event,
          searchString
        );
        this._startEventInfo.searchString = searchString;
      } else if (
        this._startEventInfo.interactionType == "returned" &&
        (!searchString ||
          this._startEventInfo.searchString[0] != searchString[0])
      ) {
        // In case of a "returned" interaction ongoing, the user may either
        // continue the search, or restart with a new search string. In that case
        // we want to change the interaction type to "restarted".
        // Detecting all the possible ways of clearing the input would be tricky,
        // thus this makes a guess by just checking the first char matches; even if
        // the user backspaces a part of the string, we still count that as a
        // "returned" interaction.
        this._startEventInfo.interactionType = "restarted";
      }

      // start is invoked on a user-generated event, but we only count the first
      // one.  Once an engagement or abandoment happens, we clear _startEventInfo.
      return;
    }

    if (!this._category) {
      return;
    }
    if (!event) {
      console.error("Must always provide an event");
      return;
    }
    const validEvents = [
      "click",
      "command",
      "drop",
      "input",
      "keydown",
      "mousedown",
      "tabswitch",
      "focus",
    ];
    if (!validEvents.includes(event.type)) {
      console.error("Can't start recording from event type: ", event.type);
      return;
    }

    this._startEventInfo = {
      timeStamp: event.timeStamp || Cu.now(),
      interactionType: this._getStartInteractionType(event, searchString),
      searchString,
    };
  }

  /**
   * Record an engagement telemetry event.
   * When the user picks a result from a search through the mouse or keyboard,
   * an engagement event is recorded. If instead the user abandons a search, by
   * blurring the input field, an abandonment event is recorded.
   *
   * On return, `details.isSessionOngoing` will be set to true if the engagement
   * did not end the search session. Not all engagements end the session. The
   * session remains ongoing when certain commands are picked (like dismissal)
   * and results that enter search mode are picked.
   *
   * @param {event} [event]
   *        A DOM event.
   *        Note: event can be null, that usually happens for paste&go or drop&go.
   *        If there's no _startEventInfo this is a no-op.
   * @param {object} details An object describing action details.
   * @param {string} [details.searchString] The user's search string. Note that
   *        this string is not sent with telemetry data. It is only used
   *        locally to discern other data, such as the number of characters and
   *        words in the string.
   * @param {string} [details.selType] type of the selected element, undefined
   *        for "blur". One of "unknown", "autofill", "visiturl", "bookmark",
   *        "help", "history", "keyword", "searchengine", "searchsuggestion",
   *        "switchtab", "remotetab", "extension", "oneoff", "dismiss".
   * @param {UrlbarResult} [details.result] The engaged result. This should be
   *        set to the result related to the picked element.
   * @param {DOMElement} [details.element] The picked view element.
   */
  record(event, details) {
    // Prevent re-entering `record()`. This can happen because
    // `#internalRecord()` will notify an engagement to the provider, that may
    // execute an action blurring the input field. Then both an engagement
    // and an abandonment would be recorded for the same session.
    // Nulling out `_startEventInfo` doesn't save us in this case, because it
    // happens after `#internalRecord()`, and `isSessionOngoing` must be
    // calculated inside it.
    if (this.#handlingRecord) {
      return;
    }

    // This should never throw, or it may break the urlbar.
    try {
      this.#handlingRecord = true;
      this.#internalRecord(event, details);
    } catch (ex) {
      console.error("Could not record event: ", ex);
    } finally {
      this.#handlingRecord = false;

      // Reset the start event info except for engagements that do not end the
      // search session. In that case, the view stays open and further
      // engagements are possible and should be recorded when they occur.
      // (`details.isSessionOngoing` is not a param; rather, it's set by
      // `#internalRecord()`.)
      if (!details.isSessionOngoing) {
        this._startEventInfo = null;
        this._discarded = false;
      }
    }
  }

  #internalRecord(event, details) {
    const startEventInfo = this._startEventInfo;

    if (!this._category || !startEventInfo) {
      return;
    }
    if (
      !event &&
      startEventInfo.interactionType != "pasted" &&
      startEventInfo.interactionType != "dropped"
    ) {
      // If no event is passed, we must be executing either paste&go or drop&go.
      throw new Error("Event must be defined, unless input was pasted/dropped");
    }
    if (!details) {
      throw new Error("Invalid event details: " + details);
    }

    let action = this.#getActionFromEvent(
      event,
      details,
      startEventInfo.interactionType
    );

    let method =
      action == "blur" || action == "tab_switch" ? "abandonment" : "engagement";

    if (method == "engagement") {
      // Not all engagements end the search session. The session remains ongoing
      // when certain commands are picked (like dismissal) and results that
      // enter search mode are picked. We should find a generalized way to
      // determine this instead of listing all the cases like this.
      details.isSessionOngoing = !!(
        [
          "dismiss",
          "inaccurate_location",
          "not_interested",
          "not_now",
          "opt_in",
          "show_less_frequently",
        ].includes(details.selType) ||
        details.result?.payload.providesSearchMode
      );
    }

    // numWords is not a perfect measurement, since it will return an incorrect
    // value for languages that do not use spaces or URLs containing spaces in
    // its query parameters, for example.
    let { numChars, numWords, searchWords } = this._parseSearchString(
      details.searchString
    );

    details.provider = details.result?.providerName;
    details.selIndex = details.result?.rowIndex ?? -1;

    let { queryContext } = this._controller._lastQueryContextWrapper || {};

    this._recordSearchEngagementTelemetry(method, startEventInfo, {
      action,
      numChars,
      numWords,
      searchWords,
      provider: details.provider,
      searchSource: details.searchSource,
      searchMode: details.searchMode,
      selectedElement: details.element,
      selIndex: details.selIndex,
      selType: details.selType,
    });

    if (!details.isSessionOngoing) {
      this.#recordExposures(queryContext);
    }

    const visibleResults = this._controller.view?.visibleResults ?? [];

    // Start tracking for a disable event if there was a Suggest result
    // during an engagement or abandonment event.
    if (
      (method == "engagement" || method == "abandonment") &&
      visibleResults.some(r => r.providerName == "UrlbarProviderQuickSuggest")
    ) {
      this.startTrackingDisableSuggest(event, details);
    }

    try {
      this._controller.manager.notifyEngagementChange(
        method,
        queryContext,
        details,
        this._controller
      );
    } catch (error) {
      // We handle and report any error here to avoid hitting the record()
      // handler, that would look like we didn't send telemetry at all.
      console.error(error);
    }
  }

  _recordSearchEngagementTelemetry(
    method,
    startEventInfo,
    {
      action,
      numWords,
      numChars,
      provider,
      searchWords,
      searchSource,
      searchMode,
      selIndex,
      selType,
      viewTime = 0,
    }
  ) {
    const browserWindow = this._controller.browserWindow;
    let sap = "urlbar";
    if (searchSource === "urlbar-handoff") {
      sap = "handoff";
    } else if (
      browserWindow.isBlankPageURL(browserWindow.gBrowser.currentURI.spec)
    ) {
      sap = "urlbar_newtab";
    } else if (
      lazy.ExtensionUtils.isExtensionUrl(browserWindow.gBrowser.currentURI)
    ) {
      sap = "urlbar_addonpage";
    }

    searchMode = searchMode ?? this._controller.input.searchMode;

    // Distinguish user typed search strings from persisted search terms.
    const interaction = this.#getInteractionType(
      method,
      startEventInfo,
      searchSource,
      searchWords,
      searchMode
    );
    const search_mode = this.#getSearchMode(searchMode);
    const currentResults = this._controller.view?.visibleResults ?? [];
    let numResults = currentResults.length;
    let groups = currentResults
      .map(r => lazy.UrlbarUtils.searchEngagementTelemetryGroup(r))
      .join(",");
    let results = currentResults
      .map(r => lazy.UrlbarUtils.searchEngagementTelemetryType(r))
      .join(",");
    let actions = currentResults
      .map((r, i) => lazy.UrlbarUtils.searchEngagementTelemetryAction(r, i))
      .filter(v => v)
      .join(",");
    let available_semantic_sources = this.#getAvailableSemanticSources().join();
    const search_engine_default_id = Services.search.defaultEngine.telemetryId;

    let eventInfo;
    if (method === "engagement") {
      let selected_result = lazy.UrlbarUtils.searchEngagementTelemetryType(
        currentResults[selIndex],
        selType
      );

      if (selType == "action") {
        let actionKey = lazy.UrlbarUtils.searchEngagementTelemetryAction(
          currentResults[selIndex],
          selIndex
        );
        selected_result = `action_${actionKey}`;
      }

      if (selected_result === "input_field" && !this._controller.view?.isOpen) {
        numResults = 0;
        groups = "";
        results = "";
      }

      eventInfo = {
        sap,
        interaction,
        search_mode,
        n_chars: numChars,
        n_words: numWords,
        n_results: numResults,
        selected_position: selIndex + 1,
        selected_result,
        provider,
        engagement_type:
          selType === "help" || selType === "dismiss" ? selType : action,
        search_engine_default_id,
        groups,
        results,
        actions,
        available_semantic_sources,
      };
    } else if (method === "abandonment") {
      eventInfo = {
        abandonment_type: action,
        sap,
        interaction,
        search_mode,
        n_chars: numChars,
        n_words: numWords,
        n_results: numResults,
        search_engine_default_id,
        groups,
        results,
        actions,
        available_semantic_sources,
      };
    } else if (method == "disable") {
      const previousEvent =
        action == "blur" || action == "tab_switch"
          ? "abandonment"
          : "engagement";
      let selected_result = "none";
      if (previousEvent == "engagement") {
        selected_result = lazy.UrlbarUtils.searchEngagementTelemetryType(
          currentResults[selIndex],
          selType
        );
      }
      eventInfo = {
        sap,
        interaction,
        search_mode,
        search_engine_default_id,
        n_chars: numChars,
        n_words: numWords,
        n_results: numResults,
        selected_result,
        results,
        feature: "suggest",
      };
    } else if (method === "bounce") {
      let selected_result = lazy.UrlbarUtils.searchEngagementTelemetryType(
        currentResults[selIndex],
        selType
      );
      eventInfo = {
        sap,
        interaction,
        search_mode,
        search_engine_default_id,
        n_chars: numChars,
        n_words: numWords,
        n_results: numResults,
        selected_result,
        selected_position: selIndex + 1,
        provider,
        engagement_type:
          selType === "help" || selType === "dismiss" ? selType : action,
        results,
        view_time: viewTime,
        threshold: lazy.UrlbarPrefs.get(
          "events.bounce.maxSecondsFromLastSearch"
        ),
      };
    } else {
      console.error(`Unknown telemetry event method: ${method}`);
      return;
    }

    lazy.logger.info(`${method} event:`, eventInfo);

    Glean.urlbar[method].record(eventInfo);
  }

  /**
   * Retrieves available semantic search sources.
   * Ensure it is the provider initializing the semantic manager, since it
   * provides the right configuration for the singleton.
   *
   * @returns {Array<string>} Array of found sources, will contain just "none"
   *   if no sources were found.
   */
  #getAvailableSemanticSources() {
    let sources = [];
    try {
      if (
        lazy.ProviderSemanticHistorySearch.semanticManager.canUseSemanticSearch
      ) {
        sources.push("history");
      }
    } catch (e) {
      lazy.logger.error("Error getting the semantic manager:", e);
    }
    if (!sources.length) {
      sources.push("none");
    }
    return sources;
  }

  #recordExposures(queryContext) {
    let exposures = this.#exposures;
    this.#exposures = [];
    this.#tentativeExposures = [];
    if (!exposures.length) {
      return;
    }

    let terminalByType = new Map();
    let keywordExposureRecorded = false;
    for (let { weakResult, resultType, keyword } of exposures) {
      let terminal = false;
      let result = weakResult.get();
      if (result) {
        this.#exposureResults.delete(result);

        let endResults = result.isHiddenExposure
          ? queryContext.results
          : this._controller.view?.visibleResults;
        terminal = endResults?.includes(result);
      }

      terminalByType.set(resultType, terminal);

      // Record the `keyword_exposure` event if there's a keyword.
      if (keyword) {
        let data = { keyword, terminal, result: resultType };
        lazy.logger.debug("Recording keyword_exposure event", data);
        Glean.urlbar.keywordExposure.record(data);
        keywordExposureRecorded = true;
      }
    }

    // Record the `exposure` event.
    let tuples = [...terminalByType].sort((a, b) => a[0].localeCompare(b[0]));
    let exposure = {
      results: tuples.map(t => t[0]).join(","),
      terminal: tuples.map(t => t[1]).join(","),
    };
    lazy.logger.debug("Recording exposure event", exposure);
    Glean.urlbar.exposure.record(exposure);

    // Submit the `urlbar-keyword-exposure` ping if any keyword exposure events
    // were recorded above.
    if (keywordExposureRecorded) {
      GleanPings.urlbarKeywordExposure.submit();
    }
  }

  /**
   * Registers an exposure for a result in the current urlbar session, if the
   * result should record exposure telemetry. All exposures that are added
   * during a session are recorded in the `exposure` event at the end of the
   * session. If keyword exposures are enabled, they will be recorded in the
   * `urlbar-keyword-exposure` ping at the end of the session as well. Exposures
   * are cleared at the end of each session and do not carry over.
   *
   * @param {UrlbarResult} result An exposure will be added for this result if
   *        exposures are enabled for its result type.
   * @param {UrlbarQueryContext} queryContext The query context associated with
   *        the result.
   */
  addExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#addExposureInternal(result, queryContext);
    }
  }

  /**
   * Registers a tentative exposure for a result in the current urlbar session.
   * Exposures that remain tentative at the end of the session are discarded and
   * are not recorded in the exposure event.
   *
   * @param {UrlbarResult} result A tentative exposure will be added for this
   *        result if exposures are enabled for its result type.
   * @param {UrlbarQueryContext} queryContext The query context associated with
   *        the result.
   */
  addTentativeExposure(result, queryContext) {
    if (result.exposureTelemetry) {
      this.#tentativeExposures.push({
        weakResult: Cu.getWeakReference(result),
        weakQueryContext: Cu.getWeakReference(queryContext),
      });
    }
  }

  /**
   * Converts all tentative exposures that were added and not yet discarded
   * during the current urlbar session into actual exposures that will be
   * recorded at the end of the session.
   */
  acceptTentativeExposures() {
    if (this.#tentativeExposures.length) {
      for (let { weakResult, weakQueryContext } of this.#tentativeExposures) {
        let result = weakResult.get();
        let queryContext = weakQueryContext.get();
        if (result && queryContext) {
          this.#addExposureInternal(result, queryContext);
        }
      }
      this.#tentativeExposures = [];
    }
  }

  /**
   * Discards all tentative exposures that were added and not yet accepted
   * during the current urlbar session.
   */
  discardTentativeExposures() {
    if (this.#tentativeExposures.length) {
      this.#tentativeExposures = [];
    }
  }

  #addExposureInternal(result, queryContext) {
    // If we haven't added an exposure for this result, add it now. The view can
    // add exposures for the same results again and again due to the nature of
    // its update process, but we should record at most one exposure per result.
    if (!this.#exposureResults.has(result)) {
      this.#exposureResults.add(result);
      let resultType = lazy.UrlbarUtils.searchEngagementTelemetryType(result);
      this.#exposures.push({
        resultType,
        weakResult: Cu.getWeakReference(result),
        keyword:
          !queryContext.isPrivate &&
          lazy.UrlbarPrefs.get("keywordExposureResults").has(resultType)
            ? queryContext.trimmedLowerCaseSearchString
            : null,
      });
    }
  }

  #getInteractionType(
    method,
    startEventInfo,
    searchSource,
    searchWords,
    searchMode
  ) {
    if (searchMode?.entry === "topsites_newtab") {
      return "topsite_search";
    }

    let interaction = startEventInfo.interactionType;
    if (
      (interaction === "returned" || interaction === "restarted") &&
      this._isRefined(new Set(searchWords), this.#previousSearchWordsSet)
    ) {
      interaction = "refined";
    }

    if (searchSource === "urlbar-persisted") {
      switch (interaction) {
        case "returned": {
          interaction = "persisted_search_terms";
          break;
        }
        case "restarted":
        case "refined": {
          interaction = `persisted_search_terms_${interaction}`;
          break;
        }
      }
    }

    if (
      (method === "engagement" &&
        lazy.UrlbarPrefs.isPersistedSearchTermsEnabled()) ||
      method === "abandonment"
    ) {
      this.#previousSearchWordsSet = new Set(searchWords);
    } else if (method === "engagement") {
      this.#previousSearchWordsSet = null;
    }

    return interaction;
  }

  #getSearchMode(searchMode) {
    if (!searchMode) {
      return "";
    }

    if (searchMode.engineName) {
      return "search_engine";
    }

    const source = lazy.UrlbarUtils.LOCAL_SEARCH_MODES.find(
      m => m.source == searchMode.source
    )?.telemetryLabel;
    return source ?? "unknown";
  }

  #getActionFromEvent(event, details, defaultInteractionType) {
    if (!event) {
      return defaultInteractionType === "dropped" ? "drop_go" : "paste_go";
    }
    if (event.type === "blur") {
      return "blur";
    }
    if (event.type === "tabswitch") {
      return "tab_switch";
    }
    if (
      details.element?.dataset.command &&
      details.element.dataset.command !== "help"
    ) {
      return details.element.dataset.command;
    }
    if (details.selType === "dismiss") {
      return "dismiss";
    }
    if (MouseEvent.isInstance(event)) {
      return event.target.classList.contains("urlbar-go-button")
        ? "go_button"
        : "click";
    }
    return "enter";
  }

  _parseSearchString(searchString) {
    let numChars = searchString.length.toString();
    let searchWords = searchString
      .substring(0, lazy.UrlbarUtils.MAX_TEXT_LENGTH)
      .trim()
      .split(lazy.UrlbarTokenizer.REGEXP_SPACES)
      .filter(t => t);
    let numWords = searchWords.length.toString();

    return {
      numChars,
      numWords,
      searchWords,
    };
  }

  /**
   * Checks whether re-searched by modifying some of the keywords from the
   * previous search. Concretely, returns true if there is intersects between
   * both keywords, otherwise returns false. Also, returns false even if both
   * are the same.
   *
   * @param {Set} currentSet The current keywords.
   * @param {Set} [previousSet] The previous keywords.
   * @returns {boolean} true if current searching are refined.
   */
  _isRefined(currentSet, previousSet = null) {
    if (!previousSet) {
      return false;
    }

    const intersect = (setA, setB) => {
      let count = 0;
      for (const word of setA.values()) {
        if (setB.has(word)) {
          count += 1;
        }
      }
      return count > 0 && count != setA.size;
    };

    return (
      intersect(currentSet, previousSet) || intersect(previousSet, currentSet)
    );
  }

  _getStartInteractionType(event, searchString) {
    if (event.interactionType) {
      return event.interactionType;
    } else if (event.type == "input") {
      return lazy.UrlbarUtils.isPasteEvent(event) ? "pasted" : "typed";
    } else if (event.type == "drop") {
      return "dropped";
    } else if (searchString) {
      return "typed";
    }
    return "topsites";
  }

  /**
   * Resets the currently tracked user-generated event that was registered via
   * start(), so it won't be recorded.  If there's no tracked event, this is a
   * no-op.
   */
  discard() {
    if (this._startEventInfo) {
      this._startEventInfo = null;
      this._discarded = true;
    }
  }

  /**
   * Extracts a telemetry type from a result and the element being interacted
   * with for event telemetry.
   *
   * @param {object} result The element to analyze.
   * @param {Element} element The element to analyze.
   * @returns {string} a string type for the telemetry event.
   */
  typeFromElement(result, element) {
    if (!element) {
      return "none";
    }
    if (element.dataset.command == "help") {
      return result?.type == lazy.UrlbarUtils.RESULT_TYPE.TIP
        ? "tiphelp"
        : "help";
    }
    if (element.dataset.command == "dismiss") {
      return "block";
    }
    // Now handle the result.
    return lazy.UrlbarUtils.telemetryTypeFromResult(result);
  }

  /**
   * Reset the internal state. This function is used for only when testing.
   */
  reset() {
    this.#previousSearchWordsSet = null;
  }

  #PING_PREFS = {
    maxRichResults: Glean.urlbar.prefMaxResults,
    "quicksuggest.dataCollection.enabled":
      Glean.urlbar.prefSuggestDataCollection,
    "suggest.quicksuggest.nonsponsored": Glean.urlbar.prefSuggestNonsponsored,
    "suggest.quicksuggest.sponsored": Glean.urlbar.prefSuggestSponsored,
    "suggest.topsites": Glean.urlbar.prefSuggestTopsites,
  };

  #readPingPrefs() {
    for (const p of Object.keys(this.#PING_PREFS)) {
      this.onPrefChanged(p);
    }
  }

  onPrefChanged(pref) {
    const metric = this.#PING_PREFS[pref];
    const prefValue = lazy.UrlbarPrefs.get(pref);
    if (metric) {
      metric.set(prefValue);
    }
    switch (pref) {
      case "suggest.quicksuggest.nonsponsored":
      case "suggest.quicksuggest.sponsored":
      case "quicksuggest.enabled":
        if (!prefValue) {
          this.handleDisableSuggest();
        }
    }
  }

  // Used to avoid re-entering `record()`.
  #handlingRecord = false;

  #previousSearchWordsSet = null;

  // These properties are used to record exposure telemetry. For general info on
  // exposures, see [1]. For keyword exposures, see [2] and [3]. Here's a
  // summary of how a result flows through the exposure telemetry code path:
  //
  // 1. The view makes the result's row visible and calls `addExposure()` for
  //    it. (Or, if the result is a hidden exposure, the view would have made
  //    its row visible.)
  // 2. If exposure telemetry should be recorded for the result, we push its
  //    telemetry type and some other data onto `#exposures`. If keyword
  //    exposures are enabled, we also include the search string in the data. We
  //    use `#exposureResults` to efficiently make sure we add at most one
  //    exposure per result to `#exposures`.
  // 3. At the end of a session, we record a single `exposure` event that
  //    includes all unique telemetry types in the `#exposures` data. We also
  //    record one `keyword_exposure` event per search string in the data, with
  //    each search string recorded as the `keyword` for that exposure. We clear
  //    `#exposures` so that the data does not carry over into the next session.
  //
  // `#tentativeExposures` supports hidden exposures and is necessary due to how
  // the view updates itself. When the view creates a row for a normal result,
  // the row can start out hidden, and it's only unhidden if the query finishes
  // without being canceled. When the view encounters a hidden-exposure result,
  // it doesn't actually create a row for it, but if the hypothetical row would
  // have started out visible, the view will call `addExposure()`. If the
  // hypothetical row would have started out hidden, the view will call
  // `addTentativeExposure()` and we'll add the result to `#tentativeExposures`.
  // Once the query finishes and the view unhides its rows, it will call
  // `acceptTentativeExposures()`, finally registering exposures for all such
  // hidden-exposure results in the query. If instead the query is canceled, the
  // view will remove its hidden rows and call `discardTentativeExposures()`.
  //
  // [1] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/metrics/urlbar_exposure
  // [2] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/pings/urlbar-keyword-exposure
  // [3] https://dictionary.telemetry.mozilla.org/apps/firefox_desktop/metrics/urlbar_keyword_exposure
  #exposures = [];
  #tentativeExposures = [];
  #exposureResults = new WeakSet();

  /**
   * Start tracking a potential disable suggest event after user has seen a
   * suggest result.
   *
   * @param {event} event A DOM event.
   * @param {object} details An object describing interaction details.
   */
  startTrackingDisableSuggest(event, details) {
    this._lastSearchDetailsForDisableSuggestTracking = {
      // The time when a user interacts a suggest result, either through
      // an engagement or an abandonment.
      interactionTime: this.getCurrentTime(),
      event,
      details,
    };
  }

  handleDisableSuggest() {
    let state = this._lastSearchDetailsForDisableSuggestTracking;
    if (
      !state ||
      this.getCurrentTime() - state.interactionTime >
        lazy.UrlbarPrefs.get("events.disableSuggest.maxSecondsFromLastSearch") *
          1000
    ) {
      this._lastSearchDetailsForDisableSuggestTracking = null;
      return;
    }

    let event = state.event;
    let details = state.details;

    let startEventInfo = {
      interactionType: this._getStartInteractionType(
        event,
        details.searchString
      ),
      searchString: details.searchString,
    };

    if (
      !event &&
      startEventInfo.interactionType != "pasted" &&
      startEventInfo.interactionType != "dropped"
    ) {
      // If no event is passed, we must be executing either paste&go or drop&go.
      throw new Error("Event must be defined, unless input was pasted/dropped");
    }
    if (!details) {
      throw new Error("Invalid event details: " + details);
    }

    let action = this.#getActionFromEvent(
      event,
      details,
      startEventInfo.interactionType
    );
    let method = "disable";

    let { numChars, numWords, searchWords } = this._parseSearchString(
      details.searchString
    );

    details.provider = details.result?.providerName;
    details.selIndex = details.result?.rowIndex ?? -1;

    this._recordSearchEngagementTelemetry(method, startEventInfo, {
      action,
      numChars,
      numWords,
      searchWords,
      provider: details.provider,
      searchSource: details.searchSource,
      searchMode: details.searchMode,
      selectedElement: details.element,
      selIndex: details.selIndex,
      selType: details.selType,
    });

    this._lastSearchDetailsForDisableSuggestTracking = null;
  }

  getCurrentTime() {
    return Cu.now();
  }

  /**
   * Start tracking a potential bounce event after the user has engaged
   * with a URL bar result.
   *
   * @param {Browser} browser The browser object.
   * @param {event} event A DOM event.
   * @param {object} details An object describing interaction details.
   */
  async startTrackingBounceEvent(browser, event, details) {
    let state = this._controller.input.getBrowserState(browser);
    let startEventInfo = this._startEventInfo;

    // If we are already tracking a bounce, then another engagement
    // could possibly lead to a bounce.
    if (state.bounceEventTracking) {
      await this.handleBounceEventTrigger(browser);
    }

    state.bounceEventTracking = {
      startTime: Date.now(),
      pickEvent: event,
      resultDetails: details,
      startEventInfo,
    };
  }

  /**
   * Handle a bounce event trigger.
   * These include closing the tab/window and navigating away via
   * browser chrome (this includes clicking on history or bookmark entries,
   * and engaging with the URL bar).
   *
   * @param {Browser} browser The browser object.
   */
  async handleBounceEventTrigger(browser) {
    let state = this._controller.input.getBrowserState(browser);
    if (state.bounceEventTracking) {
      const interactions =
        (await lazy.Interactions.getRecentInteractionsForBrowser(browser)) ??
        [];

      // handleBounceEventTrigger() can run concurrently, so we bail out
      // if a prior async invocation has already cleared bounceEventTracking.
      if (!state.bounceEventTracking) {
        return;
      }

      let totalViewTime = 0;
      for (let interaction of interactions) {
        if (interaction.created_at >= state.bounceEventTracking.startTime) {
          totalViewTime += interaction.totalViewTime || 0;
        }
      }

      // If the total view time when the user navigates away after a
      // URL bar interaction is less than the threshold of
      // events.bounce.maxSecondsFromLastSearch, we record a bounce event.
      // If totalViewTime is 0, that means the page didn't load yet, so
      // we wouldn't record a bounce event.
      if (
        totalViewTime != 0 &&
        totalViewTime <
          lazy.UrlbarPrefs.get("events.bounce.maxSecondsFromLastSearch") * 1000
      ) {
        this.recordBounceEvent(browser, totalViewTime);
      }

      state.bounceEventTracking = null;
    }
  }

  /**
   * Record a bounce event
   *
   * @param {Browser} browser The browser object.
   * @param {number} viewTime
   *  The time spent on a tab after a URL bar engagement before
   *  navigating away via browser chrome or closing the tab.
   */
  recordBounceEvent(browser, viewTime) {
    let state = this._controller.input.getBrowserState(browser);
    let event = state.bounceEventTracking.pickEvent;
    let details = state.bounceEventTracking.resultDetails;

    let startEventInfo = state.bounceEventTracking.startEventInfo;

    if (!startEventInfo) {
      return;
    }

    if (
      !event &&
      startEventInfo.interactionType != "pasted" &&
      startEventInfo.interactionType != "dropped"
    ) {
      // If no event is passed, we must be executing either paste&go or drop&go.
      throw new Error("Event must be defined, unless input was pasted/dropped");
    }
    if (!details) {
      throw new Error("Invalid event details: " + details);
    }

    let action = this.#getActionFromEvent(
      event,
      details,
      startEventInfo.interactionType
    );
    let method = "bounce";

    let { numChars, numWords, searchWords } = this._parseSearchString(
      details.searchString
    );

    details.provider = details.result?.providerName;
    details.selIndex = details.result?.rowIndex ?? -1;

    this._recordSearchEngagementTelemetry(method, startEventInfo, {
      action,
      numChars,
      numWords,
      searchWords,
      provider: details.provider,
      searchSource: details.searchSource,
      searchMode: details.searchMode,
      selIndex: details.selIndex,
      selType: details.selType,
      viewTime: viewTime / 1000,
    });
  }
}
