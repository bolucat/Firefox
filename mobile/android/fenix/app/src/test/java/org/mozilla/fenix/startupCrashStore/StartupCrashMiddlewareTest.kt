package org.mozilla.fenix.startupCrashStore

import android.text.format.DateUtils
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.test.runTest
import mozilla.components.lib.crash.Crash
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyLong
import org.mockito.Mockito.mock
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mozilla.fenix.crashes.StartupCrashCanary
import org.mozilla.fenix.startupCrash.NoTapped
import org.mozilla.fenix.startupCrash.ReopenTapped
import org.mozilla.fenix.startupCrash.ReportTapped
import org.mozilla.fenix.startupCrash.StartupCrashMiddleware
import org.mozilla.fenix.startupCrash.StartupCrashState
import org.mozilla.fenix.startupCrash.StartupCrashStore
import org.mozilla.fenix.startupCrash.UiState
import org.mozilla.fenix.utils.Settings

private const val FIVE_DAYS_IN_MILLIS = DateUtils.DAY_IN_MILLIS * 5

@RunWith(AndroidJUnit4::class)
class StartupCrashMiddlewareTest {

    @get:Rule
    val coroutineTestRule = MainCoroutineRule()
    private val scope = coroutineTestRule.scope

    private lateinit var settings: Settings
    private lateinit var crashReporter: CrashReporter
    private lateinit var canaryRepo: StartupCrashCanary

    @Before
    fun setup() {
        settings = mock()
        crashReporter = mock()
        canaryRepo = mock()
    }

    @Test
    fun `when Report is tapped then unsent crash reports are submitted and FenixReady is dispatched`() = runTest {
        val crash = Crash.NativeCodeCrash(
            timestamp = 1755089858034L,
            minidumpPath = null,
            extrasPath = null,
            processVisibility = null,
            processType = null,
            breadcrumbs = arrayListOf(),
            remoteType = null,
        )

        `when`(crashReporter.unsentCrashReportsSince(anyLong())).thenReturn(listOf(crash))
        `when`(crashReporter.submitReport(any(), any())).thenReturn(CompletableDeferred(Unit))

        val store = makeStore().first

        store.dispatch(ReportTapped)
        store.waitUntilIdle()

        val crashCaptor = argumentCaptor<Crash>()
        verify(crashReporter).unsentCrashReportsSince(anyLong())
        verify(crashReporter).submitReport(crashCaptor.capture(), any())
        assertEquals(crash, crashCaptor.value)

        verify(canaryRepo).clearCanary()
        assertEquals(UiState.Finished, store.state.uiState)
    }

    @Test
    fun `when No is tapped then crash defer period is set and FenixReady is dispatched`() = runTest {
        val currentTime = System.currentTimeMillis()

        val store = makeStore(currentTime = { currentTime }).first

        store.dispatch(NoTapped)
        store.waitUntilIdle()

        verify(canaryRepo).clearCanary()
        verify(settings).crashReportDeferredUntil = currentTime + FIVE_DAYS_IN_MILLIS
        assertEquals(UiState.Finished, store.state.uiState)
    }

    @Test
    fun `when Reopen is tapped then initAndRestart handler is invoked and state is unchanged`() {
        val storeAndFlag = makeStore()

        val store = storeAndFlag.first
        val initAndRestartInvoked = storeAndFlag.second

        val before = store.state
        store.dispatch(ReopenTapped)
        store.waitUntilIdle()

        assertEquals(true, initAndRestartInvoked())
        assertEquals(before, store.state)
    }

    private fun makeStore(
        currentTime: () -> Long = { System.currentTimeMillis() },
    ): Pair<StartupCrashStore, () -> Boolean> {
        var called = false
        val middleware = StartupCrashMiddleware(
            settings = settings,
            crashReporter = crashReporter,
            reinitializeHandler = { called = true },
            startupCrashCanaryCache = canaryRepo,
            currentTimeInMillis = currentTime,
            scope = scope,
        )

        return StartupCrashStore(
            initialState = StartupCrashState(UiState.Idle),
            middleware = listOf(middleware),
        ) to { called }
    }
}
