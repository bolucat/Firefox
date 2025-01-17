// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Kate Miháliková. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.tolocalestring
description: >
    toLocaleString return a string.
features: [Temporal]
---*/

const datetime = new Temporal.PlainDateTime(2000, 5, 2, 12, 34, 56, 987, 654, 321);

assert.sameValue(typeof datetime.toLocaleString("en", { dateStyle: "short" }), "string");
assert.sameValue(typeof datetime.toLocaleString("en", { timeStyle: "short" }), "string");

reportCompare(0, 0);
