/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkStatic
import io.mockk.verify
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.android.asCoroutineDispatcher
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.setMain
import mozilla.components.browser.state.action.SearchAction.ApplicationSearchEnginesLoaded
import mozilla.components.browser.state.action.TabListAction.AddTabAction
import mozilla.components.browser.state.action.TabListAction.RemoveTabAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.search.SearchEngine.Type.APPLICATION
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.LoadFromClipboard
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption.PasteFromClipboard
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.LoadFromClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.PasteFromClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent.Source
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import mozilla.components.support.utils.ClipboardHandler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.SimpleBrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEngineSelected
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.OrientationMode.Landscape
import org.mozilla.fenix.components.appstate.OrientationMode.Portrait
import org.mozilla.fenix.components.appstate.search.SearchState
import org.mozilla.fenix.components.appstate.search.SelectedSearchEngine
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.BrowserToolbarEnvironment
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.toolbar.BrowserToolbarMiddleware.HomeToolbarAction
import org.mozilla.fenix.home.toolbar.DisplayActions.FakeClicked
import org.mozilla.fenix.home.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.home.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.home.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.search.fixtures.assertSearchSelectorEquals
import org.mozilla.fenix.search.fixtures.buildExpectedSearchSelector
import org.mozilla.fenix.tabstray.Page
import org.robolectric.Shadows.shadowOf
import mozilla.components.ui.icons.R as iconsR

@RunWith(AndroidJUnit4::class)
class BrowserToolbarMiddlewareTest {
    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    private val appState: AppState = mockk(relaxed = true) {
        every { orientation } returns Portrait
    }
    private val appStore: AppStore = mockk(relaxed = true) {
        every { state } returns appState
    }
    private val browserStore = BrowserStore()
    private val lifecycleOwner = FakeLifecycleOwner(Lifecycle.State.RESUMED)
    private val browsingModeManager = SimpleBrowsingModeManager(Normal)

    @Before
    fun setup() = runTestOnMain {
        every { testContext.settings().shouldUseExpandedToolbar } returns false
        every { testContext.settings().isTabStripEnabled } returns false
        every { testContext.settings().tabManagerEnhancementsEnabled } returns false
    }

    @Test
    fun `WHEN initializing the toolbar THEN add browser end actions`() = runTestOnMain {
        val middleware = BrowserToolbarMiddleware(
            appStore,
            browserStore,
            mockk(),
            mockk(),
        )

        val toolbarStore = buildStore(middleware)

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `WHEN initializing the toolbar AND should use expanded toolbar THEN add browser end actions`() = runTestOnMain {
        every { testContext.settings().shouldUseExpandedToolbar } returns true
        mockkStatic(Context::isTallWindow) {
            every { any<Context>().isTallWindow() } returns true
            val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

            val toolbarStore = buildStore(middleware)

            val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
            assertEquals(0, toolbarBrowserActions.size)
        }
    }

    @Test
    fun `WHEN initializing the navigation bar AND should use expanded toolbar THEN add navigation bar actions`() = runTestOnMain {
        every { testContext.settings().shouldUseExpandedToolbar } returns true
        mockkStatic(Context::isTallWindow) {
            every { any<Context>().isTallWindow() } returns true
            val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

            val toolbarStore = buildStore(middleware)

            val navigationActions = toolbarStore.state.displayState.navigationActions
            assertEquals(5, navigationActions.size)
            val bookmarkButton = navigationActions[0] as ActionButtonRes
            val shareButton = navigationActions[1] as ActionButtonRes
            val newTabButton = navigationActions[2] as ActionButtonRes
            val tabCounterButton = navigationActions[3] as TabCounterAction
            val menuButton = navigationActions[4] as ActionButtonRes
            assertEquals(expectedBookmarkButton, bookmarkButton)
            assertEquals(expectedShareButton, shareButton)
            assertEquals(expectedNewTabButton(Source.NavigationBar), newTabButton)
            assertEqualsToolbarButton(
                expectedToolbarButton(source = Source.NavigationBar),
                tabCounterButton,
            )
            assertEquals(expectedMenuButton(Source.NavigationBar), menuButton)
        }
    }

    @Test
    fun `WHEN initializing the navigation bar AND should use expanded toolbar AND orientation is landscape THEN add no navigation bar actions`() = runTestOnMain {
        every { testContext.settings().shouldUseExpandedToolbar } returns true
        every { appState.orientation } returns Landscape
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

        val toolbarStore = buildStore(middleware)

        val navigationActions = toolbarStore.state.displayState.navigationActions
        assertEquals(0, navigationActions.size)
    }

    @Test
    fun `GIVEN normal browsing mode WHEN initializing the toolbar THEN show the number of normal tabs in the tabs counter button`() = runTestOnMain {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(createTab("test.com", private = false)),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

        val toolbarStore = buildStore(
            middleware = middleware,
            browsingModeManager = browsingModeManager,
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN private browsing mode WHEN initializing the toolbar THEN show the number of private tabs in the tabs counter button`() = runTestOnMain {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    createTab("test.com", private = true),
                    createTab("firefox.com", private = true),
                ),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

        val toolbarStore = buildStore(
            middleware = middleware,
            browsingModeManager = browsingModeManager,
        )

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(2, true), tabCounterButton)
    }

    @Test
    fun `WHEN initializing the toolbar THEN setup showing the website origin`() {
        val expectedConfiguration = PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = null,
            contextualMenuOptions = listOf(PasteFromClipboard, LoadFromClipboard),
            onClick = OriginClicked,
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())

        val toolbarStore = buildStore(middleware)

        val originConfiguration = toolbarStore.state.displayState.pageOrigin
        assertEquals(expectedConfiguration, originConfiguration)
    }

    @Test
    fun `GIVEN an environment was already set WHEN it is cleared THEN reset it to null`() {
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val store = buildStore(middleware)

        assertNotNull(middleware.environment)

        store.dispatch(EnvironmentCleared)

        assertNull(middleware.environment)
    }

    // Testing updated configuration

    @Test
    fun `GIVEN in portrait WHEN changing to landscape THEN show browser end actions`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        val appStore = AppStore(
            initialState = AppState(
                orientation = Portrait,
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(middleware)
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)

        appStore.dispatch(AppAction.OrientationChange(Landscape)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)
    }

    @Test
    fun `GIVEN in landscape WHEN changing to portrait THEN show all browser end actions`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        val appStore = AppStore(
            initialState = AppState(
                orientation = Landscape,
            ),
        )

        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(middleware)
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        val tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        val menuButton = toolbarBrowserActions[1] as ActionButtonRes
        assertEqualsToolbarButton(expectedToolbarButton(), tabCounterButton)
        assertEquals(expectedMenuButton(), menuButton)

        appStore.dispatch(AppAction.OrientationChange(Portrait)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
    }

    @Test
    fun `GIVEN in normal browsing WHEN the number of normal opened tabs is modified THEN update the tab counter`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val browserStore = BrowserStore()
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            browsingModeManager = browsingModeManager,
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0), tabCounterButton)

        val newNormalTab = createTab("test.com", private = false)
        val newPrivateTab = createTab("test.com", private = true)
        browserStore.dispatch(AddTabAction(newNormalTab)).joinBlocking()
        browserStore.dispatch(AddTabAction(newPrivateTab)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1), tabCounterButton)
    }

    @Test
    fun `GIVEN in private browsing WHEN the number of private opened tabs is modified THEN update the tab counter`() = runTestOnMain {
        Dispatchers.setMain(StandardTestDispatcher())
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val initialNormalTab = createTab("test.com", private = false)
        val initialPrivateTab = createTab("test.com", private = true)
        val browserStore = BrowserStore(
            BrowserState(
                tabs = listOf(initialNormalTab, initialPrivateTab),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            browsingModeManager = browsingModeManager,
        )
        testScheduler.advanceUntilIdle()
        var toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        var tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(1, true), tabCounterButton)

        browserStore.dispatch(RemoveTabAction(initialPrivateTab.id)).joinBlocking()
        testScheduler.advanceUntilIdle()

        toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(2, toolbarBrowserActions.size)
        tabCounterButton = toolbarBrowserActions[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
    }

    // Testing actions

    @Test
    fun `WHEN clicking the menu button THEN open the menu`() {
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
        )
        val menuButton = toolbarStore.state.displayState.browserActionsEnd[1] as ActionButtonRes

        toolbarStore.dispatch(menuButton.onClick as BrowserToolbarEvent)

        verify {
            navController.nav(
                R.id.homeFragment,
                BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                    accesspoint = MenuAccessPoint.Browser,
                ),
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking the tab counter button THEN open the tabs tray in normal mode`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabsTrayFragment(page = Page.NormalTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking the tab counter button THEN open the tabs tray in private mode`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction

        toolbarStore.dispatch(tabCounterButton.onClick)

        verify {
            navController.nav(
                R.id.homeFragment,
                NavGraphDirections.actionGlobalTabsTrayFragment(page = Page.PrivateTabs),
            )
        }
    }

    @Test
    fun `GIVEN browsing in normal mode WHEN clicking on the long click menu option THEN open a new private tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, false), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Private, browsingModeManager.mode)
        assertEquals("", toolbarStore.state.editState.query)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN browsing in private mode WHEN clicking on the long click menu option THEN open a new normal tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )
        val tabCounterButton = toolbarStore.state.displayState.browserActionsEnd[0] as TabCounterAction
        assertEqualsToolbarButton(expectedToolbarButton(0, true), tabCounterButton)
        val tabCounterMenuItems = (tabCounterButton.onLongClick as CombinedEventAndMenu).menu.items()

        toolbarStore.dispatch((tabCounterMenuItems[0] as BrowserToolbarMenuButton).onClick!!)

        assertEquals(Normal, browsingModeManager.mode)
        assertEquals("", toolbarStore.state.editState.query)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN in normal browsing mode WHEN the page origin is clicked THEN start the search UX for normal browsing`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `GIVEN in private browsing mode WHEN the page origin is clicked THEN start the search UX for private browsing`() {
        val browsingModeManager = SimpleBrowsingModeManager(Private)
        val navController: NavController = mockk(relaxed = true)
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )

        toolbarStore.dispatch(toolbarStore.state.displayState.pageOrigin.onClick as BrowserToolbarAction)

        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `WHEN choosing to paste from clipboard THEN start a new search with the current clipboard text`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val clipboard = ClipboardHandler(testContext).also {
            it.text = "test"
        }
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, clipboard, mockk())
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = mockk(),
            browsingModeManager = browsingModeManager,
        )

        toolbarStore.dispatch(PasteFromClipboardClicked)

        assertEquals(clipboard.text, toolbarStore.state.editState.query)
        verify { appStore.dispatch(SearchStarted()) }
    }

    @Test
    fun `WHEN choosing to load URL from clipboard THEN start load the URL from clipboard in a new tab`() {
        val browsingModeManager = SimpleBrowsingModeManager(Normal)
        val navController: NavController = mockk(relaxed = true)
        val clipboardUrl = "https://www.mozilla.com"
        val clipboard = ClipboardHandler(testContext).also {
            it.text = clipboardUrl
        }
        val browserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
        val useCases: UseCases = mockk {
            every { fenixBrowserUseCases } returns browserUseCases
        }
        val selectedSearchEngine = appStore.state.searchState.selectedSearchEngine?.searchEngine
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, clipboard, useCases)
        val toolbarStore = buildStore(
            middleware = middleware,
            navController = navController,
            browsingModeManager = browsingModeManager,
        )

        toolbarStore.dispatch(LoadFromClipboardClicked)

        verify {
            browserUseCases.loadUrlOrSearch(
                searchTermOrURL = clipboardUrl,
                newTab = true,
                private = false,
                searchEngine = selectedSearchEngine,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `WHEN the selected search engine changes THEN update the search selector`() {
        val appStore = AppStore()
        Dispatchers.setMain(Handler(Looper.getMainLooper()).asCoroutineDispatcher())

        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(middleware)
        val newSearchEngine = SearchEngine("test", "Test", mock(), type = APPLICATION)

        appStore.dispatch(SearchEngineSelected(newSearchEngine, true)).joinBlocking()
        shadowOf(Looper.getMainLooper()).idle() // wait for observing and processing the search engine update

        assertSearchSelectorEquals(
            expectedSearchSelector(newSearchEngine),
            toolbarStore.state.displayState.pageActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `GIVEN a search engine is already selected WHEN the search engine configuration changes THEN don't change the selected search engine`() {
        Dispatchers.setMain(Handler(Looper.getMainLooper()).asCoroutineDispatcher())

        val selectedSearchEngine = SearchEngine("test", "Test", mock(), type = APPLICATION)
        val otherSearchEngine = SearchEngine("other", "Other", mock(), type = APPLICATION)
        val appStore = AppStore(
            initialState = AppState(
                searchState = SearchState.EMPTY.copy(
                    selectedSearchEngine = SelectedSearchEngine(selectedSearchEngine, true),
                ),
            ),
        )
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val toolbarStore = buildStore(middleware)

        browserStore.dispatch(ApplicationSearchEnginesLoaded(listOf(otherSearchEngine))).joinBlocking()

        assertNotEquals(
            appStore.state.searchState.selectedSearchEngine?.searchEngine,
            browserStore.state.search.selectedOrDefaultSearchEngine,
        )
        assertSearchSelectorEquals(
            expectedSearchSelector(selectedSearchEngine),
            toolbarStore.state.displayState.pageActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `WHEN building TabCounter action THEN returns TabCounterAction with correct count and menu`() {
        val browserStore = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://www.mozilla.org"),
                    createTab(id = "b", url = "https://www.firefox.com"),
                    createTab(id = "c", url = "https://getpocket.com"),
                ),
            ),
        )

        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val store = buildStore(middleware)

        val action = middleware.buildHomeAction(
            action = HomeToolbarAction.TabCounter,
        ) as TabCounterAction

        assertEquals(3, action.count)
        assertEquals(
            testContext.getString(R.string.mozac_tab_counter_open_tab_tray, 3),
            action.contentDescription,
        )
        assertEquals(
            middleware.environment?.browsingModeManager?.mode == Private,
            action.showPrivacyMask,
        )
        assertEquals(TabCounterClicked(Source.AddressBar), action.onClick)
        assertNotNull(action.onLongClick)
    }

    @Test
    fun `WHEN building Menu action THEN returns Menu ActionButton`() {
        val middleware = BrowserToolbarMiddleware(appStore, browserStore, mockk(), mockk())
        val store = buildStore(middleware)

        val action = middleware.buildHomeAction(
            action = HomeToolbarAction.Menu,
        ) as ActionButtonRes

        assertEquals(R.drawable.mozac_ic_ellipsis_vertical_24, action.drawableResId)
        assertEquals(R.string.content_description_menu, action.contentDescription)
        assertEquals(ActionButton.State.DEFAULT, action.state)
        assertEquals(MenuClicked(source = Source.AddressBar), action.onClick)
        assertNull(action.onLongClick)
    }

    private fun buildStore(
        middleware: BrowserToolbarMiddleware,
        context: Context = testContext,
        lifecycleOwner: LifecycleOwner = this.lifecycleOwner,
        navController: NavController = mockk(),
        browsingModeManager: BrowsingModeManager = this.browsingModeManager,
    ) = BrowserToolbarStore(
        middleware = listOf(middleware),
    ).also {
        it.dispatch(
            EnvironmentRehydrated(
                BrowserToolbarEnvironment(
                    context = context,
                    viewLifecycleOwner = lifecycleOwner,
                    navController = navController,
                    browsingModeManager = browsingModeManager,
                ),
            ),
        )
    }

    private fun expectedSearchSelector(
        defaultOrSelectedSearchEngine: SearchEngine,
        searchEngineShortcuts: List<SearchEngine> = emptyList(),
    ) = buildExpectedSearchSelector(
        defaultOrSelectedSearchEngine,
        searchEngineShortcuts,
        testContext.resources,
    )

    private fun assertEqualsToolbarButton(expected: TabCounterAction, actual: TabCounterAction) {
        assertEquals(expected.count, actual.count)
        assertEquals(expected.contentDescription, actual.contentDescription)
        assertEquals(expected.showPrivacyMask, actual.showPrivacyMask)
        assertEquals(expected.onClick, actual.onClick)
        when (expected.onLongClick) {
            null -> assertNull(actual.onLongClick)
            is BrowserToolbarEvent -> assertEquals(expected.onLongClick, actual.onLongClick)
            is BrowserToolbarMenu -> assertEquals(
                (expected.onLongClick as BrowserToolbarMenu).items(),
                (actual.onLongClick as BrowserToolbarMenu).items(),
            )
            is CombinedEventAndMenu -> {
                assertEquals(
                    (expected.onLongClick as CombinedEventAndMenu).event,
                    (actual.onLongClick as CombinedEventAndMenu).event,
                )
                assertEquals(
                    (expected.onLongClick as CombinedEventAndMenu).menu.items(),
                    (actual.onLongClick as CombinedEventAndMenu).menu.items(),
                )
            }
        }
    }

    private fun expectedToolbarButton(
        tabCount: Int = 0,
        isPrivate: Boolean = false,
        source: Source = Source.AddressBar,
    ) = TabCounterAction(
        count = tabCount,
        contentDescription = if (isPrivate) {
            testContext.getString(
                R.string.mozac_tab_counter_private,
                tabCount.toString(),
            )
        } else {
            testContext.getString(
                R.string.mozac_tab_counter_open_tab_tray,
                tabCount.toString(),
            )
        },
        showPrivacyMask = isPrivate,
        onClick = TabCounterClicked(source),
        onLongClick = CombinedEventAndMenu(TabCounterLongClicked(source)) {
            when (isPrivate) {
                true -> listOf(
                    BrowserToolbarMenuButton(
                        icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                        text = StringResText(R.string.mozac_browser_menu_new_tab),
                        contentDescription = StringResContentDescription(R.string.mozac_browser_menu_new_tab),
                        onClick = AddNewTab(source),
                    ),
                )

                false -> listOf(
                    BrowserToolbarMenuButton(
                        icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_24),
                        text = StringResText(R.string.mozac_browser_menu_new_private_tab),
                        contentDescription = StringResContentDescription(R.string.mozac_browser_menu_new_private_tab),
                        onClick = AddNewPrivateTab(source),
                    ),
                )
            }
        },
    )

    private fun expectedMenuButton(source: Source = Source.AddressBar) = ActionButtonRes(
        drawableResId = R.drawable.mozac_ic_ellipsis_vertical_24,
        contentDescription = R.string.content_description_menu,
        onClick = MenuClicked(source),
    )

    private val expectedBookmarkButton = ActionButtonRes(
        drawableResId = R.drawable.mozac_ic_bookmark_24,
        contentDescription = R.string.browser_menu_bookmark_this_page_2,
        state = ActionButton.State.DISABLED,
        onClick = FakeClicked,
    )

    private val expectedShareButton = ActionButtonRes(
        drawableResId = R.drawable.mozac_ic_share_android_24,
        contentDescription = R.string.browser_menu_share,
        state = ActionButton.State.DISABLED,
        onClick = FakeClicked,
    )

    private fun expectedNewTabButton(source: Source = Source.AddressBar) = ActionButtonRes(
        drawableResId = R.drawable.mozac_ic_plus_24,
        contentDescription = R.string.home_screen_shortcut_open_new_tab_2,
        onClick = AddNewTab(source),
    )

    private class FakeLifecycleOwner(initialState: Lifecycle.State) : LifecycleOwner {
        override val lifecycle: Lifecycle = LifecycleRegistry(this).apply {
            currentState = initialState
        }
    }
}
