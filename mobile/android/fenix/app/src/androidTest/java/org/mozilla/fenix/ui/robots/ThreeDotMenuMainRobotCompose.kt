/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_OFF
import org.mozilla.fenix.components.menu.MenuDialogTestTag.DESKTOP_SITE_ON
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS
import org.mozilla.fenix.components.menu.MenuDialogTestTag.EXTENSIONS_OPTION_CHEVRON
import org.mozilla.fenix.components.menu.MenuDialogTestTag.MORE_OPTION_CHEVRON
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated

class ThreeDotMenuMainRobotCompose(private val composeTestRule: ComposeTestRule) {

    fun verifyMainMenuCFR() {
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR title is displayed.")
        composeTestRule.mainMenuCFRTitle().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR title is displayed.")
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR message is displayed.")
        composeTestRule.mainMenuCFRMessage().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR message is displayed.")
        Log.i(TAG, "verifyMainMenuCFR: Trying to verify the main menu CFR dismiss button is displayed.")
        composeTestRule.closeMainMenuCFRButton().assertIsDisplayed()
        Log.i(TAG, "verifyMainMenuCFR: Verified the main menu CFR dismiss button is displayed.")
    }

    fun verifyHomeMainMenuItems() {
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify the main menu items on the home page.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Back\" button exists.")
        composeTestRule.backButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Back\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Forward\" button exists.")
        composeTestRule.forwardButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Forward\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Refresh\" button exists.")
        composeTestRule.refreshButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Refresh\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Share\" button exists.")
        composeTestRule.shareButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Share\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Make Firefox your default\" button exists.")
        verifyMakeFirefoxYourDefaultBrowserPromotionBanner()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Make Firefox your default\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.extensionsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Extensions\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"History\" button exists.")
        composeTestRule.historyButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"History\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Bookmarks\" button exists.")
        composeTestRule.bookmarksButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Bookmarks\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Downloads\" button exists.")
        composeTestRule.downloadsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Downloads\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Passwords\" button exists.")
        composeTestRule.passwordsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Passwords\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Sign in\" button exists.")
        composeTestRule.signInButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Sign in\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Trying to verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(TAG, "verifyHomeMainMenuItems: Verified that the \"Settings\" button exists.")
        Log.i(TAG, "verifyHomeMainMenuItems: Verified the main menu items on the home page.")
    }

    fun verifyPageMainMenuItems() {
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify the main menu items on the web page.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Back\" button exists.")
        composeTestRule.backButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Back\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Forward\" button exists.")
        composeTestRule.forwardButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Forward\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Refresh\" button exists.")
        composeTestRule.refreshButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Refresh\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Share\" button exists.")
        composeTestRule.shareButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Share\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Bookmark page\" button exists.")
        composeTestRule.bookmarkPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Bookmark page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Find in page\" button exists.")
        composeTestRule.findInPageButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Find in page\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Desktop site\" button exists.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Desktop site\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Extensions\" button exists.")
        composeTestRule.extensionsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Extensions\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"More\" button exists.")
        composeTestRule.moreButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"More\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"History\" button exists.")
        composeTestRule.historyButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"History\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Bookmarks\" button exists.")
        composeTestRule.bookmarksButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Bookmarks\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Downloads\" button exists.")
        composeTestRule.downloadsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Downloads\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Passwords\" button exists.")
        composeTestRule.passwordsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Passwords\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Sign in\" button exists.")
        composeTestRule.signInButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Sign in\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Trying to verify that the \"Settings\" button exists.")
        composeTestRule.settingsButton().assertIsDisplayed()
        Log.i(TAG, "verifyPageMainMenuItems: Verified that the \"Settings\" button exists.")
        Log.i(TAG, "verifyPageMainMenuItems: Verified the main menu items on the web page.")
    }

    fun openToolsMenu() {
        Log.i(
            TAG,
            "openToolsMenu: Trying to click the Tools menu button from the new main menu design.",
        )
        composeTestRule.toolsMenuButton().performClick()
        composeTestRule.waitForIdle()
        Log.i(TAG, "openToolsMenu: Clicked the Tools menu button from the new main menu design.")
    }

    fun clickPrintContentButton() {
        Log.i(TAG, "clickPrintContentButton: Trying to click the \"Print…\" button.")
        composeTestRule.printContentButton().performClick()
        Log.i(TAG, "clickPrintContentButton: Clicked the \"Print…\" button.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifySwitchToDesktopSiteButtonIsEnabled(isEnabled: Boolean) {
        Log.i(TAG, "verifySuggestedUserName: Waiting for the \"Desktop site\" button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(getStringResource(R.string.browser_menu_desktop_site), substring = true))
        Log.i(TAG, "verifySuggestedUserName: Waited for the \"Desktop site\" button to exist")
        Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Trying to verify the Switch to Desktop Site button from the new main menu design is enabled.")
        if (isEnabled) {
            composeTestRule.desktopSiteButton().assertIsEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is enabled.")
        } else {
            composeTestRule.desktopSiteButton().assertIsNotEnabled()
            Log.i(TAG, "verifySwitchToDesktopSiteButtonIsEnabled: Verified the Switch to Desktop Site button from the new main menu design is disabled.")
        }
    }

    fun verifySwitchToDesktopSiteButton() {
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Trying to verify that the \"Switch to desktop site\" button is displayed.")
        composeTestRule.desktopSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifySwitchToDesktopSiteButton: Verified that the \"Switch to desktop site\" button is displayed.")
    }

    fun verifyDesktopSiteButtonState(isEnabled: Boolean) {
        if (isEnabled) {
            Log.i(TAG, "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"On\".")
            composeTestRule.enabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"On\".")
        } else {
            Log.i(TAG, "verifyDesktopSiteButtonState: Trying to verify that the \"Desktop site\" button is set to \"Off\".")
            composeTestRule.disabledDesktopSiteButton().assertIsDisplayed()
            Log.i(TAG, "verifyDesktopSiteButtonState: Verified that the \"Desktop site\" button is set to \"Off\".")
        }
    }

    fun clickSwitchToDesktopSiteButton() {
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Trying to click the \"Switch to desktop site\" button.")
        composeTestRule.desktopSiteButton().performClick()
        Log.i(TAG, "clickSwitchToDesktopSiteButton: Clicked the \"Switch to desktop site\" button.")
    }

    fun verifyOpenInAppButtonIsEnabled(appName: String = "", isEnabled: Boolean) {
        Log.i(
            TAG,
            "verifyOpenInAppButtonIsEnabled: Trying to verify the Open in App button from the new main menu design is enabled.",
        )
        when (appName) {
            "" -> composeTestRule.defaultOpenInAppButton().apply {
                if (isEnabled) assertIsEnabled() else assertIsNotEnabled()
                Log.i(
                    TAG,
                    "verifyOpenInAppButtonIsEnabled: Open in App button from the new main menu design is enabled = $isEnabled.",
                )
            }
            else -> composeTestRule.openInAppNameButton(appName).apply {
                if (isEnabled) assertIsEnabled() else assertIsNotEnabled()
                Log.i(
                    TAG,
                    "verifyOpenInAppButtonIsEnabled: Open in App button from the new main menu design is enabled = $isEnabled.",
                )
            }
        }
    }

    fun clickOpenInAppButton(appName: String = "") {
        Log.i(
            TAG,
            "clickOpenInAppButton: Trying to click the Open in App button from the new main menu design.",
        )
        when (appName) {
            "" -> composeTestRule.defaultOpenInAppButton().performClick()
            else -> composeTestRule.openInAppNameButton(appName).performClick()
        }
        Log.i(
            TAG,
            "clickOpenInAppButton: Clicked the Open in App button from the new main menu design.",
        )
        mDevice.waitForIdle()
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyExtensionsButtonWithInstalledExtension(extensionTitle: String) {
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Waiting for $waitingTime for the collapsed \"Extensions\" button with installed $extensionTitle to exist.")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription(extensionTitle, substring = true, ignoreCase = true), waitingTime)
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Waited for $waitingTime for the collapsed \"Extensions\" button with installed $extensionTitle to exist.")
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Trying to verify that the collapsed \"Extensions\" button with installed $extensionTitle exists.")
        composeTestRule.onNode(
            hasTestTag("mainMenu.extensions"),
        ).assert(
            hasContentDescription(extensionTitle, substring = true, ignoreCase = true),
        ).assertIsDisplayed()
        Log.i(TAG, "verifyExtensionsButtonWithInstalledExtension: Verified that the collapsed \"Extensions\" button with installed $extensionTitle exists.")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyTryRecommendedExtensionButton() {
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Waiting for $waitingTime for the \"Extensions - Try a recommended extension\" button to exists.")
        composeTestRule.waitUntilAtLeastOneExists(hasContentDescription("Extensions Try a recommended extension", substring = true), waitingTime)
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Waited for $waitingTime for the \"Extensions - Try a recommended extension\" button to exists.")
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Trying to verify that the \"Extensions - Try a recommended extension\" button exists.")
        composeTestRule.tryRecommendedExtensionButton().assertExists()
        Log.i(TAG, "verifyTryRecommendedExtensionButton: Verified that the \"Extensions - Try a recommended extension\" button exists.")
    }

    fun verifyNoExtensionsEnabledButton() {
        Log.i(TAG, "verifyNoExtensionsEnabledButton: Trying to verify that the \"Extensions - Try a recommended extension\" button exists.")
        composeTestRule.noExtensionsEnabledButton().assertExists()
        Log.i(TAG, "verifyNoExtensionsEnabledButton: Verified that the \"Extensions - Try a recommended extension\" button exists.")
    }

    fun clickSaveButton() {
        Log.i(TAG, "clickSaveButton: Trying to click the \"Save\" button from the new main menu design.")
        composeTestRule.saveMenuButton().performClick()
        Log.i(TAG, "clickSaveButton: Clicked the \"Save\" button from the new main menu design.")
    }

    fun openMoreMenu() {
        Log.i(TAG, "openMoreMenu: Trying to click the \"More\" button from the new main menu design.")
        composeTestRule.moreButton().performClick()
        Log.i(TAG, "openMoreMenu: Clicked the \"More\" button from the new main menu design.")
        waitForAppWindowToBeUpdated()
    }

    fun clickMoreOptionChevron() {
        Log.i(TAG, "clickMoreOptionChevron: Trying to click the \"More option chevron\" button from the new main menu design.")
        composeTestRule.moreChevronButton().performClick()
        Log.i(TAG, "clickMoreOptionChevron: Clicked the \"More option chevron\" button from the new main menu design.")
        waitForAppWindowToBeUpdated()
    }

    fun clickLessOptionChevron() {
        Log.i(TAG, "clickLessOptionChevron: Trying to click the \"Less option chevron\" button from the new main menu design.")
        composeTestRule.lessChevronButton().performClick()
        Log.i(TAG, "clickLessOptionChevron: Clicked the \"Less option chevron\" button from the new main menu design.")
    }

    fun verifyBookmarkThisPageButton() {
        composeTestRule.bookmarkPageButton().assertIsDisplayed()
    }

    fun clickQuitFirefoxButton() {
        Log.i(TAG, "clickQuitFirefoxButton: Trying to click the \"Quit $appName\" button from the new main menu design.")
        composeTestRule.quitFirefoxButton().performClick()
        Log.i(TAG, "clickQuitFirefoxButton: Clicked the \"Quit $appName\" button from the new main menu design.")
    }

    fun verifyTranslatePageButton() {
        Log.i(TAG, "verifyTranslatePageButton: Trying to verify that the \"Translate page\" button exists.")
        composeTestRule.translatePageButton().assertIsDisplayed()
        Log.i(TAG, "verifyTranslatePageButton: Verified that the \"Translate page\" button exists.")
    }

    fun verifyMakeFirefoxYourDefaultBrowserPromotionBanner() {
        composeTestRule.onNodeWithText(getStringResource(R.string.browser_menu_default_banner_title, appName)).assertIsDisplayed()
        composeTestRule.onNodeWithText(getStringResource(R.string.browser_menu_default_banner_subtitle_2)).assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription(getStringResource(R.string.browser_menu_default_banner_dismiss_promotion)).assertIsDisplayed()
    }

    fun verifyMoreMainMenuItems() {
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify the more main menu items on the web page.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Less\" button exists.")
        composeTestRule.lessButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Less\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Translate page\" button exists.")
        composeTestRule.translatePageButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Translate page\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Report broken site\" button exists.")
        composeTestRule.reportBrokenSiteButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Report broken site\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Add to shortcuts\" button exists.")
        composeTestRule.addToShortcutsButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Add to shortcuts\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Add app to Home screen\" button exists.")
        composeTestRule.addToHomeScreenButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Add app to Home screen\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Save to collection\" button exists.")
        composeTestRule.saveToCollectionButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Save to collection\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Open in app\" button exists.")
        composeTestRule.defaultOpenInAppButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Open in app\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Save as PDF\" button exists.")
        composeTestRule.saveAsPDFButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Save as PDF\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Trying to verify that the \"Print\" button exists.")
        composeTestRule.printContentButton().assertIsDisplayed()
        Log.i(TAG, "verifyMoreMainMenuItems: Verified that the \"Print\" button exists.")
        Log.i(TAG, "verifyMoreMainMenuItems: Verified the more main menu items on the web page.")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun openSettings(
            interact: SettingsRobot.() -> Unit,
        ): SettingsRobot.Transition {
            Log.i(
                TAG,
                "openSettings: Trying to click the Settings button from the new main menu design.",
            )
            composeTestRule.settingsButton().performClick()
            Log.i(TAG, "openSettings: Clicked the Settings button from the new main menu design.")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun clickFindInPageButton(
            interact: FindInPageRobot.() -> Unit,
        ): FindInPageRobot.Transition {
            Log.i(
                TAG,
                "clickFindInPageButton: Trying to click the FindInPage button from the new main menu design.",
            )
            composeTestRule.findInPageButton().performClick()
            Log.i(TAG, "clickFindInPageButton: Clicked the FindInPage button from the new main menu design.")

            FindInPageRobot().interact()
            return FindInPageRobot.Transition()
        }

        fun openBookmarks(
            composeTestRule: ComposeTestRule,
            interact: BookmarksRobot.() -> Unit,
        ): BookmarksRobot.Transition {
            Log.i(
                TAG,
                "openBookmarks: Trying to click the Bookmarks button from the new main menu design.",
            )
            composeTestRule.bookmarksButton().performClick()
            Log.i(TAG, "openBookmarks: Clicked the Bookmarks button from the new main menu design.")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickSignInMainMenuButton(
            composeTestRule: ComposeTestRule,
            interact: SyncSignInRobot.() -> Unit,
        ): SyncSignInRobot.Transition {
            Log.i(TAG, "clickSignInMainMenuButton: Trying to click \"Sign in\" main menu button")
            composeTestRule.onNodeWithContentDescription("Sign in Sync passwords, bookmarks, and more").performClick()
            Log.i(TAG, "clickSignInMainMenuButton: Clicked \"Sign in\" main menu button")

            SyncSignInRobot().interact()
            return SyncSignInRobot.Transition()
        }

        fun openHistory(
            interact: HistoryRobot.() -> Unit,
        ): HistoryRobot.Transition {
            Log.i(
                TAG,
                "openHistory: Trying to click the History button from the new main menu design.",
            )
            composeTestRule.historyButton().performClick()
            Log.i(TAG, "openHistory: Clicked the History button from the new main menu design.")

            HistoryRobot().interact()
            return HistoryRobot.Transition()
        }

        fun openDownloads(
            interact: DownloadRobot.() -> Unit,
        ): DownloadRobot.Transition {
            Log.i(
                TAG,
                "openDownloads: Trying to click the Download button from the new main menu design.",
            )
            composeTestRule.downloadsButton().performClick()
            Log.i(TAG, "openDownloads: Clicked the Download button from the new main menu design.")

            DownloadRobot().interact()
            return DownloadRobot.Transition()
        }

        fun openPasswords(
            interact: SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.() -> Unit,
        ): SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition {
            Log.i(
                TAG,
                "openPasswords: Trying to click the Download button from the new main menu design.",
            )
            composeTestRule.passwordsButton().performClick()
            Log.i(TAG, "openPasswords: Clicked the Download button from the new main menu design.")

            SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot().interact()
            return SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition()
        }

        fun openExtensionsFromMainMenu(interact: SettingsSubMenuAddonsManagerRobot.() -> Unit): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "openExtensionsFromMainMenu: Trying to click the \"Extensions\" button")
            assertUIObjectExists(itemWithResId("mainMenu.extensions"))
            itemWithResId("mainMenu.extensions").clickAndWaitForNewWindow(waitingTimeShort)
            Log.i(TAG, "openExtensionsFromMainMenu: Clicked the \"Extensions\" button")
            composeTestRule.waitForIdle()
            waitForAppWindowToBeUpdated()

            SettingsSubMenuAddonsManagerRobot().interact()
            return SettingsSubMenuAddonsManagerRobot.Transition()
        }

        fun clickExtensionsChevronFromMainMenu(interact: SettingsSubMenuAddonsManagerRobot.() -> Unit): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "clickExtensionsChevronFromMainMenu: Trying to click the \"Extensions chevron\" button from the new main menu design.")
            composeTestRule.extensionsChevronButton().assertExists()
            composeTestRule.extensionsChevronButton().performClick()
            Log.i(TAG, "clickExtensionsChevronFromMainMenu: Clicked the \"Extensions chevron\" button from the new main menu design.")
            composeTestRule.waitForIdle()
            waitForAppWindowToBeUpdated()

            SettingsSubMenuAddonsManagerRobot().interact()
            return SettingsSubMenuAddonsManagerRobot.Transition()
        }

        fun clickBookmarkThisPageButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickBookmarkThisPageButton: Trying to click the \"Bookmark this page\" button from the new main menu design.")
            composeTestRule.bookmarkPageButton().performClick()
            Log.i(TAG, "clickBookmarkThisPageButton: Clicked the \"Bookmark this page\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickAddToShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickAddToShortcutsButton: Trying to click the \"Add to shortcuts\" button from the new main menu design.")
            composeTestRule.addToShortcutsButton().performClick()
            Log.i(TAG, "clickAddToShortcutsButton: Clicked the \"Add to shortcuts\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickRemoveFromShortcutsButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickRemoveFromShortcutsButton: Trying to click the \"Remove from shortcuts\" button from the new main menu design.")
            composeTestRule.removeFromShortcutsButton().performClick()
            Log.i(TAG, "clickRemoveFromShortcutsButton: Clicked the \"Remove from shortcuts\" button from the new main menu design.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickEditBookmarkButton(composeTestRule: ComposeTestRule, interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickEditBookmarkButton: Trying to click the \"Edit bookmark\" button from the new main menu design.")
            composeTestRule.editBookmarkButton().performClick()
            Log.i(TAG, "clickEditBookmarkButton: Clicked the \"Edit bookmark\" button from the new main menu design.")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickAddToHomeScreenButton(interact: AddToHomeScreenRobot.() -> Unit): AddToHomeScreenRobot.Transition {
            Log.i(TAG, "clickAddToHomeScreenButton: Trying to click the \"Add to Home screen…\" button from the new main menu design.")
            composeTestRule.addToHomeScreenButton().performClick()
            Log.i(TAG, "clickAddToHomeScreenButton: Clicked the \"Add to Home screen…\" button from the new main menu design.")

            AddToHomeScreenRobot().interact()
            return AddToHomeScreenRobot.Transition()
        }

        fun clickSaveToCollectionButton(interact: CollectionRobot.() -> Unit): CollectionRobot.Transition {
            Log.i(TAG, "clickSaveToCollectionButton: Trying to click the \"Save to collection…\" button from the new main menu design.")
            composeTestRule.saveToCollectionButton().performClick()
            Log.i(TAG, "clickSaveToCollectionButton: Clicked the \"Save to collection…\" button from the new main menu design.")

            CollectionRobot().interact()
            return CollectionRobot.Transition()
        }

        fun clickSaveAsPDFButton(interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
            Log.i(TAG, "clickSaveAsPDFButton: Trying to click the \"Save as PDF…\" button from the new main menu design.")
            composeTestRule.saveAsPDFButton().performClick()
            Log.i(TAG, "clickSaveAsPDFButton: Clicked the \"Save as PDF…\" button from the new main menu design.")

            DownloadRobot().interact()
            return DownloadRobot.Transition()
        }

        fun clickTranslateButton(interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(TAG, "clickTranslateButton: Trying to click the Translate button from the new main menu design.")
            composeTestRule.translatePageButton().performClick()
            Log.i(TAG, "clickTranslateButton: Clicked the Translate button from the new main menu design.")
            Log.i(TAG, "clickTranslateButton: Waiting for compose test rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "clickTranslateButton: Waited for compose test rule to be idle")

            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        fun clickTranslatedButton(interact: TranslationsRobot.() -> Unit): TranslationsRobot.Transition {
            Log.i(
                TAG,
                "clickTranslateButton: Trying to click the Translate button from the new main menu design.",
            )
            composeTestRule.translatedButton().assertIsDisplayed()
            composeTestRule.translatedButton().performClick()
            Log.i(
                TAG,
                "clickTranslateButton: Clicked the Translate button from the new main menu design.",
            )
            TranslationsRobot(composeTestRule).interact()
            return TranslationsRobot.Transition(composeTestRule)
        }

        fun clickShareButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareButton: Trying to click the Share button from the new main menu design.")
            composeTestRule.shareButton().performClick()
            Log.i(TAG, "clickShareButton: Clicked the Share button from the new main menu design.")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickOutsideTheMainMenu(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOutsideTheMainMenu: Trying to click outside the main menu.")
            itemWithResId("$packageName:id/touch_outside").clickTopLeft()
            Log.i(TAG, "clickOutsideTheMainMenu: Clicked click outside the main menu.")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickReportBrokenSiteButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openReportSiteIssue: Trying to click the \"Report Site Issue\" button")
            composeTestRule.reportBrokenSiteButton().performClick()
            Log.i(TAG, "openReportSiteIssue: Clicked the \"Report Site Issue\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun openReportBrokenSite(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "openReportBrokenSite: Trying to click the \"Report broken site\" button")
            composeTestRule.reportBrokenSiteButton().performClick()
            Log.i(TAG, "openReportBrokenSite: Clicked the \"Report broken site\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun closeMainMenu(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "closeMainMenu: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "closeMainMenu: Clicked the device back button")

            HomeScreenRobot().interact()
            return HomeScreenRobot.Transition()
        }

        fun goForward(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goForward: Trying to click the \"Forward\" button")
            composeTestRule.forwardButton().performClick()
            Log.i(TAG, "goForward: Clicked the \"Forward\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun goToPreviousPage(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goToPreviousPage: Trying to click the \"Back\" button")
            composeTestRule.backButton().performClick()
            Log.i(TAG, "goToPreviousPage: Clicked the \"Back\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }

        fun clickRefreshButton(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickRefreshButton: Trying to click the \"Refresh\" button")
            composeTestRule.refreshButton().performClick()
            Log.i(TAG, "clickRefreshButton: Clicked the \"Refresh\" button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }
    }
}

fun mainMenuScreen(composeTestRule: ComposeTestRule, interact: ThreeDotMenuMainRobotCompose.() -> Unit): ThreeDotMenuMainRobotCompose.Transition {
    ThreeDotMenuMainRobotCompose(composeTestRule).interact()
    return ThreeDotMenuMainRobotCompose.Transition(composeTestRule)
}

private fun ComposeTestRule.mainMenuCFRTitle() = onNodeWithText(getStringResource(R.string.menu_cfr_title))

private fun ComposeTestRule.mainMenuCFRMessage() = onNodeWithText(getStringResource(R.string.menu_cfr_body))

private fun ComposeTestRule.closeMainMenuCFRButton() = onNodeWithTag("cfr.dismiss")

private fun ComposeTestRule.backButton() = onNodeWithText("Back")

private fun ComposeTestRule.forwardButton() = onNodeWithText("Forward")

private fun ComposeTestRule.refreshButton() = onNodeWithText("Refresh")

private fun ComposeTestRule.shareButton() = onNodeWithText("Share")

private fun ComposeTestRule.signInButton() = onNodeWithContentDescription("Sign in Sync passwords, bookmarks, and more")

private fun ComposeTestRule.settingsButton() = onNodeWithContentDescription("Settings")

private fun ComposeTestRule.extensionsButton() = onNodeWithTag(EXTENSIONS)

private fun ComposeTestRule.tryRecommendedExtensionButton() = onNodeWithContentDescription("Extensions Try a recommended extension", substring = true)

private fun ComposeTestRule.noExtensionsEnabledButton() = onNodeWithContentDescription("Extensions No extensions enabled", substring = true)

private fun ComposeTestRule.moreButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_more_settings), substring = true)

private fun ComposeTestRule.moreChevronButton() = onNodeWithTag(MORE_OPTION_CHEVRON, useUnmergedTree = true)

private fun ComposeTestRule.lessChevronButton() = onNodeWithTag(MORE_OPTION_CHEVRON, useUnmergedTree = true)

private fun ComposeTestRule.bookmarksButton() = onNodeWithContentDescription(getStringResource(R.string.library_bookmarks), substring = true)

private fun ComposeTestRule.historyButton() = onNodeWithContentDescription(getStringResource(R.string.library_history), substring = true)

private fun ComposeTestRule.downloadsButton() = onNodeWithContentDescription(getStringResource(R.string.library_downloads), substring = true)

private fun ComposeTestRule.passwordsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_passwords), substring = true)

private fun ComposeTestRule.backToMainMenuButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_back_button_content_description))

private fun ComposeTestRule.quitFirefoxButton() = onNodeWithContentDescription("Quit $appName")

// Page main menu items

private fun ComposeTestRule.findInPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_find_in_page), substring = true)

private fun ComposeTestRule.toolsMenuButton() = onNodeWithTag("mainMenu.tools")

private fun ComposeTestRule.saveMenuButton() = onNodeWithTag("mainMenu.save")

private fun ComposeTestRule.bookmarkPageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_bookmark_this_page_2))

private fun ComposeTestRule.desktopSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_desktop_site), substring = true)

private fun ComposeTestRule.enabledDesktopSiteButton() = onNodeWithTag(DESKTOP_SITE_ON)

private fun ComposeTestRule.disabledDesktopSiteButton() = onNodeWithTag(DESKTOP_SITE_OFF)

// Save sub menu items

private fun ComposeTestRule.editBookmarkButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_edit_bookmark))

private fun ComposeTestRule.addToShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_shortcuts))

private fun ComposeTestRule.removeFromShortcutsButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_remove_from_shortcuts))

private fun ComposeTestRule.addToHomeScreenButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_add_to_homescreen))

private fun ComposeTestRule.saveToCollectionButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_to_collection_2))

private fun ComposeTestRule.saveAsPDFButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_save_as_pdf_2))

private fun ComposeTestRule.translatePageButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translations))

private fun ComposeTestRule.translatedButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_translated))

private fun ComposeTestRule.reportBrokenSiteButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_webcompat_reporter_2))

private fun ComposeTestRule.printContentButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_print_2))

private fun ComposeTestRule.defaultOpenInAppButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_app_link))

private fun ComposeTestRule.openInAppNameButton(appName: String) = onNodeWithContentDescription(getStringResource(R.string.browser_menu_open_in_fenix, appName))

private fun ComposeTestRule.lessButton() = onNodeWithContentDescription(getStringResource(R.string.browser_menu_less_settings), substring = true)

private fun ComposeTestRule.extensionsChevronButton() = onNodeWithTag(EXTENSIONS_OPTION_CHEVRON, useUnmergedTree = true)
