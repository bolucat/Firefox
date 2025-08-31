// |reftest| shell-option(--enable-upsert) skip-if(!Map.prototype.getOrInsertComputed||!xulRuntime.shell) -- upsert is not enabled unconditionally, requires shell-options
// Copyright (C) 2015 the V8 project authors. All rights reserved.
// Copyright (C) 2025 Jonas Haukenes. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-weakmap.prototype.getorinsertcomputed
description: |
  Throws TypeError if `this` doesn't have a [[WeakMapData]] internal slot.
info: |
  WeakMap.prototype.getOrInsertComputed ( key, callbackfn )

  ...
  2. Perform ? RequireInternalSlot(M, [[WeakMapData]]).
  ...
features: [WeakMap, upsert]
---*/
assert.throws(TypeError, function() {
  WeakMap.prototype.getOrInsertComputed.call(WeakMap.prototype, {}, () => 1);
});

var map = new WeakMap();
assert.throws(TypeError, function() {
  map.getOrInsertComputed.call(WeakMap.prototype, {}, () => 1);
});


reportCompare(0, 0);
