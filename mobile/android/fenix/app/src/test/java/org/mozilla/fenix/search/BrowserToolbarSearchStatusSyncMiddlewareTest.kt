/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import androidx.lifecycle.Lifecycle
import io.mockk.mockk
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToggleEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainLooperTestRule
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.toolbar.BrowserToolbarEnvironment
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BrowserToolbarSearchStatusSyncMiddlewareTest {

    @get:Rule
    val mainLooperRule = MainLooperTestRule()

    private val appStore = AppStore()

    @Test
    fun `GIVEN an environment was already set WHEN it is cleared THEN reset it to null`() {
        val (middleware, toolbarStore) = buildMiddlewareAndAddToSearchStore()

        assertNotNull(middleware.environment)

        toolbarStore.dispatch(EnvironmentCleared)

        assertNull(middleware.environment)
    }

    @Test
    fun `WHEN the toolbar exits search mode THEN synchronize search being ended for the application`() = runTest {
        val (_, toolbarStore) = buildMiddlewareAndAddToSearchStore()
        assertFalse(appStore.state.searchState.isSearchActive)
        assertFalse(toolbarStore.state.isEditMode())

        appStore.dispatch(SearchStarted()).joinBlocking()
        mainLooperRule.idle()
        assertTrue(appStore.state.searchState.isSearchActive)
        assertTrue(toolbarStore.state.isEditMode())

        toolbarStore.dispatch(ToggleEditMode(false)).joinBlocking()
        appStore.waitUntilIdle()
        mainLooperRule.idle()
        assertFalse(appStore.state.searchState.isSearchActive)
        assertFalse(toolbarStore.state.isEditMode())
    }

    @Test
    fun `WHEN the toolbar enters search mode THEN don't update the search state for the application`() = runTest {
        val (_, toolbarStore) = buildMiddlewareAndAddToSearchStore()
        assertFalse(toolbarStore.state.isEditMode())
        assertFalse(appStore.state.searchState.isSearchActive)

        toolbarStore.dispatch(ToggleEditMode(true)).joinBlocking()
        mainLooperRule.idle()

        assertFalse(appStore.state.searchState.isSearchActive)
    }

    @Test
    fun `WHEN search starts in the application THEN put the toolbar in search mode also`() = runTest {
        val (_, toolbarStore) = buildMiddlewareAndAddToSearchStore()

        appStore.dispatch(SearchStarted()).joinBlocking()
        mainLooperRule.idle()

        assertTrue(toolbarStore.state.isEditMode())
        assertTrue(appStore.state.searchState.isSearchActive)
    }

    @Test
    fun `WHEN search is closed in the application THEN synchronize exiting edit mode in the toolbar`() = runTest {
        val (_, toolbarStore) = buildMiddlewareAndAddToSearchStore()
        appStore.dispatch(SearchStarted()).joinBlocking()
        mainLooperRule.idle()
        assertTrue(toolbarStore.state.isEditMode())
        assertTrue(appStore.state.searchState.isSearchActive)

        appStore.dispatch(SearchEnded).joinBlocking()
        mainLooperRule.idle()
        assertFalse(appStore.state.searchState.isSearchActive)
        assertFalse(toolbarStore.state.isEditMode())
    }

    private fun buildMiddlewareAndAddToSearchStore(
        appStore: AppStore = this.appStore,
    ): Pair<BrowserToolbarSearchStatusSyncMiddleware, BrowserToolbarStore> {
        val middleware = buildMiddleware(appStore)
        val toolbarStore = BrowserToolbarStore(
            middleware = listOf(middleware),
        ).also {
            it.dispatch(
                EnvironmentRehydrated(
                    BrowserToolbarEnvironment(
                        context = testContext,
                        navController = mockk(),
                        viewLifecycleOwner = TestLifecycleOwner(Lifecycle.State.RESUMED),
                        browsingModeManager = mockk(),
                    ),
                ),
            )
        }
        return middleware to toolbarStore
    }

    private fun buildMiddleware(
        appStore: AppStore = this.appStore,
    ) = BrowserToolbarSearchStatusSyncMiddleware(appStore)
}
