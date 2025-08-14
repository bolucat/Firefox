/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.annotation.SuppressLint
import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.DrawableRes
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.content.ContextCompat.getColor
import androidx.core.graphics.drawable.toDrawable
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.appbar.AppBarLayout
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.Divider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopupProperties
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.top.sites.TopSitesFeature
import mozilla.components.lib.state.ext.consumeFlow
import mozilla.components.lib.state.ext.consumeFrom
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.GleanMetrics.HomeScreen
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.showSnackBar
import org.mozilla.fenix.biometricauthentication.AuthenticationStatus
import org.mozilla.fenix.biometricauthentication.BiometricAuthenticationManager
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.tabstrip.TabStrip
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.HomepageThumbnailIntegration
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MicrosurveyAction
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.CheckIfEligibleForReviewPrompt
import org.mozilla.fenix.components.appstate.AppAction.ReviewPromptAction.ReviewPromptShown
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.qrScanner.QrScannerBinding
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.toolbar.BottomToolbarContainerView
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.databinding.FragmentHomeBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.isToolbarAtBottom
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.recordEventInNimbus
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.scaleToBottomOfView
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.tabClosedUndoMessage
import org.mozilla.fenix.ext.updateMicrosurveyPromptForConfigurationChange
import org.mozilla.fenix.home.bookmarks.BookmarksFeature
import org.mozilla.fenix.home.bookmarks.controller.DefaultBookmarksController
import org.mozilla.fenix.home.ext.showWallpaperOnboardingDialog
import org.mozilla.fenix.home.pocket.controller.DefaultPocketStoriesController
import org.mozilla.fenix.home.privatebrowsing.controller.DefaultPrivateBrowsingController
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabFeature
import org.mozilla.fenix.home.recentsyncedtabs.controller.DefaultRecentSyncedTabController
import org.mozilla.fenix.home.recenttabs.RecentTabsListFeature
import org.mozilla.fenix.home.recenttabs.controller.DefaultRecentTabsController
import org.mozilla.fenix.home.recentvisits.RecentVisitsFeature
import org.mozilla.fenix.home.recentvisits.controller.DefaultRecentVisitsController
import org.mozilla.fenix.home.search.DefaultHomeSearchController
import org.mozilla.fenix.home.sessioncontrol.DefaultSessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlController
import org.mozilla.fenix.home.sessioncontrol.SessionControlControllerCallback
import org.mozilla.fenix.home.sessioncontrol.SessionControlInteractor
import org.mozilla.fenix.home.store.HomeToolbarStoreBuilder
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.toolbar.DefaultToolbarController
import org.mozilla.fenix.home.toolbar.FenixHomeToolbar
import org.mozilla.fenix.home.toolbar.HomeNavigationBar
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable.Companion.DirectToSearchConfig
import org.mozilla.fenix.home.toolbar.HomeToolbarView
import org.mozilla.fenix.home.toolbar.SearchSelectorBinding
import org.mozilla.fenix.home.toolbar.SearchSelectorMenuBinding
import org.mozilla.fenix.home.topsites.DefaultTopSitesView
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SEARCH_ENGINE_NAME
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.AMAZON_SPONSORED_TITLE
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.EBAY_SPONSORED_TITLE
import org.mozilla.fenix.home.topsites.controller.DefaultTopSiteController
import org.mozilla.fenix.home.topsites.getTopSitesConfig
import org.mozilla.fenix.home.ui.Homepage
import org.mozilla.fenix.home.ui.MiddleSearchHomepage
import org.mozilla.fenix.messaging.DefaultMessageController
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.messaging.MessagingFeature
import org.mozilla.fenix.microsurvey.ui.MicrosurveyRequestPrompt
import org.mozilla.fenix.microsurvey.ui.ext.MicrosurveyUIData
import org.mozilla.fenix.microsurvey.ui.ext.toMicrosurveyUIData
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.WidgetPinnedReceiver
import org.mozilla.fenix.pbmlock.NavigationOrigin
import org.mozilla.fenix.pbmlock.observePrivateModeLock
import org.mozilla.fenix.perf.MarkersFragmentLifecycleCallbacks
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.reviewprompt.ReviewPromptState
import org.mozilla.fenix.reviewprompt.ReviewPromptState.Eligible.Type
import org.mozilla.fenix.search.SearchDialogFragment
import org.mozilla.fenix.search.awesomebar.AwesomeBarComposable
import org.mozilla.fenix.search.toolbar.DefaultSearchSelectorController
import org.mozilla.fenix.search.toolbar.SearchSelectorMenu
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.snackbar.SnackbarBinding
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.tabstray.TabsTrayAccessPoint
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.allowUndo
import org.mozilla.fenix.utils.showAddSearchWidgetPromptIfSupported
import org.mozilla.fenix.wallpapers.Wallpaper
import java.lang.ref.WeakReference

@Suppress("TooManyFunctions", "LargeClass")
class HomeFragment : Fragment() {
    private val args by navArgs<HomeFragmentArgs>()

    @VisibleForTesting
    internal lateinit var bundleArgs: Bundle

    @VisibleForTesting
    @Suppress("VariableNaming")
    internal var _binding: FragmentHomeBinding? = null
    internal val binding get() = _binding!!
    private val snackbarBinding = ViewBoundFeatureWrapper<SnackbarBinding>()

    private val homeViewModel: HomeScreenViewModel by activityViewModels()

    @VisibleForTesting
    internal var homeNavigationBar: HomeNavigationBar? = null

    private var _bottomToolbarContainerView: BottomToolbarContainerView? = null
    private val bottomToolbarContainerView: BottomToolbarContainerView
        get() = _bottomToolbarContainerView!!
    private var awesomeBarComposable: AwesomeBarComposable? = null

    private val searchSelectorMenu by lazy {
        SearchSelectorMenu(
            context = requireContext(),
            interactor = sessionControlInteractor,
        )
    }

    private val browsingModeManager get() = (activity as HomeActivity).browsingModeManager

    private val collectionStorageObserver = object : TabCollectionStorage.Observer {
        @SuppressLint("NotifyDataSetChanged")
        override fun onTabsAdded(tabCollection: TabCollection, sessions: List<TabSessionState>) {
            view?.let {
                if (sessions.size == 1) {
                    Snackbar.make(
                        snackBarParentView = binding.dynamicSnackbarContainer,
                        snackbarState = SnackbarState(
                            message = it.context.getString(R.string.create_collection_tab_saved),
                            duration = SnackbarState.Duration.Preset.Long,
                        ),
                    ).show()
                }
            }
        }
    }

    private val store: BrowserStore
        get() = requireComponents.core.store

    private var _sessionControlController: SessionControlController? = null
    private val sessionControlController: SessionControlController
        get() = _sessionControlController!!

    private var _sessionControlInteractor: SessionControlInteractor? = null
    private val sessionControlInteractor: SessionControlInteractor
        get() = _sessionControlInteractor!!

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var nullableToolbarView: FenixHomeToolbar? = null

    private val toolbarView: FenixHomeToolbar
        get() = nullableToolbarView!!

    private var lastAppliedWallpaperName: String = Wallpaper.DEFAULT

    private val topSitesFeature = ViewBoundFeatureWrapper<TopSitesFeature>()

    @VisibleForTesting
    internal val messagingFeatureHomescreen = ViewBoundFeatureWrapper<MessagingFeature>()

    @VisibleForTesting
    internal val messagingFeatureMicrosurvey = ViewBoundFeatureWrapper<MessagingFeature>()

    private val recentTabsListFeature = ViewBoundFeatureWrapper<RecentTabsListFeature>()
    private val recentSyncedTabFeature = ViewBoundFeatureWrapper<RecentSyncedTabFeature>()
    private val bookmarksFeature = ViewBoundFeatureWrapper<BookmarksFeature>()
    private val historyMetadataFeature = ViewBoundFeatureWrapper<RecentVisitsFeature>()
    private val tabsCleanupFeature = ViewBoundFeatureWrapper<TabsCleanupFeature>()
    private val searchSelectorBinding = ViewBoundFeatureWrapper<SearchSelectorBinding>()
    private val searchSelectorMenuBinding = ViewBoundFeatureWrapper<SearchSelectorMenuBinding>()
    private val thumbnailsFeature = ViewBoundFeatureWrapper<HomepageThumbnailIntegration>()

    private val voiceSearchFeature by lazy(LazyThreadSafetyMode.NONE) {
        ViewBoundFeatureWrapper<VoiceSearchFeature>()
    }

    private val voiceSearchLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            voiceSearchFeature.get()?.handleVoiceSearchResult(result.resultCode, result.data)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        super.onCreate(savedInstanceState)

        bundleArgs = args.toBundle()
        if (savedInstanceState != null) {
            bundleArgs.putBoolean(FOCUS_ON_ADDRESS_BAR, false)
        }

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onCreate",
        )
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()
        _binding = FragmentHomeBinding.inflate(inflater, container, false)
        val activity = activity as HomeActivity
        val components = requireComponents

        val currentWallpaperName = requireContext().settings().currentWallpaperName
        applyWallpaper(
            wallpaperName = currentWallpaperName,
            orientationChange = false,
            orientation = requireContext().resources.configuration.orientation,
        )

        lifecycleScope.launch(IO) {
            val settings = requireContext().settings()
            val showStories = settings.showPocketRecommendationsFeature
            val showSponsoredStories = showStories && settings.showPocketSponsoredStories

            if (showStories) {
                components.appStore.dispatch(
                    ContentRecommendationsAction.ContentRecommendationsFetched(
                        recommendations = components.core.pocketStoriesService.getContentRecommendations(),
                    ),
                )
            } else {
                components.appStore.dispatch(ContentRecommendationsAction.PocketStoriesClean)
            }

            if (showSponsoredStories) {
                components.appStore.dispatch(
                    ContentRecommendationsAction.SponsoredContentsChange(
                        sponsoredContents = components.core.pocketStoriesService.getSponsoredContents(),
                    ),
                )
            }
        }

        if (requireContext().settings().isExperimentationEnabled) {
            messagingFeatureHomescreen.set(
                feature = MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.HOMESCREEN,
                    runWhenReadyQueue = requireComponents.performance.visualCompletenessQueue,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )

            initializeMicrosurveyFeature(requireContext().settings().microsurveyFeatureEnabled)
        }

        if (requireContext().settings().showTopSitesFeature) {
            topSitesFeature.set(
                feature = TopSitesFeature(
                    view = DefaultTopSitesView(
                        appStore = components.appStore,
                        settings = components.settings,
                    ),
                    storage = components.core.topSitesStorage,
                    config = getTopSitesConfig(
                        settings = requireContext().settings(),
                        store = store,
                    ),
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }

        if (requireContext().settings().showRecentTabsFeature) {
            recentTabsListFeature.set(
                feature = RecentTabsListFeature(
                    browserStore = components.core.store,
                    appStore = components.appStore,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )

            recentSyncedTabFeature.set(
                feature = RecentSyncedTabFeature(
                    context = requireContext(),
                    appStore = requireComponents.appStore,
                    syncStore = requireComponents.backgroundServices.syncStore,
                    storage = requireComponents.backgroundServices.syncedTabsStorage,
                    accountManager = requireComponents.backgroundServices.accountManager,
                    historyStorage = requireComponents.core.historyStorage,
                    coroutineScope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }

        if (requireContext().settings().showBookmarksHomeFeature) {
            bookmarksFeature.set(
                feature = BookmarksFeature(
                    appStore = components.appStore,
                    bookmarksUseCase = run {
                        requireContext().components.useCases.bookmarksUseCases
                    },
                    scope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }

        if (requireContext().settings().historyMetadataUIFeature) {
            historyMetadataFeature.set(
                feature = RecentVisitsFeature(
                    appStore = components.appStore,
                    historyMetadataStorage = components.core.historyStorage,
                    historyHighlightsStorage = components.core.lazyHistoryStorage,
                    scope = viewLifecycleOwner.lifecycleScope,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }

        bundleArgs.getString(SESSION_TO_DELETE)?.let {
            homeViewModel.sessionToDelete = it
        }
        tabsCleanupFeature.set(
            feature = TabsCleanupFeature(
                context = requireContext(),
                viewModel = homeViewModel,
                browserStore = components.core.store,
                browsingModeManager = browsingModeManager,
                navController = findNavController(),
                tabsUseCases = components.useCases.tabsUseCases,
                fenixBrowserUseCases = components.useCases.fenixBrowserUseCases,
                settings = components.settings,
                snackBarParentView = binding.dynamicSnackbarContainer,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            ),
            owner = viewLifecycleOwner,
            view = binding.root,
        )

        thumbnailsFeature.set(
            feature = HomepageThumbnailIntegration(
                context = requireContext(),
                view = binding.homepageView,
                store = requireComponents.core.store,
                appStore = components.appStore,
            ),
            owner = this,
            view = binding.homepageView,
        )

        snackbarBinding.set(
            feature = SnackbarBinding(
                context = requireContext(),
                browserStore = requireContext().components.core.store,
                appStore = requireContext().components.appStore,
                snackbarDelegate = FenixSnackbarDelegate(binding.dynamicSnackbarContainer),
                navController = findNavController(),
                tabsUseCases = requireContext().components.useCases.tabsUseCases,
                sendTabUseCases = SendTabUseCases(requireComponents.backgroundServices.accountManager),
                customTabSessionId = null,
            ),
            owner = this,
            view = binding.root,
        )

        _sessionControlController = DefaultSessionControlController(
            activityRef = WeakReference(activity),
            settings = components.settings,
            engine = components.core.engine,
            messageController = DefaultMessageController(
                appStore = components.appStore,
                messagingController = components.nimbus.messaging,
                homeActivityRef = WeakReference(activity),
            ),
            store = store,
            tabCollectionStorage = components.core.tabCollectionStorage,
            addTabUseCase = components.useCases.tabsUseCases.addTab,
            restoreUseCase = components.useCases.tabsUseCases.restore,
            selectTabUseCase = components.useCases.tabsUseCases.selectTab,
            reloadUrlUseCase = components.useCases.sessionUseCases.reload,
            fenixBrowserUseCases = components.useCases.fenixBrowserUseCases,
            appStore = components.appStore,
            navControllerRef = WeakReference(findNavController()),
            viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            showAddSearchWidgetPrompt = ::showAddSearchWidgetPrompt,
        ).apply {
            registerCallback(
                object : SessionControlControllerCallback {
                    override fun registerCollectionStorageObserver() {
                        this@HomeFragment.registerCollectionStorageObserver()
                    }

                    override fun removeCollection(tabCollection: TabCollection) {
                        this@HomeFragment.removeCollection(tabCollection)
                    }

                    override fun showTabTray() {
                        this@HomeFragment.openTabsTray()
                    }
                },
            )
        }

        _sessionControlInteractor = SessionControlInteractor(
            controller = sessionControlController,
            recentTabController = DefaultRecentTabsController(
                selectTabUseCase = components.useCases.tabsUseCases.selectTab,
                navController = findNavController(),
                appStore = components.appStore,
                settings = components.settings,
            ),
            recentSyncedTabController = DefaultRecentSyncedTabController(
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                tabsUseCase = requireComponents.useCases.tabsUseCases,
                navController = findNavController(),
                accessPoint = TabsTrayAccessPoint.HomeRecentSyncedTab,
                appStore = components.appStore,
                settings = components.settings,
            ),
            bookmarksController = DefaultBookmarksController(
                navController = findNavController(),
                appStore = components.appStore,
                browserStore = components.core.store,
                settings = components.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                selectTabUseCase = components.useCases.tabsUseCases.selectTab,
            ),
            recentVisitsController = DefaultRecentVisitsController(
                navController = findNavController(),
                appStore = components.appStore,
                settings = components.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                selectOrAddTabUseCase = components.useCases.tabsUseCases.selectOrAddTab,
                storage = components.core.historyStorage,
                scope = viewLifecycleOwner.lifecycleScope,
                store = components.core.store,
            ),
            pocketStoriesController = DefaultPocketStoriesController(
                navControllerRef = WeakReference(findNavController()),
                appStore = components.appStore,
                settings = components.settings,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                marsUseCases = components.useCases.marsUseCases,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            ),
            privateBrowsingController = DefaultPrivateBrowsingController(
                navController = findNavController(),
                browsingModeManager = browsingModeManager,
                fenixBrowserUseCases = requireComponents.useCases.fenixBrowserUseCases,
                settings = components.settings,
            ),
            searchSelectorController = DefaultSearchSelectorController(
                activity = activity,
                navController = findNavController(),
            ),
            toolbarController = DefaultToolbarController(
                appStore = components.appStore,
                browserStore = components.core.store,
                nimbusComponents = components.nimbus,
                navController = findNavController(),
                settings = components.settings,
                fenixBrowserUseCases = components.useCases.fenixBrowserUseCases,
            ),
            homeSearchController = DefaultHomeSearchController(
                appStore = components.appStore,
            ),
            topSiteController = DefaultTopSiteController(
                activityRef = WeakReference(activity),
                store = store,
                navControllerRef = WeakReference(findNavController()),
                settings = components.settings,
                addTabUseCase = components.useCases.tabsUseCases.addTab,
                selectTabUseCase = components.useCases.tabsUseCases.selectTab,
                fenixBrowserUseCases = components.useCases.fenixBrowserUseCases,
                topSitesUseCases = components.useCases.topSitesUseCase,
                marsUseCases = components.useCases.marsUseCases,
                viewLifecycleScope = viewLifecycleOwner.lifecycleScope,
            ),
        )

        nullableToolbarView = buildToolbar(activity)

        if (requireContext().settings().microsurveyFeatureEnabled) {
            listenForMicrosurveyMessage(requireContext())
        }

        initComposeHomepage()

        disableAppBarDragging()

        FxNimbus.features.homescreen.recordExposure()

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onCreateView",
        )
        return binding.root
    }

    private fun buildToolbar(activity: HomeActivity): FenixHomeToolbar =
        when (activity.settings().shouldUseComposableToolbar) {
            true -> {
                val toolbarStore = buildToolbarStore(activity)

                homeNavigationBar = HomeNavigationBar(
                    context = activity,
                    container = binding.navigationBarContainer,
                    toolbarStore = toolbarStore,
                    settings = activity.settings(),
                    hideWhenKeyboardShown = true,
                )

                voiceSearchFeature.set(
                    feature = VoiceSearchFeature(
                        context = activity,
                        appStore = activity.components.appStore,
                        voiceSearchLauncher = voiceSearchLauncher,
                    ),
                    owner = viewLifecycleOwner,
                    view = binding.root,
                )

                HomeToolbarComposable(
                    context = activity,
                    homeBinding = binding,
                    navController = findNavController(),
                    toolbarStore = toolbarStore,
                    appStore = activity.components.appStore,
                    browserStore = activity.components.core.store,
                    settings = activity.settings(),
                    directToSearchConfig = DirectToSearchConfig(
                        startSearch = bundleArgs.getBoolean(FOCUS_ON_ADDRESS_BAR) ||
                                FxNimbus.features.oneClickSearch.value().enabled,
                        sessionId = args.sessionToStartSearchFor,
                        source = args.searchAccessPoint,
                    ),
                    tabStripContent = { TabStrip() },
                    searchSuggestionsContent = { modifier ->
                        (awesomeBarComposable ?: initializeAwesomeBarComposable(toolbarStore, modifier))
                            ?.SearchSuggestions()
                    },
                    navigationBarContent = homeNavigationBar?.asComposable(),
                )
            }

            false -> HomeToolbarView(
                homeBinding = binding,
                interactor = sessionControlInteractor,
                homeFragment = this,
                homeActivity = activity,
            )
        }

    private fun buildToolbarStore(activity: HomeActivity) = HomeToolbarStoreBuilder.build(
        context = activity,
        lifecycleOwner = this,
        navController = findNavController(),
        appStore = requireContext().components.appStore,
        browserStore = requireContext().components.core.store,
        browsingModeManager = activity.browsingModeManager,
    )

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)

        (toolbarView as? HomeToolbarView)?.dismissMenu()

        // If the microsurvey feature is visible, we should update it's state.
        if (shouldShowMicrosurveyPrompt(requireContext())) {
            updateMicrosurveyPromptForConfigurationChange(
                parent = binding.homeLayout,
                bottomToolbarContainerView = _bottomToolbarContainerView?.toolbarContainerView,
                reinitializeMicrosurveyPrompt = { initializeMicrosurveyPrompt() },
            )
        }

        val currentWallpaperName = requireContext().settings().currentWallpaperName
        applyWallpaper(
            wallpaperName = currentWallpaperName,
            orientationChange = true,
            orientation = newConfig.orientation,
        )
    }

    private fun showEncourageSearchCfr() {
        CFRPopup(
            anchor = toolbarView.layout,
            properties = CFRPopupProperties(
                popupBodyColors = listOf(
                    getColor(requireContext(), R.color.fx_mobile_layer_color_gradient_end),
                    getColor(requireContext(), R.color.fx_mobile_layer_color_gradient_start),
                ),
                popupVerticalOffset = ENCOURAGE_SEARCH_CFR_VERTICAL_OFFSET.dp,
                dismissButtonColor = getColor(requireContext(), R.color.fx_mobile_icon_color_oncolor),
                indicatorDirection = if (requireContext().isToolbarAtBottom()) {
                    CFRPopup.IndicatorDirection.DOWN
                } else {
                    CFRPopup.IndicatorDirection.UP
                },
            ),
            onDismiss = {
                with(requireComponents.settings) {
                    lastCfrShownTimeInMillis = System.currentTimeMillis()
                    shouldShowSearchBarCFR = false
                }
            },
            text = {
                FirefoxTheme {
                    Text(
                        text = FxNimbus.features.encourageSearchCfr.value().cfrText,
                        color = FirefoxTheme.colors.textOnColorPrimary,
                        style = FirefoxTheme.typography.body2,
                    )
                }
            },
        ).show()
    }

    @VisibleForTesting
    internal fun initializeMicrosurveyFeature(isMicrosurveyEnabled: Boolean) {
        if (isMicrosurveyEnabled) {
            messagingFeatureMicrosurvey.set(
                feature = MessagingFeature(
                    appStore = requireComponents.appStore,
                    surface = FenixMessageSurfaceId.MICROSURVEY,
                    runWhenReadyQueue = requireComponents.performance.visualCompletenessQueue,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }
    }

    private fun initializeMicrosurveyPrompt() {
        val context = requireContext()

        val isToolbarAtTheBottom = context.isToolbarAtBottom()
        // The toolbar view has already been added directly to the container.
        if (isToolbarAtTheBottom) {
            binding.root.removeView(toolbarView.layout)
        }

        _bottomToolbarContainerView = BottomToolbarContainerView(
            context = context,
            parent = binding.homeLayout,
            content = {
                FirefoxTheme {
                    Column {
                        val activity = requireActivity() as HomeActivity
                        val shouldShowMicrosurveyPrompt = !activity.isMicrosurveyPromptDismissed.value

                        if (shouldShowMicrosurveyPrompt) {
                            currentMicrosurvey
                                ?.let {
                                    if (isToolbarAtTheBottom) {
                                        updateToolbarViewUIForMicrosurveyPrompt()
                                    }

                                    Divider()

                                    MicrosurveyRequestPrompt(
                                        microsurvey = it,
                                        activity = activity,
                                        onStartSurveyClicked = {
                                            context.components.appStore.dispatch(MicrosurveyAction.Started(it.id))
                                            findNavController().nav(
                                                R.id.homeFragment,
                                                HomeFragmentDirections.actionGlobalMicrosurveyDialog(it.id),
                                            )
                                        },
                                        onCloseButtonClicked = {
                                            context.components.appStore.dispatch(
                                                MicrosurveyAction.Dismissed(it.id),
                                            )
                                            context.settings().shouldShowMicrosurveyPrompt = false
                                            activity.isMicrosurveyPromptDismissed.value = true

                                            resetToolbarViewUI()
                                            initializeMicrosurveyPrompt()
                                        },
                                    )
                                }
                        } else {
                            toolbarView.updateDividerVisibility(true)
                        }

                        if (isToolbarAtTheBottom) {
                            AndroidView(factory = { _ -> toolbarView.layout })
                        }
                    }
                }
            },
        )
    }

    private fun updateToolbarViewUIForMicrosurveyPrompt() {
        updateToolbarViewUI(R.drawable.home_bottom_bar_background_no_divider, false, 0.0f)
    }

    private fun resetToolbarViewUI() {
        val elevation = requireContext().resources.getDimension(R.dimen.browser_fragment_toolbar_elevation)
        _binding?.homeLayout?.removeView(bottomToolbarContainerView.toolbarContainerView)
        updateToolbarViewUI(
            R.drawable.home_bottom_bar_background,
            true,
            elevation,
        )
    }

    private fun updateToolbarViewUI(@DrawableRes id: Int, showDivider: Boolean, elevation: Float) {
        (toolbarView as? HomeToolbarView)?.updateBackground(id)
        toolbarView.updateDividerVisibility(showDivider)
        toolbarView.layout.elevation = elevation
    }

    private var currentMicrosurvey: MicrosurveyUIData? = null

    /**
     * Listens for the microsurvey message and initializes the microsurvey prompt if one is available.
     */
    private fun listenForMicrosurveyMessage(context: Context) {
        binding.root.consumeFrom(context.components.appStore, viewLifecycleOwner) { state ->
            state.messaging.messageToShow[FenixMessageSurfaceId.MICROSURVEY]?.let { message ->
                if (message.id != currentMicrosurvey?.id) {
                    message.toMicrosurveyUIData()?.let { microsurvey ->
                        context.components.settings.shouldShowMicrosurveyPrompt = true
                        currentMicrosurvey = microsurvey

                        initializeMicrosurveyPrompt()
                    }
                }
            }
        }
    }

    private fun shouldShowMicrosurveyPrompt(context: Context) =
        context.components.settings.shouldShowMicrosurveyPrompt

    private fun disableAppBarDragging() {
        if (binding.homeAppBar.layoutParams != null) {
            val appBarLayoutParams = binding.homeAppBar.layoutParams as CoordinatorLayout.LayoutParams
            val appBarBehavior = AppBarLayout.Behavior()
            appBarBehavior.setDragCallback(
                object : AppBarLayout.Behavior.DragCallback() {
                    override fun canDrag(appBarLayout: AppBarLayout): Boolean {
                        return false
                    }
                },
            )
            appBarLayoutParams.behavior = appBarBehavior
        }
        binding.homeAppBar.setExpanded(true)
    }

    @Suppress("LongMethod", "ComplexMethod")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        // DO NOT ADD ANYTHING ABOVE THIS getProfilerTime CALL!
        val profilerStartTime = requireComponents.core.engine.profiler?.getProfilerTime()

        super.onViewCreated(view, savedInstanceState)
        HomeScreen.homeScreenDisplayed.record(NoExtras())

        with(requireContext()) {
            if (settings().isExperimentationEnabled) {
                recordEventInNimbus("home_screen_displayed")
            }
        }

        HomeScreen.homeScreenViewCount.add()
        if (!browsingModeManager.mode.isPrivate) {
            HomeScreen.standardHomepageViewCount.add()
        }

        observeSearchEngineNameChanges()
        observeWallpaperUpdates()

        observePrivateModeLock(
            viewLifecycleOwner = viewLifecycleOwner,
            scope = viewLifecycleOwner.lifecycleScope,
            appStore = requireComponents.appStore,
            onPrivateModeLocked = {
                findNavController().navigate(
                    NavGraphDirections.actionGlobalUnlockPrivateTabsFragment(NavigationOrigin.HOME_PAGE),
                )
            },
        )

        toolbarView.build(requireComponents.core.store.state, requireContext().settings().enableHomepageSearchBar)
        if (requireContext().settings().isTabStripEnabled) {
            initTabStrip()
        }

        consumeFrom(requireComponents.core.store) {
            toolbarView.updateTabCounter(it)
        }

        requireComponents.appStore.state.wasLastTabClosedPrivate?.also {
            showUndoSnackbar(requireContext().tabClosedUndoMessage(it))
            requireComponents.appStore.dispatch(AppAction.TabStripAction.UpdateLastTabClosed(null))
        }

        toolbarView.updateTabCounter(requireComponents.core.store.state)

        val focusOnAddressBar = bundleArgs.getBoolean(FOCUS_ON_ADDRESS_BAR) ||
                FxNimbus.features.oneClickSearch.value().enabled

        if (focusOnAddressBar && !requireContext().settings().shouldUseComposableToolbar) {
            // If the fragment gets recreated by the activity, the search fragment might get recreated as well. Changing
            // between browsing modes triggers activity recreation, so when changing modes goes together with navigating
            // home, we should avoid navigating to search twice.
            val searchFragmentAlreadyAdded = parentFragmentManager.fragments.any { it is SearchDialogFragment }
            if (!searchFragmentAlreadyAdded) {
                sessionControlInteractor.onNavigateSearch()
            }
        }

        if (requireContext().settings().shouldUseComposableToolbar) {
            QrScannerBinding.register(this)
        }

        (toolbarView as? HomeToolbarView)?.let {
            searchSelectorBinding.set(
                feature = SearchSelectorBinding(
                    context = view.context,
                    toolbarView = it,
                    browserStore = requireComponents.core.store,
                    searchSelectorMenu = searchSelectorMenu,
                ),
                owner = viewLifecycleOwner,
                view = binding.root,
            )
        }

        searchSelectorMenuBinding.set(
            feature = SearchSelectorMenuBinding(
                context = view.context,
                interactor = sessionControlInteractor,
                searchSelectorMenu = searchSelectorMenu,
                browserStore = requireComponents.core.store,
            ),
            owner = viewLifecycleOwner,
            view = view,
        )

        observeReviewPromptState()

        // DO NOT MOVE ANYTHING BELOW THIS addMarker CALL!
        requireComponents.core.engine.profiler?.addMarker(
            MarkersFragmentLifecycleCallbacks.MARKER_NAME,
            profilerStartTime,
            "HomeFragment.onViewCreated",
        )
    }

    private fun initComposeHomepage() {
        binding.homeAppBarContent.isVisible = false

        binding.homepageView.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

            setContent {
                FirefoxTheme {
                    val settings = LocalContext.current.settings()
                    val appState = with(components.appStore) {
                        remember {
                            // Ignore AppState changes where only the browsing mode differs.
                            // This avoids unnecessary recompositions triggered by theme/browsing mode transitions,
                            // which are handled outside Compose via ThemeManager recreating the activity.
                            // Without this, transient states can cause visual glitches (e.g., incorrect theme/frame)
                            flow().distinctUntilChanged { old, new -> old.mode != new.mode }
                        }.collectAsState(state)
                    }

                    if (settings.enableHomepageSearchBar) {
                        MiddleSearchHomepage(
                            state = HomepageState.build(
                                appState = appState.value,
                                settings = settings,
                                browsingModeManager = browsingModeManager,
                            ),
                            interactor = sessionControlInteractor,
                            onMiddleSearchBarVisibilityChanged = { isVisible ->
                                // Hide the main address bar in the toolbar when the middle search is
                                // visible (and vice versa)
                                toolbarView.updateAddressBarVisibility(!isVisible)
                            },
                            onTopSitesItemBound = {
                                StartupTimeline.onTopSitesItemBound(activity = (requireActivity() as HomeActivity))
                            },
                        )
                    } else {
                        Homepage(
                            state = HomepageState.build(
                                appState = appState.value,
                                settings = settings,
                                browsingModeManager = browsingModeManager,
                            ),
                            interactor = sessionControlInteractor,
                            onTopSitesItemBound = {
                                StartupTimeline.onTopSitesItemBound(activity = (requireActivity() as HomeActivity))
                            },
                        )
                    }

                    LaunchedEffect(Unit) {
                        onFirstHomepageFrameDrawn()
                    }
                }
            }
        }
    }

    private fun onFirstHomepageFrameDrawn() {
        val components = requireContext().components
        val appStore = components.appStore
        val appState = appStore.state

        with(components.settings) {
            if (showWallpaperOnboardingDialog()) {
                sessionControlInteractor.showWallpapersOnboardingDialog(
                    appState.wallpaperState,
                )
            }
        }

        if (!appStore.state.mode.isPrivate) {
            sessionControlInteractor.reportSessionMetrics(state = appState)
        }

        // We want some parts of the home screen UI to be rendered first if they are
        // the most prominent parts of the visible part of the screen.
        // For this reason, we wait for the home screen recycler view to finish it's
        // layout and post an update for when it's best for non-visible parts of the
        // home screen to render itself.
        appStore.dispatch(AppAction.UpdateFirstFrameDrawn(drawn = true))
    }

    private fun initTabStrip() {
        (toolbarView as? HomeToolbarView)?.configureTabStripView {
            isVisible = true
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent { TabStrip() }
        }
    }

    @Composable
    private fun TabStrip() {
        // Tabs will not be shown as selected on the homepage when Homepage as a New Tab is not
        // enabled.
        val isSelectDisabled = !requireContext().settings().enableHomepageAsNewTab

        FirefoxTheme {
            TabStrip(
                isSelectDisabled = isSelectDisabled,
                showActionButtons = context?.settings()?.shouldUseExpandedToolbar != true,
                onAddTabClick = {
                    if (requireContext().settings().enableHomepageAsNewTab) {
                        requireComponents.useCases.fenixBrowserUseCases.addNewHomepageTab(
                            private = (requireActivity() as HomeActivity).browsingModeManager.mode.isPrivate,
                        )
                    } else {
                        sessionControlInteractor.onNavigateSearch()
                    }
                },
                onSelectedTabClick = { url ->
                    if (url != ABOUT_HOME_URL) {
                        (requireActivity() as HomeActivity).openToBrowser(BrowserDirection.FromHome)
                    }
                },
                onLastTabClose = {},
                onCloseTabClick = { isPrivate ->
                    showUndoSnackbar(requireContext().tabClosedUndoMessage(isPrivate))
                },
                onTabCounterClick = { openTabsTray() },
            )
        }
    }

    /**
     * Method used to listen to search engine name changes and trigger a top sites update accordingly
     */
    private fun observeSearchEngineNameChanges() {
        consumeFlow(store) { flow ->
            flow.map { state ->
                when (state.search.selectedOrDefaultSearchEngine?.name) {
                    AMAZON_SEARCH_ENGINE_NAME -> AMAZON_SPONSORED_TITLE
                    EBAY_SPONSORED_TITLE -> EBAY_SPONSORED_TITLE
                    else -> null
                }
            }
                .distinctUntilChanged()
                .collect {
                    topSitesFeature.withFeature {
                        it.storage.notifyObservers { onStorageUpdated() }
                    }
                }
        }
    }

    private fun showUndoSnackbar(message: String) {
        viewLifecycleOwner.lifecycleScope.allowUndo(
            binding.dynamicSnackbarContainer,
            message,
            requireContext().getString(R.string.snackbar_deleted_undo),
            {
                requireComponents.useCases.tabsUseCases.undo.invoke()
                findNavController().navigate(
                    HomeFragmentDirections.actionGlobalBrowser(null),
                )
            },
            operation = { },
        )
    }

    override fun onDestroyView() {
        super.onDestroyView()

        nullableToolbarView = null
        homeNavigationBar = null

        _sessionControlController?.unregisterCallback()
        _sessionControlController = null

        _sessionControlInteractor = null
        _bottomToolbarContainerView = null
        awesomeBarComposable = null
        _binding = null

        if (!requireContext().components.appStore.state.isPrivateScreenLocked) {
            bundleArgs.clear()
        }
        lastAppliedWallpaperName = Wallpaper.DEFAULT
    }

    override fun onStart() {
        super.onStart()

        subscribeToTabCollections()

        requireComponents.backgroundServices.accountManagerAvailableQueue.runIfReadyOrQueue {
            // By the time this code runs, we may not be attached to a context or have a view lifecycle owner.
            if ((this@HomeFragment).view?.context == null) {
                return@runIfReadyOrQueue
            }

            requireComponents.backgroundServices.accountManager.register(
                object : AccountObserver {
                    override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
                        if (authType != AuthType.Existing) {
                            view?.let {
                                Snackbar.make(
                                    snackBarParentView = binding.dynamicSnackbarContainer,
                                    snackbarState = SnackbarState(
                                        message = it.context.getString(R.string.onboarding_firefox_account_sync_is_on),
                                    ),
                                ).show()
                            }
                        }
                    }
                },
                owner = this@HomeFragment.viewLifecycleOwner,
            )
        }

        // We only want this observer live just before we navigate away to the collection creation screen
        requireComponents.core.tabCollectionStorage.unregister(collectionStorageObserver)

        // Trigger review prompt logic and show the appropriate prompt variation if applicable
        requireComponents.appStore.dispatch(CheckIfEligibleForReviewPrompt)

        if (requireComponents.termsOfUseManager.shouldShowTermsOfUsePromptOnHomepage()) {
            findNavController().navigate(
                BrowserFragmentDirections.actionGlobalTermsOfUseDialog(),
            )
        }
    }

    @VisibleForTesting
    internal fun removeCollection(tabCollection: TabCollection) {
        lifecycleScope.launch(IO) {
            requireComponents.core.tabCollectionStorage.removeCollection(tabCollection)
        }
    }

    override fun onResume() {
        super.onResume()
        if (browsingModeManager.mode == BrowsingMode.Private) {
            activity?.window?.setBackgroundDrawableResource(R.drawable.private_home_background_gradient)
        }

        hideToolbar()

        val components = requireComponents
        // Whenever a tab is selected its last access timestamp is automatically updated by A-C.
        // However, in the case of resuming the app to the home fragment, we already have an
        // existing selected tab, but its last access timestamp is outdated. No action is
        // triggered to cause an automatic update on warm start (no tab selection occurs). So we
        // update it manually here.
        components.useCases.sessionUseCases.updateLastAccess()

        evaluateMessagesForMicrosurvey(components)

        maybeShowEncourageSearchCfr(
            canShowCfr = components.settings.canShowCfr,
            shouldShowCFR = components.settings.shouldShowSearchBarCFR,
            showCfr = ::showEncourageSearchCfr,
            recordExposure = { FxNimbus.features.encourageSearchCfr.recordExposure() },
        )

        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt =
            true
        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
            AuthenticationStatus.NOT_AUTHENTICATED
    }

    private fun evaluateMessagesForMicrosurvey(components: Components) =
        components.appStore.dispatch(MessagingAction.Evaluate(FenixMessageSurfaceId.MICROSURVEY))

    @VisibleForTesting
    internal fun maybeShowEncourageSearchCfr(
        canShowCfr: Boolean,
        shouldShowCFR: Boolean,
        showCfr: () -> Unit,
        recordExposure: () -> Unit,
    ) {
        if (canShowCfr && shouldShowCFR) {
            showCfr()
            recordExposure()
        }
    }

    override fun onPause() {
        super.onPause()
        if (browsingModeManager.mode == BrowsingMode.Private) {
            activity?.window?.setBackgroundDrawable(
                getColor(requireContext(), R.color.fx_mobile_private_layer_color_1).toDrawable(),
            )
        }

        // Counterpart to the update in onResume to keep the last access timestamp of the selected
        // tab up-to-date.
        requireComponents.useCases.sessionUseCases.updateLastAccess()
    }

    private fun subscribeToTabCollections(): Observer<List<TabCollection>> {
        return Observer<List<TabCollection>> {
            requireComponents.core.tabCollectionStorage.cachedTabCollections = it
            requireComponents.appStore.dispatch(AppAction.CollectionsChange(it))
        }.also { observer ->
            requireComponents.core.tabCollectionStorage.getCollections().observe(this, observer)
        }
    }

    private fun registerCollectionStorageObserver() {
        requireComponents.core.tabCollectionStorage.register(collectionStorageObserver, this)
    }

    private fun openTabsTray() {
        if (requireContext().settings().tabManagerEnhancementsEnabled) {
            findNavController().nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionGlobalTabManagementFragment(
                    page = when (browsingModeManager.mode) {
                        BrowsingMode.Normal -> Page.NormalTabs
                        BrowsingMode.Private -> Page.PrivateTabs
                    },
                ),
            )
        } else {
            findNavController().nav(
                R.id.homeFragment,
                HomeFragmentDirections.actionGlobalTabsTrayFragment(
                    page = when (browsingModeManager.mode) {
                        BrowsingMode.Normal -> Page.NormalTabs
                        BrowsingMode.Private -> Page.PrivateTabs
                    },
                ),
            )
        }
    }

    /**
     * Shows a prompt to add a search widget to the home screen if supported by the device.
     *
     * This function should be called when the fragment's view is active (e.g., in response
     * to a user interaction). It launches a coroutine within the [viewLifecycleOwner]'s
     * [androidx.lifecycle.LifecycleCoroutineScope] to display the widget prompt using
     * [showAddSearchWidgetPromptIfSupported].
     *
     * The actual display logic, including handling success and failure callbacks, is managed by
     * [showAddSearchWidgetPromptIfSupported].
     */
    private fun showAddSearchWidgetPrompt() {
        viewLifecycleOwner.lifecycleScope.launch {
            val currentPackageName = requireActivity().packageName
            val currentWidgetManager = AppWidgetManager.getInstance(requireContext())
            val currentAddWidgetSuccessCallback = WidgetPinnedReceiver.getPendingIntent(requireContext())

            showAddSearchWidgetPromptIfSupported(
                packageName = currentPackageName,
                appWidgetManager = currentWidgetManager,
                successCallback = currentAddWidgetSuccessCallback,
            )
        }
    }

    @VisibleForTesting
    internal fun shouldEnableWallpaper() =
        (activity as? HomeActivity)?.themeManager?.currentTheme?.isPrivate?.not() ?: false

    private fun applyWallpaper(wallpaperName: String, orientationChange: Boolean, orientation: Int) {
        when {
            !shouldEnableWallpaper() ||
                (wallpaperName == lastAppliedWallpaperName && !orientationChange) -> return
            Wallpaper.nameIsDefault(wallpaperName) -> {
                binding.wallpaperImageView.isVisible = false
                lastAppliedWallpaperName = wallpaperName
            }
            else -> {
                viewLifecycleOwner.lifecycleScope.launch {
                    // loadBitmap does file lookups based on name, so we don't need a fully
                    // qualified type to load the image
                    val wallpaper = Wallpaper.Default.copy(name = wallpaperName)
                    val wallpaperImage = requireComponents.useCases.wallpaperUseCases.loadBitmap(wallpaper, orientation)
                    wallpaperImage?.let {
                        it.scaleToBottomOfView(binding.wallpaperImageView)
                        binding.wallpaperImageView.isVisible = true
                        lastAppliedWallpaperName = wallpaperName
                    } ?: run {
                        if (!isActive) return@run
                        with(binding.wallpaperImageView) {
                            isVisible = false
                            showSnackBar(
                                view = binding.dynamicSnackbarContainer,
                                text = resources.getString(R.string.wallpaper_select_error_snackbar_message),
                            )
                        }
                        // If setting a wallpaper failed reset also the contrasting text color.
                        requireContext().settings().currentWallpaperTextColor = 0L
                        lastAppliedWallpaperName = Wallpaper.DEFAULT
                    }
                }
            }
        }
    }

    private fun observeWallpaperUpdates() {
        consumeFlow(requireComponents.appStore, viewLifecycleOwner) { flow ->
            flow.filter { it.mode == BrowsingMode.Normal }
                .map { it.wallpaperState.currentWallpaper }
                .distinctUntilChanged()
                .collect {
                    if (it.name != lastAppliedWallpaperName) {
                        applyWallpaper(
                            wallpaperName = it.name,
                            orientationChange = false,
                            orientation = requireContext().resources.configuration.orientation,
                        )
                    }
                }
        }
    }

    private fun initializeAwesomeBarComposable(
        toolbarStore: BrowserToolbarStore,
        modifier: Modifier,
    ) = context?.let {
        AwesomeBarComposable(
            activity = requireActivity() as HomeActivity,
            modifier = modifier,
            components = requireComponents,
            appStore = requireComponents.appStore,
            browserStore = requireComponents.core.store,
            toolbarStore = toolbarStore,
            navController = findNavController(),
            lifecycleOwner = this,
            tabId = args.sessionToStartSearchFor,
            searchAccessPoint = args.searchAccessPoint,
        ).also {
            awesomeBarComposable = it
        }
    }

    private fun observeReviewPromptState() {
        consumeFlow(requireComponents.appStore) { appStates ->
            observeReviewPromptState(
                appStates = appStates,
                dispatchAction = requireComponents.appStore::dispatch,
                tryShowPlayStorePrompt = {
                    requireComponents.playStoreReviewPromptController
                        .tryPromptReview(requireActivity())
                },
                showCustomPrompt = {
                    findNavController().navigate(
                        NavGraphDirections.actionGlobalCustomReviewPromptDialogFragment(),
                    )
                },
            )
        }
    }

    @VisibleForTesting
    internal suspend fun observeReviewPromptState(
        appStates: Flow<AppState>,
        dispatchAction: (AppAction) -> Unit,
        tryShowPlayStorePrompt: suspend () -> Unit,
        showCustomPrompt: () -> Unit,
    ) {
        appStates
            .map { it.reviewPrompt }
            .distinctUntilChanged()
            .collect {
                when (it) {
                    ReviewPromptState.Unknown, ReviewPromptState.NotEligible -> {}

                    is ReviewPromptState.Eligible -> {
                        when (it.type) {
                            Type.PlayStore -> tryShowPlayStorePrompt()
                            Type.Custom -> showCustomPrompt()
                        }
                        dispatchAction(ReviewPromptShown)
                    }
                }
            }
    }

    companion object {
        // Navigation arguments passed to HomeFragment
        const val FOCUS_ON_ADDRESS_BAR = "focusOnAddressBar"
        private const val SESSION_TO_DELETE = "sessionToDelete"

        // Elevation for undo toasts
        internal const val TOAST_ELEVATION = 80f

        private const val ENCOURAGE_SEARCH_CFR_VERTICAL_OFFSET = 0
    }
}
