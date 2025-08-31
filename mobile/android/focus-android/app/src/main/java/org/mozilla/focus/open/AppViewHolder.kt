/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.open

import android.view.View
import android.widget.ImageView
import android.widget.TextView
import androidx.annotation.LayoutRes
import androidx.recyclerview.widget.RecyclerView
import org.mozilla.focus.R
import org.mozilla.focus.open.AppAdapter.OnAppSelectedListener

/**
 * View holder for an app item in the [OpenWithFragment] list.
 */
class AppViewHolder internal constructor(itemView: View) : RecyclerView.ViewHolder(itemView) {
    private val titleView: TextView = itemView.findViewById(R.id.title)
    private val iconView: ImageView = itemView.findViewById(R.id.icon)

    /**
     * Binds the [AppViewHolder] item.
     */
    fun bind(app: AppAdapter.App, listener: OnAppSelectedListener?) {
        titleView.text = app.label
        iconView.setImageDrawable(app.loadIcon())
        itemView.setOnClickListener(createListenerWrapper(app, listener))
    }

    private fun createListenerWrapper(
        app: AppAdapter.App,
        listener: OnAppSelectedListener?,
    ): View.OnClickListener {
        return View.OnClickListener { listener?.onAppSelected(app) }
    }

    companion object {
        @LayoutRes
        val LAYOUT_ID = R.layout.item_app
    }
}
