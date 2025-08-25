/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This class implements the alternative ways to suppress StrictMode with performance
// monitoring by wrapping the raw methods. This lint check tells us not to use the raw
// methods so we suppress the check.
@file:Suppress("MozillaStrictModeSuppression")

package org.mozilla.fenix.perf

import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.StrictMode
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import mozilla.components.support.utils.ManufacturerChecker
import org.mozilla.fenix.components.Components
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong

private const val DELAY_TO_REMOVE_STRICT_MODE_MILLIS = 1000L

private val logger = Performance.logger
private val mainLooper = Looper.getMainLooper()

/**
 * Manages strict mode settings for the application.
 *
 * This class provides mechanisms to enable and configure Android's StrictMode,
 * a developer tool that detects things you might be doing by accident and brings
 * them to your attention so you can fix them.
 *
 * It allows for defining custom thread and VM policies, applying them, and
 * managing suppressions of StrictMode violations with logging and tracking.
 *
 * @param isEnabledByBuildConfig A boolean indicating if StrictMode should be enabled
 *                                based on the application's build configuration.
 * @param components An instance of [Components] used to access core application features like the profiler.
 *                   Ideally, a more specific dependency would be injected, but this is used to
 *                   work around a circular dependency where StrictMode is part of Core,
 *                   and Core would need to be passed in here.
 * @param buildManufacturerChecker An instance of [ManufacturerChecker] used to
 *                                 apply manufacturer-specific StrictMode workarounds.
 */
open class StrictModeManager(
    private val isEnabledByBuildConfig: Boolean,
    private val components: Components,
    private val buildManufacturerChecker: ManufacturerChecker,
) {

    /**
     * The number of times StrictMode has been suppressed. StrictMode can be used to prevent main
     * thread IO but it's easy to suppress. We use this value, in combination with:
     * - a test: that fails if the suppression count increases
     * - a lint check: to ensure this value gets used instead of functions that work around it
     * - code owners: to prevent modifications to these above items without perf knowing
     * to make suppressions a more deliberate act.
     *
     * This is an Atomic* so it can be safely incremented from any thread.
     */
    @VisibleForTesting(otherwise = PRIVATE)
    val suppressionCount = AtomicLong(0)

    /***
     * Enables strict mode for debug purposes. meant to be run only in the main process.
     * @param withPenaltyDeath boolean value to decide setting the penaltyDeath as a penalty.
     */
    fun enableStrictMode(withPenaltyDeath: Boolean) {
        if (!isEnabledByBuildConfig) {
            return
        }

        val threadPolicy = buildThreadPolicy(withPenaltyDeath)
        applyThreadPolicy(threadPolicy)

        val vmPolicy = buildVmPolicy()
        applyVmPolicy(vmPolicy)
    }

    /**
     * Builds a [StrictMode.ThreadPolicy] with the common settings used in Fenix.
     *
     * @param withPenaltyDeath If true, the policy will include a custom `penaltyDeath`.
     * @return The configured [StrictMode.ThreadPolicy].
     */
    private fun buildThreadPolicy(withPenaltyDeath: Boolean): StrictMode.ThreadPolicy {
        val builder = StrictMode.ThreadPolicy.Builder()
            .detectAll()
            .penaltyLog()
        if (withPenaltyDeath) {
            builder.penaltyDeathWithIgnores(buildManufacturerChecker)
        }
        return builder.build()
    }

    /**
     * Builds a [StrictMode.VmPolicy] with a curated set of detections and penalties.
     *
     * This function constructs a VmPolicy that:
     *  - Detects leaked SQLite objects.
     *  - Detects leaked closable objects.
     *  - Detects leaked registration objects (like BroadcastReceivers).
     *  - Detects Activity leaks.
     *  - Detects file URI exposure.
     *  - Logs detected violations to Logcat.
     *
     *  Additionally, based on the Android SDK version, it enables:
     *  - Detection of content URI access without proper permissions (Android O and above).
     *  - Detection of non-SDK API usage (Android P and above).
     *  - Detection of unsafe Intent launches (Android S and above).
     *
     * @return The configured [StrictMode.VmPolicy].
     */
    private fun buildVmPolicy(): StrictMode.VmPolicy {
        val builder = StrictMode.VmPolicy.Builder()
            .detectLeakedSqlLiteObjects()
            .detectLeakedClosableObjects()
            .detectLeakedRegistrationObjects()
            .detectActivityLeaks()
            .detectFileUriExposure()
            .penaltyLog()

        builder.detectContentUriWithoutPermission()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            builder.detectNonSdkApiUsage()
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            builder.detectUnsafeIntentLaunch()
        }
        return builder.build()
    }

    /**
     * Applies the given [StrictMode.ThreadPolicy].
     *
     * @param policy The StrictMode.ThreadPolicy to apply.
     */
    @VisibleForTesting
    internal open fun applyThreadPolicy(policy: StrictMode.ThreadPolicy) {
        StrictMode.setThreadPolicy(policy)
    }

    /**
     * Applies the given [StrictMode.VmPolicy].
     *
     * @param policy The [StrictMode.VmPolicy] to apply.
     */
    @VisibleForTesting
    internal open fun applyVmPolicy(policy: StrictMode.VmPolicy) {
        StrictMode.setVmPolicy(policy)
    }

    /**
     * Revert strict mode to disable penalty based on fragment lifecycle since strict mode
     * needs to switch to penalty logs. Using the fragment life cycle allows decoupling from any
     * specific fragment.
     */
    fun attachListenerToDisablePenaltyDeath(fragmentManager: FragmentManager) {
        fragmentManager.registerFragmentLifecycleCallbacks(DisableStrictModeFragmentLifecycleCallbacks(), false)
    }

    /**
     * Runs the given [functionBlock] and sets the given [StrictMode.ThreadPolicy] after its
     * completion when in a build configuration that has StrictMode enabled. If StrictMode is
     * not enabled, simply runs the [functionBlock]. This function is written in the style of
     * [AutoCloseable.use].
     *
     * This function contains perf improvements so it should be
     * called instead of [mozilla.components.support.ktx.android.os.resetAfter] (using the wrong
     * method should be prevented by a lint check). This is significantly less convenient to run than
     * when it was written as an extension function on [StrictMode.ThreadPolicy] but I think this is
     * okay: it shouldn't be easy to ignore StrictMode.
     *
     * @return the value returned by [functionBlock].
     */
    open fun <R> resetAfter(policy: StrictMode.ThreadPolicy, functionBlock: () -> R): R {
        fun instrumentedFunctionBlock(): R {
            val startProfilerTime = components.core.engine.profiler?.getProfilerTime()
            val returnValue = functionBlock()

            if (mainLooper.thread === Thread.currentThread()) { // markers only supported on main thread.
                components.core.engine.profiler?.addMarker("StrictMode.resetAfter", startProfilerTime)
            }
            return returnValue
        }

        // Calling resetAfter takes 1-2ms (unknown device) so we only execute it if StrictMode can
        // actually be enabled. https://github.com/mozilla-mobile/fenix/issues/11617
        return if (isEnabledByBuildConfig) {
            // This can overflow and crash. However, it's unlikely we'll suppress StrictMode 9
            // quintillion times in a build config where StrictMode is enabled so we don't handle it
            // because it'd increase complexity.
            val suppressionCount = suppressionCount.incrementAndGet()

            // We log so that devs are more likely to notice that we're suppressing StrictMode violations.
            logger.warn("StrictMode violation suppressed: #$suppressionCount")

            try {
                instrumentedFunctionBlock()
            } finally {
                applyThreadPolicy(policy)
            }
        } else {
            instrumentedFunctionBlock()
        }
    }

    /**
     * A [FragmentManager.FragmentLifecycleCallbacks] implementation that handles
     * StrictMode's `penaltyDeath` for fragments.
     *
     * Note that if we use anonymous classes/functions in this class,
     * we get a class load error with a slight perf impact.
     * See https://github.com/mozilla-mobile/fenix/issues/18731 for details.
     */
    inner class DisableStrictModeFragmentLifecycleCallbacks : FragmentManager.FragmentLifecycleCallbacks() {
        override fun onFragmentResumed(fm: FragmentManager, f: Fragment) {
            fm.unregisterFragmentLifecycleCallbacks(this)

            // If we don't post when using penaltyListener on P+, the violation listener is never
            // called. My best guess is that, unlike penaltyDeath, the violations are not
            // delivered instantaneously so posting gives time for the violation listeners to
            // run before they are removed here. This may be a race so we give the listeners a
            // little extra time to run too though this way we may accidentally trigger
            // violations for non-startup, which we haven't planned to do yet.
            Handler(Looper.getMainLooper()).postDelayed(::disableStrictMode, DELAY_TO_REMOVE_STRICT_MODE_MILLIS)
        }

        // See comment on anonymous functions above.
        private fun disableStrictMode() {
            enableStrictMode(withPenaltyDeath = false)
        }
    }
}

/**
 * There are certain manufacturers that have custom font classes for the OS systems.
 * These classes violates the [StrictMode] policies on startup. As a workaround, we create
 * an exception list for these manufacturers so that dialogs do not show up on start up.
 * To add a new manufacturer to the list, log "Build.MANUFACTURER" from the device to get the
 * exact name of the manufacturer.
 */
private fun isInStrictModeExceptionList(buildManufacturerChecker: ManufacturerChecker) =
    buildManufacturerChecker.isHuawei() || buildManufacturerChecker.isOnePlus() || buildManufacturerChecker.isOppo()

private fun StrictMode.ThreadPolicy.Builder.penaltyDeathWithIgnores(
    buildManufacturerChecker: ManufacturerChecker,
): StrictMode.ThreadPolicy.Builder {
    // This workaround was added before we realized we can ignored based on violation contents
    // (see code below). This solution - blanket disabling StrictMode on some manufacturers - isn't
    // great so, if we have time, we should consider reimplementing these fixes using the methods below.
    if (isInStrictModeExceptionList(buildManufacturerChecker)) {
        return this
    }

    // If we want to apply ignores based on stack trace contents to APIs below P, we can use this methodology:
    // https://medium.com/@tokudu/how-to-whitelist-strictmode-violations-on-android-based-on-stacktrace-eb0018e909aa
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
        penaltyDeath()
    } else {
        // Ideally, we'd use a shared thread pool but we don't have any on the system currently
        // (all shared ones are coroutine dispatchers).
        val executor = Executors.newSingleThreadExecutor()
        penaltyListener(executor, ThreadPenaltyDeathWithIgnoresListener())
    }

    return this
}
