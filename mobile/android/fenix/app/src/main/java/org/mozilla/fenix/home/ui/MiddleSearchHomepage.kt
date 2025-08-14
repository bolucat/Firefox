/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.home.interactor.HomepageInteractor
import org.mozilla.fenix.home.pocket.ui.PocketSection
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Top level composable for the middle search homepage.
 *
 * @param state State representing the homepage.
 * @param interactor [HomepageInteractor] for interactions with the homepage UI.
 * @param onMiddleSearchBarVisibilityChanged Invoked when the middle search is shown/hidden.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 */
@Suppress("LongMethod", "CyclomaticComplexMethod")
@Composable
internal fun MiddleSearchHomepage(
    state: HomepageState,
    interactor: HomepageInteractor,
    onMiddleSearchBarVisibilityChanged: (isVisible: Boolean) -> Unit,
    onTopSitesItemBound: () -> Unit,
) {
    val scrollState = rememberScrollState()

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize(),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .semantics {
                    testTagsAsResourceId = true
                    testTag = HOMEPAGE
                }
                .pointerInput(state.isSearchInProgress) {
                    if (state.isSearchInProgress) {
                        awaitPointerEventScope {
                            interactor.onHomeContentFocusedWhileSearchIsActive()
                        }
                    }
                }
                .verticalScroll(scrollState),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(40.dp))

            if (state.firstFrameDrawn) {
                with(state) {
                    when (this) {
                        is HomepageState.Private -> {
                            Box(modifier = Modifier.padding(horizontal = horizontalMargin)) {
                                PrivateBrowsingDescription(
                                    onLearnMoreClick = interactor::onLearnMoreClicked,
                                )
                            }
                        }

                        is HomepageState.Normal -> {
                            if (showTopSites) {
                                TopSitesSection(
                                    topSites = topSites,
                                    topSiteColors = topSiteColors,
                                    interactor = interactor,
                                    onTopSitesItemBound = onTopSitesItemBound,
                                )
                            }

                            Spacer(modifier = Modifier.weight(1f))

                            LaunchedEffect(key1 = searchBarEnabled, key2 = searchBarVisible) {
                                onMiddleSearchBarVisibilityChanged(searchBarEnabled && searchBarVisible)
                            }

                            if (searchBarEnabled && searchBarVisible) {
                                SearchBar(
                                    modifier = Modifier
                                        .padding(horizontal = horizontalMargin)
                                        .graphicsLayer { this.alpha = alpha },
                                    onClick = interactor::onNavigateSearch,
                                )
                            }

                            Spacer(modifier = Modifier.weight(1f))

                            if (showPocketStories) {
                                PocketSection(
                                    state = pocketState,
                                    cardBackgroundColor = cardBackgroundColor,
                                    interactor = interactor,
                                )
                            }

                            Spacer(Modifier.height(state.bottomSpacerHeight))

                            Spacer(Modifier.height(47.dp))
                        }
                    }
                }
            }
        }

        if (state.isSearchInProgress) {
            Scrim(onDismiss = interactor::onHomeContentFocusedWhileSearchIsActive)
        }
    }
}

@Composable
private fun Scrim(onDismiss: () -> Unit) {
    Box(
        modifier = Modifier
            .background(FirefoxTheme.colors.layerScrim.copy(alpha = 0.75f))
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures(onTap = { onDismiss() })
            },
    )
}
