/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.click
import androidx.compose.ui.test.filter
import androidx.compose.ui.test.hasAnyChild
import androidx.compose.ui.test.hasParent
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onChildAt
import androidx.compose.ui.test.onChildren
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeLeft
import androidx.compose.ui.test.swipeRight
import mozilla.components.ui.tabcounter.TabCounterTestTags
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityComposeTestRule
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.tabstray.DefaultTabManagementFeatureHelper
import org.mozilla.fenix.tabstray.TabsTrayTestTag

fun tabDrawer(
    composeTestRule: HomeActivityComposeTestRule,
    interact: TabDrawerRobot.() -> Unit,
): TabDrawerRobot.Transition {
    TabDrawerRobot(composeTestRule).interact()
    return TabDrawerRobot.Transition(composeTestRule)
}

/**
 * Implementation of Robot Pattern for the Tabs Tray.
 */
class TabDrawerRobot(private val composeTestRule: ComposeTestRule) {

    fun verifyNormalBrowsingButtonIsSelected(isSelected: Boolean = true) {
        if (isSelected) {
            Log.i(TAG, "verifyNormalBrowsingButtonIsSelected: Trying to verify that the normal browsing button is selected")
            composeTestRule.normalBrowsingButton().assertIsSelected()
            Log.i(TAG, "verifyNormalBrowsingButtonIsSelected: Verified that the normal browsing button is selected")
        } else {
            Log.i(TAG, "verifyNormalBrowsingButtonIsSelected: Trying to verify that the normal browsing button is not selected")
            composeTestRule.normalBrowsingButton().assertIsNotSelected()
            Log.i(TAG, "verifyNormalBrowsingButtonIsSelected: Verified that the normal browsing button is not selected")
        }
    }

    fun verifyPrivateBrowsingButtonIsSelected(isSelected: Boolean = true) {
        if (isSelected) {
            Log.i(TAG, "verifyPrivateBrowsingButtonIsSelected: Trying to verify that the private browsing button is selected")
            composeTestRule.privateBrowsingButton().assertIsSelected()
            Log.i(TAG, "verifyPrivateBrowsingButtonIsSelected: Verified that the private browsing button is selected")
        } else {
            Log.i(TAG, "verifyPrivateBrowsingButtonIsSelected: Trying to verify that the private browsing button is not selected")
            composeTestRule.privateBrowsingButton().assertIsNotSelected()
            Log.i(TAG, "verifyPrivateBrowsingButtonIsSelected: Verified that the private browsing button is not selected")
        }
    }

    fun verifySyncedTabsButtonIsSelected(isSelected: Boolean = true) {
        if (isSelected) {
            Log.i(TAG, "verifySyncedTabsButtonIsSelected: Trying to verify that the synced tabs button is selected")
            composeTestRule.syncedTabsButton().assertIsSelected()
            Log.i(TAG, "verifySyncedTabsButtonIsSelected: Verified that the synced tabs button is selected")
        } else {
            Log.i(TAG, "verifySyncedTabsButtonIsSelected: Trying to verify that the synced tabs button is not selected")
            composeTestRule.syncedTabsButton().assertIsNotSelected()
            Log.i(TAG, "verifySyncedTabsButtonIsSelected: Verified that the synced tabs button is not selected")
        }
    }

    fun verifySyncedTabsListWhenUserIsNotSignedIn() {
        verifyUnauthenticatedSyncedTabsPage()
        assertUIObjectExists(
            itemContainingText(getStringResource(R.string.tab_manager_empty_synced_tabs_page_header)),
            itemContainingText(getStringResource(R.string.tab_manager_empty_synced_tabs_page_description)),
            itemWithText(getStringResource(R.string.tab_manager_empty_synced_tabs_page_sign_in_cta)),
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyExistingOpenTabs(vararg titles: String) {
        titles.forEach { title ->
            Log.i(TAG, "verifyExistingOpenTabs: Waiting for $waitingTime ms for tab with title: $title to exist")
            composeTestRule.waitUntilAtLeastOneExists(hasText(title), waitingTime)
            Log.i(TAG, "verifyExistingOpenTabs: Waited for $waitingTime ms for tab with title: $title to exist")
            Log.i(TAG, "verifyExistingOpenTabs: Trying to verify that the open tab with title: $title exists")
            composeTestRule.tabItem(title).assertExists()
            Log.i(TAG, "verifyExistingOpenTabs: Verified that the open tab with title: $title exists")
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyOpenTabsOrder(title: String, position: Int, isListViewEnabled: Boolean = false) {
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(title),
            waitingTime,
        )
        Log.i(TAG, "verifyOpenTabsOrder: Trying to verify that the open tab at position: $position has title: $title")
        when (isListViewEnabled) {
           false -> {
               composeTestRule.normalTabsListGridView()
                   .onChildAt(position - 1)
                   .assert(hasTestTag(TabsTrayTestTag.TAB_ITEM_ROOT))
                   .assert(hasAnyChild(hasText(title)))
               Log.i(
                   TAG,
                   "verifyOpenTabsOrder: Verified that the open tab at position: $position has title: $title",
               )
           }
            true -> {
                composeTestRule.normalTabsListView()
                    .onChildAt(position - 1)
                    .assert(hasAnyChild(hasText(title)))
                Log.i(
                    TAG,
                    "verifyOpenTabsOrder: Verified that the open tab at position: $position has title: $title",
                )
            }
        }
    }

    fun verifyNoExistingOpenTabs(vararg titles: String) {
        titles.forEach { title ->
            assertUIObjectExists(
                itemContainingText(title),
                exists = false,
            )
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyNormalTabsList() {
        composeTestRule.waitUntilDoesNotExist(hasTestTag("tabstray.tabList.normal.empty"), waitingTime)
        Log.i(TAG, "verifyNormalTabsList: Trying to verify that the normal tabs list exists")
        composeTestRule.normalTabsListGridView().assertExists()
        Log.i(TAG, "verifyNormalTabsList: Verified that the normal tabs list exists")
    }

    fun verifyPrivateTabsList() {
        Log.i(TAG, "verifyPrivateTabsList: Trying to verify that the private tabs list exists")
        composeTestRule.privateTabsList().assertExists()
        Log.i(TAG, "verifyPrivateTabsList: Verified that the private tabs list exists")
    }

    fun verifySyncedTabsList() {
        Log.i(TAG, "verifySyncedTabsList: Trying to verify that the synced tabs list exists")
        composeTestRule.syncedTabsList().assertExists()
        Log.i(TAG, "verifySyncedTabsList: Verified that the synced tabs list exists")
    }

    fun verifyUnauthenticatedSyncedTabsPage() {
        Log.i(TAG, "verifyUnauthenticatedSyncedTabsPage: Trying to verify that the unauthenticated synced tabs page exists")
        composeTestRule.unauthenticatedSyncedTabsPage().assertExists()
        Log.i(TAG, "verifyUnauthenticatedSyncedTabsPage: Verified that the the unauthenticated synced tabs page exists")
    }

    fun verifyNoOpenTabsInNormalBrowsing() {
        Log.i(TAG, "verifyNoOpenTabsInNormalBrowsing: Trying to verify that the empty normal tabs list exists")
        composeTestRule.emptyNormalTabsList().assertExists()
        Log.i(TAG, "verifyNoOpenTabsInNormalBrowsing: Verified that the empty normal tabs list exists")
    }

    fun verifyNoOpenTabsInPrivateBrowsing() {
        Log.i(TAG, "verifyNoOpenTabsInPrivateBrowsing: Trying to verify that the empty private tabs list exists")
        composeTestRule.emptyPrivateTabsList().assertExists()
        Log.i(TAG, "verifyNoOpenTabsInPrivateBrowsing: Verified that the empty private tabs list exists")
    }

    fun verifyAccountSettingsButton() {
        Log.i(TAG, "verifyAccountSettingsButton: Trying to verify that the \"Account settings\" menu button exists")
        composeTestRule.dropdownMenuItemAccountSettings().assertExists()
        Log.i(TAG, "verifyAccountSettingsButton: Verified that the \"Account settings\" menu button exists")
    }

    fun verifyCloseAllTabsButton() {
        Log.i(TAG, "verifyCloseAllTabsButton: Trying to verify that the \"Close all tabs\" menu button exists")
        composeTestRule.dropdownMenuItemCloseAllTabs().assertExists()
        Log.i(TAG, "verifyCloseAllTabsButton: Verified that the \"Close all tabs\" menu button exists")
    }

    fun verifySelectTabsButton() {
        Log.i(TAG, "verifySelectTabsButton: Trying to verify that the \"Select tabs\" menu button exists")
        composeTestRule.dropdownMenuItemSelectTabs().assertExists()
        Log.i(TAG, "verifySelectTabsButton: Verified that the \"Select tabs\" menu button exists")
    }

    fun verifyShareAllTabsButton() {
        Log.i(TAG, "verifyShareAllTabsButton: Trying to verify that the \"Share all tabs\" menu button exists")
        composeTestRule.dropdownMenuItemShareAllTabs().assertExists()
        Log.i(TAG, "verifyShareAllTabsButton: Verified that the \"Share all tabs\" menu button exists")
    }

    fun verifyRecentlyClosedTabsButton() {
        Log.i(TAG, "verifyRecentlyClosedTabsButton: Trying to verify that the \"Recently closed tabs\" menu button exists")
        composeTestRule.dropdownMenuItemRecentlyClosedTabs().assertExists()
        Log.i(TAG, "verifyRecentlyClosedTabsButton: Verified that the \"Recently closed tabs\" menu button exists")
    }

    fun verifyTabSettingsButton() {
        Log.i(TAG, "verifyTabSettingsButton: Trying to verify that the \"Tab settings\" menu button exists")
        composeTestRule.dropdownMenuItemTabSettings().assertExists()
        Log.i(TAG, "verifyTabSettingsButton: Verified that the \"Tab settings\" menu button exists")
    }

    fun verifyThreeDotButton() {
        Log.i(TAG, "verifyThreeDotButton: Trying to verify that the three dot button exists")
        composeTestRule.threeDotButton().assertExists()
        Log.i(TAG, "verifyThreeDotButton: Verified that the three dot button exists")
    }

    fun verifyFab() {
        Log.i(TAG, "verifyFab: Trying to verify that the new tab FAB button exists")
        composeTestRule.tabsTrayFab().assertExists()
        Log.i(TAG, "verifyFab: Verified that the new tab FAB button exists")
    }

    /**
     * Verifies a tab's thumbnail when there is only one tab open.
     */
    fun verifyTabThumbnail() {
        Log.i(TAG, "verifyTabThumbnail: Trying to verify that the tab thumbnail exists")
        composeTestRule.tabThumbnail().assertExists()
        Log.i(TAG, "verifyTabThumbnail: Verified that the tab thumbnail exists")
    }

    /**
     * Verifies a tab's close button when there is only one tab open.
     */
    fun verifyTabCloseButton() {
        Log.i(TAG, "verifyTabCloseButton: Trying to verify that the close tab button exists")
        composeTestRule.closeTabButton().assertExists()
        Log.i(TAG, "verifyTabCloseButton: Verified that the close tab button exists")
    }

    fun verifyTabTrayIsOpen() {
        Log.i(TAG, "verifyTabTrayIsOpen: Trying to verify that the tabs tray exists")
        composeTestRule.tabsTray().assertExists()
        Log.i(TAG, "verifyTabTrayIsOpen: Verified that the tabs tray exists")
    }

    fun verifyTabTrayIsClosed() {
        Log.i(TAG, "verifyTabTrayIsClosed: Trying to verify that the tabs tray does not exist")
        composeTestRule.tabsTray().assertDoesNotExist()
        Log.i(TAG, "verifyTabTrayIsClosed: Verified that the tabs tray does not exist")
    }

    /**
     * Closes a tab when there is only one tab open.
     */
    @OptIn(ExperimentalTestApi::class)
    fun closeTab() {
        Log.i(TAG, "closeTab: Waiting until the close tab button exists")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(TabsTrayTestTag.TAB_ITEM_CLOSE))
        Log.i(TAG, "closeTab: Waited until the close tab button exists")
        Log.i(TAG, "closeTab: Trying to verify that the close tab button exists")
        composeTestRule.closeTabButton().assertExists()
        Log.i(TAG, "closeTab: Verified that the close tab button exists")
        Log.i(TAG, "closeTab: Trying to click the close tab button")
        composeTestRule.closeTabButton().performClick()
        Log.i(TAG, "closeTab: Clicked the close tab button")
    }

    /**
     * Swipes a tab with [title] left.
     */
    fun swipeTabLeft(title: String, isListViewEnabled: Boolean = false) {
        Log.i(TAG, "swipeTabLeft: Trying to perform swipe left action on tab: $title")
        when (isListViewEnabled) {
            false -> composeTestRule.tabItem(title).performTouchInput { swipeLeft() }
            true -> composeTestRule.tabItemInListView(title).performTouchInput { swipeLeft() }
        }
        Log.i(TAG, "swipeTabLeft: Performed swipe left action on tab: $title")
        Log.i(TAG, "swipeTabLeft: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "swipeTabLeft: Waited for compose test rule to be idle")
    }

    /**
     * Swipes a tab with [title] right.
     */
    fun swipeTabRight(title: String, isListViewEnabled: Boolean = false) {
        Log.i(TAG, "swipeTabRight: Trying to perform swipe right action on tab: $title")
        when (isListViewEnabled) {
            false -> composeTestRule.tabItem(title).performTouchInput { swipeRight() }
            true -> composeTestRule.tabItemInListView(title).performTouchInput { swipeRight() }
        }
        Log.i(TAG, "swipeTabRight: Performed swipe right action on tab: $title")
        Log.i(TAG, "swipeTabRight: Waiting for compose test rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "swipeTabRight: Waited for compose test rule to be idle")
    }

    /**
     * Creates a collection from the provided [tabTitles].
     */
    fun createCollection(
        vararg tabTitles: String,
        collectionName: String,
        firstCollection: Boolean = true,
    ) {
        Log.i(TAG, "createCollection: Trying to click the three dot button")
        composeTestRule.threeDotButton().performClick()
        Log.i(TAG, "createCollection: Clicked the three dot button")
        Log.i(TAG, "createCollection: Trying to click the \"Select tabs\" menu button")
        composeTestRule.dropdownMenuItemSelectTabs().performClick()
        Log.i(TAG, "createCollection: Clicked the \"Select tabs\" menu button")

        for (tab in tabTitles) {
            selectTab(tab, numberOfSelectedTabs = tabTitles.indexOf(tab) + 1)
        }

        clickCollectionsButton(composeTestRule) {
            if (!firstCollection) {
                clickAddNewCollection()
            }
            typeCollectionNameAndSave(collectionName)
        }
    }

    /**
     * Selects a tab with [title].
     */
    @OptIn(ExperimentalTestApi::class)
    fun selectTab(title: String, numberOfSelectedTabs: Int = 0) {
        Log.i(TAG, "selectTab: Waiting for $waitingTime ms until the tab with title: $title exists")
        composeTestRule.waitUntilExactlyOneExists(hasText(title), waitingTime)
        Log.i(TAG, "selectTab: Waited for $waitingTime ms until the tab with title: $title exists")
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "selectTab: Trying to click tab with title: $title")
                composeTestRule.tabItem(title).performClick()
                Log.i(TAG, "selectTab: Clicked tab with title: $title")
                verifyTabsMultiSelectionCounter(numberOfSelectedTabs)

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "selectTab: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    // Dismiss tab selection
                    Log.i(TAG, "selectTab: Trying to click the device back button")
                    mDevice.pressBack()
                    Log.i(TAG, "selectTab: Clicked the device back button")
                    // Reopen tab selection section
                    Log.i(TAG, "selectTab: Trying to click the three dot button")
                    composeTestRule.threeDotButton().performClick()
                    Log.i(TAG, "selectTab: Clicked the three dot button")
                    Log.i(TAG, "selectTab: Trying to click the \"Select tabs\" menu button")
                    composeTestRule.dropdownMenuItemSelectTabs().performClick()
                    Log.i(TAG, "selectTab: Clicked the \"Select tabs\" menu button")
                }
            }
        }
    }

    /**
     * Performs a long click on a tab with [title].
     */
    fun longClickTab(title: String) {
        Log.i(TAG, "longClickTab: Trying to long click tab with title: $title")
        composeTestRule.tabItem(title)
            .performTouchInput { longClick(durationMillis = Constants.LONG_CLICK_DURATION) }
        Log.i(TAG, "longClickTab: Long clicked tab with title: $title")
    }

    /**
     * Verifies the multi selection counter displays [numOfTabs].
     */
    fun verifyTabsMultiSelectionCounter(numOfTabs: Int) {
        Log.i(TAG, "verifyTabsMultiSelectionCounter: Trying to verify that $numOfTabs tabs are selected")
        composeTestRule.multiSelectionCounter()
            .assert(hasText("$numOfTabs selected"))
        Log.i(TAG, "verifyTabsMultiSelectionCounter: Verified that $numOfTabs tabs are selected")
    }

    /**
     * Closes a tab with a given [title].
     */
    fun closeTabWithTitle(title: String) {
        Log.i(TAG, "closeTabWithTitle: Trying to click the close button for tab with title: $title")
        composeTestRule.onAllNodesWithTag(TabsTrayTestTag.TAB_ITEM_CLOSE)
            .filter(hasParent(hasText(title)))
            .onFirst()
            .performClick()
        Log.i(TAG, "closeTabWithTitle: Clicked the close button for tab with title: $title")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {

        fun openNewTab(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "openNewTab: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "openNewTab: Waited for device to be idle")
            Log.i(TAG, "openNewTab: Trying to click the new tab FAB button")
            composeTestRule.tabsTrayFab().performClick()
            Log.i(TAG, "openNewTab: Clicked the new tab FAB button")
            SearchRobot().interact()
            return SearchRobot.Transition()
        }

        fun toggleToNormalTabs(interact: TabDrawerRobot.() -> Unit): Transition {
            Log.i(TAG, "toggleToNormalTabs: Trying to click the normal browsing button")
            composeTestRule.normalBrowsingButton().performClick()
            Log.i(TAG, "toggleToNormalTabs: Clicked the normal browsing button")
            TabDrawerRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun toggleToPrivateTabs(interact: TabDrawerRobot.() -> Unit): Transition {
            Log.i(TAG, "toggleToPrivateTabs: Trying to click the private browsing button")
            composeTestRule.privateBrowsingButton().performClick()
            Log.i(TAG, "toggleToPrivateTabs: Clicked the private browsing button")
            TabDrawerRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun toggleToSyncedTabs(interact: TabDrawerRobot.() -> Unit): Transition {
            Log.i(TAG, "toggleToSyncedTabs: Trying to click the synced tabs button")
            composeTestRule.syncedTabsButton().performClick()
            Log.i(TAG, "toggleToSyncedTabs: Clicked the synced tabs button")
            TabDrawerRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun clickSignInToSyncButton(interact: SyncSignInRobot.() -> Unit): SyncSignInRobot.Transition {
            Log.i(TAG, "clickSignInToSyncButton: Trying to click the sign in to sync button and wait for $waitingTimeShort ms for a new window")
            itemWithText(getStringResource(R.string.tab_manager_empty_synced_tabs_page_sign_in_cta))
                .clickAndWaitForNewWindow(waitingTimeShort)
            Log.i(TAG, "clickSignInToSyncButton: Clicked the sign in to sync button and waited for $waitingTimeShort ms for a new window")
            SyncSignInRobot().interact()
            return SyncSignInRobot.Transition()
        }

        fun openThreeDotMenu(interact: TabDrawerRobot.() -> Unit): Transition {
            Log.i(TAG, "openThreeDotMenu: Trying to click the three dot button")
            composeTestRule.threeDotButton().performClick()
            Log.i(TAG, "openThreeDotMenu: Clicked three dot button")
            TabDrawerRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun closeAllTabs(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "closeAllTabs: Trying to click the \"Close all tabs\" menu button")
            composeTestRule.dropdownMenuItemCloseAllTabs().performClick()
            Log.i(TAG, "closeAllTabs: Clicked the \"Close all tabs\" menu button")
            HomeScreenRobot().interact()
            return HomeScreenRobot.Transition()
        }

        fun openTab(title: String, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openTab: Trying to scroll to tab with title: $title")
            composeTestRule.tabItem(title).performScrollTo()
            Log.i(TAG, "openTab: Scrolled to tab with title: $title")
            Log.i(TAG, "openTab: Trying to click tab with title: $title")
            composeTestRule.tabItem(title).performClick()
            Log.i(TAG, "openTab: Clicked tab with title: $title")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun openPrivateTab(position: Int, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openPrivateTab: Trying to click private tab at position: ${position + 1}")
            composeTestRule.privateTabsList()
                .onChildren()[position]
                .performClick()
            Log.i(TAG, "openPrivateTab: Clicked private tab at position: ${position + 1}")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun openNormalTab(position: Int, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openNormalTab: Trying to click tab at position: ${position + 1}")
            composeTestRule.normalTabsListGridView()
                .onChildren()[position]
                .performClick()
            Log.i(TAG, "openNormalTab: Clicked tab at position: ${position + 1}")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickTopBar(interact: TabDrawerRobot.() -> Unit): Transition {
            // The topBar contains other views.
            // Don't do the default click in the middle, rather click in some free space - top right.
            Log.i(TAG, "clickTopBar: Trying to click the tabs tray top bar")
            composeTestRule.banner().performTouchInput { click(position = topRight) }
            Log.i(TAG, "clickTopBar: Clicked the tabs tray top bar")
            TabDrawerRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun closeTabDrawer(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            if (DefaultTabManagementFeatureHelper.enhancementsEnabled) {
                Log.i(TAG, "closeTabDrawer: Trying to close the tabs tray by pressing the back button")
                mDevice.pressBack()
                Log.i(TAG, "closeTabDrawer: Closed the tabs tray by pressing the back button")
            } else {
                Log.i(TAG, "closeTabDrawer: Trying to close the tabs tray by clicking the handle")
                composeTestRule.bannerHandle().performSemanticsAction(SemanticsActions.OnClick)
                Log.i(TAG, "closeTabDrawer: Closed the tabs tray by clicking the handle")
            }

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickSaveCollection(interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
            Log.i(TAG, "clickSaveCollection: Trying to click the collections button")
            composeTestRule.collectionsButton().performClick()
            Log.i(TAG, "clickSaveCollection: Clicked collections button")

            CollectionRobot().interact()
            return CollectionRobot.Transition()
        }

        fun clickShareAllTabsButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareAllTabsButton: Trying to click the \"Share all tabs\" menu button button")
            composeTestRule.dropdownMenuItemShareAllTabs().performClick()
            Log.i(TAG, "clickShareAllTabsButton: Clicked the \"Share all tabs\" menu button button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }
    }
}

/**
 * Opens a transition in the [TabDrawerRobot].
 */
fun composeTabDrawer(composeTestRule: HomeActivityComposeTestRule, interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
    TabDrawerRobot(composeTestRule).interact()
    return TabDrawerRobot.Transition(composeTestRule)
}

/**
 * Clicks on the Collections button in the Tabs Tray banner and opens a transition in the [CollectionRobot].
 */
private fun clickCollectionsButton(composeTestRule: ComposeTestRule, interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
    Log.i(TAG, "clickCollectionsButton: Trying to click the collections button")
    composeTestRule.collectionsButton().performClick()
    Log.i(TAG, "clickCollectionsButton: Clicked the collections button")

    CollectionRobot().interact()
    return CollectionRobot.Transition()
}

/**
 * Obtains the root Tabs Tray.
 */
private fun ComposeTestRule.tabsTray() = onNodeWithTag(TabsTrayTestTag.TABS_TRAY)

/**
 * Obtains the Tabs Tray FAB.
 */
private fun ComposeTestRule.tabsTrayFab() = onNodeWithTag(TabsTrayTestTag.FAB)

/**
 * Obtains the normal browsing page button of the Tabs Tray banner.
 */
private fun ComposeTestRule.normalBrowsingButton() = onNodeWithTag(TabsTrayTestTag.NORMAL_TABS_PAGE_BUTTON)

/**
 * Obtains the private browsing page button of the Tabs Tray banner.
 */
private fun ComposeTestRule.privateBrowsingButton() = onNodeWithTag(TabsTrayTestTag.PRIVATE_TABS_PAGE_BUTTON)

/**
 * Obtains the synced tabs page button of the Tabs Tray banner.
 */
private fun ComposeTestRule.syncedTabsButton() = onNodeWithTag(TabsTrayTestTag.SYNCED_TABS_PAGE_BUTTON)

/**
 * Obtains the normal tabs list when in Grid view.
 */
private fun ComposeTestRule.normalTabsListGridView() = onNodeWithTag(TabsTrayTestTag.NORMAL_TABS_LIST)

/**
 * Obtains the normal tabs list when in List view.
 */
private fun ComposeTestRule.normalTabsListView() =
    onNodeWithTag(TabsTrayTestTag.NORMAL_TABS_LIST, useUnmergedTree = true)

/**
 * Obtains the private tabs list.
 */
private fun ComposeTestRule.privateTabsList() = onNodeWithTag(TabsTrayTestTag.PRIVATE_TABS_LIST)

/**
 * Obtains the synced tabs list.
 */
private fun ComposeTestRule.syncedTabsList() = onNodeWithTag(TabsTrayTestTag.SYNCED_TABS_LIST)

/**
 * Obtains the unauthenticated synced tabs page.
 */
private fun ComposeTestRule.unauthenticatedSyncedTabsPage() = onNodeWithTag(TabsTrayTestTag.UNAUTHENTICATED_SYNCED_TABS_PAGE)

/**
 * Obtains the empty normal tabs list.
 */
private fun ComposeTestRule.emptyNormalTabsList() = onNodeWithTag(TabsTrayTestTag.EMPTY_NORMAL_TABS_LIST)

/**
 * Obtains the empty private tabs list.
 */
private fun ComposeTestRule.emptyPrivateTabsList() = onNodeWithTag(TabsTrayTestTag.EMPTY_PRIVATE_TABS_LIST)

/**
 * Obtains the tab with the provided [title] in Grid view.
 */
private fun ComposeTestRule.tabItem(title: String) = onAllNodesWithTag(TabsTrayTestTag.TAB_ITEM_ROOT)
    .filter(hasAnyChild(hasText(title)))
    .onFirst()

/**
 * Obtains the tab with the provided [title] when in List view.
 */
private fun ComposeTestRule.tabItemInListView(title: String) =
    onAllNodesWithTag(TabsTrayTestTag.TAB_ITEM_ROOT, useUnmergedTree = true)
        .filter(hasAnyChild(hasText(title)))
        .onFirst()

/**
 * Obtains an open tab's close button when there's only one tab open.
 */
private fun ComposeTestRule.closeTabButton() = onNodeWithTag(TabsTrayTestTag.TAB_ITEM_CLOSE)

/**
 * Obtains an open tab's thumbnail when there's only one tab open.
 */
private fun ComposeTestRule.tabThumbnail() = onNodeWithTag(TabsTrayTestTag.TAB_ITEM_THUMBNAIL)

/**
 * Obtains the three dot button in the Tabs Tray banner.
 */
private fun ComposeTestRule.threeDotButton() = onNodeWithTag(TabsTrayTestTag.THREE_DOT_BUTTON)

/**
 * Obtains the dropdown menu item to access account settings.
 */
private fun ComposeTestRule.dropdownMenuItemAccountSettings() = onNodeWithTag(TabsTrayTestTag.ACCOUNT_SETTINGS)

/**
 * Obtains the dropdown menu item to close all tabs.
 */
private fun ComposeTestRule.dropdownMenuItemCloseAllTabs() = onNodeWithTag(TabsTrayTestTag.CLOSE_ALL_TABS)

/**
 * Obtains the dropdown menu item to access recently closed tabs.
 */
private fun ComposeTestRule.dropdownMenuItemRecentlyClosedTabs() = onNodeWithTag(TabsTrayTestTag.RECENTLY_CLOSED_TABS)

/**
 * Obtains the dropdown menu item to select tabs.
 */
private fun ComposeTestRule.dropdownMenuItemSelectTabs() = onNodeWithTag(TabsTrayTestTag.SELECT_TABS)

/**
 * Obtains the dropdown menu item to share all tabs.
 */
private fun ComposeTestRule.dropdownMenuItemShareAllTabs() = onNodeWithTag(TabsTrayTestTag.SHARE_ALL_TABS)

/**
 * Obtains the dropdown menu item to access tab settings.
 */
private fun ComposeTestRule.dropdownMenuItemTabSettings() = onNodeWithTag(TabsTrayTestTag.TAB_SETTINGS)

/**
 * Obtains the normal tabs counter.
 */
private fun ComposeTestRule.normalTabsCounter() = onNodeWithTag(TabCounterTestTags.NORMAL_TABS_COUNTER)

/**
 * Obtains the Tabs Tray banner collections button.
 */
private fun ComposeTestRule.collectionsButton() = onNodeWithTag(TabsTrayTestTag.COLLECTIONS_BUTTON)

/**
 * Obtains the Tabs Tray banner multi selection counter.
 */
private fun ComposeTestRule.multiSelectionCounter() = onNodeWithTag(TabsTrayTestTag.SELECTION_COUNTER)

/**
 * Obtains the Tabs Tray banner handle.
 */
private fun ComposeTestRule.bannerHandle() = onNodeWithTag(TabsTrayTestTag.BANNER_HANDLE)

/**
 * Obtains the media control button with the given [action] as its content description.
 */
private fun ComposeTestRule.tabMediaControlButton(action: String) = onNodeWithContentDescription(action)

/**
 * Obtains the root of the Tabs Tray banner.
 */
private fun ComposeTestRule.banner() = onNodeWithTag(TabsTrayTestTag.BANNER_ROOT)
