/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.os.Build
import android.os.PersistableBundle
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.logins.LoginsApiException
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import org.mozilla.fenix.settings.SupportUtils

/**
 * A middleware for handling side-effects in response to [LoginsAction]s.
 *
 * @param loginsStorage Storage layer for reading and writing logins.
 * @param getNavController Fetch the NavController for navigating within the local Composable nav graph.
 * @param exitLogins Invoked when back is clicked while the navController's backstack is empty.
 * @param persistLoginsSortOrder Invoked to persist the new sorting order for logins.
 * @param openTab Invoked when opening a tab when a login url is clicked.
 * @param ioDispatcher Coroutine dispatcher for IO operations.
 * @param clipboardManager For copying logins URLs.
 * @param refreshLoginsList Invoked to refresh the logins list.
 */
@Suppress("LongParameterList")
internal class LoginsMiddleware(
    private val loginsStorage: LoginsStorage,
    private val getNavController: () -> NavController,
    private val exitLogins: () -> Unit,
    private val persistLoginsSortOrder: suspend (LoginsSortOrder) -> Unit,
    private val openTab: (url: String, openInNewTab: Boolean) -> Unit,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val clipboardManager: ClipboardManager?,
    private val refreshLoginsList: Store<LoginsState, LoginsAction>.() -> Unit = { dispatch(Init) },
) : Middleware<LoginsState, LoginsAction> {

    private val scope = CoroutineScope(ioDispatcher)

    @Suppress("LongMethod", "ComplexMethod")
    override fun invoke(
        context: MiddlewareContext<LoginsState, LoginsAction>,
        next: (LoginsAction) -> Unit,
        action: LoginsAction,
    ) {
        val preReductionState = context.state
        next(action)

        when (action) {
            Init -> {
                context.store.loadLoginsList()
            }
            is SearchLogins -> {
                context.store.loadLoginsList()
            }
            is LoginsListBackClicked -> {
                exitLogins()
            }
            is LoginClicked -> {
                getNavController().navigate(LoginsDestinations.LOGIN_DETAILS)
            }
            is DetailLoginMenuAction.EditLoginMenuItemClicked -> {
                getNavController().navigate(LoginsDestinations.EDIT_LOGIN)
            }
            is LoginDeletionDialogAction.DeleteTapped -> {
                scope.launch {
                    preReductionState.loginsLoginDetailState?.login?.guid?.let {
                        loginsStorage.delete(
                            it,
                        )
                    }
                    if (preReductionState.loginsLoginDetailState != null) {
                        withContext(Dispatchers.Main) {
                            getNavController().popBackStack()
                        }
                    }
                }
            }
            is LoginsListSortMenuAction -> scope.launch {
                persistLoginsSortOrder(context.store.state.sortOrder)
            }
            is LearnMoreAboutSync -> {
                openTab(
                    SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.SYNC_SETUP),
                    true,
                )
            }
            is DetailLoginAction.GoToSiteClicked -> {
                openTab(action.url, true)
            }
            is LoginsDetailBackClicked -> {
                context.store.handleLoginsDetailsBackPressed()
            }
            is DetailLoginAction.CopyUsernameClicked -> {
                handleUsernameClicked(action.username)
            }
            is DetailLoginAction.CopyPasswordClicked -> {
                handlePasswordClicked(action.password)
            }
            is AddLoginAction.InitAdd -> {
                getNavController().navigate(LoginsDestinations.ADD_LOGIN)
            }
            is AddLoginBackClicked -> {
                getNavController().navigate(LoginsDestinations.LIST)
            }
            is AddLoginAction.AddLoginSaveClicked -> {
                context.store.handleAddLogin()
            }
            is EditLoginBackClicked -> {
                getNavController().navigate(LoginsDestinations.LOGIN_DETAILS)
            }
            is EditLoginAction.SaveEditClicked -> {
                context.store.handleEditLogin(loginItem = action.login)
            }
            is LoginsLoaded,
            is EditLoginAction.UsernameChanged,
            is EditLoginAction.PasswordChanged,
            is EditLoginAction.PasswordVisibilityChanged,
            is AddLoginAction.HostChanged,
            is AddLoginAction.UsernameChanged,
            is AddLoginAction.PasswordChanged,
            is DetailLoginMenuAction.DeleteLoginMenuItemClicked,
            is LoginDeletionDialogAction.CancelTapped,
            is ViewDisposed,
            -> Unit
        }
    }

    private fun Store<LoginsState, LoginsAction>.loadLoginsList() = scope.launch {
        val loginItems = arrayListOf<LoginItem>()

        loginsStorage.list().forEach { login ->
            loginItems.add(
                LoginItem(
                    guid = login.guid,
                    url = login.origin,
                    username = login.username,
                    password = login.password,
                    timeLastUsed = login.timeLastUsed,
                ),
            )
        }

        dispatch(LoginsLoaded(loginItems))
    }

    private fun handleUsernameClicked(username: String) {
        val usernameClipData = ClipData.newPlainText(username, username)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            usernameClipData.apply {
                description.extras = PersistableBundle().apply {
                    putBoolean("android.content.extra.IS_SENSITIVE", false)
                }
            }
        }
        clipboardManager?.setPrimaryClip(usernameClipData)
    }

    private fun handlePasswordClicked(password: String) {
        val passwordClipData = ClipData.newPlainText(password, password)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            passwordClipData.apply {
                description.extras = PersistableBundle().apply {
                    putBoolean("android.content.extra.IS_SENSITIVE", true)
                }
            }
        }
        clipboardManager?.setPrimaryClip(passwordClipData)
    }

    private fun Store<LoginsState, LoginsAction>.handleAddLogin() =
        scope.launch {
            val host = state.loginsAddLoginState?.host ?: ""
            val newLoginToAdd = LoginEntry(
                origin = host,
                formActionOrigin = host,
                httpRealm = host,
                username = state.loginsAddLoginState?.username ?: "",
                password = state.loginsAddLoginState?.password ?: "",
            )

            try {
                val loginAdded = loginsStorage.add(newLoginToAdd)
                dispatch(
                    LoginClicked(
                        LoginItem(
                            guid = loginAdded.guid,
                            url = loginAdded.origin,
                            username = loginAdded.username,
                            password = loginAdded.password,
                        ),
                    ),
                )
            } catch (exception: LoginsApiException) {
                exception.printStackTrace()
            }
        }

    private fun Store<LoginsState, LoginsAction>.handleLoginsDetailsBackPressed() = scope.launch {
        refreshLoginsList()

        withContext(Dispatchers.Main) {
            getNavController().navigate(LoginsDestinations.LIST)
        }
    }

    private fun Store<LoginsState, LoginsAction>.handleEditLogin(loginItem: LoginItem) =
        scope.launch {
            val updatedLogin = LoginEntry(
                origin = loginItem.url,
                formActionOrigin = loginItem.url,
                httpRealm = loginItem.url,
                username = state.loginsEditLoginState?.newUsername ?: loginItem.username,
                password = state.loginsEditLoginState?.newPassword ?: loginItem.password,
            )

            try {
                val loginEdited = loginsStorage.update(loginItem.guid, updatedLogin)
                dispatch(
                    LoginClicked(
                        LoginItem(
                            guid = loginEdited.guid,
                            url = loginEdited.origin,
                            username = loginEdited.username,
                            password = loginEdited.password,
                        ),
                    ),
                )
            } catch (exception: LoginsApiException) {
                exception.printStackTrace()
            }
        }
}
