// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2020 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Temporal.PlainDateTime.from accepts a custom timezone that starts with "c".
esid: sec-temporal.plaindatetime.from
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const dateTime = Temporal.PlainDateTime.from("2020-01-01T00:00:00+01:00[Custom]");
TemporalHelpers.assertPlainDateTime(dateTime, 2020, 1, "M01", 1, 0, 0, 0, 0, 0, 0);

reportCompare(0, 0);
