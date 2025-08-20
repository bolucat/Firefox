/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.reducer

import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.SessionState

internal object EngineStateReducer {
    // Maximum number of recently killed tabs to retain in memory.
    // When this limit is exceeded, the oldest entry is removed to maintain a fixed size.
    private const val MAX_RECENTLY_KILLED_TABS = 50

    /**
     * [EngineAction] Reducer function for modifying a specific [EngineState]
     * of a [SessionState].
     */
    @Suppress("LongMethod")
    fun reduce(state: BrowserState, action: EngineAction): BrowserState {
        return when (action) {
            is EngineAction.LinkEngineSessionAction -> state.copyWithEngineState(action.tabId) {
                it.copy(
                    engineSession = action.engineSession,
                    timestamp = action.timestamp,
                )
            }
            is EngineAction.UnlinkEngineSessionAction -> state.copyWithEngineState(action.tabId) {
                it.copy(
                    engineSession = null,
                    engineObserver = null,
                )
            }
            is EngineAction.UpdateEngineSessionObserverAction -> state.copyWithEngineState(action.tabId) {
                it.copy(engineObserver = action.engineSessionObserver)
            }
            is EngineAction.UpdateEngineSessionStateAction -> state.copyWithEngineState(action.tabId) { engineState ->
                if (engineState.crashed) {
                    // We ignore state updates for a crashed engine session. We want to keep the last state until
                    // this tab gets restored (or closed).
                    engineState
                } else {
                    engineState.copy(engineSessionState = action.engineSessionState)
                }
            }
            is EngineAction.UpdateEngineSessionInitializingAction -> state.copyWithEngineState(action.tabId) {
                it.copy(initializing = action.initializing)
            }
            is EngineAction.OptimizedLoadUrlTriggeredAction -> {
                state
            }
            is EngineAction.SaveToPdfExceptionAction,
            is EngineAction.SaveToPdfCompleteAction,
            -> {
                throw IllegalStateException(
                    "You need to add a middleware to handle this action in your BrowserStore. ($action)",
                )
            }
            is EngineAction.SuspendEngineSessionAction,
            is EngineAction.LoadDataAction,
            is EngineAction.LoadUrlAction,
            is EngineAction.ReloadAction,
            is EngineAction.GoBackAction,
            is EngineAction.GoForwardAction,
            is EngineAction.GoToHistoryIndexAction,
            is EngineAction.ToggleDesktopModeAction,
            is EngineAction.ExitFullScreenModeAction,
            is EngineAction.SaveToPdfAction,
            is EngineAction.PrintContentAction,
            is EngineAction.PrintContentCompletedAction,
            is EngineAction.PrintContentExceptionAction,
            is EngineAction.ClearDataAction,
            is EngineAction.FlushEngineSessionStateAction,
            -> {
                throw IllegalStateException("You need to add EngineMiddleware to your BrowserStore. ($action)")
            }
            is EngineAction.PurgeHistoryAction -> {
                state.copy(
                    tabs = purgeEngineStates(state.tabs),
                    customTabs = purgeEngineStates(state.customTabs),
                )
            }
            is EngineAction.KillEngineSessionAction -> {
                val updatedKilledTabs = LinkedHashSet(state.recentlyKilledTabs)
                updatedKilledTabs.add(action.tabId)

                // Enforce max size of 50 recently killed tabs
                if (updatedKilledTabs.size > MAX_RECENTLY_KILLED_TABS) {
                    val oldestEntry = updatedKilledTabs.first()
                    updatedKilledTabs.remove(oldestEntry)
                }

                state.copy(recentlyKilledTabs = updatedKilledTabs)
            }
            is EngineAction.CreateEngineSessionAction -> {
                if (state.recentlyKilledTabs.isEmpty()) return state
                val updatedKilledTabs = LinkedHashSet(state.recentlyKilledTabs)
                updatedKilledTabs.remove(action.tabId)
                state.copy(recentlyKilledTabs = updatedKilledTabs)
            }
        }
    }
}

private inline fun BrowserState.copyWithEngineState(
    tabId: String,
    crossinline update: (EngineState) -> EngineState,
): BrowserState {
    return updateTabOrCustomTabState(tabId) { current ->
        current.createCopy(engineState = update(current.engineState))
    }
}

/**
 * When `PurgeHistoryAction` gets dispatched `EngineDelegateMiddleware` will take care of calling
 * `purgeHistory()` on all `EngineSession` instances. However some tabs may not have an `EngineSession`
 * assigned (yet), instead we keep track of the `EngineSessionState` to restore when needed. Creating
 * an `EngineSession` for every tab, just to call `purgeHistory()` on them, is wasteful and may cause
 * problems if there are a lot of tabs. So instead we just remove the EngineSessionState from those
 * sessions. The next time they get rendered we will only load the assigned URL and since they have
 * no state to restore, they will have no history.
 */
@Suppress("UNCHECKED_CAST")
private fun <T : SessionState> purgeEngineStates(tabs: List<T>): List<T> {
    return tabs.map { session ->
        if (session.engineState.engineSession == null && session.engineState.engineSessionState != null) {
            session.createCopy(engineState = session.engineState.copy(engineSessionState = null))
        } else {
            session
        } as T
    }
}
