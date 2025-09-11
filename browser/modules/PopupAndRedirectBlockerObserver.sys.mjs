/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SitePermissions: "resource:///modules/SitePermissions.sys.mjs",
});

export var PopupAndRedirectBlockerObserver = {
  /**
   * This is to check if we are currently in the process of appending a
   * notification.
   * `NotificationBox.appendNotification()` runs asynchronously and
   * returns a promise. While it is resolving, `NotificationBox.getNotificationWithValue()`
   * will still return null.
   */
  mNotificationPromise: null,

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "DOMUpdateBlockedPopups":
        this.onDOMUpdateBlockedPopupsAndRedirect(aEvent);
        break;
      case "DOMUpdateBlockedRedirect":
        this.onDOMUpdateBlockedPopupsAndRedirect(aEvent);
        break;
      case "command":
        this.onCommand(aEvent);
        break;
      case "popupshowing":
        this.onPopupShowing(aEvent);
        break;
      case "popuphiding":
        this.onPopupHiding(aEvent);
        break;
    }
  },

  /**
   * Handles a "DOMUpdateBlockedPopups" or "DOMUpdateBlockedRedirect" event
   * received from the JSWindowActorParent.
   * @param {*} aEvent
   */
  onDOMUpdateBlockedPopupsAndRedirect(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { gBrowser, gPermissionPanel } = window;
    if (aEvent.originalTarget != gBrowser.selectedBrowser) {
      return;
    }

    gPermissionPanel.refreshPermissionIcons();

    const popupCount =
      gBrowser.selectedBrowser.popupAndRedirectBlocker.getBlockedPopupCount();
    const isRedirectBlocked =
      gBrowser.selectedBrowser.popupAndRedirectBlocker.isRedirectBlocked();
    if (!popupCount && !isRedirectBlocked) {
      this.hideNotification(gBrowser);
      return;
    }

    if (Services.prefs.getBoolPref("privacy.popups.showBrowserMessage")) {
      this.ensureInitializedForWindow(window);
      this.showBrowserMessage(gBrowser, popupCount, isRedirectBlocked);
    }
  },

  hideNotification(aBrowser) {
    const notificationBox = aBrowser.getNotificationBox();
    const notification =
      notificationBox.getNotificationWithValue("popup-blocked");
    if (notification) {
      notificationBox.removeNotification(notification);
    }
  },

  ensureInitializedForWindow(aWindow) {
    const popup = aWindow.document.getElementById("blockedPopupOptions");
    // Make sure we don't add the same event handlers multiple times.
    if (popup.getAttribute("initialized")) {
      return;
    }

    popup.setAttribute("initialized", true);
    popup.addEventListener("command", this);
    popup.addEventListener("popupshowing", this);
    popup.addEventListener("popuphiding", this);
  },

  async showBrowserMessage(aBrowser, aPopupCount, aIsRedirectBlocked) {
    const l10nId = (() => {
      if (aPopupCount >= this.maxReportedPopups) {
        return aIsRedirectBlocked
          ? "popup-warning-exceeded-with-redirect-message"
          : "popup-warning-exceeded-message";
      }

      return aIsRedirectBlocked
        ? "redirect-warning-with-popup-message"
        : "popup-warning-message";
    })();
    const label = {
      "l10n-id": l10nId,
      "l10n-args": {
        popupCount: aPopupCount,
      },
    };
    const notificationBox = aBrowser.getNotificationBox();
    const notification = this.mNotificationPromise
      ? await this.mNotificationPromise
      : notificationBox.getNotificationWithValue("popup-blocked");
    if (notification) {
      notification.label = label;
      return;
    }

    const image = "chrome://browser/skin/notification-icons/popup.svg";
    const priority = notificationBox.PRIORITY_INFO_MEDIUM;
    this.mNotificationPromise = notificationBox.appendNotification(
      "popup-blocked",
      { label, image, priority },
      [
        {
          "l10n-id": "popup-warning-button",
          popup: "blockedPopupOptions",
          callback: null,
        },
      ]
    );
    await this.mNotificationPromise;
    this.mNotificationPromise = null;
  },

  /**
   * Event handler that is triggered when a user clicks on the "options"
   * button in the notification which opens a popup.
   * @param {*} aEvent
   */
  async onPopupShowing(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { gBrowser, document } = window;

    // "Allow pop-ups for site..."
    const blockedPopupAllowSite = document.getElementById(
      "blockedPopupAllowSite"
    );
    blockedPopupAllowSite.removeAttribute("hidden");
    document.l10n.setAttributes(
      blockedPopupAllowSite,
      "popups-infobar-allow2",
      { uriHost: gBrowser.currentURI.displayHost }
    );

    // "Dont show this message when..."
    const blockedPopupDontShowMessage = document.getElementById(
      "blockedPopupDontShowMessage"
    );
    blockedPopupDontShowMessage.setAttribute("checked", false);

    gBrowser.selectedBrowser.popupAndRedirectBlocker
      .getBlockedRedirect()
      .then(blockedRedirect => {
        this.onPopupShowingBlockedRedirect(blockedRedirect, window);
      });
    gBrowser.selectedBrowser.popupAndRedirectBlocker
      .getBlockedPopups()
      .then(blockedPopups => {
        this.onPopupShowingBlockedPopups(blockedPopups, window);
      });
  },

  onPopupShowingBlockedRedirect(aBlockedRedirect, aWindow) {
    const { gBrowser, document } = aWindow;
    const browser = gBrowser.selectedBrowser;

    const blockedRedirectSeparator = document.getElementById(
      "blockedRedirectSeparator"
    );
    blockedRedirectSeparator.hidden = !aBlockedRedirect;

    if (!aBlockedRedirect) {
      return;
    }

    // We may end up in a race condition and to avoid showing duplicate
    // items, make sure the list is actually empty.
    const nextElement = blockedRedirectSeparator.nextElementSibling;
    if (nextElement?.hasAttribute("redirectInnerWindowId")) {
      return;
    }

    const menuitem = document.createXULElement("menuitem");
    document.l10n.setAttributes(menuitem, "popup-trigger-redirect-menuitem", {
      redirectURI: aBlockedRedirect.redirectURISpec,
    });
    menuitem.setAttribute("redirectURISpec", aBlockedRedirect.redirectURISpec);
    // Store the source inner window id, so we can check if the document
    // that triggered the redirect is still the same.
    menuitem.setAttribute(
      "redirectInnerWindowId",
      aBlockedRedirect.innerWindowId
    );
    // Store the browser for the current tab. The active tab may change,
    // so we keep a reference to it.
    menuitem.browser = browser;
    // Same reason as source inner window id, we compare it with the one
    // of the browsing context at the time of unblocking.
    menuitem.browsingContext = aBlockedRedirect.browsingContext;

    blockedRedirectSeparator.after(menuitem);
  },

  onPopupShowingBlockedPopups(aBlockedPopups, aWindow) {
    const { gBrowser, document } = aWindow;
    const browser = gBrowser.selectedBrowser;

    const blockedPopupsSeparator = document.getElementById(
      "blockedPopupsSeparator"
    );
    blockedPopupsSeparator.hidden = !aBlockedPopups.length;

    if (!aBlockedPopups.length) {
      return;
    }

    // We may end up in a race condition and to avoid showing duplicate
    // items, make sure the list is actually empty.
    const nextElement = blockedPopupsSeparator.nextElementSibling;
    if (nextElement?.hasAttribute("popupInnerWindowId")) {
      return;
    }

    for (let i = 0; i < aBlockedPopups.length; ++i) {
      const blockedPopup = aBlockedPopups[i];

      const menuitem = document.createXULElement("menuitem");
      document.l10n.setAttributes(menuitem, "popup-show-popup-menuitem", {
        popupURI: blockedPopup.popupWindowURISpec,
      });
      menuitem.setAttribute("popupReportIndex", i);
      // Store the source inner window id, so we can check if the document
      // that triggered the redirect is still the same.
      menuitem.setAttribute("popupInnerWindowId", blockedPopup.innerWindowId);
      // Store the browser for the current tab. The active tab may change,
      // so we keep a reference to it.
      menuitem.browser = browser;
      // Same reason as source inner window id, we compare it with the one
      // of the browsing context at the time of unblocking.
      menuitem.browsingContext = blockedPopup.browsingContext;

      blockedPopupsSeparator.after(menuitem);
    }
  },

  /**
   * Event handler that is triggered when the "options" popup of the
   * notification closes.
   * @param {*} aEvent
   */
  onPopupHiding(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { document } = window;

    // Remove the blocked redirect, if any.
    const blockedRedirectSeparator = document.getElementById(
      "blockedRedirectSeparator"
    );
    let item = blockedRedirectSeparator.nextElementSibling;
    if (item?.hasAttribute("redirectInnerWindowId")) {
      item.remove();
    }

    // Remove the blocked popups, if any.
    const blockedPopupsSeparator = document.getElementById(
      "blockedPopupsSeparator"
    );
    let next = null;
    for (
      item = blockedPopupsSeparator.nextElementSibling;
      item?.hasAttribute("popupInnerWindowId");
      item = next
    ) {
      next = item.nextElementSibling;
      item.remove();
    }
  },

  /**
   * Event handler that is triggered when a user clicks on one of the
   * fields in the "options" popup of the notification.
   * @param {*} aEvent
   */
  onCommand(aEvent) {
    if (aEvent.target.hasAttribute("popupReportIndex")) {
      this.showBlockedPopup(aEvent);
      return;
    }

    if (aEvent.target.hasAttribute("redirectURISpec")) {
      this.navigateToBlockedRedirect(aEvent);
      return;
    }

    switch (aEvent.target.id) {
      case "blockedPopupAllowSite":
        this.toggleAllowPopupsForSite(aEvent);
        break;
      case "blockedPopupEdit":
        this.editPopupSettings(aEvent);
        break;
      case "blockedPopupDontShowMessage":
        this.dontShowMessage(aEvent);
        break;
    }
  },

  showBlockedPopup(aEvent) {
    const { browser, browsingContext } = aEvent.target;
    const innerWindowId = aEvent.target.getAttribute("popupInnerWindowId");
    const popupReportIndex = aEvent.target.getAttribute("popupReportIndex");

    browser.popupAndRedirectBlocker.unblockPopup(
      browsingContext,
      innerWindowId,
      popupReportIndex
    );
  },

  navigateToBlockedRedirect(aEvent) {
    const { browser, browsingContext } = aEvent.target;
    const innerWindowId = aEvent.target.getAttribute("redirectInnerWindowId");
    const redirectURISpec = aEvent.target.getAttribute("redirectURISpec");

    browser.popupAndRedirectBlocker.unblockRedirect(
      browsingContext,
      innerWindowId,
      redirectURISpec
    );
  },

  async toggleAllowPopupsForSite(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { gBrowser } = window;

    const permission = Services.perms.testPermissionFromPrincipal(
      gBrowser.contentPrincipal,
      "popup"
    );
    if (permission == Services.perms.ALLOW_ACTION) {
      throw new Error("Popups should not be allowed in this state");
    }

    // The toggle should only be visible (and therefore clickable) if
    // popups are currently blocked.
    lazy.SitePermissions.setForPrincipal(
      gBrowser.contentPrincipal,
      "popup",
      lazy.SitePermissions.ALLOW
    );
    gBrowser.getNotificationBox().removeCurrentNotification();

    // The order is important here. We want to unblock all popups of the
    // current document first and then potentially redirect somewhere
    // else.
    await gBrowser.selectedBrowser.popupAndRedirectBlocker.unblockAllPopups();
    await gBrowser.selectedBrowser.popupAndRedirectBlocker.unblockFirstRedirect();
  },

  editPopupSettings(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { openPreferences } = window;

    openPreferences("privacy-permissions-block-popups");
  },

  dontShowMessage(aEvent) {
    const window = aEvent.originalTarget.ownerGlobal;
    const { gBrowser } = window;

    Services.prefs.setBoolPref("privacy.popups.showBrowserMessage", false);
    gBrowser.getNotificationBox().removeCurrentNotification();
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  PopupAndRedirectBlockerObserver,
  "maxReportedPopups",
  "privacy.popups.maxReported"
);
