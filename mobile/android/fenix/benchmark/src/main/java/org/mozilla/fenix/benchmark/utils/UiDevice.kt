/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.benchmark.utils

import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector

const val WAITING_TIME_MS = 1000L

fun UiDevice.clearPackageData(packageName: String) {
    executeShellCommand("pm clear $packageName")
}

fun UiDevice.revokeNotificationPermission(packageName: String) {
    executeShellCommand("pm revoke $packageName android.permission.POST_NOTIFICATIONS")
}

fun UiDevice.isWallpaperOnboardingShown() : Boolean {
    val wallpaperFragmentText = findObject(
        UiSelector().text("Choose a wallpaper that speaks to you.")
    )
    return wallpaperFragmentText.exists()
}

fun UiDevice.dismissWallpaperOnboarding() {
    val closeButton = findObject(
        UiSelector().description("Close tab")
    )
    closeButton.click()
}

fun UiDevice.dismissCFR() {
    val cfrDismiss = findObject(
        UiSelector().resourceId("cfr.dismiss")
    )
    if (cfrDismiss.exists()) {
        cfrDismiss.click()
    }
}

fun UiDevice.completeOnboarding() {
    val dismissSetAsDefault = findObject(UiSelector().resourceId("android:id/button2"))
    dismissSetAsDefault.waitForExists(WAITING_TIME_MS)
    dismissSetAsDefault.click()

    val dismissFirefoxSearchWidget = findObject(UiSelector().text("Not now"))
    dismissFirefoxSearchWidget.waitForExists(WAITING_TIME_MS)
    dismissFirefoxSearchWidget.click()

    val dismissSignInOnboarding = findObject(UiSelector().text("Not now"))
    dismissSignInOnboarding.waitForExists(WAITING_TIME_MS)
    dismissSignInOnboarding.click()

    val enableNotificationOnboarding = findObject(UiSelector().text("Turn on notifications"))
    enableNotificationOnboarding.waitForExists(WAITING_TIME_MS)
    enableNotificationOnboarding.click()

    val systemAllow = findObject(UiSelector().text("Allow"))
    systemAllow.waitForExists(WAITING_TIME_MS)
    systemAllow.click()
}

fun UiDevice.waitUntilPageLoaded() {
    // Refresh icon toggles between refresh and stop; refresh is only shown after page has loaded
    val refresh = findObject(
        UiSelector().description("Refresh"),
    )
    refresh.waitForExists(WAITING_TIME_MS)
}

fun UiDevice.openTabsTray(useNewToolbar: Boolean) {
    val tabsTrayButton = findObject(
        UiSelector().resourceId(getTabCounterId(useNewToolbar))
    )
    tabsTrayButton.waitForExists(WAITING_TIME_MS)
    tabsTrayButton.click()
}

fun UiDevice.openNewPrivateTabOnTabsTray() {
    val pbmButton = findObject(
        UiSelector().descriptionStartsWith("Private Tabs Open:")
    )
    pbmButton.waitForExists(WAITING_TIME_MS)
    pbmButton.click()

    val newTabFab = findObject(
        UiSelector().description("Add private tab"),
    )
    newTabFab.waitForExists(WAITING_TIME_MS)
    newTabFab.click()
}

fun UiDevice.openNewTabOnTabsTray() {
    val newTabFab = findObject(
        UiSelector().description("Add tab"),
    )
    newTabFab.waitForExists(WAITING_TIME_MS)
    newTabFab.click()

    if (isWallpaperOnboardingShown()) {
        dismissWallpaperOnboarding()
    }
}

fun UiDevice.switchTabs(siteName: String, newTabUrl: String) {
    var newTabGridItem = findObject(
        UiSelector().textContains(siteName)
    )

    if (newTabGridItem.waitForExists(WAITING_TIME_MS)) {
        newTabGridItem.click()
    } else {
        newTabGridItem = findObject(
            UiSelector().textContains(newTabUrl)
        )
        newTabGridItem.waitForExists(WAITING_TIME_MS)
        newTabGridItem.click()
    }

    waitUntilPageLoaded()

    dismissCFR()
}

fun UiDevice.closeTab(siteName: String, siteUrl: String) {
    val closeTabButtonSiteName = findObject(
        UiSelector()
            .descriptionContains("Close tab")
            .descriptionContains(siteName)
    )

    if (closeTabButtonSiteName.waitForExists(WAITING_TIME_MS)) {
        closeTabButtonSiteName.click()
    } else {
        val closeTabButtonTabUrl = findObject(
            UiSelector()
                .descriptionContains("Close tab")
                .descriptionContains(siteUrl)
        )

        closeTabButtonTabUrl.waitForExists(WAITING_TIME_MS)
        closeTabButtonTabUrl.click()
    }
}

fun UiDevice.closeAllTabs() {
    val contextualMenu = findObject(
        UiSelector().description("Open tabs menu")
    )
    contextualMenu.waitForExists(WAITING_TIME_MS)
    contextualMenu.click()

    val closeAllTabsButton = findObject(
        UiSelector().text("Close all tabs")
    )
    closeAllTabsButton.waitForExists(WAITING_TIME_MS)
    closeAllTabsButton.click()
}

fun UiDevice.enterSearchMode(useNewToolbar: Boolean) {
    val urlBar = findObject(
        UiSelector().resourceId(getUrlBarId(useNewToolbar))
    )
    urlBar.waitForExists(WAITING_TIME_MS)
    urlBar.click()
}

fun UiDevice.loadSite(url: String, useNewToolbar: Boolean) {
    val urlBarEditField = findObject(
        UiSelector().resourceId(getUrlBarEditField(useNewToolbar))
    )
    urlBarEditField.setText(url)
    pressEnter()

    waitUntilPageLoaded()

    dismissCFR()
}

fun UiDevice.flingToEnd(scrollableId: String, maxSwipes: Int) {
    val scrollable = UiScrollable(UiSelector().resourceId(scrollableId))
    scrollable.waitForExists(WAITING_TIME_MS)
    scrollable.flingToEnd(maxSwipes)
}

fun UiDevice.flingToBeginning(scrollableId: String, maxSwipes: Int) {
    val scrollable = UiScrollable(UiSelector().resourceId(scrollableId))
    scrollable.waitForExists(WAITING_TIME_MS)
    scrollable.flingToBeginning(maxSwipes)
}

private fun getUrlBarId(useNewToolbar: Boolean) = when (useNewToolbar) {
    true -> "ADDRESSBAR_URL"
    false -> "$TARGET_PACKAGE:id/toolbar"
}

private fun getUrlBarEditField(useNewToolbar: Boolean) = when (useNewToolbar) {
    true -> "$TARGET_PACKAGE:id/mozac_addressbar_search_query_input"
    false -> "$TARGET_PACKAGE:id/mozac_browser_toolbar_edit_url_view"
}

private fun getTabCounterId(useNewToolbar: Boolean) = when (useNewToolbar) {
    true -> "TabCounterTestTags.tabCounter"
    false -> "$TARGET_PACKAGE:id/counter_box"
}
