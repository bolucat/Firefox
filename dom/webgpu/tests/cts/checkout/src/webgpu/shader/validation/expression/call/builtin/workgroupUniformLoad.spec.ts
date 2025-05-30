export const description = `
Validation tests for the workgroupUniformLoad() builtin.
`;

import { makeTestGroup } from '../../../../../../common/framework/test_group.js';
import { keysOf } from '../../../../../../common/util/data_tables.js';
import { ShaderValidationTest } from '../../../shader_validation_test.js';

export const g = makeTestGroup(ShaderValidationTest);

const kEntryPoints = {
  none: { supportsBarrier: true, code: `` },
  compute: {
    supportsBarrier: true,
    code: `@compute @workgroup_size(1)
fn main() {
  foo();
}`,
  },
  vertex: {
    supportsBarrier: false,
    code: `@vertex
fn main() -> @builtin(position) vec4f {
  foo();
  return vec4f();
}`,
  },
  fragment: {
    supportsBarrier: false,
    code: `@fragment
fn main() {
  foo();
}`,
  },
  compute_and_fragment: {
    supportsBarrier: false,
    code: `@compute @workgroup_size(1)
fn main1() {
  foo();
}

@fragment
fn main2() {
  foo();
}
`,
  },
  fragment_without_call: {
    supportsBarrier: true,
    code: `@fragment
fn main() {
}
`,
  },
};

g.test('only_in_compute')
  .specURL('https://www.w3.org/TR/WGSL/#sync-builtin-functions')
  .desc(
    `
Synchronization functions must only be used in the compute shader stage.
`
  )
  .params(u =>
    u
      .combine('entry_point', keysOf(kEntryPoints))
      .combine('call', ['bar()', 'workgroupUniformLoad(&wgvar)'])
  )
  .fn(t => {
    const config = kEntryPoints[t.params.entry_point];
    const code = `
${config.code}

var<workgroup> wgvar : u32;

fn bar() -> u32 {
  return 0;
}

fn foo() {
  _ = ${t.params.call};
}`;
    t.expectCompileResult(t.params.call === 'bar()' || config.supportsBarrier, code);
  });

// A list of types that contains atomics, with a single control case.
const kAtomicTypes: string[] = [
  'bool', // control case
  'array<atomic<i32>, 4>',
  'AtomicStruct',
];

g.test('no_atomics')
  .desc(
    `
The argument passed to workgroupUniformLoad cannot contain any atomic types.

NOTE: Various other valid types are tested via execution tests, so we only check for invalid types here.
`
  )
  .params(u =>
    u.combine('type', kAtomicTypes).combine('call', ['bar()', 'workgroupUniformLoad(&wgvar)'])
  )
  .fn(t => {
    const code = `
struct AtomicStruct {
  a : atomic<u32>
}

var<workgroup> wgvar : ${t.params.type};

fn bar() -> bool {
  return true;
}

fn foo() {
  _ = ${t.params.call};
}`;
    t.expectCompileResult(t.params.type === 'bool' || t.params.call === 'bar()', code);
  });

g.test('param_constructible_only')
  .desc(
    `
The type of the argument passed to workgroupUniformLoad must be constructible.
`
  )
  .params(u => u.combine('stage', ['const', 'override']))
  .fn(t => {
    const code = `
${t.params.stage} array_size = 10u;
var<workgroup> wgvar : array<u32, array_size>;

fn foo() {
  _ = workgroupUniformLoad(&wgvar)[0];
}`;
    t.expectCompileResult(t.params.stage === 'const', code);
  });

g.test('must_use')
  .desc('Tests that the result must be used')
  .params(u => u.combine('use', [true, false] as const))
  .fn(t => {
    const code = `
    var<workgroup> v : u32;
    fn foo() {
      ${t.params.use ? '_ =' : ''} workgroupUniformLoad(&v);
    }`;
    t.expectCompileResult(t.params.use, code);
  });
