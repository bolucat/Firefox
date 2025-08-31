/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.middleware

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.compose.base.snackbar.SnackbarTimeout
import mozilla.components.feature.downloads.DownloadsUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIAction
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState

/**
 * Middleware for deleting a Download from disk.
 *
 * @param undoDelay The recommended time an "undo" action should be available for.
 * @param removeDownloadUseCase The [DownloadsUseCases.RemoveDownloadUseCase] used to remove the download.
 * @param dispatcher The injected dispatcher used to run suspending operations on.
 */
class DownloadDeleteMiddleware(
    private val undoDelay: Long = SnackbarTimeout.Action.value,
    private val removeDownloadUseCase: DownloadsUseCases.RemoveDownloadUseCase,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Main,
) : Middleware<DownloadUIState, DownloadUIAction> {

    private var lastDeleteOperation: DeleteOperation? = null

    /*
     * CoroutineScope used to launch the delete operation. This is a custom CoroutineScope with
     * an injected dispatcher, because the delete operations is short and should not be cancelled
     * when the UI is destroyed.
     */
    private val coroutineScope = CoroutineScope(dispatcher)

    override fun invoke(
        context: MiddlewareContext<DownloadUIState, DownloadUIAction>,
        next: (DownloadUIAction) -> Unit,
        action: DownloadUIAction,
    ) {
        next(action)
        when (action) {
            is DownloadUIAction.AddPendingDeletionSet ->
                startDelayedRemoval(context, action.itemIds, undoDelay)

            is DownloadUIAction.UndoPendingDeletion -> lastDeleteOperation?.cancel()
            else -> {
                // no - op
            }
        }
    }

    private fun startDelayedRemoval(
        context: MiddlewareContext<DownloadUIState, DownloadUIAction>,
        items: Set<String>,
        delay: Long,
    ) {
        val job = coroutineScope.launch {
            try {
                delay(delay)
                items.forEach { removeDownloadUseCase(it) }
                context.dispatch(DownloadUIAction.FileItemDeletedSuccessfully)
            } catch (e: CancellationException) {
                context.store.dispatch(DownloadUIAction.UndoPendingDeletionSet(items))
            } finally {
                // This avoids mistakenly clearing lastDeleteOperation if another job was started before
                // this one finished.
                if (lastDeleteOperation?.items == items) {
                    lastDeleteOperation = null
                }
            }
        }
        lastDeleteOperation = DeleteOperation(job, items)
    }

    private data class DeleteOperation(
        private val deleteJob: Job,
        val items: Set<String>,
    ) {
        fun cancel() {
            deleteJob.cancel(CancellationException("Undo deletion"))
        }
    }
}
