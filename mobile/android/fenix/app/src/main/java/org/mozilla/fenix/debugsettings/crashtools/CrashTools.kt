/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.crashtools

import android.text.format.DateUtils
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.PrimaryButton
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.crashes.StartupCrashCanary
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings

private const val SECOND_IN_MILLISECOND = 1000L

@Composable
internal fun CrashTools(
    settings: Settings = components.settings,
    canary: StartupCrashCanary =
        StartupCrashCanary.build(LocalContext.current.applicationContext),
) {
    var now = System.currentTimeMillis()
    var genericDeferPeriod by remember { mutableLongStateOf(settings.crashReportDeferredUntil - now) }
    LaunchedEffect(Unit) {
        while (true) {
            now = System.currentTimeMillis()
            genericDeferPeriod = settings.crashReportDeferredUntil - now
            delay(SECOND_IN_MILLISECOND)
        }
    }
    Column(
        modifier = Modifier
            .padding(all = 16.dp)
            .verticalScroll(state = rememberScrollState()),
    ) {
        Text(
            text = stringResource(
                R.string.crash_debug_deferral_timer,
                convertMillisToDHMS(maxOf(genericDeferPeriod, 0)),
            ),
            style = FirefoxTheme.typography.body2,
        )
        PrimaryButton(
            text = stringResource(R.string.crash_debug_deferral_button),
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                settings.crashReportDeferredUntil = 0L
                genericDeferPeriod = 0L
            },
        )
        Text(
            text = stringResource(R.string.crash_debug_crash_app_warning),
            color = FirefoxTheme.colors.actionCritical,
            style = FirefoxTheme.typography.subtitle2,
        )
        PrimaryButton(
            text = stringResource(R.string.crash_debug_generic_crash_trigger),
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                throw ArithmeticException("Debug drawer triggered exception.")
            },
        )
        Text(
            text = stringResource(R.string.crash_debug_startup_crash_warning),
            color = FirefoxTheme.colors.actionCritical,
            style = FirefoxTheme.typography.subtitle2,
        )
        PrimaryButton(
            text = stringResource(R.string.crash_debug_crash_on_next_startup),
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                CoroutineScope(Dispatchers.IO).launch {
                    canary.createCanary()
                }.invokeOnCompletion {
                    throw ArithmeticException("Debug drawer triggered exception.")
                }
            },
        )
    }
}

internal fun convertMillisToDHMS(milliseconds: Long): String {
    return DateUtils.formatElapsedTime(milliseconds / SECOND_IN_MILLISECOND)
}

@FlexibleWindowLightDarkPreview
@Composable
private fun CrashToolsPreview() {
    FirefoxTheme {
        CrashTools(Settings(LocalContext.current))
    }
}
