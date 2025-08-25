/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import androidx.annotation.VisibleForTesting
import androidx.lifecycle.Lifecycle.State.RESUMED
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.Mode
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import org.mozilla.fenix.search.SearchFragmentAction.EnvironmentCleared
import org.mozilla.fenix.search.SearchFragmentAction.EnvironmentRehydrated
import org.mozilla.fenix.search.SearchFragmentAction.SearchStarted
import org.mozilla.fenix.search.SearchFragmentStore.Environment
import mozilla.components.lib.state.Action as MVIAction

/**
 * [SearchFragmentStore] [Middleware] to synchronize search related details from [BrowserToolbarStore].
 *
 * @param toolbarStore The [BrowserToolbarStore] to sync from.
 * @param browserStore The [BrowserStore] to sync from.
 */
class BrowserToolbarToFenixSearchMapperMiddleware(
    private val toolbarStore: BrowserToolbarStore,
    private val browserStore: BrowserStore? = null,
) : Middleware<SearchFragmentState, SearchFragmentAction> {
    @VisibleForTesting
    internal var environment: Environment? = null
    private var syncSearchStartedJob: Job? = null
    private var syncSearchQueryJob: Job? = null

    override fun invoke(
        context: MiddlewareContext<SearchFragmentState, SearchFragmentAction>,
        next: (SearchFragmentAction) -> Unit,
        action: SearchFragmentAction,
    ) {
        if (action is EnvironmentRehydrated) {
            environment = action.environment

            syncSearchStatus(context)

            if (toolbarStore.state.isEditMode()) {
                syncUserQuery(context)
            }
        } else if (action is EnvironmentCleared) {
            environment = null
        }

        next(action)
    }

    private fun syncSearchStatus(context: MiddlewareContext<SearchFragmentState, SearchFragmentAction>) {
        syncSearchStartedJob?.cancel()
        syncSearchStartedJob = toolbarStore.observeWhileActive {
            distinctUntilChangedBy { it.mode }
                .collect {
                    if (it.mode == Mode.EDIT) {
                        val editState = toolbarStore.state.editState
                        context.dispatch(
                            SearchStarted(
                                selectedSearchEngine = null,
                                isUserSelected = true,
                                inPrivateMode = environment?.browsingModeManager?.mode?.isPrivate == true,
                                searchStartedForCurrentUrl = editState.isQueryPrefilled &&
                                    browserStore?.state?.selectedTab?.content?.url == editState.query,
                            ),
                        )

                        syncUserQuery(context)
                    } else {
                        stopSyncingUserQuery()
                    }
                }
        }
    }

    private fun syncUserQuery(context: MiddlewareContext<SearchFragmentState, SearchFragmentAction>) {
        syncSearchQueryJob?.cancel()
        syncSearchQueryJob = toolbarStore.observeWhileActive {
            map { it.editState.query }
                .distinctUntilChanged()
                .collect { query ->
                    context.dispatch(
                        SearchFragmentAction.UpdateQuery(
                            when (toolbarStore.state.editState.isQueryPrefilled) {
                                true -> "" // consider a prefilled & preselected toolbar query as not entered by user
                                false -> query
                            },
                        ),
                    )
                }
        }
    }

    private fun stopSyncingUserQuery() {
        syncSearchQueryJob?.cancel()
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job? = environment?.viewLifecycleOwner?.run {
        lifecycleScope.launch {
            repeatOnLifecycle(RESUMED) {
                flow().observe()
            }
        }
    }
}
