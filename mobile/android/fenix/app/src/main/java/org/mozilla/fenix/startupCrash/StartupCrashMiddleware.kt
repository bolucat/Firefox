/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.startupCrash

import android.text.format.DateUtils
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.joinAll
import kotlinx.coroutines.launch
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.lib.crash.store.CrashReportCache
import mozilla.components.lib.crash.store.TimeInMillis
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import org.mozilla.fenix.crashes.SettingsCrashReportCache
import org.mozilla.fenix.crashes.StartupCrashCanary
import org.mozilla.fenix.utils.Settings

private const val FIVE_DAYS_IN_MILLIS = DateUtils.DAY_IN_MILLIS * 5

internal class StartupCrashMiddleware(
    settings: Settings,
    private val crashReporter: CrashReporter,
    private val reinitializeHandler: suspend () -> Unit,
    private val startupCrashCanaryCache: StartupCrashCanary,
    private val currentTimeInMillis: () -> TimeInMillis = { System.currentTimeMillis() },
    private val cache: CrashReportCache = SettingsCrashReportCache(settings),
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.IO),
) : Middleware<StartupCrashState, StartupCrashAction> {

    override fun invoke(
        context: MiddlewareContext<StartupCrashState, StartupCrashAction>,
        next: (StartupCrashAction) -> Unit,
        action: StartupCrashAction,
    ) {
        next(action)

        when (action) {
            ReportTapped -> {
                scope.launch {
                    sendUnsentCrashReports(context.store)
                    startupCrashCanaryCache.clearCanary()
                }
            }
            ReopenTapped -> scope.launch {
                reinitializeHandler()
            }
            NoTapped -> {
                scope.launch {
                    cache.setDeferredUntil(currentTimeInMillis() + FIVE_DAYS_IN_MILLIS)
                    startupCrashCanaryCache.clearCanary()
                }
            }
            CrashReportCompleted,
            -> {}
        }
    }

    private suspend fun sendUnsentCrashReports(store: Store<StartupCrashState, StartupCrashAction>) {
        crashReporter.unsentCrashReportsSince(cutoffDate()).map {
            crashReporter.submitReport(it)
        }.joinAll()
        store.dispatch(CrashReportCompleted)
    }

    private suspend fun cutoffDate(): TimeInMillis {
        return cache.getCutoffDate() ?: currentTimeInMillis().also {
            cache.setCutoffDate(it)
        }
    }
}
