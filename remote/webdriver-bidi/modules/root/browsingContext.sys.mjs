/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RootBiDiModule } from "chrome://remote/content/webdriver-bidi/modules/RootBiDiModule.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  BrowsingContextListener:
    "chrome://remote/content/shared/listeners/BrowsingContextListener.sys.mjs",
  capture: "chrome://remote/content/shared/Capture.sys.mjs",
  ContextDescriptorType:
    "chrome://remote/content/shared/messagehandler/MessageHandler.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  EventPromise: "chrome://remote/content/shared/Sync.sys.mjs",
  getTimeoutMultiplier: "chrome://remote/content/shared/AppInfo.sys.mjs",
  getWebDriverSessionById:
    "chrome://remote/content/shared/webdriver/Session.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  modal: "chrome://remote/content/shared/Prompt.sys.mjs",
  registerNavigationId:
    "chrome://remote/content/shared/NavigationManager.sys.mjs",
  NavigationListener:
    "chrome://remote/content/shared/listeners/NavigationListener.sys.mjs",
  PollPromise: "chrome://remote/content/shared/Sync.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  print: "chrome://remote/content/shared/PDF.sys.mjs",
  ProgressListener: "chrome://remote/content/shared/Navigate.sys.mjs",
  PromptListener:
    "chrome://remote/content/shared/listeners/PromptListener.sys.mjs",
  SessionDataMethod:
    "chrome://remote/content/shared/messagehandler/sessiondata/SessionData.sys.mjs",
  setDefaultAndAssertSerializationOptions:
    "chrome://remote/content/webdriver-bidi/RemoteValue.sys.mjs",
  TabManager: "chrome://remote/content/shared/TabManager.sys.mjs",
  UserContextManager:
    "chrome://remote/content/shared/UserContextManager.sys.mjs",
  waitForInitialNavigationCompleted:
    "chrome://remote/content/shared/Navigate.sys.mjs",
  WindowGlobalMessageHandler:
    "chrome://remote/content/shared/messagehandler/WindowGlobalMessageHandler.sys.mjs",
  windowManager: "chrome://remote/content/shared/WindowManager.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.WEBDRIVER_BIDI)
);

// Maximal window dimension allowed when emulating a viewport.
const MAX_WINDOW_SIZE = 10000000;

/**
 * @typedef {string} ClipRectangleType
 */

/**
 * Enum of possible clip rectangle types supported by the
 * browsingContext.captureScreenshot command.
 *
 * @readonly
 * @enum {ClipRectangleType}
 */
export const ClipRectangleType = {
  Box: "box",
  Element: "element",
};

/**
 * @typedef {object} CreateType
 */

/**
 * Enum of types supported by the browsingContext.create command.
 *
 * @readonly
 * @enum {CreateType}
 */
const CreateType = {
  tab: "tab",
  window: "window",
};

/**
 * @typedef {string} LocatorType
 */

/**
 * Enum of types supported by the browsingContext.locateNodes command.
 *
 * @readonly
 * @enum {LocatorType}
 */
export const LocatorType = {
  accessibility: "accessibility",
  css: "css",
  innerText: "innerText",
  xpath: "xpath",
};

/**
 * @typedef {string} OriginType
 */

/**
 * Enum of origin type supported by the
 * browsingContext.captureScreenshot command.
 *
 * @readonly
 * @enum {OriginType}
 */
export const OriginType = {
  document: "document",
  viewport: "viewport",
};

const TIMEOUT_SET_HISTORY_INDEX = 1000;

/**
 * Enum of user prompt types supported by the browsingContext.handleUserPrompt
 * command, these types can be retrieved from `dialog.args.promptType`.
 *
 * @readonly
 * @enum {UserPromptType}
 */
const UserPromptType = {
  alert: "alert",
  confirm: "confirm",
  prompt: "prompt",
  beforeunload: "beforeunload",
};

/**
 * An object that contains details of a viewport.
 *
 * @typedef {object} Viewport
 *
 * @property {number} height
 *     The height of the viewport.
 * @property {number} width
 *     The width of the viewport.
 */

/**
 * @typedef {string} WaitCondition
 */

/**
 * Wait conditions supported by WebDriver BiDi for navigation.
 *
 * @enum {WaitCondition}
 */
const WaitCondition = {
  None: "none",
  Interactive: "interactive",
  Complete: "complete",
};

/**
 * Used as an argument for browsingContext._updateNavigableViewport command
 * to represent an object which holds viewport settings which should be applied.
 *
 * @typedef ViewportOverride
 *
 * @property {number|null} devicePixelRatio
 *     A value to override device pixel ratio, or `null` to reset it to
 *     the original value.
 * @property {Viewport|null} viewport
 *     Dimensions to set the viewport to, or `null` to reset it
 *     to the original dimensions.
 */

class BrowsingContextModule extends RootBiDiModule {
  #blockedCreateCommands;
  #contextListener;
  #navigationListener;
  #promptListener;
  #subscribedEvents;

  /**
   * Create a new module instance.
   *
   * @param {MessageHandler} messageHandler
   *     The MessageHandler instance which owns this Module instance.
   */
  constructor(messageHandler) {
    super(messageHandler);

    this.#contextListener = new lazy.BrowsingContextListener();
    this.#contextListener.on("attached", this.#onContextAttached);
    this.#contextListener.on("discarded", this.#onContextDiscarded);

    this.#navigationListener = new lazy.NavigationListener(
      this.messageHandler.navigationManager
    );
    this.#navigationListener.on(
      "fragment-navigated",
      this.#onFragmentNavigated
    );
    this.#navigationListener.on("history-updated", this.#onHistoryUpdated);
    this.#navigationListener.on(
      "navigation-committed",
      this.#onNavigationCommitted
    );
    this.#navigationListener.on("navigation-failed", this.#onNavigationFailed);
    this.#navigationListener.on(
      "navigation-started",
      this.#onNavigationStarted
    );

    // Create the prompt listener and listen to "closed" and "opened" events.
    this.#promptListener = new lazy.PromptListener();
    this.#promptListener.on("closed", this.#onPromptClosed);
    this.#promptListener.on("opened", this.#onPromptOpened);

    // Set of event names which have active subscriptions.
    this.#subscribedEvents = new Set();

    // Treat the event of moving a page to BFCache as context discarded event for iframes.
    this.messageHandler.on("windowglobal-pagehide", this.#onPageHideEvent);

    // Maps browsers to a promise and resolver that is used to block the create method.
    this.#blockedCreateCommands = new WeakMap();
  }

  destroy() {
    this.#blockedCreateCommands = new WeakMap();

    this.#contextListener.off("attached", this.#onContextAttached);
    this.#contextListener.off("discarded", this.#onContextDiscarded);
    this.#contextListener.destroy();

    this.#navigationListener.off(
      "fragment-navigated",
      this.#onFragmentNavigated
    );
    this.#navigationListener.off("history-updated", this.#onHistoryUpdated);
    this.#navigationListener.off(
      "navigation-committed",
      this.#onNavigationCommitted
    );
    this.#navigationListener.off("navigation-failed", this.#onNavigationFailed);
    this.#navigationListener.off(
      "navigation-started",
      this.#onNavigationStarted
    );
    this.#navigationListener.destroy();

    this.#promptListener.off("closed", this.#onPromptClosed);
    this.#promptListener.off("opened", this.#onPromptOpened);
    this.#promptListener.destroy();

    this.#subscribedEvents = null;

    this.messageHandler.off("windowglobal-pagehide", this.#onPageHideEvent);
  }

  /**
   * Activates and focuses the given top-level browsing context.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  async activate(options = {}) {
    const { context: contextId } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );
    const context = this.#getBrowsingContext(contextId);

    lazy.assert.topLevel(
      context,
      lazy.pprint`Browsing context with id ${contextId} is not top-level`
    );

    const targetTab = lazy.TabManager.getTabForBrowsingContext(context);
    const targetWindow = lazy.TabManager.getWindowForTab(targetTab);
    const selectedTab = lazy.TabManager.getTabBrowser(targetWindow).selectedTab;

    const activated = [
      lazy.windowManager.focusWindow(targetWindow),
      lazy.TabManager.selectTab(targetTab),
    ];

    if (targetTab !== selectedTab && !lazy.AppInfo.isAndroid) {
      // We need to wait until the "document.visibilityState" of the currently
      // selected tab in the target window is marked as "hidden".
      //
      // Bug 1884142: It's not supported on Android for the TestRunner package.
      const selectedBrowser = lazy.TabManager.getBrowserForTab(selectedTab);
      activated.push(
        this.#waitForVisibilityChange(selectedBrowser.browsingContext)
      );
    }

    await Promise.all(activated);
  }

  /**
   * Used as an argument for browsingContext.captureScreenshot command, as one of the available variants
   * {BoxClipRectangle} or {ElementClipRectangle}, to represent a target of the command.
   *
   * @typedef ClipRectangle
   */

  /**
   * Used as an argument for browsingContext.captureScreenshot command
   * to represent a box which is going to be a target of the command.
   *
   * @typedef BoxClipRectangle
   *
   * @property {ClipRectangleType} [type=ClipRectangleType.Box]
   * @property {number} x
   * @property {number} y
   * @property {number} width
   * @property {number} height
   */

  /**
   * Used as an argument for the browsingContext.captureScreenshot command to
   * represent the output image format.
   *
   * @typedef ImageFormat
   *
   * @property {string} type
   *     The output screenshot format such as `image/png`.
   * @property {number=} quality
   *     A number between 0 and 1 representing the screenshot quality.
   */

  /**
   * Used as an argument for browsingContext.captureScreenshot command
   * to represent an element which is going to be a target of the command.
   *
   * @typedef ElementClipRectangle
   *
   * @property {ClipRectangleType} [type=ClipRectangleType.Element]
   * @property {SharedReference} element
   */

  /**
   * Capture a base64-encoded screenshot of the provided browsing context.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context to screenshot.
   * @param {ClipRectangle=} options.clip
   *     A box or an element of which a screenshot should be taken.
   *     If not present, take a screenshot of the whole viewport.
   * @param {OriginType=} options.origin
   * @param {ImageFormat=} options.format
   *    Configuration options for the output image.
   *
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  async captureScreenshot(options = {}) {
    const {
      clip = null,
      context: contextId,
      origin = OriginType.viewport,
      format = { type: "image/png", quality: undefined },
    } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );
    const context = this.#getBrowsingContext(contextId);

    const originTypeValues = Object.values(OriginType);
    lazy.assert.that(
      value => originTypeValues.includes(value),
      `Expected "origin" to be one of ${originTypeValues}, ` +
        lazy.pprint`got ${origin}`
    )(origin);

    lazy.assert.object(
      format,
      lazy.pprint`Expected "format" to be an object, got ${format}`
    );

    const { quality, type: formatType } = format;

    lazy.assert.string(
      formatType,
      lazy.pprint`Expected "type" to be a string, got ${formatType}`
    );

    if (quality !== undefined) {
      lazy.assert.number(
        quality,
        lazy.pprint`Expected "quality" to be a number, got ${quality}`
      );

      lazy.assert.that(
        imageQuality => imageQuality >= 0 && imageQuality <= 1,
        lazy.pprint`Expected "quality" to be in the range of 0 to 1, got ${quality}`
      )(quality);
    }

    if (clip !== null) {
      lazy.assert.object(
        clip,
        lazy.pprint`Expected "clip" to be an object, got ${clip}`
      );

      const { type: clipType } = clip;
      switch (clipType) {
        case ClipRectangleType.Box: {
          const { x, y, width, height } = clip;

          lazy.assert.number(
            x,
            lazy.pprint`Expected "x" to be a number, got ${x}`
          );
          lazy.assert.number(
            y,
            lazy.pprint`Expected "y" to be a number, got ${y}`
          );
          lazy.assert.number(
            width,
            lazy.pprint`Expected "width" to be a number, got ${width}`
          );
          lazy.assert.number(
            height,
            lazy.pprint`Expected "height" to be a number, got ${height}`
          );

          break;
        }

        case ClipRectangleType.Element: {
          const { element } = clip;

          lazy.assert.object(
            element,
            lazy.pprint`Expected "element" to be an object, got ${element}`
          );

          break;
        }

        default:
          throw new lazy.error.InvalidArgumentError(
            `Expected "type" to be one of ${Object.values(
              ClipRectangleType
            )}, ` + lazy.pprint`got ${clipType}`
          );
      }
    }

    const rect = await this.messageHandler.handleCommand({
      moduleName: "browsingContext",
      commandName: "_getScreenshotRect",
      destination: {
        type: lazy.WindowGlobalMessageHandler.type,
        id: context.id,
      },
      params: {
        clip,
        origin,
      },
      retryOnAbort: true,
    });

    if (rect.width === 0 || rect.height === 0) {
      throw new lazy.error.UnableToCaptureScreen(
        `The dimensions of requested screenshot are incorrect, got width: ${rect.width} and height: ${rect.height}.`
      );
    }

    const canvas = await lazy.capture.canvas(
      context.topChromeWindow,
      context,
      rect.x,
      rect.y,
      rect.width,
      rect.height
    );

    return {
      data: lazy.capture.toBase64(canvas, formatType, quality),
    };
  }

  /**
   * Close the provided browsing context.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context to close.
   * @param {boolean=} options.promptUnload
   *     Flag to indicate if a potential beforeunload prompt should be shown
   *     when closing the browsing context. Defaults to false.
   *
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   * @throws {InvalidArgumentError}
   *     If the browsing context is not a top-level one.
   */
  async close(options = {}) {
    const { context: contextId, promptUnload = false } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    lazy.assert.boolean(
      promptUnload,
      lazy.pprint`Expected "promptUnload" to be a boolean, got ${promptUnload}`
    );

    const context = lazy.TabManager.getBrowsingContextById(contextId);
    if (!context) {
      throw new lazy.error.NoSuchFrameError(
        `Browsing Context with id ${contextId} not found`
      );
    }

    lazy.assert.topLevel(
      context,
      lazy.pprint`Browsing context with id ${contextId} is not top-level`
    );

    if (lazy.TabManager.getTabCount() === 1) {
      // The behavior when closing the very last tab is currently unspecified.
      // As such behave like Marionette and don't allow closing it.
      // See: https://github.com/w3c/webdriver-bidi/issues/187
      return;
    }

    const tab = lazy.TabManager.getTabForBrowsingContext(context);
    await lazy.TabManager.removeTab(tab, { skipPermitUnload: !promptUnload });
  }

  /**
   * Create a new browsing context using the provided type "tab" or "window".
   *
   * @param {object=} options
   * @param {boolean=} options.background
   *     Whether the tab/window should be open in the background. Defaults to false,
   *     which means that the tab/window will be open in the foreground.
   * @param {string=} options.referenceContext
   *     Id of the top-level browsing context to use as reference.
   *     If options.type is "tab", the new tab will open in the same window as
   *     the reference context, and will be added next to the reference context.
   *     If options.type is "window", the reference context is ignored.
   * @param {CreateType} options.type
   *     Type of browsing context to create.
   * @param {string=} options.userContext
   *     The id of the user context which should own the browsing context.
   *     Defaults to the default user context.
   *
   * @throws {InvalidArgumentError}
   *     If the browsing context is not a top-level one.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  async create(options = {}) {
    const {
      background = false,
      referenceContext: referenceContextId = null,
      type: typeHint,
      userContext: userContextId = null,
    } = options;

    if (![CreateType.tab, CreateType.window].includes(typeHint)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "type" to be one of ${Object.values(CreateType)}, ` +
          lazy.pprint`got ${typeHint}`
      );
    }

    lazy.assert.boolean(
      background,
      lazy.pprint`Expected "background" to be a boolean, got ${background}`
    );

    let referenceContext = null;
    if (referenceContextId !== null) {
      lazy.assert.string(
        referenceContextId,
        lazy.pprint`Expected "referenceContext" to be a string, got ${referenceContextId}`
      );

      referenceContext =
        lazy.TabManager.getBrowsingContextById(referenceContextId);
      if (!referenceContext) {
        throw new lazy.error.NoSuchFrameError(
          `Browsing Context with id ${referenceContextId} not found`
        );
      }

      if (referenceContext.parent) {
        throw new lazy.error.InvalidArgumentError(
          `referenceContext with id ${referenceContextId} is not a top-level browsing context`
        );
      }
    }

    let userContext = lazy.UserContextManager.defaultUserContextId;
    if (referenceContext !== null) {
      userContext =
        lazy.UserContextManager.getIdByBrowsingContext(referenceContext);
    }

    if (userContextId !== null) {
      lazy.assert.string(
        userContextId,
        lazy.pprint`Expected "userContext" to be a string, got ${userContextId}`
      );

      if (!lazy.UserContextManager.hasUserContextId(userContextId)) {
        throw new lazy.error.NoSuchUserContextError(
          `User Context with id ${userContextId} was not found`
        );
      }

      userContext = userContextId;

      if (
        lazy.AppInfo.isAndroid &&
        userContext != lazy.UserContextManager.defaultUserContextId
      ) {
        throw new lazy.error.UnsupportedOperationError(
          `browsingContext.create with non-default "userContext" not supported for ${lazy.AppInfo.name}`
        );
      }
    }

    let browser;

    // Since each tab in GeckoView has its own Gecko instance running,
    // which means also its own window object, for Android we will need to focus
    // a previously focused window in case of opening the tab in the background.
    const previousWindow = Services.wm.getMostRecentBrowserWindow();
    const previousTab =
      lazy.TabManager.getTabBrowser(previousWindow).selectedTab;

    // The type supported varies by platform, as Android can only support one window.
    // As such, type compatibility will need to be checked and will fallback if necessary.
    let type;
    if (
      (typeHint == "tab" && lazy.TabManager.supportsTabs()) ||
      (typeHint == "window" && lazy.windowManager.supportsWindows())
    ) {
      type = typeHint;
    } else if (lazy.TabManager.supportsTabs()) {
      type = "tab";
    } else if (lazy.windowManager.supportsWindows()) {
      type = "window";
    } else {
      throw new lazy.error.UnsupportedOperationError(
        `Not supported in ${lazy.AppInfo.name}`
      );
    }

    let waitForVisibilityChangePromise;
    switch (type) {
      case "window": {
        const newWindow = await lazy.windowManager.openBrowserWindow({
          focus: !background,
          userContextId: userContext,
        });
        browser = lazy.TabManager.getTabBrowser(newWindow).selectedBrowser;
        break;
      }
      case "tab": {
        // The window to open the new tab in.
        let window = Services.wm.getMostRecentBrowserWindow();

        let referenceTab;
        if (referenceContext !== null) {
          referenceTab =
            lazy.TabManager.getTabForBrowsingContext(referenceContext);
          window = lazy.TabManager.getWindowForTab(referenceTab);
        }

        if (!background && !lazy.AppInfo.isAndroid) {
          // When opening a new foreground tab we need to wait until the
          // "document.visibilityState" of the currently selected tab in this
          // window is marked as "hidden".
          //
          // Bug 1884142: It's not supported on Android for the TestRunner package.
          const selectedTab = lazy.TabManager.getTabBrowser(window).selectedTab;

          // Create the promise immediately, but await it later in parallel with
          // waitForInitialNavigationCompleted.
          waitForVisibilityChangePromise = this.#waitForVisibilityChange(
            lazy.TabManager.getBrowserForTab(selectedTab).browsingContext
          );
        }

        const tab = await lazy.TabManager.addTab({
          focus: !background,
          referenceTab,
          userContextId: userContext,
        });
        browser = lazy.TabManager.getBrowserForTab(tab);
      }
    }

    // ConfigurationModule cannot block parsing for initial about:blank load, so we block
    // browsing_context.create till configuration is applied.
    let blocker = this.#blockedCreateCommands.get(browser);
    // If the configuration is done before we have a browser, a resolved blocker already exists.
    if (!blocker) {
      blocker = Promise.withResolvers();
      if (!this.#hasConfigurationForContext(userContext)) {
        blocker.resolve();
      }
      this.#blockedCreateCommands.set(browser, blocker);
    }

    await Promise.all([
      lazy.waitForInitialNavigationCompleted(
        browser.browsingContext.webProgress,
        {
          unloadTimeout: 5000,
        }
      ),
      waitForVisibilityChangePromise,
      blocker.promise,
    ]);

    this.#blockedCreateCommands.delete(browser);

    // The tab on Android is always opened in the foreground,
    // so we need to select the previous tab,
    // and we have to wait until is fully loaded.
    // TODO: Bug 1845559. This workaround can be removed,
    // when the API to create a tab for Android supports the background option.
    if (lazy.AppInfo.isAndroid && background) {
      await lazy.windowManager.focusWindow(previousWindow);
      await lazy.TabManager.selectTab(previousTab);
    }

    // Force a reflow by accessing `clientHeight` (see Bug 1847044).
    browser.parentElement.clientHeight;

    return {
      context: lazy.TabManager.getIdForBrowser(browser),
    };
  }

  /**
   * An object that holds the WebDriver Bidi browsing context information.
   *
   * @typedef BrowsingContextInfo
   *
   * @property {string} context
   *     The id of the browsing context.
   * @property {string=} parent
   *     The parent of the browsing context if it's the root browsing context
   *     of the to be processed browsing context tree.
   * @property {string} url
   *     The current documents location.
   * @property {string} userContext
   *     The id of the user context owning this browsing context.
   * @property {Array<BrowsingContextInfo>=} children
   *     List of child browsing contexts. Only set if maxDepth hasn't been
   *     reached yet.
   * @property {string} clientWindow
   *     The id of the window the browsing context belongs to.
   */

  /**
   * An object that holds the WebDriver Bidi browsing context tree information.
   *
   * @typedef BrowsingContextGetTreeResult
   *
   * @property {Array<BrowsingContextInfo>} contexts
   *     List of child browsing contexts.
   */

  /**
   * Returns a tree of all browsing contexts that are descendents of the
   * given context, or all top-level contexts when no root is provided.
   *
   * @param {object=} options
   * @param {number=} options.maxDepth
   *     Depth of the browsing context tree to traverse. If not specified
   *     the whole tree is returned.
   * @param {string=} options.root
   *     Id of the root browsing context.
   *
   * @returns {BrowsingContextGetTreeResult}
   *     Tree of browsing context information.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  getTree(options = {}) {
    const { maxDepth = null, root: rootId = null } = options;

    if (maxDepth !== null) {
      lazy.assert.positiveInteger(
        maxDepth,
        lazy.pprint`Expected "maxDepth" to be a positive integer, got ${maxDepth}`
      );
    }

    let contexts;
    if (rootId !== null) {
      // With a root id specified return the context info for itself
      // and the full tree.
      lazy.assert.string(
        rootId,
        lazy.pprint`Expected "root" to be a string, got ${rootId}`
      );
      contexts = [this.#getBrowsingContext(rootId)];
    } else {
      // Return all top-level browsing contexts.
      contexts = lazy.TabManager.getBrowsers().map(
        browser => browser.browsingContext
      );
    }

    const contextsInfo = contexts.map(context => {
      return getBrowsingContextInfo(context, { maxDepth });
    });

    return { contexts: contextsInfo };
  }

  /**
   * Closes an open prompt.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context.
   * @param {boolean=} options.accept
   *     Whether user prompt should be accepted or dismissed.
   *     Defaults to true.
   * @param {string=} options.userText
   *     Input to the user prompt's value field.
   *     Defaults to an empty string.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchAlertError}
   *     If there is no current user prompt.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   * @throws {UnsupportedOperationError}
   *     Raised when the command is called for "beforeunload" prompt.
   */
  async handleUserPrompt(options = {}) {
    const { accept = true, context: contextId, userText = "" } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    const context = this.#getBrowsingContext(contextId);

    lazy.assert.boolean(
      accept,
      lazy.pprint`Expected "accept" to be a boolean, got ${accept}`
    );

    lazy.assert.string(
      userText,
      lazy.pprint`Expected "userText" to be a string, got ${userText}`
    );

    const tab = lazy.TabManager.getTabForBrowsingContext(context);
    const browser = lazy.TabManager.getBrowserForTab(tab);
    const window = lazy.TabManager.getWindowForTab(tab);
    const dialog = lazy.modal.findPrompt({
      window,
      contentBrowser: browser,
    });

    const closePrompt = async callback => {
      const dialogClosed = new lazy.EventPromise(
        window,
        "DOMModalDialogClosed"
      );
      callback();
      await dialogClosed;
    };

    if (dialog && dialog.isOpen) {
      switch (dialog.promptType) {
        case UserPromptType.alert:
          await closePrompt(() => dialog.accept());
          return;

        case UserPromptType.beforeunload:
        case UserPromptType.confirm:
          await closePrompt(() => {
            if (accept) {
              dialog.accept();
            } else {
              dialog.dismiss();
            }
          });

          return;

        case UserPromptType.prompt:
          await closePrompt(() => {
            if (accept) {
              dialog.text = userText;
              dialog.accept();
            } else {
              dialog.dismiss();
            }
          });

          return;

        default:
          throw new lazy.error.UnsupportedOperationError(
            `Prompts of type "${dialog.promptType}" are not supported`
          );
      }
    }

    throw new lazy.error.NoSuchAlertError();
  }

  /**
   * Used as an argument for browsingContext.locateNodes command, as one of the available variants
   * {AccessibilityLocator}, {CssLocator}, {InnerTextLocator} or {XPathLocator}, to represent a way of how lookup of nodes
   * is going to be performed.
   *
   * @typedef Locator
   */

  /**
   * Used as a value argument for browsingContext.locateNodes command
   * in case of a lookup by accessibility attributes.
   *
   * @typedef AccessibilityLocatorValue
   *
   * @property {string=} name
   * @property {string=} role
   */

  /**
   * Used as an argument for browsingContext.locateNodes command
   * to represent a lookup by accessibility attributes.
   *
   * @typedef AccessibilityLocator
   *
   * @property {LocatorType} [type=LocatorType.accessibility]
   * @property {AccessibilityLocatorValue} value
   */

  /**
   * Used as an argument for browsingContext.locateNodes command
   * to represent a lookup by css selector.
   *
   * @typedef CssLocator
   *
   * @property {LocatorType} [type=LocatorType.css]
   * @property {string} value
   */

  /**
   * Used as an argument for browsingContext.locateNodes command
   * to represent a lookup by inner text.
   *
   * @typedef InnerTextLocator
   *
   * @property {LocatorType} [type=LocatorType.innerText]
   * @property {string} value
   * @property {boolean=} ignoreCase
   * @property {("full"|"partial")=} matchType
   * @property {number=} maxDepth
   */

  /**
   * Used as an argument for browsingContext.locateNodes command
   * to represent a lookup by xpath.
   *
   * @typedef XPathLocator
   *
   * @property {LocatorType} [type=LocatorType.xpath]
   * @property {string} value
   */

  /**
   * Returns a list of all nodes matching
   * the specified locator.
   *
   * @param {object} options
   * @param {string} options.context
   *     Id of the browsing context.
   * @param {Locator} options.locator
   *     The type of lookup which is going to be used.
   * @param {number=} options.maxNodeCount
   *     The maximum amount of nodes which is going to be returned.
   *     Defaults to return all the found nodes.
   * @property {SerializationOptions=} serializationOptions
   *     An object which holds the information of how the DOM nodes
   *     should be serialized.
   * @property {Array<SharedReference>=} startNodes
   *     A list of references to nodes, which are used as
   *     starting points for lookup.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {InvalidSelectorError}
   *     Raised if a locator value is invalid.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   * @throws {UnsupportedOperationError}
   *     Raised when unsupported lookup types are used.
   */
  async locateNodes(options = {}) {
    const {
      context: contextId,
      locator,
      maxNodeCount = null,
      serializationOptions,
      startNodes = null,
    } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    const context = this.#getBrowsingContext(contextId);

    lazy.assert.object(
      locator,
      lazy.pprint`Expected "locator" to be an object, got ${locator}`
    );

    const locatorTypes = Object.values(LocatorType);

    lazy.assert.that(
      locatorType => locatorTypes.includes(locatorType),
      `Expected "locator.type" to be one of ${locatorTypes}, ` +
        lazy.pprint`got ${locator.type}`
    )(locator.type);

    if (
      [LocatorType.css, LocatorType.innerText, LocatorType.xpath].includes(
        locator.type
      )
    ) {
      lazy.assert.string(
        locator.value,
        `Expected "locator.value" of "locator.type" "${locator.type}" to be a string, ` +
          lazy.pprint`got ${locator.value}`
      );
    }
    if (locator.type == LocatorType.accessibility) {
      lazy.assert.object(
        locator.value,
        `Expected "locator.value" of "locator.type" "${locator.type}" to be an object, ` +
          lazy.pprint`got ${locator.value}`
      );

      const { name = null, role = null } = locator.value;
      if (name !== null) {
        lazy.assert.string(
          locator.value.name,
          `Expected "locator.value.name" of "locator.type" "${locator.type}" to be a string, ` +
            lazy.pprint`got ${name}`
        );
      }
      if (role !== null) {
        lazy.assert.string(
          locator.value.role,
          `Expected "locator.value.role" of "locator.type" "${locator.type}" to be a string, ` +
            lazy.pprint`got ${role}`
        );
      }
    }

    if (
      ![LocatorType.accessibility, LocatorType.css, LocatorType.xpath].includes(
        locator.type
      )
    ) {
      throw new lazy.error.UnsupportedOperationError(
        `"locator.type" argument with value: ${locator.type} is not supported yet.`
      );
    }

    if (maxNodeCount != null) {
      const maxNodeCountErrorMsg = lazy.pprint`Expected "maxNodeCount" to be an integer and greater than 0, got ${maxNodeCount}`;
      lazy.assert.that(maxNodeCount => {
        lazy.assert.integer(maxNodeCount, maxNodeCountErrorMsg);
        return maxNodeCount > 0;
      }, maxNodeCountErrorMsg)(maxNodeCount);
    }

    const serializationOptionsWithDefaults =
      lazy.setDefaultAndAssertSerializationOptions(serializationOptions);

    if (startNodes != null) {
      lazy.assert.isNonEmptyArray(
        startNodes,
        lazy.pprint`Expected "startNodes" to be a non-empty array, got ${startNodes}`
      );
    }

    const result = await this._forwardToWindowGlobal(
      "_locateNodes",
      context.id,
      {
        locator,
        maxNodeCount,
        serializationOptions: serializationOptionsWithDefaults,
        startNodes,
      },
      { retryOnAbort: true }
    );

    return {
      nodes: result.serializedNodes,
    };
  }

  /**
   * An object that holds the WebDriver Bidi navigation information.
   *
   * @typedef BrowsingContextNavigateResult
   *
   * @property {string} navigation
   *     Unique id for this navigation.
   * @property {string} url
   *     The requested or reached URL.
   */

  /**
   * Navigate the given context to the provided url, with the provided wait condition.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context to navigate.
   * @param {string} options.url
   *     Url for the navigation.
   * @param {WaitCondition=} options.wait
   *     Wait condition for the navigation, one of "none", "interactive", "complete".
   *     Defaults to "none".
   *
   * @returns {BrowsingContextNavigateResult}
   *     Navigation result.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context for context cannot be found.
   */
  async navigate(options = {}) {
    const { context: contextId, url, wait = WaitCondition.None } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    lazy.assert.string(
      url,
      lazy.pprint`Expected "url" to be string, got ${url}`
    );

    const waitConditions = Object.values(WaitCondition);
    if (!waitConditions.includes(wait)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "wait" to be one of ${waitConditions}, ` +
          lazy.pprint`got ${wait}`
      );
    }

    const context = this.#getBrowsingContext(contextId);

    // webProgress will be stable even if the context navigates, retrieve it
    // immediately before doing any asynchronous call.
    const webProgress = context.webProgress;

    const base = await this.messageHandler.handleCommand({
      moduleName: "browsingContext",
      commandName: "_getBaseURL",
      destination: {
        type: lazy.WindowGlobalMessageHandler.type,
        id: context.id,
      },
      retryOnAbort: true,
    });

    let targetURI;
    try {
      const baseURI = Services.io.newURI(base);
      targetURI = Services.io.newURI(url, null, baseURI);
    } catch (e) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "url" to be a valid URL (${e.message})`
      );
    }

    return this.#awaitNavigation(
      webProgress,
      () => {
        context.loadURI(targetURI, {
          loadFlags: Ci.nsIWebNavigation.LOAD_FLAGS_IS_LINK,
          // Fake user activation.
          hasValidUserGestureActivation: true,
          // Prevent HTTPS-First upgrades.
          schemelessInput: Ci.nsILoadInfo.SchemelessInputTypeSchemeful,
          triggeringPrincipal:
            Services.scriptSecurityManager.getSystemPrincipal(),
        });
      },
      {
        targetURI,
        wait,
      }
    );
  }

  /**
   * An object that holds the information about margins
   * for Webdriver BiDi browsingContext.print command.
   *
   * @typedef BrowsingContextPrintMarginParameters
   *
   * @property {number=} bottom
   *     Bottom margin in cm. Defaults to 1cm (~0.4 inches).
   * @property {number=} left
   *     Left margin in cm. Defaults to 1cm (~0.4 inches).
   * @property {number=} right
   *     Right margin in cm. Defaults to 1cm (~0.4 inches).
   * @property {number=} top
   *     Top margin in cm. Defaults to 1cm (~0.4 inches).
   */

  /**
   * An object that holds the information about paper size
   * for Webdriver BiDi browsingContext.print command.
   *
   * @typedef BrowsingContextPrintPageParameters
   *
   * @property {number=} height
   *     Paper height in cm. Defaults to US letter height (27.94cm / 11 inches).
   * @property {number=} width
   *     Paper width in cm. Defaults to US letter width (21.59cm / 8.5 inches).
   */

  /**
   * Used as return value for Webdriver BiDi browsingContext.print command.
   *
   * @typedef BrowsingContextPrintResult
   *
   * @property {string} data
   *     Base64 encoded PDF representing printed document.
   */

  /**
   * Creates a paginated PDF representation of a document
   * of the provided browsing context, and returns it
   * as a Base64-encoded string.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context.
   * @param {boolean=} options.background
   *     Whether or not to print background colors and images.
   *     Defaults to false, which prints without background graphics.
   * @param {BrowsingContextPrintMarginParameters=} options.margin
   *     Paper margins.
   * @param {('landscape'|'portrait')=} options.orientation
   *     Paper orientation. Defaults to 'portrait'.
   * @param {BrowsingContextPrintPageParameters=} options.page
   *     Paper size.
   * @param {Array<number|string>=} options.pageRanges
   *     Paper ranges to print, e.g., ['1-5', 8, '11-13'].
   *     Defaults to the empty array, which means print all pages.
   * @param {number=} options.scale
   *     Scale of the webpage rendering. Defaults to 1.0.
   * @param {boolean=} options.shrinkToFit
   *     Whether or not to override page size as defined by CSS.
   *     Defaults to true, in which case the content will be scaled
   *     to fit the paper size.
   *
   * @returns {BrowsingContextPrintResult}
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  async print(options = {}) {
    const {
      context: contextId,
      background,
      margin,
      orientation,
      page,
      pageRanges,
      scale,
      shrinkToFit,
    } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );
    const context = this.#getBrowsingContext(contextId);

    const settings = lazy.print.addDefaultSettings({
      background,
      margin,
      orientation,
      page,
      pageRanges,
      scale,
      shrinkToFit,
    });

    for (const prop of ["top", "bottom", "left", "right"]) {
      lazy.assert.positiveNumber(
        settings.margin[prop],
        `Expected "margin.${prop}" to be a positive number, ` +
          lazy.pprint`got ${settings.margin[prop]}`
      );
    }
    for (const prop of ["width", "height"]) {
      lazy.assert.positiveNumber(
        settings.page[prop],
        `Expected "page.${prop}" to be a positive number, ` +
          lazy.pprint`got ${settings.page[prop]}`
      );
    }
    lazy.assert.positiveNumber(
      settings.scale,
      `Expected "scale" to be a positive number, ` +
        lazy.pprint`got ${settings.scale}`
    );
    lazy.assert.that(
      scale =>
        scale >= lazy.print.minScaleValue && scale <= lazy.print.maxScaleValue,
      `scale ${settings.scale} is outside the range ${lazy.print.minScaleValue}-${lazy.print.maxScaleValue}`
    )(settings.scale);
    lazy.assert.boolean(
      settings.shrinkToFit,
      lazy.pprint`Expected "shrinkToFit" to be a boolean, got ${settings.shrinkToFit}`
    );
    lazy.assert.that(
      orientation => lazy.print.defaults.orientationValue.includes(orientation),
      `Expected "orientation" to be one of ${lazy.print.defaults.orientationValue}", ` +
        lazy.pprint`got {settings.orientation}`
    )(settings.orientation);
    lazy.assert.boolean(
      settings.background,
      lazy.pprint`Expected "background" to be a boolean, got ${settings.background}`
    );
    lazy.assert.array(
      settings.pageRanges,
      lazy.pprint`Expected "pageRanges" to be an array, got ${settings.pageRanges}`
    );

    const printSettings = await lazy.print.getPrintSettings(settings);
    const binaryString = await lazy.print.printToBinaryString(
      context,
      printSettings
    );

    return {
      data: btoa(binaryString),
    };
  }

  /**
   * Reload the given context's document, with the provided wait condition.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context to navigate.
   * @param {bool=} options.ignoreCache
   *     If true ignore the browser cache. [Not yet supported]
   * @param {WaitCondition=} options.wait
   *     Wait condition for the navigation, one of "none", "interactive", "complete".
   *     Defaults to "none".
   *
   * @returns {BrowsingContextNavigateResult}
   *     Navigation result.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameError}
   *     If the browsing context for context cannot be found.
   */
  async reload(options = {}) {
    const {
      context: contextId,
      ignoreCache,
      wait = WaitCondition.None,
    } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    if (typeof ignoreCache != "undefined") {
      throw new lazy.error.UnsupportedOperationError(
        `Argument "ignoreCache" is not supported yet.`
      );
    }

    const waitConditions = Object.values(WaitCondition);
    if (!waitConditions.includes(wait)) {
      throw new lazy.error.InvalidArgumentError(
        `Expected "wait" to be one of ${waitConditions}, ` +
          lazy.pprint`got ${wait}`
      );
    }

    const context = this.#getBrowsingContext(contextId);

    // webProgress will be stable even if the context navigates, retrieve it
    // immediately before doing any asynchronous call.
    const webProgress = context.webProgress;

    return this.#awaitNavigation(
      webProgress,
      () => {
        context.reload(Ci.nsIWebNavigation.LOAD_FLAGS_NONE);
      },
      { wait }
    );
  }

  /**
   * Set the top-level browsing context's viewport to a given dimension.
   *
   * @param {object=} options
   * @param {string=} options.context
   *     Id of the browsing context.
   * @param {(number|null)=} options.devicePixelRatio
   *     A value to override device pixel ratio, or `null` to reset it to
   *     the original value. Different values will not cause the rendering to change,
   *     only image srcsets and media queries will be applied as if DPR is redefined.
   * @param {(Viewport|null)=} options.viewport
   *     Dimensions to set the viewport to, or `null` to reset it
   *     to the original dimensions.
   * @param {Array<string>=} options.userContexts
   *     Optional list of user context ids.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {UnsupportedOperationError}
   *     Raised when the command is called on Android.
   */
  async setViewport(options = {}) {
    const {
      context: contextId = null,
      devicePixelRatio,
      viewport,
      userContexts: userContextIds = null,
    } = options;

    const userContexts = new Set();

    if (contextId !== null) {
      lazy.assert.string(
        contextId,
        lazy.pprint`Expected "context" to be a string, got ${contextId}`
      );
    } else if (userContextIds !== null) {
      lazy.assert.isNonEmptyArray(
        userContextIds,
        lazy.pprint`Expected "userContexts" to be a non-empty array, got ${userContextIds}`
      );

      for (const userContextId of userContextIds) {
        lazy.assert.string(
          userContextId,
          lazy.pprint`Expected elements of "userContexts" to be a string, got ${userContextId}`
        );

        const internalId =
          lazy.UserContextManager.getInternalIdById(userContextId);

        if (internalId === null) {
          throw new lazy.error.NoSuchUserContextError(
            `User context with id: ${userContextId} doesn't exist`
          );
        }

        userContexts.add(internalId);
      }
    } else {
      throw new lazy.error.InvalidArgumentError(
        `At least one of "context" or "userContexts" arguments should be provided`
      );
    }

    if (contextId !== null && userContextIds !== null) {
      throw new lazy.error.InvalidArgumentError(
        `Providing both "context" and "userContexts" arguments is not supported`
      );
    }

    if (viewport !== undefined && viewport !== null) {
      lazy.assert.object(
        viewport,
        lazy.pprint`Expected "viewport" to be an object, got ${viewport}`
      );

      const { height, width } = viewport;
      lazy.assert.positiveInteger(
        height,
        lazy.pprint`Expected viewport's "height" to be a positive integer, got ${height}`
      );
      lazy.assert.positiveInteger(
        width,
        lazy.pprint`Expected viewport's "width" to be a positive integer, got ${width}`
      );

      if (height > MAX_WINDOW_SIZE || width > MAX_WINDOW_SIZE) {
        throw new lazy.error.UnsupportedOperationError(
          `"width" or "height" cannot be larger than ${MAX_WINDOW_SIZE} px`
        );
      }
    }

    if (devicePixelRatio !== undefined && devicePixelRatio !== null) {
      lazy.assert.number(
        devicePixelRatio,
        lazy.pprint`Expected "devicePixelRatio" to be a number or null, got ${devicePixelRatio}`
      );
      lazy.assert.that(
        value => value > 0,
        lazy.pprint`Expected "devicePixelRatio" to be greater than 0, got ${devicePixelRatio}`
      )(devicePixelRatio);
    }

    const navigables = new Set();
    if (contextId !== null) {
      const navigable = this.#getBrowsingContext(contextId);
      lazy.assert.topLevel(
        navigable,
        `Browsing context with id ${contextId} is not top-level`
      );

      navigables.add(navigable);
    }

    if (lazy.AppInfo.isAndroid) {
      // Bug 1840084: Add Android support for modifying the viewport.
      throw new lazy.error.UnsupportedOperationError(
        `Command not yet supported for ${lazy.AppInfo.name}`
      );
    }

    const viewportOverride = {
      devicePixelRatio,
      viewport,
    };

    const sessionDataItems = [];
    if (userContextIds !== null) {
      for (const userContext of userContexts) {
        // Prepare the list of navigables to update.
        lazy.UserContextManager.getTabsForUserContext(userContext).forEach(
          tab => {
            const contentBrowser = lazy.TabManager.getBrowserForTab(tab);
            navigables.add(contentBrowser.browsingContext);
          }
        );
        sessionDataItems.push({
          category: "viewport-overrides",
          moduleName: "_configuration",
          values: [viewportOverride],
          contextDescriptor: {
            type: lazy.ContextDescriptorType.UserContext,
            id: userContext,
          },
          method: lazy.SessionDataMethod.Add,
        });
      }
    } else {
      for (const navigable of navigables) {
        sessionDataItems.push({
          category: "viewport-overrides",
          moduleName: "_configuration",
          values: [viewportOverride],
          contextDescriptor: {
            type: lazy.ContextDescriptorType.TopBrowsingContext,
            id: navigable.browserId,
          },
          method: lazy.SessionDataMethod.Add,
        });
      }
    }

    if (sessionDataItems.length) {
      // TODO: Bug 1953079. Saving the viewport overrides in the session data works fine
      // with one session, but when we start supporting multiple BiDi session, we will
      // have to rethink this approach.
      await this.messageHandler.updateSessionData(sessionDataItems);
    }

    const commands = [];

    for (const navigable of navigables) {
      commands.push(
        this._updateNavigableViewport({
          navigable,
          viewportOverride,
        })
      );
    }

    await Promise.all(commands);
  }

  /**
   * Traverses the history of a given context by a given delta.
   *
   * @param {object=} options
   * @param {string} options.context
   *     Id of the browsing context.
   * @param {number} options.delta
   *     The number of steps we have to traverse.
   *
   * @throws {InvalidArgumentError}
   *     Raised if an argument is of an invalid type or value.
   * @throws {NoSuchFrameException}
   *     When a context is not available.
   * @throws {NoSuchHistoryEntryError}
   *     When a requested history entry does not exist.
   */
  async traverseHistory(options = {}) {
    const { context: contextId, delta } = options;

    lazy.assert.string(
      contextId,
      lazy.pprint`Expected "context" to be a string, got ${contextId}`
    );

    const context = this.#getBrowsingContext(contextId);

    lazy.assert.topLevel(
      context,
      lazy.pprint`Browsing context with id ${contextId} is not top-level`
    );

    lazy.assert.integer(
      delta,
      lazy.pprint`Expected "delta" to be an integer, got ${delta}`
    );

    const sessionHistory = context.sessionHistory;
    const allSteps = sessionHistory.count;
    const currentIndex = sessionHistory.index;
    const targetIndex = currentIndex + delta;
    const validEntry = targetIndex >= 0 && targetIndex < allSteps;

    if (!validEntry) {
      throw new lazy.error.NoSuchHistoryEntryError(
        `History entry with delta ${delta} not found`
      );
    }

    context.goToIndex(targetIndex);

    // On some platforms the requested index isn't set immediately.
    await lazy.PollPromise(
      (resolve, reject) => {
        if (sessionHistory.index == targetIndex) {
          resolve();
        } else {
          reject();
        }
      },
      {
        errorMessage: `History was not updated for index "${targetIndex}"`,
        timeout: TIMEOUT_SET_HISTORY_INDEX * lazy.getTimeoutMultiplier(),
      }
    );
  }

  /**
   * Start and await a navigation on the provided BrowsingContext. Returns a
   * promise which resolves when the navigation is done according to the provided
   * navigation strategy.
   *
   * @param {WebProgress} webProgress
   *     The WebProgress instance to observe for this navigation.
   * @param {Function} startNavigationFn
   *     A callback that starts a navigation.
   * @param {object} options
   * @param {string=} options.targetURI
   *     The target URI for the navigation.
   * @param {WaitCondition} options.wait
   *     The WaitCondition to use to wait for the navigation.
   *
   * @returns {Promise<BrowsingContextNavigateResult>}
   *     A Promise that resolves to navigate results when the navigation is done.
   */
  async #awaitNavigation(webProgress, startNavigationFn, options) {
    const { targetURI, wait } = options;

    const context = webProgress.browsingContext;
    const browserId = context.browserId;

    const resolveWhenCommitted = wait === WaitCondition.None;
    const listener = new lazy.ProgressListener(webProgress, {
      expectNavigation: true,
      navigationManager: this.messageHandler.navigationManager,
      resolveWhenCommitted,
      targetURI,
      // In case the webprogress is already navigating, always wait for an
      // explicit start flag.
      waitForExplicitStart: true,
    });

    const onDocumentInteractive = (evtName, wrappedEvt) => {
      if (webProgress.browsingContext.id !== wrappedEvt.contextId) {
        // Ignore load events for unrelated browsing contexts.
        return;
      }

      if (wrappedEvt.readyState === "interactive") {
        listener.stopIfStarted();
      }
    };

    const contextDescriptor = {
      type: lazy.ContextDescriptorType.TopBrowsingContext,
      id: browserId,
    };

    // For the Interactive wait condition, resolve as soon as
    // the document becomes interactive.
    if (wait === WaitCondition.Interactive) {
      await this.messageHandler.eventsDispatcher.on(
        "browsingContext._documentInteractive",
        contextDescriptor,
        onDocumentInteractive
      );
    }

    const navigationId = lazy.registerNavigationId({
      contextDetails: { context: webProgress.browsingContext },
    });
    const navigated = listener.start(navigationId);

    try {
      await startNavigationFn();
      await navigated;

      let url;
      if (wait === WaitCondition.None) {
        // If wait condition is None, the navigation resolved before the current
        // context has navigated.
        url = listener.targetURI.spec;
      } else {
        url = listener.currentURI.spec;
      }

      return {
        navigation: navigationId,
        url,
      };
    } finally {
      if (listener.isStarted) {
        listener.stop();
      }
      listener.destroy();

      if (wait === WaitCondition.Interactive) {
        await this.messageHandler.eventsDispatcher.off(
          "browsingContext._documentInteractive",
          contextDescriptor,
          onDocumentInteractive
        );
      }
    }
  }

  /**
   * Wrapper around RootBiDiModule._emitEventForBrowsingContext to additionally
   * check that the payload of the event contains a valid `context` id.
   *
   * All browsingContext module events should have such a property set, and a
   * missing id usually indicates that the browsing context which triggered the
   * event is out of scope for the current WebDriver BiDi session (eg. chrome or
   * webextension).
   *
   * @param {string} browsingContextId
   *     The ID of the browsing context to which the event should be emitted.
   * @param {string} eventName
   *     The name of the event to be emitted.
   * @param {object} eventPayload
   *     The payload to be sent with the event.
   * @param {number|string} eventPayload.context
   *     A unique context id computed by the TabManager.
   */
  #emitContextEventForBrowsingContext(
    browsingContextId,
    eventName,
    eventPayload
  ) {
    // All browsingContext events should include a context id in the payload.
    const { context = null } = eventPayload;
    if (context === null) {
      // If the context could not be found by the TabManager, the event is most
      // likely related to an unsupported context: eg chrome (bug 1722679) or
      // webextension (bug 1755014).
      lazy.logger.trace(
        `[${browsingContextId}] Skipping event ${eventName} because of a missing unique context id`
      );
      return;
    }

    this._emitEventForBrowsingContext(
      browsingContextId,
      eventName,
      eventPayload
    );
  }

  /**
   * Retrieves a browsing context based on its id.
   *
   * @param {number} contextId
   *     Id of the browsing context.
   * @returns {BrowsingContext=}
   *     The browsing context or null if <var>contextId</var> is null.
   * @throws {NoSuchFrameError}
   *     If the browsing context cannot be found.
   */
  #getBrowsingContext(contextId) {
    // The WebDriver BiDi specification expects null to be
    // returned if no browsing context id has been specified.
    if (contextId === null) {
      return null;
    }

    const context = lazy.TabManager.getBrowsingContextById(contextId);
    if (context === null) {
      throw new lazy.error.NoSuchFrameError(
        `Browsing Context with id ${contextId} not found`
      );
    }

    return context;
  }

  #hasConfigurationForContext(userContext) {
    const internalId = lazy.UserContextManager.getInternalIdById(userContext);
    const contextDescriptor = {
      type: lazy.ContextDescriptorType.UserContext,
      id: internalId,
    };
    return this.messageHandler.sessionData.hasSessionData(
      "_configuration",
      undefined,
      contextDescriptor
    );
  }

  #onContextAttached = async (eventName, data = {}) => {
    if (this.#subscribedEvents.has("browsingContext.contextCreated")) {
      const { browsingContext, why } = data;

      // Filter out top-level browsing contexts that are created because of a
      // cross-group navigation.
      if (why === "replace") {
        return;
      }

      // TODO: Bug 1852941. We should also filter out events which are emitted
      // for DevTools frames.

      // Filter out notifications for chrome context until support gets
      // added (bug 1722679).
      if (!browsingContext.webProgress) {
        return;
      }

      // Filter out notifications for webextension contexts until support gets
      // added (bug 1755014).
      if (browsingContext.currentRemoteType === "extension") {
        return;
      }

      const browsingContextInfo = getBrowsingContextInfo(browsingContext, {
        maxDepth: 0,
      });

      this.#emitContextEventForBrowsingContext(
        browsingContext.id,
        "browsingContext.contextCreated",
        browsingContextInfo
      );
    }
  };

  #onContextDiscarded = async (eventName, data = {}) => {
    if (this.#subscribedEvents.has("browsingContext.contextDestroyed")) {
      const { browsingContext, why } = data;

      // Filter out top-level browsing contexts that are destroyed because of a
      // cross-group navigation.
      if (why === "replace") {
        return;
      }

      // TODO: Bug 1852941. We should also filter out events which are emitted
      // for DevTools frames.

      // Filter out notifications for chrome context until support gets
      // added (bug 1722679).
      if (!browsingContext.webProgress) {
        return;
      }

      // Filter out notifications for webextension contexts until support gets
      // added (bug 1755014).
      if (browsingContext.currentRemoteType === "extension") {
        return;
      }

      // If this event is for a child context whose top or parent context is also destroyed,
      // we don't need to send it, in this case the event for the top/parent context is enough.
      if (
        browsingContext.parent &&
        (browsingContext.top.isDiscarded || browsingContext.parent.isDiscarded)
      ) {
        return;
      }

      const browsingContextInfo = getBrowsingContextInfo(browsingContext);

      this.#emitContextEventForBrowsingContext(
        browsingContext.id,
        "browsingContext.contextDestroyed",
        browsingContextInfo
      );
    }
  };

  #onFragmentNavigated = async (eventName, data) => {
    const { navigationId, navigableId, url } = data;
    const context = this.#getBrowsingContext(navigableId);

    if (this.#subscribedEvents.has("browsingContext.fragmentNavigated")) {
      const browsingContextInfo = {
        context: navigableId,
        navigation: navigationId,
        timestamp: Date.now(),
        url,
      };
      this.#emitContextEventForBrowsingContext(
        context.id,
        "browsingContext.fragmentNavigated",
        browsingContextInfo
      );
    }
  };

  #onHistoryUpdated = async (eventName, data) => {
    const { navigableId, url } = data;
    const context = this.#getBrowsingContext(navigableId);

    if (this.#subscribedEvents.has("browsingContext.historyUpdated")) {
      const browsingContextInfo = {
        context: navigableId,
        timestamp: Date.now(),
        url,
      };
      this.#emitContextEventForBrowsingContext(
        context.id,
        "browsingContext.historyUpdated",
        browsingContextInfo
      );
    }
  };

  #onPromptClosed = async (eventName, data) => {
    if (this.#subscribedEvents.has("browsingContext.userPromptClosed")) {
      const { contentBrowser, detail } = data;
      const navigableId = lazy.TabManager.getIdForBrowser(contentBrowser);

      if (navigableId === null) {
        return;
      }

      const params = {
        context: navigableId,
        accepted: detail.accepted,
        type: detail.promptType,
        userText: detail.userText,
      };
      this.#emitContextEventForBrowsingContext(
        contentBrowser.browsingContext.id,
        "browsingContext.userPromptClosed",
        params
      );
    }
  };

  #onPromptOpened = async (eventName, data) => {
    if (this.#subscribedEvents.has("browsingContext.userPromptOpened")) {
      const { contentBrowser, prompt } = data;
      const type = prompt.promptType;

      // Do not send opened event for unsupported prompt types.
      if (!(type in UserPromptType)) {
        lazy.logger.trace(`Prompt type "${type}" not supported`);
        return;
      }

      const navigableId = lazy.TabManager.getIdForBrowser(contentBrowser);

      const session = lazy.getWebDriverSessionById(
        this.messageHandler.sessionId
      );
      const handlerConfig = session.userPromptHandler.getPromptHandler(type);

      const eventPayload = {
        context: navigableId,
        handler: handlerConfig.handler,
        message: await prompt.getText(),
        type,
      };

      if (type === "prompt") {
        eventPayload.defaultValue = await prompt.getInputText();
      }

      this.#emitContextEventForBrowsingContext(
        contentBrowser.browsingContext.id,
        "browsingContext.userPromptOpened",
        eventPayload
      );
    }
  };

  #onNavigationCommitted = async (eventName, data) => {
    const { navigableId, navigationId, url, contextId } = data;

    if (this.#subscribedEvents.has("browsingContext.navigationCommitted")) {
      const eventPayload = {
        context: navigableId,
        navigation: navigationId,
        timestamp: Date.now(),
        url,
      };
      this.#emitContextEventForBrowsingContext(
        contextId,
        "browsingContext.navigationCommitted",
        eventPayload
      );
    }
  };

  #onNavigationFailed = async (eventName, data) => {
    const { navigableId, navigationId, url, contextId } = data;

    if (this.#subscribedEvents.has("browsingContext.navigationFailed")) {
      const eventPayload = {
        context: navigableId,
        navigation: navigationId,
        timestamp: Date.now(),
        url,
      };
      this.#emitContextEventForBrowsingContext(
        contextId,
        "browsingContext.navigationFailed",
        eventPayload
      );
    }
  };

  #onNavigationStarted = async (eventName, data) => {
    const { navigableId, navigationId, url } = data;
    const context = this.#getBrowsingContext(navigableId);

    if (this.#subscribedEvents.has("browsingContext.navigationStarted")) {
      const eventPayload = {
        context: navigableId,
        navigation: navigationId,
        timestamp: Date.now(),
        url,
      };
      this.#emitContextEventForBrowsingContext(
        context.id,
        "browsingContext.navigationStarted",
        eventPayload
      );
    }
  };

  #onPageHideEvent = (name, eventPayload) => {
    const { context } = eventPayload;
    if (context.parent) {
      this.#onContextDiscarded("windowglobal-pagehide", {
        browsingContext: context,
      });
    }
  };

  #stopListeningToContextEvent(event) {
    this.#subscribedEvents.delete(event);

    const hasContextEvent =
      this.#subscribedEvents.has("browsingContext.contextCreated") ||
      this.#subscribedEvents.has("browsingContext.contextDestroyed");

    if (!hasContextEvent) {
      this.#contextListener.stopListening();
    }
  }

  #stopListeningToNavigationEvent(event) {
    this.#subscribedEvents.delete(event);

    const hasNavigationEvent =
      this.#subscribedEvents.has("browsingContext.fragmentNavigated") ||
      this.#subscribedEvents.has("browsingContext.historyUpdated") ||
      this.#subscribedEvents.has("browsingContext.navigationFailed") ||
      this.#subscribedEvents.has("browsingContext.navigationStarted");

    if (!hasNavigationEvent) {
      this.#navigationListener.stopListening();
    }
  }

  #stopListeningToPromptEvent(event) {
    this.#subscribedEvents.delete(event);

    const hasPromptEvent =
      this.#subscribedEvents.has("browsingContext.userPromptClosed") ||
      this.#subscribedEvents.has("browsingContext.userPromptOpened");

    if (!hasPromptEvent) {
      this.#promptListener.stopListening();
    }
  }

  #subscribeEvent(event) {
    switch (event) {
      case "browsingContext.contextCreated":
      case "browsingContext.contextDestroyed": {
        this.#contextListener.startListening();
        this.#subscribedEvents.add(event);
        break;
      }
      case "browsingContext.fragmentNavigated":
      case "browsingContext.historyUpdated":
      case "browsingContext.navigationCommitted":
      case "browsingContext.navigationFailed":
      case "browsingContext.navigationStarted": {
        this.#navigationListener.startListening();
        this.#subscribedEvents.add(event);
        break;
      }
      case "browsingContext.userPromptClosed":
      case "browsingContext.userPromptOpened": {
        this.#promptListener.startListening();
        this.#subscribedEvents.add(event);
        break;
      }
    }
  }

  #unsubscribeEvent(event) {
    switch (event) {
      case "browsingContext.contextCreated":
      case "browsingContext.contextDestroyed": {
        this.#stopListeningToContextEvent(event);
        break;
      }
      case "browsingContext.fragmentNavigated":
      case "browsingContext.historyUpdated":
      case "browsingContext.navigationCommitted":
      case "browsingContext.navigationFailed":
      case "browsingContext.navigationStarted": {
        this.#stopListeningToNavigationEvent(event);
        break;
      }
      case "browsingContext.userPromptClosed":
      case "browsingContext.userPromptOpened": {
        this.#stopListeningToPromptEvent(event);
        break;
      }
    }
  }

  #waitForVisibilityChange(browsingContext) {
    return this._forwardToWindowGlobal(
      "_awaitVisibilityState",
      browsingContext.id,
      { value: "hidden" },
      { retryOnAbort: true }
    );
  }

  /**
   * Internal commands
   */

  _applySessionData(params) {
    // TODO: Bug 1775231. Move this logic to a shared module or an abstract
    // class.
    const { category } = params;
    if (category === "event") {
      const filteredSessionData = params.sessionData.filter(item =>
        this.messageHandler.matchesContext(item.contextDescriptor)
      );
      for (const event of this.#subscribedEvents.values()) {
        const hasSessionItem = filteredSessionData.some(
          item => item.value === event
        );
        // If there are no session items for this context, we should unsubscribe from the event.
        if (!hasSessionItem) {
          this.#unsubscribeEvent(event);
        }
      }

      // Subscribe to all events, which have an item in SessionData.
      for (const { value } of filteredSessionData) {
        this.#subscribeEvent(value);
      }
    }
  }

  /**
   * Communicate to this module that the _ConfigurationModule is done.
   *
   * @param {BrowsingContext} navigable
   *     Browsing context for which the configuration completed.
   */
  _onConfigurationComplete({ navigable }) {
    const browser = navigable.embedderElement;

    if (!this.#blockedCreateCommands.has(browser)) {
      this.#blockedCreateCommands.set(browser, Promise.withResolvers());
    }

    const blocker = this.#blockedCreateCommands.get(browser);
    blocker.resolve();
  }

  /**
   * Update the viewport of the navigable.
   *
   * @param {object} options
   * @param {BrowsingContext} options.navigable
   *     Navigable whose viewport should be updated.
   * @param {ViewportOverride} options.viewportOverride
   *     Object which holds viewport settings
   *     which should be applied.
   */
  async _updateNavigableViewport(options) {
    const { navigable, viewportOverride } = options;
    const { devicePixelRatio, viewport } = viewportOverride;

    const browser = navigable.embedderElement;
    const currentHeight = browser.clientHeight;
    const currentWidth = browser.clientWidth;

    let targetHeight, targetWidth;
    if (viewport === undefined) {
      // Don't modify the viewport's size.
      targetHeight = currentHeight;
      targetWidth = currentWidth;
    } else if (viewport === null) {
      // Reset viewport to the original dimensions.
      targetHeight = browser.parentElement.clientHeight;
      targetWidth = browser.parentElement.clientWidth;

      browser.style.removeProperty("height");
      browser.style.removeProperty("width");
    } else {
      const { height, width } = viewport;

      targetHeight = height;
      targetWidth = width;

      browser.style.setProperty("height", targetHeight + "px");
      browser.style.setProperty("width", targetWidth + "px");
    }

    if (devicePixelRatio !== undefined) {
      if (devicePixelRatio !== null) {
        navigable.overrideDPPX = devicePixelRatio;
      } else {
        // Will reset to use the global default scaling factor.
        navigable.overrideDPPX = 0;
      }
    }

    if (targetHeight !== currentHeight || targetWidth !== currentWidth) {
      if (!navigable.isActive) {
        // Force a synchronous update of the remote browser dimensions so that
        // background tabs get resized.
        browser.ownerDocument.synchronouslyUpdateRemoteBrowserDimensions(
          /* aIncludeInactive = */ true
        );
      }
      // Wait until the viewport has been resized
      await this._forwardToWindowGlobal(
        "_awaitViewportDimensions",
        navigable.id,
        {
          height: targetHeight,
          width: targetWidth,
        },
        { retryOnAbort: true }
      );
    }
  }

  static get supportedEvents() {
    return [
      "browsingContext.contextCreated",
      "browsingContext.contextDestroyed",
      "browsingContext.domContentLoaded",
      "browsingContext.fragmentNavigated",
      "browsingContext.historyUpdated",
      "browsingContext.load",
      "browsingContext.navigationCommitted",
      "browsingContext.navigationFailed",
      "browsingContext.navigationStarted",
      "browsingContext.userPromptClosed",
      "browsingContext.userPromptOpened",
    ];
  }
}

/**
 * Get the WebDriver BiDi browsing context information.
 *
 * @param {BrowsingContext} context
 *     The browsing context to get the information from.
 * @param {object=} options
 * @param {boolean=} options.includeParentId
 *     Flag that indicates if the parent ID should be included.
 *     Defaults to true.
 * @param {number=} options.maxDepth
 *     Depth of the browsing context tree to traverse. If not specified
 *     the whole tree is returned.
 * @returns {BrowsingContextInfo}
 *     The information about the browsing context.
 */
export const getBrowsingContextInfo = (context, options = {}) => {
  const { includeParentId = true, maxDepth = null } = options;

  let children = null;
  if (maxDepth === null || maxDepth > 0) {
    children = context.children.map(context =>
      getBrowsingContextInfo(context, {
        maxDepth: maxDepth === null ? maxDepth : maxDepth - 1,
        includeParentId: false,
      })
    );
  }

  const userContext = lazy.UserContextManager.getIdByBrowsingContext(context);
  const originalOpener =
    context.crossGroupOpener !== null
      ? lazy.TabManager.getIdForBrowsingContext(context.crossGroupOpener)
      : null;
  const contextInfo = {
    children,
    context: lazy.TabManager.getIdForBrowsingContext(context),
    // TODO: Bug 1904641. If a browsing context was not tracked in TabManager,
    // because it was created and discarded before the WebDriver BiDi session was
    // started, we get undefined as id for this browsing context.
    // We should remove this condition, when we can provide a correct id here.
    originalOpener: originalOpener === undefined ? null : originalOpener,
    url: context.currentURI.spec,
    userContext,
    clientWindow: lazy.windowManager.getIdForBrowsingContext(context),
  };

  if (includeParentId) {
    // Only emit the parent id for the top-most browsing context.
    const parentId = lazy.TabManager.getIdForBrowsingContext(context.parent);
    contextInfo.parent = parentId;
  }

  return contextInfo;
};

export const browsingContext = BrowsingContextModule;
