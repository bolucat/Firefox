// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script_Extensions=Carian`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0000B7,
    0x00205A,
    0x00205D,
    0x002E31
  ],
  ranges: [
    [0x0102A0, 0x0102D0]
  ]
});
testPropertyEscapes(
  /^\p{Script_Extensions=Carian}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Carian}"
);
testPropertyEscapes(
  /^\p{Script_Extensions=Cari}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Cari}"
);
testPropertyEscapes(
  /^\p{scx=Carian}+$/u,
  matchSymbols,
  "\\p{scx=Carian}"
);
testPropertyEscapes(
  /^\p{scx=Cari}+$/u,
  matchSymbols,
  "\\p{scx=Cari}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0000B6],
    [0x0000B8, 0x002059],
    [0x00205B, 0x00205C],
    [0x00205E, 0x002E30],
    [0x002E32, 0x00DBFF],
    [0x00E000, 0x01029F],
    [0x0102D1, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script_Extensions=Carian}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Carian}"
);
testPropertyEscapes(
  /^\P{Script_Extensions=Cari}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Cari}"
);
testPropertyEscapes(
  /^\P{scx=Carian}+$/u,
  nonMatchSymbols,
  "\\P{scx=Carian}"
);
testPropertyEscapes(
  /^\P{scx=Cari}+$/u,
  nonMatchSymbols,
  "\\P{scx=Cari}"
);

reportCompare(0, 0);
