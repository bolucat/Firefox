/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket

import androidx.navigation.NavController
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkStatic
import io.mockk.spyk
import io.mockk.verify
import io.mockk.verifyOrder
import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.PocketSponsoredStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import mozilla.components.service.pocket.PocketStory.SponsoredContentCallbacks
import mozilla.components.service.pocket.ext.getCurrentFlightImpressions
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.Pocket
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.mars.MARSUseCases
import org.mozilla.fenix.home.pocket.controller.DefaultPocketStoriesController
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import java.lang.ref.WeakReference

@RunWith(RobolectricTestRunner::class) // For gleanTestRule
class DefaultPocketStoriesControllerTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private val navController: NavController = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val marsUseCases: MARSUseCases = mockk(relaxed = true)

    private val scope = coroutinesTestRule.scope

    @Test
    fun `GIVEN a category is selected WHEN that same category is clicked THEN deselect it and record telemetry`() {
        val category1 = PocketRecommendedStoriesCategory("cat1", emptyList())
        val category2 = PocketRecommendedStoriesCategory("cat2", emptyList())
        val selections = listOf(PocketRecommendedStoriesSelectedCategory(category2.name))
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategories = listOf(category1, category2),
                        pocketStoriesCategoriesSelections = selections,
                    ),
                ),
            ),
        )
        val controller = createController(appStore = store)
        assertNull(Pocket.homeRecsCategoryClicked.testGetValue())

        controller.handleCategoryClick(category2)
        verify(exactly = 0) { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(category2.name)) }
        verify { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(category2.name)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(category2.name, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("deselected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("1", event.single().extra!!["selected_total"])
    }

    @Test
    fun `GIVEN 8 categories are selected WHEN when a new one is clicked THEN the oldest selected is deselected before selecting the new one and record telemetry`() {
        val category1 = PocketRecommendedStoriesSelectedCategory(name = "cat1", selectionTimestamp = 111)
        val category2 = PocketRecommendedStoriesSelectedCategory(name = "cat2", selectionTimestamp = 222)
        val category3 = PocketRecommendedStoriesSelectedCategory(name = "cat3", selectionTimestamp = 333)
        val oldestSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "oldestSelectedCategory", selectionTimestamp = 0)
        val category4 = PocketRecommendedStoriesSelectedCategory(name = "cat4", selectionTimestamp = 444)
        val category5 = PocketRecommendedStoriesSelectedCategory(name = "cat5", selectionTimestamp = 555)
        val category6 = PocketRecommendedStoriesSelectedCategory(name = "cat6", selectionTimestamp = 678)
        val category7 = PocketRecommendedStoriesSelectedCategory(name = "cat7", selectionTimestamp = 890)
        val newSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "newSelectedCategory", selectionTimestamp = 654321)
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategoriesSelections = listOf(
                            category1,
                            category2,
                            category3,
                            category4,
                            category5,
                            category6,
                            category7,
                            oldestSelectedCategory,
                        ),
                    ),
                ),
            ),
        )
        val controller = createController(appStore = store)
        assertNull(Pocket.homeRecsCategoryClicked.testGetValue())

        controller.handleCategoryClick(PocketRecommendedStoriesCategory(newSelectedCategory.name))

        verify { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(oldestSelectedCategory.name)) }
        verify { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(newSelectedCategory.name)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(newSelectedCategory.name, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("selected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("8", event.single().extra!!["selected_total"])
    }

    @Test
    fun `GIVEN fewer than 8 categories are selected WHEN when a new one is clicked THEN don't deselect anything but select the newly clicked category and record telemetry`() {
        val category1 = PocketRecommendedStoriesSelectedCategory(name = "cat1", selectionTimestamp = 111)
        val category2 = PocketRecommendedStoriesSelectedCategory(name = "cat2", selectionTimestamp = 222)
        val category3 = PocketRecommendedStoriesSelectedCategory(name = "cat3", selectionTimestamp = 333)
        val oldestSelectedCategory = PocketRecommendedStoriesSelectedCategory(name = "oldestSelectedCategory", selectionTimestamp = 0)
        val category4 = PocketRecommendedStoriesSelectedCategory(name = "cat4", selectionTimestamp = 444)
        val category5 = PocketRecommendedStoriesSelectedCategory(name = "cat5", selectionTimestamp = 555)
        val category6 = PocketRecommendedStoriesSelectedCategory(name = "cat6", selectionTimestamp = 678)
        val store = spyk(
            AppStore(
                AppState(
                    recommendationState = ContentRecommendationsState(
                        pocketStoriesCategoriesSelections = listOf(
                            category1,
                            category2,
                            category3,
                            category4,
                            category5,
                            category6,
                            oldestSelectedCategory,
                        ),
                    ),
                ),
            ),
        )
        val newSelectedCategoryName = "newSelectedCategory"
        val controller = createController(appStore = store)

        controller.handleCategoryClick(PocketRecommendedStoriesCategory(newSelectedCategoryName))

        verify(exactly = 0) { store.dispatch(ContentRecommendationsAction.DeselectPocketStoriesCategory(oldestSelectedCategory.name)) }
        verify { store.dispatch(ContentRecommendationsAction.SelectPocketStoriesCategory(newSelectedCategoryName)) }

        assertNotNull(Pocket.homeRecsCategoryClicked.testGetValue())
        val event = Pocket.homeRecsCategoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("category_name"))
        assertEquals(newSelectedCategoryName, event.single().extra!!["category_name"])
        assertTrue(event.single().extra!!.containsKey("new_state"))
        assertEquals("selected", event.single().extra!!["new_state"])
        assertTrue(event.single().extra!!.containsKey("selected_total"))
        assertEquals("7", event.single().extra!!["selected_total"])
    }

    @Test
    fun `WHEN a new recommended story is shown THEN update the State`() {
        val store = spyk(AppStore())
        val controller = createController(appStore = store)
        val storyShown: PocketRecommendedStory = mockk()
        val storyPosition = Triple(1, 2, 3)

        controller.handleStoryShown(storyShown, storyPosition)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = storyShown, position = 3),
                    ),
                ),
            )
        }
    }

    @Test
    fun `WHEN a new sponsored story is shown THEN update the State and record telemetry`() {
        val store = spyk(AppStore())
        val controller = createController(appStore = store)
        val storyShown: PocketSponsoredStory = mockk {
            every { shim.click } returns "testClickShim"
            every { shim.impression } returns "testImpressionShim"
            every { id } returns 123
        }
        var wasPingSent = false
        mockkStatic("mozilla.components.service.pocket.ext.PocketStoryKt") {
            // Simulate that the story was already shown 3 times.
            every { storyShown.getCurrentFlightImpressions() } returns listOf(2L, 3L, 7L)
            // Test that the spoc ping is immediately sent with the needed data.
            Pings.spoc.testBeforeNextSubmit { reason ->
                assertEquals(storyShown.shim.impression, Pocket.spocShim.testGetValue())
                assertEquals(Pings.spocReasonCodes.impression.name, reason?.name)
                wasPingSent = true
            }

            controller.handleStoryShown(storyShown, storyPosition = Triple(1, 2, 3))

            verify {
                store.dispatch(
                    ContentRecommendationsAction.PocketStoriesShown(
                        impressions = listOf(
                            PocketImpression(story = storyShown, position = 3),
                        ),
                    ),
                )
            }
            assertNotNull(Pocket.homeRecsSpocShown.testGetValue())
            assertEquals(1, Pocket.homeRecsSpocShown.testGetValue()!!.size)
            val data = Pocket.homeRecsSpocShown.testGetValue()!!.single().extra
            assertEquals("123", data?.entries?.first { it.key == "spoc_id" }?.value)
            assertEquals("1x2", data?.entries?.first { it.key == "position" }?.value)
            assertEquals("4", data?.entries?.first { it.key == "times_shown" }?.value)
            assertTrue(wasPingSent)
        }
    }

    @Test
    fun `WHEN a sponsored content is shown THEN update the State and record telemetry`() {
        val store = spyk(AppStore())
        val controller = createController(appStore = store)
        val sponsoredContent = SponsoredContent(
            url = "https://firefox.com",
            title = "Firefox",
            callbacks = SponsoredContentCallbacks(
                clickUrl = "https://firefox.com/click",
                impressionUrl = "https://firefox.com/impression",
            ),
            imageUrl = "https://test.com/image1.jpg",
            domain = "firefox.com",
            excerpt = "Mozilla Firefox",
            sponsor = "Mozilla",
            blockKey = "1",
            caps = mockk(relaxed = true),
            priority = 3,
        )

        mockkStatic("mozilla.components.service.pocket.ext.PocketStoryKt") {
            every { sponsoredContent.getCurrentFlightImpressions() } returns listOf(2L, 3L, 7L)

            controller.handleStoryShown(sponsoredContent, storyPosition = Triple(1, 2, 3))

            assertEquals(1, Pocket.homeRecsSpocShown.testGetValue()!!.size)

            val data = Pocket.homeRecsSpocShown.testGetValue()!!.single().extra
            assertEquals(2, data?.size)
            assertEquals("1x2", data?.entries?.first { it.key == "position" }?.value)
            assertEquals("4", data?.entries?.first { it.key == "times_shown" }?.value)

            verify {
                store.dispatch(
                    ContentRecommendationsAction.PocketStoriesShown(
                        impressions = listOf(
                            PocketImpression(story = sponsoredContent, position = 3),
                        ),
                    ),
                )
            }
        }
    }

    @Test
    fun `WHEN new stories are shown THEN update the State and record telemetry`() {
        val store = spyk(AppStore())
        val controller = createController(appStore = store)
        val recommendation = mockk<ContentRecommendation>()
        val story = mockk<PocketStory>()
        val sponsoredStory = mockk<PocketSponsoredStory>()
        val storiesShown = listOf(recommendation, story, sponsoredStory)

        assertNull(Pocket.homeRecsShown.testGetValue())

        controller.handleStoriesShown(storiesShown)

        verify {
            store.dispatch(
                ContentRecommendationsAction.PocketStoriesShown(
                    impressions = listOf(
                        PocketImpression(story = recommendation, position = 0),
                        PocketImpression(story = story, position = 1),
                    ),
                ),
            )
        }

        assertNotNull(Pocket.homeRecsShown.testGetValue())
        assertEquals(1, Pocket.homeRecsShown.testGetValue()!!.size)
        assertNull(Pocket.homeRecsShown.testGetValue()!!.single().extra)
    }

    @Test
    fun `WHEN a recommended story is clicked THEN open that story's url using HomeActivity and record telemetry`() {
        val story = PocketRecommendedStory(
            title = "",
            url = "testLink",
            imageUrl = "",
            publisher = "",
            category = "",
            timeToRead = 0,
            timesShown = 123,
        )
        val controller = createController()
        assertNull(Pocket.homeRecsStoryClicked.testGetValue())

        controller.handleStoryClicked(story, storyPosition = Triple(1, 2, 3))

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = true,
                private = false,
            )
        }

        assertNotNull(Pocket.homeRecsStoryClicked.testGetValue())
        val event = Pocket.homeRecsStoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("position"))
        assertEquals("1x2", event.single().extra!!["position"])
        assertTrue(event.single().extra!!.containsKey("times_shown"))
        assertEquals(story.timesShown.inc().toString(), event.single().extra!!["times_shown"])
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a recommended story is clicked THEN open that story's url using HomeActivity and record telemetry`() {
        every { settings.enableHomepageAsNewTab } returns true

        val story = PocketRecommendedStory(
            title = "",
            url = "testLink",
            imageUrl = "",
            publisher = "",
            category = "",
            timeToRead = 0,
            timesShown = 123,
        )
        val controller = createController()
        assertNull(Pocket.homeRecsStoryClicked.testGetValue())

        controller.handleStoryClicked(story, storyPosition = Triple(1, 2, 3))

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = false,
                private = false,
            )
        }

        assertNotNull(Pocket.homeRecsStoryClicked.testGetValue())
        val event = Pocket.homeRecsStoryClicked.testGetValue()!!
        assertEquals(1, event.size)
        assertTrue(event.single().extra!!.containsKey("position"))
        assertEquals("1x2", event.single().extra!!["position"])
        assertTrue(event.single().extra!!.containsKey("times_shown"))
        assertEquals(story.timesShown.inc().toString(), event.single().extra!!["times_shown"])
    }

    @Test
    fun `WHEN a sponsored story is clicked THEN open that story's url using HomeActivity and record telemetry`() {
        val storyClicked = PocketSponsoredStory(
            id = 7,
            title = "",
            url = "testLink",
            imageUrl = "",
            sponsor = "",
            shim = mockk {
                every { click } returns "testClickShim"
                every { impression } returns "testImpressionShim"
            },
            priority = 3,
            caps = mockk(relaxed = true),
        )
        val controller = createController()
        var wasPingSent = false
        assertNull(Pocket.homeRecsSpocClicked.testGetValue())
        mockkStatic("mozilla.components.service.pocket.ext.PocketStoryKt") {
            // Simulate that the story was already shown 2 times.
            every { storyClicked.getCurrentFlightImpressions() } returns listOf(2L, 3L)
            // Test that the spoc ping is immediately sent with the needed data.
            Pings.spoc.testBeforeNextSubmit { reason ->
                assertEquals(storyClicked.shim.click, Pocket.spocShim.testGetValue())
                assertEquals(Pings.spocReasonCodes.click.name, reason?.name)
                wasPingSent = true
            }

            controller.handleStoryClicked(storyClicked, storyPosition = Triple(2, 3, 4))

            verify {
                navController.navigate(R.id.browserFragment)
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = storyClicked.url,
                    newTab = true,
                    private = false,
                )
            }

            assertNotNull(Pocket.homeRecsSpocClicked.testGetValue())
            assertEquals(1, Pocket.homeRecsSpocClicked.testGetValue()!!.size)
            val data = Pocket.homeRecsSpocClicked.testGetValue()!!.single().extra
            assertEquals("7", data?.entries?.first { it.key == "spoc_id" }?.value)
            assertEquals("2x3", data?.entries?.first { it.key == "position" }?.value)
            assertEquals("3", data?.entries?.first { it.key == "times_shown" }?.value)
            assertTrue(wasPingSent)
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a sponsored story is clicked THEN navigate to the sponsored story's url and record the interaction`() {
        every { settings.enableHomepageAsNewTab } returns true

        val storyClicked = PocketSponsoredStory(
            id = 7,
            title = "",
            url = "testLink",
            imageUrl = "",
            sponsor = "",
            shim = mockk {
                every { click } returns "testClickShim"
                every { impression } returns "testImpressionShim"
            },
            priority = 3,
            caps = mockk(relaxed = true),
        )
        val controller = createController()
        var wasPingSent = false
        assertNull(Pocket.homeRecsSpocClicked.testGetValue())
        mockkStatic("mozilla.components.service.pocket.ext.PocketStoryKt") {
            // Simulate that the story was already shown 2 times.
            every { storyClicked.getCurrentFlightImpressions() } returns listOf(2L, 3L)
            // Test that the spoc ping is immediately sent with the needed data.
            Pings.spoc.testBeforeNextSubmit { reason ->
                assertEquals(storyClicked.shim.click, Pocket.spocShim.testGetValue())
                assertEquals(Pings.spocReasonCodes.click.name, reason?.name)
                wasPingSent = true
            }

            controller.handleStoryClicked(storyClicked, storyPosition = Triple(2, 3, 4))

            verify {
                navController.navigate(R.id.browserFragment)
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = storyClicked.url,
                    newTab = false,
                    private = false,
                )
            }

            assertNotNull(Pocket.homeRecsSpocClicked.testGetValue())
            assertEquals(1, Pocket.homeRecsSpocClicked.testGetValue()!!.size)
            val data = Pocket.homeRecsSpocClicked.testGetValue()!!.single().extra
            assertEquals("7", data?.entries?.first { it.key == "spoc_id" }?.value)
            assertEquals("2x3", data?.entries?.first { it.key == "position" }?.value)
            assertEquals("3", data?.entries?.first { it.key == "times_shown" }?.value)
            assertTrue(wasPingSent)
        }
    }

    @Test
    fun `WHEN a sponsored content is clicked THEN navigate to the sponsored content URL and record the interaction`() {
        val sponsoredContent = SponsoredContent(
            url = "https://firefox.com",
            title = "Firefox",
            callbacks = SponsoredContentCallbacks(
                clickUrl = "https://firefox.com/click",
                impressionUrl = "https://firefox.com/impression",
            ),
            imageUrl = "https://test.com/image1.jpg",
            domain = "firefox.com",
            excerpt = "Mozilla Firefox",
            sponsor = "Mozilla",
            blockKey = "1",
            caps = mockk(relaxed = true),
            priority = 3,
        )
        val controller = createController()

        assertNull(Pocket.homeRecsSpocClicked.testGetValue())

        mockkStatic("mozilla.components.service.pocket.ext.PocketStoryKt") {
            every { sponsoredContent.getCurrentFlightImpressions() } returns listOf(2L, 3L)

            controller.handleStoryClicked(sponsoredContent, storyPosition = Triple(2, 3, 4))

            assertEquals(1, Pocket.homeRecsSpocClicked.testGetValue()!!.size)
            val data = Pocket.homeRecsSpocClicked.testGetValue()!!.single().extra
            assertEquals(2, data?.size)
            assertEquals("2x3", data?.entries?.first { it.key == "position" }?.value)
            assertEquals("3", data?.entries?.first { it.key == "times_shown" }?.value)

            coVerify {
                navController.navigate(R.id.browserFragment)
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = sponsoredContent.url,
                    newTab = true,
                    private = false,
                )
                marsUseCases.recordInteraction(sponsoredContent.callbacks.clickUrl)
            }
        }
    }

    @Test
    fun `WHEN a story is clicked THEN its link is opened`() {
        val story = PocketRecommendedStory("", "url", "", "", "", 0, 0)
        val controller = createController()

        controller.handleStoryClicked(story, storyPosition = Triple(1, 2, 3))

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN a story is clicked THEN its link is opened`() {
        every { settings.enableHomepageAsNewTab } returns true

        val story = PocketRecommendedStory("", "url", "", "", "", 0, 0)
        val controller = createController()

        controller.handleStoryClicked(story, storyPosition = Triple(1, 2, 3))

        verifyOrder {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = story.url,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN the discover more button is clicked THEN navigate to the discover more stories screen`() {
        val controller = createController()

        controller.handleDiscoverMoreClicked()

        verify {
            navController.nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionHomeFragmentToStoriesFragment(),
            )
        }
    }

    private fun createController(
        appStore: AppStore = AppStore(),
    ) = DefaultPocketStoriesController(
        navControllerRef = WeakReference(navController),
        appStore = appStore,
        settings = settings,
        fenixBrowserUseCases = fenixBrowserUseCases,
        marsUseCases = marsUseCases,
        viewLifecycleScope = scope,
    )
}
