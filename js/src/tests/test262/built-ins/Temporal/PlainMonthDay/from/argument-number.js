// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainmonthday.from
description: A number is invalid in place of an ISO string for Temporal.PlainMonthDay
features: [Temporal]
---*/

const numbers = [
  1,
  1118,
  -1118,
  12345,
];

for (const arg of numbers) {
  assert.throws(
    TypeError,
    () => Temporal.PlainMonthDay.from(arg),
    `A number (${arg}) is not a valid ISO string for PlainMonthDay`
  );
}

reportCompare(0, 0);
