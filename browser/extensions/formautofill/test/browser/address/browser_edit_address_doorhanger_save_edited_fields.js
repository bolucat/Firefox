"use strict";

const SUBMIT_RECORD = {
  "given-name": "John",
  "family-name": "Doe",
  organization: "Mozilla",
  "street-address": "123 Sesame Street",
  tel: "+13453453456",
};

const TEST_CASE = [
  {
    description: "adding the email field",
    editedFields: {
      email: "test@mozilla.org",
    },
  },
  {
    description: "changing the given-name field",
    editedFields: {
      name: "Jane",
    },
  },
  {
    description: "appending the street-address field",
    editedFields: {
      "street-address": SUBMIT_RECORD["street-address"] + " 4F",
    },
  },
  {
    description: "removing some fields",
    editedFields: {
      name: "",
      tel: "",
    },
  },
  {
    description: "doing all kinds of stuff",
    editedFields: {
      organization: SUBMIT_RECORD.organization.toLowerCase(),
      "address-level1": "California",
      tel: "",
      "street-address": SUBMIT_RECORD["street-address"] + " Apt.6",
      name: "Jane Doe",
    },
  },
];

function recordToFormSelector(record) {
  let obj = {};
  for (const [key, value] of Object.entries(record)) {
    obj[`#${key}`] = value;
  }
  return obj;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.formautofill.addresses.capture.enabled", true],
      ["extensions.formautofill.addresses.supported", "on"],
      ["extensions.formautofill.addresses.capture.requiredFields", ""],
    ],
  });
});

// Test different scenarios when we change something in the edit address dorhanger
add_task(async function test_save_edited_fields() {
  await expectSavedAddressesCount(0);

  for (const TEST of TEST_CASE) {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: ADDRESS_FORM_URL },
      async function (browser) {
        info(`Test ${TEST.description}`);

        info(`Wait for save doorhanger shown`);
        const onSavePopupShown = waitForPopupShown();
        await focusUpdateSubmitForm(browser, {
          focusSelector: "#given-name",
          newValues: recordToFormSelector(SUBMIT_RECORD),
        });
        await onSavePopupShown;

        info(`Wait for edit doorhanger shown`);
        const onEditPopupShown = waitForPopupShown();
        await clickAddressDoorhangerButton(EDIT_ADDRESS_BUTTON);
        await onEditPopupShown;

        info(`Fill edit doorhanger`);
        fillEditDoorhanger(TEST.editedFields);
        await clickAddressDoorhangerButton(MAIN_BUTTON);
      }
    );

    const expectedRecord = normalizeAddressFields({
      ...SUBMIT_RECORD,
      ...TEST.editedFields,
    });

    const addresses = await expectSavedAddressesCount(1);
    for (const [key, value] of Object.entries(expectedRecord)) {
      is(addresses[0][key] ?? "", value, `${key} field is saved`);
    }

    await removeAllRecords();
  }
});
