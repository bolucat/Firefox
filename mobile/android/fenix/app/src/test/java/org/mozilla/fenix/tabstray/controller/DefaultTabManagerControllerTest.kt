/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.controller

import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import io.mockk.MockKAnnotations
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.storage.sync.Tab
import mozilla.components.browser.storage.sync.TabEntry
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.accounts.push.CloseTabsUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.Collections
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.DefaultBrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.maxActiveTime
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_NORMAL_TABS
import org.mozilla.fenix.home.HomeScreenViewModel.Companion.ALL_PRIVATE_TABS
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.tabstray.TabsTrayAction
import org.mozilla.fenix.tabstray.TabsTrayState
import org.mozilla.fenix.tabstray.TabsTrayStore
import org.mozilla.fenix.tabstray.ui.TabManagementFragmentDirections
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import java.util.concurrent.TimeUnit

@RunWith(RobolectricTestRunner::class) // for gleanTestRule
class DefaultTabManagerControllerTest {
    @MockK(relaxed = true)
    private lateinit var trayStore: TabsTrayStore

    @MockK(relaxed = true)
    private lateinit var browserStore: BrowserStore

    @MockK(relaxed = true)
    private lateinit var browsingModeManager: BrowsingModeManager

    @MockK(relaxed = true)
    private lateinit var navController: NavController

    @MockK(relaxed = true)
    private lateinit var profiler: Profiler

    @MockK(relaxed = true)
    private lateinit var navigationInteractor: NavigationInteractor

    @MockK(relaxed = true)
    private lateinit var tabsUseCases: TabsUseCases

    @MockK(relaxed = true)
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases

    @MockK(relaxed = true)
    private lateinit var activity: HomeActivity

    private val appStore: AppStore = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)

    private val bookmarksStorage: BookmarksStorage = mockk(relaxed = true)
    private val closeSyncedTabsUseCases: CloseTabsUseCases = mockk(relaxed = true)
    private val collectionStorage: TabCollectionStorage = mockk(relaxed = true)

    private val coroutinesTestRule: MainCoroutineRule = MainCoroutineRule()
    private val testDispatcher = coroutinesTestRule.testDispatcher

    private val mockPrivateTab = mockk<TabSessionState> {
        every { content.private } returns true
    }

    private val mockNormalTab = mockk<TabSessionState> {
        every { content.private } returns false
    }

    val gleanTestRule = FenixGleanTestRule(testContext)

    @get:Rule
    val chain: RuleChain = RuleChain.outerRule(gleanTestRule).around(coroutinesTestRule)

    @Before
    fun setup() {
        MockKAnnotations.init(this)
    }

    @Test
    fun `GIVEN private mode WHEN the fab is clicked THEN a profile marker is added for the operations executed`() {
        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        createController().handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())

        verifyOrder {
            profiler.getProfilerTime()
            navController.popBackStack()
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
            navigationInteractor.onTabManagerDismissed()
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN private mode and homepage as a new tab is enabled WHEN the fab is clicked THEN a new private homepage tab is displayed`() {
        every { settings.enableHomepageAsNewTab } returns true

        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        createController().handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())

        verifyOrder {
            profiler.getProfilerTime()
            fenixBrowserUseCases.addNewHomepageTab(
                private = true,
            )
            navigationInteractor.onTabManagerDismissed()
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN normal mode WHEN the fab is clicked THEN a profile marker is added for the operations executed`() {
        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        createController().handleNormalTabsFabClick()

        verifyOrder {
            profiler.getProfilerTime()
            navController.popBackStack()
            navController.navigate(
                TabManagementFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
            navigationInteractor.onTabManagerDismissed()
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN normal mode and homepage as a new tab is enabled WHEN the fab is clicked THEN a new homepage tab is displayed`() {
        every { settings.enableHomepageAsNewTab } returns true

        profiler = spyk(profiler) {
            every { getProfilerTime() } returns Double.MAX_VALUE
        }

        createController().handleNormalTabsFabClick()

        verifyOrder {
            profiler.getProfilerTime()
            fenixBrowserUseCases.addNewHomepageTab(
                private = false,
            )
            navigationInteractor.onTabManagerDismissed()
            profiler.addMarker(
                "DefaultTabManagerController.onNewTabTapped",
                Double.MAX_VALUE,
            )
        }
    }

    @Test
    fun `GIVEN private mode WHEN the fab is clicked THEN Event#NewPrivateTabTapped is added to telemetry`() {
        assertNull(TabsTray.newPrivateTabTapped.testGetValue())

        createController().handlePrivateTabsFabClick()

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN normal mode WHEN the fab is clicked THEN Event#NewTabTapped is added to telemetry`() {
        assertNull(TabsTray.newTabTapped.testGetValue())

        createController().handleNormalTabsFabClick()

        assertNotNull(TabsTray.newTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN the user is on the synced tabs page WHEN the fab is clicked THEN fire off a sync action`() {
        every { trayStore.state.syncing } returns false

        createController().handleSyncedTabsFabClick()

        verify { trayStore.dispatch(TabsTrayAction.SyncNow) }
    }

    @Test
    fun `GIVEN the user is on the synced tabs page and there is already an active sync WHEN the fab is clicked THEN no action should be taken`() {
        every { trayStore.state.syncing } returns true

        createController().handleSyncedTabsFabClick()

        verify(exactly = 0) { trayStore.dispatch(TabsTrayAction.SyncNow) }
    }

    @Test
    fun `WHEN handleTabDeletion is called THEN Event#ClosedExistingTab is added to telemetry`() {
        val tab: TabSessionState = mockk {
            every { content.private } returns true
            every { id } returns "testTabId"
        }

        assertNull(TabsTray.closedExistingTab.testGetValue())

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns "otherTabId"
        }

        createController().handleTabDeletion("testTabId", "unknown")
        assertNotNull(TabsTray.closedExistingTab.testGetValue())
    }

    @Test
    fun `GIVEN active private download WHEN handleTabDeletion is called for the last private tab THEN showCancelledDownloadWarning is called`() {
        var showCancelledDownloadWarningInvoked = false

        val tab: TabSessionState = mockk {
            every { content.private } returns true
            every { id } returns "testTabId"
        }

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns "testTabId"
            every { downloads } returns mapOf(
                "1" to DownloadState(
                    "https://mozilla.org/download",
                    private = true,
                    destinationDirectory = "Download",
                    status = DownloadState.Status.DOWNLOADING,
                ),
            )
        }

        val controller = createController(
            showCancelledDownloadWarning = { _, _, _ ->
                showCancelledDownloadWarningInvoked = true
            },
        )

        controller.handleTabDeletion("testTabId", "unknown")

        assertTrue(showCancelledDownloadWarningInvoked)
    }

    @Test
    fun `WHEN handleTabTrayPageClicked is called THEN it emits an action for the Page of that tab position`() {
        val page = Page.SyncedTabs
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        createController().handleTabPageClicked(page)
        verify { trayStore.dispatch(TabsTrayAction.PageSelected(page)) }
    }

    @Test
    fun `GIVEN already on browserFragment WHEN handleNavigateToBrowser is called THEN the manager is closed`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment

        createController().handleNavigateToBrowser()

        verify(exactly = 0) { navController.popBackStack() }
        verify(exactly = 0) { navController.popBackStack(any<Int>(), any()) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called THEN the manager is closed and popBackStack is executed`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment + 1
        every { navController.popBackStack(R.id.browserFragment, false) } returns true

        createController().handleNavigateToBrowser()

        verify { navController.popBackStack(R.id.browserFragment, false) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called and popBackStack fails THEN it navigates to browserFragment`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment + 1
        every { navController.popBackStack(R.id.browserFragment, false) } returns false

        createController().handleNavigateToBrowser()

        verify { navController.popBackStack(R.id.browserFragment, false) }
        verify { navController.popBackStack() }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN not already on browserFragment WHEN handleNavigateToBrowser is called and popBackStack succeeds THEN the method finishes`() {
        every { navController.popBackStack(R.id.browserFragment, false) } returns true

        createController().handleNavigateToBrowser()

        verify(exactly = 1) { navController.popBackStack(R.id.browserFragment, false) }
        verify(exactly = 0) { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN the homepage is currently shown WHEN navigate to home is called THEN the manager is closed`() {
        every { navController.currentDestination?.id } returns R.id.homeFragment

        createController().handleNavigateToHome()

        verify(exactly = 0) { navController.popBackStack() }
        verify(exactly = 0) { navController.popBackStack(any<Int>(), any()) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN the browser is currently shown WHEN navigate to home is called THEN the manager is closed and popBackStack is executed`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns true

        createController().handleNavigateToHome()

        verify { navController.popBackStack(R.id.homeFragment, false) }
        verify(exactly = 0) { navController.navigate(any<Int>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>()) }
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
    }

    @Test
    fun `GIVEN the browser is currently shown WHEN navigate to home is called and pop back stack fails THEN it navigates to home`() {
        every { navController.currentDestination?.id } returns R.id.browserFragment
        every { navController.popBackStack(R.id.homeFragment, false) } returns false

        createController().handleNavigateToHome()

        verify { navController.popBackStack(R.id.homeFragment, false) }
        verify { navController.popBackStack() }
        verify { navController.navigate(TabManagementFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `WHEN navigate to home is called and popBackStack succeeds THEN the method finishes`() {
        every { navController.popBackStack(R.id.homeFragment, false) } returns true

        createController().handleNavigateToHome()

        verify(exactly = 1) { navController.popBackStack(R.id.homeFragment, false) }
        verify(exactly = 0) { navController.navigate(TabManagementFragmentDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN more tabs opened WHEN handleTabDeletion is called THEN that tab is removed and an undo snackbar is shown`() {
        val tab: TabSessionState = mockk {
            every { content } returns mockk()
            every { content.private } returns true
            every { id } returns "22"
        }
        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab, mockNormalTab)
            every { selectedTabId } returns "0"
        }

        var showUndoSnackbarForTabInvoked = false
        createController(
            showUndoSnackbarForTab = {
                assertTrue(it)
                showUndoSnackbarForTabInvoked = true
            },
        ).handleTabDeletion("22")

        verify { tabsUseCases.removeTab("22") }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN only one tab opened WHEN handleTabDeletion is called THEN that it navigates to home where the tab will be removed`() {
        val testTabId = "33"
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }))
        val tab: TabSessionState = mockk {
            every { content } returns mockk()
            every { content.private } returns true
            every { id } returns testTabId
        }

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(tab)
            every { selectedTabId } returns testTabId
            every { downloads } returns emptyMap()
        }

        controller.handleTabDeletion(testTabId)

        verify { controller.dismissTabManagerAndNavigateHome(testTabId) }
        verify(exactly = 0) { tabsUseCases.removeTab(any()) }
        assertFalse(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all private tabs THEN that it navigates to home where that tabs will be removed and shows undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(
                showUndoSnackbarForTab = {
                    assertTrue(it)
                    showUndoSnackbarForTabInvoked = true
                },
            ),
        )

        val privateTab = createTab(url = "url", private = true)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(mockPrivateTab, mockPrivateTab)
        }

        controller.deleteMultipleTabs(listOf(privateTab, mockk()))

        verify { controller.dismissTabManagerAndNavigateHome(ALL_PRIVATE_TABS) }
        assertTrue(showUndoSnackbarForTabInvoked)
        verify(exactly = 0) { tabsUseCases.removeTabs(any()) }
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close all normal tabs THEN that it navigates to home where that tabs will be removed and shows undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false

        val normalTab = createTab(url = "url", private = false)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(mockNormalTab, mockNormalTab)
        }

        val controller = spyk(
            createController(
                showUndoSnackbarForTab = {
                    assertFalse(it)
                    showUndoSnackbarForTabInvoked = true
                },
            ),
        )

        controller.deleteMultipleTabs(listOf(normalTab, normalTab))

        verify { controller.dismissTabManagerAndNavigateHome(ALL_NORMAL_TABS) }
        verify(exactly = 0) { tabsUseCases.removeTabs(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close some private tabs THEN that it uses tabsUseCases#removeTabs and shows an undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(
            createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }),
        )
        val privateTab = createTab(id = "42", url = "url", private = true)

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(mockNormalTab, mockNormalTab)
        }

        controller.deleteMultipleTabs(listOf(privateTab))

        verify { tabsUseCases.removeTabs(listOf("42")) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `WHEN handleMultipleTabsDeletion is called to close some normal tabs THEN that it uses tabsUseCases#removeTabs and shows an undo snackbar`() {
        var showUndoSnackbarForTabInvoked = false
        val controller = spyk(createController(showUndoSnackbarForTab = { showUndoSnackbarForTabInvoked = true }))
        val privateTab = createTab(id = "24", url = "url", private = false)

        every { browserStore.state } returns mockk {
            every { findTab("24") } returns privateTab
            every { selectedTabId } returns "24"
            every { tabs } returns listOf(mockNormalTab, mockNormalTab)
        }

        controller.deleteMultipleTabs(listOf(privateTab))

        verify { tabsUseCases.removeTabs(listOf("24")) }
        verify(exactly = 0) { controller.dismissTabManagerAndNavigateHome(any()) }
        assertTrue(showUndoSnackbarForTabInvoked)
    }

    @Test
    fun `GIVEN one tab is selected WHEN the delete selected tabs button is clicked THEN report the telemetry and delete the tabs`() {
        val controller = spyk(createController())

        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "url"))
        every { controller.deleteMultipleTabs(any()) } just runs

        controller.handleDeleteSelectedTabsClicked()

        assertNotNull(TabsTray.closeSelectedTabs.testGetValue())
        val snapshot = TabsTray.closeSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))

        verify { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    @Test
    fun `GIVEN private mode selected WHEN sendNewTabEvent is called THEN NewPrivateTabTapped is tracked in telemetry`() {
        createController().sendNewTabEvent(true)

        assertNotNull(TabsTray.newPrivateTabTapped.testGetValue())
    }

    @Test
    fun `GIVEN normal mode selected WHEN sendNewTabEvent is called THEN NewTabTapped is tracked in telemetry`() {
        assertNull(TabsTray.newTabTapped.testGetValue())

        createController().sendNewTabEvent(false)

        assertNotNull(TabsTray.newTabTapped.testGetValue())
    }

    @Test
    fun `WHEN dismissTabManagerAndNavigateHome is called with a specific tab id THEN navigates home is opened to delete that tab`() {
        var navigateToHomeAndDeleteSessionInvoked = false
        createController(
            navigateToHomeAndDeleteSession = {
                assertEquals("randomId", it)
                navigateToHomeAndDeleteSessionInvoked = true
            },
        ).dismissTabManagerAndNavigateHome("randomId")

        assertTrue(navigateToHomeAndDeleteSessionInvoked)
    }

    @Test
    fun `WHEN a synced tab is clicked THEN the metrics are reported and the tab is opened`() {
        val tab = mockk<Tab>()
        val entry = mockk<TabEntry>()
        assertNull(Events.syncedTabOpened.testGetValue())

        every { tab.active() }.answers { entry }
        every { entry.url }.answers { "https://mozilla.org" }

        createController().handleSyncedTabClicked(tab)

        assertNotNull(Events.syncedTabOpened.testGetValue())

        verify {
            activity.openToBrowserAndLoad(
                searchTermOrURL = "https://mozilla.org",
                newTab = true,
                from = BrowserDirection.FromTabManager,
            )
        }
    }

    @Test
    fun `WHEN a synced tab is closed THEN a command to close the tab is queued AND an undo snackbar is shown`() {
        var showUndoSnackbarForSyncedTabInvoked = false
        val controller = createController(
            showUndoSnackbarForSyncedTab = {
                showUndoSnackbarForSyncedTabInvoked = true
            },
        )

        val tab = Tab(
            history = listOf(TabEntry(title = "Get Firefox", url = "https://getfirefox.com", iconUrl = null)),
            active = 0,
            lastUsed = 0,
            inactive = false,
        )
        controller.handleSyncedTabClosed("1234", tab)

        coVerify(exactly = 1) { closeSyncedTabsUseCases.close("1234", any()) }
        assertTrue(showUndoSnackbarForSyncedTabInvoked)
    }

    @Test
    fun `GIVEN no tabs selected and the user is not in multi select mode WHEN the user long taps a tab THEN that tab will become selected`() {
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tab1 = TabSessionState(
            id = "1",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val tab2 = TabSessionState(
            id = "2",
            content = ContentState(
                url = "www.google.com",
            ),
        )
        trayStore.dispatch(TabsTrayAction.ExitSelectMode)
        trayStore.waitUntilIdle()

        controller.handleTabSelected(tab1, "Tab Manager")
        verify(exactly = 1) { controller.handleTabSelected(tab1, "Tab Manager") }

        controller.handleTabSelected(tab2, "Tab Manager")
        verify(exactly = 1) { controller.handleTabSelected(tab2, "Tab Manager") }
    }

    @Test
    fun `GIVEN the user is in multi select mode and a tab is selected WHEN the user taps the selected tab THEN the tab will become unselected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        val tab1 = TabSessionState(
            id = "1",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val tab2 = TabSessionState(
            id = "2",
            content = ContentState(
                url = "www.google.com",
            ),
        )
        val controller = createController()
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab1))
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab2))
        trayStore.waitUntilIdle()

        controller.handleTabSelected(tab1, "Tab Manager")
        middleware.assertLastAction(TabsTrayAction.RemoveSelectTab::class) {
            assertEquals(tab1, it.tab)
        }

        controller.handleTabSelected(tab2, "Tab Manager")
        middleware.assertLastAction(TabsTrayAction.RemoveSelectTab::class) {
            assertEquals(tab2, it.tab)
        }
    }

    @Test
    fun `GIVEN at least a tab is selected and the user is in multi select mode WHEN the user taps a tab THEN that tab will become selected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.waitUntilIdle()
        val controller = createController()
        val tab1 = TabSessionState(
            id = "1",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val tab2 = TabSessionState(
            id = "2",
            content = ContentState(
                url = "www.google.com",
            ),
        )

        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(tab1))
        trayStore.waitUntilIdle()

        controller.handleTabSelected(tab2, "Tab Manager")

        middleware.assertLastAction(TabsTrayAction.AddSelectTab::class) {
            assertEquals(tab2, it.tab)
        }
    }

    @Test
    fun `GIVEN at least a tab is selected and the user is in multi select mode WHEN the user taps an inactive tab THEN that tab will not be selected`() {
        val middleware = CaptureActionsMiddleware<TabsTrayState, TabsTrayAction>()
        trayStore = TabsTrayStore(middlewares = listOf(middleware))
        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.waitUntilIdle()
        val controller = spyk(createController())
        val normalTab = TabSessionState(
            id = "1",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val inactiveTab = TabSessionState(
            id = "2",
            content = ContentState(
                url = "www.google.com",
            ),
        )

        trayStore.dispatch(TabsTrayAction.EnterSelectMode)
        trayStore.dispatch(TabsTrayAction.AddSelectTab(normalTab))
        trayStore.waitUntilIdle()

        controller.handleTabSelected(inactiveTab, INACTIVE_TABS_FEATURE_NAME)

        middleware.assertLastAction(TabsTrayAction.AddSelectTab::class) {
            assertEquals(normalTab, it.tab)
        }
    }

    @Test
    fun `GIVEN the user selects only the current tab WHEN the user forces tab to be inactive THEN tab does not become inactive`() {
        val currentTab = TabSessionState(content = mockk(), id = "currentTab", createdAt = 11)
        val secondTab = TabSessionState(content = mockk(), id = "secondTab", createdAt = 22)
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab, secondTab),
                selectedTabId = currentTab.id,
            ),
        )

        every { trayStore.state.mode.selectedTabs } returns setOf(currentTab)

        createController().handleForceSelectedTabsAsInactiveClicked(numDays = 5)

        browserStore.waitUntilIdle()

        val updatedCurrentTab = browserStore.state.tabs.first { it.id == currentTab.id }
        assertEquals(updatedCurrentTab, currentTab)
        val updatedSecondTab = browserStore.state.tabs.first { it.id == secondTab.id }
        assertEquals(updatedSecondTab, secondTab)
    }

    @Test
    fun `GIVEN the user selects multiple tabs including the current tab WHEN the user forces them all to be inactive THEN all but current tab become inactive`() {
        val currentTab = TabSessionState(content = mockk(), id = "currentTab", createdAt = 11)
        val secondTab = TabSessionState(content = mockk(), id = "secondTab", createdAt = 22)
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab, secondTab),
                selectedTabId = currentTab.id,
            ),
        )

        every { trayStore.state.mode.selectedTabs } returns setOf(currentTab, secondTab)

        createController().handleForceSelectedTabsAsInactiveClicked(numDays = 5)

        browserStore.waitUntilIdle()

        val updatedCurrentTab = browserStore.state.tabs.first { it.id == currentTab.id }
        assertEquals(updatedCurrentTab, currentTab)
        val updatedSecondTab = browserStore.state.tabs.first { it.id == secondTab.id }
        assertNotEquals(updatedSecondTab, secondTab)
        val expectedTime = System.currentTimeMillis() - TimeUnit.DAYS.toMillis(5)
        // Account for System.currentTimeMillis() giving different values in test vs the system under test
        // and also for the waitUntilIdle to block for even hundreds of milliseconds.
        assertTrue(updatedSecondTab.lastAccess in (expectedTime - 5000)..expectedTime)
        assertTrue(updatedSecondTab.createdAt in (expectedTime - 5000)..expectedTime)
    }

    @Test
    fun `GIVEN no value is provided for inactive days WHEN forcing tabs as inactive THEN set their last active time 15 days ago and exit multi selection`() {
        val controller = spyk(createController())
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"))
        every { browserStore.state.selectedTabId } returns "test"

        controller.handleForceSelectedTabsAsInactiveClicked()

        verify { controller.handleForceSelectedTabsAsInactiveClicked(numDays = 15L) }

        verify { trayStore.dispatch(TabsTrayAction.ExitSelectMode) }
    }

    fun `WHEN the inactive tabs section is expanded THEN the expanded telemetry event should be reported`() {
        val controller = createController()

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())

        controller.handleInactiveTabsHeaderClicked(expanded = true)

        assertNotNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())
    }

    @Test
    fun `WHEN the inactive tabs section is collapsed THEN the collapsed telemetry event should be reported`() {
        val controller = createController()

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNull(TabsTray.inactiveTabsCollapsed.testGetValue())

        controller.handleInactiveTabsHeaderClicked(expanded = false)

        assertNull(TabsTray.inactiveTabsExpanded.testGetValue())
        assertNotNull(TabsTray.inactiveTabsCollapsed.testGetValue())
    }

    @Test
    fun `WHEN the inactive tabs auto-close feature prompt is dismissed THEN update settings and report the telemetry event`() {
        val controller = createController()

        assertNull(TabsTray.autoCloseDimissed.testGetValue())

        controller.handleInactiveTabsAutoCloseDialogDismiss()

        assertNotNull(TabsTray.autoCloseDimissed.testGetValue())
        verify { settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true }
    }

    @Test
    fun `WHEN the inactive tabs auto-close feature prompt is accepted THEN update settings and report the telemetry event`() {
        val controller = createController()

        assertNull(TabsTray.autoCloseTurnOnClicked.testGetValue())

        controller.handleEnableInactiveTabsAutoCloseClicked()

        assertNotNull(TabsTray.autoCloseTurnOnClicked.testGetValue())

        verify { settings.closeTabsAfterOneMonth = true }
        verify { settings.closeTabsAfterOneWeek = false }
        verify { settings.closeTabsAfterOneDay = false }
        verify { settings.manuallyCloseTabs = false }
        verify { settings.hasInactiveTabsAutoCloseDialogBeenDismissed = true }
    }

    @Test
    fun `WHEN an inactive tab is selected THEN report the telemetry event and open the tab`() {
        val controller = spyk(createController())
        val tab = TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )

        every { controller.handleTabSelected(any(), any()) } just runs

        assertNull(TabsTray.openInactiveTab.testGetValue())

        controller.handleInactiveTabClicked(tab)

        assertNotNull(TabsTray.openInactiveTab.testGetValue())

        verify { controller.handleTabSelected(tab, INACTIVE_TABS_FEATURE_NAME) }
    }

    @Test
    fun `WHEN an inactive tab is closed THEN report the telemetry event and delete the tab`() {
        val controller = spyk(createController())
        val tab = TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )

        every { controller.handleTabDeletion(any(), any()) } just runs

        assertNull(TabsTray.closeInactiveTab.testGetValue())

        controller.handleCloseInactiveTabClicked(tab)

        assertNotNull(TabsTray.closeInactiveTab.testGetValue())

        verify { controller.handleTabDeletion(tab.id, INACTIVE_TABS_FEATURE_NAME) }
    }

    @Test
    fun `WHEN all inactive tabs are closed THEN perform the deletion and report the telemetry event and show a Snackbar`() {
        var showSnackbarInvoked = false
        val controller = createController(
            showUndoSnackbarForInactiveTab = {
                showSnackbarInvoked = true
            },
        )
        val inactiveTab: TabSessionState = mockk {
            every { lastAccess } returns maxActiveTime
            every { createdAt } returns 0
            every { id } returns "24"
            every { content } returns mockk {
                every { private } returns false
            }
        }

        every { browserStore.state } returns mockk {
            every { tabs } returns listOf(inactiveTab)
            every { selectedTabId } returns "24"
        }

        assertNull(TabsTray.closeAllInactiveTabs.testGetValue())

        controller.handleDeleteAllInactiveTabsClicked()

        verify { tabsUseCases.removeTabs(listOf("24")) }
        assertNotNull(TabsTray.closeAllInactiveTabs.testGetValue())
        assertTrue(showSnackbarInvoked)
    }

    @Test
    fun `WHEN a tab is selected THEN report the metric, update the state, and open the browser`() {
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tab = TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val source = INACTIVE_TABS_FEATURE_NAME

        every { controller.handleNavigateToBrowser() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, source)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(source, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToBrowser() }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a homepage tab is selected THEN report the metric, update the state, and show the homepage`() {
        every { settings.enableHomepageAsNewTab } returns true

        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tab = TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "about:home",
            ),
        )
        val source = "Tab Manager"

        every { controller.handleNavigateToHome() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, source)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(source, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToHome() }
    }

    @Test
    fun `WHEN a tab is selected without a source THEN report the metric with an unknown source, update the state, and open the browser`() {
        trayStore = TabsTrayStore()
        val controller = spyk(createController())
        val tab = TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        )
        val sourceText = "unknown"

        every { controller.handleNavigateToBrowser() } just runs

        assertNull(TabsTray.openedExistingTab.testGetValue())

        controller.handleTabSelected(tab, null)

        assertNotNull(TabsTray.openedExistingTab.testGetValue())
        val snapshot = TabsTray.openedExistingTab.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals(sourceText, snapshot.single().extra?.getValue("source"))

        verify { tabsUseCases.selectTab(tab.id) }
        verify { controller.handleNavigateToBrowser() }
    }

    @Test
    fun `GIVEN a private tab is open and selected with a normal tab also open WHEN the private tab is closed and private home page shown and normal tab is selected from the tab manager THEN normal tab is displayed  `() {
        val normalTab = TabSessionState(
            content = ContentState(url = "https://simulate.com", private = false),
            id = "normalTab",
        )
        val privateTab = TabSessionState(
            content = ContentState(url = "https://mozilla.com", private = true),
            id = "privateTab",
        )
        trayStore = TabsTrayStore()
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(normalTab, privateTab),
            ),
        )
        var appStateModeUpdate: BrowsingMode? = null
        browsingModeManager = DefaultBrowsingModeManager(
            intent = null,
            settings = settings,
            onModeChange = { updatedMode ->
                appStateModeUpdate = updatedMode
            },
        )

        val controller = createController()

        browserStore.dispatch(TabListAction.SelectTabAction(privateTab.id)).joinBlocking()
        controller.handleTabSelected(privateTab, null)

        assertEquals(privateTab.id, browserStore.state.selectedTabId)
        assertEquals(true, browsingModeManager.mode.isPrivate)
        assertEquals(BrowsingMode.Private, appStateModeUpdate)

        controller.handleTabDeletion("privateTab")
        browserStore.dispatch(TabListAction.SelectTabAction(normalTab.id)).joinBlocking()
        controller.handleTabSelected(normalTab, null)

        assertEquals(normalTab.id, browserStore.state.selectedTabId)
        assertEquals(false, browsingModeManager.mode.isPrivate)
        assertEquals(BrowsingMode.Normal, appStateModeUpdate)
    }

    @Test
    fun `GIVEN a normal tab is selected WHEN the last private tab is deleted THEN that private tab is removed and an undo snackbar is shown and original normal tab is still displayed`() {
        val currentTab = TabSessionState(content = ContentState(url = "https://simulate.com", private = false), id = "currentTab")
        val privateTab = TabSessionState(content = ContentState(url = "https://mozilla.com", private = true), id = "privateTab")
        var showUndoSnackbarForTabInvoked = false
        var navigateToHomeAndDeleteSessionInvoked = false
        trayStore = TabsTrayStore()
        browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(currentTab, privateTab),
                selectedTabId = currentTab.id,
            ),
        )

        val controller = createController(
            showUndoSnackbarForTab = {
                showUndoSnackbarForTabInvoked = true
            },
            navigateToHomeAndDeleteSession = {
                navigateToHomeAndDeleteSessionInvoked = true
            },
        )

        controller.handleTabSelected(currentTab, "source")
        controller.handleTabDeletion("privateTab")

        assertTrue(showUndoSnackbarForTabInvoked)
        assertFalse(navigateToHomeAndDeleteSessionInvoked)
    }

    @Test
    fun `GIVEN no tabs are currently selected WHEN a normal tab is long clicked THEN the tab is selected and the metric is reported`() {
        val normalTab = TabSessionState(
            content = ContentState(url = "https://simulate.com", private = false),
            id = "normalTab",
        )
        every { trayStore.state.mode.selectedTabs } returns emptySet()

        assertNull(Collections.longPress.testGetValue())

        createController().handleTabLongClick(normalTab)

        assertNotNull(Collections.longPress.testGetValue())
        verify { trayStore.dispatch(TabsTrayAction.AddSelectTab(normalTab)) }
    }

    @Test
    fun `GIVEN at least one tab is selected WHEN a normal tab is long clicked THEN the long click is ignored`() {
        val normalTabClicked = TabSessionState(
            content = ContentState(url = "https://simulate.com", private = false),
            id = "normalTab",
        )
        val alreadySelectedTab = TabSessionState(
            content = ContentState(url = "https://simulate.com", private = false),
            id = "selectedTab",
        )
        every { trayStore.state.mode.selectedTabs } returns setOf(alreadySelectedTab)

        createController().handleTabLongClick(normalTabClicked)

        assertNull(Collections.longPress.testGetValue())
        verify(exactly = 0) { trayStore.dispatch(any()) }
    }

    @Test
    fun `WHEN a private tab is long clicked THEN the long click is ignored`() {
        val privateTab = TabSessionState(
            content = ContentState(url = "https://simulate.com", private = true),
            id = "privateTab",
        )

        createController().handleTabLongClick(privateTab)

        assertNull(Collections.longPress.testGetValue())
        verify(exactly = 0) { trayStore.dispatch(any()) }
    }

    @Test
    fun `GIVEN one tab is selected WHEN the share button is clicked THEN report the telemetry and navigate away`() {
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"))

        createController().handleShareSelectedTabsClicked()

        verify(exactly = 1) { navController.navigate(any<NavDirections>()) }

        assertNotNull(TabsTray.shareSelectedTabs.testGetValue())
        val snapshot = TabsTray.shareSelectedTabs.testGetValue()!!
        assertEquals(1, snapshot.size)
        assertEquals("1", snapshot.single().extra?.getValue("tab_count"))
    }

    @Test
    fun `GIVEN one tab is selected WHEN the add selected tabs to collection button is clicked THEN report the telemetry and show the collections dialog`() {
        val controller = spyk(createController())
        every { controller.showCollectionsDialog(any()) } just runs

        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"))
        every { controller.showCollectionsDialog(any()) } answers { }

        assertNull(TabsTray.saveToCollection.testGetValue())

        controller.handleAddSelectedTabsToCollectionClicked()

        assertNotNull(TabsTray.saveToCollection.testGetValue())
    }

    @Test
    fun `GIVEN one tab selected and no bookmarks previously saved WHEN saving selected tabs to bookmarks THEN save bookmark in root, report telemetry, show snackbar`() = runTestOnMain {
        var showBookmarkSnackbarInvoked = false

        coEvery { bookmarksStorage.getRecentBookmarks(1) } returns listOf()
        coEvery { bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id) } returns makeBookmarkFolder(guid = BookmarkRoot.Mobile.id)
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"))

        createController(
            showBookmarkSnackbar = { _, _ ->
                showBookmarkSnackbarInvoked = true
            },
        ).handleBookmarkSelectedTabsClicked()

        verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(1)) }
        coVerify(exactly = 1) { bookmarksStorage.addItem(eq(BookmarkRoot.Mobile.id), any(), any(), any()) }
        assertTrue(showBookmarkSnackbarInvoked)
    }

    @Test
    fun `GIVEN one tab selected and a previously saved bookmark WHEN saving selected tabs to bookmarks THEN save bookmark in last saved folder, report telemetry, show snackbar`() = runTestOnMain {
        var showBookmarkSnackbarInvoked = false

        val parentGuid = "parentGuid"
        val previousBookmark = makeBookmarkItem(parentGuid = parentGuid)
        coEvery { bookmarksStorage.getRecentBookmarks(eq(1), any(), any()) } returns listOf(previousBookmark)
        coEvery { bookmarksStorage.getBookmark(parentGuid) } returns makeBookmarkFolder(guid = parentGuid)
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"))

        createController(
            showBookmarkSnackbar = { _, _ ->
                showBookmarkSnackbarInvoked = true
            },
        ).handleBookmarkSelectedTabsClicked()

        verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(1)) }
        coVerify(exactly = 1) { bookmarksStorage.addItem(eq(parentGuid), any(), any(), any()) }
        assertTrue(showBookmarkSnackbarInvoked)
    }

    @Test
    fun `GIVEN multiple tabs selected and no bookmarks previously saved WHEN saving selected tabs to bookmarks THEN save bookmarks in root, report telemetry, show a snackbar`() = runTestOnMain {
        var showBookmarkSnackbarInvoked = false

        coEvery { bookmarksStorage.getRecentBookmarks(1) } returns listOf()
        coEvery { bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id) } returns makeBookmarkFolder(guid = BookmarkRoot.Mobile.id)
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"), createTab(url = "https://mozilla2.org"))

        createController(
            showBookmarkSnackbar = { _, _ ->
                showBookmarkSnackbarInvoked = true
            },
        ).handleBookmarkSelectedTabsClicked()

        verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(2)) }
        coVerify(exactly = 2) { bookmarksStorage.addItem(eq(BookmarkRoot.Mobile.id), any(), any(), any()) }
        assertTrue(showBookmarkSnackbarInvoked)
    }

    @Test
    fun `GIVEN multiple tabs selected and a previously saved bookmark WHEN saving selected tabs to bookmarks THEN save bookmarks in same folder as recent bookmark, report telemetry, show a snackbar`() = runTestOnMain {
        var showBookmarkSnackbarInvoked = false

        val parentGuid = "parentGuid"
        val previousBookmark = makeBookmarkItem(parentGuid = parentGuid)
        coEvery { bookmarksStorage.getRecentBookmarks(eq(1), any(), any()) } returns listOf(previousBookmark)
        coEvery { bookmarksStorage.getBookmark(parentGuid) } returns makeBookmarkFolder(guid = parentGuid)
        every { trayStore.state.mode.selectedTabs } returns setOf(createTab(url = "https://mozilla.org"), createTab(url = "https://mozilla2.org"))

        createController(
            showBookmarkSnackbar = { _, _ ->
                showBookmarkSnackbarInvoked = true
            },
        ).handleBookmarkSelectedTabsClicked()

        verify { trayStore.dispatch(TabsTrayAction.BookmarkSelectedTabs(2)) }
        coVerify(exactly = 2) { bookmarksStorage.addItem(eq(parentGuid), any(), any(), any()) }
        assertTrue(showBookmarkSnackbarInvoked)
    }

    @Test
    fun `GIVEN active page is not normal tabs WHEN the normal tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.PrivateTabs

        assertNull(TabsTray.normalModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.NormalTabs)

        assertNotNull(TabsTray.normalModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is normal tabs WHEN normal tabs page button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.normalModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.NormalTabs)

        assertNull(TabsTray.normalModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is not private tabs WHEN the private tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.privateModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.PrivateTabs)

        assertNotNull(TabsTray.privateModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is private tabs WHEN the private tabs button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.PrivateTabs

        assertNull(TabsTray.privateModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.PrivateTabs)

        assertNull(TabsTray.privateModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is not synced tabs WHEN the synced tabs page button is clicked THEN report the metric`() {
        every { trayStore.state.selectedPage } returns Page.NormalTabs

        assertNull(TabsTray.syncedModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.SyncedTabs)

        assertNotNull(TabsTray.syncedModeTapped.testGetValue())
    }

    @Test
    fun `GIVEN active page is synced tabs WHEN the synced tabs page button is clicked THEN do not report the metric`() {
        every { trayStore.state.selectedPage } returns Page.SyncedTabs

        assertNull(TabsTray.syncedModeTapped.testGetValue())

        createController().handleTabPageClicked(Page.SyncedTabs)

        assertNull(TabsTray.syncedModeTapped.testGetValue())
    }

    private fun createController(
        navigateToHomeAndDeleteSession: (String) -> Unit = { },
        showUndoSnackbarForTab: (Boolean) -> Unit = { _ -> },
        showUndoSnackbarForInactiveTab: (Int) -> Unit = { _ -> },
        showUndoSnackbarForSyncedTab: (CloseTabsUseCases.UndoableOperation) -> Unit = { _ -> },
        showCancelledDownloadWarning: (Int, String?, String?) -> Unit = { _, _, _ -> },
        showCollectionSnackbar: (Int, Boolean) -> Unit = { _, _ -> },
        showBookmarkSnackbar: (Int, String?) -> Unit = { _, _ -> },
    ): DefaultTabManagerController {
        return DefaultTabManagerController(
            activity = activity,
            appStore = appStore,
            tabsTrayStore = trayStore,
            browserStore = browserStore,
            settings = settings,
            browsingModeManager = browsingModeManager,
            navController = navController,
            navigateToHomeAndDeleteSession = navigateToHomeAndDeleteSession,
            profiler = profiler,
            navigationInteractor = navigationInteractor,
            tabsUseCases = tabsUseCases,
            fenixBrowserUseCases = fenixBrowserUseCases,
            bookmarksStorage = bookmarksStorage,
            closeSyncedTabsUseCases = closeSyncedTabsUseCases,
            collectionStorage = collectionStorage,
            ioDispatcher = testDispatcher,
            showUndoSnackbarForTab = showUndoSnackbarForTab,
            showUndoSnackbarForInactiveTab = showUndoSnackbarForInactiveTab,
            showUndoSnackbarForSyncedTab = showUndoSnackbarForSyncedTab,
            showCancelledDownloadWarning = showCancelledDownloadWarning,
            showCollectionSnackbar = showCollectionSnackbar,
            showBookmarkSnackbar = showBookmarkSnackbar,
        )
    }

    private fun makeBookmarkFolder(guid: String) = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        parentGuid = BookmarkRoot.Mobile.id,
        guid = guid,
        position = 42U,
        title = "title",
        url = "url",
        dateAdded = 0L,
        lastModified = 0L,
        children = null,
    )

    private fun makeBookmarkItem(parentGuid: String) = BookmarkNode(
        type = BookmarkNodeType.ITEM,
        parentGuid = parentGuid,
        guid = "guid",
        position = 42U,
        title = "title",
        url = "url",
        dateAdded = 0L,
        lastModified = 0L,
        children = null,
    )
}
