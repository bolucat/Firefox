/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

const PAGE_URL = "https://example.com/chrome/dom/base/test/file_empty.html";
const languages = ["en-US", "es-ES", "fr-FR"];

add_task(async function test_set_language_override() {
  const tab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
  const browser = gBrowser.getBrowserForTab(tab);

  await BrowserTestUtils.browserLoaded(browser);

  info("Get default language");
  const defaultLanguage = await getIntlLanguage(browser);

  const browsingContext = browser.browsingContext;

  const languageOverride = getLanguageToOverride(defaultLanguage);

  info("Set language override");
  browsingContext.languageOverride = languageOverride;
  await assertLanguageOverridden(browser, languageOverride);

  const secondLanguageOverride = getSecondLanguageToOverride(
    defaultLanguage,
    languageOverride
  );

  info("Set language override again");
  browsingContext.languageOverride = secondLanguageOverride;
  await assertLanguageOverridden(browser, secondLanguageOverride);

  info("Reset language override");
  browsingContext.languageOverride = "";
  await assertLanguageIsNotOverridden(browser, defaultLanguage);

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_set_language_override_and_navigate() {
  const tab = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
  const browser = gBrowser.getBrowserForTab(tab);

  await BrowserTestUtils.browserLoaded(browser);

  info("Get default language");
  const defaultLanguage = await getIntlLanguage(browser);

  const browsingContext = browser.browsingContext;

  const languageOverride = getLanguageToOverride(defaultLanguage);

  info("Set language override");
  browsingContext.languageOverride = languageOverride;
  await assertLanguageOverridden(browser, languageOverride);

  info("Navigate browsing context");
  const url = "https://example.com/chrome/dom/base/test/dummy.html";
  const loaded = BrowserTestUtils.browserLoaded(browser, false, url, false);
  BrowserTestUtils.startLoadingURIString(browser, url);
  await loaded;
  await assertLanguageOverridden(browser, languageOverride);

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_set_language_override_in_different_contexts() {
  const tab1 = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
  const browser1 = gBrowser.getBrowserForTab(tab1);

  await BrowserTestUtils.browserLoaded(browser1);

  const tab2 = BrowserTestUtils.addTab(gBrowser, PAGE_URL);
  const browser2 = gBrowser.getBrowserForTab(tab2);

  await BrowserTestUtils.browserLoaded(browser2);

  info("Get default language in the first tab");
  const defaultLanguage1 = await getIntlLanguage(browser1);

  info("Get default language in the second tab");
  const defaultLanguage2 = await getIntlLanguage(browser2);

  const browsingContext1 = browser1.browsingContext;

  const languageOverride = getLanguageToOverride(defaultLanguage1);

  info("Set language override to the first tab");
  browsingContext1.languageOverride = languageOverride;
  await assertLanguageOverridden(browser1, languageOverride);

  info("Make sure that in the second tab language is not overridden");
  await assertLanguageIsNotOverridden(browser2, defaultLanguage2);

  info("Reset language override");
  browsingContext1.languageOverride = "";
  await assertLanguageIsNotOverridden(browser1, defaultLanguage1);

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
});

function getLanguageToOverride(defaultLanguage) {
  return languages.find(lang => lang !== defaultLanguage);
}

function getSecondLanguageToOverride(defaultLanguage, secondLanguage) {
  return languages.find(
    lang => lang !== defaultLanguage && lang !== secondLanguage
  );
}

async function getIntlLanguage(browser) {
  return SpecialPowers.spawn(browser, [], () => {
    return content.eval(`Intl.DateTimeFormat().resolvedOptions().locale`);
  });
}

async function assertLanguageOverridden(browser, languageOverride) {
  is(
    await getIntlLanguage(browser),
    languageOverride,
    "new Intl.DateTimeFormat().resolvedOptions().locale is overridden"
  );
}

async function assertLanguageIsNotOverridden(browser, defaultLanguage) {
  is(
    await getIntlLanguage(browser),
    defaultLanguage,
    "new Intl.DateTimeFormat().resolvedOptions().locale is not overridden"
  );
}
