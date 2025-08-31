/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.rememberNestedScrollInteropConnection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopupLayout
import mozilla.components.compose.cfr.CFRPopupProperties
import org.mozilla.fenix.components.appstate.OrientationMode
import org.mozilla.fenix.compose.BottomSheetHandle
import org.mozilla.fenix.theme.FirefoxTheme

private const val CFR_HORIZONTAL_OFFSET = 160
private const val CFR_VERTICAL_OFFSET_LANDSCAPE = 0
private const val CFR_VERTICAL_OFFSET_PORTRAIT = -6

/**
 * The menu dialog bottom sheet.
 *
 * @param modifier [Modifier] to be applied to [BottomSheetHandle].
 * @param onRequestDismiss Invoked when when accessibility services or UI automation requests
 * dismissal of the bottom sheet.
 * @param handlebarContentDescription Bottom sheet handlebar content description.
 * @param isMenuDragBarDark Whether or not the menu's drag bar background should be dark.
 * @param cornerShape The shape of the bottom sheet's top corners.
 * @param handleColor The color of the handle.
 * @param handleCornerRadius The corner radius of the handle.
 * @param menuCfrState An optional [MenuCFRState] that describes how to display a
 * contextual feature recommendation (CFR) popup in the menu.
 * @param content The children composable to be laid out.
 */
@Composable
fun MenuDialogBottomSheet(
    modifier: Modifier = Modifier,
    onRequestDismiss: () -> Unit,
    handlebarContentDescription: String,
    isMenuDragBarDark: Boolean = false,
    cornerShape: Shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp),
    handleColor: Color = FirefoxTheme.colors.borderInverted,
    handleCornerRadius: CornerRadius = CornerRadius.Zero,
    menuCfrState: MenuCFRState? = null,
    content: @Composable () -> Unit,
) {
    Column(
        modifier = Modifier
            .background(
                color = FirefoxTheme.colors.layer1,
                shape = cornerShape,
            )
            .nestedScroll(rememberNestedScrollInteropConnection()),
    ) {
        if (menuCfrState?.showCFR == true) {
            CFRBottomSheetHandle(
                modifier = modifier,
                state = menuCfrState,
                onRequestDismiss = onRequestDismiss,
                contentDescription = handlebarContentDescription,
                isMenuDragBarDark = isMenuDragBarDark,
                cornerShape = cornerShape,
                handleColor = handleColor,
                handleCornerRadius = handleCornerRadius,
            )
        } else {
            MenuBottomSheetHandle(
                modifier = modifier,
                onRequestDismiss = onRequestDismiss,
                contentDescription = handlebarContentDescription,
                isMenuDragBarDark = isMenuDragBarDark,
                cornerShape = cornerShape,
                color = handleColor,
                cornerRadius = handleCornerRadius,
            )
        }

        content()
    }
}

@Composable
private fun MenuBottomSheetHandle(
    modifier: Modifier = Modifier,
    onRequestDismiss: () -> Unit,
    contentDescription: String,
    isMenuDragBarDark: Boolean = false,
    cornerShape: Shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp),
    color: Color = FirefoxTheme.colors.borderInverted,
    cornerRadius: CornerRadius = CornerRadius.Zero,
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                color = if (isMenuDragBarDark) {
                    FirefoxTheme.colors.layerSearch
                } else {
                    Color.Transparent
                },
                shape = cornerShape,
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        BottomSheetHandle(
            onRequestDismiss = onRequestDismiss,
            contentDescription = contentDescription,
            modifier = modifier,
            cornerRadius = cornerRadius,
            color = color,
        )
    }
}

/**
 * A handle present on top of a bottom sheet that will also serves as a anchor for the CFR.
 */
@Composable
private fun CFRBottomSheetHandle(
    modifier: Modifier = Modifier,
    state: MenuCFRState,
    contentDescription: String,
    onRequestDismiss: () -> Unit,
    isMenuDragBarDark: Boolean,
    cornerShape: Shape = RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp),
    handleColor: Color = FirefoxTheme.colors.borderInverted,
    handleCornerRadius: CornerRadius = CornerRadius.Zero,
) {
    val (indicatorDirection, verticalOffset) = when (state.orientation) {
        OrientationMode.Landscape -> CFRPopup.IndicatorDirection.UP to CFR_VERTICAL_OFFSET_LANDSCAPE
        else -> CFRPopup.IndicatorDirection.DOWN to CFR_VERTICAL_OFFSET_PORTRAIT
    }

    CFRPopupLayout(
        showCFR = state.showCFR,
        properties = CFRPopupProperties(
            popupAlignment = CFRPopup.PopupAlignment.INDICATOR_CENTERED_IN_ANCHOR,
            popupBodyColors = listOf(
                FirefoxTheme.colors.layerGradientEnd.toArgb(),
                FirefoxTheme.colors.layerGradientStart.toArgb(),
            ),
            dismissButtonColor = FirefoxTheme.colors.iconOnColor.toArgb(),
            indicatorDirection = indicatorDirection,
            popupVerticalOffset = verticalOffset.dp,
            indicatorArrowStartOffset = CFR_HORIZONTAL_OFFSET.dp,
        ),
        onCFRShown = state.onShown,
        onDismiss = state.onDismiss,
        title = {
            FirefoxTheme {
                Text(
                    text = stringResource(id = state.titleRes),
                    color = FirefoxTheme.colors.textOnColorPrimary,
                    style = FirefoxTheme.typography.subtitle2,
                )
            }
        },
        text = {
            FirefoxTheme {
                Text(
                    text = stringResource(id = state.messageRes),
                    color = FirefoxTheme.colors.textOnColorPrimary,
                    style = FirefoxTheme.typography.body2,
                )
            }
        },
    ) {
        MenuBottomSheetHandle(
            modifier = modifier,
            onRequestDismiss = onRequestDismiss,
            contentDescription = contentDescription,
            isMenuDragBarDark = isMenuDragBarDark,
            cornerShape = cornerShape,
            color = handleColor,
            cornerRadius = handleCornerRadius,
        )
    }
}

/**
 * State object that describe the contextual feature recommendation (CFR) popup in the menu.
 *
 * @property showCFR Whether or not to display the CFR.
 * @property titleRes The string resource ID of the title to display in the CFR.
 * @property messageRes The string resource ID of the message to display in the CFR body.
 * @property orientation The [OrientationMode] of the device.
 * @property onShown Invoked when the CFR is shown.
 * @property onDismiss Invoked when the CFR is dismissed. Returns true if the dismissal was
 * explicit (e.g. user clicked on the "X" close button).
 */
data class MenuCFRState(
    val showCFR: Boolean,
    @param:StringRes val titleRes: Int,
    @param:StringRes val messageRes: Int,
    val orientation: OrientationMode,
    val onShown: () -> Unit,
    val onDismiss: (Boolean) -> Unit,
)
