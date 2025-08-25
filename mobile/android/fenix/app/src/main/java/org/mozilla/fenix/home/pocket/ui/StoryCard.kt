/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.PocketSponsoredStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.Image
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.theme.FirefoxTheme
import kotlin.math.roundToInt

private val cardShape = RoundedCornerShape(8.dp)
private val defaultCardContentPadding = 8.dp
private val imageSize = 345.dp
private val imageHeight = 180.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun StoryCard(
    story: PocketStory,
    onClick: (story: PocketStory, position: Triple<Int, Int, Int>) -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(
        onClick = {
            onClick(story, Triple(0, 0, 0))
        },
        modifier = modifier,
        shape = cardShape,
        colors = CardDefaults.cardColors(containerColor = FirefoxTheme.colors.layer2),
    ) {
        Column(
            modifier = Modifier.padding(all = defaultCardContentPadding),
        ) {
            val imageUrl = story.imageUrl.replace(
                "{wh}",
                with(LocalDensity.current) {
                    "${imageSize.toPx().roundToInt()}x${
                        imageSize.toPx().roundToInt()
                    }"
                },
            )

            Image(
                url = imageUrl,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(imageHeight)
                    .clip(cardShape),
                private = false,
                targetSize = imageSize,
                contentScale = ContentScale.Crop,
            )

            Column(
                modifier = Modifier.padding(all = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    text = story.title,
                    color = FirefoxTheme.colors.textPrimary,
                    overflow = TextOverflow.Ellipsis,
                    style = FirefoxTheme.typography.headline7,
                )

                var subtitle = ""
                when (story) {
                    is ContentRecommendation -> {
                        subtitle = story.publisher
                    }

                    is SponsoredContent -> {
                        subtitle = story.sponsor
                    }

                    is PocketRecommendedStory,
                    is PocketSponsoredStory,
                    -> {
                        // no-op, don't handle these [PocketStory] types as they are no longer
                        // supported after the Merino recommendation migration.
                    }
                }

                if (subtitle.isNotEmpty()) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Favicon(url = story.url, size = 16.dp)

                        Spacer(modifier = Modifier.width(8.dp))

                        Text(
                            text = subtitle,
                            color = FirefoxTheme.colors.textSecondary,
                            overflow = TextOverflow.Ellipsis,
                            style = FirefoxTheme.typography.subtitle1,
                        )
                    }
                }
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun StoryCardPreview() {
    FirefoxTheme {
        Column(modifier = Modifier.padding(16.dp)) {
            StoryCard(
                story = FakeHomepagePreview.pocketStories(limit = 1).first(),
                onClick = { _, _ -> },
            )
        }
    }
}
