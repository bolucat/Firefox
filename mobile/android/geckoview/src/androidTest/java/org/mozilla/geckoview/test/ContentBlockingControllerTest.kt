/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// For ContentBlockingException
@file:Suppress("DEPRECATION")

package org.mozilla.geckoview.test

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.Matchers.containsString
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.not
import org.hamcrest.Matchers.notNullValue
import org.hamcrest.Matchers.startsWith
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.ContentBlocking
import org.mozilla.geckoview.ContentBlocking.AntiTracking
import org.mozilla.geckoview.ContentBlocking.CookieBannerMode
import org.mozilla.geckoview.ContentBlockingController
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled

@RunWith(AndroidJUnit4::class)
@MediumTest
class ContentBlockingControllerTest : BaseSessionTest() {
    // Smoke test for safe browsing settings, most testing is through platform tests
    @Test
    fun safeBrowsingSettings() {
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        val google = contentBlocking.safeBrowsingProviders.first { it.name == "google" }
        val google4 = contentBlocking.safeBrowsingProviders.first { it.name == "google4" }
        val google5 = contentBlocking.safeBrowsingProviders.first { it.name == "google5" }

        // Let's make sure the initial value of safeBrowsingProviders is correct
        assertThat(
            "Expected number of default providers",
            contentBlocking.safeBrowsingProviders.size,
            equalTo(3),
        )
        assertThat("Google legacy provider is present", google, notNullValue())
        assertThat("Google provider is present", google4, notNullValue())
        assertThat("Google5 provider is present", google5, notNullValue())

        // Checks that the default provider values make sense
        assertThat(
            "Default provider values are sensible",
            google.getHashUrl,
            containsString("/safebrowsing-dummy/"),
        )
        assertThat(
            "Default provider values are sensible",
            google.advisoryUrl,
            startsWith("https://developers.google.com/"),
        )
        assertThat(
            "Default provider values are sensible",
            google4.getHashUrl,
            containsString("/safebrowsing4-dummy/"),
        )
        assertThat(
            "Default provider values are sensible",
            google4.updateUrl,
            containsString("/safebrowsing4-dummy/"),
        )
        assertThat(
            "Default provider values are sensible",
            google4.dataSharingUrl,
            startsWith("https://safebrowsing.googleapis.com/"),
        )
        assertThat(
            "Default provider values are sensible",
            google5.getHashUrl,
            containsString("/safebrowsing5-dummy/"),
        )
        assertThat(
            "Default provider values are sensible",
            google5.updateUrl,
            containsString("/safebrowsing5-dummy/"),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "browser.safebrowsing.provider.google4.updateURL",
            "browser.safebrowsing.provider.google4.gethashURL",
            "browser.safebrowsing.provider.google4.lists",
            "browser.safebrowsing.provider.google5.updateURL",
            "browser.safebrowsing.provider.google5.gethashURL",
            "browser.safebrowsing.provider.google5.lists",
            "browser.safebrowsing.provider.google5.enabled",
        )

        assertThat(
            "Initial prefs value is correct",
            originalPrefs[0] as String,
            equalTo(google4.updateUrl),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[1] as String,
            equalTo(google4.getHashUrl),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[2] as String,
            equalTo(google4.lists.joinToString(",")),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[3] as String,
            equalTo(google5.updateUrl),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[4] as String,
            equalTo(google5.getHashUrl),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[5] as String,
            equalTo(google5.lists.joinToString(",")),
        )
        assertThat(
            "Initial prefs value is correct",
            originalPrefs[6] as Boolean,
            equalTo(google5.enabled),
        )

        // Makes sure we can override a default value
        val override = ContentBlocking.SafeBrowsingProvider
            .from(ContentBlocking.GOOGLE_SAFE_BROWSING_PROVIDER)
            .updateUrl("http://test-update-url.com")
            .getHashUrl("http://test-get-hash-url.com")
            .build()

        val overrideV5 = ContentBlocking.SafeBrowsingProvider
            .from(ContentBlocking.GOOGLE_SAFE_BROWSING_V5_PROVIDER)
            .updateUrl("http://test-update-url-v5.com")
            .getHashUrl("http://test-get-hash-url-v5.com")
            .enabled(true)
            .build()

        // ... and that we can add a custom provider
        val custom = ContentBlocking.SafeBrowsingProvider
            .withName("custom-provider")
            .updateUrl("http://test-custom-update-url.com")
            .getHashUrl("http://test-custom-get-hash-url.com")
            .lists("a", "b", "c")
            .build()

        assertThat(
            "Override value is correct",
            override.updateUrl,
            equalTo("http://test-update-url.com"),
        )
        assertThat(
            "Override value is correct",
            override.getHashUrl,
            equalTo("http://test-get-hash-url.com"),
        )

        assertThat(
            "Override value is correct",
            overrideV5.updateUrl,
            equalTo("http://test-update-url-v5.com"),
        )
        assertThat(
            "Override value is correct",
            overrideV5.getHashUrl,
            equalTo("http://test-get-hash-url-v5.com"),
        )
        assertThat(
            "Override value is correct",
            overrideV5.enabled,
            equalTo(true),
        )

        assertThat(
            "Custom provider value is correct",
            custom.updateUrl,
            equalTo("http://test-custom-update-url.com"),
        )
        assertThat(
            "Custom provider value is correct",
            custom.getHashUrl,
            equalTo("http://test-custom-get-hash-url.com"),
        )
        assertThat(
            "Custom provider value is correct",
            custom.lists,
            equalTo(arrayOf("a", "b", "c")),
        )

        contentBlocking.setSafeBrowsingProviders(override, overrideV5, custom)

        val prefs = sessionRule.getPrefs(
            "browser.safebrowsing.provider.google4.updateURL",
            "browser.safebrowsing.provider.google4.gethashURL",
            "browser.safebrowsing.provider.google5.updateURL",
            "browser.safebrowsing.provider.google5.gethashURL",
            "browser.safebrowsing.provider.google5.enabled",
            "browser.safebrowsing.provider.custom-provider.updateURL",
            "browser.safebrowsing.provider.custom-provider.gethashURL",
            "browser.safebrowsing.provider.custom-provider.lists",
        )

        assertThat(
            "Pref value is set correctly",
            prefs[0] as String,
            equalTo("http://test-update-url.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[1] as String,
            equalTo("http://test-get-hash-url.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[2] as String,
            equalTo("http://test-update-url-v5.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[3] as String,
            equalTo("http://test-get-hash-url-v5.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[4] as Boolean,
            equalTo(true),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[5] as String,
            equalTo("http://test-custom-update-url.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[6] as String,
            equalTo("http://test-custom-get-hash-url.com"),
        )
        assertThat(
            "Pref value is set correctly",
            prefs[7] as String,
            equalTo("a,b,c"),
        )

        // Restore defaults
        contentBlocking.setSafeBrowsingProviders(google, google4, google5)

        // Checks that after restoring the providers the prefs get updated
        val restoredPrefs = sessionRule.getPrefs(
            "browser.safebrowsing.provider.google4.updateURL",
            "browser.safebrowsing.provider.google4.gethashURL",
            "browser.safebrowsing.provider.google4.lists",
            "browser.safebrowsing.provider.google5.updateURL",
            "browser.safebrowsing.provider.google5.gethashURL",
            "browser.safebrowsing.provider.google5.lists",
        )

        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[0] as String,
            equalTo(originalPrefs[0]),
        )
        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[1] as String,
            equalTo(originalPrefs[1]),
        )
        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[2] as String,
            equalTo(originalPrefs[2]),
        )
        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[3] as String,
            equalTo(originalPrefs[3]),
        )
        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[4] as String,
            equalTo(originalPrefs[4]),
        )
        assertThat(
            "Restored prefs value is correct",
            restoredPrefs[5] as String,
            equalTo(originalPrefs[5]),
        )
    }

    @Test
    fun getLog() {
        val category = ContentBlocking.AntiTracking.TEST
        sessionRule.runtime.settings.contentBlocking.setAntiTracking(category)
        mainSession.settings.useTrackingProtection = true
        mainSession.loadTestPath(TRACKERS_PATH)

        sessionRule.waitUntilCalled(object : ContentBlocking.Delegate {
            @AssertCalled(count = 1)
            override fun onContentBlocked(
                session: GeckoSession,
                event: ContentBlocking.BlockEvent,
            ) {
            }
        })

        sessionRule.waitForResult(
            sessionRule.runtime.contentBlockingController.getLog(mainSession).accept {
                assertThat("Log must not be null", it, notNullValue())
                assertThat("Log must have at least one entry", it?.size, not(0))
                it?.forEach {
                    it.blockingData.forEach {
                        assertThat(
                            "Category must match",
                            it.category,
                            equalTo(ContentBlockingController.Event.BLOCKED_TRACKING_CONTENT),
                        )
                        assertThat("Blocked must be true", it.blocked, equalTo(true))
                        assertThat("Count must be at least 1", it.count, not(0))
                    }
                }
            },
        )
    }

    @Test
    fun cookieBannerHandlingSettings() {
        // Check default value

        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.cookieBannerMode,
            equalTo(CookieBannerMode.COOKIE_BANNER_MODE_DISABLED),
        )
        assertThat(
            "Expect correct default value for private browsing",
            contentBlocking.cookieBannerModePrivateBrowsing,
            equalTo(CookieBannerMode.COOKIE_BANNER_MODE_REJECT),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "cookiebanners.service.mode",
            "cookiebanners.service.mode.privateBrowsing",
        )

        assertThat("Initial value is correct", originalPrefs[0] as Int, equalTo(contentBlocking.cookieBannerMode))
        assertThat("Initial value is correct", originalPrefs[1] as Int, equalTo(contentBlocking.cookieBannerModePrivateBrowsing))

        contentBlocking.cookieBannerMode = CookieBannerMode.COOKIE_BANNER_MODE_REJECT_OR_ACCEPT
        contentBlocking.cookieBannerModePrivateBrowsing = CookieBannerMode.COOKIE_BANNER_MODE_DISABLED

        val actualPrefs = sessionRule.getPrefs(
            "cookiebanners.service.mode",
            "cookiebanners.service.mode.privateBrowsing",
        )

        assertThat("Initial value is correct", actualPrefs[0] as Int, equalTo(contentBlocking.cookieBannerMode))
        assertThat("Initial value is correct", actualPrefs[1] as Int, equalTo(contentBlocking.cookieBannerModePrivateBrowsing))
    }

    @Test
    fun cookieBannerGlobalRulesEnabledSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.cookieBannerGlobalRulesEnabled,
            equalTo(false),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "cookiebanners.service.enableGlobalRules",
        )

        assertThat("Actual value is correct", originalPrefs[0] as Boolean, equalTo(contentBlocking.cookieBannerGlobalRulesEnabled))

        contentBlocking.cookieBannerGlobalRulesEnabled = true

        val actualPrefs = sessionRule.getPrefs("cookiebanners.service.enableGlobalRules")

        assertThat("Actual value is correct", actualPrefs[0] as Boolean, equalTo(contentBlocking.cookieBannerGlobalRulesEnabled))
    }

    @Test
    fun cookieBannerGlobalRulesSubFramesEnabledSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.cookieBannerGlobalRulesSubFramesEnabled,
            equalTo(false),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "cookiebanners.service.enableGlobalRules.subFrames",
        )

        assertThat("Actual value is correct", originalPrefs[0] as Boolean, equalTo(contentBlocking.cookieBannerGlobalRulesSubFramesEnabled))

        contentBlocking.cookieBannerGlobalRulesSubFramesEnabled = true

        val actualPrefs = sessionRule.getPrefs("cookiebanners.service.enableGlobalRules.subFrames")

        assertThat("Actual value is correct", actualPrefs[0] as Boolean, equalTo(contentBlocking.cookieBannerGlobalRulesSubFramesEnabled))
    }

    @Test
    fun cookieBannerHandlingDetectOnlyModeSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.cookieBannerDetectOnlyMode,
            equalTo(false),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "cookiebanners.service.detectOnly",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as Boolean,
            equalTo(contentBlocking.cookieBannerDetectOnlyMode),
        )

        contentBlocking.cookieBannerDetectOnlyMode = true

        val actualPrefs = sessionRule.getPrefs(
            "cookiebanners.service.detectOnly",
        )

        assertThat(
            "Initial value is correct",
            actualPrefs[0] as Boolean,
            equalTo(contentBlocking.cookieBannerDetectOnlyMode),
        )
    }

    @Test
    fun queryParameterStrippingSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.queryParameterStrippingEnabled,
            equalTo(false),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.enabled",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as Boolean,
            equalTo(contentBlocking.queryParameterStrippingEnabled),
        )

        contentBlocking.queryParameterStrippingEnabled = true

        val actualPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.enabled",
        )

        assertThat(
            "The value is updated",
            actualPrefs[0] as Boolean,
            equalTo(contentBlocking.queryParameterStrippingEnabled),
        )
    }

    @Test
    fun queryParameterStrippingPrivateBrowsingSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.queryParameterStrippingPrivateBrowsingEnabled,
            equalTo(false),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.enabled.pbmode",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as Boolean,
            equalTo(contentBlocking.queryParameterStrippingPrivateBrowsingEnabled),
        )

        contentBlocking.queryParameterStrippingPrivateBrowsingEnabled = true

        val actualPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.enabled.pbmode",
        )

        assertThat(
            "The value is updated",
            actualPrefs[0] as Boolean,
            equalTo(contentBlocking.queryParameterStrippingPrivateBrowsingEnabled),
        )
    }

    @Test
    fun queryParameterStrippingAllowListSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is empty string",
            contentBlocking.queryParameterStrippingAllowList.joinToString(","),
            equalTo(""),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.allow_list",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as String,
            equalTo(contentBlocking.queryParameterStrippingAllowList.joinToString(",")),
        )

        contentBlocking.setQueryParameterStrippingAllowList("item_one", "item_two")

        val actualPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.allow_list",
        )

        assertThat(
            "The value is updated",
            actualPrefs[0] as String,
            equalTo(contentBlocking.queryParameterStrippingAllowList.joinToString(",")),
        )
    }

    @Test
    fun queryParameterStrippingStripListSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is empty string",
            contentBlocking.queryParameterStrippingStripList.joinToString(","),
            equalTo(""),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.strip_list",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as String,
            equalTo(contentBlocking.queryParameterStrippingStripList.joinToString(",")),
        )

        contentBlocking.setQueryParameterStrippingAllowList("item_one", "item_two")

        val actualPrefs = sessionRule.getPrefs(
            "privacy.query_stripping.strip_list",
        )

        assertThat(
            "The value is updated",
            actualPrefs[0] as String,
            equalTo(contentBlocking.queryParameterStrippingStripList.joinToString(",")),
        )
    }

    @Test
    fun etpCategorySettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which ETP standard",
            contentBlocking.getEnhancedTrackingProtectionCategory(),
            equalTo(ContentBlocking.EtpCategory.STANDARD),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val defaultPrefs = sessionRule.getPrefs(
            "browser.contentblocking.category",
        )

        assertThat(
            "Initial value is correct",
            defaultPrefs[0] as String,
            equalTo("standard"),
        )

        contentBlocking.setEnhancedTrackingProtectionCategory(ContentBlocking.EtpCategory.STRICT)
        assertThat(
            "The getter returns the updated value for strict",
            contentBlocking.getEnhancedTrackingProtectionCategory(),
            equalTo(ContentBlocking.EtpCategory.STRICT),
        )

        val updatedPrefsStrict = sessionRule.getPrefs(
            "browser.contentblocking.category",
        )

        assertThat(
            "The pref value is updated",
            updatedPrefsStrict[0] as String,
            equalTo("strict"),
        )

        contentBlocking.setEnhancedTrackingProtectionCategory(ContentBlocking.EtpCategory.CUSTOM)
        assertThat(
            "The getter returns the updated value for custom",
            contentBlocking.getEnhancedTrackingProtectionCategory(),
            equalTo(ContentBlocking.EtpCategory.CUSTOM),
        )

        val updatedPrefsCustom = sessionRule.getPrefs(
            "browser.contentblocking.category",
        )

        assertThat(
            "The pref value is updated",
            updatedPrefsCustom[0] as String,
            equalTo("custom"),
        )
    }

    @Test
    fun toggleEmailTrackingForPrivateBrowsingMode() {
        // check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        val originalPref = sessionRule.getPrefs(
            "privacy.trackingprotection.emailtracking.pbmode.enabled",
        )
        assertThat(
            "Expect correct default value which is off",
            originalPref[0] as Boolean,
            equalTo(false),
        )

        contentBlocking.setEmailTrackerBlockingPrivateBrowsing(true)

        val updatedPref = sessionRule.getPrefs(
            "privacy.trackingprotection.emailtracking.pbmode.enabled",
        )
        assertThat(
            "Expect new value which is on",
            updatedPref[0] as Boolean,
            equalTo(true),
        )
    }

    @Test
    fun toggleEmailTrackingWhenETBAddedToAntiTrackingList() {
        // check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        val originalPref = sessionRule.getPrefs(
            "privacy.trackingprotection.emailtracking.enabled",
        )
        assertThat(
            "Expect correct default value which is off",
            originalPref[0] as Boolean,
            equalTo(false),
        )

        contentBlocking.setAntiTracking(AntiTracking.EMAIL)

        val updatedPref = sessionRule.getPrefs(
            "privacy.trackingprotection.emailtracking.enabled",
        )
        assertThat(
            "Expect new value which is on",
            updatedPref[0] as Boolean,
            equalTo(true),
        )
    }

    @Test
    fun bounceTrackingProtectionModeSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default value which is off",
            contentBlocking.bounceTrackingProtectionMode,
            equalTo(ContentBlocking.BounceTrackingProtectionMode.BOUNCE_TRACKING_PROTECTION_MODE_DISABLED),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalPrefs = sessionRule.getPrefs(
            "privacy.bounceTrackingProtection.mode",
        )

        assertThat(
            "Initial value is correct",
            originalPrefs[0] as Int,
            equalTo(contentBlocking.bounceTrackingProtectionMode),
        )

        contentBlocking.bounceTrackingProtectionMode = ContentBlocking.BounceTrackingProtectionMode.BOUNCE_TRACKING_PROTECTION_MODE_ENABLED

        val actualPrefs = sessionRule.getPrefs(
            "privacy.bounceTrackingProtection.mode",
        )

        assertThat(
            "The value is updated",
            actualPrefs[0] as Int,
            equalTo(contentBlocking.bounceTrackingProtectionMode),
        )

        // Set a new pref value, with a different setter method.
        contentBlocking.setBounceTrackingProtectionMode(ContentBlocking.BounceTrackingProtectionMode.BOUNCE_TRACKING_PROTECTION_MODE_ENABLED_STANDBY)

        val actualPrefs2 = sessionRule.getPrefs(
            "privacy.bounceTrackingProtection.mode",
        )

        assertThat(
            "The value is updated",
            actualPrefs2[0] as Int,
            equalTo(contentBlocking.bounceTrackingProtectionMode),
        )

        // Test that the getter returns the correct value.
        assertThat(
            "The getter returns the correct value",
            contentBlocking.getBounceTrackingProtectionMode(),
            equalTo(ContentBlocking.BounceTrackingProtectionMode.BOUNCE_TRACKING_PROTECTION_MODE_ENABLED_STANDBY),
        )
    }

    @Test
    fun allowListTrackingProtectionSettings() {
        // Check default value
        val contentBlocking = sessionRule.runtime.settings.contentBlocking

        assertThat(
            "Expect correct default for allowListBaselineTrackingProtection value which is true",
            contentBlocking.allowListBaselineTrackingProtection,
            equalTo(true),
        )

        assertThat(
            "Expect correct default for allowListConvenienceTrackingProtection value which is true",
            contentBlocking.allowListConvenienceTrackingProtection,
            equalTo(true),
        )

        // Checks that the pref value is also consistent with the runtime settings
        val originalAllowListBaseline = sessionRule.getPrefs(
            "privacy.trackingprotection.allow_list.baseline.enabled",
        )

        val originalAllowListConvenience = sessionRule.getPrefs(
            "privacy.trackingprotection.allow_list.convenience.enabled",
        )

        assertThat(
            "Initial allow list baseline value is correct",
            originalAllowListBaseline[0],
            equalTo(contentBlocking.allowListBaselineTrackingProtection),
        )

        assertThat(
            "Initial initial allow list convenience value is correct",
            originalAllowListConvenience[0],
            equalTo(contentBlocking.allowListConvenienceTrackingProtection),
        )

        contentBlocking.allowListConvenienceTrackingProtection = false

        val actualPrefs = sessionRule.getPrefs(
            "privacy.trackingprotection.allow_list.convenience.enabled",
        )

        assertThat(
            "Convenience is updated",
            actualPrefs[0] as Boolean,
            equalTo(false),
        )

        // Set a new pref value, with a different setter method.
        contentBlocking.setAllowListConvenienceTrackingProtection(true)

        val actualPrefs2 = sessionRule.getPrefs(
            "privacy.trackingprotection.allow_list.convenience.enabled",
        )

        assertThat(
            "Convenience is updated",
            actualPrefs2[0] as Boolean,
            equalTo(true),
        )
    }
}
