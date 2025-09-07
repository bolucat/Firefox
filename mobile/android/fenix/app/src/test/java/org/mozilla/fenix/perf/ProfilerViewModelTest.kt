/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import io.mockk.MockKAnnotations
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.mockk
import io.mockk.slot
import io.mockk.unmockkAll
import io.mockk.verify
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.Engine
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowApplication

@ExperimentalCoroutinesApi
@RunWith(RobolectricTestRunner::class)
class ProfilerViewModelTest {

    @get:Rule
    val coroutineRule = MainCoroutineRule()

    @MockK
    lateinit var mockProfiler: Profiler

    @RelaxedMockK
    lateinit var mockApplication: Application

    @RelaxedMockK
    lateinit var mockComponents: Components

    @RelaxedMockK
    lateinit var mockEngine: Engine

    @MockK
    lateinit var mockProfilerUtils: ProfilerUtils

    private lateinit var viewModel: ProfilerViewModel
    private lateinit var context: Context
    private lateinit var shadowApplication: ShadowApplication

    @Before
    fun setUp() {
        MockKAnnotations.init(this, relaxUnitFun = true)
        context = ApplicationProvider.getApplicationContext()
        shadowApplication = shadowOf(context as Application)
        val mockFenixApplicationContext = mockk<FenixApplication>(relaxed = true)

        // Forward service calls from the mock to the real context for Robolectric to track.
        every { mockApplication.startForegroundService(any()) } answers {
            context.startForegroundService(firstArg())
        }
        every { mockApplication.startService(any()) } answers {
            context.startService(firstArg())
        }

        // Extension function Context.components requires applicationContext to be FenixApplication
        every { mockFenixApplicationContext.components } returns mockComponents
        every { mockApplication.applicationContext } returns mockFenixApplicationContext
        every { mockFenixApplicationContext.getSystemService(any()) } returns context.getSystemService(Context.CLIPBOARD_SERVICE)
        every { mockFenixApplicationContext.cacheDir } returns context.cacheDir

        // Set up component chain: components -> core -> engine -> profiler
        every { mockComponents.core.engine } returns mockEngine
        every { mockComponents.core.engine.profiler } returns mockProfiler
        every { mockProfiler.isProfilerActive() } returns false

        every { mockApplication.getSystemService(Context.CLIPBOARD_SERVICE) } returns context.getSystemService(Context.CLIPBOARD_SERVICE)
        every { mockApplication.cacheDir } returns context.cacheDir

        every { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) } returns "http://example.com/profile/123"
        every { mockProfilerUtils.finishProfileSave(any(), any(), any()) } answers {
            val callback = arg<(Int) -> Unit>(2)
            callback.invoke(R.string.profiler_uploaded_url_to_clipboard)
        }
    }

    @After
    fun tearDown() {
        unmockkAll()
    }

    private fun initializeViewModel(
        isInitiallyActive: Boolean,
        mainDispatcher: CoroutineDispatcher = coroutineRule.testDispatcher,
        ioDispatcher: CoroutineDispatcher = coroutineRule.testDispatcher,
    ) {
        every { mockProfiler.isProfilerActive() } returns isInitiallyActive
        viewModel = ProfilerViewModel(
            application = mockApplication,
            mainDispatcher = mainDispatcher,
            ioDispatcher = ioDispatcher,
            profilerUtils = mockProfilerUtils,
        )
    }

    @Test
    fun `WHEN the ViewModel is created THEN its initial UI state and active status correctly reflect the profiler's state`() = runTest(coroutineRule.testDispatcher.scheduler) {
        initializeViewModel(isInitiallyActive = false)

        val collectedUiStates = mutableListOf<ProfilerUiState>()
        val uiCollectJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedUiStates)
        }
        val collectedActiveStates = mutableListOf<Boolean>()
        val activeCollectJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.isProfilerActive.toList(collectedActiveStates)
        }

        advanceUntilIdle()
        assertEquals(listOf(ProfilerUiState.Idle), collectedUiStates)
        assertEquals(listOf(false), collectedActiveStates)

        uiCollectJob.cancel()
        activeCollectJob.cancel()

        collectedUiStates.clear()
        collectedActiveStates.clear()

        initializeViewModel(isInitiallyActive = true)
        val uiCollectJob2 = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedUiStates)
        }
        val activeCollectJob2 = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.isProfilerActive.toList(collectedActiveStates)
        }

        advanceUntilIdle()
        assertEquals(listOf(ProfilerUiState.Idle), collectedUiStates)
        assertEquals(listOf(true), collectedActiveStates)

        uiCollectJob2.cancel()
        activeCollectJob2.cancel()
    }

    @Test
    fun `GIVEN the profiler is unavailable WHEN start profiling is tried THEN the state should be moved to error`() = runTest(coroutineRule.testDispatcher.scheduler) {
        every { mockComponents.core.engine.profiler } returns null

        initializeViewModel(
            isInitiallyActive = false,
            mainDispatcher = coroutineRule.testDispatcher,
            ioDispatcher = coroutineRule.testDispatcher,
        )
        advanceUntilIdle()

        assertEquals(ProfilerUiState.Idle, viewModel.uiState.value)
        viewModel.initiateProfilerStartProcess(ProfilerSettings.Firefox)
        advanceUntilIdle()

        val finalState = viewModel.uiState.value
        assertTrue(finalState is ProfilerUiState.Error)
        assertEquals(R.string.profiler_error, (finalState as ProfilerUiState.Error).messageResId)
        assertTrue(
            finalState.errorDetails.contains("Profiler not available"),
        )
        verify(exactly = 0) { mockProfiler.startProfiler(any(), any()) }
    }

    @Test
    fun `GIVEN the profiler is already running WHEN profiling again THEN the UI state remains Running and no new profiler session is started`() = runTest(coroutineRule.testDispatcher.scheduler) {
        initializeViewModel(
            isInitiallyActive = true,
            mainDispatcher = coroutineRule.testDispatcher,
            ioDispatcher = coroutineRule.testDispatcher,
        )
        advanceUntilIdle()
        assertEquals(ProfilerUiState.Idle, viewModel.uiState.value)
        viewModel.initiateProfilerStartProcess(ProfilerSettings.Firefox)
        advanceUntilIdle()
        assertEquals(ProfilerUiState.Running, viewModel.uiState.value)
        verify(exactly = 0) { mockProfiler.startProfiler(any(), any()) }
    }

    @Test
    fun `WHEN profiling starts successfully THEN the profiler starts and service launches with notification`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = false,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )
        val settings = ProfilerSettings.Firefox

        val collectedStates = mutableListOf<ProfilerUiState>()
        val collectionJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedStates)
        }

        viewModel.initiateProfilerStartProcess(settings)
        every { mockProfiler.isProfilerActive() } returns true
        advanceUntilIdle()

        collectionJob.cancel()

        val expectedSequence = listOf(
            ProfilerUiState.Idle::class,
            ProfilerUiState.Starting::class,
            ProfilerUiState.ShowToast::class,
            ProfilerUiState.Running::class,
        )
        val actualSequence = collectedStates.map { it::class }
        assertEquals("The sequence of UI states was not as expected", expectedSequence, actualSequence)

        verify { mockProfiler.startProfiler(settings.threads, settings.features) }

        val startedServiceIntent = shadowApplication.nextStartedService
        assertNotNull("A service should have been started", startedServiceIntent)
        assertEquals(ProfilerService.ACTION_START_PROFILING, startedServiceIntent.action)
    }

    @Test
    fun `GIVEN the profiler is not currently active WHEN an attempt is made to stop and save a profile THEN the UI state becomes Finished and no profile URL is available`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = false,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )
        advanceUntilIdle()

        viewModel.stopProfilerAndSave()
        advanceUntilIdle()

        val lastState = viewModel.uiState.value
        assertTrue(lastState is ProfilerUiState.Finished)
        assertNull((lastState as ProfilerUiState.Finished).profileUrl)

        verify(exactly = 0) { mockProfiler.stopProfiler(any(), any()) }
        val startedServiceIntent = shadowApplication.nextStartedService
        assertNull("No service should have been started", startedServiceIntent)
    }

    @Test
    fun `WHEN a profile is successfully saved THEN the UI shows successful completing with copied URL`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = true,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )

        val fakeProfileData = "profile_data".toByteArray()
        val expectedUrl = "http://example.com/profile/123"
        val successCallbackSlot = slot<(ByteArray?) -> Unit>()

        every {
            mockProfiler.stopProfiler(capture(successCallbackSlot), any())
        } answers {
            successCallbackSlot.captured(fakeProfileData)
        }

        val collectedStates = mutableListOf<ProfilerUiState>()
        val collectionJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedStates)
        }

        viewModel.stopProfilerAndSave()
        advanceUntilIdle()
        collectionJob.cancel()

        assertTrue(collectedStates.any { it is ProfilerUiState.Idle })
        assertTrue(collectedStates.any { it is ProfilerUiState.Gathering })
        val toastState = collectedStates.find { it is ProfilerUiState.ShowToast }
        assertNotNull(toastState)
        assertEquals(R.string.profiler_uploaded_url_to_clipboard, (toastState as ProfilerUiState.ShowToast).messageResId)
        val finishedState = collectedStates.last()
        assertTrue(finishedState is ProfilerUiState.Finished)
        assertEquals(expectedUrl, (finishedState as ProfilerUiState.Finished).profileUrl)

        verify { mockProfiler.stopProfiler(any(), any()) }
        verify { mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication) }
        verify { mockProfilerUtils.finishProfileSave(mockApplication, expectedUrl, any()) }
        val startedServiceIntent = shadowApplication.nextStartedService
        assertNotNull("A service should have been started to stop profiling", startedServiceIntent)
        assertEquals(ProfilerService.ACTION_STOP_PROFILING, startedServiceIntent.action)
    }

    @Test
    fun `WHEN the profiler fails during saving the profile THEN state changes to error and the error message is displayed`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = true,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )

        val exception = RuntimeException("Profiler Stop Failed")
        val errorCallbackSlot = slot<(Throwable) -> Unit>()

        every {
            mockProfiler.stopProfiler(any(), capture(errorCallbackSlot))
        } answers {
            errorCallbackSlot.captured(exception)
        }

        val collectedStates = mutableListOf<ProfilerUiState>()
        val collectionJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedStates)
        }

        viewModel.stopProfilerAndSave()
        advanceUntilIdle()
        collectionJob.cancel()

        assertTrue(collectedStates.any { it is ProfilerUiState.Idle })
        assertTrue(collectedStates.any { it is ProfilerUiState.Gathering })
        val errorState = collectedStates.last()
        assertTrue(errorState is ProfilerUiState.Error)
        assertEquals(R.string.profiler_error, (errorState as ProfilerUiState.Error).messageResId)
        assertEquals("Profiler Stop Failed", errorState.errorDetails)

        verify { mockProfiler.stopProfiler(any(), any()) }
        verify(exactly = 0) { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) }
        val startedServiceIntent = shadowApplication.nextStartedService
        assertNotNull("Intent for stopping service was not captured", startedServiceIntent)
        assertEquals(ProfilerService.ACTION_STOP_PROFILING, startedServiceIntent.action)
    }

    @Test
    fun `WHEN data save after stopping the profiler THEN toast shows error and service stops`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = true,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )

        val fakeProfileData = "profile_data".toByteArray()
        val saveException = java.io.IOException("Disk full")
        val successCallbackSlot = slot<(ByteArray?) -> Unit>()

        every {
            mockProfiler.stopProfiler(capture(successCallbackSlot), any())
        } answers {
            successCallbackSlot.captured(fakeProfileData)
        }

        every {
            mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication)
        } throws saveException

        val collectedStates = mutableListOf<ProfilerUiState>()
        val collectionJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedStates)
        }

        viewModel.stopProfilerAndSave()
        advanceUntilIdle()
        collectionJob.cancel()

        val expectedSequence = listOf(
            ProfilerUiState.Idle::class,
            ProfilerUiState.Gathering::class,
            ProfilerUiState.Error::class,
        )
        val actualSequence = collectedStates.map { it::class }
        assertEquals("The sequence of UI states was not as expected", expectedSequence, actualSequence)

        val errorState = collectedStates.last() as ProfilerUiState.Error
        assertEquals(R.string.profiler_io_error, errorState.messageResId)
        assertEquals("Disk full", errorState.errorDetails)

        verify { mockProfiler.stopProfiler(any(), any()) }
        verify { mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication) }
        verify(exactly = 0) { mockProfilerUtils.finishProfileSave(any(), any(), any()) }
        val startedServiceIntent = shadowApplication.nextStartedService
        assertNotNull("A service should have been started", startedServiceIntent)
        assertEquals(ProfilerService.ACTION_STOP_PROFILING, startedServiceIntent.action)
    }

    @Test
    fun `WHEN the profiler is stopped without saving THEN the UI shows the profiler is stopped without a URL copied`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = true,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )

        val successCallbackSlot = slot<(ByteArray?) -> Unit>()
        every {
            mockProfiler.stopProfiler(capture(successCallbackSlot), any())
        } answers {
            successCallbackSlot.captured(null)
        }

        val collectedUiStates = mutableListOf<ProfilerUiState>()
        val collectionJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.uiState.toList(collectedUiStates)
        }

        viewModel.stopProfilerWithoutSaving()
        advanceUntilIdle()
        collectionJob.cancel()

        assertEquals("Expected 3 state emissions: Initial, Stopping, Finished", 3, collectedUiStates.size)
        assertTrue("First collected state should be Idle", collectedUiStates[0] is ProfilerUiState.Idle)
        assertTrue("Second collected state should be Stopping", collectedUiStates[1] is ProfilerUiState.Stopping)
        val finalState = collectedUiStates[2]
        assertTrue("Third collected state should be Finished", finalState is ProfilerUiState.Finished)
        assertNull("Profile URL should be null in the final Finished state", (finalState as ProfilerUiState.Finished).profileUrl)

        verify { mockProfiler.stopProfiler(any(), any()) }
        verify(exactly = 0) { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) }
        val startedServiceIntent = shadowApplication.nextStartedService
        assertNotNull("Intent for stopping service was not captured", startedServiceIntent)
        assertEquals(ProfilerService.ACTION_STOP_PROFILING, startedServiceIntent.action)
    }

    @Test
    fun `WHEN the profiler's active status changes THEN the ViewModel's status updates accordingly`() = runTest(coroutineRule.testDispatcher.scheduler) {
        val testDispatcher = coroutineRule.testDispatcher
        initializeViewModel(
            isInitiallyActive = false,
            mainDispatcher = testDispatcher,
            ioDispatcher = testDispatcher,
        )

        val collectedStates = mutableListOf<Boolean>()
        val collectJob = launch(UnconfinedTestDispatcher(testScheduler)) {
            viewModel.isProfilerActive.toList(collectedStates)
        }
        advanceUntilIdle()

        every { mockProfiler.isProfilerActive() } returns true
        viewModel.updateProfilerActiveStatus()
        advanceUntilIdle()

        every { mockProfiler.isProfilerActive() } returns false
        viewModel.updateProfilerActiveStatus()
        advanceUntilIdle()

        collectJob.cancel()

        assertEquals(listOf(false, true, false), collectedStates)
    }
}
