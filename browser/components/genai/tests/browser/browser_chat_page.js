/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "resource:///modules/GenAI.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const CONTENT_AREA_CONTEXT_MENU = "contentAreaContextMenu";
const TAB_CONTEXT_MENU = "tabContextMenu";
const TOOL_CONTEXT_MENU = "sidebar-context-menu";

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("sidebar.new-sidebar.has-used");
  Services.prefs.clearUserPref("browser.engagement.sidebar-button.has-used");
});

// Bug 1895789 to standarize contextmenu helpers in BrowserTestUtils
async function openContextMenu({ menuId, browser }) {
  const tab = gBrowser.getTabForBrowser(browser);
  const win = tab.ownerGlobal;

  const contextMenu = win.document.getElementById(menuId);
  if (!contextMenu) {
    throw new Error(`Context menu with ${menuId} not found`);
  }

  const promise = BrowserTestUtils.waitForEvent(contextMenu, "popupshown");

  if (menuId === TAB_CONTEXT_MENU) {
    EventUtils.synthesizeMouseAtCenter(
      tab,
      { type: "contextmenu", button: 2 },
      win
    );
  } else if (menuId === TOOL_CONTEXT_MENU) {
    const { sidebarMain } = SidebarController;
    const aichatEl = sidebarMain.shadowRoot.querySelector(
      'moz-button[view="viewGenaiChatSidebar"]'
    );

    EventUtils.synthesizeMouseAtCenter(
      aichatEl,
      { type: "contextmenu", button: 2 },
      aichatEl.ownerGlobal
    );
  } else {
    BrowserTestUtils.synthesizeMouse(
      null,
      0,
      0,
      { type: "contextmenu" },
      browser
    );
  }
  await promise;
}

async function hideContextMenu(menuId) {
  const contextMenu = document.getElementById(menuId);
  const promise = BrowserTestUtils.waitForEvent(contextMenu, "popuphidden");
  contextMenu.hidePopup();
  await promise;
}

async function runContextMenuTest({
  menuId,
  targetId,
  expectedLabel,
  expectedDescription,
  stub,
  browser,
}) {
  await openContextMenu({ menuId, browser });

  const menu = document.getElementById(targetId);
  const menuItems = [...menu.querySelectorAll("menuitem")].filter(
    item => !item.hidden
  );

  await TestUtils.waitForCondition(() => {
    return menuItems[0]?.label === expectedLabel;
  }, expectedDescription);

  menuItems[0].click();
  await hideContextMenu(menuId);

  if (stub) {
    assertContextMenuStubResult(stub);
  }

  stub.resetHistory();
}

function assertContextMenuStubResult(stub) {
  Assert.equal(stub.callCount, 1, "one menu prompt");
  Assert.equal(stub.firstCall.args[0].id, "summarize", "summarize prompt");
  Assert.ok(stub.firstCall.args[0].badge, "new badge");
  Assert.equal(
    Services.prefs.getBoolPref("browser.ml.chat.page.menuBadge"),
    false,
    "badge dismissed"
  );
}

async function ensureSidebarLauncherIsVisible() {
  await TestUtils.waitForTick();
  // Show the sidebar launcher if its hidden
  if (SidebarController.sidebarContainer.hidden) {
    document.getElementById("sidebar-button").doCommand();
  }
  await TestUtils.waitForTick();
  Assert.ok(
    BrowserTestUtils.isVisible(SidebarController.sidebarMain),
    "Sidebar launcher is visible"
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

/**
 * Check page, tab, and tool context menu have summarize prompt
 */
add_task(async function test_page_and_tab_menu_prompt() {
  const sandbox = sinon.createSandbox();
  const stub = sandbox.stub(GenAI, "handleAskChat");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.page", true],
      ["browser.ml.chat.page.menuBadge", true],
      ["sidebar.revamp", true],
    ],
  });
  await ensureSidebarLauncherIsVisible();

  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    await runContextMenuTest({
      menuId: CONTENT_AREA_CONTEXT_MENU,
      targetId: "context-ask-chat",
      expectedLabel: "Summarize Page",
      expectedDescription: "Page prompt added",
      stub,
      browser,
    });

    await runContextMenuTest({
      menuId: TAB_CONTEXT_MENU,
      targetId: "context_askChat",
      expectedLabel: "Summarize Page",
      expectedDescription: "Page prompt added",
      stub,
      browser,
    });

    SidebarController.show();

    await runContextMenuTest({
      menuId: TOOL_CONTEXT_MENU,
      targetId: TOOL_CONTEXT_MENU,
      expectedLabel: "Summarize Page",
      expectedDescription: "Page prompt added",
      stub,
      browser: gBrowser.selectedBrowser,
    });
  });

  sandbox.restore();
  SidebarController.hide();
});

/**
 *  Check Summarize Page button in page and tab menu is disabled
 *  When browser context is invalid
 */
add_task(async function test_summarize_page_button_is_disabled_in_menu() {
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    // invalid context
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });
    await TestUtils.waitForCondition(() => {
      return document.getElementById("context-ask-chat").getItemAtIndex(0)
        .disabled;
    }, "Summarize page button is disabled");

    Assert.ok(
      document.getElementById("context-ask-chat").getItemAtIndex(0).disabled,
      "Summarize page button is disabled because context is invalid"
    );
    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
  });
});

/**
 * Check situations page menu should not be shown
 */
add_task(async function test_page_menu_no_chatbot() {
  await SpecialPowers.pushPrefEnv({
    clear: [["browser.ml.chat.provider"]],
    set: [["browser.ml.chat.page", true]],
  });
  await BrowserTestUtils.withNewTab("https://example.com", async browser => {
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });

    await TestUtils.waitForCondition(() => {
      const menu = document.getElementById("context-ask-chat");
      return menu && !menu.hidden && !menu.disabled;
    }, "Menu should be visible");

    Assert.equal(
      document.getElementById("context-ask-chat").hidden,
      false,
      "chatbot menu shown"
    );

    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
    await SpecialPowers.pushPrefEnv({
      set: [["browser.ml.chat.menu", false]],
    });
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });

    Assert.ok(
      document.getElementById("context-ask-chat").hidden,
      "hidden for no menu pref"
    );

    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
    await SpecialPowers.popPrefEnv();
    await SpecialPowers.pushPrefEnv({
      set: [["sidebar.revamp", false]],
    });
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });

    Assert.equal(
      document.getElementById("context-ask-chat").hidden,
      false,
      "old sidebar shows menu"
    );

    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
    await SpecialPowers.pushPrefEnv({
      set: [
        ["sidebar.revamp", true],
        ["sidebar.main.tools", "history"],
      ],
    });
    await ensureSidebarLauncherIsVisible();
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });

    Assert.ok(
      document.getElementById("context-ask-chat").hidden,
      "hidden for no chatbot tool"
    );

    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
    await SpecialPowers.pushPrefEnv({
      set: [["sidebar.main.tools", "aichat,history"]],
    });
    await openContextMenu({ menuId: CONTENT_AREA_CONTEXT_MENU, browser });

    Assert.equal(
      document.getElementById("context-ask-chat").hidden,
      false,
      "new sidebar with tool shows menu"
    );

    await hideContextMenu(CONTENT_AREA_CONTEXT_MENU);
  });
});

/**
 * Check tab menu has a label and separator
 *
 */
add_task(async function test_tab_menu_has_label_and_separator() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", ""],
      ["browser.ml.chat.menu", true],
    ],
  });

  await openContextMenu({
    menuId: TAB_CONTEXT_MENU,
    browser: gBrowser.selectedTab.linkedBrowser,
  });

  let menu;
  await TestUtils.waitForCondition(() => {
    menu = document.getElementById("context_askChat");
    return menu && !menu.hidden && !menu.disabled;
  }, "Menu should be visible");

  Assert.ok(
    menu.label === "Ask an AI Chatbot" &&
      menu.nextSibling.localName === "menuseparator",
    "Chatbot menu from tab menu has label and menusepartor"
  );

  await hideContextMenu(TAB_CONTEXT_MENU);

  // Set menu prefs false
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.menu", false]],
  });

  await openContextMenu({
    menuId: TAB_CONTEXT_MENU,
    browser: gBrowser.selectedTab.linkedBrowser,
  });

  await TestUtils.waitForCondition(
    () => menu.hidden,
    "Menu is hidden after changing menu prefs"
  );

  const separator = menu.nextElementSibling;
  Assert.ok(
    menu.hidden && separator.hidden,
    "<menu> and <menuseparator> are hidden"
  );

  await hideContextMenu(TAB_CONTEXT_MENU);
});

/**
 * Check tab menu should not be shown
 *
 */
add_task(async function test_tab_menu_no_chatbot() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.menu", true]],
  });

  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const tab2 = await BrowserTestUtils.addTab(gBrowser, "https://example.com");

  // chatbot menu is hidden for multiselection
  gBrowser.selectedTabs = [tab1, tab2];

  await openContextMenu({
    menuId: TAB_CONTEXT_MENU,
    browser: tab1.linkedBrowser,
  });
  Assert.ok(
    document.getElementById("context_askChat").hidden,
    "Chatbot menu item is hidden when multiple tabs are selected"
  );

  await hideContextMenu(TAB_CONTEXT_MENU);
  gBrowser.clearMultiSelectedTabs();
  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);

  // chatbot menu is hidden when page pref is false
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.page", false]],
  });

  await openContextMenu({
    menuId: TAB_CONTEXT_MENU,
    browser: gBrowser.selectedTab.linkedBrowser,
  });
  Assert.ok(
    document.getElementById("context_askChat").hidden,
    "Chatbot menu item in tab context menu is hidden"
  );

  await hideContextMenu(TAB_CONTEXT_MENU);
  await SpecialPowers.popPrefEnv();
});

/**
 * Check badge toggle by prefs
 */
add_task(async function test_toggle_new_badge() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", "http://localhost:8080"],
      ["browser.ml.chat.sidebar", true],
      ["browser.ml.chat.page", true],
      ["browser.ml.chat.page.footerBadge", true],
    ],
  });

  await SidebarController.show("viewGenaiChatSidebar");

  const { document } = SidebarController.browser.contentWindow;
  const buttonContainer = document.getElementById("summarize-btn-container");
  const badge = buttonContainer.querySelector(".badge");

  Assert.notEqual(
    getComputedStyle(badge).display,
    "none",
    "new badge set visible"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.page.footerBadge", false]],
  });

  await TestUtils.waitForCondition(
    () => getComputedStyle(badge).display == "none",
    "Badge changed by css"
  );

  Assert.equal(
    getComputedStyle(badge).display,
    "none",
    "new badge set dismissed"
  );
});

/**
 * Test badge dismissal and check if summarizeCurrentPage() is executed
 */
add_task(async function test_click_summarize_button() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.page.footerBadge", true]],
  });

  const { document } = SidebarController.browser.contentWindow;
  const summarizeButton = document.getElementById("summarize-button");

  const sandbox = sinon.createSandbox();
  const stub = sandbox.stub(GenAI, "summarizeCurrentPage");

  summarizeButton.click();

  Assert.equal(
    Services.prefs.getBoolPref("browser.ml.chat.page.footerBadge"),
    false
  );
  Assert.equal(stub.callCount, 1);

  sandbox.restore();
  SidebarController.hide();
});

/**
 * Test provider-less summarization - onboarding then summarize
 */
add_task(async function test_provider_less_summarization() {
  const origTabs = gBrowser.tabs.length;
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.provider", ""],
      ["browser.ml.chat.sidebar", false],
    ],
  });

  await GenAI.summarizeCurrentPage(window, "test");

  await TestUtils.waitForCondition(
    () => SidebarController.isOpen,
    "Sidebar opened for onboarding"
  );
  Assert.equal(gBrowser.tabs.length, origTabs, "No tabs opened");

  // Mock selecting a provider with onboarding
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.provider", "http://localhost:8080"]],
  });
  const resolve = await TestUtils.waitForCondition(
    () => SidebarController.browser.contentWindow.showOnboarding?.resolve,
    "Chat loaded ready for onboarding"
  );
  resolve();

  await TestUtils.waitForCondition(
    () => gBrowser.tabs.length == origTabs + 1,
    "Chat opened tab for summarize"
  );

  SidebarController.hide();
  gBrowser.removeTab(gBrowser.selectedTab);
});
