/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.util.TypedValue
import android.view.Window
import androidx.annotation.AnyRes
import androidx.annotation.StyleRes
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.ktx.android.content.getStatusBarColor
import mozilla.components.support.ktx.android.view.createWindowInsetsController
import mozilla.components.support.ktx.android.view.setNavigationBarColorCompat
import mozilla.components.support.ktx.android.view.setStatusBarColorCompat
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.customtabs.ExternalAppBrowserActivity

abstract class ThemeManager {

    abstract var currentTheme: BrowsingMode

    /**
     * Returns the style resource corresponding to the [currentTheme].
     */
    @get:StyleRes
    val currentThemeResource get() = when (currentTheme) {
        BrowsingMode.Normal -> R.style.NormalTheme
        BrowsingMode.Private -> R.style.PrivateTheme
    }

    /**
     * Handles status bar theme change since the window does not dynamically recreate
     *
     * @param activity The activity to apply the status bar theme to.
     * @param overrideThemeStatusBarColor Whether to override the theme's status bar color.
     */
    fun applyStatusBarTheme(activity: Activity, overrideThemeStatusBarColor: Boolean = false) =
        applyStatusBarTheme(activity.window, activity, overrideThemeStatusBarColor)

    private fun applyStatusBarTheme(
        window: Window,
        context: Context,
        overrideThemeStatusBarColor: Boolean,
    ) {
        when (currentTheme) {
            BrowsingMode.Normal -> {
                when (context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) {
                    Configuration.UI_MODE_NIGHT_UNDEFINED, // We assume light here per Android doc's recommendation
                    Configuration.UI_MODE_NIGHT_NO,
                    -> {
                        updateLightSystemBars(window, context, overrideThemeStatusBarColor)
                    }
                    Configuration.UI_MODE_NIGHT_YES -> {
                        clearLightSystemBars(window)
                        setStatusBarColor(window, context, overrideThemeStatusBarColor)
                        updateNavigationBar(window, context)
                    }
                }
            }
            BrowsingMode.Private -> {
                clearLightSystemBars(window)
                setStatusBarColor(window, context, overrideThemeStatusBarColor)
                updateNavigationBar(window, context)
            }
        }
    }

    fun setActivityTheme(activity: Activity) {
        activity.setTheme(currentThemeResource)
    }

    companion object {
        /**
         * Resolves the attribute to a resource ID.
         *
         * @param attribute The attribute to resolve.
         * @param context Any context.
         * @return The resource ID of the resolved attribute.
         */
        @AnyRes
        fun resolveAttribute(attribute: Int, context: Context): Int {
            val typedValue = TypedValue()
            val theme = context.theme
            theme.resolveAttribute(attribute, typedValue, true)

            return typedValue.resourceId
        }

        @Composable
        fun resolveAttributeColor(attribute: Int): androidx.compose.ui.graphics.Color {
            val resourceId = resolveAttribute(attribute, LocalContext.current)
            return colorResource(resourceId)
        }

        private fun updateLightSystemBars(window: Window, context: Context, overrideThemeStatusBarColor: Boolean) {
            setStatusBarColor(window, context, overrideThemeStatusBarColor)
            window.createWindowInsetsController().isAppearanceLightStatusBars = true

            // display handle light navigation bar color
            window.createWindowInsetsController().isAppearanceLightNavigationBars = true

            updateNavigationBar(window, context)
        }

        private fun clearLightSystemBars(window: Window) {
            window.createWindowInsetsController().isAppearanceLightStatusBars = false

            // display handle light navigation bar color
            window.createWindowInsetsController().isAppearanceLightNavigationBars = false
        }

        private fun updateNavigationBar(window: Window, context: Context) {
            window.setNavigationBarColorCompat(context.getColorFromAttr(R.attr.layer1))
        }

        private fun setStatusBarColor(
            window: Window,
            context: Context,
            overrideThemeStatusBarColor: Boolean,
        ) {
            if (overrideThemeStatusBarColor) {
                window.setStatusBarColorCompat(context.getColorFromAttr(R.attr.layer3))
            } else {
                context.getStatusBarColor()?.let { window.setStatusBarColorCompat(it) }
            }
        }
    }
}

class DefaultThemeManager(
    currentTheme: BrowsingMode,
    private val activity: Activity,
) : ThemeManager() {
    override var currentTheme: BrowsingMode = currentTheme
        set(value) {
            if (currentTheme != value) {
                // ExternalAppBrowserActivity doesn't need to switch between private and non-private.
                if (activity is ExternalAppBrowserActivity) return
                // Don't recreate if activity is finishing
                if (activity.isFinishing) return

                field = value

                val intent = activity.intent ?: Intent().also { activity.intent = it }
                intent.putExtra(HomeActivity.PRIVATE_BROWSING_MODE, value == BrowsingMode.Private)

                activity.recreate()
            }
        }
}
