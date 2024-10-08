// |reftest| skip error:SyntaxError module -- source-phase-imports is not supported
// Copyright (C) 2024 Chengzhong Wu. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: >
  ImportBinding in ImportDeclaration may be 'source' and 'from'
esid: sec-modules
info: |
  ImportDeclaration:
    import source ImportedBinding FromClause ;

negative:
  phase: resolution
  type: SyntaxError
features: [source-phase-imports]
flags: [module]
---*/

$DONOTEVALUATE();

import "../resources/ensure-linking-error_FIXTURE.js";

import source from '<do not resolve>';
import from from '<do not resolve>';
