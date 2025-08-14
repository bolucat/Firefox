/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package mozilla.components.concept.engine.preferences

import mozilla.components.ExperimentalAndroidComponentsApi

private const val UNSUPPORTED_ERROR = "Browser preferences support is not available on this engine."

/**
 * Entry point for interacting with browser preferences.
 *
 * Caution: These functions should be used carefully with a clear understanding of how
 * they may interact with preexisting ways to get or set browser preferences.
 *
 * For example, a defined browser preference in [mozilla.components.concept.engine.Settings]
 * should *not* be interacted with using this API. Critical preferences should always be well
 * defined as [mozilla.components.concept.engine.Settings] options.
 */
interface BrowserPreferencesRuntime {

    /**
     * Method for registering the preference observer delegate on the runtime.
     *
     * @param prefObserverDelegate The delegate to register.
     */
    fun registerPrefObserverDelegate(prefObserverDelegate: BrowserPrefObserverDelegate): Unit =
        throw UnsupportedOperationException(UNSUPPORTED_ERROR)

    /**
     * Method for unregistering the preference observer delegate on the runtime.
     **/
    fun unregisterPrefObserverDelegate(): Unit =
        throw UnsupportedOperationException(UNSUPPORTED_ERROR)

    /**
     * Method for registering a specific browser preference for observation on the observer delegate.
     */
    fun registerPrefForObservation(
        pref: String,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Method for removing a specific browser preference for observation on the observer delegate.
     */
    fun unregisterPrefForObservation(
        pref: String, onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Gets value and basic information about a given pref.
     *
     * @param pref The preference to find information on.
     * @param onSuccess Callback invoked when the preference was valid and information could be
     * obtained on it.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun getBrowserPref(
        pref: String,
        onSuccess: (BrowserPreference<*>) -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Gets value and basic information about a list of pref.
     *
     * @param prefs The list of preferences to find information on.
     * @param onSuccess Callback invoked when the preference was valid and information could be
     * obtained on it.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun getBrowserPrefs(
        prefs: List<String>,
        onSuccess: (List<BrowserPreference<*>>) -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Set a browser preference to a given value on a given branch.
     *
     * @param pref The preference to set.
     * @param value The value to set the preference to.
     * @param branch Selecting [Branch.USER] will change the user's active preference value. Selecting
     * [Branch.DEFAULT] will change the default for the preference. If no user preference is
     * stated, then in may become the active preference value.
     * @param onSuccess Callback invoked when the preference sets.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun setBrowserPref(
        pref: String,
        value: String,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Set a browser preference to a given value on a given branch.
     *
     * @param pref The preference to set.
     * @param value The value to set the preference to.
     * @param branch Selecting [Branch.USER] will change the user's active preference value. Selecting
     * [Branch.DEFAULT] will change the default for the preference. If no user preference is
     * stated, then in may become the active preference value.
     * @param onSuccess Callback invoked when the preference sets.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun setBrowserPref(
        pref: String,
        value: Boolean,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Set a browser preference to a given value on a given branch.
     *
     * @param pref The preference to set.
     * @param value The value to set the preference to.
     * @param branch Selecting [Branch.USER] will change the user's active preference value. Selecting
     * [Branch.DEFAULT] will change the default for the preference. If no user preference is
     * stated, then in may become the active preference value.
     * @param onSuccess Callback invoked when the preference sets.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun setBrowserPref(
        pref: String,
        value: Int,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * Set a list of browser preferences to a given value on a given branch.
     *
     * @param prefs List of prefs and values to set.
     * @param onSuccess Callback invoked when the preference sets.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun setBrowserPrefs(
        prefs: List<SetBrowserPreference<*>>,
        onSuccess: (Map<String, Boolean>) -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))

    /**
     * This will clear a user preferences value.
     * This will, in effect, reset the value to the default value.
     * If no default value exists the preference will cease to exist.
     *
     * @param pref The user preference to clear.
     * @param onSuccess Callback invoked when the preference was successfully cleared.
     * @param onError Callback invoked when an issue occurs.
     */
    @ExperimentalAndroidComponentsApi
    fun clearBrowserUserPref(
        pref: String,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ): Unit = onError(UnsupportedOperationException(UNSUPPORTED_ERROR))
}
