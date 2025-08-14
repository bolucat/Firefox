/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.closeApp
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.restartApp
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

/**
 *  Tests for verifying basic functionality of tabbed browsing
 *
 *  Including:
 *  - Opening a tab
 *  - Opening a private tab
 *  - Verifying tab list
 *  - Closing all tabs
 *  - Close tab
 *  - Swipe to close tab (temporarily disabled)
 *  - Undo close tab
 *  - Close private tabs persistent notification
 *  - Empty tab tray state
 *  - Tab tray details
 *  - Shortcut context menu navigation
 */

class TabbedBrowsingTest : TestSetup() {
    @get:Rule(order = 0)
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(
                skipOnboarding = true,
            ),
        ) { it.activity }

    @get:Rule(order = 1)
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule(order = 2)
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903599
    @Test
    fun closeAllTabsTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openTabDrawer(composeTestRule) {
            verifyNormalTabsList()
        }.openThreeDotMenu {
            verifyCloseAllTabsButton()
            verifyShareAllTabsButton()
            verifySelectTabsButton()
        }.closeAllTabs {
            verifyTabCounter("0")
        }

        // Repeat for Private Tabs
        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openTabDrawer(composeTestRule) {
            verifyPrivateTabsList()
        }.openThreeDotMenu {
            verifyCloseAllTabsButton()
        }.closeAllTabs {
            verifyTabCounter("0")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2349580
    @Test
    fun closingTabsTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            closeTab()
            verifySnackBarText("Tab closed")
            clickSnackbarButton(composeTestRule, "UNDO")
        }
        browserScreen {
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903604
    @Test
    fun swipeToCloseTabsTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            waitForPageToLoad()
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            swipeTabRight("Test_Page_1")
            verifySnackBarText("Tab closed")
        }
        homeScreen {
            verifyTabCounter("0")
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            waitForPageToLoad()
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            swipeTabLeft("Test_Page_1")
            verifySnackBarText("Tab closed")
        }
        homeScreen {
            verifyTabCounter("0")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903591
    @Test
    fun closingPrivateTabsTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen { }.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            closeTab()
            verifySnackBarText("Private tab closed")
            clickSnackbarButton(composeTestRule, "UNDO")
        }
        browserScreen {
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903592
    @SmokeTest
    @Test
    @SkipLeaks
    fun verifyCloseAllPrivateTabsNotificationTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            mDevice.openNotification()
        }

        notificationShade {
            verifyPrivateTabsNotification()
        }.clickClosePrivateTabsNotification {
            verifyHomeScreen()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903600
    @Test
    fun verifyEmptyTabTray() {
        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyNormalBrowsingButtonIsSelected()
            verifyPrivateBrowsingButtonIsSelected(false)
            verifySyncedTabsButtonIsSelected(false)
            verifyNoOpenTabsInNormalBrowsing()
            verifyFab()
            verifyThreeDotButton()
        }.openThreeDotMenu {
            verifyTabSettingsButton()
            verifyRecentlyClosedTabsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903585
    @Test
    fun verifyEmptyPrivateTabsTrayTest() {
        homeScreen {
        }.openTabDrawer(composeTestRule) {
        }.toggleToPrivateTabs {
            verifyNormalBrowsingButtonIsSelected(false)
            verifyPrivateBrowsingButtonIsSelected(true)
            verifySyncedTabsButtonIsSelected(false)
            verifyNoOpenTabsInPrivateBrowsing()
            verifyFab()
            verifyThreeDotButton()
        }.openThreeDotMenu {
            verifyTabSettingsButton()
            verifyRecentlyClosedTabsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903601
    @Test
    fun verifyTabsTrayWithOpenTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.openTabDrawer(composeTestRule) {
            verifyNormalBrowsingButtonIsSelected()
            verifyPrivateBrowsingButtonIsSelected(isSelected = false)
            verifySyncedTabsButtonIsSelected(isSelected = false)
            verifyThreeDotButton()
            verifyNormalTabsList()
            verifyFab()
            verifyTabThumbnail()
            verifyExistingOpenTabs(defaultWebPage.title)
            verifyTabCloseButton()
        }.openTab(defaultWebPage.title) {
            verifyUrl(defaultWebPage.url.toString())
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903587
    @SmokeTest
    @Test
    fun verifyPrivateTabsTrayWithOpenTabTest() {
        val website = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openTabDrawer(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openNewTab {
        }.submitQuery(website.url.toString()) {
        }.openTabDrawer(composeTestRule) {
            verifyNormalBrowsingButtonIsSelected(false)
            verifyPrivateBrowsingButtonIsSelected(true)
            verifySyncedTabsButtonIsSelected(false)
            verifyThreeDotButton()
            verifyPrivateTabsList()
            verifyExistingOpenTabs(website.title)
            verifyTabCloseButton()
            verifyTabThumbnail()
            verifyFab()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/927314
    @Test
    fun tabsCounterShortcutMenuCloseTabTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
            waitForPageToLoad()
        }.goToHomescreen(composeTestRule) {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(secondWebPage.url) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
            verifyTabButtonShortcutMenuItems()
        }.closeTabFromShortcutsMenu {
            browserScreen {
                verifyTabCounter("1")
                verifyPageContent(firstWebPage.content)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2343663
    @Test
    @SkipLeaks(
        reasons = [
            "https://bugzilla.mozilla.org/show_bug.cgi?id=1962065",
            "https://bugzilla.mozilla.org/show_bug.cgi?id=1962070",
        ],
    )
    fun tabsCounterShortcutMenuNewPrivateTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewPrivateTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2343662
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1962065"])
    fun tabsCounterShortcutMenuNewTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/927315
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1962065"])
    fun privateTabsCounterShortcutMenuCloseTabTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        homeScreen {}.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
            waitForPageToLoad()
        }.goToHomescreen(composeTestRule) {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(secondWebPage.url) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
            verifyTabButtonShortcutMenuItems()
        }.closeTabFromShortcutsMenu {
            browserScreen {
                verifyTabCounter("1")
                verifyPageContent(firstWebPage.content)
            }
        }.openTabButtonShortcutsMenu {
        }.closeTabFromShortcutsMenu {
            homeScreen {
                verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2344199
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1962065"])
    fun privateTabsCounterShortcutMenuNewPrivateTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {}.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewPrivateTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2344198
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1962065"])
    fun privateTabsCounterShortcutMenuNewTabTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {}.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewTabFromShortcutsMenu {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1046683
    @Test
    fun verifySyncedTabsWhenUserIsNotSignedInTest() {
        navigationToolbar {
        }.openTabDrawer(composeTestRule) {
            verifySyncedTabsButtonIsSelected(isSelected = false)
        }.toggleToSyncedTabs {
            verifySyncedTabsButtonIsSelected(isSelected = true)
            verifySyncedTabsListWhenUserIsNotSignedIn()
        }.clickSignInToSyncButton {
            verifyTurnOnSyncMenu()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903598
    @SmokeTest
    @Test
    fun shareTabsFromTabsTrayTest() {
        val firstWebsite = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebsite = TestAssetHelper.getGenericAsset(mockWebServer, 2)
        val firstWebsiteTitle = firstWebsite.title
        val secondWebsiteTitle = secondWebsite.title
        val sharingApp = "Gmail"
        val sharedUrlsString = "${firstWebsite.url}\n\n${secondWebsite.url}"

        homeScreen {
        }.openNavigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebsite.url) {
            verifyPageContent(firstWebsite.content)
        }.openTabDrawer(composeTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebsite.url.toString()) {
            verifyPageContent(secondWebsite.content)
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            verifyExistingOpenTabs("Test_Page_2")
        }.openThreeDotMenu {
            verifyShareAllTabsButton()
        }.clickShareAllTabsButton {
            verifyShareTabsOverlay(firstWebsiteTitle, secondWebsiteTitle)
            verifySharingWithSelectedApp(
                sharingApp,
                sharedUrlsString,
                "$firstWebsiteTitle, $secondWebsiteTitle",
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/526244
    @Test
    fun privateModeStaysAsDefaultAfterRestartTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
        }.goToHomescreen(composeTestRule) {
        }.togglePrivateBrowsingMode()

        closeApp(composeTestRule.activityRule)
        restartApp(composeTestRule.activityRule)

        homeScreen {
            verifyPrivateBrowsingHomeScreenItems()
        }.openTabDrawer(composeTestRule) {
        }.toggleToNormalTabs {
            verifyExistingOpenTabs(defaultWebPage.title)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2228470
    @SmokeTest
    @Test
    fun privateTabsDoNotPersistAfterClosingAppTest() {
        val firstWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebPage.url) {
        }.openTabDrawer(composeTestRule) {
        }.openNewTab {
        }.submitQuery(secondWebPage.url.toString()) {
        }
        closeApp(composeTestRule.activityRule)
        restartApp(composeTestRule.activityRule)
        homeScreen {
            verifyPrivateBrowsingHomeScreenItems()
        }.openTabDrawer(composeTestRule) {
            verifyNoOpenTabsInPrivateBrowsing()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3024942
    @Test
    fun verifyTabsTrayListView() {
        appContext.settings().gridTabView = false

        val webPages = TestAssetHelper.getGenericAssets(mockWebServer)

        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[3].url.toString())

        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyNormalTabsList()
        }.closeTabDrawer {}
        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyOpenTabsOrder(title = webPages[0].title, position = 1, isListViewEnabled = true)
            verifyOpenTabsOrder(title = webPages[1].title, position = 2, isListViewEnabled = true)
            verifyOpenTabsOrder(title = webPages[2].title, position = 3, isListViewEnabled = true)
            verifyOpenTabsOrder(title = webPages[3].title, position = 4, isListViewEnabled = true)
            swipeTabLeft(title = webPages[0].title, isListViewEnabled = true)
            verifyOpenTabsOrder(title = webPages[1].title, position = 1, isListViewEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1126911
    @Test
    fun verifyTabsTrayGridView() {
        appContext.settings().gridTabView = true

        val webPages = TestAssetHelper.getGenericAssets(mockWebServer)

        MockBrowserDataHelper.createTabItem(webPages[0].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[1].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[2].url.toString())
        MockBrowserDataHelper.createTabItem(webPages[3].url.toString())

        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyNormalTabsList()
        }.closeTabDrawer {}
        homeScreen {
        }.openTabDrawer(composeTestRule) {
            verifyOpenTabsOrder(title = webPages[0].title, position = 1)
            verifyOpenTabsOrder(title = webPages[1].title, position = 2)
            verifyOpenTabsOrder(title = webPages[2].title, position = 3)
            verifyOpenTabsOrder(title = webPages[3].title, position = 4)
            swipeTabLeft(title = webPages[0].title)
            verifyOpenTabsOrder(title = webPages[1].title, position = 1)
        }
    }
}
