/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.address.store

import mozilla.components.lib.state.Action

/**
 * Actions used within a [AddressStore].
 */
sealed interface AddressAction : Action

/**
 * Actions dispatched when updating the form.
 */
sealed class FormChange : AddressAction {
    /**
     * Name field has been changed.
     */
    data class Name(val updatedText: String) : FormChange()

    /**
     * Street Address field has changed.
     */
    data class StreetAddress(val updatedText: String) : FormChange()

    /**
     * City field has changed.
     */
    data class City(val updatedText: String) : FormChange()

    /**
     * Postal code field has changed.
     */
    data class PostalCode(val updatedText: String) : FormChange()

    /**
     * Sub region (i.e. state or province) field has changed.
     */
    data class SubRegion(val subRegion: String) : FormChange()

    /**
     * Country field has changed.
     */
    data class Country(val countryCode: String) : FormChange()

    /**
     * Telephone field has changed.
     */
    data class Phone(val updatedText: String) : FormChange()

    /**
     * Email field has changed.
     */
    data class Email(val updatedText: String) : FormChange()
}

/**
 * Actions dispatched when interacting with the deletion dialog.
 */
sealed class DeleteDialogAction : AddressAction {
    /**
     * Delete button was tapped.
     */
    data object DeleteTapped : DeleteDialogAction()

    /**
     * Cancel button was tapped.
     */
    data object CancelTapped : DeleteDialogAction()
}

/**
 * The Environment has been rehydrated from a configuration change.
 */
data class EnvironmentRehydrated(val environment: AddressEnvironment) : AddressAction

/**
 * The Address View appeared.
 */
data object ViewAppeared : AddressAction

/**
 * Back button was tapped.
 */
data object BackTapped : AddressAction

/**
 * Cancel button was tapped.
 */
data object CancelTapped : AddressAction

/**
 * Save button was tapped.
 */
data object SaveTapped : AddressAction

/**
 * Delete button was tapped.
 */
data object DeleteTapped : AddressAction
