/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.compose.browser.browser

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.navigation.NavController
import mozilla.components.browser.state.helper.Target
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.awesomebar.AwesomeBar
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.engine.WebContent
import mozilla.components.compose.tabstray.TabList
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.feature.awesomebar.provider.ClipboardSuggestionProvider
import mozilla.components.feature.awesomebar.provider.SearchActionProvider
import mozilla.components.feature.awesomebar.provider.SearchSuggestionProvider
import mozilla.components.feature.awesomebar.provider.SessionSuggestionProvider
import mozilla.components.feature.fxsuggest.FxSuggestSuggestionProvider
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.composableStore
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.ext.observeAsState
import org.mozilla.samples.compose.browser.browser.BrowserToolbarMiddleware.Companion.Dependencies
import org.mozilla.samples.compose.browser.components
import mozilla.components.feature.awesomebar.R as awesomebarR
import mozilla.components.feature.fxsuggest.R as fxsuggestR

/**
 * The main browser screen.
 */
@Suppress("LongMethod")
@Composable
fun BrowserScreen(navController: NavController) {
    val context = LocalContext.current

    val store = composableStore<BrowserScreenState, BrowserScreenAction> { restoredState ->
        BrowserScreenStore(restoredState ?: BrowserScreenState())
    }
    val toolbarStore = remember {
        BrowserToolbarStore(
            middleware = listOf(
                BrowserToolbarMiddleware(
                    initialDependencies = Dependencies(
                        context = context,
                        navController = navController,
                        browserScreenStore = store,
                    ),
                ),
            ),
        )
    }

    val toolbarState by toolbarStore.observeAsState(initialValue = toolbarStore.state) { it }
    val showTabs = store.observeAsComposableState { state -> state.showTabs }

    BackHandler(enabled = toolbarState.isEditMode()) {
        toolbarStore.dispatch(BrowserToolbarAction.ToggleEditMode(false))
    }
    AcornTheme {
        Box {
            Column {
                BrowserToolbar(
                    store = toolbarStore,
                )

                Box {
                    WebContent(
                        components().engine,
                        components().store,
                        Target.SelectedTab,
                    )

                    val url = toolbarState.editState.query
                    if (toolbarState.isEditMode() && url.isNotEmpty()) {
                        Suggestions(
                            url,
                            onSuggestionClicked = { suggestion ->
                                toolbarStore.dispatch(BrowserToolbarAction.ToggleEditMode(false))
                                suggestion.onSuggestionClicked?.invoke()
                            },
                            onAutoComplete = { suggestion ->
                                toolbarStore.dispatch(
                                    BrowserEditToolbarAction.SearchQueryUpdated(
                                        suggestion.editSuggestion!!,
                                    ),
                                )
                            },
                        )
                    }
                }
            }

            if (showTabs.value == true) {
                TabsTray(
                    store = store,
                    toolbarStore = toolbarStore,
                )
            }
        }
    }
}

/**
 * Shows the lit of tabs.
 */
@Composable
fun TabsTray(
    store: Store<BrowserScreenState, BrowserScreenAction>,
    toolbarStore: BrowserToolbarStore,
) {
    val components = components()

    BackHandler(onBack = { store.dispatch(BrowserScreenAction.HideTabs) })

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .fillMaxHeight()
            .background(color = MaterialTheme.colorScheme.onSurfaceVariant)
            .clickable {
                store.dispatch(BrowserScreenAction.HideTabs)
            },
    ) {
        Column(
            modifier = Modifier
                .fillMaxHeight(fraction = 0.8f)
                .align(Alignment.BottomStart),
        ) {
            TabList(
                store = components().store,
                onTabSelected = { tab ->
                    components.tabsUseCases.selectTab(tab.id)
                    store.dispatch(BrowserScreenAction.HideTabs)
                },
                onTabClosed = { tab ->
                    components.tabsUseCases.removeTab(tab.id)
                },
                modifier = Modifier.weight(1f),
            )
            Button(
                onClick = {
                    components.tabsUseCases.addTab(
                        url = "about:blank",
                        selectTab = true,
                    )
                    store.dispatch(BrowserScreenAction.HideTabs)
                    toolbarStore.dispatch(BrowserToolbarAction.ToggleEditMode(true))
                },
            ) {
                Text("+")
            }
        }
    }
}

@Composable
private fun Suggestions(
    url: String,
    onSuggestionClicked: (AwesomeBar.Suggestion) -> Unit,
    onAutoComplete: (AwesomeBar.Suggestion) -> Unit,
) {
    val context = LocalContext.current
    val components = components()

    val sessionSuggestionProvider = remember(context) {
        SessionSuggestionProvider(
            components.store,
            components.tabsUseCases.selectTab,
            switchToTabDescription = context.getString(
                awesomebarR.string.switch_to_tab_description,
            ),
        )
    }

    val searchActionProvider = remember {
        SearchActionProvider(components.store, components.searchUseCases.defaultSearch)
    }

    val fxSuggestSuggestionProvider = remember(context) {
        FxSuggestSuggestionProvider(
            loadUrlUseCase = components.sessionUseCases.loadUrl,
            includeSponsoredSuggestions = false,
            includeNonSponsoredSuggestions = true,
            sponsoredSuggestionDescription = context.getString(
                fxsuggestR.string.sponsored_suggestion_description,
            ),
        )
    }

    val searchSuggestionProvider = remember(context) {
        SearchSuggestionProvider(
            components.store,
            components.searchUseCases.defaultSearch,
            components.client,
            mode = SearchSuggestionProvider.Mode.MULTIPLE_SUGGESTIONS,
            engine = components.engine,
            filterExactMatch = true,
        )
    }

    val clipboardSuggestionProvider = remember(context) {
        ClipboardSuggestionProvider(
            context,
            components.sessionUseCases.loadUrl,
        )
    }

    val keyboardController = LocalSoftwareKeyboardController.current

    AwesomeBar(
        url,
        providers = listOf(
            sessionSuggestionProvider,
            searchActionProvider,
            fxSuggestSuggestionProvider,
            searchSuggestionProvider,
            clipboardSuggestionProvider,
        ),
        onSuggestionClicked = { suggestion -> onSuggestionClicked(suggestion) },
        onAutoComplete = { suggestion -> onAutoComplete(suggestion) },
        onScroll = { keyboardController?.hide() },
    )
}
