/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.content.Intent
import android.content.res.Resources
import android.os.Build
import android.speech.RecognizerIntent
import androidx.annotation.VisibleForTesting
import androidx.core.graphics.drawable.toDrawable
import androidx.lifecycle.Lifecycle.State.RESUMED
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.NavController
import androidx.navigation.NavOptions
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.AwesomeBarAction.EngagementFinished
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.search.SearchEngine.Type.APPLICATION
import mozilla.components.browser.state.search.SearchEngine.Type.CUSTOM
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.AutocompleteProvidersUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.HintUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchAborted
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.UrlSuggestionAutocompleted
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.CommitUrl
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToggleEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.concept.engine.EngineSession
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.ktx.kotlin.isUrl
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.UnifiedSearch
import org.mozilla.fenix.GleanMetrics.VoiceSearch
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.QrScannerAction.QrScannerRequested
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEngineSelected
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequestCleared
import org.mozilla.fenix.components.appstate.VoiceSearchAction.VoiceInputRequested
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.TABS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.toolbar.BrowserToolbarEnvironment
import org.mozilla.fenix.ext.toolbarHintRes
import org.mozilla.fenix.search.EditPageEndActionsInteractions.ClearSearchClicked
import org.mozilla.fenix.search.EditPageEndActionsInteractions.QrScannerClicked
import org.mozilla.fenix.search.EditPageEndActionsInteractions.VoiceSearchButtonClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorItemClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSettingsItemClicked
import org.mozilla.fenix.search.ext.searchEngineShortcuts
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.Settings
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.ContentDescription.StringContentDescription as SearchSelectorDescription
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.Icon.DrawableIcon as SearchSelectorIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringContentDescription as MenuItemStringDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription as MenuItemDescriptionRes
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableIcon as MenuItemIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon as MenuItemIconRes
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText as MenuItemStringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringText as MenuItemStringText
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.ui.icons.R as iconsR

@VisibleForTesting
internal sealed class SearchSelectorEvents : BrowserToolbarEvent {
    data object SearchSelectorClicked : SearchSelectorEvents()

    data object SearchSettingsItemClicked : SearchSelectorEvents()

    data class SearchSelectorItemClicked(
        val searchEngine: SearchEngine,
    ) : SearchSelectorEvents()
}

@VisibleForTesting
internal sealed class EditPageEndActionsInteractions : BrowserToolbarEvent {
    data object ClearSearchClicked : EditPageEndActionsInteractions()
    data object QrScannerClicked : EditPageEndActionsInteractions()

    data object VoiceSearchButtonClicked : SearchSelectorEvents()
}

/**
 * [BrowserToolbarStore] middleware handling the configuration of the composable toolbar
 * while in edit mode.
 *
 * @param appStore [AppStore] used for querying and updating application state.
 * @param browserStore [BrowserStore] used for querying and updating browser state.
 * @param components [Components] for accessing other functionalities of the application.
 * @param settings [Settings] for accessing application settings.
 */
class BrowserToolbarSearchMiddleware(
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val components: Components,
    private val settings: Settings,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    @VisibleForTesting
    internal var environment: BrowserToolbarEnvironment? = null
    private var syncCurrentSearchEngineJob: Job? = null
    private var syncAvailableSearchEnginesJob: Job? = null
    private var observeQRScannerInputJob: Job? = null
    private var observeVoiceInputJob: Job? = null

    @Suppress("CyclomaticComplexMethod", "LongMethod")
    override fun invoke(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        if (action !is SearchSelectorEvents) {
            next(action)
        }

        when (action) {
            is EnvironmentRehydrated -> {
                environment = action.environment as? BrowserToolbarEnvironment

                if (context.state.isEditMode()) {
                    syncCurrentSearchEngine(context)
                }
            }

            is EnvironmentCleared -> {
                environment = null
                context.dispatch(AutocompleteProvidersUpdated(emptyList()))
            }

            is ToggleEditMode -> {
                if (action.editMode) {
                    refreshConfigurationAfterSearchEngineChange(
                        context = context,
                        searchEngine = reconcileSelectedEngine(),
                    )
                    observeVoiceInputResults(context)
                    syncCurrentSearchEngine(context)
                    syncAvailableEngines(context)
                    updateSearchEndPageActions(context)
                } else {
                    syncCurrentSearchEngineJob?.cancel()
                    syncAvailableSearchEnginesJob?.cancel()

                    if (observeQRScannerInputJob?.isActive == true) {
                        appStore.dispatch(AppAction.QrScannerAction.QrScannerDismissed)
                    }
                    observeQRScannerInputJob?.cancel()
                    observeVoiceInputJob?.cancel()
                }
            }

            is SearchAborted -> {
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                    val sourceTabId = appStore.state.searchState.sourceTabId
                    appStore.dispatch(SearchEnded)
                    browserStore.dispatch(EngagementFinished(abandoned = true))
                    if (sourceTabId != null) {
                        environment?.navController?.navigate(R.id.browserFragment)
                    }
                }
            }

            is SearchSelectorClicked -> {
                UnifiedSearch.searchMenuTapped.record(NoExtras())
            }

            is SearchSettingsItemClicked -> {
                context.dispatch(SearchQueryUpdated(""))
                appStore.dispatch(SearchEnded)
                browserStore.dispatch(EngagementFinished(abandoned = true))
                environment?.navController?.navigate(
                    BrowserFragmentDirections.actionGlobalSearchEngineFragment(),
                )
            }

            is SearchSelectorItemClicked -> {
                appStore.dispatch(SearchEngineSelected(action.searchEngine, true))
                appStore.dispatch(SearchStarted())
                refreshConfigurationAfterSearchEngineChange(context, action.searchEngine)
                updateSearchEndPageActions(context) // to update the visibility of the qr scanner button
            }

            is UrlSuggestionAutocompleted -> {
                components.core.engine.speculativeConnect(action.url)
            }

            is CommitUrl -> {
                // Do not load URL if application search engine is selected.
                if (reconcileSelectedEngine()?.type == SearchEngine.Type.APPLICATION) {
                    return
                }

                val navController = environment?.navController ?: return

                when (action.text) {
                    "about:crashes" -> {
                        // The list of past crashes can be accessed via "settings > about", but desktop and
                        // fennec users may be used to navigating to "about:crashes". So we intercept this here
                        // and open the crash list activity instead.
                        navController.navigate(
                            NavGraphDirections.actionGlobalCrashListFragment(),
                        )
                    }
                    "about:addons" -> {
                        navController.navigate(
                            NavGraphDirections.actionGlobalAddonsManagementFragment(),
                        )
                        browserStore.dispatch(EngagementFinished(abandoned = false))
                    }
                    "about:glean" -> {
                        navController.navigate(
                            NavGraphDirections.actionGlobalGleanDebugToolsFragment(),
                        )
                    }
                    "moz://a" -> openSearchOrUrl(
                        SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.MANIFESTO),
                        navController,
                    )
                    else ->
                        if (action.text.isNotBlank()) {
                            openSearchOrUrl(action.text, navController)
                        } else {
                            browserStore.dispatch(EngagementFinished(abandoned = true))
                        }
                }

                appStore.dispatch(SearchEnded)
            }

            is ClearSearchClicked -> {
                Events.browserToolbarInputCleared.record(NoExtras())
                context.dispatch(SearchQueryUpdated(""))
            }

            is SearchQueryUpdated -> {
                updateSearchEndPageActions(context)
            }

            is QrScannerClicked -> {
                Events.browserToolbarQrScanTapped.record(NoExtras())
                observeQrScannerInput(context)
                appStore.dispatch(QrScannerRequested)
            }

            is VoiceSearchButtonClicked -> {
                VoiceSearch.tapped.record(NoExtras())
                appStore.dispatch(VoiceInputRequested)
            }

            else -> {
                // no-op.
            }
        }
    }

    private fun openSearchOrUrl(text: String, navController: NavController) {
        val searchEngine = reconcileSelectedEngine()
        val isDefaultEngine = searchEngine?.id == browserStore.state.search.selectedOrDefaultSearchEngine?.id
        val newTab = if (settings.enableHomepageAsNewTab) {
            false
        } else {
            appStore.state.searchState.sourceTabId == null
        }

        navController.navigate(
            NavGraphDirections.actionGlobalBrowser(),
        )

        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = text,
            newTab = newTab,
            forceSearch = !isDefaultEngine,
            private = environment?.browsingModeManager?.mode == Private,
            searchEngine = searchEngine,
        )

        if (text.isUrl() || searchEngine == null) {
            Events.enteredUrl.record(Events.EnteredUrlExtra(autocomplete = false))
        } else {
            val searchAccessPoint = when (appStore.state.searchState.searchAccessPoint) {
                MetricsUtils.Source.NONE -> MetricsUtils.Source.ACTION
                else -> appStore.state.searchState.searchAccessPoint
            }

            MetricsUtils.recordSearchMetrics(
                searchEngine,
                isDefaultEngine,
                searchAccessPoint,
                components.nimbus.events,
            )
        }

        browserStore.dispatch(EngagementFinished(abandoned = false))
    }

    private fun refreshConfigurationAfterSearchEngineChange(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        searchEngine: SearchEngine?,
    ) {
        updateSearchSelectorMenu(context, searchEngine, browserStore.state.search.searchEngineShortcuts)
        updateAutocompleteProviders(context, searchEngine)
        updateToolbarHint(context, searchEngine)
    }

    private fun updateToolbarHint(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        engine: SearchEngine?,
    ) {
        val defaultEngine = browserStore.state.search.selectedOrDefaultSearchEngine
        val hintRes = engine.toolbarHintRes(defaultEngine)
        context.dispatch(HintUpdated(hintRes))
    }

    /**
     * Synchronously update the toolbar with a new search selector.
     */
    private fun updateSearchSelectorMenu(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine?,
        searchEngineShortcuts: List<SearchEngine>,
    ) {
        val environment = environment ?: return

        val searchSelector = buildSearchSelector(
            selectedSearchEngine, searchEngineShortcuts, environment.context.resources,
        )
        context.dispatch(
            SearchActionsStartUpdated(
                when (searchSelector == null) {
                    true -> emptyList()
                    else -> listOf(searchSelector)
                },
            ),
        )
    }

    private fun updateAutocompleteProviders(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine?,
    ) {
        if (settings.shouldAutocompleteInAwesomebar) {
            val autocompleteProviders = buildAutocompleteProvidersList(selectedSearchEngine)
            context.dispatch(AutocompleteProvidersUpdated(autocompleteProviders))
        } else {
            context.dispatch(AutocompleteProvidersUpdated(emptyList()))
        }
    }

    private fun buildAutocompleteProvidersList(selectedSearchEngine: SearchEngine?) = when (selectedSearchEngine?.id) {
        browserStore.state.search.selectedOrDefaultSearchEngine?.id -> listOfNotNull(
            when (settings.shouldShowHistorySuggestions) {
                true -> components.core.historyStorage
                false -> null
            },
            when (settings.shouldShowBookmarkSuggestions) {
                true -> components.core.bookmarksStorage
                false -> null
            },
            components.core.domainsAutocompleteProvider,
        )

        TABS_SEARCH_ENGINE_ID -> listOf(
            components.core.sessionAutocompleteProvider,
            components.backgroundServices.syncedTabsAutocompleteProvider,
        )

        BOOKMARKS_SEARCH_ENGINE_ID -> listOf(
            components.core.bookmarksStorage,
        )

        HISTORY_SEARCH_ENGINE_ID -> listOf(
            components.core.historyStorage,
        )

        else -> emptyList()
    }

    private fun syncCurrentSearchEngine(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        syncCurrentSearchEngineJob?.cancel()
        syncCurrentSearchEngineJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.searchState.selectedSearchEngine?.searchEngine }
                .collect {
                    it.searchState.selectedSearchEngine?.let {
                        refreshConfigurationAfterSearchEngineChange(context, it.searchEngine)
                    }
                }
        }
    }

    private fun syncAvailableEngines(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        syncAvailableSearchEnginesJob?.cancel()
        syncAvailableSearchEnginesJob = browserStore.observeWhileActive {
            distinctUntilChangedBy { it.search.searchEngineShortcuts }
                .collect {
                    refreshConfigurationAfterSearchEngineChange(
                        context = context,
                        searchEngine = reconcileSelectedEngine(),
                    )
                }
        }
    }

    private fun reconcileSelectedEngine(): SearchEngine? =
        appStore.state.searchState.selectedSearchEngine?.searchEngine
            ?: browserStore.state.search.selectedOrDefaultSearchEngine

    private fun updateSearchEndPageActions(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        selectedSearchEngine: SearchEngine? = reconcileSelectedEngine(),
    ) = context.dispatch(
        SearchActionsEndUpdated(
            buildSearchEndPageActions(
                context.state.editState.query,
                selectedSearchEngine,
            ),
        ),
    )

    private fun buildSearchEndPageActions(
        queryText: String,
        selectedSearchEngine: SearchEngine?,
    ): List<Action> = buildList {
        val isValidSearchEngine = selectedSearchEngine?.isGeneral == true ||
                selectedSearchEngine?.type == CUSTOM

        if (settings.shouldShowVoiceSearch && isSpeechRecognitionAvailable()) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_microphone_24,
                    contentDescription = R.string.voice_search_content_description,
                    onClick = VoiceSearchButtonClicked,
                ),
            )
        }
        if (queryText.isNotEmpty()) {
            add(
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_cross_circle_fill_24,
                    contentDescription = R.string.mozac_clear_button_description,
                    state = ActionButton.State.DEFAULT,
                    onClick = ClearSearchClicked,
                ),
            )
        } else if (isValidSearchEngine) {
            add(
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_qr_code_24,
                    contentDescription = R.string.mozac_feature_qr_scanner,
                    state = ActionButton.State.DEFAULT,
                    onClick = QrScannerClicked,
                ),
            )
        }
    }

    private fun observeQrScannerInput(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        observeQRScannerInputJob = null
        observeQRScannerInputJob = appStore.observeWhileActive {
            distinctUntilChangedBy { it.qrScannerState.lastScanData }
                .collect {
                    if (it.qrScannerState.lastScanData?.isNotEmpty() == true) {
                        observeQRScannerInputJob?.cancel()

                        appStore.dispatch(AppAction.QrScannerAction.QrScannerInputConsumed)
                        context.dispatch(SearchQueryUpdated(it.qrScannerState.lastScanData))
                        components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
                            searchTermOrURL = it.qrScannerState.lastScanData,
                            newTab = appStore.state.searchState.sourceTabId == null,
                            flags = EngineSession.LoadUrlFlags.external(),
                            private = false,
                        )
                        environment?.navController?.navigate(
                            resId = R.id.browserFragment,
                            args = null,
                            navOptions = when (environment?.navController?.currentDestination?.id) {
                                R.id.historyFragment,
                                R.id.bookmarkFragment,
                                    -> NavOptions.Builder()
                                        .setPopUpTo(R.id.browserFragment, true)
                                        .build()

                                else -> null
                            },
                        )
                    }
                }
        }
    }

    private fun observeVoiceInputResults(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        observeVoiceInputJob?.cancel()
        observeVoiceInputJob = appStore.observeWhileActive {
            map { it.voiceSearchState.voiceInputResult }
                .distinctUntilChanged()
                .collect { voiceInputResult ->
                    if (!voiceInputResult.isNullOrEmpty()) {
                        context.dispatch(
                            SearchQueryUpdated(
                                query = voiceInputResult,
                                showAsPreselected = true,
                            ),
                        )
                        appStore.dispatch(VoiceInputRequestCleared)
                    }
                }
        }
    }

    @VisibleForTesting
    internal fun isSpeechRecognitionAvailable() = environment?.context?.let {
        Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
            .resolveActivity(it.packageManager) != null
    } ?: false

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job? = environment?.viewLifecycleOwner?.run {
        lifecycleScope.launch {
            repeatOnLifecycle(RESUMED) {
                flow().observe()
            }
        }
    }

    /**
     * Static functionalities of the [BrowserToolbarSearchMiddleware].
     */
    companion object {
        /**
         * Builds a [SearchSelectorAction] to be shown in the [BrowserToolbar].
         *
         * @param selectedSearchEngine The currently selected search engine.
         * @param searchEngineShortcuts The list of search engines available for selection.
         * @param resources [Resources] Used for accessing application resources.
         */
        fun buildSearchSelector(
            selectedSearchEngine: SearchEngine?,
            searchEngineShortcuts: List<SearchEngine>,
            resources: Resources,
        ): SearchSelectorAction? {
            if (selectedSearchEngine == null) {
                return null
            }

            val menuItems = buildList<BrowserToolbarMenuItem> {
                add(
                    BrowserToolbarMenuButton(
                        icon = null,
                        text = MenuItemStringResText(R.string.search_header_menu_item_2),
                        contentDescription = MenuItemDescriptionRes(R.string.search_header_menu_item_2),
                        onClick = null,
                    ),
                )
                addAll(searchEngineShortcuts.toToolbarMenuItems(resources))
                add(
                    BrowserToolbarMenuButton(
                        icon = MenuItemIconRes(R.drawable.mozac_ic_settings_24),
                        text = MenuItemStringResText(R.string.search_settings_menu_item),
                        contentDescription = MenuItemDescriptionRes(R.string.search_settings_menu_item),
                        onClick = SearchSettingsItemClicked,
                    ),
                )
            }

            return SearchSelectorAction(
                icon = SearchSelectorIcon(
                    drawable = selectedSearchEngine.icon.toDrawable(resources),
                    shouldTint = selectedSearchEngine.type == APPLICATION,
                ),
                contentDescription = SearchSelectorDescription(
                    resources.getString(
                        R.string.search_engine_selector_content_description,
                        selectedSearchEngine.name,
                    ),
                ),
                menu = BrowserToolbarMenu { menuItems },
                onClick = SearchSelectorClicked,
            )
        }

        private fun List<SearchEngine>.toToolbarMenuItems(
            resources: Resources,
        ) = map { searchEngine ->
            BrowserToolbarMenuButton(
                icon = MenuItemIcon(
                    drawable = searchEngine.icon.toDrawable(resources),
                    shouldTint = searchEngine.type == APPLICATION,
                ),
                text = MenuItemStringText(searchEngine.name),
                contentDescription = MenuItemStringDescription(searchEngine.name),
                onClick = SearchSelectorItemClicked(searchEngine),
            )
        }
    }
}
