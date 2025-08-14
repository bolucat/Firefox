/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.ext.flow

/**
 * [BrowserToolbarStore] middleware that will synchronize bookmarks searches being ended
 * when the toolbar exits search mode.
 */
internal class BrowserToolbarSyncToBookmarksMiddleware(
    private val toolbarStore: BrowserToolbarStore,
    private val scope: CoroutineScope,
) : Middleware<BookmarksState, BookmarksAction> {
    override fun invoke(
        context: MiddlewareContext<BookmarksState, BookmarksAction>,
        next: (BookmarksAction) -> Unit,
        action: BookmarksAction,
    ) {
        next(action)

        if (action is Init) {
            toolbarStore.flow()
                .map { it.isEditMode() }
                .onEach { isInEditMode ->
                    if (context.state.isSearching && !isInEditMode) {
                        context.store.dispatch(SearchDismissed)
                    }
                }
                .launchIn(scope)
        }
    }
}
