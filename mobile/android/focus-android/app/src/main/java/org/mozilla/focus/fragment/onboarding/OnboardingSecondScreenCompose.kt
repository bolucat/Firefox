/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment.onboarding

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.focus.R
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.ui.theme.focusTypography
import org.mozilla.focus.ui.theme.gradientBackground

private const val TOP_SPACER_WEIGHT = 1f

@Composable
@Preview
private fun OnBoardingSecondScreenComposePreview() {
    FocusTheme {
        OnBoardingSecondScreenCompose({}, {})
    }
}

/**
 * Displays the second onBoarding screen
 *
 * @param setAsDefaultBrowser Will be called when the user clicks on SetDefaultBrowser button.
 * @param skipScreen Will be called when the user clicks on Skip button.
 */
@Composable
@Suppress("LongMethod")
fun OnBoardingSecondScreenCompose(
    setAsDefaultBrowser: () -> Unit,
    skipScreen: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .gradientBackground(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.weight(TOP_SPACER_WEIGHT))

        Image(
            painter = painterResource(R.drawable.onboarding_second_screen_icon),
            contentDescription = LocalContext.current.getString(R.string.app_name),
            modifier = Modifier
                .size(200.dp, 300.dp)
                .weight(1f, false),
        )

        Text(
            text = stringResource(
                R.string.onboarding_second_screen_title,
                stringResource(R.string.onboarding_short_app_name),
            ),
            modifier = Modifier
                .padding(top = 32.dp, start = 16.dp, end = 16.dp),
            textAlign = TextAlign.Center,
            style = focusTypography.onboardingTitle,
        )

        Text(
            text = stringResource(
                R.string.onboarding_second_screen_subtitle_one,
            ),
            modifier = Modifier
                .padding(top = 16.dp, start = 16.dp, end = 16.dp),
            textAlign = TextAlign.Center,
            style = focusTypography.onboardingSubtitle,
        )

        Text(
            text = stringResource(
                R.string.onboarding_second_screen_subtitle_two,
                stringResource(R.string.onboarding_short_app_name),
            ),
            modifier = Modifier
                .padding(top = 16.dp, start = 16.dp, end = 16.dp),
            textAlign = TextAlign.Center,
            style = focusTypography.onboardingSubtitle,
        )

        ComponentOnBoardingSecondScreenButtons(setAsDefaultBrowser, skipScreen)
    }
}

@Composable
private fun ComponentOnBoardingSecondScreenButtons(
    setAsDefaultBrowser: () -> Unit,
    skipScreen: () -> Unit,
) {
    Button(
        onClick = setAsDefaultBrowser,
        modifier = Modifier
            .padding(top = 33.dp, start = 16.dp, end = 16.dp)
            .fillMaxWidth(),
        colors = ButtonDefaults.textButtonColors(
            containerColor = colorResource(R.color.onboardingButtonOneColor),
        ),
    ) {
        Text(
            text = AnnotatedString(stringResource(id = R.string.onboarding_second_screen_default_browser_button_text)),
            color = PhotonColors.White,
        )
    }
    Button(
        onClick = skipScreen,
        modifier = Modifier
            .padding(top = 8.dp, start = 16.dp, end = 16.dp, bottom = 74.dp)
            .fillMaxWidth(),
        colors = ButtonDefaults.textButtonColors(
            containerColor = colorResource(R.color.onboardingButtonTwoColor),
        ),
    ) {
        Text(
            text = AnnotatedString(stringResource(id = R.string.onboarding_second_screen_skip_button_text)),
            color = PhotonColors.Black,
        )
    }
}
