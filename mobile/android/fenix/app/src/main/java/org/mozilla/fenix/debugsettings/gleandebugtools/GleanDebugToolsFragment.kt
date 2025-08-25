/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.gleandebugtools

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.navigation.fragment.findNavController
import mozilla.telemetry.glean.Glean
import org.mozilla.fenix.R
import org.mozilla.fenix.components.lazyStore
import org.mozilla.fenix.debugsettings.gleandebugtools.ui.GleanDebugToolsScreen
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * [Fragment] for displaying the Glean Debug Tools in the about:glean page.
 */
class GleanDebugToolsFragment : Fragment() {

    private val store by lazyStore {
        GleanDebugToolsStore(
            initialState = GleanDebugToolsState(
                logPingsToConsoleEnabled = Glean.getLogPings(),
                debugViewTag = Glean.getDebugViewTag() ?: "",
            ),
            middlewares = listOf(
                GleanDebugToolsMiddleware(
                    gleanDebugToolsStorage = DefaultGleanDebugToolsStorage(),
                    clipboardHandler = requireComponents.clipboardHandler,
                    openDebugView = { debugViewLink ->
                        val intent = Intent(Intent.ACTION_VIEW)
                        intent.data = debugViewLink.toUri()
                        requireContext().startActivity(intent)
                    },
                    showToast = { pingType ->
                        val toast = Toast.makeText(
                            requireContext(),
                            requireContext().getString(
                                R.string.glean_debug_tools_send_ping_toast_message,
                                pingType,
                            ),
                            Toast.LENGTH_LONG,
                        )
                        toast.show()
                    },
                ),
            ),
        )
    }

    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? = content {
        FirefoxTheme {
            Scaffold(
                topBar = {
                    TopAppBar(
                        title = {
                            Text(
                                text = stringResource(R.string.glean_debug_tools_title),
                                color = FirefoxTheme.colors.textPrimary,
                                style = FirefoxTheme.typography.headline6,
                            )
                        },
                        navigationIcon = {
                            val directions = GleanDebugToolsFragmentDirections.actionGlobalBrowser()
                            IconButton(onClick = { findNavController().navigate(directions) }) {
                                Icon(
                                    painter = painterResource(R.drawable.mozac_ic_back_24),
                                    contentDescription = stringResource(
                                        R.string.bookmark_navigate_back_button_content_description,
                                    ),
                                    tint = FirefoxTheme.colors.iconPrimary,
                                )
                            }
                        },
                        windowInsets = WindowInsets(
                            top = 0.dp,
                            bottom = 0.dp,
                        ),
                        colors = TopAppBarDefaults.topAppBarColors(containerColor = FirefoxTheme.colors.layer1),
                    )
                },
                containerColor = FirefoxTheme.colors.layer1,
            ) { paddingValues ->
                GleanDebugToolsScreen(
                    gleanDebugToolsStore = store,
                    modifier = Modifier.padding(paddingValues),
                )
            }
        }
    }
}
