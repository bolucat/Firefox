// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `General_Category=Initial_Punctuation`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0000AB,
    0x002018,
    0x00201F,
    0x002039,
    0x002E02,
    0x002E04,
    0x002E09,
    0x002E0C,
    0x002E1C,
    0x002E20
  ],
  ranges: [
    [0x00201B, 0x00201C]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Initial_Punctuation}+$/u,
  matchSymbols,
  "\\p{General_Category=Initial_Punctuation}"
);
testPropertyEscapes(
  /^\p{General_Category=Pi}+$/u,
  matchSymbols,
  "\\p{General_Category=Pi}"
);
testPropertyEscapes(
  /^\p{gc=Initial_Punctuation}+$/u,
  matchSymbols,
  "\\p{gc=Initial_Punctuation}"
);
testPropertyEscapes(
  /^\p{gc=Pi}+$/u,
  matchSymbols,
  "\\p{gc=Pi}"
);
testPropertyEscapes(
  /^\p{Initial_Punctuation}+$/u,
  matchSymbols,
  "\\p{Initial_Punctuation}"
);
testPropertyEscapes(
  /^\p{Pi}+$/u,
  matchSymbols,
  "\\p{Pi}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x002E03
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0000AA],
    [0x0000AC, 0x002017],
    [0x002019, 0x00201A],
    [0x00201D, 0x00201E],
    [0x002020, 0x002038],
    [0x00203A, 0x002E01],
    [0x002E05, 0x002E08],
    [0x002E0A, 0x002E0B],
    [0x002E0D, 0x002E1B],
    [0x002E1D, 0x002E1F],
    [0x002E21, 0x00DBFF],
    [0x00E000, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Initial_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Initial_Punctuation}"
);
testPropertyEscapes(
  /^\P{General_Category=Pi}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Pi}"
);
testPropertyEscapes(
  /^\P{gc=Initial_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{gc=Initial_Punctuation}"
);
testPropertyEscapes(
  /^\P{gc=Pi}+$/u,
  nonMatchSymbols,
  "\\P{gc=Pi}"
);
testPropertyEscapes(
  /^\P{Initial_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{Initial_Punctuation}"
);
testPropertyEscapes(
  /^\P{Pi}+$/u,
  nonMatchSymbols,
  "\\P{Pi}"
);

reportCompare(0, 0);
