/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const REMOTE_SETTINGS_RECORDS = [
  {
    type: "dynamic-suggestions",
    suggestion_type: "important_dates",
    attachment: [
      {
        keywords: [["event", [" 1"]]],
        data: {
          result: {
            isImportantDate: true,
            payload: {
              dates: ["2025-03-05", "2026-02-18"],
              name: "Event 1",
            },
          },
        },
      },
      {
        keywords: [["multi d", ["ay event"]]],
        data: {
          result: {
            isImportantDate: true,
            payload: {
              dates: [
                ["2025-06-10", "2025-06-20"],
                ["2026-06-10", "2026-06-20"],
              ],
              name: "Multi Day Event",
            },
          },
        },
      },
    ],
  },
  {
    type: "dynamic-suggestions",
    suggestion_type: "other_suggestions",
    attachment: [
      {
        keywords: [["event", [" 2"]]],
        data: {
          result: {
            isBestMatch: true,
            payload: {
              title: "Top Pick Suggestion 1",
              url: "https://foo.com/",
              description:
                "A suggestion that just so happens to have the same keyword",
            },
          },
        },
      },
    ],
  },
  {
    type: "dynamic-suggestions",
    suggestion_type: "other_suggestions",
    attachment: [
      {
        keywords: [["event", [" 3"]]],
        data: {
          result: {
            isBestMatch: true,
            payload: {
              title: "Top Pick Suggestion 2",
              url: "https://foo.com/",
              description:
                "Another suggestion that just so happens to have the same keyword",
            },
          },
        },
      },
    ],
  },
];

let SystemDate;

add_setup(async function () {
  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    remoteSettingsRecords: REMOTE_SETTINGS_RECORDS,
    prefs: [
      [
        "quicksuggest.dynamicSuggestionTypes",
        "important_dates,other_suggestions",
      ],
      ["suggest.quicksuggest.nonsponsored", true],
      ["suggest.quicksuggest.sponsored", true],
      ["quicksuggest.ampTopPickCharThreshold", 0],
    ],
  });
  await Services.search.init();

  SystemDate = Cu.getGlobalForObject(QuickSuggestTestUtils).Date;
});

add_task(async function fourDaysBefore() {
  setTime("2025-03-01T00:00");

  let query = "event 1";
  let expected = makeExpectedResult({
    date: "Wednesday, March 5, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-countdown",
      args: { name: "Event 1", daysUntilStart: 4 },
    },
  });

  await checkDatesResults(query, expected);
});

add_task(async function onDayOfEvent() {
  setTime("2025-03-05T00:00");

  let query = "event 1";
  let expected = makeExpectedResult({
    date: "Wednesday, March 5, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-today",
      args: { name: "Event 1" },
    },
  });

  await checkDatesResults(query, expected);
});

add_task(async function oneDayAfter() {
  setTime("2025-03-06T00:00");

  let query = "event 1";
  let expected = makeExpectedResult({
    // Should select the event in the next year.
    date: "Wednesday, February 18, 2026",
    // Since the event is over SHOW_COUNTDOWN_THRESHOLD_DAYS
    // days away, it should not display the countdown.
    description: "Event 1",
  });

  await checkDatesResults(query, expected);
});

add_task(async function afterAllInstances() {
  setTime("2027-01-01T00:00");

  let query = "event 1";
  let expected = null;

  await checkDatesResults(query, expected);
});

add_task(async function beforeMultiDay() {
  setTime("2025-06-09T23:59");

  let query = "multi day event";
  let expected = makeExpectedResult({
    date: "June 10 – 20, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-countdown-range",
      args: { name: "Multi Day Event", daysUntilStart: 1 },
    },
  });

  await checkDatesResults(query, expected);
});

add_task(async function duringMultiDay() {
  setTime("2025-06-19T00:00");

  let query = "multi day event";
  let expected = makeExpectedResult({
    date: "June 10 – 20, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-ongoing",
      args: { name: "Multi Day Event", daysUntilEnd: 1 },
    },
  });

  await checkDatesResults(query, expected);
});

add_task(async function lastDayDuringMultiDay() {
  setTime("2025-06-20T00:00");

  let query = "multi day event";
  let expected = makeExpectedResult({
    date: "June 10 – 20, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-ends-today",
      args: { name: "Multi Day Event" },
    },
  });

  await checkDatesResults(query, expected);
});

// Test whether the date suggestion is before the other
// isBestMatch suggestion.
add_task(async function testTwoSuggestions() {
  setTime("2025-03-01T00:00");

  // 1 date suggestion and 2 other suggestions match this, but
  // one of the two other suggestions should be deduped.
  let query = "event";
  let expectedDateSuggestion = makeExpectedResult({
    date: "Wednesday, March 5, 2025",
    descriptionL10n: {
      id: "urlbar-result-dates-countdown",
      args: { daysUntilStart: 4, name: "Event 1" },
      cacheable: true,
      excludeArgsFromCacheKey: true,
    },
  });

  let expectedOtherSuggestion = {
    type: UrlbarUtils.RESULT_TYPE.URL,
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    heuristic: false,
    isBestMatch: true,
    isRichSuggestion: true,
    suggestedIndex: 1,
    payload: {
      source: "rust",
      provider: "Dynamic",
      title: "Top Pick Suggestion 1",
      url: "https://foo.com/",
      telemetryType: "other_suggestions",
      displayUrl: "foo.com",
      description: "A suggestion that just so happens to have the same keyword",
      isManageable: true,
      isSponsored: false,
      helpUrl: QuickSuggest.HELP_URL,
    },
  };

  await checkDatesResults(query, [
    expectedDateSuggestion,
    expectedOtherSuggestion,
  ]);
});

/**
 * Stubs the Date object of the system global to use timeStr
 * when the constructor is called without arguments.
 *
 * @param {string} timeStr
 *   An ISO time string.
 */
function setTime(timeStr) {
  let DateStub = function (...args) {
    if (!args.length) {
      return new SystemDate(timeStr);
    }
    return new SystemDate(...args);
  };
  DateStub.prototype = SystemDate.prototype;

  Object.getOwnPropertyNames(SystemDate).forEach(prop => {
    const desc = Object.getOwnPropertyDescriptor(SystemDate, prop);
    Object.defineProperty(DateStub, prop, desc);
  });

  Cu.getGlobalForObject(QuickSuggestTestUtils).Date = DateStub;
}

async function checkDatesResults(query, expected) {
  info(
    "Doing query: " +
      JSON.stringify({
        query,
        expected,
      })
  );
  await check_results({
    context: createContext(query, {
      providers: [UrlbarProviderQuickSuggest.name],
      isPrivate: false,
    }),
    matches: expected ? [expected].flat() : [],
  });
}

function makeExpectedResult({
  date,
  description,
  descriptionL10n,
  isSponsored = false,
  isBestMatch = true,
  isRichSuggestion = undefined,
}) {
  let name = description ?? descriptionL10n.args.name;
  return {
    type: UrlbarUtils.RESULT_TYPE.URL,
    source: UrlbarUtils.RESULT_SOURCE.SEARCH,
    heuristic: false,
    isBestMatch,
    suggestedIndex: 1,
    isRichSuggestion,
    payload: {
      title: date,
      url: Services.search.defaultEngine.getSubmission(name).uri.spec,
      description,
      descriptionL10n: descriptionL10n
        ? { cacheable: true, excludeArgsFromCacheKey: true, ...descriptionL10n }
        : undefined,
      isSponsored,
      telemetryType: "important_dates",
      source: "rust",
      provider: "Dynamic",
      isManageable: true,
      isBlockable: true,
      helpUrl: QuickSuggest.HELP_URL,
      icon: "chrome://browser/skin/calendar-24.svg",
    },
  };
}
