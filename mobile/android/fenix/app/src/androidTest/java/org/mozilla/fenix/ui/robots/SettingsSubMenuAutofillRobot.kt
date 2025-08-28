/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.isNotDisplayed
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onChildAt
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.test.espresso.Espresso.closeSoftKeyboard
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withChild
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.UiSelector
import org.hamcrest.CoreMatchers.allOf
import org.hamcrest.CoreMatchers.endsWith
import org.junit.Assert.assertEquals
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.settings.address.ui.edit.EditAddressTestTag

class SettingsSubMenuAutofillRobot(private val composeTestRule: ComposeTestRule) {

    fun verifyAutofillToolbarTitle() {
        assertUIObjectExists(autofillToolbarTitle())
    }
    fun verifyManageAddressesToolbarTitle() {
        Log.i(TAG, "verifyManageAddressesToolbarTitle: Trying to verify that the \"Manage addresses\" toolbar title is displayed")
        onView(
            allOf(
                withId(R.id.navigationToolbar),
                withChild(
                    withText(R.string.preferences_addresses_manage_addresses),
                ),
            ),
        ).check(matches(isDisplayed()))
        Log.i(TAG, "verifyManageAddressesToolbarTitle: Verified that the \"Manage addresses\" toolbar title is displayed")
    }

    fun verifyAddressAutofillSection(isAddressAutofillEnabled: Boolean, userHasSavedAddress: Boolean) {
        assertUIObjectExists(
            autofillToolbarTitle(),
            addressesSectionTitle(),
            saveAndAutofillAddressesOption(),
            saveAndAutofillAddressesSummary(),
        )

        if (userHasSavedAddress) {
            assertUIObjectExists(manageAddressesButton())
        } else {
            assertUIObjectExists(addAddressButton())
        }

        verifyAddressesAutofillToggle(isAddressAutofillEnabled)
    }

    fun verifyCreditCardsAutofillSection(isAddressAutofillEnabled: Boolean, userHasSavedCreditCard: Boolean) {
        assertUIObjectExists(
            autofillToolbarTitle(),
            creditCardsSectionTitle(),
            saveAndAutofillCreditCardsOption(),
            saveAndAutofillCreditCardsSummary(),
            syncCreditCardsAcrossDevicesButton(),

        )

        if (userHasSavedCreditCard) {
            assertUIObjectExists(manageSavedCreditCardsButton())
        } else {
            assertUIObjectExists(addCreditCardButton())
        }

        verifySaveAndAutofillCreditCardsToggle(isAddressAutofillEnabled)
    }

    fun verifyManageAddressesSection(vararg savedAddressDetails: String) {
        assertUIObjectExists(
            navigateBackButton(),
            manageAddressesToolbarTitle(),
            addAddressButton(),
        )
        for (savedAddressDetail in savedAddressDetails) {
            Log.i(TAG, "verifyManageAddressesSection: Trying to verify that: $savedAddressDetail detail is displayed")
            composeTestRule.onNodeWithText(savedAddressDetail).assertIsDisplayed()
            Log.i(TAG, "verifyManageAddressesSection: Verified that: $savedAddressDetail detail is displayed")
        }
    }

    fun verifySavedCreditCardsSection(creditCardLastDigits: String, creditCardExpiryDate: String) {
        assertUIObjectExists(
            navigateBackButton(),
            savedCreditCardsToolbarTitle(),
            addCreditCardButton(),
            itemContainingText(creditCardLastDigits),
            itemContainingText(creditCardExpiryDate),
        )
    }

    fun verifyAddressesAutofillToggle(enabled: Boolean) {
        Log.i(TAG, "verifyAddressesAutofillToggle: Trying to verify that the \"Save and autofill addresses\" toggle is checked: $enabled")
        onView(withText(R.string.preferences_addresses_save_and_autofill_addresses_2))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (enabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        ),
                    ),
                ),
            )
        Log.i(TAG, "verifyAddressesAutofillToggle: Verified that the \"Save and autofill addresses\" toggle is checked: $enabled")
    }

    fun verifySaveAndAutofillCreditCardsToggle(enabled: Boolean) {
        Log.i(TAG, "verifySaveAndAutofillCreditCardsToggle: Trying to verify that the \"Save and autofill cards\" toggle is checked: $enabled")
        onView(withText(R.string.preferences_credit_cards_save_and_autofill_cards_2))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (enabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        ),
                    ),
                ),
            )
        Log.i(TAG, "verifySaveAndAutofillCreditCardsToggle: Verified that the \"Save and autofill cards\" toggle is checked: $enabled")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyAddAddressView() {
        Log.i(TAG, "verifyAddAddressView: Trying to perform \"Close soft keyboard\" action")
        // Closing the keyboard to ensure full visibility of the "Add address" view
        closeSoftKeyboard()
        Log.i(TAG, "verifyAddAddressView: Performed \"Close soft keyboard\" action")
        Log.i(TAG, "verifyAddAddressView: Trying to verify the \"Add address\" view items")
        listOf(
            composeTestRule.navigateBackButton(),
            composeTestRule.addAddressToolbarTitle(),
            composeTestRule.toolbarCheckmarkButton(),
            composeTestRule.nameTextInput(),
            composeTestRule.streetAddressTextInput(),
            composeTestRule.cityTextInput(),
            composeTestRule.subRegionDropDown(),
            composeTestRule.zipCodeTextInput(),
            composeTestRule.countryDropDown(),
            composeTestRule.phoneTextInput(),
            composeTestRule.emailTextInput(),
        ).forEach { it.assertIsDisplayed() }

        if (composeTestRule.saveButton().isNotDisplayed()) {
            composeTestRule.saveButton().performScrollTo()
        }

        listOf(
            composeTestRule.saveButton(),
            composeTestRule.cancelButton(),
        ).forEach { it.assertIsDisplayed() }
        Log.i(TAG, "verifyAddAddressView: Verified the \"Add address\" view items")
    }

    fun verifyCountryOption(country: String) {
        Log.i(TAG, "verifyCountryOption: Trying to perform \"Close soft keyboard\" action")
        // Closing the keyboard to ensure full visibility of the "Add address" view
        closeSoftKeyboard()
        Log.i(TAG, "verifyCountryOption: Performed \"Close soft keyboard\" action")
        assertUIObjectExists(itemContainingText(country))
    }

    fun verifyStateOption(state: String) {
        Log.i(TAG, "verifyStateOption: Trying to verify that state: $state is displayed")
        composeTestRule.subRegionOption(state).assertIsDisplayed()
        Log.i(TAG, "verifyStateOption: Verified that state: $state is displayed")
    }

    fun verifyCountryOptions(vararg countries: String) {
        Log.i(TAG, "verifyCountryOptions: Trying to click the \"Country or region\" dropdown")
        composeTestRule.countryDropDown().performClick()
        Log.i(TAG, "verifyCountryOptions: Clicked the \"Country or region\" dropdown")
        for (country in countries) {
            assertUIObjectExists(itemContainingText(country))
        }
    }

    fun selectCountry(country: String) {
        Log.i(TAG, "selectCountry: Trying to click the \"Country or region\" dropdown")
        composeTestRule.countryDropDown().performClick()
        Log.i(TAG, "selectCountry: Clicked the \"Country or region\" dropdown")
        Log.i(TAG, "selectCountry: Trying to select $country dropdown option")
        composeTestRule.countryOption(country).performClick()
        Log.i(TAG, "selectCountry: Selected $country dropdown option")
    }

    fun verifyEditAddressView() {
        Log.i(TAG, "verifyEditAddressView: Trying to click device back button to dismiss keyboard using device back button")
        mDevice.pressBack()
        Log.i(TAG, "verifyEditAddressView: Clicked device back button to dismiss keyboard using device back button")
        Log.i(TAG, "verifyEditAddressView: Trying to verify that the \"Edit address\" items are displayed")
        listOf(
            composeTestRule.navigateBackButton(),
            composeTestRule.editAddressToolbarTitle(),
            composeTestRule.toolbarCheckmarkButton(),
            composeTestRule.toolbarDeleteAddressButton(),
            composeTestRule.nameTextInput().assertIsDisplayed(),
            composeTestRule.streetAddressTextInput(),
            composeTestRule.cityTextInput(),
            composeTestRule.subRegionDropDown(),
        ).forEach { it.assertIsDisplayed() }

        if (composeTestRule.countryDropDown().isNotDisplayed()) {
            composeTestRule.countryDropDown().performScrollTo()
        }

        listOf(
            composeTestRule.zipCodeTextInput(),
            composeTestRule.countryDropDown(),
            composeTestRule.phoneTextInput(),
            composeTestRule.emailTextInput(),
        ).forEach { it.assertIsDisplayed() }

        if (composeTestRule.saveButton().isNotDisplayed()) {
            composeTestRule.saveButton().performScrollTo()
        }

        listOf(
            composeTestRule.saveButton(),
            composeTestRule.cancelButton(),
            composeTestRule.deleteAddressButton(),
        ).forEach { it.assertIsDisplayed() }
        Log.i(TAG, "verifyEditAddressView: Verified that the \"Edit address\" items are displayed")
    }

    fun clickSaveAndAutofillAddressesOption() {
        Log.i(TAG, "clickSaveAndAutofillAddressesOption: Trying to click the \"Save and fill addresses\" button")
        saveAndAutofillAddressesOption().click()
        Log.i(TAG, "clickSaveAndAutofillAddressesOption: Clicked the \"Save and fill addresses\" button")
    }
    fun clickAddAddressButton() {
        Log.i(TAG, "clickAddAddressButton: Trying to click the \"Add address\" button")
        addAddressButton().click()
        Log.i(TAG, "clickAddAddressButton: Clicked the \"Add address\" button")
    }
    fun clickManageAddressesButton() {
        Log.i(TAG, "clickManageAddressesButton: Trying to click the \"Manage addresses\" button")
        manageAddressesButton().click()
        Log.i(TAG, "clickManageAddressesButton: Clicked the \"Manage addresses\" button")
    }
    fun clickSavedAddress(composeTestRule: ComposeTestRule, name: String) {
        Log.i(TAG, "clickSavedAddress: Trying to click the $name saved address and and wait for $waitingTime ms for a new window")
        composeTestRule.onNodeWithText(name, useUnmergedTree = true).performClick()
        Log.i(TAG, "clickSavedAddress: Clicked the $name saved address and and waited for $waitingTime ms for a new window")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickDeleteAddressButton() {
        Log.i(TAG, "clickDeleteAddressButton: Waiting for $waitingTime ms for the delete address toolbar button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(EditAddressTestTag.TOPBAR_DELETE_BUTTON), waitingTime)
        Log.i(TAG, "clickDeleteAddressButton: Waited for $waitingTime ms for the delete address toolbar button to exist")
        Log.i(TAG, "clickDeleteAddressButton: Trying to click the delete address toolbar button")
        composeTestRule.toolbarDeleteAddressButton().performClick()
        Log.i(TAG, "clickDeleteAddressButton: Clicked the delete address toolbar button")
    }
    fun clickCancelDeleteAddressButton() {
        Log.i(TAG, "clickCancelDeleteAddressButton: Trying to click the \"CANCEL\" button from the delete address dialog")
        composeTestRule.cancelDeleteAddressButton().performClick()
        Log.i(TAG, "clickCancelDeleteAddressButton: Clicked the \"CANCEL\" button from the delete address dialog")
    }

    fun clickConfirmDeleteAddressButton() {
        Log.i(TAG, "clickConfirmDeleteAddressButton: Trying to click the \"DELETE\" button from the delete address dialog")
        composeTestRule.confirmDeleteAddressButton().performClick()
        Log.i(TAG, "clickConfirmDeleteAddressButton: Clicked \"DELETE\" button from the delete address dialog")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickSubRegionOption(subRegion: String) {
        composeTestRule.subRegionOption(subRegion).performScrollTo()
        Log.i(TAG, "clickSubRegionOption: Waiting for $waitingTime ms for the \"State\" $subRegion dropdown option to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(subRegion), waitingTime)
        Log.i(TAG, "clickSubRegionOption: Waited for $waitingTime ms for the \"State\" $subRegion dropdown option to exist")
        Log.i(TAG, "clickSubRegionOption: Trying to click the \"State\" $subRegion dropdown option")
        composeTestRule.subRegionOption(subRegion).performClick()
        Log.i(TAG, "clickSubRegionOption: Clicked the \"State\" $subRegion dropdown option")
    }

    fun clickCountryDropdown() {
        Log.i(TAG, "clickCountryDropdown: Trying to click \"Country or region\" dropdown")
        composeTestRule.countryDropDown().performClick()
        Log.i(TAG, "clickCountryDropdown: Clicked \"Country or region\" dropdown")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickCountryOption(country: String) {
        Log.i(TAG, "clickCountryOption: Waiting for $waitingTime ms for the \"Country or region\" $country dropdown option to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasText(country), waitingTime)
        Log.i(TAG, "clickCountryOption: Waited for $waitingTime ms for the \"Country or region\" $country dropdown option to exist")
        Log.i(TAG, "clickCountryOption: Trying to click \"Country or region\" $country dropdown option")
        composeTestRule.countryOption(country).performClick()
        Log.i(TAG, "clickCountryOption: Clicked \"Country or region\" $country dropdown option")
    }
    fun verifyAddAddressButton() = assertUIObjectExists(addAddressButton())

    @OptIn(ExperimentalTestApi::class)
    fun fillAndSaveAddress(
        navigateToAutofillSettings: Boolean,
        isAddressAutofillEnabled: Boolean = true,
        userHasSavedAddress: Boolean = false,
        name: String,
        streetAddress: String,
        city: String,
        state: String,
        zipCode: String,
        country: String,
        phoneNumber: String,
        emailAddress: String,
    ) {
        if (navigateToAutofillSettings) {
            homeScreen {
            }.openThreeDotMenu {
            }.openSettings {
            }.openAutofillSubMenu(composeTestRule) {
                verifyAddressAutofillSection(isAddressAutofillEnabled, userHasSavedAddress)
                clickAddAddressButton()
            }
        }
        Log.i(TAG, "fillAndSaveAddress: Waiting for $waitingTime ms for \"Name\" text field to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(EditAddressTestTag.NAME_FIELD), waitingTime)
        Log.i(TAG, "fillAndSaveAddress: Waited for $waitingTime ms for \"Name\" text field to exist")
        Log.i(TAG, "fillAndSaveAddress: Trying to click device back button to dismiss keyboard using device back button")
        mDevice.pressBack()
        Log.i(TAG, "fillAndSaveAddress: Clicked device back button to dismiss keyboard using device back button")
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"Name\" to $name")
        composeTestRule.nameTextInput().performTextInput(name)
        Log.i(TAG, "fillAndSaveAddress: \"Name\" was set to $name")
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"Street Address\" to $streetAddress")
        composeTestRule.streetAddressTextInput().performTextInput(streetAddress)
        Log.i(TAG, "fillAndSaveAddress: \"Street Address\" was set to $streetAddress")
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"City\" to $city")
        composeTestRule.cityTextInput().performTextInput(city)
        Log.i(TAG, "fillAndSaveAddress: \"City\" was set to $city")
        Log.i(TAG, "fillAndSaveAddress: Trying to click \"State\" dropdown button")
        composeTestRule.subRegionDropDown().performClick()
        Log.i(TAG, "fillAndSaveAddress: Clicked \"State\" dropdown button")
        Log.i(TAG, "fillAndSaveAddress: Trying to click the $state dropdown option")
        clickSubRegionOption(state)
        Log.i(TAG, "fillAndSaveAddress: Clicked $state dropdown option")
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"Zip\" to $zipCode")
        composeTestRule.zipCodeTextInput().performTextInput(zipCode)
        Log.i(TAG, "fillAndSaveAddress: \"Zip\" was set to $zipCode")
        Log.i(TAG, "fillAndSaveAddress: Trying to click \"Country or region\" dropdown button")
        composeTestRule.countryDropDown().performClick()
        Log.i(TAG, "fillAndSaveAddress: Clicked \"Country or region\" dropdown button")
        Log.i(TAG, "fillAndSaveAddress: Trying to click $country dropdown option")
        clickCountryOption(country)
        Log.i(TAG, "fillAndSaveAddress: Clicked $country dropdown option")
        if (composeTestRule.saveButton().isNotDisplayed()) {
            composeTestRule.saveButton().performScrollTo()
        }
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"Phone\" to $phoneNumber")
        composeTestRule.phoneTextInput().performTextInput(phoneNumber)
        Log.i(TAG, "fillAndSaveAddress: \"Phone\" was set to $phoneNumber")
        Log.i(TAG, "fillAndSaveAddress: Trying to set \"Email\" to $emailAddress")
        composeTestRule.emailTextInput().performTextInput(emailAddress)
        Log.i(TAG, "fillAndSaveAddress: \"Email\" was set to $emailAddress")
        Log.i(TAG, "fillAndSaveAddress: Trying to click the \"Save\" button")
        composeTestRule.saveButton().performClick()
        Log.i(TAG, "fillAndSaveAddress: Clicked the \"Save\" button")
        Log.i(TAG, "fillAndSaveAddress: Waiting for $waitingTime ms for for \"Manage addresses\" button to exist")
        manageAddressesButton().waitForExists(waitingTime)
        Log.i(TAG, "fillAndSaveAddress: Waited for $waitingTime ms for for \"Manage addresses\" button to exist")
    }

    fun clickAddCreditCardButton() {
        Log.i(TAG, "clickAddCreditCardButton: Trying to click the \"Add credit card\" button")
        addCreditCardButton().click()
        Log.i(TAG, "clickAddCreditCardButton: Clicked the \"Add credit card\" button")
    }
    fun clickManageSavedCreditCardsButton() {
        Log.i(TAG, "clickManageSavedCreditCardsButton: Trying to click the \"Manage saved cards\" button")
        manageSavedCreditCardsButton().click()
        Log.i(TAG, "clickManageSavedCreditCardsButton: Clicked the \"Manage saved cards\" button")
    }
    fun clickSecuredCreditCardsLaterButton() {
        Log.i(TAG, "clickSecuredCreditCardsLaterButton: Trying to click the \"Later\" button")
        securedCreditCardsLaterButton().click()
        Log.i(TAG, "clickSecuredCreditCardsLaterButton: Clicked the \"Later\" button")
    }
    fun clickSavedCreditCard() {
        Log.i(TAG, "clickSavedCreditCard: Trying to click the saved credit card and and wait for $waitingTime ms for a new window")
        savedCreditCardNumber().clickAndWaitForNewWindow(waitingTime)
        Log.i(TAG, "clickSavedCreditCard: Clicked the saved credit card and and waited for $waitingTime ms for a new window")
    }
    fun clickDeleteCreditCardToolbarButton() {
        Log.i(TAG, "clickDeleteCreditCardToolbarButton: Waiting for $waitingTime ms for the delete credit card toolbar button to exist")
        deleteCreditCardToolbarButton().waitForExists(waitingTime)
        Log.i(TAG, "clickDeleteCreditCardToolbarButton: Waited for $waitingTime ms for the delete credit card toolbar button to exist")
        Log.i(TAG, "clickDeleteCreditCardToolbarButton: Trying to click the delete credit card toolbar button")
        deleteCreditCardToolbarButton().click()
        Log.i(TAG, "clickDeleteCreditCardToolbarButton: Clicked the delete credit card toolbar button")
    }
    fun clickDeleteCreditCardMenuButton() {
        Log.i(TAG, "clickDeleteCreditCardMenuButton: Waiting for $waitingTime ms for the delete credit card menu button to exist")
        deleteCreditCardMenuButton().waitForExists(waitingTime)
        Log.i(TAG, "clickDeleteCreditCardMenuButton: Waited for $waitingTime ms for the delete credit card menu button to exist")
        Log.i(TAG, "clickDeleteCreditCardMenuButton: Trying to click the delete credit card menu button")
        deleteCreditCardMenuButton().click()
        Log.i(TAG, "clickDeleteCreditCardMenuButton: Clicked the delete credit card menu button")
    }
    fun clickSaveAndAutofillCreditCardsOption() {
        Log.i(TAG, "clickSaveAndAutofillCreditCardsOption: Trying to click the \"Save and autofill cards\" option")
        saveAndAutofillCreditCardsOption().click()
        Log.i(TAG, "clickSaveAndAutofillCreditCardsOption: Clicked the \"Save and autofill cards\" option")
    }

    fun clickConfirmDeleteCreditCardButton() {
        Log.i(TAG, "clickConfirmDeleteCreditCardButton: Trying to click the \"Delete\" credit card dialog button")
        confirmDeleteCreditCardButton().click()
        Log.i(TAG, "clickConfirmDeleteCreditCardButton: Clicked the \"Delete\" credit card dialog button")
    }

    fun clickCancelDeleteCreditCardButton() {
        Log.i(TAG, "clickCancelDeleteCreditCardButton: Trying to click the \"Cancel\" credit card dialog button")
        cancelDeleteCreditCardButton().click()
        Log.i(TAG, "clickCancelDeleteCreditCardButton: Clicked the \"Cancel\" credit card dialog button")
    }

    fun clickExpiryMonthOption(expiryMonth: String) {
        Log.i(TAG, "clickExpiryMonthOption: Waiting for $waitingTime ms for the $expiryMonth expiry month option to exist")
        expiryMonthOption(expiryMonth).waitForExists(waitingTime)
        Log.i(TAG, "clickExpiryMonthOption: Waited for $waitingTime ms for the $expiryMonth expiry month option to exist")
        Log.i(TAG, "clickExpiryMonthOption: Trying to click $expiryMonth expiry month option")
        expiryMonthOption(expiryMonth).click()
        Log.i(TAG, "clickExpiryMonthOption: Clicked $expiryMonth expiry month option")
    }

    fun clickExpiryYearOption(expiryYear: String) {
        Log.i(TAG, "clickExpiryYearOption: Waiting for $waitingTime ms for the $expiryYear expiry year option to exist")
        expiryYearOption(expiryYear).waitForExists(waitingTime)
        Log.i(TAG, "clickExpiryYearOption: Waited for $waitingTime ms for the $expiryYear expiry year option to exist")
        Log.i(TAG, "clickExpiryYearOption: Trying to click $expiryYear expiry year option")
        expiryYearOption(expiryYear).click()
        Log.i(TAG, "clickExpiryYearOption: Clicked $expiryYear expiry year option")
    }

    fun verifyAddCreditCardsButton() = assertUIObjectExists(addCreditCardButton())

    fun fillAndSaveCreditCard(cardNumber: String, cardName: String, expiryMonth: String, expiryYear: String) {
        Log.i(TAG, "fillAndSaveCreditCard: Waiting for $waitingTime ms for the credit card number text field to exist")
        creditCardNumberTextInput().waitForExists(waitingTime)
        Log.i(TAG, "fillAndSaveCreditCard: Waited for $waitingTime ms for the credit card number text field to exist")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to set the credit card number to: $cardNumber")
        creditCardNumberTextInput().setText(cardNumber)
        Log.i(TAG, "fillAndSaveCreditCard: The credit card number was set to: $cardNumber")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to set the name on card to: $cardName")
        nameOnCreditCardTextInput().setText(cardName)
        Log.i(TAG, "fillAndSaveCreditCard: The credit card name was set to: $cardName")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to click the expiry month dropdown")
        expiryMonthDropDown().click()
        Log.i(TAG, "fillAndSaveCreditCard: Clicked the expiry month dropdown")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to click $expiryMonth expiry month option")
        clickExpiryMonthOption(expiryMonth)
        Log.i(TAG, "fillAndSaveCreditCard: Clicked $expiryMonth expiry month option")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to click the expiry year dropdown")
        expiryYearDropDown().click()
        Log.i(TAG, "fillAndSaveCreditCard: Clicked the expiry year dropdown")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to click $expiryYear expiry year option")
        clickExpiryYearOption(expiryYear)
        Log.i(TAG, "fillAndSaveCreditCard: Clicked $expiryYear expiry year option")
        Log.i(TAG, "fillAndSaveCreditCard: Trying to click the \"Save\" button")
        saveButton().click()
        Log.i(TAG, "fillAndSaveCreditCard: Clicked the \"Save\" button")
        Log.i(TAG, "fillAndSaveCreditCard: Waiting for $waitingTime ms for the \"Manage saved cards\" button to exist")
        manageSavedCreditCardsButton().waitForExists(waitingTime)
        Log.i(TAG, "fillAndSaveCreditCard: Waited for $waitingTime ms for the \"Manage saved cards\" button to exist")
    }

    fun clearCreditCardNumber() =
        creditCardNumberTextInput().also {
            Log.i(TAG, "clearCreditCardNumber: Waiting for $waitingTime ms for the credit card number text field to exist")
            it.waitForExists(waitingTime)
            Log.i(TAG, "clearCreditCardNumber: Waited for $waitingTime ms for the credit card number text field to exist")
            Log.i(TAG, "clearCreditCardNumber: Trying to clear the credit card number text field")
            it.clearTextField()
            Log.i(TAG, "clearCreditCardNumber: Cleared the credit card number text field")
        }

    fun clearNameOnCreditCard() =
        nameOnCreditCardTextInput().also {
            Log.i(TAG, "clearNameOnCreditCard: Waiting for $waitingTime ms for name on card text field to exist")
            it.waitForExists(waitingTime)
            Log.i(TAG, "clearNameOnCreditCard: Waited for $waitingTime ms for name on card text field to exist")
            Log.i(TAG, "clearNameOnCreditCard: Trying to clear the name on card text field")
            it.clearTextField()
            Log.i(TAG, "clearNameOnCreditCard: Cleared the name on card text field")
        }

    fun clickSaveCreditCardToolbarButton() {
        Log.i(TAG, "clickSaveCreditCardToolbarButton: Trying to click the save credit card toolbar button")
        saveCreditCardToolbarButton().click()
        Log.i(TAG, "clickSaveCreditCardToolbarButton: Clicked the save credit card toolbar button")
    }

    fun verifyEditCreditCardView(
        cardNumber: String,
        cardName: String,
        expiryMonth: String,
        expiryYear: String,
    ) {
        assertUIObjectExists(
            editCreditCardToolbarTitle(),
            navigateBackButton(),
            deleteCreditCardToolbarButton(),
            saveCreditCardToolbarButton(),
        )
        Log.i(TAG, "verifyEditCreditCardView: Trying to verify that the card number text field is set to: $cardNumber")
        assertEquals(cardNumber, creditCardNumberTextInput().text)
        Log.i(TAG, "verifyEditCreditCardView: Verified that the card number text field was set to: $cardNumber")
        Log.i(TAG, "verifyEditCreditCardView: Trying to verify that the card name text field is set to: $cardName")
        assertEquals(cardName, nameOnCreditCardTextInput().text)
        Log.i(TAG, "verifyEditCreditCardView: Verified that the card card name text field was set to: $cardName")

        // Can't get the text from the drop-down items, need to verify them individually
        assertUIObjectExists(
            expiryYearDropDown(),
            expiryMonthDropDown(),
        )

        assertUIObjectExists(
            itemContainingText(expiryMonth),
            itemContainingText(expiryYear),
        )

        assertUIObjectExists(
            saveButton(),
            cancelButton(),
        )

        assertUIObjectExists(deleteCreditCardMenuButton())
    }

    fun verifyEditCreditCardToolbarTitle() = assertUIObjectExists(editCreditCardToolbarTitle())

    fun verifyCreditCardNumberErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.credit_cards_number_validation_error_message_2)))

    fun verifyNameOnCreditCardErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.credit_cards_name_on_card_validation_error_message_2)))

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBack: Clicked the device back button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun goBackToAutofillSettings(interact: SettingsSubMenuAutofillRobot.() -> Unit): SettingsSubMenuAutofillRobot.Transition {
            Log.i(TAG, "goBackToAutofillSettings: Trying to click the navigate up toolbar button")
            navigateBackButton().click()
            Log.i(TAG, "goBackToAutofillSettings: Clicked the navigate up toolbar button")

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun goBackToAutofillSettings(composeTestRule: ComposeTestRule, interact: SettingsSubMenuAutofillRobot.() -> Unit): SettingsSubMenuAutofillRobot.Transition {
            Log.i(TAG, "goBackToAutofillSettings: Trying to click the navigate up toolbar button")
            composeTestRule.navigateBackButton().performClick()
            Log.i(TAG, "goBackToAutofillSettings: Clicked the navigate up toolbar button")

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun goBackToSavedCreditCards(interact: SettingsSubMenuAutofillRobot.() -> Unit): SettingsSubMenuAutofillRobot.Transition {
            Log.i(TAG, "goBackToSavedCreditCards: Trying to click the navigate up toolbar button")
            navigateBackButton().click()
            Log.i(TAG, "goBackToSavedCreditCards: Clicked the navigate up toolbar button")

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun goBackToBrowser(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goBackToBrowser: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBackToBrowser: Clicked the device back button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }
    }
}

fun autofillScreen(composeTestRule: ComposeTestRule, interact: SettingsSubMenuAutofillRobot.() -> Unit): SettingsSubMenuAutofillRobot.Transition {
    SettingsSubMenuAutofillRobot(composeTestRule).interact()
    return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
}

private fun autofillToolbarTitle() = itemContainingText(getStringResource(R.string.preferences_autofill))
private fun addressesSectionTitle() = itemContainingText(getStringResource(R.string.preferences_addresses))
private fun manageAddressesToolbarTitle() =
    mDevice.findObject(
        UiSelector()
            .resourceId("$packageName:id/navigationToolbar")
            .childSelector(UiSelector().text(getStringResource(R.string.addresses_manage_addresses))),
    )

private fun saveAndAutofillAddressesOption() = itemContainingText(getStringResource(R.string.preferences_addresses_save_and_autofill_addresses_2))
private fun saveAndAutofillAddressesSummary() = itemContainingText(getStringResource(R.string.preferences_addresses_save_and_autofill_addresses_summary_2))
private fun addAddressButton() = itemContainingText(getStringResource(R.string.preferences_addresses_add_address))
private fun manageAddressesButton() =
    mDevice.findObject(
        UiSelector()
            .resourceId("android:id/title")
            .text(getStringResource(R.string.preferences_addresses_manage_addresses)),
    )

private fun ComposeTestRule.addAddressToolbarTitle() = onNodeWithText(getStringResource(R.string.preferences_addresses_add_address))
private fun ComposeTestRule.editAddressToolbarTitle() = onNodeWithText(getStringResource(R.string.addresses_edit_address))
private fun ComposeTestRule.toolbarCheckmarkButton() = onNodeWithContentDescription(getStringResource(R.string.address_menu_save_address))
private fun navigateBackButton() = itemWithDescription(getStringResource(R.string.action_bar_up_description))
private fun ComposeTestRule.navigateBackButton() = onNodeWithContentDescription("Navigate back")
private fun ComposeTestRule.nameTextInput() = onNodeWithTag(EditAddressTestTag.NAME_FIELD).onChildAt(0)
private fun ComposeTestRule.streetAddressTextInput() = onNodeWithTag(EditAddressTestTag.STREET_ADDRESS_FIELD).onChildAt(0)
private fun ComposeTestRule.cityTextInput() = onNodeWithTag(EditAddressTestTag.CITY_FIELD).onChildAt(0)
private fun ComposeTestRule.subRegionDropDown() = onNodeWithTag(EditAddressTestTag.SUBREGION_FIELD)
private fun ComposeTestRule.zipCodeTextInput() = onNodeWithTag(EditAddressTestTag.ZIP_FIELD).onChildAt(0)
private fun ComposeTestRule.countryDropDown() = onNodeWithTag(EditAddressTestTag.COUNTRY_FIELD)
private fun ComposeTestRule.phoneTextInput() = onNodeWithTag(EditAddressTestTag.PHONE_FIELD).onChildAt(0)
private fun ComposeTestRule.emailTextInput() = onNodeWithTag(EditAddressTestTag.EMAIL_FIELD).onChildAt(0)
private fun ComposeTestRule.saveButton() = onNodeWithTag(EditAddressTestTag.SAVE_BUTTON)
private fun ComposeTestRule.cancelButton() = onNodeWithTag(EditAddressTestTag.CANCEL_BUTTON)
private fun ComposeTestRule.deleteAddressButton() = onNodeWithTag(EditAddressTestTag.DELETE_BUTTON)
private fun ComposeTestRule.toolbarDeleteAddressButton() = onNodeWithTag(EditAddressTestTag.TOPBAR_DELETE_BUTTON)
private fun ComposeTestRule.cancelDeleteAddressButton() = onNodeWithTag(EditAddressTestTag.DIALOG_CANCEL_BUTTON)
private fun ComposeTestRule.confirmDeleteAddressButton() = onNodeWithTag(EditAddressTestTag.DIALOG_DELETE_BUTTON)

private fun creditCardsSectionTitle() = itemContainingText(getStringResource(R.string.preferences_credit_cards_2))
private fun saveAndAutofillCreditCardsOption() = itemContainingText(getStringResource(R.string.preferences_credit_cards_save_and_autofill_cards_2))
private fun saveAndAutofillCreditCardsSummary() = itemContainingText(getStringResource(R.string.preferences_credit_cards_save_and_autofill_cards_summary_2))
private fun syncCreditCardsAcrossDevicesButton() = itemContainingText(getStringResource(R.string.preferences_credit_cards_sync_cards_across_devices))
private fun addCreditCardButton() = mDevice.findObject(UiSelector().textContains(getStringResource(R.string.preferences_credit_cards_add_credit_card_2)))
private fun savedCreditCardsToolbarTitle() = itemContainingText(getStringResource(R.string.credit_cards_saved_cards))
private fun editCreditCardToolbarTitle() = itemContainingText(getStringResource(R.string.credit_cards_edit_card))
private fun manageSavedCreditCardsButton() = mDevice.findObject(UiSelector().textContains(getStringResource(R.string.preferences_credit_cards_manage_saved_cards_2)))
private fun creditCardNumberTextInput() = mDevice.findObject(UiSelector().resourceId("$packageName:id/card_number_input"))
private fun nameOnCreditCardTextInput() = mDevice.findObject(UiSelector().resourceId("$packageName:id/name_on_card_input"))
private fun expiryMonthDropDown() = mDevice.findObject(UiSelector().resourceId("$packageName:id/expiry_month_drop_down"))
private fun expiryYearDropDown() = mDevice.findObject(UiSelector().resourceId("$packageName:id/expiry_year_drop_down"))
private fun savedCreditCardNumber() = mDevice.findObject(UiSelector().resourceId("$packageName:id/credit_card_logo"))
private fun deleteCreditCardToolbarButton() = mDevice.findObject(UiSelector().resourceId("$packageName:id/delete_credit_card_button"))
private fun saveCreditCardToolbarButton() = itemWithResId("$packageName:id/save_credit_card_button")
private fun deleteCreditCardMenuButton() = itemContainingText(getStringResource(R.string.credit_cards_delete_card_button))
private fun confirmDeleteCreditCardButton() = onView(withId(android.R.id.button1)).inRoot(RootMatchers.isDialog())
private fun cancelDeleteCreditCardButton() = onView(withId(android.R.id.button2)).inRoot(RootMatchers.isDialog())
private fun securedCreditCardsLaterButton() = onView(withId(android.R.id.button2)).inRoot(RootMatchers.isDialog())
private fun saveButton() = itemWithResId("$packageName:id/save_button")
private fun cancelButton() = itemWithResId("$packageName:id/cancel_button")
private fun savedAddress(name: String) = mDevice.findObject(UiSelector().textContains(name))
private fun ComposeTestRule.subRegionOption(subRegion: String) = onNodeWithText(subRegion)
private fun ComposeTestRule.countryOption(country: String) = onNodeWithText(country)

private fun expiryMonthOption(expiryMonth: String) = mDevice.findObject(UiSelector().textContains(expiryMonth))
private fun expiryYearOption(expiryYear: String) = mDevice.findObject(UiSelector().textContains(expiryYear))
