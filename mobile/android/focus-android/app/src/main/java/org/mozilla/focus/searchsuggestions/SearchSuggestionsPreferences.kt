/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.searchsuggestions

import android.content.Context
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import org.mozilla.focus.R
import org.mozilla.focus.ext.settings

class SearchSuggestionsPreferences(private val context: Context) {
    private val settings = context.settings
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)

    fun searchSuggestionsEnabled(): Boolean = settings.shouldShowSearchSuggestions()
    fun hasUserToggledSearchSuggestions(): Boolean = settings.userHasToggledSearchSuggestions()
    fun userHasDismissedNoSuggestionsMessage(): Boolean = settings.userHasDismissedNoSuggestionsMessage()

    fun enableSearchSuggestions() {
        preferences.edit {
            putBoolean(TOGGLED_SUGGESTIONS_PREF, true)
                .putBoolean(
                    context.resources.getString(R.string.pref_key_show_search_suggestions),
                    true,
                )
        }
    }

    fun disableSearchSuggestions() {
        preferences.edit {
            putBoolean(TOGGLED_SUGGESTIONS_PREF, true)
                .putBoolean(
                    context.resources.getString(R.string.pref_key_show_search_suggestions),
                    false,
                )
        }
    }

    fun dismissNoSuggestionsMessage() {
        preferences.edit {
            putBoolean(DISMISSED_NO_SUGGESTIONS_PREF, true)
        }
    }

    companion object {
        const val TOGGLED_SUGGESTIONS_PREF = "user_has_toggled_search_suggestions"
        const val DISMISSED_NO_SUGGESTIONS_PREF = "user_dismissed_no_search_suggestions"
    }
}
