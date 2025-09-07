/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.theme

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.text.BasicText
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.modifier.thenConditional

/**
 * A custom typography for Acorn Theming.
 *
 * @property headline5 Currently not in-use.
 * @property headline6 Used for headings on Onboarding Modals and App Bar Titles.
 * @property headline7 Used for headings on Cards, Dialogs, Banners, and Homepage.
 * @property headline8 Used for Small Headings.
 * @property subtitle1 Used for Lists.
 * @property subtitle2 Currently not in-use.
 * @property body1 Currently not in-use.
 * @property body2 Used for body text.
 * @property button Used for Buttons.
 * @property caption Used for captions.
 * @property overline Used for Sheets.
 */
@Suppress("LongParameterList")
class AcornTypography(
    val headline5: TextStyle,
    val headline6: TextStyle,
    val headline7: TextStyle,
    val headline8: TextStyle,
    val subtitle1: TextStyle,
    val subtitle2: TextStyle,
    val body1: TextStyle,
    val body2: TextStyle,
    val button: TextStyle,
    val caption: TextStyle,
    val overline: TextStyle,
)

/**
 * A base [TextStyle] that ensures consistent line-height behavior across all Acorn typography tokens.
 *
 * This default style applies a centered line-height alignment and disables trimming so that
 * extra padding above and below text is preserved, which is matching Material 3’s line height handling.
 */
private val DefaultTextStyle = TextStyle.Default.copy(
    lineHeightStyle = LineHeightStyle(
        alignment = LineHeightStyle.Alignment.Center,
        trim = LineHeightStyle.Trim.None,
    ),
)

val defaultTypography = AcornTypography(
    headline5 = DefaultTextStyle.copy(
        fontSize = 24.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 0.18.sp,
        lineHeight = 32.sp,
    ),

    headline6 = DefaultTextStyle.copy(
        fontSize = 20.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = 0.15.sp,
        lineHeight = 24.sp,
    ),

    headline7 = DefaultTextStyle.copy(
        fontSize = 16.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = 0.15.sp,
        lineHeight = 24.sp,
    ),

    headline8 = DefaultTextStyle.copy(
        fontSize = 14.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = 0.4.sp,
        lineHeight = 20.sp,
    ),

    subtitle1 = DefaultTextStyle.copy(
        fontSize = 16.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 0.15.sp,
        lineHeight = 24.sp,
    ),

    subtitle2 = DefaultTextStyle.copy(
        fontSize = 14.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = 0.1.sp,
        lineHeight = 24.sp,
    ),

    body1 = DefaultTextStyle.copy(
        fontSize = 16.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 0.5.sp,
        lineHeight = 24.sp,
    ),

    body2 = DefaultTextStyle.copy(
        fontSize = 14.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 0.25.sp,
        lineHeight = 20.sp,
    ),

    button = DefaultTextStyle.copy(
        fontSize = 14.sp,
        fontWeight = FontWeight.W500,
        letterSpacing = 0.25.sp,
        lineHeight = 14.sp,
    ),

    caption = DefaultTextStyle.copy(
        fontSize = 12.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 0.4.sp,
        lineHeight = 16.sp,
    ),

    overline = DefaultTextStyle.copy(
        fontSize = 10.sp,
        fontWeight = FontWeight.W400,
        letterSpacing = 1.5.sp,
        lineHeight = 16.sp,
    ),
)

@Composable
@Preview
@Suppress("LongMethod")
private fun NewTypographyPreview() {
    val textStyles = listOf(
        Pair("Headline 5", defaultTypography.headline5),
        Pair("Headline 6", defaultTypography.headline6),
        Pair("Headline 7", defaultTypography.headline7),
        Pair("Headline 8", defaultTypography.headline8),
        Pair("Subtitle1", defaultTypography.subtitle1),
        Pair("Subtitle2", defaultTypography.subtitle2),
        Pair("Body1", defaultTypography.body1),
        Pair("Body2", defaultTypography.body2),
        Pair("Button", defaultTypography.button),
        Pair("Caption", defaultTypography.caption),
        Pair("Overline", defaultTypography.overline),
    )

    var textShadingEnabled by remember { mutableStateOf(true) }

    AcornTheme(colors = lightColorPalette) {
        Column(
            modifier = Modifier
            .width(200.dp)
            .background(AcornTheme.colors.layer1),

        ) {
            Row {
                Column(
                    modifier = Modifier.padding(start = 8.dp),
                ) {
                    Text(
                        text = "Line Height",
                        style = AcornTheme.typography.caption,
                        modifier = Modifier.background(AcornTheme.colors.layerInformation),
                    )

                    Spacer(Modifier.height(4.dp))

                    Text(
                        text = "Text Size",
                        style = AcornTheme.typography.caption,
                        modifier = Modifier.background(AcornTheme.colors.layerCritical),
                    )
                }

                Spacer(modifier = Modifier.size(8.dp))

                Switch(
                    checked = textShadingEnabled,
                    onCheckedChange = { textShadingEnabled = it },
                )
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp, horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
            }

            Column(
                modifier = Modifier
                    .background(AcornTheme.colors.layer1)
                    .padding(horizontal = 16.dp),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                for (index in 0..textStyles.size - 1) {
                    Row(
                        modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        LineHeightText(
                            text = textStyles[index].first,
                            style = textStyles[index].second,
                            textShadingEnabled = textShadingEnabled,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun LineHeightText(
    text: String,
    style: TextStyle,
    textShadingEnabled: Boolean,
) {
    val convertedTextSize: Dp = with(LocalDensity.current) {
        style.fontSize.toDp()
    }

    Box(
        modifier = Modifier
            .width(intrinsicSize = IntrinsicSize.Max)
            .thenConditional(modifier = Modifier.background(color = AcornTheme.colors.layerInformation)) {
                textShadingEnabled
            },
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(convertedTextSize)
                .thenConditional(modifier = Modifier.background(color = AcornTheme.colors.layerCritical)) {
                    textShadingEnabled
                }
                .align(alignment = Alignment.Center),
        )

        BasicText(
            text = text,
            style = style,
            modifier = Modifier.wrapContentWidth(),
        )
    }
}
