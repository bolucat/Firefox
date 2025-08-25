/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.app.Activity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.ActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.fragment.app.FragmentActivity
import mozilla.components.lib.state.ext.observeAsState
import org.mozilla.fenix.R

/**
 *  The UI host for displaying the Biometric Authentication dialog
 *  @param store The [LoginsStore]
 */
@Composable
internal fun BiometricAuthenticationDialog(store: LoginsStore) {
    val state by store.observeAsState(store.state) { it }
    val context = LocalContext.current
    val activity = context as? FragmentActivity
    var authorizationStarted by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        if (state.biometricAuthenticationState != BiometricAuthenticationState.Authorized) {
            store.dispatch(BiometricAuthenticationAction.AuthenticationInProgress)
            authorizationStarted = true
        }
    }

    if (activity != null && authorizationStarted) {
        if (DefaultBiometricUtils.canUseBiometricAuthentication(activity = activity)) {
            ShowBiometricAuthenticationDialog(
                activity = activity,
                onAuthSuccess = {
                    store.dispatch(BiometricAuthenticationAction.AuthenticationSucceeded)
                    store.dispatch(BiometricAuthenticationDialogAction(false))
                },
                onAuthFailure = {
                    store.dispatch(BiometricAuthenticationAction.AuthenticationFailed)
                    store.dispatch(BiometricAuthenticationDialogAction(true))
                },
            )
        } else if (DefaultBiometricUtils.canUsePinVerification(activity = activity)) {
            ShowPinVerificationDialog(activity = activity, store = store)
        } else {
            ShowPinWarningDialog(
                activity = activity,
                onAuthSuccess = {
                    store.dispatch(BiometricAuthenticationAction.AuthenticationSucceeded)
                    store.dispatch(BiometricAuthenticationDialogAction(false))
                },
            )
        }
    }
}

@Composable
private fun ShowBiometricAuthenticationDialog(
    activity: FragmentActivity,
    onAuthSuccess: () -> Unit,
    onAuthFailure: () -> Unit,
) {
    DefaultBiometricUtils.showBiometricPromptForCompose(
        title = activity.resources.getString(R.string.logins_biometric_prompt_message_2),
        activity = activity,
        onAuthSuccess = onAuthSuccess,
        onAuthFailure = onAuthFailure,
    )
}

@Composable
private fun ShowPinVerificationDialog(activity: FragmentActivity, store: LoginsStore) {
    val startForResult =
        rememberLauncherForActivityResult(
            ActivityResultContracts.StartActivityForResult(),
        ) { result: ActivityResult ->
            if (result.resultCode == Activity.RESULT_OK) {
                store.dispatch(BiometricAuthenticationAction.AuthenticationSucceeded)
            }
        }
    val credentialIntent =
        DefaultBiometricUtils.getConfirmDeviceCredentialIntent(
            title = activity.resources.getString(R.string.logins_biometric_prompt_message_2),
            activity = activity,
        )
    if (credentialIntent != null) {
        SideEffect {
            startForResult.launch(credentialIntent)
        }
    }
}

@Composable
private fun ShowPinWarningDialog(activity: FragmentActivity, onAuthSuccess: () -> Unit) {
    DefaultBiometricUtils.showPinWarningPrompt(
        activity = activity,
        onAuthSuccess = onAuthSuccess,
    )
}
