/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.redesign.view

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.PrimaryButton
import mozilla.components.lib.state.ext.observeAsState
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.fenix.R
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.onboarding.view.ToolbarOption
import org.mozilla.fenix.onboarding.view.ToolbarOptionType
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The default ratio of the image height to the parent height.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_DEFAULT = 1.0f

/**
 * The ratio of the image height to the parent height for medium sized devices.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_MEDIUM = 0.7f

/**
 * The ratio of the image height to the parent height for small devices like Nexus 4, Nexus 1.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_SMALL = 0.4f

/**
 * The default ratio of the image height to the parent height.
 */
private const val IMAGE_HEIGHT_RATIO_DEFAULT = 0.4f

/**
 * The ratio of the image height to the parent height for medium sized devices.
 */
private const val IMAGE_HEIGHT_RATIO_MEDIUM = 0.3f

/**
 * The ratio of the image height to the parent height for small devices.
 */
private const val IMAGE_HEIGHT_RATIO_SMALL = 0.2f

/**
 * This is the width of the 'select toolbar' image resources.
 */
private val imageResourceWidth = 56.dp

/**
 * A Composable for displaying toolbar placement onboarding page content.
 *
 * @param onboardingStore The [OnboardingStore] that holds the toolbar selection state.
 * @param pageState The page content that's displayed.
 * @param onToolbarSelectionClicked Callback for when a toolbar selection is clicked.
 */
@Composable
fun ToolbarOnboardingPageRedesign(
    onboardingStore: OnboardingStore,
    pageState: OnboardingPageState,
    onToolbarSelectionClicked: (ToolbarOptionType) -> Unit,
) {
    BoxWithConstraints {
        val boxWithConstraintsScope = this
        // Base
        Column(
            modifier = Modifier
                .background(FirefoxTheme.colors.layer1)
                .padding(horizontal = 16.dp, vertical = 24.dp)
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.SpaceBetween,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            with(pageState) {
                // Main content group
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Spacer(Modifier)

                    Image(
                        painter = painterResource(id = imageRes),
                        contentDescription = stringResource(
                            R.string.onboarding_customize_toolbar_main_image_content_description,
                        ),
                        modifier = Modifier.height(mainImageHeight(boxWithConstraintsScope)),
                    )

                    Spacer(Modifier.height(32.dp))

                    Text(
                        text = title,
                        color = FirefoxTheme.colors.textPrimary,
                        textAlign = TextAlign.Center,
                        style = FirefoxTheme.typography.headline5,
                    )

                    Spacer(Modifier.height(16.dp))

                    Text(
                        text = description,
                        color = FirefoxTheme.colors.textPrimary,
                        textAlign = TextAlign.Center,
                        style = FirefoxTheme.typography.body2,
                    )

                    Spacer(Modifier.height(34.dp))

                    val state by onboardingStore.observeAsState(initialValue = onboardingStore.state) { state -> state }

                    Row(Modifier.width(176.dp), horizontalArrangement = Arrangement.Center) {
                        toolbarOptions?.let {
                            ToolbarOptions(
                                boxWithConstraintsScope = boxWithConstraintsScope,
                                options = it,
                                selectedOption = state.toolbarOptionSelected,
                                onClick = onToolbarSelectionClicked,
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                PrimaryButton(
                    text = primaryButton.text,
                    modifier = Modifier
                        .width(width = FirefoxTheme.layout.size.maxWidth.small)
                        .semantics { testTag = title + "onboarding_card.positive_button" },
                    onClick = primaryButton.onClick,
                )
            }
        }

        LaunchedEffect(pageState) {
            pageState.onRecordImpressionEvent()
        }
    }
}

@Composable
private fun ToolbarOptions(
    boxWithConstraintsScope: BoxWithConstraintsScope,
    options: List<ToolbarOption>,
    selectedOption: ToolbarOptionType,
    onClick: (ToolbarOptionType) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
    ) {
        options.forEach {
            SelectableImageItem(
                boxWithConstraintsScope = boxWithConstraintsScope,
                toolbarOption = it,
                selectedOption = selectedOption,
                onClick = onClick,
            )

            if (it != options.last()) {
                Spacer(Modifier.width(26.dp))
            }
        }
    }
}

@Composable
private fun SelectableImageItem(
    boxWithConstraintsScope: BoxWithConstraintsScope,
    toolbarOption: ToolbarOption,
    selectedOption: ToolbarOptionType,
    onClick: (ToolbarOptionType) -> Unit,
) {
    val isSelectedOption = toolbarOption.toolbarType == selectedOption

    val toolbarImageContentDescriptionResId = when (toolbarOption.toolbarType) {
        ToolbarOptionType.TOOLBAR_BOTTOM -> R.string.onboarding_customize_toolbar_placement_bottom_content_description
        ToolbarOptionType.TOOLBAR_TOP -> R.string.onboarding_customize_toolbar_placement_top_content_description
    }

    Column(
        modifier = Modifier
            .clickable(
                onClickLabel = stringResource(R.string.onboarding_customize_toolbar_a11y_action_label_select),
                onClick = {
                    // Only call action if option selected is different.
                    if (!isSelectedOption) {
                        onClick(toolbarOption.toolbarType)
                    }
                },
            )
            .semantics {
                selected = isSelectedOption
                role = Role.RadioButton
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            painter = painterResource(id = toolbarOption.imageRes),
            contentDescription = stringResource(toolbarImageContentDescriptionResId),
            modifier = if (isSelectedOption) {
                val borderModifier = borderSize(boxWithConstraintsScope)
                Modifier
                    .width(optionImageWidth(boxWithConstraintsScope))
                    .border(
                        2.dp,
                        FirefoxTheme.colors.actionPrimary,
                        RoundedCornerShape(borderModifier),
                    )
            } else {
                Modifier.width(optionImageWidth(boxWithConstraintsScope))
            },
        )

        Text(
            text = toolbarOption.label,
            color = FirefoxTheme.colors.textPrimary,
            modifier = Modifier.padding(vertical = 6.dp),
            style = FirefoxTheme.typography.caption,
        )

        if (isSelectedOption) {
            Box(
                modifier = Modifier
                    .background(
                        color = FirefoxTheme.colors.layerAccent,
                        shape = CircleShape,
                    )
                    .size(24.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = R.drawable.mozac_ic_checkmark_24),
                    contentDescription = null, // decorative only.
                    modifier = Modifier.size(12.dp),
                    tint = PhotonColors.White,
                )
            }
        } else {
            Box(
                modifier = Modifier
                    .size(24.dp)
                    .border(2.dp, FirefoxTheme.colors.actionSecondary, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
            }
        }
    }
}

private fun mainImageHeight(boxWithConstraintsScope: BoxWithConstraintsScope): Dp {
    val imageHeightRatio: Float = when {
        boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> IMAGE_HEIGHT_RATIO_SMALL
        boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> IMAGE_HEIGHT_RATIO_MEDIUM
        else -> IMAGE_HEIGHT_RATIO_DEFAULT
    }
    return boxWithConstraintsScope.maxHeight.times(imageHeightRatio)
}

private fun borderSize(boxWithConstraintsScope: BoxWithConstraintsScope) = when {
    boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> 4.dp
    boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> 7.dp
    else -> 10.dp
}

private fun optionImageWidth(boxWithConstraintsScope: BoxWithConstraintsScope): Dp {
    val imageHeightRatio: Float = when {
        boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> OPTION_IMAGE_HEIGHT_RATIO_SMALL
        boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> OPTION_IMAGE_HEIGHT_RATIO_MEDIUM
        else -> OPTION_IMAGE_HEIGHT_RATIO_DEFAULT
    }
    return imageResourceWidth.times(imageHeightRatio)
}

// *** Code below used for previews only *** //

@FlexibleWindowLightDarkPreview
@Composable
private fun OnboardingPagePreview() {
    FirefoxTheme {
        ToolbarOnboardingPageRedesign(
            onboardingStore = OnboardingStore(),
            pageState = OnboardingPageState(
                imageRes = R.drawable.ic_onboarding_customize_toolbar,
                title = stringResource(id = R.string.onboarding_customize_toolbar_title),
                description = stringResource(id = R.string.onboarding_customize_toolbar_description),
                primaryButton = Action(
                    text = stringResource(
                        id = R.string.onboarding_save_and_start_button,
                    ),
                    onClick = {},
                ),
                toolbarOptions = listOf(
                    ToolbarOption(
                        toolbarType = ToolbarOptionType.TOOLBAR_TOP,
                        imageRes = R.drawable.ic_onboarding_top_toolbar,
                        label = stringResource(R.string.onboarding_customize_toolbar_top_option),
                    ),
                    ToolbarOption(
                        toolbarType = ToolbarOptionType.TOOLBAR_BOTTOM,
                        imageRes = R.drawable.ic_onboarding_bottom_toolbar,
                        label = stringResource(R.string.onboarding_customize_toolbar_bottom_option),
                    ),
                ),
                onRecordImpressionEvent = {},
            ),
            onToolbarSelectionClicked = {},
        )
    }
}
