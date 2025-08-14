/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import androidx.annotation.DrawableRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.ui.platform.ComposeView
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.constraintlayout.widget.ConstraintSet
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.isVisible
import androidx.core.view.updateLayoutParams
import androidx.navigation.fragment.findNavController
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.support.ktx.android.content.res.resolveAttribute
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.databinding.FragmentHomeBinding
import org.mozilla.fenix.databinding.FragmentHomeToolbarViewLayoutBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.increaseTapAreaVertically
import org.mozilla.fenix.ext.isLargeWindow
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.HomeMenuView
import org.mozilla.fenix.search.toolbar.SearchSelector
import org.mozilla.fenix.utils.ToolbarPopupWindow
import java.lang.ref.WeakReference

/**
 * View class for setting up the home screen toolbar.
 */
internal class HomeToolbarView(
    private val homeBinding: FragmentHomeBinding,
    private val interactor: ToolbarInteractor,
    private val homeFragment: HomeFragment,
    private val homeActivity: HomeActivity,
) : FenixHomeToolbar {
    private var context = homeFragment.requireContext()

    override val layout: View = homeBinding.toolbarLayoutStub.inflate()
    private val toolbarBinding = FragmentHomeToolbarViewLayoutBinding.bind(layout)

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val menuButton = toolbarBinding.menuButton

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val tabButton = toolbarBinding.tabButton

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var tabCounterView: TabCounterView? = null

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var homeMenuView: HomeMenuView? = null

    init {
        initLayoutParameters()
    }

    override fun build(browserState: BrowserState, middleSearchEnabled: Boolean) {
        initLayoutParameters()

        toolbarBinding.toolbarText.compoundDrawablePadding =
            context.pixelSizeFor(R.dimen.search_bar_search_engine_icon_padding)

        toolbarBinding.toolbarWrapper.setOnClickListener {
            interactor.onNavigateSearch()
        }

        toolbarBinding.toolbarWrapper.setOnLongClickListener {
            ToolbarPopupWindow.show(
                WeakReference(it),
                WeakReference(homeBinding.dynamicSnackbarContainer),
                handlePasteAndGo = interactor::onPasteAndGo,
                handlePaste = interactor::onPaste,
                copyVisible = false,
            )
            true
        }

        toolbarBinding.toolbarWrapper.increaseTapAreaVertically(
            TOOLBAR_WRAPPER_INCREASE_HEIGHT_DPS,
        )

        updateButtonVisibility(browserState)
        updateAddressBarVisibility(!middleSearchEnabled)
    }

    override fun updateButtonVisibility(browserState: BrowserState) {
        val showMenu = true
        val showTabCounter = !context.settings().isTabStripEnabled
        toolbarBinding.menuButton.isVisible = showMenu
        toolbarBinding.tabButton.isVisible = showTabCounter

        tabCounterView = if (showTabCounter) {
            buildTabCounter().also {
                it.update(browserState)
            }
        } else {
            null
        }

        homeMenuView = if (showMenu) {
            buildHomeMenu()
        } else {
            null
        }
    }

    /**
     * Dismisses the home menu.
     */
    fun dismissMenu() {
        homeMenuView?.dismissMenu()
    }

    /**
     * Configure the tab strip [ComposeView].
     *
     * @param block Configuration block for the tab strip [ComposeView].
     */
    fun configureTabStripView(block: ComposeView.() -> Unit) = block(toolbarBinding.tabStripView)

    /**
     * Configure the search selector.
     *
     * @param block Configuration block for the search selector.
     */
    fun configureSearchSelector(block: SearchSelector.() -> Unit) = block(toolbarBinding.searchSelectorButton)

    /**
     * Updates the background of the toolbar.
     *
     * @param id The resource ID of the drawable to use as the background.
     */
    fun updateBackground(@DrawableRes id: Int) {
        toolbarBinding.toolbar.setBackgroundResource(id)
    }

    override fun updateTabCounter(browserState: BrowserState) {
        tabCounterView?.update(browserState)
    }

    override fun updateDividerVisibility(isVisible: Boolean) {
        toolbarBinding.toolbarDivider.isVisible = isVisible
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun buildHomeMenu() = HomeMenuView(
        context = context,
        lifecycleOwner = homeFragment.viewLifecycleOwner,
        homeActivity = homeActivity,
        navController = homeFragment.findNavController(),
        fenixBrowserUseCases = context.components.useCases.fenixBrowserUseCases,
        menuButton = WeakReference(toolbarBinding.menuButton),
    ).also { it.build() }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun buildTabCounter() = TabCounterView(
        context = context,
        browsingModeManager = homeActivity.browsingModeManager,
        navController = homeFragment.findNavController(),
        tabCounter = toolbarBinding.tabButton,
        showLongPressMenu = !context.isLargeWindow(),
        settings = context.settings(),
    )

    private fun initLayoutParameters() {
        when (context.settings().toolbarPosition) {
            ToolbarPosition.TOP -> {
                toolbarBinding.toolbarLayout.layoutParams = CoordinatorLayout.LayoutParams(
                    ConstraintLayout.LayoutParams.MATCH_PARENT,
                    ConstraintLayout.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.TOP
                }

                val isTabletAndTabStripEnabled = context.settings().isTabStripEnabled
                ConstraintSet().apply {
                    clone(toolbarBinding.toolbarLayout)
                    clear(toolbarBinding.toolbar.id, ConstraintSet.BOTTOM)
                    clear(toolbarBinding.toolbarDivider.id, ConstraintSet.BOTTOM)

                    if (isTabletAndTabStripEnabled) {
                        connect(
                            toolbarBinding.toolbar.id,
                            ConstraintSet.TOP,
                            toolbarBinding.tabStripView.id,
                            ConstraintSet.BOTTOM,
                        )
                    } else {
                        connect(
                            toolbarBinding.toolbar.id,
                            ConstraintSet.TOP,
                            ConstraintSet.PARENT_ID,
                            ConstraintSet.TOP,
                        )
                    }
                    connect(
                        toolbarBinding.toolbarDivider.id,
                        ConstraintSet.TOP,
                        toolbarBinding.toolbar.id,
                        ConstraintSet.BOTTOM,
                    )
                    connect(
                        toolbarBinding.toolbarDivider.id,
                        ConstraintSet.BOTTOM,
                        ConstraintSet.PARENT_ID,
                        ConstraintSet.BOTTOM,
                    )
                    applyTo(toolbarBinding.toolbarLayout)
                }

                toolbarBinding.toolbar.background = AppCompatResources.getDrawable(
                    context,
                    context.theme.resolveAttribute(R.attr.bottomBarBackgroundTop),
                )

                homeBinding.homeAppBar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                    topMargin =
                        context.pixelSizeFor(R.dimen.home_fragment_top_toolbar_header_margin) +
                        if (isTabletAndTabStripEnabled) {
                            context.pixelSizeFor(R.dimen.tab_strip_height)
                        } else {
                            0
                        }
                }
            }

            ToolbarPosition.BOTTOM -> {}
        }
    }

    override fun updateAddressBarVisibility(isVisible: Boolean) {
        if (isVisible) {
            toolbarBinding.toolbarWrapper.visibility = View.VISIBLE
        } else {
            toolbarBinding.toolbarWrapper.visibility = View.INVISIBLE
        }
    }

    companion object {
        const val TOOLBAR_WRAPPER_INCREASE_HEIGHT_DPS = 5
    }
}
