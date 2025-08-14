/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

/**
 * Menu dialog test tags are used to identify the items in automated tests.
 */
internal object MenuDialogTestTag {
    private const val MAIN_MENU = "mainMenu"

    const val EXTENSIONS = "$MAIN_MENU.extensions"
    const val SHARE = "$MAIN_MENU.share"
    const val MORE_OPTION_CHEVRON = "$MAIN_MENU.more.chevron"

    const val EXTENSIONS_OPTION_CHEVRON = "$MAIN_MENU.extensions.chevron"
    const val DESKTOP_SITE_ON = "$MAIN_MENU.desktop.site.on"
    const val DESKTOP_SITE_OFF = "$MAIN_MENU.desktop.site.off"

    const val RECOMMENDED_ADDON_ITEM = "recommended.addon.item"
    const val RECOMMENDED_ADDON_ITEM_TITLE = "$RECOMMENDED_ADDON_ITEM.title"
    const val WEB_EXTENSION_ITEM = "webExtension.item"
}
