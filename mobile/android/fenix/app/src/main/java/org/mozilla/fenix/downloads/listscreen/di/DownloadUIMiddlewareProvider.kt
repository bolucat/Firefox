/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.di

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import mozilla.components.lib.state.Middleware
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.downloads.listscreen.middleware.BroadcastSender
import org.mozilla.fenix.downloads.listscreen.middleware.DefaultBroadcastSender
import org.mozilla.fenix.downloads.listscreen.middleware.DefaultFileItemDescriptionProvider
import org.mozilla.fenix.downloads.listscreen.middleware.DownloadDeleteMiddleware
import org.mozilla.fenix.downloads.listscreen.middleware.DownloadTelemetryMiddleware
import org.mozilla.fenix.downloads.listscreen.middleware.DownloadUIMapperMiddleware
import org.mozilla.fenix.downloads.listscreen.middleware.DownloadUIShareMiddleware
import org.mozilla.fenix.downloads.listscreen.middleware.DownloadsServiceCommunicationMiddleware
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIAction
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.getUndoDelay

internal object DownloadUIMiddlewareProvider {

    @Volatile
    private var broadcastSender: BroadcastSender? = null

    internal fun provideMiddleware(
        coroutineScope: CoroutineScope,
        applicationContext: Context,
    ): List<Middleware<DownloadUIState, DownloadUIAction>> = listOf(
        provideUIMapperMiddleware(applicationContext, coroutineScope),
        provideShareMiddleware(applicationContext),
        provideTelemetryMiddleware(),
        provideDeleteMiddleware(applicationContext.getUndoDelay(), applicationContext.components),
        provideDownloadsServiceCommunicationMiddleware(applicationContext),
    )

    private fun provideDeleteMiddleware(undoDelay: Long, components: Components) =
        DownloadDeleteMiddleware(
            undoDelay = undoDelay,
            removeDownloadUseCase = components.useCases.downloadUseCases.removeDownload,
        )

    private fun provideShareMiddleware(applicationContext: Context) =
        DownloadUIShareMiddleware(applicationContext = applicationContext)

    private fun provideUIMapperMiddleware(
        applicationContext: Context,
        coroutineScope: CoroutineScope,
    ) = DownloadUIMapperMiddleware(
        browserStore = applicationContext.components.core.store,
        scope = coroutineScope,
        fileItemDescriptionProvider = DefaultFileItemDescriptionProvider(
            context = applicationContext,
            fileSizeFormatter = applicationContext.components.core.fileSizeFormatter,
            downloadEstimator = applicationContext.components.core.downloadEstimator,
        ),
    )

    private fun provideTelemetryMiddleware() = DownloadTelemetryMiddleware()

    private fun provideDownloadsServiceCommunicationMiddleware(applicationContext: Context) =
        DownloadsServiceCommunicationMiddleware(
           provideBroadcastSender(applicationContext),
        )

    private fun provideBroadcastSender(applicationContext: Context): BroadcastSender {
        initializeBroadcastSender(applicationContext)
        return requireNotNull(broadcastSender) {
            "BroadcastSender not initialized. Call initialize(applicationContext) first."
        }
    }

    private fun initializeBroadcastSender(applicationContext: Context) {
        if (broadcastSender == null) {
            synchronized(this) {
                if (broadcastSender == null) {
                    broadcastSender = DefaultBroadcastSender(applicationContext)
                }
            }
        }
    }
}
