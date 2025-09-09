/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.controller

import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabstray.ui.TabManagementFragmentDirections
import org.robolectric.RobolectricTestRunner
import mozilla.components.browser.state.state.createTab as createStateTab

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class NavigationInteractorTest {
    private lateinit var store: BrowserStore
    private val testTab: TabSessionState = createStateTab(url = "https://mozilla.org")
    private val navController: NavController = mockk(relaxed = true)
    private val accountManager: FxaAccountManager = mockk(relaxed = true)

    val coroutinesTestRule: MainCoroutineRule = MainCoroutineRule()
    val gleanTestRule = FenixGleanTestRule(testContext)

    @get:Rule
    val chain: RuleChain = RuleChain.outerRule(gleanTestRule).around(coroutinesTestRule)

    @Before
    fun setup() {
        store = BrowserStore(initialState = BrowserState(tabs = listOf(testTab)))
    }

    @Test
    fun `WHEN the tab manager is dismissed THEN the metric is reported`() {
        assertNull(TabsTray.closed.testGetValue())

        createInteractor().onTabManagerDismissed()

        assertNotNull(TabsTray.closed.testGetValue())
    }

    @Test
    fun `onAccountSettingsClicked calls navigation on DefaultNavigationInteractor`() {
        every { accountManager.authenticatedAccount() }.answers { mockk(relaxed = true) }

        createInteractor().onAccountSettingsClicked()

        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalAccountSettingsFragment()) }
    }

    @Test
    fun `onAccountSettingsClicked when not logged in calls navigation to turn on sync`() {
        every { accountManager.authenticatedAccount() }.answers { null }

        createInteractor().onAccountSettingsClicked()

        verify(exactly = 1) {
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalTurnOnSync(
                    entrypoint = FenixFxAEntryPoint.NavigationInteraction,
                ),
            )
        }
    }

    @Test
    fun `onTabSettingsClicked calls navigation on DefaultNavigationInteractor`() {
        createInteractor().onTabSettingsClicked()
        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalTabSettingsFragment()) }
    }

    @Test
    fun `onOpenRecentlyClosedClicked calls navigation on DefaultNavigationInteractor`() {
        assertNull(Events.recentlyClosedTabsOpened.testGetValue())

        createInteractor().onOpenRecentlyClosedClicked()

        verify(exactly = 1) { navController.navigate(TabManagementFragmentDirections.actionGlobalRecentlyClosed()) }
        assertNotNull(Events.recentlyClosedTabsOpened.testGetValue())
    }

    @Test
    fun `onCloseAllTabsClicked calls navigation on DefaultNavigationInteractor`() {
        var dismissTabManagerAndNavigateHomeInvoked = false
        createInteractor(
            dismissTabManagerAndNavigateHome = {
                dismissTabManagerAndNavigateHomeInvoked = true
            },
        ).onCloseAllTabsClicked(false)

        assertTrue(dismissTabManagerAndNavigateHomeInvoked)
    }

    @Test
    fun `GIVEN active private download WHEN onCloseAllTabsClicked is called for private tabs THEN showCancelledDownloadWarning is called`() {
        var showCancelledDownloadWarningInvoked = false
        val mockedStore: BrowserStore = mockk()
        val controller = spyk(
            createInteractor(
                browserStore = mockedStore,
                showCancelledDownloadWarning = { _, _, _ ->
                    showCancelledDownloadWarningInvoked = true
                },
            ),
        )
        val tab: TabSessionState = mockk { every { content.private } returns true }
        every { mockedStore.state } returns mockk {
            every { tabs } returns listOf(tab)
        }
        every { mockedStore.state.downloads } returns mapOf(
            "1" to DownloadState(
                "https://mozilla.org/download",
                private = true,
                destinationDirectory = "Download",
                status = DownloadState.Status.DOWNLOADING,
            ),
        )

        controller.onCloseAllTabsClicked(true)

        assertTrue(showCancelledDownloadWarningInvoked)
    }

    private fun createInteractor(
        browserStore: BrowserStore = store,
        dismissTabManagerAndNavigateHome: (String) -> Unit = { _ -> },
        showCancelledDownloadWarning: (Int, String?, String?) -> Unit = { _, _, _ -> },
    ): NavigationInteractor {
        return DefaultNavigationInteractor(
            browserStore = browserStore,
            navController = navController,
            dismissTabManagerAndNavigateHome = dismissTabManagerAndNavigateHome,
            showCancelledDownloadWarning = showCancelledDownloadWarning,
            accountManager = accountManager,
        )
    }
}
