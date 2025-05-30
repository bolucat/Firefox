// |reftest| error:SyntaxError
// Copyright (C) 2013 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: >
    A newline may not precede the `*` token in a `yield` expression.
features: [generators]
es6id: 14.4
negative:
  phase: parse
  type: SyntaxError
---*/

$DONOTEVALUATE();

var obj = {
  *g() {
    yield
    * 1
  }
};
