/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.controller

import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.TopSites
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Analytics
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.mars.MARSUseCases
import org.mozilla.fenix.home.topsites.ShortcutsFragmentDirections
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import java.lang.ref.WeakReference

@RunWith(RobolectricTestRunner::class) // For gleanTestRule
class DefaultTopSiteControllerTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private val activity: HomeActivity = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val tabsUseCases: TabsUseCases = mockk(relaxed = true)
    private val selectTabUseCase: TabsUseCases = mockk(relaxed = true)
    private val topSitesUseCases: TopSitesUseCases = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val marsUseCases: MARSUseCases = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val analytics: Analytics = mockk(relaxed = true)

    private val scope = coroutinesTestRule.scope

    private val searchEngine = SearchEngine(
        id = "test",
        name = "Test Engine",
        icon = mockk(relaxed = true),
        type = SearchEngine.Type.BUNDLED,
        resultUrls = listOf("https://example.org/?q={searchTerms}"),
    )

    private val googleSearchEngine = SearchEngine(
        id = "googleTest",
        name = "Google Test Engine",
        icon = mockk(relaxed = true),
        type = SearchEngine.Type.BUNDLED,
        resultUrls = listOf("https://www.google.com/?q={searchTerms}"),
        suggestUrl = "https://www.google.com/",
    )

    private val duckDuckGoSearchEngine = SearchEngine(
        id = "ddgTest",
        name = "DuckDuckGo Test Engine",
        icon = mockk(relaxed = true),
        type = SearchEngine.Type.BUNDLED,
        resultUrls = listOf("https://duckduckgo.com/?q=%7BsearchTerms%7D&t=fpas"),
        suggestUrl = "https://ac.duckduckgo.com/ac/?q=%7BsearchTerms%7D&type=list",
    )

    private lateinit var store: BrowserStore

    @Before
    fun setup() {
        store = BrowserStore(
            BrowserState(
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        every { navController.currentDestination } returns mockk {
            every { id } returns R.id.homeFragment
        }
        every { activity.components.analytics } returns analytics
    }

    @Test
    fun handleSelectDefaultTopSite() {
        val topSite = TopSite.Default(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openDefault.testGetValue())
        assertEquals(1, TopSites.openDefault.testGetValue()!!.size)
        assertNull(TopSites.openDefault.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectNonDefaultTopSite() {
        val topSite = TopSite.Frecent(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN Default TopSite is selected THEN open top site in existing tab`() {
        val topSite = TopSite.Default(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)
        every { settings.enableHomepageAsNewTab } returns true

        controller.handleSelectTopSite(topSite, position = 0)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = topSite.url,
                newTab = false,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN current destination is the shortcuts fragment WHEN a top site is selected THEN open top site in a new tab`() {
        every { navController.currentDestination } returns mockk {
            every { id } returns R.id.shortcutsFragment
        }

        val topSite = TopSite.Default(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        verify {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
    }

    @Test
    fun `GIVEN existing tab for url WHEN Default TopSite selected THEN open new tab`() {
        val url = "mozilla.org"
        val existingTabForUrl = createTab(url = url)

        store = BrowserStore(
            BrowserState(
                tabs = listOf(existingTabForUrl),
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        val topSite = TopSite.Default(
            id = 1L,
            title = "Mozilla",
            url = url,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openDefault.testGetValue())
        assertEquals(1, TopSites.openDefault.testGetValue()!!.size)
        assertNull(TopSites.openDefault.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN existing tab for url WHEN Provided TopSite selected THEN open new tab`() {
        val url = "mozilla.org"
        val existingTabForUrl = createTab(url = url)

        store = BrowserStore(
            BrowserState(
                tabs = listOf(existingTabForUrl),
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = url,
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )
        val position = 0
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openContileTopSite.testGetValue())
        assertEquals(1, TopSites.openContileTopSite.testGetValue()!!.size)
        assertNull(TopSites.openContileTopSite.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { controller.recordTopSitesClickTelemetry(topSite, position) }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `GIVEN existing tab for url WHEN Frecent TopSite selected THEN navigate to tab`() {
        val url = "mozilla.org"
        val existingTabForUrl = createTab(url = url)

        store = BrowserStore(
            BrowserState(
                tabs = listOf(existingTabForUrl),
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        val topSite = TopSite.Frecent(
            id = 1L,
            title = "Mozilla",
            url = url,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNull(TopSites.openInNewTab.testGetValue())

        assertNotNull(TopSites.openFrecency.testGetValue())
        assertEquals(1, TopSites.openFrecency.testGetValue()!!.size)
        assertNull(TopSites.openFrecency.testGetValue()!!.single().extra)

        verify {
            selectTabUseCase.invoke(existingTabForUrl.id)
            navController.navigate(R.id.browserFragment)
        }
    }

    @Test
    fun `GIVEN existing tab for url WHEN Pinned TopSite selected THEN navigate to tab`() {
        val url = "mozilla.org"
        val existingTabForUrl = createTab(url = url)

        store = BrowserStore(
            BrowserState(
                tabs = listOf(existingTabForUrl),
                search = SearchState(
                    regionSearchEngines = listOf(searchEngine),
                ),
            ),
        )

        val topSite = TopSite.Pinned(
            id = 1L,
            title = "Mozilla",
            url = url,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNull(TopSites.openInNewTab.testGetValue())

        assertNotNull(TopSites.openPinned.testGetValue())
        assertEquals(1, TopSites.openPinned.testGetValue()!!.size)
        assertNull(TopSites.openPinned.testGetValue()!!.single().extra)

        verify {
            selectTabUseCase.invoke(existingTabForUrl.id)
            navController.navigate(R.id.browserFragment)
        }
    }

    @Test
    fun handleSelectGoogleDefaultTopSiteUS() {
        val topSite = TopSite.Default(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("US", "US"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openDefault.testGetValue())
        assertEquals(1, TopSites.openDefault.testGetValue()!!.size)
        assertNull(TopSites.openDefault.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = SupportUtils.GOOGLE_US_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectGoogleDefaultTopSiteXX() {
        val topSite = TopSite.Default(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("DE", "FR"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openDefault.testGetValue())
        assertEquals(1, TopSites.openDefault.testGetValue()!!.size)
        assertNull(TopSites.openDefault.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                SupportUtils.GOOGLE_XX_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectGoogleDefaultTopSite_EventPerformedSearchTopSite() {
        assertNull(Events.performedSearch.testGetValue())

        val topSite = TopSite.Default(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(googleSearchEngine)

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(Events.performedSearch.testGetValue())

        assertNotNull(TopSites.openDefault.testGetValue())
        assertEquals(1, TopSites.openDefault.testGetValue()!!.size)
        assertNull(TopSites.openDefault.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)
    }

    @Test
    fun handleSelectDuckDuckGoTopSite_EventPerformedSearchTopSite() {
        assertNull(Events.performedSearch.testGetValue())

        val topSite = TopSite.Pinned(
            id = 1L,
            title = "DuckDuckGo",
            url = "https://duckduckgo.com",
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(
            googleSearchEngine,
            duckDuckGoSearchEngine,
        )

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(Events.performedSearch.testGetValue())
    }

    @Test
    fun handleSelectGooglePinnedTopSiteUS() {
        val topSite = TopSite.Pinned(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("US", "US"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openPinned.testGetValue())
        assertEquals(1, TopSites.openPinned.testGetValue()!!.size)
        assertNull(TopSites.openPinned.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                SupportUtils.GOOGLE_US_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectGooglePinnedTopSiteXX() {
        val topSite = TopSite.Pinned(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("DE", "FR"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openPinned.testGetValue())
        assertEquals(1, TopSites.openPinned.testGetValue()!!.size)
        assertNull(TopSites.openPinned.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                SupportUtils.GOOGLE_XX_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectGoogleFrecentTopSiteUS() {
        val topSite = TopSite.Frecent(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("US", "US"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openFrecency.testGetValue())
        assertEquals(1, TopSites.openFrecency.testGetValue()!!.size)
        assertNull(TopSites.openFrecency.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                SupportUtils.GOOGLE_US_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectGoogleFrecentTopSiteXX() {
        val topSite = TopSite.Frecent(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        store.dispatch(SearchAction.SetRegionAction(RegionState("DE", "FR"))).joinBlocking()

        controller.handleSelectTopSite(topSite, position = 0)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openFrecency.testGetValue())
        assertEquals(1, TopSites.openFrecency.testGetValue()!!.size)
        assertNull(TopSites.openFrecency.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openGoogleSearchAttribution.testGetValue())
        assertEquals(1, TopSites.openGoogleSearchAttribution.testGetValue()!!.size)
        assertNull(TopSites.openGoogleSearchAttribution.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                SupportUtils.GOOGLE_XX_URL,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun handleSelectProvidedTopSite() {
        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )
        val position = 0
        val controller = spyk(createController())

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        controller.handleSelectTopSite(topSite, position)

        assertNotNull(TopSites.openInNewTab.testGetValue())
        assertEquals(1, TopSites.openInNewTab.testGetValue()!!.size)
        assertNull(TopSites.openInNewTab.testGetValue()!!.single().extra)

        assertNotNull(TopSites.openContileTopSite.testGetValue())
        assertEquals(1, TopSites.openContileTopSite.testGetValue()!!.size)
        assertNull(TopSites.openContileTopSite.testGetValue()!!.single().extra)

        verify {
            tabsUseCases.addTab.invoke(
                url = topSite.url,
                selectTab = true,
                startLoading = true,
            )
        }
        verify { controller.recordTopSitesClickTelemetry(topSite, position) }
        verify { navController.navigate(R.id.browserFragment) }
    }

    @Test
    fun `WHEN the provided top site is clicked THEN send a click callback request`() {
        val controller = spyk(createController())
        val topSite = TopSite.Provided(
            id = 3,
            title = "Mozilla",
            url = "https://mozilla.com",
            clickUrl = "https://mozilla.com/click",
            imageUrl = "https://test.com/image2.jpg",
            impressionUrl = "https://mozilla.com/impression",
            createdAt = 3,
        )
        val position = 0

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        assertNull(TopSites.contileClick.testGetValue())

        var topSiteImpressionPinged = false
        Pings.topsitesImpression.testBeforeNextSubmit {
            assertEquals(3L, TopSites.contileTileId.testGetValue())
            assertEquals("mozilla", TopSites.contileAdvertiser.testGetValue())
            assertNull(TopSites.contileReportingUrl.testGetValue())

            topSiteImpressionPinged = true
        }

        controller.handleSelectTopSite(topSite, position)

        verify { marsUseCases.recordInteraction(topSite.clickUrl) }

        val event = TopSites.contileClick.testGetValue()!!

        assertEquals(1, event.size)
        assertEquals("top_sites", event[0].category)
        assertEquals("contile_click", event[0].name)
        assertEquals("1", event[0].extra!!["position"])
        assertEquals("newtab", event[0].extra!!["source"])

        assertTrue(topSiteImpressionPinged)
    }

    @Test
    fun `WHEN the provided top site is seen THEN send a impression callback request`() {
        val controller = spyk(createController())
        val topSite = TopSite.Provided(
            id = 3,
            title = "Mozilla",
            url = "https://mozilla.com",
            clickUrl = "https://mozilla.com/click",
            imageUrl = "https://test.com/image2.jpg",
            impressionUrl = "https://mozilla.com/impression",
            createdAt = 3,
        )
        val position = 0

        every { controller.getAvailableSearchEngines() } returns listOf(searchEngine)

        assertNull(TopSites.contileImpression.testGetValue())

        var topSiteImpressionSubmitted = false
        Pings.topsitesImpression.testBeforeNextSubmit {
            assertEquals(3L, TopSites.contileTileId.testGetValue())
            assertEquals("mozilla", TopSites.contileAdvertiser.testGetValue())
            assertNull(TopSites.contileReportingUrl.testGetValue())

            topSiteImpressionSubmitted = true
        }

        controller.handleTopSiteImpression(topSite, position)

        verify { marsUseCases.recordInteraction(topSite.impressionUrl) }

        val event = TopSites.contileImpression.testGetValue()!!

        assertEquals(1, event.size)
        assertEquals("top_sites", event[0].category)
        assertEquals("contile_impression", event[0].name)
        assertEquals("1", event[0].extra!!["position"])
        assertEquals("newtab", event[0].extra!!["source"])

        assertTrue(topSiteImpressionSubmitted)
    }

    @Test
    fun `WHEN the default Google top site is removed THEN the correct metric is recorded`() {
        val controller = spyk(createController())
        val topSite = TopSite.Default(
            id = 1L,
            title = "Google",
            url = SupportUtils.GOOGLE_URL,
            createdAt = 0,
        )
        assertNull(TopSites.remove.testGetValue())
        assertNull(TopSites.googleTopSiteRemoved.testGetValue())

        controller.handleRemoveTopSiteClicked(topSite)

        assertNotNull(TopSites.googleTopSiteRemoved.testGetValue())
        assertEquals(1, TopSites.googleTopSiteRemoved.testGetValue()!!.size)
        assertNull(TopSites.googleTopSiteRemoved.testGetValue()!!.single().extra)

        assertNotNull(TopSites.remove.testGetValue())
        assertEquals(1, TopSites.remove.testGetValue()!!.size)
        assertNull(TopSites.remove.testGetValue()!!.single().extra)
    }

    @Test
    fun `WHEN the frecent top site is updated THEN add the frecent top site as a pinned top site`() {
        val topSite = TopSite.Frecent(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )

        val controller = spyk(createController())
        val title = "Firefox"
        val url = "firefox.com"

        controller.updateTopSite(topSite = topSite, title = title, url = url)

        verify {
            topSitesUseCases.addPinnedSites(title = title, url = url)
        }
    }

    @Test
    fun `WHEN the pinned top site is updated THEN update the pinned top site in storage`() {
        val topSite = TopSite.Pinned(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )

        val controller = spyk(createController())
        val title = "Firefox"
        val url = "firefox.com"

        controller.updateTopSite(topSite = topSite, title = title, url = url)

        verify {
            topSitesUseCases.updateTopSites(topSite = topSite, title = title, url = url)
        }
    }

    @Test
    fun `WHEN handleTopSiteSettingsClicked is called THEN navigate to the HomeSettingsFragment AND report the interaction`() {
        createController().handleTopSiteSettingsClicked()

        assertNotNull(TopSites.contileSettings.testGetValue())
        assertEquals(1, TopSites.contileSettings.testGetValue()!!.size)
        assertNull(TopSites.contileSettings.testGetValue()!!.single().extra)

        verify { navController.navigate(R.id.homeSettingsFragment) }
    }

    @Test
    fun `WHEN handleSponsorPrivacyClicked is called THEN navigate to the privacy webpage AND report the interaction`() {
        createController().handleSponsorPrivacyClicked()

        assertNotNull(TopSites.contileSponsorsAndPrivacy.testGetValue())
        assertEquals(1, TopSites.contileSponsorsAndPrivacy.testGetValue()!!.size)
        assertNull(TopSites.contileSponsorsAndPrivacy.testGetValue()!!.single().extra)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.SPONSOR_PRIVACY),
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `GIVEN current destination is the shortcuts fragmentWHEN handleSponsorPrivacyClicked is called THEN navigate to the privacy webpage AND report the interaction`() {
        every { navController.currentDestination } returns mockk {
            every { id } returns R.id.shortcutsFragment
        }

        createController().handleSponsorPrivacyClicked()

        assertNotNull(TopSites.contileSponsorsAndPrivacy.testGetValue())
        assertEquals(1, TopSites.contileSponsorsAndPrivacy.testGetValue()!!.size)
        assertNull(TopSites.contileSponsorsAndPrivacy.testGetValue()!!.single().extra)

        verify {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.SPONSOR_PRIVACY),
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN top site long clicked is called THEN report the top site long click telemetry`() {
        assertNull(TopSites.longPress.testGetValue())

        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )

        createController().handleTopSiteLongClicked(topSite)

        assertEquals(topSite.type, TopSites.longPress.testGetValue()!!.single().extra!!["type"])
    }

    @Test
    fun `WHEN handleOpenInPrivateTabClicked is called with a TopSite#Provided site THEN navigate to the top site and record telemetry`() {
        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )
        createController().handleOpenInPrivateTabClicked(topSite)

        assertNotNull(TopSites.openContileInPrivateTab.testGetValue())
        assertEquals(1, TopSites.openContileInPrivateTab.testGetValue()!!.size)
        assertNull(TopSites.openContileInPrivateTab.testGetValue()!!.single().extra)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = topSite.url,
                newTab = true,
                private = true,
            )
        }
    }

    @Test
    fun `GIVEN current destination is the shortcuts fragment WHEN handleOpenInPrivateTabClicked is called with a TopSite#Provided site THEN navigate to the top site and record telemetry`() {
        every { navController.currentDestination } returns mockk {
            every { id } returns R.id.shortcutsFragment
        }

        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )
        createController().handleOpenInPrivateTabClicked(topSite)

        assertNotNull(TopSites.openContileInPrivateTab.testGetValue())
        assertEquals(1, TopSites.openContileInPrivateTab.testGetValue()!!.size)
        assertNull(TopSites.openContileInPrivateTab.testGetValue()!!.single().extra)

        verify {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = topSite.url,
                newTab = true,
                private = true,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN handleOpenInPrivateTabClicked is called with a TopSite#Provided site THEN navigate to the top site and record telemetry`() {
        every { settings.enableHomepageAsNewTab } returns true

        val topSite = TopSite.Provided(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            clickUrl = "",
            imageUrl = "",
            impressionUrl = "",
            createdAt = 0,
        )
        createController().handleOpenInPrivateTabClicked(topSite)

        assertNotNull(TopSites.openContileInPrivateTab.testGetValue())
        assertEquals(1, TopSites.openContileInPrivateTab.testGetValue()!!.size)
        assertNull(TopSites.openContileInPrivateTab.testGetValue()!!.single().extra)

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = topSite.url,
                newTab = true,
                private = true,
            )
        }
    }

    @Test
    fun `WHEN handleOpenInPrivateTabClicked is called with a Default, Pinned, or Frecent top site THEN openInPrivateTab event is recorded`() {
        val controller = createController()
        val topSite1 = TopSite.Default(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val topSite2 = TopSite.Pinned(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        val topSite3 = TopSite.Frecent(
            id = 1L,
            title = "Mozilla",
            url = "mozilla.org",
            createdAt = 0,
        )
        assertNull(TopSites.openInPrivateTab.testGetValue())

        controller.handleOpenInPrivateTabClicked(topSite1)
        controller.handleOpenInPrivateTabClicked(topSite2)
        controller.handleOpenInPrivateTabClicked(topSite3)

        assertNotNull(TopSites.openInPrivateTab.testGetValue())
        assertEquals(3, TopSites.openInPrivateTab.testGetValue()!!.size)
        for (event in TopSites.openInPrivateTab.testGetValue()!!) {
            assertNull(event.extra)
        }
    }

    private fun createController(): DefaultTopSiteController =
        DefaultTopSiteController(
            activityRef = WeakReference(activity),
            navControllerRef = WeakReference(navController),
            store = store,
            settings = settings,
            addTabUseCase = tabsUseCases.addTab,
            selectTabUseCase = selectTabUseCase.selectTab,
            fenixBrowserUseCases = fenixBrowserUseCases,
            topSitesUseCases = topSitesUseCases,
            marsUseCases = marsUseCases,
            viewLifecycleScope = scope,
        )
}
