// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script_Extensions=Tangsa`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x016A70, 0x016ABE],
    [0x016AC0, 0x016AC9]
  ]
});
testPropertyEscapes(
  /^\p{Script_Extensions=Tangsa}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Tangsa}"
);
testPropertyEscapes(
  /^\p{Script_Extensions=Tnsa}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Tnsa}"
);
testPropertyEscapes(
  /^\p{scx=Tangsa}+$/u,
  matchSymbols,
  "\\p{scx=Tangsa}"
);
testPropertyEscapes(
  /^\p{scx=Tnsa}+$/u,
  matchSymbols,
  "\\p{scx=Tnsa}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x016ABF
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00DBFF],
    [0x00E000, 0x016A6F],
    [0x016ACA, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script_Extensions=Tangsa}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Tangsa}"
);
testPropertyEscapes(
  /^\P{Script_Extensions=Tnsa}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Tnsa}"
);
testPropertyEscapes(
  /^\P{scx=Tangsa}+$/u,
  nonMatchSymbols,
  "\\P{scx=Tangsa}"
);
testPropertyEscapes(
  /^\P{scx=Tnsa}+$/u,
  nonMatchSymbols,
  "\\P{scx=Tnsa}"
);

reportCompare(0, 0);
