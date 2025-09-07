/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
// Tests the "Forget About This Site" button from the context menu
const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  HistoryController: "resource:///modules/HistoryController.sys.mjs",
});

let URLs, dates, today;

add_setup(async () => {
  const historyInfo = await populateHistory();
  URLs = historyInfo.URLs;
  dates = historyInfo.dates;
  today = dates[0];
});

registerCleanupFunction(async () => {
  await PlacesUtils.history.clear();
});

// TO DO - move below helper into universal helper with Places Bug 1954843
/**
 * Executes a task after opening the bookmarks dialog, then cancels the dialog.
 *
 * @param {boolean} autoCancel
 *        whether to automatically cancel the dialog at the end of the task
 * @param {Function} openFn
 *        generator function causing the dialog to open
 * @param {Function} taskFn
 *        the task to execute once the dialog is open
 * @param {Function} closeFn
 *        A function to be used to wait for pending work when the dialog is
 *        closing. It is passed the dialog window handle and should return a promise.
 * @returns {string} guid
 *          Bookmark guid
 */
var withBookmarksDialog = async function (autoCancel, openFn, taskFn, closeFn) {
  let dialogUrl = "chrome://browser/content/places/bookmarkProperties.xhtml";
  let closed = false;
  // We can't show the in-window prompt for windows which don't have
  // gDialogBox, like the library (Places:Organizer) window.
  let hasDialogBox = !!Services.wm.getMostRecentWindow("").gDialogBox;
  let dialogPromise;
  if (hasDialogBox) {
    dialogPromise = BrowserTestUtils.promiseAlertDialogOpen(null, dialogUrl, {
      isSubDialog: true,
    });
  } else {
    dialogPromise = BrowserTestUtils.domWindowOpenedAndLoaded(null, window => {
      return window.document.documentURI.startsWith(dialogUrl);
    }).then(window => {
      ok(
        window.location.href.startsWith(dialogUrl),
        "The bookmark properties dialog is open: " + window.location.href
      );
      // This is needed for the overlay.
      return SimpleTest.promiseFocus(window).then(() => window);
    });
  }
  let dialogClosePromise = dialogPromise.then(window => {
    if (!hasDialogBox) {
      return BrowserTestUtils.domWindowClosed(window);
    }
    let container = window.top.document.getElementById("window-modal-dialog");
    return BrowserTestUtils.waitForEvent(container, "close").then(() => {
      return BrowserTestUtils.waitForMutationCondition(
        container,
        { childList: true, attributes: true },
        () => !container.hasChildNodes() && !container.open
      );
    });
  });
  dialogClosePromise.then(() => {
    closed = true;
  });

  info("withBookmarksDialog: opening the dialog");
  // The dialog might be modal and could block our events loop, so executeSoon.
  executeSoon(openFn);

  info("withBookmarksDialog: waiting for the dialog");
  let dialogWin = await dialogPromise;

  // Ensure overlay is loaded
  info("waiting for the overlay to be loaded");
  await dialogWin.document.mozSubdialogReady;

  // Check the first input is focused.
  let doc = dialogWin.document;
  let elt = doc.querySelector('input:not([hidden="true"])');
  ok(elt, "There should be an input to focus.");

  if (elt) {
    info("waiting for focus on the first textfield");
    await TestUtils.waitForCondition(
      () => doc.activeElement == elt,
      "The first non collapsed input should have been focused"
    );
  }

  info("withBookmarksDialog: executing the task");

  let closePromise = () => Promise.resolve();
  if (closeFn) {
    closePromise = closeFn(dialogWin);
  }
  let guid;
  try {
    await taskFn(dialogWin);
  } finally {
    if (!closed && autoCancel) {
      info("withBookmarksDialog: canceling the dialog");
      doc.getElementById("bookmarkpropertiesdialog").cancelDialog();
      await closePromise;
    }
    guid = await PlacesUIUtils.lastBookmarkDialogDeferred.promise;
    // Give the dialog a little time to close itself.
    await dialogClosePromise;
  }
  return guid;
};

add_task(async function test_history_cards_created() {
  const {
    component: { lists },
  } = await showHistorySidebar();
  Assert.equal(lists.length, dates.length, "There is a card for each day.");
  for (const list of lists) {
    Assert.equal(
      list.tabItems.length,
      URLs.length,
      "Card shows the correct number of visits."
    );
  }
  SidebarController.hide();
});

add_task(async function test_history_searchbox_focus_and_context_menu() {
  const { component, contentWindow } = await showHistorySidebar();
  const { searchTextbox } = component;

  ok(component.shadowRoot.activeElement, "check activeElement is present");
  Assert.equal(
    component.shadowRoot.activeElement,
    searchTextbox,
    "Check search box is focused"
  );

  const promisePopupShown = BrowserTestUtils.waitForEvent(
    contentWindow,
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(
    searchTextbox,
    { type: "contextmenu", button: 2 },
    contentWindow
  );
  const { target: menu } = await promisePopupShown;
  Assert.equal(
    menu.id,
    "textbox-contextmenu",
    "The correct context menu is shown."
  );
  menu.hidePopup();

  SidebarController.hide();
});

add_task(async function test_history_searchbox_focused_with_history_pending() {
  const sandbox = sinon.createSandbox();

  // This stubs any new instance created so that isHistoryPending getter always
  // returns true to simulate waiting for history to load.
  sandbox
    .stub(lazy.HistoryController.prototype, "isHistoryPending")
    .value(true);

  // Show the new history sidebar but don't wait for pendingHistory as this will timeout
  // since the check isHistoryPending will always return true.
  const { component } = await showHistorySidebar({
    waitForPendingHistory: false,
  });
  const { searchTextbox } = component;

  ok(component.shadowRoot.activeElement, "check activeElement is present");
  Assert.equal(
    component.shadowRoot.activeElement,
    searchTextbox,
    "Check search box is focused"
  );

  // Clean-up by hiding the sidebar, because the instance associated with this
  // History Sidebar remains overidden and the sandbox.restore() only affects
  // new instances that are created.
  SidebarController.hide();
  sandbox.restore();
});

add_task(async function test_history_search() {
  const { component, contentWindow } = await showHistorySidebar();
  const { searchTextbox } = component;

  info("Input a search query.");
  EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, contentWindow);
  EventUtils.sendString("Example Domain 1", contentWindow);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () =>
      component.lists.length === 1 &&
      component.shadowRoot.querySelector(
        "moz-card[data-l10n-id=sidebar-search-results-header]"
      ) &&
      component.lists[0]
  );
  await BrowserTestUtils.waitForCondition(() => {
    const { rowEls } = component.lists[0];
    return rowEls.length === 1 && rowEls[0].mainEl.href === URLs[1];
  }, "There is one matching search result.");

  info("Input a bogus search query.");
  EventUtils.synthesizeMouseAtCenter(searchTextbox, {}, contentWindow);
  EventUtils.sendString("Bogus Query", contentWindow);
  await TestUtils.waitForCondition(() => {
    const tabList = component.lists[0];
    return tabList?.emptyState;
  }, "There are no matching search results.");

  info("Clear the search query.");
  let inputChildren = SpecialPowers.InspectorUtils.getChildrenForNode(
    searchTextbox.inputEl,
    true,
    false
  );
  let clearButton = inputChildren.find(e => e.localName == "button");
  EventUtils.synthesizeMouseAtCenter(clearButton, {}, contentWindow);
  await TestUtils.waitForCondition(
    () => !component.lists[0].emptyState,
    "The original cards are restored."
  );
  SidebarController.hide();
});

add_task(async function test_history_sort() {
  const { component, contentWindow } = await showHistorySidebar();
  const { menuButton } = component;
  const menu = component._menu;
  const sortByDateButton = component._menuSortByDate;
  const sortBySiteButton = component._menuSortBySite;
  const sortByDateSiteButton = component._menuSortByDateSite;
  const sortByLastVisitedButton = component._menuSortByLastVisited;

  info("Sort history by site.");
  let promiseMenuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(menuButton, {}, contentWindow);
  await promiseMenuShown;
  menu.activateItem(sortBySiteButton);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.lists.length === URLs.length
  );
  ok(true, "There is a card for each site.");

  Assert.equal(
    sortBySiteButton.getAttribute("checked"),
    "true",
    "Sort by site is checked."
  );
  for (const card of component.cards) {
    Assert.equal(card.expanded, true, "All cards are expanded.");
  }

  info("Sort history by date.");
  promiseMenuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(menuButton, {}, contentWindow);
  await promiseMenuShown;
  menu.activateItem(sortByDateButton);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.lists.length === dates.length
  );
  ok(true, "There is a card for each date.");
  Assert.equal(
    sortByDateButton.getAttribute("checked"),
    "true",
    "Sort by date is checked."
  );
  for (const [i, card] of component.cards.entries()) {
    Assert.equal(
      card.expanded,
      i === 0 || i === 1,
      "The cards for Today and Yesterday are expanded."
    );
  }

  info("Sort history by date and site.");
  promiseMenuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(menuButton, {}, contentWindow);
  await promiseMenuShown;
  menu.activateItem(sortByDateSiteButton);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.lists.length === dates.length * URLs.length
  );
  Assert.ok(
    true,
    "There is a card for each date, and a nested card for each site."
  );
  Assert.equal(
    sortByDateSiteButton.getAttribute("checked"),
    "true",
    "Sort by date and site is checked."
  );
  const outerCards = [...component.cards].filter(
    el => !el.classList.contains("nested-card")
  );
  for (const [i, card] of outerCards.entries()) {
    Assert.equal(
      card.expanded,
      i === 0 || i === 1,
      "The cards for Today and Yesterday are expanded."
    );
  }

  info("Sort history by last visited.");
  promiseMenuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(menuButton, {}, contentWindow);
  await promiseMenuShown;
  menu.activateItem(sortByLastVisitedButton);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.lists.length === 1
  );
  Assert.equal(
    component.lists[0].tabItems.length,
    URLs.length,
    "There is a single card with a row for each site."
  );
  Assert.equal(
    sortByLastVisitedButton.getAttribute("checked"),
    "true",
    "Sort by last visited is checked."
  );

  SidebarController.hide();
});

add_task(async function test_history_hover_buttons() {
  const { component, contentWindow } = await showHistorySidebar();
  const { lists } = component;
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => !!lists.length
  );
  await BrowserTestUtils.waitForMutationCondition(
    lists[0].shadowRoot,
    { subtree: true, childList: true },
    () => lists[0].rowEls.length === URLs.length
  );
  ok(true, "History rows are shown.");
  const rows = lists[0].rowEls;

  info("Open the first link.");
  // We intentionally turn off this a11y check, because the following click is purposefully targeting a not
  // focusable link within a history item row which are following list-style keyboard navigation pattern.
  // This pattern is tested above. A keyboard-only user could focus these links using arrow navigation, but
  // a mouse user would not need these links to become focusable, therefore this rule check shall be ignored
  // by a11y_checks suite. Bug 1961686 is a follow up update a helper so we can later remove this.
  AccessibilityUtils.setEnv({ focusableRule: false });
  await waitForPageLoadTask(
    () => EventUtils.synthesizeMouseAtCenter(rows[0].mainEl, {}, contentWindow),
    URLs[0]
  );
  AccessibilityUtils.resetEnv();

  info("Remove the first entry.");
  const promiseRemoved = PlacesTestUtils.waitForNotification("page-removed");
  EventUtils.synthesizeMouseAtCenter(
    rows[0].secondaryButtonEl,
    {},
    contentWindow
  );
  await promiseRemoved;
  await TestUtils.waitForCondition(
    () => lists[0].rowEls.length === URLs.length - 1,
    "The removed entry should no longer be visible."
  );
  SidebarController.hide();
});

add_task(async function test_history_context_menu() {
  const { component } = await showHistorySidebar();
  const { lists } = component;
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => !!lists.length
  );
  await BrowserTestUtils.waitForMutationCondition(
    lists[0].shadowRoot,
    { subtree: true, childList: true },
    () => !!lists[0].rowEls.length
  );
  ok(true, "History rows are shown.");
  const contextMenu = SidebarController.currentContextMenu;
  let rows = lists[0].rowEls;

  function getItem(item) {
    return window.document.getElementById("sidebar-history-context-" + item);
  }

  info("Delete from history.");
  const promiseRemoved = PlacesTestUtils.waitForNotification("page-removed");
  let site = rows[0].mainEl.href;
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("delete-page"))
  );
  await promiseRemoved;
  await TestUtils.waitForCondition(
    () => () => rows[0].mainEl.href !== site,
    "The removed entry should no longer be visible."
  );

  rows = lists[0].rowEls;
  const { url } = rows[0];

  info("Open link in a new window.");
  let promiseWin = BrowserTestUtils.waitForNewWindow({ url });
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("open-in-window"))
  );
  let win = await promiseWin;
  await BrowserTestUtils.closeWindow(win);

  info("Open link in a new private window.");
  promiseWin = BrowserTestUtils.waitForNewWindow({ url });
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("open-in-private-window"))
  );
  const privateWin = await promiseWin;
  ok(
    PrivateBrowsingUtils.isWindowPrivate(privateWin),
    "The new window is in private browsing mode."
  );
  await BrowserTestUtils.closeWindow(privateWin);

  info(`Copy link from: ${rows[0].mainEl.href}`);
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("copy-link"))
  );
  await TestUtils.waitForCondition(
    () => SpecialPowers.getClipboardData("text/plain") == url,
    "The copied URL is correct."
  );

  info("Open link in new tab.");
  const promiseTabOpen = BrowserTestUtils.waitForEvent(
    window.gBrowser.tabContainer,
    "TabOpen"
  );
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("open-in-tab"))
  );
  await promiseTabOpen;
  await BrowserTestUtils.browserLoaded(
    window.gBrowser,
    false,
    rows[0].mainEl.href
  );
  is(window.gBrowser.currentURI.spec, rows[0].mainEl.href, "New tab opened");

  info("Clear all data from website");
  let dialogOpened = BrowserTestUtils.promiseAlertDialogOpen(
    null,
    "chrome://browser/content/places/clearDataForSite.xhtml",
    { isSubDialog: true }
  );
  site = rows[0].mainEl.href;
  const promiseForgotten = PlacesTestUtils.waitForNotification("page-removed");
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(getItem("forget-site"))
  );
  let dialog = await dialogOpened;
  let forgetSiteText = dialog.document.getElementById(
    "clear-data-for-site-list"
  );
  let siteToForget = rows[0].mainEl.href.substring(
    rows[0].mainEl.href.includes("https") ? 8 : 7,
    rows[0].mainEl.href.length - 1
  );
  let siteForgottenIsCorrect;
  await BrowserTestUtils.waitForMutationCondition(
    forgetSiteText,
    { attributes: true, attributeFilter: ["data-l10n-args"] },
    () => {
      let text = JSON.parse(forgetSiteText.getAttribute("data-l10n-args")).site;
      siteForgottenIsCorrect =
        text.includes(siteToForget) || siteToForget.includes(text);
      return siteForgottenIsCorrect;
    }
  );
  ok(
    siteForgottenIsCorrect,
    "The text for forgetting a specific site should be set"
  );
  let dialogClosed = BrowserTestUtils.waitForEvent(dialog, "unload");
  let removeButton = dialog.document
    .querySelector("dialog")
    .getButton("accept");
  removeButton.click();
  await Promise.all([dialogClosed, promiseForgotten]);
  await TestUtils.waitForCondition(
    () => rows[0].mainEl.href !== site,
    "The forgotten entry should no longer be visible."
  );

  info("Open new container tab");
  let promiseTabOpened = BrowserTestUtils.waitForNewTab(window.gBrowser, null);
  rows[0].mainEl.scrollIntoView();
  const eventDetails = { type: "contextmenu", button: 2 };
  info("Wait for context menu");
  let shown = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    rows[0].mainEl,
    eventDetails,
    // eslint-disable-next-line mozilla/use-ownerGlobal
    rows[0].mainEl.ownerDocument.defaultView
  );
  await shown;
  let hidden = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  let containerContextMenu = window.document.getElementById(
    "sidebar-history-context-menu-container-tab"
  );
  let menuPopup = containerContextMenu.menupopup;
  info("Wait for container sub menu");
  let menuPopupPromise = BrowserTestUtils.waitForEvent(menuPopup, "popupshown");
  containerContextMenu.openMenu(true);
  await menuPopupPromise;
  info("Click first child to open a tab in a container");
  contextMenu.activateItem(menuPopup.childNodes[0]);
  await hidden;
  await promiseTabOpened;

  info("Add new bookmark");
  let bookmarkName;
  await withBookmarksDialog(
    false,
    async () => {
      // Open the context menu.
      await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
        contextMenu.activateItem(getItem("bookmark-page"))
      );
    },
    async dialogWin => {
      bookmarkName = dialogWin.document.getElementById(
        "editBMPanel_namePicker"
      ).value;
      EventUtils.synthesizeKey("VK_RETURN", {}, dialogWin);
    }
  );
  await toggleSidebarPanel(window, "viewBookmarksSidebar");
  let tree =
    SidebarController.browser.contentDocument.getElementById("bookmarks-view");
  let toolbarKey = tree._view._nodeDetails
    .keys()
    .find(key => key.includes("toolbar"));
  let toolbar = tree._view._nodeDetails.get(toolbarKey);
  await BrowserTestUtils.waitForMutationCondition(
    toolbar,
    { attributes: true, attributeFilter: "hasChildren" },
    () => toolbar.hasChildren
  );
  toolbar.containerOpen = true;
  let vals = [];
  tree._view._nodeDetails.values().forEach(val => vals.push(val.title));
  ok(vals.includes(bookmarkName), "Bookmark entry exists");
  await PlacesUtils.bookmarks.eraseEverything();

  cleanUpExtraTabs();
  SidebarController.hide();
});

add_task(async function test_select_and_remove() {
  const { component, contentWindow } = await showHistorySidebar();
  await component.updateComplete;
  await BrowserTestUtils.waitForMutationCondition(
    component.lists[0].shadowRoot,
    { subtree: true, childList: true },
    () => component.lists[0].rowEls.length
  );
  const rows = component.lists[0].rowEls;
  Assert.ok(rows, "History rows are shown.");

  function promiseRowSelected(index) {
    return BrowserTestUtils.waitForMutationCondition(
      rows[index],
      { attributes: true },
      () => rows[index].hasAttribute("selected")
    );
  }

  info("Select the first two pages.");
  rows[0].focus();
  EventUtils.synthesizeKey("KEY_ArrowDown", { shiftKey: true }, contentWindow);
  for (let i = 0; i < 2; i++) {
    await promiseRowSelected(i);
  }

  info("Select all pages.");
  EventUtils.synthesizeKey("A", { accelKey: true }, contentWindow);
  for (let i = 0; i < rows.length; i++) {
    await promiseRowSelected(i);
  }

  info("Press Enter key.");
  EventUtils.synthesizeKey("KEY_Enter", {}, contentWindow);
  Assert.equal(
    gBrowser.tabs.length,
    1,
    "Enter key does not open pages during multi-selection."
  );

  info("Delete from history.");
  const contextMenu = SidebarController.currentContextMenu;
  const notification = PlacesTestUtils.waitForNotification("page-removed");
  await openAndWaitForContextMenu(contextMenu, rows[0].mainEl, () =>
    contextMenu.activateItem(
      document.getElementById("sidebar-history-context-delete-pages")
    )
  );
  const pagesRemoved = await notification;
  Assert.equal(
    pagesRemoved.length,
    rows.length,
    "The selected pages were removed."
  );

  SidebarController.hide();
});

add_task(async function test_history_empty_state() {
  const { component } = await showHistorySidebar();
  info("Clear all history.");
  await PlacesUtils.history.clear();
  info("Waiting for history empty state to be present");
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => !!component.emptyState
  );
  ok(
    BrowserTestUtils.isVisible(component.emptyState),
    "Empty state is displayed."
  );
  SidebarController.hide();
});

add_task(async function test_sort_by_site_headings() {
  const { component, contentWindow } = await showHistorySidebar();
  const {
    _menu: menu,
    menuButton,
    _menuSortBySite: sortBySiteButton,
  } = component;

  await PlacesUtils.history.insertMany([
    {
      url: "file:///example.html",
      title: "Example Local File",
      visits: [{ date: today }],
    },
    {
      url: "https://www.example.com/",
      title: "Example Website",
      visits: [{ date: today }],
    },
  ]);

  info("Sort history by site.");
  let promiseMenuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(menuButton, {}, contentWindow);
  await promiseMenuShown;
  menu.activateItem(sortBySiteButton);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.cards.length === 2
  );

  const [fileCard, websiteCard] = component.cards;
  Assert.deepEqual(
    document.l10n.getAttributes(fileCard),
    {
      args: null,
      id: "sidebar-history-site-localhost",
    },
    "Correct heading shown for local files."
  );
  Assert.equal(
    websiteCard.heading,
    "example.com",
    "Correct heading shown for websites."
  );

  SidebarController.hide();
});
