/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import android.graphics.Bitmap
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.drawable.toBitmap
import androidx.core.graphics.drawable.toDrawable
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction.UpdateProgressAction
import mozilla.components.browser.state.action.ContentAction.UpdateSecurityInfoAction
import mozilla.components.browser.state.action.ContentAction.UpdateTitleAction
import mozilla.components.browser.state.action.ContentAction.UpdateUrlAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SecurityInfoState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainLooperTestRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.DisplayActions.ShareClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.EndPageActions.CustomButtonClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartBrowserActions.CloseClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartPageActions.SiteInfoClicked
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class CustomTabBrowserToolbarMiddlewareTest {
    @get:Rule
    val mainLooperRule = MainLooperTestRule()

    private val customTabId = "test"
    private val customTab: CustomTabSessionState = mockk(relaxed = true) {
        every { id } returns customTabId
    }
    private val browserStore = BrowserStore(
        BrowserState(
            customTabs = listOf(customTab),
        ),
    )
    private val permissionsStorage: SitePermissionsStorage = mockk()
    private val cookieBannersStorage: CookieBannersStorage = mockk()
    private val useCases: CustomTabsUseCases = mockk()
    private val trackingProtectionUseCases: TrackingProtectionUseCases = mockk()
    private val publicSuffixList: PublicSuffixList = mockk {
        every { getPublicSuffixPlusOne(any()) } returns CompletableDeferred(null)
    }
    private val lifecycleOwner = TestLifecycleOwner(Lifecycle.State.RESUMED)
    private val navController: NavController = mockk()
    private val closeTabDelegate: () -> Unit = mockk()
    private val settings: Settings = mockk {
        every { shouldUseBottomToolbar } returns true
    }

    @Test
    fun `GIVEN the custom tab is configured to show a close button WHEN initializing the toolbar THEN add a close button`() {
        every { customTab.config.showCloseButton } returns true
        every { customTab.config.closeButtonIcon } returns null
        val expectedCloseButton = ActionButton(
            drawable = AppCompatResources.getDrawable(testContext, R.drawable.mozac_ic_cross_24),
            contentDescription = testContext.getString(R.string.mozac_feature_customtabs_exit_button),
            onClick = CloseClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertEquals(1, toolbarBrowserActions.size)
        val closeButton = toolbarBrowserActions[0] as ActionButton
        assertNotNull(closeButton.drawable)
        assertEquals(expectedCloseButton.drawable?.state, closeButton.drawable?.state)
    }

    @Test
    fun `GIVEN the custom tab is configured to show a custom close button WHEN initializing the toolbar THEN add a close button with a custom icon`() {
        every { customTab.config.showCloseButton } returns true
        val closeButtonIcon: Bitmap = testContext.getDrawable(R.drawable.ic_back_button)!!.toBitmap(10, 10)
        every { customTab.config.closeButtonIcon } returns closeButtonIcon
        val expectedCloseButton = ActionButton(
            drawable = closeButtonIcon.toDrawable(testContext.resources),
            contentDescription = testContext.getString(R.string.mozac_feature_customtabs_exit_button),
            onClick = CloseClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertEquals(1, toolbarBrowserActions.size)
        val closeButton = toolbarBrowserActions[0] as ActionButton
        assertEquals(expectedCloseButton.contentDescription, closeButton.contentDescription)
        assertEquals(expectedCloseButton.shouldTint, closeButton.shouldTint)
        assertNotNull(closeButton.drawable)
        assertEquals(expectedCloseButton.drawable?.state, closeButton.drawable?.state)
        assertEquals(expectedCloseButton.onClick, closeButton.onClick)
    }

    @Test
    fun `GIVEN the custom tab is not configured to show a close button WHEN initializing the toolbar THEN don't add a close button`() {
        every { customTab.config.showCloseButton } returns false

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertTrue(toolbarBrowserActions.isEmpty())
    }

    @Test
    fun `GIVEN the custom tab is configured to show a custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(R.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertFalse(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN a private custom tab is configured to show a custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(R.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        every { customTab.content.private } returns true
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertTrue(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN a normal custom tab is configured to show a tinted custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(R.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        every { customTab.config.actionButtonConfig?.tint } returns true
        every { customTab.content.private } returns false
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertTrue(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN the url if of a local file WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.url } returns "content://test"
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_page_portrait_24,
            contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is secure WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.securityInfo.secure } returns true
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is insecure WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.securityInfo.secure } returns false
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_shield_slash_24,
            contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN an environment was already set WHEN it is cleared THEN reset it to null`() {
        val middleware = buildMiddleware()
        val store = buildStore(middleware)

        assertNotNull(middleware.environment)

        store.dispatch(EnvironmentCleared)

        assertNull(middleware.environment)
    }

    @Test
    fun `GIVEN the website is insecure WHEN the conection becomes secure THEN update appropriate security indicator`() = runTest {
        val customTab = createCustomTab(url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedSecureIndicator = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val expectedInsecureIndicator = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_shield_slash_24,
            contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val toolbarStore = buildStore(middleware)
        mainLooperRule.idle()
        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var securityIndicator = toolbarPageActions[0]
        assertEquals(expectedInsecureIndicator, securityIndicator)

        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfoState(true))).joinBlocking()
        mainLooperRule.idle()
        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecureIndicator, securityIndicator)
    }

    @Test
    fun `WHEN the website title changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "URL",
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        mainLooperRule.idle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateTitleAction(customTabId, "UpdatedTitle")).joinBlocking()
        mainLooperRule.idle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedDetails.copy(title = "UpdatedTitle"), pageOrigin)
    }

    @Test
    fun `GIVEN no title available WHEN the website url changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        var expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = "URL",
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        mainLooperRule.idle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateUrlAction(customTabId, "UpdatedURL")).joinBlocking()
        mainLooperRule.idle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedDetails.copy(url = "UpdatedURL"), pageOrigin)
    }

    @Test
    fun `GIVEN a title previously available WHEN the website url changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        var expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "URL",
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        mainLooperRule.idle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateUrlAction(customTabId, "UpdatedURL")).joinBlocking()
        mainLooperRule.idle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(
            expectedDetails.copy(
                // If a title was used previously and not available after then the URL is shown as title also.
                title = "UpdatedURL",
                url = "UpdatedURL",
            ),
            pageOrigin,
        )
    }

    @Test
    fun `GIVEN an url with an ip address for the domain WHEN showing displaying the page origin THEN correctly infer the ip address as the domain`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "http://127.0.0.1/test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedPageOrigin = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "127.0.0.1",
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        mainLooperRule.idle()
        val pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedPageOrigin, pageOrigin)
    }

    @Test
    fun `GIVEN the custom tab is not configured to show a share button WHEN initializing the toolbar THEN show just a menu button`() {
        every { customTab.config.showShareMenuItem } returns false
        val expectedMenuButton = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            onClick = MenuClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(1, toolbarBrowserActions.size)
        val menuButton = toolbarBrowserActions[0]
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN the custom tab is configured to show a share button WHEN initializing the toolbar THEN show both a share and a menu buttons`() {
        every { customTab.config.showShareMenuItem } returns true
        val expectedShareButton = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_share_android_24,
            contentDescription = R.string.mozac_feature_customtabs_share_link,
            onClick = ShareClicked,
        )
        val expectedMenuButton = ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            onClick = MenuClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val shareButton = toolbarBrowserActions[0]
        val menuButton = toolbarBrowserActions[1]
        assertEquals(expectedShareButton, shareButton)
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN a bottom toolbar WHEN the loading progress changes THEN update the progress bar`() = runTest {
        every { settings.shouldUseBottomToolbar } returns true
        val customTab = createCustomTab(url = "test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(
                customTabs = listOf(customTab),
            ),
        )
        val middleware = buildMiddleware(browserStore)
        val toolbarStore = buildStore(middleware)

        browserStore.dispatch(UpdateProgressAction(customTabId, 50)).joinBlocking()
        mainLooperRule.idle()
        assertEquals(
            ProgressBarConfig(
                progress = 50,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )

        browserStore.dispatch(UpdateProgressAction(customTabId, 80)).joinBlocking()
        mainLooperRule.idle()
        assertEquals(
            ProgressBarConfig(
                progress = 80,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    @Test
    fun `GIVEN a top toolbar WHEN the loading progress changes THEN update the progress bar`() = runTest {
        every { settings.shouldUseBottomToolbar } returns false
        val customTab = createCustomTab(url = "test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(
                customTabs = listOf(customTab),
            ),
        )
        val middleware = buildMiddleware(browserStore)
        val toolbarStore = buildStore(middleware)

        browserStore.dispatch(UpdateProgressAction(customTabId, 22)).joinBlocking()
        mainLooperRule.idle()
        assertEquals(
            ProgressBarConfig(
                progress = 22,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )

        browserStore.dispatch(UpdateProgressAction(customTabId, 67)).joinBlocking()
        mainLooperRule.idle()
        assertEquals(
            ProgressBarConfig(
                progress = 67,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    private fun buildMiddleware(
        browserStore: BrowserStore = this.browserStore,
        permissionsStorage: SitePermissionsStorage = this.permissionsStorage,
        cookieBannersStorage: CookieBannersStorage = this.cookieBannersStorage,
        useCases: CustomTabsUseCases = this.useCases,
        trackingProtectionUseCases: TrackingProtectionUseCases = this.trackingProtectionUseCases,
        publicSuffixList: PublicSuffixList = this.publicSuffixList,
        settings: Settings = this.settings,
    ) = CustomTabBrowserToolbarMiddleware(
        customTabId = this.customTabId,
        browserStore = browserStore,
        permissionsStorage = permissionsStorage,
        cookieBannersStorage = cookieBannersStorage,
        useCases = useCases,
        trackingProtectionUseCases = trackingProtectionUseCases,
        publicSuffixList = publicSuffixList,
        settings = settings,
    )

    private fun buildStore(
        middleware: CustomTabBrowserToolbarMiddleware = buildMiddleware(),
        context: Context = testContext,
        lifecycleOwner: LifecycleOwner = this@CustomTabBrowserToolbarMiddlewareTest.lifecycleOwner,
        navController: NavController = this@CustomTabBrowserToolbarMiddlewareTest.navController,
        closeTabDelegate: () -> Unit = this@CustomTabBrowserToolbarMiddlewareTest.closeTabDelegate,
    ) = BrowserToolbarStore(
        middleware = listOf(middleware),
    ).also {
        it.dispatch(
            EnvironmentRehydrated(
                CustomTabToolbarEnvironment(
                    context = context,
                    viewLifecycleOwner = lifecycleOwner,
                    navController = navController,
                    closeTabDelegate = closeTabDelegate,
                ),
            ),
        )
    }
}
