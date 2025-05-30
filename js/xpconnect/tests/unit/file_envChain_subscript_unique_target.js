/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var unique_target_qualified = 10;
unique_target_unqualified = 20;
let unique_target_lexical = 30;
this.unique_target_prop = 40;

const funcs = Cu.getJSTestingFunctions();
const envs = [];
let env = funcs.getInnerMostEnvironmentObject();
while (env) {
  envs.push({
    type: funcs.getEnvironmentObjectType(env) || "*SystemGlobal*",
    qualified: !!Object.getOwnPropertyDescriptor(env, "unique_target_qualified"),
    unqualified: !!Object.getOwnPropertyDescriptor(env, "unique_target_unqualified"),
    lexical: !!Object.getOwnPropertyDescriptor(env, "unique_target_lexical"),
    prop: !!Object.getOwnPropertyDescriptor(env, "unique_target_prop"),
  });

  env = funcs.getEnclosingEnvironmentObject(env);
}

this.ENVS = envs;
