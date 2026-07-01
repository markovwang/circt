# SystemVerilog Constraint Solving

This note records follow-up work for SystemVerilog class constraint support in
the Verilog front-end and the arcilator-oriented runtime path. The current
implementation imports class constraint blocks into Moore IR, preserves simple
predicate bodies, imports basic `randomize()` calls, and lowers a small
constraint subset to runtime Bitwuzla wrapper calls.

## Current Milestone

The importer accepts class constraint declarations and represents them as
`moore.class.constraintdecl` operations inside the owning class declaration.
Simple expression constraints and lists are preserved as predicate regions.
Unsupported constraint forms are diagnosed instead of being accepted silently.

This is no longer only an IR-preservation step, but it is still not full
SystemVerilog randomization semantics. Model writeback, runtime mode handling,
seed/replay, inline `with` constraints, and most constraint features remain
future work.

## Implementation Progress

The current branch has completed the first arcilator-oriented skeleton for class
constraint solving:

- Moore preserves `rand` and `randc` class property metadata.
- ImportVerilog preserves simple class constraint blocks and imports simple
  object `randomize()` calls.
- Arc runtime exposes an optional Bitwuzla-backed C ABI.
- MooreToCore creates hidden `rand_mode` and `constraint_mode` storage fields.
- MooreToCore emits generated runtime helpers that build solver calls for a
  small constraint subset.
- 1-dim unpacked arrays are represented as one solver bit-vector variable per
  element.
- A generated predicate helper crosschecks solver model values before the
  randomize helper reports success.
- Scalar integral `rand` field model values are committed back into object
  storage after crosscheck succeeds.
- Randomize helpers destroy the solver on success and failure paths.
- A minimal SV compile-flow test now checks `circt-verilog --ir-moore` and the
  `circt-verilog --ir-moore | circt-opt --convert-moore-to-core` lowering path.
- The Bitwuzla-enabled runtime/tool build has been verified with the existing
  `build-bitwuzla` cache using `ninja -C build-bitwuzla -j12
  CIRCTArcRuntime CIRCTArcJITRuntime circt-opt`. That cache currently resolves
  Bitwuzla from `/home/markov/Project/bitwuzla`; fresh builds should prefer the
  installed `/usr/local` prefix.
- `build-bitwuzla/bin/arcilator` has been built with
  `ninja -C build-bitwuzla -j14 arcilator`.
- A first arcilator execution-path spike showed that an SV `initial`-based
  input is the wrong shape for this feature: arcilator should not grow generic
  `initial` / `llhd.process` support for class randomization.
- The current executable demo path is AOT-first: a normal `func.func @main`
  harness uses `moore.class.new` and `moore.class.randomize`, MooreToCore lowers
  that into the generated `__circt_randomize_Packet` helper, arcilator emits
  LLVM IR, and `clang++`/lld links the executable with `CIRCTArcRuntime` and
  Bitwuzla. It does not rely on SV `initial`, generic `llhd.process` lowering,
  JIT, or a hand-copied randomize helper.

This is still a skeleton for runtime solving. The major missing semantic pieces
are aggregate writeback and runtime mode behavior: 1-dim unpacked array values
are solved and crosschecked, but not written back yet; runtime reads of
`rand_mode` and `constraint_mode` are also not wired yet.

## Commit Map

- `1c86a06ee` `[Arc] Add optional constraint solver runtime hook`
  - Adds `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE`.
  - Adds `ConstraintSolver.h` and disabled fallback runtime entry points.
  - Wires `ConstraintSolver.cpp` into Arc runtime builds.
- `027016cba` `[Arc] Implement Bitwuzla constraint solver wrapper`
  - Implements the enabled Bitwuzla-backed runtime wrapper.
  - Adds API coverage for variables, constants, equality, signed greater-than,
    assertions, solver checks, and model extraction.
  - Adds Arc runtime unit coverage for availability.
- `0140f2bc6` `[Moore] Preserve class rand property metadata`
  - Adds `isRand` and `isRandC` metadata to Moore class property declarations.
  - Teaches ImportVerilog to preserve `rand`/`randc` on class properties.
  - Extends ImportVerilog constraint tests.
- `9229b2bc2` `[MooreToCore] Prepare class randomize lowering`
  - Adds `LowerClassRandomize.cpp`.
  - Collects local class randomize descriptors.
  - Inserts hidden mode fields for rand fields and constraint blocks.
  - Wires the preparation step into `--convert-moore-to-core`.
- `bb98d4a2a` `[MooreToCore] Lower randomize constraints to solver calls`
  - Emits private `__circt_randomize_<Class>` helpers.
  - Declares solver runtime functions in generated IR.
  - Lowers constants, class property reads, equality, and signed
    greater-than to solver calls.
  - Adds unsupported-operation diagnostics.
- `050beaf8b` `[MooreToCore] Crosscheck randomize solver models`
  - Adds one solver variable per 1-dim unpacked array element.
  - Rejects variable-index array access in constraints.
  - Generates `__circt_randomize_check_<Class>` predicate helpers.
  - Calls the predicate helper after solver SAT to validate model values.
- `47aa4852b` `[ImportVerilog] Import class randomize calls`
  - Adds `moore.class.randomize`.
  - Imports simple object `randomize()` calls from SystemVerilog.
  - Lowers `moore.class.randomize` to the generated randomize helper.
  - Adds ImportVerilog and MooreToCore randomize-call tests.
- `c6c5566fd` `[MooreToCore] Commit scalar randomize models`
  - Writes scalar integral `rand` model values back into class object storage
    after solver SAT and predicate crosscheck.
  - Adds solver cleanup with `arcRuntimeSolverDestroy` on success and failure
    paths.
  - Extends the randomize-call test to check model extraction, field store, and
    solver destruction.
- `477086351` `[ImportVerilog] Test randomize compile flow`
  - Adds a minimal SV-to-Moore-to-Core randomize flow test.
  - Checks frontend import of `moore.class.randomize` and lowering to solver
    calls, model extraction, scalar writeback, and solver cleanup.
  - Narrows the old frontend "randomization not supported" remark for supported
    builtin class methods.
- `e05e4bb01` `[Docs] Plan arcilator randomize executable demo`
  - Adds the AOT-first execution plan.
  - Records that SV `initial` and generic `llhd.process` lowering are explicit
    non-goals for this milestone.
- `ca0417de1` `[test] Expose Bitwuzla randomize link flags to lit`
  - Adds the `bitwuzla-randomize` lit feature and executable-link
    substitutions.
- `286c93c33` `[arcilator] Test AOT constraint solver runtime calls`
  - Adds a native executable smoke test for direct `arcRuntimeSolver*` calls.
- `47f8608b4` `[arcilator] Add AOT randomize helper execution demo`
  - Adds a non-`initial` `func.func @main` harness that calls
    `__circt_randomize_Packet`.
  - Links the emitted LLVM IR into an executable and verifies solver writeback.
- `9dff107da` `[Docs] Plan regular randomize AOT lowering`
  - Records the shorter plan: keep the executable harness in the test input and
    avoid demo-only MooreToCore entry-generation options.
- `f016cf379` `[arcilator] Add randomize lowering AOT demo`
  - Adds `test/arcilator/randomize-lowering-aot.mlir`, which starts from Moore
    IR with a normal `func.func @main`, runs MooreToCore, emits LLVM through
    arcilator, links with `CIRCTArcRuntime` and Bitwuzla, and runs the native
    executable.

## Known Follow-Up Gaps

Before building solver behavior on top of the first milestone, keep the
remaining IR preservation boundaries explicit:

- Done: constraint body symbol visibility. Constraint bodies sit in their own
  nested operation, but symbol users inside the body must still resolve
  class-level and module-level symbols, including static class properties,
  inherited properties, globals, and helper ops such as class upcasts.
- Open: extern constraint definitions. Out-of-block definitions such as
  `constraint Packet::c_len { ... }` currently diagnose as unsupported. A future
  preservation milestone may attach the external body to the class constraint
  declaration instead.

These are preservation problems, not solver problems. Unsupported preservation
cases should continue to fail with precise diagnostics rather than silently
degrading into incomplete IR.

## Solver Scope

A real solver should model the SystemVerilog constraint system rather than
lowering constraints directly to ordinary procedural code. At minimum it should
handle:

- `rand` and `randc` class properties.
- Constraint enablement through `constraint_mode`.
- Object randomization through `randomize()`.
- Inline `with` constraints.
- Constraint inheritance and override specifiers.
- `solve before` ordering.
- `soft` constraints and `disable soft`.
- `dist` weights.
- `foreach`, implication, conditional, and uniqueness constraints.
- Deterministic seeding and reproducibility.

These features interact, so they should be designed together before extending
the first milestone beyond simple predicate preservation.

## Suggested Architecture

Keep parsed constraints in Moore IR as the source-level representation, then add
a separate lowering or analysis step that extracts a solver problem from a class
type and a specific `randomize()` call site.

The actual solving path should be SMT-backed, targeting bit-vector solvers such
as Bitwuzla rather than a hand-written enumerator as the long-term engine.
SystemVerilog features that are not native SMT theory, such as `solve before`
ordering and `dist` weights, should be represented explicitly and implemented
through deterministic problem construction, solver queries, and post-processing
layers around the SMT backend.

The solver-facing representation should separate:

- Random variables and their domains.
- Hard constraints.
- Soft constraints and their disable rules.
- Solve ordering constraints.
- Distribution weights.
- Inheritance and override metadata.
- Runtime inputs, including existing object field values and inline `with`
  constraints.

The lowering from Moore IR to this representation should be explicit and
testable. Avoid embedding solver assumptions in the importer; the importer
should continue to preserve source structure and report unsupported source
forms precisely.

Backend integration should initially target arcilator and simulation
verification flows. Avoid making the second lowering path broader than needed:
trying to serve every possible backend will introduce design trade-offs that
are not justified by the current use case.

## Implemented Randomize Subset

- Backend target: arcilator-style simulation runtime helpers.
- Solver path: optional Bitwuzla-backed Arc runtime wrapper.
- Imported call form: simple object `randomize()` calls without inline `with`
  constraints.
- Supported rand fields: integral scalar `rand` fields and 1-dim unpacked
  arrays of integral elements.
- Array lowering: one solver bit-vector variable per unpacked array element.
- Supported constraint expressions: constants, class property reads, constant
  array element reads, equality, and signed greater-than.
- Result validation: generated predicate helpers re-evaluate lowered
  constraints over solver model values before the helper reports success.
- Model commit: scalar integral `rand` fields are written back to object
  storage after crosscheck succeeds. 1-dim unpacked array model values are not
  written back yet.
- Runtime mode storage: hidden `rand_mode` and `constraint_mode` fields are
  created in class storage, but runtime enable/disable behavior is not wired
  yet.
- Build verification: the enabled Bitwuzla path builds for Arc runtime
  libraries and `circt-opt`. The verified `build-bitwuzla` cache uses the local
  Bitwuzla checkout; the project also has Bitwuzla installed under
  `/usr/local` for fresh configurations.
- Arcilator AOT status: `build-bitwuzla/bin/arcilator` builds, the solver
  runtime can be called from an emitted LLVM executable, and the regular
  MooreToCore path can lower `moore.class.new` plus `moore.class.randomize` from
  a non-`initial` `func.func @main` harness into a native executable.

Unsupported forms should diagnose instead of being silently ignored. The current
path is enough for a scalar-rand demo skeleton, but it is not a complete
SystemVerilog `randomize()` implementation.

## Audit TODO List

Track follow-up work in this order:

1. Stabilize remaining IR preservation boundaries.
   - Keep extern constraint definitions diagnosed as unsupported until there is
     a body-attachment strategy.
   - Add negative tests for every newly encountered unsupported constraint form.
2. Add a solver problem representation independent of arcilator or any backend
   runtime.
   - Represent random variables, finite domains, hard predicates, object inputs,
     solve results, and failure.
   - Keep the representation SMT-oriented so the supported subset lowers cleanly
     to bit-vector solver queries.
   - Do not embed solver assumptions in the Verilog importer.
3. Lower a minimal Moore constraint subset into the solver problem.
   - Start with integer `rand` fields, class property reads, inherited property
     reads, literals, comparisons, equality, inequality, and boolean `and`.
   - Reject unsupported Moore operations with diagnostics.
4. Implement a deterministic minimal SMT solver backend.
   - Start with a Bitwuzla-class bit-vector backend for simple finite integer
     domains.
   - Cover satisfiable and unsatisfiable problems.
5. Wire `randomize()` for one object instance.
   - Build a problem from the object class and active constraints.
   - Return success or failure explicitly.
   - Define and test object-state behavior on solve failure.
6. Add deterministic seed and replay behavior.
   - Repeated runs with the same seed should produce the same result.
   - Tests should check invariants and replay, not one arbitrary random value.
7. Add inline `with` constraints.
   - Treat them as call-site runtime inputs to problem construction.
   - Keep unsupported inline forms diagnostic.
8. Add `constraint_mode`.
   - Model runtime constraint enablement before expanding more constraint
     syntax.
9. Expand SystemVerilog constraint semantics incrementally.
   - Add inheritance override rules, `solve before`, `soft`, `disable soft`,
     `dist`, implication, conditionals, `foreach`, `unique`, `randc`, and wider
     aggregate cases one feature at a time.
   - Implement non-SMT-native features, such as `solve before` and `dist`,
     through explicit extensions around SMT problem construction and result
     selection.
10. Integrate with arcilator-oriented simulation flows.
    - Prefer designs that serve arcilator and simulation verification directly.
    - Choose whether arcilator interprets solver IR directly, lowers to runtime
      helpers, or calls a solver backend.
    - Avoid broad generic backend commitments unless a concrete use case
      justifies the trade-off.

Each stage should include positive tests for supported behavior and negative
tests for unsupported SystemVerilog features so unsupported forms do not
silently degrade into incomplete behavior.

## Audit TODO List (Chinese)

求解路径应以 SMT solver 为基础，目标是 Bitwuzla 这类 bit-vector solver，
而不是长期依赖手写枚举器。`solve before`、`dist` 等 SMT theory 不原生
支持的 SystemVerilog 语义，应在 SMT problem construction、solver query
和 result selection 周围通过显式扩展层实现。

再次 lowering 的后端集成应优先面向 arcilator 和仿真验证流程。不要一开始
把目标扩展成通用后端方案；做大做全会引入不必要的设计 trade-off。

后续 audit 建议按下面顺序推进：

1. 稳定剩余 IR preservation 边界。
   - 在有明确 body-attachment 策略之前，继续把 extern constraint
     definitions 诊断为 unsupported。
   - 每遇到一个新的 unsupported constraint form，都补对应负向测试。
2. 添加独立于 arcilator 和任何后端 runtime 的 solver problem 表示。
   - 表达随机变量、有限 domain、hard predicate、对象输入、求解结果和失败。
   - 让该表示面向 SMT，保证已支持子集可以清晰 lowering 到 bit-vector
     solver query。
   - 不要把 solver 假设塞进 Verilog importer。
3. 把最小 Moore constraint 子集 lowering 到 solver problem。
   - 从 integer `rand` 字段、class property read、继承 property read、
     literal、比较、相等、不等和 boolean `and` 开始。
   - 对 unsupported Moore op 产生明确诊断。
4. 实现 deterministic minimal SMT solver backend。
   - 先使用 Bitwuzla 这类 bit-vector backend 覆盖简单有限整数 domain。
   - 覆盖 satisfiable 和 unsatisfiable 两类问题。
5. 接入单个 object instance 的 `randomize()`。
   - 从对象 class 和 active constraints 构造 problem。
   - 明确返回 success 或 failure。
   - 定义并测试 solve failure 时的 object state 行为。
6. 添加 deterministic seed / replay 行为。
   - 同一个 seed 重复运行应得到相同结果。
   - 测试应检查 invariant 和 replay，不要依赖某一个任意随机值。
7. 添加 inline `with` constraints。
   - 把它们作为 call-site runtime inputs 参与 problem construction。
   - 对 unsupported inline form 保持明确诊断。
8. 添加 `constraint_mode`。
   - 在扩展更多 constraint syntax 之前，先建模 runtime constraint
     enablement。
9. 逐步扩展 SystemVerilog constraint semantics。
   - 按功能逐项添加 inheritance override rules、`solve before`、`soft`、
     `disable soft`、`dist`、implication、conditional、`foreach`、`unique`、
     `randc` 和更宽的 aggregate case。
   - 对 `solve before`、`dist` 这类 SMT theory 不原生支持的语义，通过 SMT
     problem construction 和 result selection 周围的显式扩展层实现。
10. 集成到面向 arcilator 的仿真验证流程。
    - 优先服务 arcilator 和 simulation verification。
    - 选择 arcilator 直接解释 solver IR、lower 到 runtime helper，或调用
      solver backend。
    - 除非有明确用例，否则避免承诺宽泛的通用后端设计。

每个阶段都应包含正向测试和负向测试，确保 supported behavior 稳定，同时
unsupported SystemVerilog feature 不会静默退化成不完整 IR。

## Testing Expectations

Solver work should include both import tests and behavioral tests. Useful
coverage includes:

- Generated Moore IR for constraint declarations remains stable.
- Unsupported constraint kinds produce specific diagnostics.
- `randomize()` imports to Moore IR and lowers to the generated helper for the
  currently supported subset.
- Once `randomize()` is implemented, repeated runs with the same seed are
  deterministic.
- Failed randomization leaves object state consistent with SystemVerilog
  semantics.
- Inherited constraints and inline constraints compose in the expected order.

Behavioral tests should avoid relying on one arbitrary random value. Prefer
checking invariants over many seeds, deterministic seed replay, and failure
cases where the constraint system is unsatisfiable.
