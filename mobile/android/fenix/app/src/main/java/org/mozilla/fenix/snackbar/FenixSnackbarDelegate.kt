/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.snackbar

import android.view.View
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.compose.ui.text.style.TextOverflow
import com.google.android.material.snackbar.Snackbar.LENGTH_LONG
import mozilla.components.ui.widgets.SnackbarDelegate
import org.mozilla.fenix.compose.core.Action
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.compose.snackbar.toSnackbarDuration

typealias SnackbarFactory = (
    parentView: View,
    state: SnackbarState,
) -> Snackbar

/**
 * An implementation of [SnackbarDelegate] used to display the snackbar.
 *
 * @param view The view to find a parent from.
 * @param snackbarFactory A lambda function to create a [Snackbar].
 */
class FenixSnackbarDelegate(
    private val view: View,
    private val snackbarFactory: SnackbarFactory = { parent, state ->
        Snackbar.make(
            parent,
            state,
        )
    },
) : SnackbarDelegate {

    // Holds onto a reference of a [Snackbar] that is displayed.
    private var snackbar: Snackbar? = null

    /**
     * Displays a snackbar.
     *
     * @param text The text to show. Can be formatted text.
     * @param duration How long to display the message.
     * @param isError Whether the snackbar should be styled as an error.
     * @param action Optional String resource to display for the action.
     * The [listener] must also be provided to show an action button.
     * @param listener Optional callback to be invoked when the action is clicked.
     * An [action] must also be provided to show an action button.
     */
    fun show(
        @StringRes text: Int,
        duration: Int = LENGTH_LONG,
        isError: Boolean = false,
        @StringRes action: Int = 0,
        listener: ((v: View) -> Unit)? = null,
    ) {
        show(
            snackBarParentView = view,
            text = text,
            duration = duration,
            isError = isError,
            action = action,
            listener = listener,
        )
    }

    /**
     * Displays a snackbar.
     *
     * @param text The text to show.
     * @param subText The optional sub-text to show.
     * @param subTextOverflow Defines how visual overflow of the [subText] should be handled.
     * @param duration How long to display the message.
     * @param isError Whether the snackbar should be styled as an error.
     * @param action Optional String to display for the action.
     * The [listener] must also be provided to show an action button.
     * @param listener Optional callback to be invoked when the action is clicked.
     * An [action] must also be provided to show an action button.
     */
    fun show(
        text: String,
        subText: String? = null,
        subTextOverflow: TextOverflow? = null,
        duration: Int = LENGTH_LONG,
        isError: Boolean = false,
        action: String? = null,
        listener: ((v: View) -> Unit)? = null,
    ) = show(
        snackBarParentView = view,
        text = text,
        subText = subText,
        subTextOverflow = subTextOverflow,
        duration = duration,
        isError = isError,
        action = action,
        listener = listener,
    )

    override fun show(
        snackBarParentView: View,
        @StringRes text: Int,
        subText: String?,
        subTextOverflow: TextOverflow?,
        duration: Int,
        isError: Boolean,
        @StringRes action: Int,
        listener: ((v: View) -> Unit)?,
    ) = show(
        snackBarParentView = snackBarParentView,
        text = snackBarParentView.context.getString(text),
        subText = subText,
        subTextOverflow = subTextOverflow,
        duration = duration,
        isError = isError,
        action = if (action == 0) null else snackBarParentView.context.getString(action),
        listener = listener,
    )

    override fun show(
        snackBarParentView: View,
        text: String,
        subText: String?,
        subTextOverflow: TextOverflow?,
        duration: Int,
        isError: Boolean,
        action: String?,
        listener: ((v: View) -> Unit)?,
    ) {
        val snackbar = snackbarFactory(
            snackBarParentView,
            makeSnackbarState(
                snackBarParentView = snackBarParentView,
                text = text,
                subText = subText,
                subTextOverflow = subTextOverflow,
                duration = duration,
                isError = isError,
                actionText = action,
                listener = listener,
            ),
        )

        this.snackbar?.dismiss()
        this.snackbar = snackbar

        snackbar.show()
    }

    /**
     * Dismiss the existing snackbar.
     */
    fun dismiss() {
        snackbar?.dismiss()
    }

    @VisibleForTesting
    internal fun makeSnackbarState(
        snackBarParentView: View,
        text: String,
        subText: String? = null,
        subTextOverflow: TextOverflow? = null,
        duration: Int,
        isError: Boolean,
        actionText: String?,
        listener: ((v: View) -> Unit)?,
    ): SnackbarState {
        val action: Action? = if (actionText != null && listener != null) {
            Action(
                label = actionText,
                onClick = {
                    listener.invoke(snackBarParentView)
                },
            )
        } else {
            null
        }

        val subMessage = subText?.let {
            SnackbarState.SubMessage(
                text = it,
                textOverflow = subTextOverflow ?: TextOverflow.Ellipsis,
            )
        }

        return SnackbarState(
            message = text,
            subMessage = subMessage,
            duration = duration.toSnackbarDuration(),
            type = if (isError) {
                SnackbarState.Type.Warning
            } else {
                SnackbarState.Type.Default
            },
            action = action,
        )
    }
}
