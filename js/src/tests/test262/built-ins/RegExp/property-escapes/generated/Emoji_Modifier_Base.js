// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Emoji_Modifier_Base`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v16.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x00261D,
    0x0026F9,
    0x01F385,
    0x01F3C7,
    0x01F47C,
    0x01F48F,
    0x01F491,
    0x01F4AA,
    0x01F57A,
    0x01F590,
    0x01F6A3,
    0x01F6C0,
    0x01F6CC,
    0x01F90C,
    0x01F90F,
    0x01F926,
    0x01F977,
    0x01F9BB
  ],
  ranges: [
    [0x00270A, 0x00270D],
    [0x01F3C2, 0x01F3C4],
    [0x01F3CA, 0x01F3CC],
    [0x01F442, 0x01F443],
    [0x01F446, 0x01F450],
    [0x01F466, 0x01F478],
    [0x01F481, 0x01F483],
    [0x01F485, 0x01F487],
    [0x01F574, 0x01F575],
    [0x01F595, 0x01F596],
    [0x01F645, 0x01F647],
    [0x01F64B, 0x01F64F],
    [0x01F6B4, 0x01F6B6],
    [0x01F918, 0x01F91F],
    [0x01F930, 0x01F939],
    [0x01F93C, 0x01F93E],
    [0x01F9B5, 0x01F9B6],
    [0x01F9B8, 0x01F9B9],
    [0x01F9CD, 0x01F9CF],
    [0x01F9D1, 0x01F9DD],
    [0x01FAC3, 0x01FAC5],
    [0x01FAF0, 0x01FAF8]
  ]
});
testPropertyEscapes(
  /^\p{Emoji_Modifier_Base}+$/u,
  matchSymbols,
  "\\p{Emoji_Modifier_Base}"
);
testPropertyEscapes(
  /^\p{EBase}+$/u,
  matchSymbols,
  "\\p{EBase}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x01F484,
    0x01F490,
    0x01F9B7,
    0x01F9BA,
    0x01F9D0
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00261C],
    [0x00261E, 0x0026F8],
    [0x0026FA, 0x002709],
    [0x00270E, 0x00DBFF],
    [0x00E000, 0x01F384],
    [0x01F386, 0x01F3C1],
    [0x01F3C5, 0x01F3C6],
    [0x01F3C8, 0x01F3C9],
    [0x01F3CD, 0x01F441],
    [0x01F444, 0x01F445],
    [0x01F451, 0x01F465],
    [0x01F479, 0x01F47B],
    [0x01F47D, 0x01F480],
    [0x01F488, 0x01F48E],
    [0x01F492, 0x01F4A9],
    [0x01F4AB, 0x01F573],
    [0x01F576, 0x01F579],
    [0x01F57B, 0x01F58F],
    [0x01F591, 0x01F594],
    [0x01F597, 0x01F644],
    [0x01F648, 0x01F64A],
    [0x01F650, 0x01F6A2],
    [0x01F6A4, 0x01F6B3],
    [0x01F6B7, 0x01F6BF],
    [0x01F6C1, 0x01F6CB],
    [0x01F6CD, 0x01F90B],
    [0x01F90D, 0x01F90E],
    [0x01F910, 0x01F917],
    [0x01F920, 0x01F925],
    [0x01F927, 0x01F92F],
    [0x01F93A, 0x01F93B],
    [0x01F93F, 0x01F976],
    [0x01F978, 0x01F9B4],
    [0x01F9BC, 0x01F9CC],
    [0x01F9DE, 0x01FAC2],
    [0x01FAC6, 0x01FAEF],
    [0x01FAF9, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Emoji_Modifier_Base}+$/u,
  nonMatchSymbols,
  "\\P{Emoji_Modifier_Base}"
);
testPropertyEscapes(
  /^\P{EBase}+$/u,
  nonMatchSymbols,
  "\\P{EBase}"
);

reportCompare(0, 0);
