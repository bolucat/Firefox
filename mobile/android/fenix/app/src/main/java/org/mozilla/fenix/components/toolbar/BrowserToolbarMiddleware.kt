/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.Lifecycle.State.RESUMED
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.CopyToClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.LoadFromClipboardClicked
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.PasteFromClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.NavigationActionsUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.UpdateProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.Init
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent.Source
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text.StringResText
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.applyRegistrableDomainSpan
import mozilla.components.support.ktx.kotlin.getOrigin
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.kotlin.isUrl
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.support.utils.ClipboardHandler
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.AddressToolbar
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Normal
import org.mozilla.fenix.browser.browsingmode.BrowsingMode.Private
import org.mozilla.fenix.browser.store.BrowserScreenAction
import org.mozilla.fenix.browser.store.BrowserScreenStore
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.NimbusComponents
import org.mozilla.fenix.components.UseCases
import org.mozilla.fenix.components.appstate.AppAction.BookmarkAction
import org.mozilla.fenix.components.appstate.AppAction.CurrentTabClosed
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction.SnackbarDismissed
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.toolbar.DisplayActions.AddBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.EditBookmarkClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateBackLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.NavigateForwardLongClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.RefreshClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.ShareClicked
import org.mozilla.fenix.components.toolbar.DisplayActions.StopRefreshClicked
import org.mozilla.fenix.components.toolbar.PageEndActionsInteractions.ReaderModeClicked
import org.mozilla.fenix.components.toolbar.PageEndActionsInteractions.TranslateClicked
import org.mozilla.fenix.components.toolbar.PageOriginInteractions.OriginClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewPrivateTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.AddNewTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.CloseCurrentTab
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterClicked
import org.mozilla.fenix.components.toolbar.TabCounterInteractions.TabCounterLongClicked
import org.mozilla.fenix.ext.isLargeWindow
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.quicksettings.protections.cookiebanners.getCookieBannerUIMode
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.tabstray.ext.isActiveDownload
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.lastSavedFolderCache
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.ui.icons.R as iconsR

private const val TALL_SCREEN_HEIGHT_DP = 480

@VisibleForTesting
internal sealed class DisplayActions : BrowserToolbarEvent {
    data class MenuClicked(override val source: Source) : DisplayActions()
    data object NavigateBackClicked : DisplayActions()
    data object NavigateBackLongClicked : DisplayActions()
    data object NavigateForwardClicked : DisplayActions()
    data object NavigateForwardLongClicked : DisplayActions()
    data class RefreshClicked(val bypassCache: Boolean) : DisplayActions()
    data object StopRefreshClicked : DisplayActions()
    data class AddBookmarkClicked(override val source: Source) : DisplayActions()
    data class EditBookmarkClicked(override val source: Source) : DisplayActions()
    data class ShareClicked(override val source: Source) : DisplayActions()
}

@VisibleForTesting
internal sealed class StartPageActions : BrowserToolbarEvent {
    data object SiteInfoClicked : StartPageActions()
}

@VisibleForTesting
internal sealed class TabCounterInteractions : BrowserToolbarEvent {
    data class TabCounterClicked(override val source: Source) : TabCounterInteractions()
    data class TabCounterLongClicked(override val source: Source) : TabCounterInteractions()
    data class AddNewTab(override val source: Source) : TabCounterInteractions()
    data class AddNewPrivateTab(override val source: Source) : TabCounterInteractions()
    data object CloseCurrentTab : TabCounterInteractions()
}

@VisibleForTesting
internal sealed class PageOriginInteractions : BrowserToolbarEvent {
    data object OriginClicked : PageOriginInteractions()
}

@VisibleForTesting
internal sealed class PageEndActionsInteractions : BrowserToolbarEvent {
    data class ReaderModeClicked(
        val isActive: Boolean,
    ) : PageEndActionsInteractions()

    data object TranslateClicked : PageEndActionsInteractions()
}

/**
 * Helper function to determine whether the app's current window height
 * is at least more than [TALL_SCREEN_HEIGHT_DP].
 *
 * This is useful when navigation bar should only be enabled on
 * taller screens (e.g., to avoid crowding content vertically).
 *
 * @return true if the window height size is more than [TALL_SCREEN_HEIGHT_DP].
 */
@VisibleForTesting
internal fun Context.isTallWindow(): Boolean {
    return resources.configuration.screenHeightDp > TALL_SCREEN_HEIGHT_DP
}

/**
 * [Middleware] responsible for configuring and handling interactions with the composable toolbar.
 *
 * @param appStore [AppStore] allowing to integrate with other features of the applications.
 * @param browserScreenStore [BrowserScreenStore] used for integration with other browser screen functionalities.
 * @param browserStore [BrowserStore] to sync from.
 * @param permissionsStorage [SitePermissionsStorage] to find currently selected tab site permissions.
 * @param cookieBannersStorage [CookieBannersStorage] to get the current status of cookie banner ui mode.
 * @param trackingProtectionUseCases [TrackingProtectionUseCases] allowing to query
 * tracking protection data of the current tab.
 * @param useCases [UseCases] helping this integrate with other features of the applications.
 * @param nimbusComponents [NimbusComponents] used for accessing Nimbus events to use in telemetry.
 * @param clipboard [ClipboardHandler] to use for reading from device's clipboard.
 * @param publicSuffixList [PublicSuffixList] used to obtain the base domain of the current site.
 * @param settings [Settings] for accessing user preferences.
 * @param sessionUseCases [SessionUseCases] for interacting with the current session.
 * @param bookmarksStorage [BookmarksStorage] to read and write bookmark data related to the current site.
 */
@Suppress("LargeClass", "LongParameterList", "TooManyFunctions")
class BrowserToolbarMiddleware(
    private val appStore: AppStore,
    private val browserScreenStore: BrowserScreenStore,
    private val browserStore: BrowserStore,
    private val permissionsStorage: SitePermissionsStorage,
    private val cookieBannersStorage: CookieBannersStorage,
    private val trackingProtectionUseCases: TrackingProtectionUseCases,
    private val useCases: UseCases,
    private val nimbusComponents: NimbusComponents,
    private val clipboard: ClipboardHandler,
    private val publicSuffixList: PublicSuffixList,
    private val settings: Settings,
    private val sessionUseCases: SessionUseCases = SessionUseCases(browserStore),
    private val bookmarksStorage: BookmarksStorage,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    @VisibleForTesting
    internal var environment: BrowserToolbarEnvironment? = null

    @Suppress("LongMethod", "CyclomaticComplexMethod", "NestedBlockDepth", "ReturnCount")
    override fun invoke(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is Init -> {
                next(action)

                appStore.dispatch(SearchEnded)

                updateStartPageActions(context)
            }

            is EnvironmentRehydrated -> {
                next(action)

                environment = action.environment as? BrowserToolbarEnvironment

                updateStartBrowserActions(context)
                updateCurrentPageOrigin(context)
                updateEndBrowserActions(context)
                updateEndPageActions(context)
                environment?.viewLifecycleOwner?.lifecycleScope?.launch {
                    updateNavigationActions(context)
                }

                observeProgressBarUpdates(context)
                observeOrientationChanges(context)
                observeTabsCountUpdates(context)
                observeAcceptingCancellingPrivateDownloads(context)
                observePageNavigationStatus(context)
                observePageOriginUpdates(context)
                observeSelectedTabBookmarkedUpdates(context)
                observeReaderModeUpdates(context)
                observePageTranslationsUpdates(context)
                observePageRefreshUpdates(context)
                observePageSecurityUpdates(context)
            }

            is EnvironmentCleared -> {
                next(action)

                environment = null
            }

            is StartPageActions.SiteInfoClicked -> {
                onSiteInfoClicked()
            }

            is MenuClicked -> {
                environment?.navController?.nav(
                    R.id.browserFragment,
                    BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                        accesspoint = MenuAccessPoint.Browser,
                    ),
                )

                next(action)
            }

            is TabCounterClicked -> {
                runWithinEnvironment {
                    thumbnailsFeature?.requestScreenshot()

                    if (settings.tabManagerEnhancementsEnabled) {
                        navController.nav(
                            R.id.browserFragment,
                            BrowserFragmentDirections.actionGlobalTabManagementFragment(
                                page = when (browsingModeManager.mode) {
                                    Normal -> Page.NormalTabs
                                    Private -> Page.PrivateTabs
                                },
                            ),
                        )
                    } else {
                        navController.nav(
                            R.id.browserFragment,
                            BrowserFragmentDirections.actionGlobalTabsTrayFragment(
                                page = when (browsingModeManager.mode) {
                                    Normal -> Page.NormalTabs
                                    Private -> Page.PrivateTabs
                                },
                            ),
                        )
                    }
                }

                next(action)
            }
            is AddNewTab -> {
                openNewTab(Normal)
                next(action)
            }
            is AddNewPrivateTab -> {
                openNewTab(Private)
                next(action)
            }
            is CloseCurrentTab -> {
                browserStore.state.selectedTab?.let { selectedTab ->
                    val isLastTab = browserStore.state.getNormalOrPrivateTabs(selectedTab.content.private).size == 1

                    if (!isLastTab) {
                        useCases.tabsUseCases.removeTab(selectedTab.id, selectParentIfExists = true)
                        appStore.dispatch(CurrentTabClosed(selectedTab.content.private))
                        return@let
                    }

                    if (!selectedTab.content.private) {
                        runWithinEnvironment {
                            navController.navigate(
                                BrowserFragmentDirections.actionGlobalHome(
                                    sessionToDelete = selectedTab.id,
                                ),
                            )
                            return@let
                        }
                    }

                    val privateDownloads = browserStore.state.downloads.filter {
                        it.value.private && it.value.isActiveDownload()
                    }
                    if (privateDownloads.isNotEmpty() && !browserScreenStore.state.cancelPrivateDownloadsAccepted) {
                        browserScreenStore.dispatch(
                            BrowserScreenAction.ClosingLastPrivateTab(
                                tabId = selectedTab.id,
                                inProgressPrivateDownloads = privateDownloads.size,
                            ),
                        )
                    } else {
                        runWithinEnvironment {
                            navController.navigate(
                                BrowserFragmentDirections.actionGlobalHome(
                                    sessionToDelete = selectedTab.id,
                                ),
                            )
                        }
                    }
                }
            }

            is OriginClicked -> {
                when (environment?.navController?.currentDestination?.id) {
                    R.id.browserFragment -> Events.searchBarTapped.record(Events.SearchBarTappedExtra("BROWSER"))
                    R.id.homeFragment -> Events.searchBarTapped.record(Events.SearchBarTappedExtra("HOME"))
                }

                val selectedTab = browserStore.state.selectedTab ?: return
                val searchTerms = selectedTab.content.searchTerms
                if (searchTerms.isBlank()) {
                    runWithinEnvironment {
                        navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(
                                focusOnAddressBar = true,
                                sessionToStartSearchFor = selectedTab.id,
                            ),
                        )
                    }
                } else {
                    context.dispatch(SearchQueryUpdated(searchTerms))
                    appStore.dispatch(SearchStarted(selectedTab.id))
                }
            }
            is CopyToClipboardClicked -> {
                Events.copyUrlTapped.record(NoExtras())

                val selectedTab = browserStore.state.selectedTab
                val url = selectedTab?.readerState?.activeUrl ?: selectedTab?.content?.url
                clipboard.text = url

                // Android 13+ shows by default a popup for copied text.
                // Avoid overlapping popups informing the user when the URL is copied to the clipboard.
                // and only show our snackbar when Android will not show an indication by default.
                // See https://developer.android.com/develop/ui/views/touch-and-input/copy-paste#duplicate-notifications).
                if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
                    appStore.dispatch(URLCopiedToClipboard)
                }
            }
            is PasteFromClipboardClicked -> runWithinEnvironment {
                context.dispatch(SearchQueryUpdated(clipboard.text.orEmpty()))
                appStore.dispatch(SearchStarted(browserStore.state.selectedTabId))
            }
            is LoadFromClipboardClicked -> {
                clipboard.extractURL()?.let {
                    val searchEngine = reconcileSelectedEngine()
                    val selectedTabId = browserStore.state.selectedTabId ?: return
                    if (it.isUrl() || searchEngine == null) {
                        browserStore.dispatch(
                            ContentAction.UpdateSearchTermsAction(
                                selectedTabId,
                                "",
                            ),
                        )
                        Events.enteredUrl.record(Events.EnteredUrlExtra(autocomplete = false))
                    } else {
                        browserStore.dispatch(
                            ContentAction.UpdateSearchTermsAction(
                                selectedTabId,
                                it,
                            ),
                        )
                        val searchAccessPoint = MetricsUtils.Source.ACTION
                        MetricsUtils.recordSearchMetrics(
                            engine = searchEngine,
                            isDefault = true,
                            searchAccessPoint = searchAccessPoint,
                            nimbusEventStore = nimbusComponents.events,
                        )
                    }

                    useCases.fenixBrowserUseCases.loadUrlOrSearch(
                        searchTermOrURL = it,
                        newTab = false,
                        searchEngine = searchEngine,
                        private = environment?.browsingModeManager?.mode == Private,
                    )
                } ?: run {
                    Logger("BrowserOriginContextMenu").error("Clipboard contains URL but unable to read text")
                }
            }
            is NavigateBackClicked -> {
                browserStore.state.selectedTab?.let {
                    browserStore.dispatch(EngineAction.GoBackAction(it.id))
                }
                next(action)
            }
            is NavigateBackLongClicked -> {
                showTabHistory()
                next(action)
            }
            is NavigateForwardClicked -> {
                browserStore.state.selectedTab?.let {
                    browserStore.dispatch(EngineAction.GoForwardAction(it.id))
                }
                next(action)
            }
            is NavigateForwardLongClicked -> {
                showTabHistory()
                next(action)
            }

            is ReaderModeClicked -> runWithinEnvironment {
                when (action.isActive) {
                    true -> {
                        ReaderMode.closed.record(NoExtras())
                        readerModeController?.hideReaderView()
                    }
                    false -> {
                        ReaderMode.opened.record(NoExtras())
                        readerModeController?.showReaderView()
                    }
                }
            }

            is TranslateClicked -> {
                Translations.action.record(Translations.ActionExtra("main_flow_toolbar"))

                appStore.dispatch(SnackbarDismissed)
                runWithinEnvironment {
                    navController.navigateSafe(
                        resId = R.id.browserFragment,
                        directions = BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment(),
                    )
                }
            }

            is RefreshClicked -> {
                AddressToolbar.reloadTapped.record((NoExtras()))

                val tabId = browserStore.state.selectedTabId
                if (action.bypassCache) {
                    sessionUseCases.reload.invoke(
                        tabId,
                        flags = LoadUrlFlags.select(
                            LoadUrlFlags.BYPASS_CACHE,
                        ),
                    )
                } else {
                    sessionUseCases.reload(tabId)
                }
                next(action)
            }
            is StopRefreshClicked -> {
                val tabId = browserStore.state.selectedTabId
                sessionUseCases.stopLoading(tabId)
                next(action)
            }

            is AddBookmarkClicked -> {
                val selectedTab = browserStore.state.selectedTab

                selectedTab?.let {
                    environment?.viewLifecycleOwner?.lifecycleScope?.launch(Dispatchers.IO) {
                        val parentGuid = settings.lastSavedFolderCache.getGuid() ?: BookmarkRoot.Mobile.id
                        val parentNode = bookmarksStorage.getBookmark(parentGuid)
                        val guidToEdit = useCases.bookmarksUseCases.addBookmark(
                            url = selectedTab.content.url,
                            title = selectedTab.content.title,
                            parentGuid = parentGuid,
                        )

                        appStore.dispatch(
                            BookmarkAction.BookmarkAdded(
                                guidToEdit = guidToEdit,
                                parentNode = parentNode,
                                source = action.source.toMetricSource(),
                            ),
                        )
                    }
                }

                next(action)
            }

            is EditBookmarkClicked -> runWithinEnvironment {
                val selectedTab = browserStore.state.selectedTab ?: return

                environment?.viewLifecycleOwner?.lifecycleScope?.launch(Dispatchers.Main) {
                    val guidToEdit: String? = withContext(Dispatchers.IO) {
                      bookmarksStorage
                        .getBookmarksWithUrl(selectedTab.content.url)
                        .firstOrNull()
                        ?.guid
                    }

                    guidToEdit?.let { guid ->
                        navController.navigateSafe(
                            R.id.browserFragment,
                            BrowserFragmentDirections.actionGlobalBookmarkEditFragment(
                                guidToEdit = guid,
                                requiresSnackbarPaddingForToolbar = true,
                            ),
                        )
                    }
                }

                next(action)
            }

            is ShareClicked -> runWithinEnvironment {
                AddressToolbar.shareTapped.record((NoExtras()))

                val selectedTab = browserStore.state.selectedTab ?: return
                if (selectedTab.content.url.isContentUrl()) {
                    browserStore.dispatch(
                        ShareResourceAction.AddShareAction(
                            selectedTab.id,
                            ShareResourceState.LocalResource(selectedTab.content.url),
                        ),
                    )
                } else {
                    navController.nav(
                        R.id.browserFragment,
                        BrowserFragmentDirections.actionGlobalShareFragment(
                            sessionId = selectedTab.id,
                            data = arrayOf(
                                ShareData(
                                    url = selectedTab.content.url,
                                    title = selectedTab.content.title,
                                ),
                            ),
                            showPage = true,
                        ),
                    )
                }

                next(action)
            }

            else -> next(action)
        }
    }

    private fun showTabHistory() = runWithinEnvironment {
        navController.nav(
            R.id.browserFragment,
            BrowserFragmentDirections.actionGlobalTabHistoryDialogFragment(
                activeSessionId = null,
            ),
        )
    }

    private fun onSiteInfoClicked() {
        val tab = browserStore.state.selectedTab ?: return
        val scope = environment?.viewLifecycleOwner?.lifecycleScope ?: return
        scope.launch(Dispatchers.IO) {
            val sitePermissions: SitePermissions? = tab.content.url.getOrigin()?.let { origin ->
                permissionsStorage.findSitePermissionsBy(origin, private = tab.content.private)
            }

            scope.launch(Dispatchers.Main) {
                trackingProtectionUseCases.containsException(tab.id) { hasTrackingProtectionException ->
                    scope.launch {
                        val cookieBannerUIMode = cookieBannersStorage.getCookieBannerUIMode(
                            tab = tab,
                            isFeatureEnabledInPrivateMode = settings.shouldUseCookieBannerPrivateMode,
                            publicSuffixList = publicSuffixList,
                        )

                        val isTrackingProtectionEnabled =
                            tab.trackingProtection.enabled && !hasTrackingProtectionException
                        val directions = if (settings.enableUnifiedTrustPanel) {
                            BrowserFragmentDirections.actionBrowserFragmentToTrustPanelFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isSecured = tab.content.securityInfo.secure,
                                sitePermissions = sitePermissions,
                                certificateName = tab.content.securityInfo.issuer,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        } else {
                            BrowserFragmentDirections.actionBrowserFragmentToQuickSettingsSheetDialogFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isLocalPdf = tab.content.url.isContentUrl(),
                                isSecured = tab.content.securityInfo.secure,
                                sitePermissions = sitePermissions,
                                gravity = settings.toolbarPosition.androidGravity,
                                certificateName = tab.content.securityInfo.issuer,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        }
                        environment?.navController?.nav(
                            R.id.browserFragment,
                            directions,
                        )
                    }
                }
            }
        }
    }

    private fun updateStartBrowserActions(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) =
        context.dispatch(
            BrowserActionsStartUpdated(
                buildStartBrowserActions(),
            ),
        )

    private fun updateStartPageActions(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) =
        context.dispatch(
            BrowserDisplayToolbarAction.PageActionsStartUpdated(
                buildStartPageActions(),
            ),
    )

    private fun updateEndBrowserActions(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) =
        context.dispatch(
            BrowserActionsEndUpdated(
                buildEndBrowserActions(),
            ),
    )

    private fun buildStartPageActions(): List<Action> {
        return listOf(
            ToolbarActionConfig(ToolbarAction.SiteInfo),
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action)
        }
    }

    private fun updateEndPageActions(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) =
        context.dispatch(
            PageActionsEndUpdated(
                buildEndPageActions(),
            ),
    )

    private fun buildStartBrowserActions(): List<Action> {
        val environment = environment ?: return emptyList()
        val isWideScreen = environment.context.isLargeWindow()
        val isShortScreen = !environment.context.isTallWindow()
        val shouldNavigationButtonBeVisible = isWideScreen || (settings.shouldUseExpandedToolbar && isShortScreen)

        return listOf(
            ToolbarActionConfig(ToolbarAction.Back) { shouldNavigationButtonBeVisible },
            ToolbarActionConfig(ToolbarAction.Forward) { shouldNavigationButtonBeVisible },
            ToolbarActionConfig(ToolbarAction.RefreshOrStop) { shouldNavigationButtonBeVisible },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action)
        }
    }

    private fun buildEndPageActions(): List<Action> {
        val isWideOrShortScreen = environment?.context?.isLargeWindow() == true ||
                environment?.context?.isTallWindow() == false
        val isExpandedAndTallScreen = settings.shouldUseExpandedToolbar &&
                environment?.context?.isTallWindow() == true

        return listOf(
            ToolbarActionConfig(ToolbarAction.ReaderMode) {
                browserScreenStore.state.readerModeStatus.isAvailable
            },
            ToolbarActionConfig(ToolbarAction.Translate) {
                browserScreenStore.state.pageTranslationStatus.isTranslationPossible &&
                    (settings.shouldUseExpandedToolbar || isWideOrShortScreen) &&
                    FxNimbus.features.translations.value().mainFlowToolbarEnabled
            },
            ToolbarActionConfig(ToolbarAction.Share) {
                isWideOrShortScreen && !settings.isTabStripEnabled && !isExpandedAndTallScreen
            },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action)
        }
    }

    private fun buildEndBrowserActions(): List<Action> {
        val isWideOrShortScreen = environment?.context?.isLargeWindow() == true ||
                environment?.context?.isTallWindow() == false
        val isExpandedAndTallScreen = settings.shouldUseExpandedToolbar &&
                environment?.context?.isTallWindow() == true

        return listOf(
            ToolbarActionConfig(ToolbarAction.NewTab) {
                !settings.isTabStripEnabled && !isExpandedAndTallScreen
            },
            ToolbarActionConfig(ToolbarAction.TabCounter) {
                !settings.isTabStripEnabled && !isExpandedAndTallScreen
            },
            ToolbarActionConfig(ToolbarAction.Share) {
                isWideOrShortScreen && settings.isTabStripEnabled && !isExpandedAndTallScreen
            },
            ToolbarActionConfig(ToolbarAction.Menu) { !isExpandedAndTallScreen },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action)
        }
    }

    private fun buildNavigationActions(isBookmarked: Boolean): List<Action> {
        val isExpandedAndTallScreen = settings.shouldUseExpandedToolbar &&
                environment?.context?.isTallWindow() == true

        return listOf(
            ToolbarActionConfig(ToolbarAction.Bookmark) {
                isExpandedAndTallScreen && !isBookmarked
            },
            ToolbarActionConfig(ToolbarAction.EditBookmark) {
                isExpandedAndTallScreen && isBookmarked
            },
            ToolbarActionConfig(ToolbarAction.Share) { isExpandedAndTallScreen },
            ToolbarActionConfig(ToolbarAction.NewTab) { isExpandedAndTallScreen },
            ToolbarActionConfig(ToolbarAction.TabCounter) { isExpandedAndTallScreen },
            ToolbarActionConfig(ToolbarAction.Menu) { isExpandedAndTallScreen },
        ).filter { config ->
            config.isVisible()
        }.map { config ->
            buildAction(config.action, Source.NavigationBar)
        }
    }

    private suspend fun updateNavigationActions(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        val isBookmarked =
            browserStore.state.selectedTab?.content?.url?.let {
                bookmarksStorage.getBookmarksWithUrl(it).isNotEmpty()
            } == true

        context.dispatch(
            NavigationActionsUpdated(
                buildNavigationActions(isBookmarked),
            ),
        )
    }

    private fun buildTabCounterMenu(source: Source) =
        CombinedEventAndMenu(TabCounterLongClicked(source)) {
        listOf(
            BrowserToolbarMenuButton(
                icon = DrawableResIcon(iconsR.drawable.mozac_ic_plus_24),
                text = StringResText(R.string.mozac_browser_menu_new_tab),
                contentDescription = StringResContentDescription(R.string.mozac_browser_menu_new_tab),
                onClick = AddNewTab(source),
            ),

            BrowserToolbarMenuButton(
                icon = DrawableResIcon(iconsR.drawable.mozac_ic_private_mode_24),
                text = StringResText(R.string.mozac_browser_menu_new_private_tab),
                contentDescription = StringResContentDescription(R.string.mozac_browser_menu_new_private_tab),
                onClick = AddNewPrivateTab(source),
            ),

            BrowserToolbarMenuDivider,

            BrowserToolbarMenuButton(
                icon = DrawableResIcon(iconsR.drawable.mozac_ic_cross_24),
                text = StringResText(R.string.mozac_close_tab),
                contentDescription = StringResContentDescription(R.string.mozac_close_tab),
                onClick = CloseCurrentTab,
            ),
        )
    }

    private fun buildProgressBar(progress: Int = 0) = ProgressBarConfig(progress)

    private fun openNewTab(
        browsingMode: BrowsingMode,
    ) = runWithinEnvironment {
        browsingModeManager.mode = browsingMode
        navController.navigate(
            BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
        )
    }

    private fun observeProgressBarUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.progress }
            .collect {
                context.dispatch(
                    UpdateProgressBarConfig(
                        buildProgressBar(it.selectedTab?.content?.progress ?: 0),
                    ),
                )
            }
        }
    }

    private fun observeOrientationChanges(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        appStore.observeWhileActive {
            distinctUntilChangedBy { it.orientation }
            .collect {
                updateStartBrowserActions(context)
                updateEndBrowserActions(context)
                updateEndPageActions(context)
                updateNavigationActions(context)
            }
        }
    }

    private fun observeTabsCountUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.tabs.size }
            .collect {
                updateEndBrowserActions(context)
                updateNavigationActions(context)
            }
        }
    }

    private fun observePageOriginUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.url }
            .collect {
                updateCurrentPageOrigin(context)
                updateNavigationActions(context)
            }
        }
    }

    private fun updateCurrentPageOrigin(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
    ) = environment?.viewLifecycleOwner?.lifecycleScope?.launch {
        val url = browserStore.state.selectedTab?.content?.url?.let {
            it.applyRegistrableDomainSpan(publicSuffixList)
        }
        val searchTerms = browserStore.state.selectedTab?.content?.searchTerms ?: ""

        val displayUrl = url?.let { originalUrl ->
            if (originalUrl.toString() == ABOUT_HOME_URL) {
                // Default to showing the toolbar hint when the URL is ABOUT_HOME.
                ""
            } else if (searchTerms.isNotBlank()) {
                searchTerms
            } else {
                URLStringUtils.toDisplayUrl(originalUrl)
            }
        }

        context.dispatch(
            BrowserDisplayToolbarAction.PageOriginUpdated(
                PageOrigin(
                    hint = R.string.search_hint,
                    title = null,
                    url = displayUrl,
                    contextualMenuOptions = ContextualMenuOption.entries,
                    onClick = OriginClicked,
                ),
            ),
        )
    }

    private fun observePageSecurityUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.securityInfo }
                .collect {
                    updateStartPageActions(context)
                }
        }
    }

    private fun observeAcceptingCancellingPrivateDownloads(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.cancelPrivateDownloadsAccepted }
            .collect {
                if (it.cancelPrivateDownloadsAccepted) {
                    context.dispatch(CloseCurrentTab)
                }
            }
        }
    }

    private fun observeReaderModeUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.readerModeStatus }
                .collect {
                    updateEndPageActions(context)
                }
        }
    }

    private fun observePageTranslationsUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserScreenStore.observeWhileActive {
            distinctUntilChangedBy { it.pageTranslationStatus }
            .collect {
                updateEndPageActions(context)
            }
        }
    }

    private fun observePageNavigationStatus(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy {
                arrayOf(
                    it.selectedTab?.content?.canGoBack,
                    it.selectedTab?.content?.canGoForward,
                )
            }.collect {
                updateStartBrowserActions(context)
            }
        }
    }

    private fun observePageRefreshUpdates(context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            distinctUntilChangedBy { it.selectedTab?.content?.loading == true }
                .collect { updateStartBrowserActions(context) }
        }
    }

    private fun observeSelectedTabBookmarkedUpdates(
        context: MiddlewareContext<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        appStore.observeWhileActive {
            distinctUntilChangedBy {
                it.snackbarState is SnackbarState.BookmarkAdded ||
                        it.snackbarState is SnackbarState.BookmarkDeleted
            }.collect { isBookmarked ->
                updateNavigationActions(context)
            }
        }
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job? = environment?.viewLifecycleOwner?.run {
        lifecycleScope.launch {
            repeatOnLifecycle(RESUMED) {
                flow().observe()
            }
        }
    }

    private inline fun runWithinEnvironment(
        block: BrowserToolbarEnvironment.() -> Unit,
    ) = environment?.let { block(it) }

    @VisibleForTesting
    internal enum class ToolbarAction {
        NewTab,
        Back,
        Forward,
        RefreshOrStop,
        Menu,
        ReaderMode,
        Translate,
        TabCounter,
        SiteInfo,
        Bookmark,
        EditBookmark,
        Share,
    }

    private data class ToolbarActionConfig(
        val action: ToolbarAction,
        val isVisible: () -> Boolean = { true },
    )

    private fun reconcileSelectedEngine(): SearchEngine? =
        appStore.state.searchState.selectedSearchEngine?.searchEngine
            ?: browserStore.state.search.selectedOrDefaultSearchEngine

    @Suppress("LongMethod")
    @VisibleForTesting
    internal fun buildAction(
        toolbarAction: ToolbarAction,
        source: Source = Source.AddressBar,
    ): Action = when (toolbarAction) {
        ToolbarAction.NewTab -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_plus_24,
            contentDescription = if (environment?.browsingModeManager?.mode == Private) {
                R.string.home_screen_shortcut_open_new_private_tab_2
            } else {
                R.string.home_screen_shortcut_open_new_tab_2
            },
            onClick = if (environment?.browsingModeManager?.mode == Private) {
                AddNewPrivateTab(source)
            } else {
                AddNewTab(source)
            },
        )

        ToolbarAction.Back -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_back_24,
            contentDescription = R.string.browser_menu_back,
            state = if (browserStore.state.selectedTab?.content?.canGoBack == true) {
                ActionButton.State.DEFAULT
            } else {
                ActionButton.State.DISABLED
            },
            onClick = NavigateBackClicked,
            onLongClick = NavigateBackLongClicked,
        )

        ToolbarAction.Forward -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_forward_24,
            contentDescription = R.string.browser_menu_forward,
            state = if (browserStore.state.selectedTab?.content?.canGoForward == true) {
                ActionButton.State.DEFAULT
            } else {
                ActionButton.State.DISABLED
            },
            onClick = NavigateForwardClicked,
            onLongClick = NavigateForwardLongClicked,
        )

        ToolbarAction.RefreshOrStop -> {
            if (browserStore.state.selectedTab?.content?.loading != true) {
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_arrow_clockwise_24,
                    contentDescription = R.string.browser_menu_refresh,
                    onClick = RefreshClicked(bypassCache = false),
                    onLongClick = RefreshClicked(bypassCache = true),
                )
            } else {
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_cross_24,
                    contentDescription = R.string.browser_menu_stop,
                    onClick = StopRefreshClicked,
                )
            }
        }

        ToolbarAction.Menu -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            onClick = MenuClicked(source),
        )

        ToolbarAction.ReaderMode -> ActionButtonRes(
            drawableResId = R.drawable.ic_readermode,
            contentDescription = if (browserScreenStore.state.readerModeStatus.isActive) {
                R.string.browser_menu_read_close
            } else {
                R.string.browser_menu_read
            },
            state = if (browserScreenStore.state.readerModeStatus.isActive) {
                ActionButton.State.ACTIVE
            } else {
                ActionButton.State.DEFAULT
            },
            onClick = ReaderModeClicked(browserScreenStore.state.readerModeStatus.isActive),
        )

        ToolbarAction.Translate -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_translate_24,
            contentDescription = R.string.browser_toolbar_translate,
            state = if (browserScreenStore.state.pageTranslationStatus.isTranslated) {
                ActionButton.State.ACTIVE
            } else {
                ActionButton.State.DEFAULT
            },
            onClick = TranslateClicked,
        )

        ToolbarAction.TabCounter -> {
            val environment = requireNotNull(environment)
            val isInPrivateMode = environment.browsingModeManager.mode.isPrivate
            val tabsCount = browserStore.state.getNormalOrPrivateTabs(isInPrivateMode).size

            val tabCounterDescription = if (isInPrivateMode) {
                environment.context.getString(
                    R.string.mozac_tab_counter_private,
                    tabsCount.toString(),
                )
            } else {
                environment.context.getString(
                    R.string.mozac_tab_counter_open_tab_tray,
                    tabsCount.toString(),
                )
            }

            TabCounterAction(
                count = tabsCount,
                contentDescription = tabCounterDescription,
                showPrivacyMask = isInPrivateMode,
                onClick = TabCounterClicked(source),
                onLongClick = buildTabCounterMenu(source),
            )
        }

        ToolbarAction.SiteInfo -> {
            if (browserStore.state.selectedTab?.content?.url?.isContentUrl() == true) {
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_page_portrait_24,
                    contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            } else if (browserStore.state.selectedTab?.content?.securityInfo?.secure == true) {
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_shield_checkmark_24,
                    contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            } else {
                ActionButtonRes(
                    drawableResId = R.drawable.mozac_ic_shield_slash_24,
                    contentDescription = R.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = StartPageActions.SiteInfoClicked,
                )
            }
        }

        ToolbarAction.Bookmark -> {
            ActionButtonRes(
                drawableResId = R.drawable.mozac_ic_bookmark_24,
                contentDescription = R.string.browser_menu_bookmark_this_page_2,
                onClick = AddBookmarkClicked(source),
            )
        }

        ToolbarAction.EditBookmark -> {
            ActionButtonRes(
                drawableResId = R.drawable.mozac_ic_bookmark_fill_24,
                contentDescription = R.string.browser_menu_edit_bookmark,
                onClick = EditBookmarkClicked(source),
            )
        }

        ToolbarAction.Share -> ActionButtonRes(
            drawableResId = R.drawable.mozac_ic_share_android_24,
            contentDescription = R.string.browser_menu_share,
            onClick = ShareClicked(source),
        )
    }

    private fun Source.toMetricSource() = when (this) {
        Source.AddressBar -> MetricsUtils.BookmarkAction.Source.BROWSER_TOOLBAR
        Source.NavigationBar -> MetricsUtils.BookmarkAction.Source.BROWSER_NAVBAR
    }
}
