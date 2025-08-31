// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2023 Justin Grant. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal-intl
description: Islamic calendar "islamic".
features: [Temporal]
---*/

assert.throws(RangeError, function () {
  Temporal.PlainDate.from({ year, month: 1, day: 1, calendar });
}, "fallback for calendar ID 'islamic' only supported in Intl.DateTimeFormat constructor, not Temporal");

reportCompare(0, 0);
