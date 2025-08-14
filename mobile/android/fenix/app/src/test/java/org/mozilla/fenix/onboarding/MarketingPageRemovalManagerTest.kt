/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.content.SharedPreferences
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.distributions.DistributionIdManager
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class MarketingPageRemovalManagerTest {
    @get:Rule
    val mainCoroutineRule = MainCoroutineRule()

    private lateinit var pages: MutableList<OnboardingPageUiData>
    private lateinit var settings: Settings
    private lateinit var mockedLifecycleOwner: TestLifecycleOwner
    private lateinit var distributionIdManager: DistributionIdManager
    private lateinit var prefKey: String

    @Before
    fun setup() {
        pages = mutableListOf<OnboardingPageUiData>().apply {
            add(
                OnboardingPageUiData(
                    type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
                    imageRes = 0,
                    title = "sync title",
                    description = "sync body",
                    primaryButtonLabel = "sync primary button text",
                    secondaryButtonLabel = "sync secondary button text",
                    privacyCaption = null,
                ),
            )
            add(
                OnboardingPageUiData(
                    type = OnboardingPageUiData.Type.MARKETING_DATA,
                    imageRes = 0,
                    title = "marketing title",
                    description = "notification body",
                    primaryButtonLabel = "notification primary button text",
                    secondaryButtonLabel = "notification secondary button text",
                    privacyCaption = null,
                ),
            )
        }
        settings = Settings(testContext)
        mockedLifecycleOwner = TestLifecycleOwner(Lifecycle.State.CREATED)
        distributionIdManager = mockk(relaxed = true)
        prefKey = testContext.getString(R.string.pref_key_should_show_marketing_onboarding)
    }

    @Test
    fun `we should show marketing AND there is not a partnership`() = runTest {
        every { distributionIdManager.isPartnershipDistribution() } returns false

        val removePage = MarketingPageRemovalSupport(
            prefKey = prefKey,
            pagesToDisplay = pages,
            distributionIdManager = distributionIdManager,
            settings = settings,
            ioContext = testScheduler,
            lifecycleOwner = mockedLifecycleOwner,
        )
        settings.shouldShowMarketingOnboarding = true

        removePage.start()

        testScheduler.advanceUntilIdle()

        assertTrue(pages.size == 2)
    }

    @Test
    fun `we should show marketing AND there is a partnership`() = runTest {
        every { distributionIdManager.isPartnershipDistribution() } returns true

        val removePage = MarketingPageRemovalSupport(
            prefKey = prefKey,
            pagesToDisplay = pages,
            distributionIdManager = distributionIdManager,
            settings = settings,
            ioContext = testScheduler,
            lifecycleOwner = mockedLifecycleOwner,
        )
        settings.shouldShowMarketingOnboarding = true

        removePage.start()

        testScheduler.advanceUntilIdle()

        assertTrue(pages.size == 2)
    }

    @Test
    fun `we should not show marketing AND there is a partnership`() = runTest {
        every { distributionIdManager.isPartnershipDistribution() } returns true

        val removePage = MarketingPageRemovalSupport(
            prefKey = prefKey,
            pagesToDisplay = pages,
            distributionIdManager = distributionIdManager,
            settings = settings,
            ioContext = testScheduler,
            lifecycleOwner = mockedLifecycleOwner,
        )
        settings.shouldShowMarketingOnboarding = false

        removePage.start()

        testScheduler.advanceUntilIdle()

        assertTrue(pages.size == 2)
    }

    @Test
    fun `we should not show marketing AND there is not a partnership`() = runTest {
        every { distributionIdManager.isPartnershipDistribution() } returns false

        val removePage = MarketingPageRemovalSupport(
            prefKey = prefKey,
            pagesToDisplay = pages,
            distributionIdManager = distributionIdManager,
            settings = settings,
            ioContext = testScheduler,
            lifecycleOwner = mockedLifecycleOwner,
        )
        settings.shouldShowMarketingOnboarding = false

        removePage.start()

        testScheduler.advanceUntilIdle()

        assertTrue(pages.size == 1)
    }

    @Test
    fun `remove page`() {
        pages.removeIfPageNotReached(0)

        assertTrue(pages.size == 1)
    }

    @Test
    fun `do not remove page`() {
        pages.removeIfPageNotReached(1)

        assertTrue(pages.size == 2)
    }

    @Test
    fun `do not remove page if marketing does not exist`() {
        val pages = mutableListOf<OnboardingPageUiData>().apply {
            add(
                OnboardingPageUiData(
                    type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
                    imageRes = 0,
                    title = "sync title",
                    description = "sync body",
                    primaryButtonLabel = "sync primary button text",
                    secondaryButtonLabel = "sync secondary button text",
                    privacyCaption = null,
                ),
            )
            add(
                OnboardingPageUiData(
                    type = OnboardingPageUiData.Type.THEME_SELECTION,
                    imageRes = 0,
                    title = "theme title",
                    description = "notification body",
                    primaryButtonLabel = "notification primary button text",
                    secondaryButtonLabel = "notification secondary button text",
                    privacyCaption = null,
                ),
            )
        }

        pages.removeIfPageNotReached(0)

        assertTrue(pages.size == 2)
    }

    @Test
    fun `flow emits initial value from SharedPreferences`() = runTest {
        val prefs = mockk<SharedPreferences>(relaxed = true)
        val lifecycleOwner = TestLifecycleOwner()
        every { prefs.getBoolean("my_key", false) } returns true

        val results = mutableListOf<Boolean>()
        val job = launch {
            prefs.flowScopedBooleanPreference(lifecycleOwner, "my_key", false)
                .toList(results)
        }

        lifecycleOwner.onResume()
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(true), results)
        job.cancel()
    }

    @Test
    fun `flow emits updated value on preference change`() = runTest {
        val prefs = mockk<SharedPreferences>(relaxed = true)
        val lifecycleOwner = TestLifecycleOwner()
        val listenerSlot = slot<SharedPreferences.OnSharedPreferenceChangeListener>()

        every { prefs.registerOnSharedPreferenceChangeListener(capture(listenerSlot)) } just Runs
        every { prefs.unregisterOnSharedPreferenceChangeListener(any()) } just Runs
        every { prefs.getBoolean("my_key", false) } returnsMany listOf(true, false)

        val results = mutableListOf<Boolean>()
        val job = launch {
            prefs.flowScopedBooleanPreference(lifecycleOwner, "my_key", false)
                .toList(results)
        }

        lifecycleOwner.onResume()
        testScheduler.advanceUntilIdle()

        listenerSlot.captured.onSharedPreferenceChanged(prefs, "my_key")
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(true, false), results)
        job.cancel()
    }
}
