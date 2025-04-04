/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_setup(async () => {
  // Ensure all bookmarks cleared before the test starts.
  await PlacesUtils.bookmarks.eraseEverything();
});

add_task(async function test() {
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.toolbarGuid,
    title: "Plain Bob",
    url: "https://example.com",
  });

  registerCleanupFunction(async () => {
    await PlacesUtils.bookmarks.remove(bookmark);
  });

  let sidebarBox = document.getElementById("sidebar-box");
  is(sidebarBox.hidden, true, "The sidebar should be hidden");

  // Uncollapse the personal toolbar if needed.
  let toolbar = document.getElementById("PersonalToolbar");
  let wasCollapsed = toolbar.collapsed;
  if (wasCollapsed) {
    await promiseSetToolbarVisibility(toolbar, true);
  }

  let sidebar = await promiseLoadedSidebar("viewBookmarksSidebar");
  registerCleanupFunction(() => {
    SidebarController.hide();
  });

  // Focus the tree and check if its controller is returned.
  let tree = sidebar.contentDocument.getElementById("bookmarks-view");
  tree.focus();

  let controller = PlacesUIUtils.getControllerForCommand(
    window,
    "placesCmd_copy"
  );
  let treeController =
    tree.controllers.getControllerForCommand("placesCmd_copy");
  Assert.equal(controller, treeController, "tree controller was returned");

  // Open the context menu for a toolbar item, and check if the toolbar's
  // controller is returned.
  let toolbarItems = document.getElementById("PlacesToolbarItems");
  // Ensure the toolbar has displayed the bookmark. This might be async, so
  // wait a little if necessary.
  await TestUtils.waitForCondition(
    () => toolbarItems.children.length == 1,
    "Should have only one item on the toolbar"
  );

  let placesContext = document.getElementById("placesContext");
  let popupShownPromise = BrowserTestUtils.waitForEvent(
    placesContext,
    "popupshown"
  );
  EventUtils.synthesizeMouse(
    toolbarItems.children[0],
    4,
    4,
    { type: "contextmenu", button: 2 },
    window
  );
  await popupShownPromise;

  controller = PlacesUIUtils.getControllerForCommand(window, "placesCmd_copy");
  let toolbarController = document
    .getElementById("PlacesToolbar")
    .controllers.getControllerForCommand("placesCmd_copy");
  Assert.equal(
    controller,
    toolbarController,
    "the toolbar controller was returned"
  );

  let popupHiddenPromise = BrowserTestUtils.waitForEvent(
    placesContext,
    "popuphidden"
  );
  placesContext.hidePopup();
  await popupHiddenPromise;

  // Now that the context menu is closed, try to get the tree controller again.
  tree.focus();
  controller = PlacesUIUtils.getControllerForCommand(window, "placesCmd_copy");
  Assert.equal(controller, treeController, "tree controller was returned");

  if (wasCollapsed) {
    await promiseSetToolbarVisibility(toolbar, false);
  }
});

function promiseLoadedSidebar(cmd) {
  return new Promise(resolve => {
    let sidebar = document.getElementById("sidebar");
    sidebar.addEventListener(
      "load",
      function () {
        executeSoon(() => resolve(sidebar));
      },
      { capture: true, once: true }
    );

    SidebarController.show(cmd);
  });
}
