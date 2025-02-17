// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaintime.prototype.add
description: Objects of a subclass are never created as return values for add()
includes: [temporalHelpers.js]
features: [Temporal]
---*/

TemporalHelpers.checkSubclassingIgnored(
  Temporal.PlainTime,
  [12, 34, 56, 987, 654, 321],
  "add",
  [{ nanoseconds: 1 }],
  (result) => TemporalHelpers.assertPlainTime(result, 12, 34, 56, 987, 654, 322),
);

reportCompare(0, 0);
