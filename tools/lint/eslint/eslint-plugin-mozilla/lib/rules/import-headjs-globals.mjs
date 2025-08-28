/**
 * @file Import globals from head.js and from any files that were
 * imported by head.js (as far as we can correctly resolve the path).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import fs from "fs";
import helpers from "../helpers.mjs";
import globals from "../globals.mjs";

function importHead(context, filePath, node) {
  try {
    let stats = fs.statSync(filePath);
    if (!stats.isFile()) {
      return;
    }
  } catch (e) {
    return;
  }

  let newGlobals = globals.getGlobalsForFile({ filePath });
  helpers.addGlobals(newGlobals, context.sourceCode.getScope(node));
}

export default {
  // This rule currently has no messages.
  // eslint-disable-next-line eslint-plugin/prefer-message-ids
  meta: {
    docs: {
      url: "https://firefox-source-docs.mozilla.org/code-quality/lint/linters/eslint-plugin-mozilla/rules/import-headjs-globals.html",
    },
    schema: [],
    type: "problem",
  },

  create(context) {
    return {
      Program(node) {
        let heads = helpers.getTestHeadFiles(context);
        for (let head of heads) {
          importHead(context, head, node);
        }
      },
    };
  },
};
