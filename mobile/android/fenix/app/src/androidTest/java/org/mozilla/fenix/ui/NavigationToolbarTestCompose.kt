/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("DEPRECATION")

package org.mozilla.fenix.ui

import android.content.Context
import android.hardware.camera2.CameraManager
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.Espresso
import androidx.test.rule.ActivityTestRule
import mozilla.components.concept.engine.utils.EngineReleaseChannel
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assume
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableOrDisableBackGestureNavigationOnDevice
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.DataGenerationHelper.createCustomTabIntent
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.MockBrowserDataHelper
import org.mozilla.fenix.helpers.MockBrowserDataHelper.createBookmarkItem
import org.mozilla.fenix.helpers.MockBrowserDataHelper.generateBookmarkFolder
import org.mozilla.fenix.helpers.MockBrowserDataHelper.setCustomSearchEngine
import org.mozilla.fenix.helpers.SearchDispatcher
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getLoremIpsumAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.checkTextSizeOnWebsite
import org.mozilla.fenix.ui.robots.clickContextMenuItem
import org.mozilla.fenix.ui.robots.customTabScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.longClickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.searchScreen

class NavigationToolbarTestCompose : TestSetup() {
    private lateinit var searchMockServer: MockWebServer

    private val bookmarkFolderName = "My Folder"

    private val queryString: String = "firefox"

    private val defaultSearchEngineList =
        listOf(
            "Bing",
            "DuckDuckGo",
            "Google",
        )

    private val generalEnginesList = listOf("DuckDuckGo", "Google", "Bing")
    private val topicEnginesList = listOf("Wikipedia (en)")

    private val firefoxSuggestHeader = getStringResource(R.string.firefox_suggest_header)

    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityTestRule(
                isComposableToolbarEnabled = true,
                isMenuRedesignEnabled = true,
                isPWAsPromptEnabled = false,
                isWallpaperOnboardingEnabled = false,
                isOpenInAppBannerEnabled = false,
                isMicrosurveyEnabled = false,
                isTermsOfServiceAccepted = true,
            ),
        ) { it.activity }

    @get:Rule
    val intentReceiverActivityTestRule = ActivityTestRule(
        IntentReceiverActivity::class.java,
        true,
        false,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Before
    override fun setUp() {
        super.setUp()
        searchMockServer = MockWebServer().apply {
            dispatcher = SearchDispatcher()
            start()
        }
    }

    @After
    override fun tearDown() {
        super.tearDown()
        searchMockServer.shutdown()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135074
    @SmokeTest
    @Test
    fun verifySecurePageSecuritySubMenuTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = "https://mozilla-mobile.github.io/testapp/loginForm"
            val defaultWebPageTitle = "Login_form"

            navigationToolbar {
            }.enterURLAndEnterToBrowserWithComposableToolbar(
                composeTestRule,
                defaultWebPage.toUri(),
            ) {
                verifyPageContent("Login Form")
            }.openSiteSecuritySheetWithComposableToolbar(composeTestRule) {
                verifyQuickActionSheet(defaultWebPage, true)
                openSecureConnectionSubMenu(true)
                verifySecureConnectionSubMenu(defaultWebPageTitle, defaultWebPage, true)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135075
    @SmokeTest
    @Test
    fun verifyInsecurePageSecuritySubMenuTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val defaultWebPage = getGenericAsset(mockWebServer, 1)

            navigationToolbar {
            }.enterURLAndEnterToBrowserWithComposableToolbar(composeTestRule, defaultWebPage.url) {
                verifyPageContent(defaultWebPage.content)
            }.openSiteSecuritySheetWithComposableToolbar(composeTestRule) {
                verifyQuickActionSheet(defaultWebPage.url.toString(), false)
                openSecureConnectionSubMenu(false)
                verifySecureConnectionSubMenu(
                    defaultWebPage.title,
                    defaultWebPage.url.toString(),
                    false,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135076
    @SmokeTest
    @Test
    @SkipLeaks
    fun verifyClearCookiesFromQuickSettingsTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val loginPage = "https://mozilla-mobile.github.io/testapp/loginForm"
            val originWebsite = "mozilla-mobile.github.io"

            navigationToolbar {
            }.enterURLAndEnterToBrowserWithComposableToolbar(composeTestRule, loginPage.toUri()) {
                waitForPageToLoad(waitingTimeLong)
            }.openSiteSecuritySheetWithComposableToolbar(composeTestRule) {
                clickQuickActionSheetClearSiteData()
                verifyClearSiteDataPrompt(originWebsite)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135005
    @SmokeTest
    @Test
    fun verifyFontSizingChangeTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            // Goes through the settings and changes the default text on a webpage, then verifies if the text has changed.
            val fenixApp = composeTestRule.activity.applicationContext as FenixApplication
            val webpage = getLoremIpsumAsset(mockWebServer).url

            // This value will represent the text size percentage the webpage will scale to. The default value is 100%.
            val textSizePercentage = 180

            homeScreen {
            }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
            }.openSettings {
            }.openAccessibilitySubMenu {
                clickFontSizingSwitch()
                verifyEnabledMenuItems()
                changeTextSizeSlider(textSizePercentage)
                verifyTextSizePercentage(textSizePercentage)
            }.goBack {
            }.goBack {
            }
            navigationToolbar {
            }.enterURLAndEnterToBrowserWithComposableToolbar(composeTestRule, webpage) {
                checkTextSizeOnWebsite(textSizePercentage, fenixApp.components)
            }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
            }.openSettings {
            }.openAccessibilitySubMenu {
                clickFontSizingSwitch()
                verifyMenuItemsAreDisabled()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135034
    @SmokeTest
    @Test
    fun verifySearchForBookmarkedItemsTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val firstWebPage = getGenericAsset(mockWebServer, 1)
            val secondWebPage = TestAssetHelper.getHTMLControlsFormAsset(mockWebServer)

            val newFolder = generateBookmarkFolder(title = bookmarkFolderName, position = null)
            createBookmarkItem(firstWebPage.url.toString(), firstWebPage.title, null, newFolder)
            createBookmarkItem(secondWebPage.url.toString(), secondWebPage.title, null)

            homeScreen {
            }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
            }.openBookmarks(composeTestRule) {
            }.clickSearchButton {
                // Search for a valid term
                typeSearchWithComposableToolbar(firstWebPage.title)
                verifySearchSuggestionsAreDisplayed(composeTestRule, firstWebPage.url.toString())
                verifySuggestionsAreNotDisplayed(composeTestRule, secondWebPage.url.toString())
                // Search for invalid term
                typeSearchWithComposableToolbar("Android")
                verifySuggestionsAreNotDisplayed(composeTestRule, firstWebPage.url.toString())
                verifySuggestionsAreNotDisplayed(composeTestRule, secondWebPage.url.toString())
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135037
    @SmokeTest
    @Test
    fun verifyTheCustomTabsMainMenuItemsTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val customMenuItem = "TestMenuItem"
            val customTabPage = getGenericAsset(mockWebServer, 1)

            intentReceiverActivityTestRule.launchActivity(
                createCustomTabIntent(
                    customTabPage.url.toString(),
                    customMenuItem,
                ),
            )

            customTabScreen {
                verifyCustomTabCloseButtonWithComposableToolbar(composeTestRule)
            }.openMainMenuWithComposableToolbar(composeTestRule) {
                verifyRedesignedCustomTabsMainMenuItemsExist(customMenuItem, true)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135106
    @SmokeTest
    @Test
    fun verifyShowSearchSuggestionsToggleTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
                // The Google related suggestions aren't always displayed on cold run
                // Bugzilla ticket: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
                clickSearchSelectorButtonWithComposableToolbar(composeTestRule)
                selectTemporarySearchMethodWithComposableToolbar(composeTestRule, "DuckDuckGo")
                typeSearchWithComposableToolbar("mozilla ")
                verifySearchSuggestionsAreDisplayed(composeTestRule, "mozilla firefox")
            }.dismissSearchBar {
            }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
            }.openSettings {
            }.openSearchSubMenu {
                toggleShowSearchSuggestions()
            }.goBack {
            }.goBack {
            }.openSearchWithComposableToolbar(composeTestRule) {
                // The Google related suggestions aren't always displayed on cold run
                // Bugzilla ticket: https://bugzilla.mozilla.org/show_bug.cgi?id=1813587
                clickSearchSelectorButtonWithComposableToolbar(composeTestRule)
                selectTemporarySearchMethodWithComposableToolbar(composeTestRule, "DuckDuckGo")
                typeSearchWithComposableToolbar("mozilla")
                verifySuggestionsWithComposableToolbarAreNotDisplayed(composeTestRule)
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135025
    @SmokeTest
    @Test
    fun verifyTheDefaultSearchEngineCanBeChangedTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            // Goes through the settings and changes the default search engine, then verifies it has changed.
            defaultSearchEngineList.forEach {
                homeScreen {
                }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
                }.openSettings {
                }.openSearchSubMenu {
                    openDefaultSearchEngineMenu()
                    changeDefaultSearchEngine(it)
                    exitMenu()
                }
                searchScreen {
                    verifySearchEngineIconWithComposableToolbar(
                        composeTestRule = composeTestRule,
                        name = it,
                    )
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135009
    @SmokeTest
    @Test
    fun scanQRCodeToOpenAWebpageTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val cameraManager =
                TestHelper.appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
            Assume.assumeTrue(cameraManager.cameraIdList.isNotEmpty())

            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
                clickScanButtonWithComposableToolbar(composeTestRule)
                grantSystemPermission()
                verifyScannerOpen()
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2154215
    @SmokeTest
    @Test
    fun verifyHistorySearchWithBrowsingHistoryTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val firstPageUrl = getGenericAsset(searchMockServer, 1)
            val secondPageUrl = getGenericAsset(searchMockServer, 2)

            MockBrowserDataHelper.createHistoryItem(firstPageUrl.url.toString())
            MockBrowserDataHelper.createHistoryItem(secondPageUrl.url.toString())

            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
                clickSearchSelectorButtonWithComposableToolbar(composeTestRule)
                selectTemporarySearchMethodWithComposableToolbar(
                    composeTestRule,
                    searchEngineName = "History",
                )
                typeSearchWithComposableToolbar(searchTerm = "Mozilla")
                verifySuggestionsAreNotDisplayed(rule = composeTestRule, "Mozilla")
                clickClearButtonWithComposableToolbar(composeTestRule)
                typeSearchWithComposableToolbar(searchTerm = "generic")
                // verifyTypedToolbarText("generic", exists = true)
                verifySearchSuggestionsAreDisplayed(
                    rule = composeTestRule,
                    searchSuggestions = arrayOf(
                        firstPageUrl.url.toString(),
                        secondPageUrl.url.toString(),
                    ),
                )
            }.clickSearchSuggestion(firstPageUrl.url.toString()) {
                verifyUrlWithComposableToolbar(composeTestRule, firstPageUrl.url.toString())
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135109
    // Verifies a temporary change of search engine from the Search shortcut menu
    @SmokeTest
    @Test
    fun searchEnginesCanBeChangedTemporarilyFromSearchSelectorMenuTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            (generalEnginesList + topicEnginesList).forEach {
                homeScreen {
                }.openSearchWithComposableToolbar(composeTestRule) {
                    clickSearchSelectorButtonWithComposableToolbar(composeTestRule)
                    verifySearchShortcutListWithComposableToolbar(composeTestRule, it)
                    selectTemporarySearchMethodWithComposableToolbar(composeTestRule, it)
                    verifySearchEngineIconWithComposableToolbar(composeTestRule, it)
                }.submitQueryWithComposableToolbar("mozilla ") {
                    verifyUrlWithComposableToolbar(composeTestRule, "mozilla")
                }.goToHomescreenWithComposableToolbar(composeTestRule) {
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135027
    @SmokeTest
    @Test
    fun searchHistoryNotRememberedInPrivateBrowsingTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            TestHelper.appContext.settings().shouldShowSearchSuggestionsInPrivate = true

            val firstPageUrl = getGenericAsset(searchMockServer, 1)
            val searchEngineName = "TestSearchEngine"

            setCustomSearchEngine(searchMockServer, searchEngineName)
            createBookmarkItem(firstPageUrl.url.toString(), firstPageUrl.title, 1u)

            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
            }.submitQueryWithComposableToolbar("test page 1") {
            }.goToHomescreenWithComposableToolbar(composeTestRule) {
            }.togglePrivateBrowsingMode()

            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
            }.submitQueryWithComposableToolbar("test page 2") {
            }.openSearchWithComposableToolbar(composeTestRule) {
                typeSearchWithComposableToolbar(searchTerm = "test page")
                verifyTheSuggestionsHeader(composeTestRule, firefoxSuggestHeader)
                verifyTheSuggestionsHeader(composeTestRule, "TestSearchEngine search")
                verifySearchSuggestionsAreDisplayed(
                    rule = composeTestRule,
                    searchSuggestions = arrayOf(
                        "test page 1",
                        firstPageUrl.url.toString(),
                    ),
                )
                // 2 search engine suggestions and 2 browser suggestions (1 history, 1 bookmark)
                verifySearchSuggestionsCount(
                    composeTestRule,
                    numberOfSuggestions = 4,
                    searchTerm = "test page",
                )
                verifySuggestionsAreNotDisplayed(
                    composeTestRule,
                    searchSuggestions = arrayOf(
                        "test page 2",
                    ),
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135012
    @SmokeTest
    @Test
    fun searchResultsOpenedInNewTabsGenerateSearchGroupsTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val firstPageUrl = getGenericAsset(searchMockServer, 1).url
            val secondPageUrl = getGenericAsset(searchMockServer, 2).url
            val searchEngineName = "TestSearchEngine"
            // setting our custom mockWebServer search URL
            setCustomSearchEngine(searchMockServer, searchEngineName)

            // Performs a search and opens 2 dummy search results links to create a search group
            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
            }.submitQueryWithComposableToolbar(queryString) {
                longClickPageObject(itemWithText("Link 1"))
                clickContextMenuItem("Open link in new tab")
                clickSnackbarButton(composeTestRule, "SWITCH")
                verifyUrlWithComposableToolbar(composeTestRule, firstPageUrl.toString())
                Espresso.pressBack()
                longClickPageObject(itemWithText("Link 2"))
                clickContextMenuItem("Open link in new tab")
                clickSnackbarButton(composeTestRule, "SWITCH")
                verifyUrlWithComposableToolbar(composeTestRule, secondPageUrl.toString())
            }.goToHomescreenWithComposableToolbar(composeTestRule) {
                verifyRecentlyVisitedSearchGroupDisplayed(
                    composeTestRule,
                    shouldBeDisplayed = true,
                    searchTerm = queryString,
                    groupSize = 3,
                )
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135013
    @SmokeTest
    @Test
    fun searchGroupIsNotGeneratedForLinksOpenedInPrivateTabsTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            // setting our custom mockWebServer search URL
            val searchEngineName = "TestSearchEngine"
            setCustomSearchEngine(searchMockServer, searchEngineName)

            // Performs a search and opens 2 dummy search results links to create a search group
            homeScreen {
            }.openSearchWithComposableToolbar(composeTestRule) {
            }.submitQueryWithComposableToolbar(queryString) {
                longClickPageObject(itemWithText("Link 1"))
                clickContextMenuItem("Open link in private tab")
                longClickPageObject(itemWithText("Link 2"))
                clickContextMenuItem("Open link in private tab")
            }.openTabDrawerWithComposableToolbar(composeTestRule) {
            }.toggleToPrivateTabs {
            }.openPrivateTab(0) {
            }.openTabDrawerWithComposableToolbar(composeTestRule) {
            }.openPrivateTab(1) {
            }.goToHomescreenWithComposableToolbar(composeTestRule, isPrivateModeEnabled = true) {
                togglePrivateBrowsingModeOnOff(composeTestRule = composeTestRule)
                verifyRecentlyVisitedSearchGroupDisplayed(
                    composeTestRule,
                    shouldBeDisplayed = false,
                    searchTerm = queryString,
                    groupSize = 3,
                )
            }.openThreeDotMenuWithComposableToolbar(composeTestRule) {
            }.openHistory {
                verifyHistoryItemExists(shouldExist = false, item = "3 sites")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3135071
    // Swipes the nav bar left/right to switch between tabs
    @SmokeTest
    @Test
    @SkipLeaks
    fun swipeToSwitchTabTest() {
        runWithCondition(
            composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.BETA ||
                composeTestRule.activity.components.core.engine.version.releaseChannel !== EngineReleaseChannel.RELEASE,
        ) {
            val firstWebPage = getGenericAsset(mockWebServer, 1)
            val secondWebPage = getGenericAsset(mockWebServer, 2)

            // Disable the back gesture from the edge of the screen on the device.
            enableOrDisableBackGestureNavigationOnDevice(backGestureNavigationEnabled = false)

            navigationToolbar {
            }.enterURLAndEnterToBrowserWithComposableToolbar(composeTestRule, firstWebPage.url) {
            }.openTabDrawerWithComposableToolbar(composeTestRule) {
            }.openNewTab {
            }.submitQueryWithComposableToolbar(secondWebPage.url.toString()) {
                swipeNavBarRightWithComposableToolbar(composeTestRule, secondWebPage.url.toString())
                verifyUrlWithComposableToolbar(composeTestRule, firstWebPage.url.toString())
                swipeNavBarLeftWithComposableToolbar(composeTestRule, firstWebPage.url.toString())
                verifyUrlWithComposableToolbar(composeTestRule, secondWebPage.url.toString())
            }
        }
    }
}
