/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.awesomebar.provider

import android.graphics.Bitmap
import androidx.annotation.IntRange
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.storage.HistoryMetadata
import mozilla.components.feature.awesomebar.facts.emitSearchTermSuggestionClickedFact
import mozilla.components.feature.search.SearchUseCases.SearchUseCase
import mozilla.components.feature.search.ext.buildSearchUrl
import java.util.UUID

/**
 * Return 2 search term suggestions by default. Same as on desktop.
 */
private const val DEFAULT_SUGGESTION_LIMIT = 2

/**
 * A too big limit but which help ensure the SearchSuggestionProvider' suggestions which should be placed
 * below the ones from this provider will appear correctly.
 */
const val SEARCH_TERMS_MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT: Int = 1000

/**
 * Error message if clients are requesting for a too big number of suggestions.
 */
private const val MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT_REACHED =
    "Cannot show more than $SEARCH_TERMS_MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT suggestions."

/**
 * A [AwesomeBar.SuggestionProvider] implementation that will show past searches done with the
 * specified [searchEngine] allowing to easily redo a search or continue with a lightly modified search.
 *
 * @param historyStorage an instance of the [PlacesHistoryStorage] used
 * to query matching metadata records.
 * @param searchUseCase the use case invoked to do a new search with the suggested search term.
 * @param searchEngine the current search engine used for speculative connects with the first result.
 * @param maxNumberOfSuggestions optional parameter to specify the maximum number of returned suggestions.
 * Defaults to `2`.
 * @param icon optional [Bitmap] to he shown as the suggestions header.
 * Defaults to `null` in which case the [searchEngine]'s icon will be used.
 * @param engine optional [Engine] instance to call [Engine.speculativeConnect] for the
 * highest scored suggestion URL.
 * @param showEditSuggestion optional parameter to specify if the suggestion should show the edit button.
 * @param suggestionsHeader optional parameter to specify if the suggestion should have a header
 */
class SearchTermSuggestionsProvider(
    private val historyStorage: PlacesHistoryStorage,
    private val searchUseCase: SearchUseCase,
    private val searchEngine: SearchEngine?,
    @param:IntRange(from = 0, to = SEARCH_TERMS_MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT.toLong())
    private val maxNumberOfSuggestions: Int = DEFAULT_SUGGESTION_LIMIT,
    private val icon: Bitmap? = null,
    private val engine: Engine? = null,
    private val showEditSuggestion: Boolean = true,
    private val suggestionsHeader: String? = null,
) : AwesomeBar.SuggestionProvider {
    init {
        if (maxNumberOfSuggestions > SEARCH_TERMS_MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT) {
            throw IllegalArgumentException(MAXIMUM_ALLOWED_SUGGESTIONS_LIMIT_REACHED)
        }
    }

    override val id: String = UUID.randomUUID().toString()

    override fun groupTitle(): String? {
        return suggestionsHeader
    }

    override suspend fun onInputChanged(text: String): List<AwesomeBar.Suggestion> = coroutineScope {
        if (text.isBlank()) {
            return@coroutineScope emptyList()
        }

        historyStorage.cancelReads(text)
        val suggestions = withContext(this.coroutineContext) {
            historyStorage.getHistoryMetadataSince(Long.MIN_VALUE)
                .asSequence()
                .filter { it.totalViewTime > 0 }
                .filter { it.key.searchTerm?.startsWith(text) ?: false }
                .distinctBy { it.key.searchTerm }
                .sortedByDescending { it.createdAt }
                .take(maxNumberOfSuggestions)
                .toList()
        }

        searchEngine?.let {
            suggestions.firstOrNull()?.key?.searchTerm?.let { searchTerm ->
                engine?.speculativeConnect(it.buildSearchUrl(searchTerm))
            }
        }

        return@coroutineScope suggestions.into(
            provider = this@SearchTermSuggestionsProvider,
            searchEngine = searchEngine,
            icon = icon,
            searchUseCase = searchUseCase,
            showEditSuggestion = showEditSuggestion,
        )
    }
}

private fun Iterable<HistoryMetadata>.into(
    provider: AwesomeBar.SuggestionProvider,
    searchEngine: SearchEngine?,
    icon: Bitmap?,
    searchUseCase: SearchUseCase,
    showEditSuggestion: Boolean = true,
): List<AwesomeBar.Suggestion> {
    return this.mapIndexedNotNull { index, result ->
        val safeSearchTerm = result.key.searchTerm ?: return@mapIndexedNotNull null

        AwesomeBar.Suggestion(
            provider = provider,
            icon = icon ?: searchEngine?.icon,
            title = result.key.searchTerm,
            description = null,
            editSuggestion = if (showEditSuggestion) safeSearchTerm else null,
            // Reducing MAX_VALUE by 2: To allow SearchActionProvider to go above and
            // still have one additional spot above available.
            score = Int.MAX_VALUE - (index + 2),
            onSuggestionClicked = {
                searchUseCase.invoke(safeSearchTerm)
                emitSearchTermSuggestionClickedFact()
            },
        )
    }
}
