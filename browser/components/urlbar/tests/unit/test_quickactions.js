/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ActionsProviderQuickActions:
    "resource:///modules/ActionsProviderQuickActions.sys.mjs",
});

add_setup(async () => {
  UrlbarPrefs.set("secondaryActions.featureGate", true);

  ActionsProviderQuickActions.addAction("newaction", {
    commands: ["newaction"],
  });

  registerCleanupFunction(async () => {
    UrlbarPrefs.clear("secondaryActions.featureGate");
    ActionsProviderQuickActions.removeAction("newaction");
  });
});

add_task(async function nomatch() {
  let context = createContext("this doesnt match", {});
  let results = await ActionsProviderQuickActions.queryActions(context);
  Assert.strictEqual(results, null, "there were no matches");
});

add_task(async function quickactions_match() {
  let context = createContext("new", {});
  let results = await ActionsProviderQuickActions.queryActions(context);
  Assert.equal(results[0].key, "newaction", "Matched the new action");
});

add_task(async function quickactions_match_multiple() {
  ActionsProviderQuickActions.addAction("multiaction", {
    commands: ["testcommand1", "commandtest2"],
  });

  let context = createContext("testcommand1", {});
  let results = await ActionsProviderQuickActions.queryActions(context);
  Assert.equal(
    results[0].key,
    "multiaction",
    "Matched the action with first keyword"
  );

  context = createContext("commandtest2", {});
  results = await ActionsProviderQuickActions.queryActions(context);
  Assert.equal(
    results[0].key,
    "multiaction",
    "Matched the action with first keyword"
  );

  ActionsProviderQuickActions.removeAction("multiaction");
});

add_task(async function duplicate_matches() {
  ActionsProviderQuickActions.addAction("testaction", {
    commands: ["testaction", "test"],
  });

  let context = createContext("test", {});
  let results = await ActionsProviderQuickActions.queryActions(context);

  Assert.equal(results[0].key, "testaction", "Matched the test action");

  ActionsProviderQuickActions.removeAction("testaction");
});

add_task(async function remove_action() {
  ActionsProviderQuickActions.addAction("testaction", {
    commands: ["testaction"],
  });
  ActionsProviderQuickActions.removeAction("testaction");

  let context = createContext("test", {});
  let result = await ActionsProviderQuickActions.queryActions(context);

  Assert.strictEqual(result, null, "there were no matches");
});

add_task(async function minimum_search_string() {
  let searchString = "newa";
  for (let minimumSearchString of [3]) {
    info(`Setting 'minimumSearchString' to ${minimumSearchString}`);
    UrlbarPrefs.set("quickactions.minimumSearchString", minimumSearchString);
    for (let i = 1; i < 4; i++) {
      let context = createContext(searchString.substring(0, i), {});
      let result = await ActionsProviderQuickActions.queryActions(context);
      let isActive = await ActionsProviderQuickActions.isActive(context);

      if (i >= minimumSearchString) {
        Assert.equal(result[0].key, "newaction", "Matched the new action");
        Assert.equal(isActive, true, "Provider is active");
      } else {
        Assert.equal(isActive, false, "Provider is not active");
      }
    }
  }
  UrlbarPrefs.clear("quickactions.minimumSearchString");
});

add_task(async function interventions_disabled() {
  let interventionsProvider = UrlbarProvidersManager.getProvider(
    "UrlbarProviderInterventions"
  );
  // Mock the relevent method of Query so we don't have to start a real one.
  interventionsProvider.queryInstance = {
    getProvider: name => UrlbarProvidersManager.getProvider(name),
  };
  let context = createContext("test", { isPrivate: false });

  Assert.ok(
    !(await interventionsProvider.isActive(context)),
    "Urlbar interventions are disabled when actions are enabled"
  );
  interventionsProvider.queryInstance = null;
});

add_task(async function test_multiple_exact_matches() {
  ActionsProviderQuickActions.addAction("multiaction1", {
    commands: ["testcommand1"],
  });
  ActionsProviderQuickActions.addAction("multiaction2", {
    commands: ["testcommand1"],
  });

  let context = createContext("testcommand1", {});
  let results = await ActionsProviderQuickActions.queryActions(context);

  Assert.equal(results.length, 2, "Matched both actions");
  Assert.equal(results[0].key, "multiaction1", "Matched the test action");
  Assert.equal(results[1].key, "multiaction2", "Matched the test action");

  ActionsProviderQuickActions.removeAction("multiaction1");
  ActionsProviderQuickActions.removeAction("multiaction2");
});
