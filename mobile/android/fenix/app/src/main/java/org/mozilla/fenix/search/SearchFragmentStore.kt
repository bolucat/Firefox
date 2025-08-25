/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.concept.awesomebar.AwesomeBar.Suggestion
import mozilla.components.concept.awesomebar.AwesomeBar.SuggestionProvider
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.UiStore
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.automotive.isAndroidAutomotiveAvailable
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.search.SearchFragmentAction.Init
import org.mozilla.fenix.search.SearchFragmentStore.Environment
import org.mozilla.fenix.utils.Settings

/**
 * The [Store] for holding the [SearchFragmentState] and applying [SearchFragmentAction]s.
 *
 * @param initialState The initial state of the store.
 * @param middleware The [Middleware]s to apply to the store.
 */
class SearchFragmentStore(
    initialState: SearchFragmentState,
    middleware: List<Middleware<SearchFragmentState, SearchFragmentAction>> = emptyList(),
) : UiStore<SearchFragmentState, SearchFragmentAction>(
    initialState = initialState,
    reducer = ::searchStateReducer,
    middleware = middleware,
) {
    init {
        dispatch(Init)
    }

    /**
     * The current environment of the search UX allowing access to various
     * other application features that this integrates with.
     *
     * This is Activity/Fragment lifecycle dependent and should be handled carefully to avoid memory leaks.
     *
     * @property context Activity [Context] used for various system interactions.
     * @property viewLifecycleOwner [LifecycleOwner] depending on which lifecycle related operations will be scheduled.
     * @property browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
     * @property navController [NavController] used to navigate to other destinations.
     */
    data class Environment(
        val context: Context,
        val viewLifecycleOwner: LifecycleOwner,
        val browsingModeManager: BrowsingModeManager,
        val navController: NavController,
    )
}

/**
 * Wraps a `SearchEngine` to give consumers the context that it was selected as a shortcut
 */
sealed class SearchEngineSource {
    abstract val searchEngine: SearchEngine?

    /**
     * No search engine
     */
    object None : SearchEngineSource() {
        override val searchEngine: SearchEngine? = null
    }

    /**
     * Search engine set as default
     */
    data class Default(override val searchEngine: SearchEngine) : SearchEngineSource()

    /**
     * Search engine for quick search
     * This is for any search engine that is not the user selected default.
     */
    data class Shortcut(override val searchEngine: SearchEngine) : SearchEngineSource()

    /**
     * Search engine for history
     */
    data class History(override val searchEngine: SearchEngine) : SearchEngineSource()

    /**
     * Search engine for bookmarks
     */
    data class Bookmarks(override val searchEngine: SearchEngine) : SearchEngineSource()

    /**
     * Search engine for tabs
     */
    data class Tabs(override val searchEngine: SearchEngine) : SearchEngineSource()
}

/**
 * The state for the Search Screen
 *
 * @property query The current search query string.
 * @property url The current URL of the tab (if this fragment is shown for an already existing tab).
 * @property searchTerms The search terms used to search previously in this tab (if this fragment is shown
 * for an already existing tab).
 * @property searchEngineSource The current selected search engine with the context of how it was selected.
 * @property defaultEngine The current default search engine (or null if none is available yet).
 * @property searchSuggestionsProviders The list of search suggestions providers that the user can choose from.
 * @property searchSuggestionsOrientedAtBottom Whether or not the search suggestions should be oriented at the
 * bottom of the screen.
 * @property searchStartedForCurrentUrl Whether or not the search started with editing the current URL.
 * @property shouldShowSearchSuggestions Whether or not to show search suggestions in the AwesomeBar.
 * @property showSearchSuggestions Whether or not to show search suggestions from the search engine in the AwesomeBar.
 * @property showSearchSuggestionsHint Whether or not to show search suggestions in private hint panel.
 * @property showSearchShortcuts Whether or not to show search shortcuts in the AwesomeBar.
 * @property areShortcutsAvailable Whether or not there are >=2 search engines installed
 * so to know to present users with certain options or not.
 * @property showSearchShortcutsSetting Whether the setting for showing search shortcuts is enabled
 * or disabled.
 * @property showClipboardSuggestions Whether or not to show clipboard suggestion in the AwesomeBar.
 * @property showSearchTermHistory Whether or not to show suggestions based on the previously used search terms
 * with the currently selected search engine.
 * @property showHistorySuggestionsForCurrentEngine Whether or not to show history suggestions for only
 * the current search engine.
 * @property showAllHistorySuggestions Whether or not to show history suggestions in the AwesomeBar.
 * @property showBookmarksSuggestionsForCurrentEngine Whether or not to show bookmarks suggestions for only
 * the current search engine.
 * @property showAllBookmarkSuggestions Whether or not to show the bookmark suggestion in the AwesomeBar.
 * @property showSyncedTabsSuggestionsForCurrentEngine Whether or not to show synced tabs suggestions for only
 * the current search engine.
 * @property showAllSyncedTabsSuggestions Whether or not to show the synced tabs suggestion in the AwesomeBar.
 * @property showSessionSuggestionsForCurrentEngine Whether or not to show local tabs suggestions for only
 * the current search engine.
 * @property showAllSessionSuggestions Whether or not to show the session suggestion in the AwesomeBar.
 * @property showSponsoredSuggestions Whether or not to show sponsored Firefox Suggest search suggestions in the
 * AwesomeBar. Always `false` in private mode, or when a non-default engine is selected.
 * @property showNonSponsoredSuggestions Whether or not to show Firefox Suggest search suggestions for web content
 * in the AwesomeBar. Always `false` in private mode, or when a non-default engine is selected.
 * @property showTrendingSearches Whether the setting for showing trending searches is enabled or disabled.
 * @property showRecentSearches Whether the setting for showing recent searches is enabled or disabled.
 * @property showShortcutsSuggestions Whether the setting for showing shortcuts suggestions is enabled or disabled.
 * @property showQrButton Whether or not to show the QR button.
 * @property tabId The ID of the current tab.
 * @property pastedText The text pasted from the long press toolbar menu.
 * @property searchAccessPoint The source of the performed search.
 * @property clipboardHasUrl Indicates if the clipboard contains an URL.
 */
data class SearchFragmentState(
    val query: String,
    val url: String,
    val searchTerms: String,
    val searchEngineSource: SearchEngineSource,
    val defaultEngine: SearchEngine?,
    val searchSuggestionsProviders: List<SuggestionProvider>,
    val searchSuggestionsOrientedAtBottom: Boolean,
    val searchStartedForCurrentUrl: Boolean,
    val shouldShowSearchSuggestions: Boolean,
    val showSearchSuggestions: Boolean,
    val showSearchSuggestionsHint: Boolean,
    val showSearchShortcuts: Boolean,
    val areShortcutsAvailable: Boolean,
    val showSearchShortcutsSetting: Boolean,
    val showClipboardSuggestions: Boolean,
    val showSearchTermHistory: Boolean,
    val showHistorySuggestionsForCurrentEngine: Boolean,
    val showAllHistorySuggestions: Boolean,
    val showBookmarksSuggestionsForCurrentEngine: Boolean,
    val showAllBookmarkSuggestions: Boolean,
    val showSyncedTabsSuggestionsForCurrentEngine: Boolean,
    val showAllSyncedTabsSuggestions: Boolean,
    val showSessionSuggestionsForCurrentEngine: Boolean,
    val showAllSessionSuggestions: Boolean,
    val showSponsoredSuggestions: Boolean,
    val showNonSponsoredSuggestions: Boolean,
    val showTrendingSearches: Boolean,
    val showRecentSearches: Boolean,
    val showShortcutsSuggestions: Boolean,
    val showQrButton: Boolean,
    val tabId: String?,
    val pastedText: String? = null,
    val searchAccessPoint: MetricsUtils.Source,
    val clipboardHasUrl: Boolean = false,
) : State {
    /**
     * Static functionality of [SearchFragmentState].
     */
    companion object {
        /**
         * Default empty [SearchFragmentState].
         */
        val EMPTY = SearchFragmentState(
            query = "",
            url = "",
            searchTerms = "",
            searchEngineSource = SearchEngineSource.None,
            defaultEngine = null,
            searchSuggestionsProviders = emptyList(),
            searchSuggestionsOrientedAtBottom = false,
            searchStartedForCurrentUrl = false,
            shouldShowSearchSuggestions = false,
            showSearchSuggestions = false,
            showSearchSuggestionsHint = false,
            showSearchShortcuts = false,
            areShortcutsAvailable = false,
            showSearchShortcutsSetting = false,
            showClipboardSuggestions = false,
            showSearchTermHistory = false,
            showHistorySuggestionsForCurrentEngine = false,
            showAllHistorySuggestions = false,
            showBookmarksSuggestionsForCurrentEngine = false,
            showAllBookmarkSuggestions = false,
            showSyncedTabsSuggestionsForCurrentEngine = false,
            showAllSyncedTabsSuggestions = false,
            showSessionSuggestionsForCurrentEngine = false,
            showAllSessionSuggestions = false,
            showSponsoredSuggestions = false,
            showNonSponsoredSuggestions = false,
            showTrendingSearches = false,
            showRecentSearches = false,
            showShortcutsSuggestions = false,
            showQrButton = false,
            tabId = null,
            pastedText = null,
            searchAccessPoint = MetricsUtils.Source.NONE,
            clipboardHasUrl = false,
        )
    }
}

/**
 * Creates the initial state for the search fragment.
 */
fun createInitialSearchFragmentState(
    activity: HomeActivity,
    components: Components,
    tabId: String?,
    pastedText: String?,
    searchAccessPoint: MetricsUtils.Source,
    searchEngine: SearchEngine? = null,
    isAndroidAutomotiveAvailable: Boolean = activity.isAndroidAutomotiveAvailable(),
): SearchFragmentState {
    val settings = components.settings
    val tab = tabId?.let { components.core.store.state.findTab(it) }
    val url = tab?.content?.url.orEmpty()

    val searchEngineSource = if (searchEngine != null) {
        SearchEngineSource.Shortcut(searchEngine)
    } else {
        SearchEngineSource.None
    }

    return SearchFragmentState(
        query = url,
        url = url,
        searchTerms = tab?.content?.searchTerms.orEmpty(),
        searchEngineSource = searchEngineSource,
        searchSuggestionsProviders = emptyList(),
        searchSuggestionsOrientedAtBottom = false,
        searchStartedForCurrentUrl = false,
        shouldShowSearchSuggestions = false,
        defaultEngine = null,
        showSearchSuggestions = shouldShowSearchSuggestions(
            browsingMode = activity.browsingModeManager.mode,
            settings = settings,
        ),
        showSearchSuggestionsHint = false,
        showSearchShortcuts = false,
        areShortcutsAvailable = false,
        showSearchShortcutsSetting = settings.shouldShowSearchShortcuts,
        showClipboardSuggestions = settings.shouldShowClipboardSuggestions,
        showSearchTermHistory = settings.showUnifiedSearchFeature && settings.shouldShowHistorySuggestions,
        showHistorySuggestionsForCurrentEngine = false,
        showAllHistorySuggestions = settings.shouldShowHistorySuggestions,
        showBookmarksSuggestionsForCurrentEngine = false,
        showAllBookmarkSuggestions = settings.shouldShowBookmarkSuggestions,
        showSyncedTabsSuggestionsForCurrentEngine = false,
        showAllSyncedTabsSuggestions = settings.shouldShowSyncedTabsSuggestions,
        showSessionSuggestionsForCurrentEngine = false,
        showAllSessionSuggestions = true,
        showSponsoredSuggestions = activity.browsingModeManager.mode == BrowsingMode.Normal &&
            settings.enableFxSuggest && settings.showSponsoredSuggestions,
        showNonSponsoredSuggestions = activity.browsingModeManager.mode == BrowsingMode.Normal &&
            settings.enableFxSuggest && settings.showNonSponsoredSuggestions,
        showTrendingSearches = shouldShowTrendingSearchSuggestions(
            browsingMode = activity.browsingModeManager.mode,
            settings = settings,
            isTrendingSuggestionSupported =
            components.core.store.state.search.selectedOrDefaultSearchEngine?.trendingUrl != null,
        ),
        showRecentSearches = settings.shouldShowRecentSearchSuggestions,
        showShortcutsSuggestions = settings.shouldShowShortcutSuggestions,
        showQrButton = !isAndroidAutomotiveAvailable,
        tabId = tabId,
        pastedText = pastedText,
        searchAccessPoint = searchAccessPoint,
    )
}

/**
 * Actions to dispatch through the `SearchStore` to modify `SearchState` through the reducer.
 */
sealed class SearchFragmentAction : Action {
    /**
     * Automated action for when the [SearchFragmentStore] is created to trigger all needed setup.
     */
    data object Init : SearchFragmentAction()

    /**
     * Action for when a new search is started.
     *
     * @property selectedSearchEngine The user selected search engine to use for the new search
     * or `null` if the default search engine should be used.
     * @property isUserSelected Whether or not the search engine was selected by the user.
     * @property inPrivateMode Whether or not the search is started in private browsing mode.
     * @property searchStartedForCurrentUrl Whether or not the search started with editing the current URL.
     */
    data class SearchStarted(
        val selectedSearchEngine: SearchEngine?,
        val isUserSelected: Boolean,
        val inPrivateMode: Boolean,
        val searchStartedForCurrentUrl: Boolean,
    ) : SearchFragmentAction()

    /**
     * Action for when search suggestions should be visible or not.
     *
     * @property visible Whether or not the search suggestions should be visible.
     */
    data class SearchSuggestionsVisibilityUpdated(
        val visible: Boolean,
    ) : SearchFragmentAction()

    /**
     * Action to update the search suggestions providers.
     *
     * @property providers The new search suggestions providers.
     */
    data class SearchProvidersUpdated(
        val providers: List<SuggestionProvider>,
    ) : SearchFragmentAction()

    /**
     * Action to enable or disable search suggestions.
     */
    data class SetShowSearchSuggestions(val show: Boolean) : SearchFragmentAction()

    /**
     * All actions through which a new search engine is selected
     */
    sealed class SearchEnginesSelectedActions : SearchFragmentAction()

    /**
     * Action when default search engine is selected.
     */
    data class SearchDefaultEngineSelected(
        val engine: SearchEngine,
        val browsingMode: BrowsingMode,
        val settings: Settings,
    ) : SearchEnginesSelectedActions()

    /**
     * Action when shortcut search engine is selected.
     */
    data class SearchShortcutEngineSelected(
        val engine: SearchEngine,
        val browsingMode: BrowsingMode,
        val settings: Settings,
    ) : SearchEnginesSelectedActions()

    /**
     * Action when history search engine is selected.
     */
    data class SearchHistoryEngineSelected(val engine: SearchEngine) : SearchEnginesSelectedActions()

    /**
     * Action when bookmarks search engine is selected.
     */
    data class SearchBookmarksEngineSelected(val engine: SearchEngine) : SearchEnginesSelectedActions()

    /**
     * Action when tabs search engine is selected.
     */
    data class SearchTabsEngineSelected(val engine: SearchEngine) : SearchEnginesSelectedActions()

    /**
     * Action when allow search suggestion in private mode hint is tapped.
     */
    data class AllowSearchSuggestionsInPrivateModePrompt(val show: Boolean) : SearchFragmentAction()

    /**
     * Action when query is updated.
     */
    data class UpdateQuery(val query: String) : SearchFragmentAction()

    /**
     * Action when updating clipboard URL.
     */
    data class UpdateClipboardHasUrl(val hasUrl: Boolean) : SearchFragmentAction()

    /**
     * Updates the local `SearchFragmentState` from the global `SearchState` in `BrowserStore`.
     * If the unified search is enabled, then search shortcuts should not be shown.
     */
    data class UpdateSearchState(val search: SearchState, val isUnifiedSearchEnabled: Boolean) : SearchFragmentAction()

    /**
     * Action indicating a suggestion was clicked.
     */
    data class SuggestionClicked(
        val suggestion: Suggestion,
    ) : SearchFragmentAction()

    /**
     * Action indicating a suggestion was selected for being edited before loading it.
     */
    data class SuggestionSelected(
        val suggestion: Suggestion,
    ) : SearchFragmentAction()

    /**
     * Signals a new valid [Environment] has been set.
     */
    data class EnvironmentRehydrated(val environment: Environment) : SearchFragmentAction()

    /**
     * Signals the current [Environment] is not valid anymore.
     */
    data object EnvironmentCleared : SearchFragmentAction()

    /**
     * Action indicating the user allowed to show suggestions in private mode.
     */
    data object PrivateSuggestionsCardAccepted : SearchFragmentAction()
}

/**
 * The SearchState Reducer.
 */
@Suppress("LongMethod")
private fun searchStateReducer(state: SearchFragmentState, action: SearchFragmentAction): SearchFragmentState {
    return when (action) {
        is Init -> {
            // no-op. Expected to be handled in middlewares.
            state
        }

        is SearchFragmentAction.SearchDefaultEngineSelected ->
            state.copy(
                searchEngineSource = SearchEngineSource.Default(action.engine),
                showSearchSuggestions = shouldShowSearchSuggestions(action.browsingMode, action.settings),
                showSearchShortcuts = action.settings.shouldShowSearchShortcuts &&
                    !action.settings.showUnifiedSearchFeature,
                showClipboardSuggestions = action.settings.shouldShowClipboardSuggestions,
                showSearchTermHistory = action.settings.showUnifiedSearchFeature &&
                    action.settings.shouldShowHistorySuggestions,
                showHistorySuggestionsForCurrentEngine = false, // we'll show all history
                showAllHistorySuggestions = action.settings.shouldShowHistorySuggestions,
                showBookmarksSuggestionsForCurrentEngine = false, // we'll show all bookmarks
                showAllBookmarkSuggestions = action.settings.shouldShowBookmarkSuggestions,
                showSyncedTabsSuggestionsForCurrentEngine = false, // we'll show all synced tabs
                showAllSyncedTabsSuggestions = action.settings.shouldShowSyncedTabsSuggestions,
                showSessionSuggestionsForCurrentEngine = false, // we'll show all local tabs
                showSponsoredSuggestions = action.browsingMode == BrowsingMode.Normal &&
                    action.settings.enableFxSuggest && action.settings.showSponsoredSuggestions,
                showNonSponsoredSuggestions = action.browsingMode == BrowsingMode.Normal &&
                    action.settings.enableFxSuggest && action.settings.showNonSponsoredSuggestions,
                showAllSessionSuggestions = true,
                showTrendingSearches = shouldShowTrendingSearchSuggestions(
                    browsingMode = action.browsingMode,
                    settings = action.settings,
                    isTrendingSuggestionSupported = action.engine.trendingUrl != null,
                ),
                showRecentSearches = action.settings.shouldShowRecentSearchSuggestions,
                showShortcutsSuggestions = action.settings.shouldShowShortcutSuggestions,
            )
        is SearchFragmentAction.SearchShortcutEngineSelected ->
            state.copy(
                searchEngineSource = SearchEngineSource.Shortcut(action.engine),
                showSearchSuggestions = shouldShowSearchSuggestions(action.browsingMode, action.settings),
                showSearchShortcuts = when (action.settings.showUnifiedSearchFeature) {
                    true -> false
                    false -> action.settings.shouldShowSearchShortcuts
                },
                showClipboardSuggestions = action.settings.shouldShowClipboardSuggestions,
                showSearchTermHistory = action.settings.showUnifiedSearchFeature &&
                    action.settings.shouldShowHistorySuggestions,
                showHistorySuggestionsForCurrentEngine = action.settings.showUnifiedSearchFeature &&
                    action.settings.shouldShowHistorySuggestions && !action.engine.isGeneral,
                showAllHistorySuggestions = when (action.settings.showUnifiedSearchFeature) {
                    true -> false
                    false -> action.settings.shouldShowHistorySuggestions
                },
                showBookmarksSuggestionsForCurrentEngine = action.settings.showUnifiedSearchFeature &&
                    action.settings.shouldShowBookmarkSuggestions && !action.engine.isGeneral,
                showAllBookmarkSuggestions = when (action.settings.showUnifiedSearchFeature) {
                    true -> false
                    false -> action.settings.shouldShowBookmarkSuggestions
                },
                showSyncedTabsSuggestionsForCurrentEngine = action.settings.showUnifiedSearchFeature &&
                    action.settings.shouldShowSyncedTabsSuggestions && !action.engine.isGeneral,
                showAllSyncedTabsSuggestions = when (action.settings.showUnifiedSearchFeature) {
                    true -> false
                    false -> action.settings.shouldShowSyncedTabsSuggestions
                },
                showSessionSuggestionsForCurrentEngine = action.settings.showUnifiedSearchFeature &&
                    !action.engine.isGeneral,
                showAllSessionSuggestions = when (action.settings.showUnifiedSearchFeature) {
                    true -> false
                    false -> true
                },
                showSponsoredSuggestions = false,
                showNonSponsoredSuggestions = false,
                showTrendingSearches = shouldShowTrendingSearchSuggestions(
                    browsingMode = action.browsingMode,
                    settings = action.settings,
                    isTrendingSuggestionSupported = action.engine.trendingUrl != null,
                ),
                showRecentSearches = action.settings.shouldShowRecentSearchSuggestions,
                showShortcutsSuggestions = action.settings.shouldShowShortcutSuggestions,
            )
        is SearchFragmentAction.SearchHistoryEngineSelected ->
            state.copy(
                searchEngineSource = SearchEngineSource.History(action.engine),
                showSearchSuggestions = false,
                showSearchShortcuts = false,
                showClipboardSuggestions = false,
                showSearchTermHistory = false,
                showHistorySuggestionsForCurrentEngine = false,
                showAllHistorySuggestions = true,
                showBookmarksSuggestionsForCurrentEngine = false,
                showAllBookmarkSuggestions = false,
                showSyncedTabsSuggestionsForCurrentEngine = false,
                showAllSyncedTabsSuggestions = false,
                showSessionSuggestionsForCurrentEngine = false,
                showAllSessionSuggestions = false,
                showSponsoredSuggestions = false,
                showNonSponsoredSuggestions = false,
                showTrendingSearches = false,
                showRecentSearches = false,
                showShortcutsSuggestions = false,
            )
        is SearchFragmentAction.SearchBookmarksEngineSelected ->
            state.copy(
                searchEngineSource = SearchEngineSource.Bookmarks(action.engine),
                showSearchSuggestions = false,
                showSearchShortcuts = false,
                showClipboardSuggestions = false,
                showSearchTermHistory = false,
                showHistorySuggestionsForCurrentEngine = false,
                showAllHistorySuggestions = false,
                showBookmarksSuggestionsForCurrentEngine = false,
                showAllBookmarkSuggestions = true,
                showSyncedTabsSuggestionsForCurrentEngine = false,
                showAllSyncedTabsSuggestions = false,
                showSessionSuggestionsForCurrentEngine = false,
                showAllSessionSuggestions = false,
                showSponsoredSuggestions = false,
                showNonSponsoredSuggestions = false,
                showTrendingSearches = false,
                showRecentSearches = false,
                showShortcutsSuggestions = false,
            )
        is SearchFragmentAction.SearchTabsEngineSelected ->
            state.copy(
                searchEngineSource = SearchEngineSource.Tabs(action.engine),
                showSearchSuggestions = false,
                showSearchShortcuts = false,
                showClipboardSuggestions = false,
                showSearchTermHistory = false,
                showHistorySuggestionsForCurrentEngine = false,
                showAllHistorySuggestions = false,
                showBookmarksSuggestionsForCurrentEngine = false,
                showAllBookmarkSuggestions = false,
                showSyncedTabsSuggestionsForCurrentEngine = false,
                showAllSyncedTabsSuggestions = true,
                showSessionSuggestionsForCurrentEngine = false,
                showAllSessionSuggestions = true,
                showSponsoredSuggestions = false,
                showNonSponsoredSuggestions = false,
                showTrendingSearches = false,
                showRecentSearches = false,
                showShortcutsSuggestions = false,
            )
        is SearchFragmentAction.UpdateQuery ->
            state.copy(query = action.query)
        is SearchFragmentAction.AllowSearchSuggestionsInPrivateModePrompt ->
            state.copy(showSearchSuggestionsHint = action.show)
        is SearchFragmentAction.SetShowSearchSuggestions ->
            state.copy(showSearchSuggestions = action.show)
        is SearchFragmentAction.UpdateSearchState -> {
            state.copy(
                defaultEngine = action.search.selectedOrDefaultSearchEngine,
                areShortcutsAvailable = action.search.searchEngines.size > 1,
                showSearchShortcuts = !action.isUnifiedSearchEnabled &&
                    state.url.isEmpty() &&
                    state.showSearchShortcutsSetting &&
                    action.search.searchEngines.size > 1,
                searchEngineSource = when (state.searchEngineSource) {
                    is SearchEngineSource.Default, is SearchEngineSource.None -> {
                        action.search.selectedOrDefaultSearchEngine?.let { SearchEngineSource.Default(it) }
                            ?: SearchEngineSource.None
                    }
                    else -> {
                        state.searchEngineSource
                    }
                },
            )
        }
        is SearchFragmentAction.UpdateClipboardHasUrl -> {
            state.copy(
                clipboardHasUrl = action.hasUrl,
            )
        }

        is SearchFragmentAction.SearchProvidersUpdated -> {
            state.copy(
                searchSuggestionsProviders = action.providers,
            )
        }

        is SearchFragmentAction.SearchSuggestionsVisibilityUpdated -> {
            state.copy(shouldShowSearchSuggestions = action.visible)
        }

        is SearchFragmentAction.SearchStarted -> {
            state.copy(searchStartedForCurrentUrl = action.searchStartedForCurrentUrl)
        }

        is SearchFragmentAction.EnvironmentRehydrated,
        is SearchFragmentAction.EnvironmentCleared,
        is SearchFragmentAction.SuggestionClicked,
        is SearchFragmentAction.PrivateSuggestionsCardAccepted,
        is SearchFragmentAction.SuggestionSelected,
            -> {
            // no-op. Expected to be handled in middlewares.
            state
        }
    }
}

/**
 * Check whether search suggestions should be shown in the AwesomeBar.
 *
 * @param browsingMode Current browsing mode: normal or private.
 * @param settings Persistence layer containing user option's for showing search suggestions.
 *
 * @return `true` if search suggestions should be shown `false` otherwise.
 */
@VisibleForTesting
internal fun shouldShowSearchSuggestions(
    browsingMode: BrowsingMode,
    settings: Settings,
) = when (browsingMode) {
    BrowsingMode.Normal -> settings.shouldShowSearchSuggestions
    BrowsingMode.Private ->
        settings.shouldShowSearchSuggestions && settings.shouldShowSearchSuggestionsInPrivate
}

/**
 * Returns true if trending search suggestions should be shown to the user.
 *
 * @param browsingMode Current browsing mode: normal or private.
 * @param settings Persistence layer containing user option's for showing search suggestions.
 * @param isTrendingSuggestionSupported is true when trending URL is available.
 */
@VisibleForTesting
internal fun shouldShowTrendingSearchSuggestions(
    browsingMode: BrowsingMode,
    settings: Settings,
    isTrendingSuggestionSupported: Boolean,
) =
    settings.trendingSearchSuggestionsEnabled && settings.isTrendingSearchesVisible &&
        isTrendingSuggestionSupported && shouldShowSearchSuggestions(browsingMode, settings)
