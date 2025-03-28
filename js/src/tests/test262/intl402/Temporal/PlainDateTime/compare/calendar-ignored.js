// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.compare
description: Calendar is not taken into account for the comparison.
features: [Temporal]
---*/

const dt1 = new Temporal.PlainDateTime(1976, 11, 18, 15, 23, 30, 123, 456, 789, "iso8601");
const dt2 = new Temporal.PlainDateTime(2019, 10, 29, 10, 46, 38, 271, 986, 102, "iso8601");
const dt3 = new Temporal.PlainDateTime(2019, 10, 29, 10, 46, 38, 271, 986, 102, "iso8601");
const dt4 = new Temporal.PlainDateTime(2019, 10, 29, 10, 46, 38, 271, 986, 102, "gregory");

assert.sameValue(Temporal.PlainDateTime.compare(dt1, dt2), -1, "smaller");
assert.sameValue(Temporal.PlainDateTime.compare(dt2, dt3), 0, "equal with same calendar");
assert.sameValue(Temporal.PlainDateTime.compare(dt2, dt4), 0, "equal with different calendar");

reportCompare(0, 0);
