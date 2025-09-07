/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat.getColor
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.selector.selectedNormalTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.toolbar.BrowserToolbar
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopup.PopupAlignment.BODY_TO_ANCHOR_CENTER
import mozilla.components.compose.cfr.CFRPopup.PopupAlignment.INDICATOR_CENTERED_IN_ANCHOR
import mozilla.components.compose.cfr.CFRPopupProperties
import mozilla.components.concept.engine.EngineSession.CookieBannerHandlingStatus
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.AddressToolbar
import org.mozilla.fenix.GleanMetrics.CookieBanners
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings
import mozilla.components.browser.toolbar.R as toolbarR

/**
 * Vertical padding needed to improve the visual alignment of the popup and respect the UX design.
 */
private const val CFR_TO_ANCHOR_VERTICAL_PADDING = -6
private const val TAB_SWIPE_CFR_ARROW_OFFSET = 160

/**
 * Delegate for handling all the business logic for showing CFRs in the toolbar.
 *
 * @param context used for various Android interactions.
 * @param browserStore will be observed for tabs updates
 * @param settings used to read and write persistent user settings
 * @param toolbar will serve as anchor for the CFRs
 * @param isPrivate Whether or not the session is private.
 * @param customTabId Optional custom tab id used to identify the custom tab in which to show a CFR.
 */
@Suppress("LongParameterList")
class BrowserToolbarCFRPresenter(
    private val context: Context,
    private val browserStore: BrowserStore,
    private val settings: Settings,
    private val toolbar: BrowserToolbar,
    private val isPrivate: Boolean,
    private val customTabId: String? = null,
) {
    @VisibleForTesting
    internal var scope: CoroutineScope? = null

    @VisibleForTesting
    internal var popup: CFRPopup? = null

    /**
     * Start observing [browserStore] for updates which may trigger showing a CFR.
     */
    @Suppress("MagicNumber")
    fun start() {
        @Suppress("ComplexCondition")
        if (!isPrivate && !settings.hasShownTabSwipeCFR &&
            !settings.isTabStripEnabled && settings.isSwipeToolbarToSwitchTabsEnabled
        ) {
            scope = browserStore.flowScoped { flow ->
                flow
                    .distinctUntilChangedBy { it.selectedNormalTab?.id }
                    .collect {
                        if (settings.shouldShowTabSwipeCFR && !settings.hasShownTabSwipeCFR) {
                            scope?.cancel()
                            settings.shouldShowTabSwipeCFR = false
                            settings.hasShownTabSwipeCFR = true
                            showTabSwipeCFR()
                        }
                    }
            }
        }

        when (getCFRToShow()) {
            ToolbarCFR.COOKIE_BANNERS -> {
                scope = browserStore.flowScoped { flow ->
                    flow.mapNotNull { it.findCustomTabOrSelectedTab(customTabId) }
                        .ifAnyChanged { tab ->
                            arrayOf(
                                tab.cookieBanner,
                            )
                        }
                        .filter {
                            it.content.private && it.cookieBanner == CookieBannerHandlingStatus.HANDLED
                        }
                        .collect {
                            scope?.cancel()
                            settings.shouldShowCookieBannersCFR = false
                            showCookieBannersCFR()
                        }
                }
            }

            ToolbarCFR.NONE -> {
                // no-op
            }
        }
    }

    private fun getCFRToShow(): ToolbarCFR = when {
        isPrivate && settings.shouldShowCookieBannersCFR && settings.shouldUseCookieBannerPrivateMode -> {
            ToolbarCFR.COOKIE_BANNERS
        }

        else -> ToolbarCFR.NONE
    }

    /**
     * Stop listening for [browserStore] updates.
     * CFRs already shown are not automatically dismissed.
     */
    fun stop() {
        scope?.cancel()
    }

    @VisibleForTesting
    @Suppress("LongMethod")
    internal fun showCookieBannersCFR() {
        CFRPopup(
            anchor = toolbar.findViewById(
                toolbarR.id.mozac_browser_toolbar_site_info_indicator,
            ),
            properties = CFRPopupProperties(
                popupAlignment = INDICATOR_CENTERED_IN_ANCHOR,
                popupBodyColors = listOf(
                    getColor(context, R.color.fx_mobile_layer_color_gradient_end),
                    getColor(context, R.color.fx_mobile_layer_color_gradient_start),
                ),
                popupVerticalOffset = CFR_TO_ANCHOR_VERTICAL_PADDING.dp,
                dismissButtonColor = getColor(context, R.color.fx_mobile_icon_color_oncolor),
                indicatorDirection = if (settings.toolbarPosition == ToolbarPosition.TOP) {
                    CFRPopup.IndicatorDirection.UP
                } else {
                    CFRPopup.IndicatorDirection.DOWN
                },
            ),
            onDismiss = {
                CookieBanners.cfrDismissal.record(NoExtras())
            },
            text = {
                FirefoxTheme {
                    Column {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(
                                painter = painterResource(id = R.drawable.ic_cookies_disabled),
                                contentDescription = null,
                                tint = FirefoxTheme.colors.iconPrimary,
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(
                                    R.string.cookie_banner_cfr_title,
                                    context.getString(R.string.firefox),
                                ),
                                color = FirefoxTheme.colors.textOnColorPrimary,
                                style = FirefoxTheme.typography.subtitle2,
                            )
                        }
                        Text(
                            text = context.getString(R.string.cookie_banner_cfr_message),
                            color = FirefoxTheme.colors.textOnColorPrimary,
                            style = FirefoxTheme.typography.body2,
                            modifier = Modifier.padding(top = 2.dp),
                        )
                    }
                }
            },
        ).run {
            popup = this
            show()
            CookieBanners.cfrShown.record(NoExtras())
        }
    }

    @VisibleForTesting
    @Suppress("LongMethod")
    internal fun showTabSwipeCFR() {
        CFRPopup(
            anchor = toolbar.findViewById(
                toolbarR.id.mozac_browser_toolbar_background,
            ),
            properties = CFRPopupProperties(
                popupAlignment = BODY_TO_ANCHOR_CENTER,
                popupBodyColors = listOf(
                    getColor(context, R.color.fx_mobile_layer_color_gradient_end),
                    getColor(context, R.color.fx_mobile_layer_color_gradient_start),
                ),
                dismissButtonColor = getColor(context, R.color.fx_mobile_icon_color_oncolor),
                indicatorDirection = if (context.settings().toolbarPosition == ToolbarPosition.TOP) {
                    CFRPopup.IndicatorDirection.UP
                } else {
                    CFRPopup.IndicatorDirection.DOWN
                },
                indicatorArrowStartOffset = TAB_SWIPE_CFR_ARROW_OFFSET.dp,
            ),
            onDismiss = {
                AddressToolbar.swipeCfrDismissed.record(NoExtras())
                popup = null
            },
            text = {
                FirefoxTheme {
                    Column {
                        Text(
                            text = context.getString(R.string.address_bar_swipe_cfr_message_2),
                            color = FirefoxTheme.colors.textOnColorPrimary,
                            style = FirefoxTheme.typography.body2,
                            modifier = Modifier.padding(top = 2.dp),
                        )
                    }
                }
            },
        ).run {
            AddressToolbar.swipeCfrShown.record(NoExtras())
            popup = this
            show()
        }
    }
}

/**
 * The CFR to be shown in the toolbar.
 */
private enum class ToolbarCFR {
    COOKIE_BANNERS, NONE
}
