export const description = `Validation tests for diagnostic directive and attribute`;

import { makeTestGroup } from '../../../../common/framework/test_group.js';
import { keysOf } from '../../../../common/util/data_tables.js';
import { ShaderValidationTest } from '../shader_validation_test.js';

export const g = makeTestGroup(ShaderValidationTest);

const kSpecDiagnosticRules = ['derivative_uniformity', 'subgroup_uniformity'];
const kSpecDiagnosticSeverities = ['off', 'info', 'warning', 'error'];
const kDiagnosticTypes = ['attribute', 'directive'];

const kBadSeverities = ['none', 'warn', 'goose', 'fatal', 'severe'];
const kBadSingleTokenRules = ['unknown', 'blahblahblah', 'derivative_uniform'];

function generateDiagnostic(type: string, severity: string, rule: string): string {
  const diagnostic = `diagnostic(${severity}, ${rule})`;
  if (type === 'directive') {
    return diagnostic;
  } else {
    return '@' + diagnostic;
  }
}

const kValidLocations = {
  module: (diag: string) => `${diag};`,
  function: (diag: string) => `${diag} fn foo() { }`,
  compound: (diag: string) => `fn foo() { ${diag} { } }`,
  if_stmt: (diag: string) => `fn foo() { ${diag} if true { } }`,
  if_then: (diag: string) => `fn foo() { if true ${diag} { } }`,
  if_else: (diag: string) => `fn foo() { if true { } else ${diag} { } }`,
  switch_stmt: (diag: string) => `fn foo() { ${diag} switch 0 { default { } } }`,
  switch_body: (diag: string) => `fn foo() { switch 0 ${diag} { default { } } }`,
  switch_default: (diag: string) => `fn foo() { switch 0 { default ${diag} { } } }`,
  switch_case: (diag: string) => `fn foo() { switch 0 { case 0 ${diag} { } default { } } }`,
  loop_stmt: (diag: string) => `fn foo() { ${diag} loop { break; } }`,
  loop_body: (diag: string) => `fn foo() { loop ${diag} { break; } }`,
  loop_continuing: (diag: string) => `fn foo() { loop { continuing ${diag} { break if true; } } }`,
  while_stmt: (diag: string) => `fn foo() { ${diag} while true { break; } }`,
  while_body: (diag: string) => `fn foo() { while true ${diag} { break; } }`,
  for_stmt: (diag: string) => `fn foo() { ${diag} for (var i = 0; i < 10; i++) { } }`,
  for_body: (diag: string) => `fn foo() { for (var i = 0; i < 10; i++) ${diag} { } }`,
};

const kInvalidLocations = {
  module_var: (diag: string) => `${diag} var<private> x : u32;`,
  module_const: (diag: string) => `${diag} const x = 0;`,
  module_override: (diag: string) => `${diag} override x : u32;`,
  struct: (diag: string) => `${diag} struct S { x : u32 }`,
  struct_member: (diag: string) => ` struct S { ${diag} x : u32 }`,
  function_params: (diag: string) => `fn foo${diag}() { }`,
  function_var: (diag: string) => `fn foo() { ${diag} var x = 0; }`,
  function_let: (diag: string) => `fn foo() { ${diag} let x = 0; }`,
  function_const: (diag: string) => `fn foo() { ${diag} const x = 0; }`,
  pre_else: (diag: string) => `fn foo() { if true { } ${diag} else { } }`,
  pre_default: (diag: string) => `fn foo() { switch 0 { ${diag} default { } } }`,
  pre_case: (diag: string) => `fn foo() { switch 0 { ${diag} case 0 { } default { } } }`,
  pre_continuing: (diag: string) => `fn foo() { loop { ${diag} continuing { break if true; } } }`,
  pre_for_params: (diag: string) => `fn foo() { for ${diag} (var i = 0; i < 10; i++) { } }`,
};

const kNestedLocations = {
  compound: (d1: string, d2: string) => `${d1} fn foo() { ${d2} { } }`,
  if_stmt: (d1: string, d2: string) => `fn foo() { ${d1} if true ${d2} { } }`,
  switch_stmt: (d1: string, d2: string) => `fn foo() { ${d1} switch 0 ${d2} { default { } } }`,
  switch_body: (d1: string, d2: string) => `fn foo() { switch 0 ${d1} { default ${d2} { } } }`,
  switch_case: (d1: string, d2: string) =>
    `fn foo() { switch 0 { case 0 ${d1} { } default ${d2} { } } }`,
  loop_stmt: (d1: string, d2: string) => `fn foo() { ${d1} loop ${d2} { break; } }`,
  while_stmt: (d1: string, d2: string) => `fn foo() { ${d1} while true ${d2} { break; } }`,
  for_stmt: (d1: string, d2: string) => `fn foo() { ${d1} for (var i = 0; i < 10; i++) ${d2} { } }`,
};

g.test('valid_params')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests required accepted diagnostic parameters`)
  .params(u =>
    u
      .combine('severity', kSpecDiagnosticSeverities)
      .combine('rule', kSpecDiagnosticRules)
      .combine('type', kDiagnosticTypes)
  )
  .fn(t => {
    const diag = generateDiagnostic(t.params.type, t.params.severity, t.params.rule);
    let code = ``;
    if (t.params.type === 'directive') {
      code = kValidLocations['module'](diag);
    } else {
      code = kValidLocations['function'](diag);
    }
    t.expectCompileResult(true, code);
  });

g.test('invalid_severity')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests invalid severities are rejected`)
  .params(u => u.combine('severity', kBadSeverities).combine('type', kDiagnosticTypes))
  .fn(t => {
    const diag = generateDiagnostic(t.params.type, t.params.severity, 'derivative_uniformity');
    let code = ``;
    if (t.params.type === 'directive') {
      code = kValidLocations['module'](diag);
    } else {
      code = kValidLocations['function'](diag);
    }
    t.expectCompileResult(false, code);
  });

g.test('warning_unknown_rule')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests unknown single token rules issue a warning`)
  .params(u => u.combine('type', kDiagnosticTypes).combine('rule', kBadSingleTokenRules))
  .fn(t => {
    const diag = generateDiagnostic(t.params.type, 'info', t.params.rule);
    let code = ``;
    if (t.params.type === 'directive') {
      code = kValidLocations['module'](diag);
    } else {
      code = kValidLocations['function'](diag);
    }
    t.expectCompileWarning(true, code);
  });

g.test('valid_locations')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests valid locations`)
  .params(u =>
    u
      .combine('type', kDiagnosticTypes)
      .combine('location', keysOf(kValidLocations))
      .combine('rule', kSpecDiagnosticRules)
  )
  .fn(t => {
    const diag = generateDiagnostic(t.params.type, 'info', t.params.rule);
    const code = kValidLocations[t.params.location](diag);
    let res = true;
    if (t.params.type === 'directive') {
      res = t.params.location === 'module';
    } else {
      res = t.params.location !== 'module';
    }
    if (res === false) {
      t.expectCompileResult(true, kValidLocations[t.params.location](''));
    }
    t.expectCompileResult(res, code);
  });

g.test('invalid_locations')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests invalid locations`)
  .params(u =>
    u
      .combine('type', kDiagnosticTypes)
      .combine('location', keysOf(kInvalidLocations))
      .combine('rule', kSpecDiagnosticRules)
  )
  .fn(t => {
    const diag = generateDiagnostic(t.params.type, 'info', t.params.rule);
    t.expectCompileResult(true, kInvalidLocations[t.params.location](''));
    t.expectCompileResult(false, kInvalidLocations[t.params.location](diag));
  });

g.test('conflicting_directive')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests conflicts between directives`)
  .params(u => u.combine('s1', kSpecDiagnosticSeverities).combine('s2', kSpecDiagnosticSeverities))
  .fn(t => {
    const d1 = generateDiagnostic('directive', t.params.s1, 'derivative_uniformity');
    const d2 = generateDiagnostic('directive', t.params.s2, 'derivative_uniformity');
    const code = `${kValidLocations['module'](d1)}\n${kValidLocations['module'](d2)}`;
    t.expectCompileResult(t.params.s1 === t.params.s2, code);
  });

g.test('duplicate_attribute_same_location')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests duplicate diagnostics at the same location must be on different rules`)
  .params(u =>
    u
      .combine('loc', keysOf(kValidLocations))
      .combine('same_rule', [true, false] as const)
      .beginSubcases()
      .combine('s1', kSpecDiagnosticSeverities)
      .combine('s2', kSpecDiagnosticSeverities)
      .filter(u => {
        return u.loc !== 'module';
      })
  )
  .fn(t => {
    const rule1 = 'derivative_uniformity';
    const rule2 = 'another_diagnostic_rule';
    const d1 = generateDiagnostic('attribute', t.params.s1, rule1);
    const d2 = generateDiagnostic('attribute', t.params.s2, t.params.same_rule ? rule1 : rule2);
    const code = `${kValidLocations[t.params.loc](`${d1} ${d2}`)}`;
    t.expectCompileResult(!t.params.same_rule, code);
  });

g.test('conflicting_attribute_different_location')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests conflicts between attributes`)
  .params(u =>
    u
      .combine('loc', keysOf(kNestedLocations))
      .combine('s1', kSpecDiagnosticSeverities)
      .combine('s2', kSpecDiagnosticSeverities)
      .filter(u => {
        return u.s1 !== u.s2;
      })
  )
  .fn(t => {
    const d1 = generateDiagnostic('attribute', t.params.s1, 'derivative_uniformity');
    const d2 = generateDiagnostic('attribute', t.params.s2, 'derivative_uniformity');
    const code = `${kNestedLocations[t.params.loc](d1, d2)}`;
    t.expectCompileResult(true, code);
  });

g.test('after_other_directives')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc(`Tests other global directives before a diagnostic directive.`)
  .params(u =>
    u.combine('directive', ['enable f16', 'requires readonly_and_readwrite_storage_textures'])
  )
  .fn(t => {
    if (t.params.directive.startsWith('requires')) {
      t.skipIfLanguageFeatureNotSupported('readonly_and_readwrite_storage_textures');
    }

    let code = `${t.params.directive};`;
    code += generateDiagnostic('directive', 'info', 'derivative_uniformity') + ';';
    t.expectCompileResult(true, code);
  });

interface ScopeCase {
  code: string;
  result: boolean | 'warn';
}

function scopeCode(body: string): string {
  return `
@group(0) @binding(0) var t : texture_1d<f32>;
@group(0) @binding(1) var s : sampler;
var<private> non_uniform_cond : bool;
var<private> non_uniform_coord : f32;
var<private> non_uniform_val : u32;
@fragment fn main() {
  ${body}
}
`;
}

const kScopeCases: Record<string, ScopeCase> = {
  override_global_off: {
    code: `
    ${generateDiagnostic('directive', 'error', 'derivative_uniformity')};
    ${scopeCode(`
      ${generateDiagnostic('', 'off', 'derivative_uniformity')}
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }`)};
    `,
    result: true,
  },
  override_global_on: {
    code: `
    ${generateDiagnostic('directive', 'off', 'derivative_uniformity')};
    ${scopeCode(`
      ${generateDiagnostic('', 'error', 'derivative_uniformity')}
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }`)}
    `,
    result: false,
  },
  override_global_warn: {
    code: `
    ${generateDiagnostic('directive', 'error', 'derivative_uniformity')};
    ${scopeCode(`
      ${generateDiagnostic('', 'warning', 'derivative_uniformity')}
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }`)}
    `,
    result: 'warn',
  },
  global_if_nothing_else_warn: {
    code: `
    ${generateDiagnostic('directive', 'warning', 'derivative_uniformity')};
    ${scopeCode(`
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }`)}
    `,
    result: 'warn',
  },
  deepest_nesting_warn: {
    code: scopeCode(`
      ${generateDiagnostic('', 'error', 'derivative_uniformity')}
      if non_uniform_cond {
        ${generateDiagnostic('', 'warning', 'derivative_uniformity')}
        if non_uniform_cond {
          _ = textureSample(t,s,0.0);
        }
      }`),
    result: 'warn',
  },
  deepest_nesting_off: {
    code: scopeCode(`
      ${generateDiagnostic('', 'error', 'derivative_uniformity')}
      if non_uniform_cond {
        ${generateDiagnostic('', 'off', 'derivative_uniformity')}
        if non_uniform_cond {
          _ = textureSample(t,s,0.0);
        }
      }`),
    result: true,
  },
  deepest_nesting_error: {
    code: scopeCode(`
      ${generateDiagnostic('', 'off', 'derivative_uniformity')}
      if non_uniform_cond {
        ${generateDiagnostic('', 'error', 'derivative_uniformity')}
        if non_uniform_cond {
          _ = textureSample(t,s,0.0);
        }
      }`),
    result: false,
  },
  other_nest_unaffected: {
    code: `
    ${generateDiagnostic('directive', 'warning', 'derivative_uniformity')};
    ${scopeCode(`
      ${generateDiagnostic('', 'off', 'derivative_uniformity')}
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }
      if non_uniform_cond {
        _ = textureSample(t,s,0.0);
      }`)}
    `,
    result: 'warn',
  },
  deeper_nest_no_effect: {
    code: `
    ${generateDiagnostic('directive', 'error', 'derivative_uniformity')};
    ${scopeCode(`
      if non_uniform_cond {
        ${generateDiagnostic('', 'off', 'derivative_uniformity')}
        if non_uniform_cond {
        }
        _ = textureSample(t,s,0.0);
      }`)}
    `,
    result: false,
  },
  call_unaffected_error: {
    code: `
    ${generateDiagnostic('directive', 'error', 'derivative_uniformity')};
    fn foo() { _ = textureSample(t,s,0.0); }
    ${scopeCode(`
      ${generateDiagnostic('', 'off', 'derivative_uniformity')}
      if non_uniform_cond {
        foo();
      }`)}
    `,
    result: false,
  },
  call_unaffected_warn: {
    code: `
    ${generateDiagnostic('directive', 'warning', 'derivative_uniformity')};
    fn foo() { _ = textureSample(t,s,0.0); }
    ${scopeCode(`
      ${generateDiagnostic('', 'off', 'derivative_uniformity')}
      if non_uniform_cond {
        foo();
      }`)}
    `,
    result: 'warn',
  },
  call_unaffected_off: {
    code: `
    ${generateDiagnostic('directive', 'off', 'derivative_uniformity')};
    fn foo() { _ = textureSample(t,s,0.0); }
    ${scopeCode(`
      ${generateDiagnostic('', 'error', 'derivative_uniformity')}
      if non_uniform_cond {
        foo();
      }`)}
    `,
    result: true,
  },
  if_condition_error: {
    code: scopeCode(`
      if (non_uniform_cond) {
        ${generateDiagnostic('', 'error', 'derivative_uniformity')}
        if textureSample(t,s,non_uniform_coord).x > 0.0
          ${generateDiagnostic('', 'off', 'derivative_uniformity')} {
        }
      }`),
    result: false,
  },
  if_condition_warn: {
    code: scopeCode(`
      if non_uniform_cond {
        ${generateDiagnostic('', 'warning', 'derivative_uniformity')}
        if textureSample(t,s,non_uniform_coord).x > 0.0
          ${generateDiagnostic('', 'error', 'derivative_uniformity')} {
        }
      }`),
    result: 'warn',
  },
  if_condition_off: {
    code: scopeCode(`
      if non_uniform_cond {
        ${generateDiagnostic('', 'off', 'derivative_uniformity')}
        if textureSample(t,s,non_uniform_coord).x > 0.0
          ${generateDiagnostic('', 'error', 'derivative_uniformity')} {
        }
      }`),
    result: true,
  },
  switch_error: {
    code: scopeCode(`
        ${generateDiagnostic('', 'error', 'derivative_uniformity')}
        switch non_uniform_val {
          case 0 ${generateDiagnostic('', 'off', 'derivative_uniformity')} {
          }
          default {
            _ = textureSample(t,s,0.0);
          }
        }`),
    result: false,
  },
  switch_warn: {
    code: scopeCode(`
        ${generateDiagnostic('', 'warning', 'derivative_uniformity')}
        switch non_uniform_val {
          case 0 ${generateDiagnostic('', 'off', 'derivative_uniformity')} {
          }
          default {
            _ = textureSample(t,s,0.0);
          }
        }`),
    result: 'warn',
  },
  switch_off: {
    code: scopeCode(`
        ${generateDiagnostic('', 'off', 'derivative_uniformity')}
        switch non_uniform_val {
          case 0 ${generateDiagnostic('', 'error', 'derivative_uniformity')}{
          }
          default {
            _ = textureSample(t,s,0.0);
          }
        }`),
    result: true,
  },
};

g.test('diagnostic_scoping')
  .specURL('https://gpuweb.github.io/gpuweb/wgsl/#diagnostics')
  .desc('Tests that innermost scope controls the diagnostic')
  .params(u => u.combine('case', keysOf(kScopeCases)))
  .fn(t => {
    const testcase = kScopeCases[t.params.case];
    if (testcase.result === 'warn') {
      t.expectCompileWarning(true, testcase.code);
    } else {
      t.expectCompileResult(testcase.result as boolean, testcase.code);
    }
  });
