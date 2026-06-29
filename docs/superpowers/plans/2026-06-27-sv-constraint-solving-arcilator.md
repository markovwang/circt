# SV Constraint Solving for Arcilator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an arcilator-oriented, Bitwuzla-backed runtime path for SystemVerilog class `randomize()` over integral scalar `rand` fields and 1-dim unpacked arrays of integral elements.

**Architecture:** Moore continues to preserve static class constraint structure. The arcilator path generates runtime helper calls that read object state, `rand_mode`, `constraint_mode`, inline constraints, and seed state, then build one Bitwuzla query at runtime. Bitwuzla is optional: normal arcilator builds must not require it unless class randomization support is enabled.

**Tech Stack:** CIRCT Moore dialect, Arc/arcilator runtime, LLVM dialect lowering, CMake optional dependency handling, Bitwuzla C API, lit/FileCheck tests.

## Implementation Status

Status as of commit `477086351`:

- Task 1 complete: optional Arc runtime solver hook.
- Task 2 complete: Bitwuzla-backed runtime wrapper.
- Task 3 complete: Moore/ImportVerilog preserve `rand` and `randc` property metadata.
- Task 4 complete: MooreToCore class randomize descriptor collection and hidden mode field skeleton.
- Task 5 complete: scalar constraint expression lowering to solver runtime calls.
- Task 6 complete: 1-dim unpacked array solver variables and generated predicate crosscheck helper.
- Task 7 complete for code/tests: simple object `randomize()` imports to Moore IR and lowers to the generated helper.
- Post-plan demo step complete: scalar integral `rand` model values are committed back into object storage after crosscheck succeeds, and solver cleanup is emitted on success/failure paths.
- Compile-flow demo complete: a minimal SV file now checks `circt-verilog --ir-moore` and `circt-verilog --ir-moore | circt-opt --convert-moore-to-core`.
- Bitwuzla-enabled build/link verification complete with the installed `/usr/local` Bitwuzla prefix: `ninja -C build-bitwuzla -j12 CIRCTArcRuntime CIRCTArcJITRuntime circt-opt` passed.

Remaining implementation gaps:

- 1-dim unpacked array model values are solved and crosschecked but not committed back into object fields yet.
- `rand_mode` and `constraint_mode` are stored but not read at runtime.
- Inline `with` constraints are rejected / unsupported.
- Deterministic seed/replay behavior is not implemented.
- Constraint expression support is still narrow: constants, class property reads, constant array element reads, equality, and signed greater-than.
- Unsupported SV constraint features must keep producing diagnostics instead of partial behavior.

## Next Work Order

This is the lean path to a minimal arcilator-oriented demo. Defer broad
expression coverage, inline `with`, and seed/replay until this path is working.

1. **Push the current branch state.**
   - The branch is currently ahead of `origin/codex-sv-class-constraints` by the compile-flow commit `477086351` plus any newer documentation-only progress commits.
   - Push before handing work to another agent so other agents can continue from the same state.

2. **Done: add a compile-flow demo from SV to runtime solver calls.**
   - Add or extend a lit test that runs `circt-verilog --ir-moore` and pipes into `circt-opt --convert-moore-to-core`.
   - Use a minimal class with one scalar integral `rand` field, one simple constraint such as `x > 0`, and one plain `obj.randomize()` call.
   - Check that ImportVerilog emits `moore.class.randomize`.
   - Check that MooreToCore emits `__circt_randomize_<Class>` with solver create, bit-vector variable creation, constraint assertion, solver check, model extraction, scalar writeback, and solver destroy.
   - Narrow the old "randomization not supported" diagnostic so this supported simple call does not produce misleading output.

3. **Done: verify the Bitwuzla-enabled runtime build/link path.**
   - Use the installed Bitwuzla prefix at `/usr/local`.
   - Build the minimal targets needed for `bin/circt-opt` and the Arc runtime.
   - Keep the local Bitwuzla path out of generic tests and CMake defaults.
   - Confirm the disabled fallback build still works.

4. **Spike the actual arcilator execution path.**
   - Try to feed the compile-flow demo output into the existing arcilator flow.
   - If it works, turn that command into the first execution demo.
   - If it does not work, document the concrete blocker and the smallest adapter or harness that would be needed.
   - Do not build a general backend abstraction for this spike.

5. **Commit 1-dim unpacked array model values back to object storage.**
   - Reuse the existing per-element solver variables and crosscheck path.
   - Extract each element model value and store it back into the corresponding object field element.
   - Keep variable-index array access unsupported.
   - Add FileCheck coverage for per-element model extraction and per-element store.

6. **Make `rand_mode` and `constraint_mode` affect solving.**
   - Skip assertion of constraint blocks whose `constraint_mode` is disabled.
   - Do not write back fields whose `rand_mode` is disabled.
   - For combinations that v1 cannot model correctly, emit an unsupported diagnostic rather than partial behavior.
   - Add tests for disabled constraint blocks, disabled rand fields, and unsupported mixed cases.

7. **Backlog after the minimal demo path works.**
   - Add expression operators only as needed by concrete tests: likely `!=`, `&&`, signed `< <= >=`, and simple `+` / `-`.
   - Add inline `with` constraints after the mode behavior is stable.
   - Add deterministic seed/replay after there is a real execution demo to replay.
   - Keep dynamic arrays, queues, associative arrays, multidim unpacked arrays, strings, reals, class handles, and object graph constraints unsupported for now.

## Commit Map

- `1c86a06ee` `[Arc] Add optional constraint solver runtime hook`
  - Adds the CMake option and disabled fallback runtime ABI.
  - Adds `ConstraintSolver.h` / `ConstraintSolver.cpp`.
  - Wires the source into Arc runtime builds.
- `027016cba` `[Arc] Implement Bitwuzla constraint solver wrapper`
  - Implements Bitwuzla variable, constant, comparison, assert/check, and model extraction wrappers.
  - Keeps the fallback path available when Bitwuzla support is disabled.
  - Adds Arc runtime unit coverage.
- `0140f2bc6` `[Moore] Preserve class rand property metadata`
  - Adds `isRand` / `isRandC` attributes to class properties.
  - Teaches ImportVerilog to preserve class property randomization metadata.
  - Updates import tests for constraint/rand metadata preservation.
- `9229b2bc2` `[MooreToCore] Prepare class randomize lowering`
  - Adds `LowerClassRandomize.cpp`.
  - Collects local descriptors for rand fields and constraint blocks.
  - Adds hidden `rand_mode` / `constraint_mode` storage fields.
  - Wires the preparation pass into `--convert-moore-to-core`.
- `bb98d4a2a` `[MooreToCore] Lower randomize constraints to solver calls`
  - Generates `__circt_randomize_<Class>` helper skeletons.
  - Declares solver runtime functions in generated IR.
  - Lowers scalar rand field reads, constants, equality, and signed greater-than to solver calls.
  - Adds positive and unsupported-op tests.
- `050beaf8b` `[MooreToCore] Crosscheck randomize solver models`
  - Adds per-element solver variables for supported 1-dim unpacked arrays.
  - Rejects variable-index rand array access.
  - Generates `__circt_randomize_check_<Class>`.
  - Calls the crosscheck helper after solver SAT.
- `47aa4852b` `[ImportVerilog] Import class randomize calls`
  - Adds `moore.class.randomize`.
  - Imports simple object `randomize()` calls without inline constraints.
  - Lowers `moore.class.randomize` to the generated helper.
  - Adds ImportVerilog and MooreToCore tests.
- `c6c5566fd` `[MooreToCore] Commit scalar randomize models`
  - Emits scalar integral rand-field writeback after solver SAT and predicate crosscheck.
  - Emits `arcRuntimeSolverDestroy` on success and failure paths.
  - Extends `randomize-call.mlir` to check model extraction, field store, and solver cleanup.
- `7a4033ef7` `[Docs] Update randomize writeback status`
  - Records scalar writeback as complete in the plan and user-facing constraint solving documentation.
  - Keeps array writeback, mode reads, inline constraints, seed/replay, and expression coverage as remaining work.
- `477086351` `[ImportVerilog] Test randomize compile flow`
  - Adds `test/Conversion/ImportVerilog/randomize-flow.sv`.
  - Checks that a minimal SV class `randomize()` call imports to `moore.class.randomize`.
  - Checks that the same SV source lowers through `--convert-moore-to-core` to solver create, variable creation, assertion, check, model extraction, scalar writeback, and solver cleanup.
  - Narrows the old frontend "not supported" remark so supported builtin class methods do not produce a misleading diagnostic.

## Global Constraints

- Runtime solver backend: Bitwuzla-class bit-vector SMT solver, called through C/C++ runtime APIs.
- Do not emit SMT-LIB text for this feature.
- Backend target: arcilator and simulation verification only.
- Bitwuzla dependency is optional and required only when class randomization support is enabled.
- First supported field shapes: integral scalar `rand` fields and 1-dim unpacked arrays of integral elements.
- 1-dim unpacked arrays lower to per-element bit-vector variables, not SMT array theory.
- First unsupported field shapes: dynamic arrays, queues, associative arrays, multidim unpacked arrays, strings, reals, class handles, object graphs, and variable-index array access.
- Runtime state controls randomization: `rand_mode`, `constraint_mode`, object field values, inline `with`, and seed/replay state.
- Every successful solver model is crosschecked by a generated runtime predicate evaluator before candidate values are committed to the object.
- Do not embed solver backend assumptions in the Verilog importer.

---

## File Structure

- Create `include/circt/Dialect/Arc/Runtime/ConstraintSolver.h`: small public runtime wrapper over Bitwuzla used by generated arcilator code.
- Create `lib/Dialect/Arc/Runtime/ConstraintSolver.cpp`: implementation of the wrapper, compiled only when Bitwuzla support is enabled.
- Modify `lib/Dialect/Arc/Runtime/CMakeLists.txt`: add optional runtime source, compile definition, and Bitwuzla link dependency.
- Modify top-level `CMakeLists.txt`: add `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE` and Bitwuzla discovery.
- Modify `lib/Conversion/ImportVerilog/Structure.cpp`: preserve enough metadata for `rand` field detection if existing property declarations do not retain it.
- Modify `include/circt/Dialect/Moore/MooreOps.td`: add minimal property/constraint attributes needed by extraction, such as `isRand`, `isRandC`, and stable constraint symbol names if missing.
- Modify `lib/Dialect/Moore/MooreOps.cpp`: verify any new attributes and keep class declaration invariants.
- Create `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`: local constraint descriptors, expression lowering, helper generation, and runtime `func.call`s before class objects are fully lowered.
- Modify `lib/Conversion/MooreToCore/CMakeLists.txt`: add the new helper source to the existing Moore-to-Core conversion library.
- Modify `lib/Conversion/MooreToCore/MooreToCore.cpp`: call into the helper when lowering class declarations and class builtin randomization calls.
- Test `test/Conversion/MooreToCore/class-randomize-crosscheck.mlir`: verify generated predicate evaluator and commit gate.
- Add tests under `test/Conversion/ImportVerilog`, `test/Dialect/Moore`, and `test/Conversion/MooreToCore`.
- Update `docs/SystemVerilogConstraintSolving.md` as support lands.

---

### Task 1: Optional Bitwuzla Runtime Hook

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `lib/Dialect/Arc/Runtime/CMakeLists.txt`
- Create: `include/circt/Dialect/Arc/Runtime/ConstraintSolver.h`
- Create: `lib/Dialect/Arc/Runtime/ConstraintSolver.cpp`
- Test: configure-only and compile tests through `ninja -C build CIRCTArcRuntime`

**Interfaces:**
- Produces:
  - `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE` CMake option.
  - `CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE` compile definition when enabled.
  - `ConstraintSolver.h` C ABI for generated code.
- Consumes: no earlier task outputs.

- [ ] **Step 1: Write the runtime header**

Create `include/circt/Dialect/Arc/Runtime/ConstraintSolver.h` with this initial API:

```c++
#ifndef CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H
#define CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H

#include <stdbool.h>
#include <stdint.h>

#include "circt/Dialect/Arc/Runtime/ArcRuntime.h"

ARC_RUNTIME_EXPORT bool arcRuntimeSolverIsAvailable(void);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverCreate(void);
ARC_RUNTIME_EXPORT void arcRuntimeSolverDestroy(void *solver);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverBvVar(void *solver, const char *name,
                                               uint32_t width);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverBvConst(void *solver, uint64_t value,
                                                 uint32_t width);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverEq(void *solver, void *lhs,
                                            void *rhs);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverSgt(void *solver, void *lhs,
                                             void *rhs);
ARC_RUNTIME_EXPORT void arcRuntimeSolverAssert(void *solver, void *term);
ARC_RUNTIME_EXPORT bool arcRuntimeSolverCheck(void *solver);
ARC_RUNTIME_EXPORT uint64_t arcRuntimeSolverGetBv(void *solver, void *term);

#endif // CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H
```

- [ ] **Step 2: Write the disabled implementation**

Create `lib/Dialect/Arc/Runtime/ConstraintSolver.cpp` with a disabled fallback first:

```c++
#include "circt/Dialect/Arc/Runtime/ConstraintSolver.h"

#include <cassert>

#ifndef CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE

extern "C" bool arcRuntimeSolverIsAvailable(void) { return false; }
extern "C" void *arcRuntimeSolverCreate(void) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}
extern "C" void arcRuntimeSolverDestroy(void *) {}
extern "C" void *arcRuntimeSolverBvVar(void *, const char *, uint32_t) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}
extern "C" void *arcRuntimeSolverBvConst(void *, uint64_t, uint32_t) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}
extern "C" void *arcRuntimeSolverEq(void *, void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}
extern "C" void *arcRuntimeSolverSgt(void *, void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}
extern "C" void arcRuntimeSolverAssert(void *, void *) {}
extern "C" bool arcRuntimeSolverCheck(void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return false;
}
extern "C" uint64_t arcRuntimeSolverGetBv(void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return 0;
}

#endif
```

- [ ] **Step 3: Add the source to Arc runtime build**

In `lib/Dialect/Arc/Runtime/CMakeLists.txt`, add `ConstraintSolver.cpp` to `ArcRuntimeLibSources`:

```cmake
set(ArcRuntimeLibSources
  ArcRuntime.cpp
  ConstraintSolver.cpp
  ModelInstance.cpp
  TraceEncoder.cpp
  VCDTraceEncoder.cpp
)
```

- [ ] **Step 4: Add optional Bitwuzla CMake option**

In top-level `CMakeLists.txt`, near the existing Z3 configuration block, add:

```cmake
option(CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE
  "Enable Bitwuzla-backed SystemVerilog class randomize support in Arc runtime."
  OFF)

if(CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE)
  find_package(Bitwuzla CONFIG QUIET)
  if(NOT Bitwuzla_FOUND)
    find_path(Bitwuzla_INCLUDE_DIR NAMES bitwuzla/c/bitwuzla.h)
    find_library(Bitwuzla_LIBRARY NAMES bitwuzla)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Bitwuzla DEFAULT_MSG
      Bitwuzla_LIBRARY Bitwuzla_INCLUDE_DIR)
    if(Bitwuzla_FOUND AND NOT TARGET Bitwuzla::bitwuzla)
      add_library(Bitwuzla::bitwuzla UNKNOWN IMPORTED)
      set_target_properties(Bitwuzla::bitwuzla PROPERTIES
        IMPORTED_LOCATION "${Bitwuzla_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Bitwuzla_INCLUDE_DIR}")
    endif()
  endif()
  message(STATUS "Arc class randomize support enabled with Bitwuzla.")
endif()
```

- [ ] **Step 5: Wire compile definitions and link dependency**

In `lib/Dialect/Arc/Runtime/CMakeLists.txt`, after `CIRCTArcRuntime` is created, add:

```cmake
if(CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE)
  target_compile_definitions(CIRCTArcRuntime
    PUBLIC CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE=1)
  target_link_libraries(CIRCTArcRuntime PRIVATE Bitwuzla::bitwuzla)
endif()
```

Repeat for `CIRCTArcJITRuntime`:

```cmake
if(CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE)
  target_compile_definitions(obj.CIRCTArcJITRuntime
    PUBLIC CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE=1)
  target_link_libraries(CIRCTArcJITRuntime PRIVATE Bitwuzla::bitwuzla)
endif()
```

- [ ] **Step 6: Build disabled configuration**

Run:

```bash
ninja -C build CIRCTArcRuntime
```

Expected: build succeeds without Bitwuzla installed.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt lib/Dialect/Arc/Runtime/CMakeLists.txt \
  include/circt/Dialect/Arc/Runtime/ConstraintSolver.h \
  lib/Dialect/Arc/Runtime/ConstraintSolver.cpp
git commit -m "[Arc] Add optional constraint solver runtime hook"
```

---

### Task 2: Bitwuzla Runtime Wrapper Implementation

**Files:**
- Modify: `lib/Dialect/Arc/Runtime/ConstraintSolver.cpp`
- Modify: `unittests/Dialect/Arc/Runtime/ArcRuntimeTest.cpp`

**Interfaces:**
- Consumes: C ABI from Task 1.
- Produces: working implementation of the C ABI when `CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE` is defined.

- [ ] **Step 1: Add enabled implementation skeleton**

Extend `lib/Dialect/Arc/Runtime/ConstraintSolver.cpp`:

```c++
#ifdef CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE

#include <bitwuzla/c/bitwuzla.h>
#include <memory>
#include <string>

namespace {
struct SolverState {
  BitwuzlaTermManager *tm = nullptr;
  Bitwuzla *solver = nullptr;
  BitwuzlaOptions *options = nullptr;

  SolverState() {
    tm = bitwuzla_term_manager_new();
    options = bitwuzla_options_new();
    bitwuzla_set_option(options, BITWUZLA_OPT_PRODUCE_MODELS, 1);
    solver = bitwuzla_new(tm, options);
  }

  ~SolverState() {
    if (solver)
      bitwuzla_delete(solver);
    if (options)
      bitwuzla_options_delete(options);
    if (tm)
      bitwuzla_term_manager_delete(tm);
  }
};

static SolverState *asState(void *solver) {
  return reinterpret_cast<SolverState *>(solver);
}

static BitwuzlaTerm asTerm(void *term) {
  return reinterpret_cast<BitwuzlaTerm>(term);
}

static void *asOpaque(BitwuzlaTerm term) {
  return reinterpret_cast<void *>(term);
}
} // namespace

extern "C" bool arcRuntimeSolverIsAvailable(void) { return true; }

extern "C" void *arcRuntimeSolverCreate(void) {
  return new SolverState();
}

extern "C" void arcRuntimeSolverDestroy(void *solver) {
  delete asState(solver);
}

#endif
```

- [ ] **Step 2: Add bit-vector variable and constant creation**

Add inside the enabled block:

```c++
extern "C" void *arcRuntimeSolverBvVar(void *solver, const char *name,
                                       uint32_t width) {
  auto *state = asState(solver);
  auto sort = bitwuzla_mk_bv_sort(state->tm, width);
  return asOpaque(bitwuzla_mk_const(state->tm, sort, name));
}

extern "C" void *arcRuntimeSolverBvConst(void *solver, uint64_t value,
                                         uint32_t width) {
  auto *state = asState(solver);
  auto sort = bitwuzla_mk_bv_sort(state->tm, width);
  return asOpaque(bitwuzla_mk_bv_value_uint64(state->tm, sort, value));
}
```

- [ ] **Step 3: Add equality and signed greater-than**

Add:

```c++
extern "C" void *arcRuntimeSolverEq(void *solver, void *lhs, void *rhs) {
  auto *state = asState(solver);
  return asOpaque(bitwuzla_mk_term2(state->tm, BITWUZLA_KIND_EQUAL,
                                    asTerm(lhs), asTerm(rhs)));
}

extern "C" void *arcRuntimeSolverSgt(void *solver, void *lhs, void *rhs) {
  auto *state = asState(solver);
  return asOpaque(bitwuzla_mk_term2(state->tm, BITWUZLA_KIND_BV_SGT,
                                    asTerm(lhs), asTerm(rhs)));
}
```

- [ ] **Step 4: Add assert/check/model extraction**

Add:

```c++
extern "C" void arcRuntimeSolverAssert(void *solver, void *term) {
  auto *state = asState(solver);
  bitwuzla_assert(state->solver, asTerm(term));
}

extern "C" bool arcRuntimeSolverCheck(void *solver) {
  auto *state = asState(solver);
  return bitwuzla_check_sat(state->solver) == BITWUZLA_SAT;
}

extern "C" uint64_t arcRuntimeSolverGetBv(void *solver, void *term) {
  auto *state = asState(solver);
  BitwuzlaTerm value = bitwuzla_get_value(state->solver, asTerm(term));
  const char *bits = bitwuzla_term_value_get_str(value);
  uint64_t result = 0;
  for (const char *p = bits; *p; ++p)
    result = (result << 1) | static_cast<uint64_t>(*p == '1');
  return result;
}
```

- [ ] **Step 5: Add runtime unit test**

In `unittests/Dialect/Arc/Runtime/ArcRuntimeTest.cpp`, include the solver header:

```c++
#include "circt/Dialect/Arc/Runtime/ConstraintSolver.h"
```

Add:

```c++
TEST(ArcRuntimeTest, ConstraintSolverFindsBvModel) {
  if (!arcRuntimeSolverIsAvailable())
    GTEST_SKIP() << "Bitwuzla randomize runtime is disabled";

  void *solver = arcRuntimeSolverCreate();
  void *x = arcRuntimeSolverBvVar(solver, "x", 8);
  void *zero = arcRuntimeSolverBvConst(solver, 0, 8);
  void *positive = arcRuntimeSolverSgt(solver, x, zero);
  arcRuntimeSolverAssert(solver, positive);
  EXPECT_TRUE(arcRuntimeSolverCheck(solver));
  EXPECT_GT(arcRuntimeSolverGetBv(solver, x), 0u);
  arcRuntimeSolverDestroy(solver);
}
```

- [ ] **Step 6: Compile with Bitwuzla enabled**

Run in a build configured with Bitwuzla:

```bash
cmake -G Ninja llvm/llvm -B build-bitwuzla \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_EXTERNAL_PROJECTS=circt \
  -DLLVM_EXTERNAL_CIRCT_SOURCE_DIR=$PWD \
  -DLLVM_ENABLE_LLD=ON \
  -DCIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE=ON
ninja -C build-bitwuzla CIRCTArcRuntime CIRCTArcRuntimeUnitTests
build-bitwuzla/unittests/Dialect/Arc/Runtime/CIRCTArcRuntimeUnitTests \
  --gtest_filter=ArcRuntimeTest.ConstraintSolverFindsBvModel
```

Expected: runtime library links against Bitwuzla and the unit test passes.

- [ ] **Step 7: Commit**

```bash
git add lib/Dialect/Arc/Runtime/ConstraintSolver.cpp \
  unittests/Dialect/Arc/Runtime/ArcRuntimeTest.cpp
git commit -m "[Arc] Implement Bitwuzla constraint solver wrapper"
```

---

### Task 3: Preserve Rand Metadata in Moore Class Properties

**Files:**
- Modify: `include/circt/Dialect/Moore/MooreOps.td`
- Modify: `lib/Conversion/ImportVerilog/Structure.cpp`
- Modify: `lib/Dialect/Moore/MooreOps.cpp`
- Test: `test/Conversion/ImportVerilog/rand-properties.sv`
- Test: `test/Dialect/Moore/classes.mlir`

**Interfaces:**
- Consumes: current `moore.class.propertydecl`.
- Produces:
  - `moore.class.propertydecl` optional unit attrs `isRand` and `isRandC`.
  - Importer preserves slang rand/randc metadata.

- [ ] **Step 1: Write failing import test**

Create `test/Conversion/ImportVerilog/rand-properties.sv`:

```systemverilog
// RUN: circt-verilog --ir-moore %s | FileCheck %s
// REQUIRES: slang

class Packet;
  rand int len;
  randc bit [7:0] token;
  rand int words[4];
  int non_rand;
endclass

// CHECK-LABEL: moore.class.classdecl @Packet
// CHECK: moore.class.propertydecl @len : !moore.i32 attributes {isRand}
// CHECK: moore.class.propertydecl @token : !moore.l8 attributes {isRandC}
// CHECK: moore.class.propertydecl @words : !moore.array<4 x i32> attributes {isRand}
// CHECK: moore.class.propertydecl @non_rand : !moore.i32
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/rand-properties.sv \
  | llvm/build/bin/FileCheck test/Conversion/ImportVerilog/rand-properties.sv
```

Expected: FAIL because `isRand` and `isRandC` are not printed.

- [ ] **Step 3: Add attrs to ClassPropertyDeclOp**

In `include/circt/Dialect/Moore/MooreOps.td`, change `ClassPropertyDeclOp` arguments to:

```tablegen
let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type,
                     OptionalAttr<UnitAttr>:$isRand,
                     OptionalAttr<UnitAttr>:$isRandC);
```

Keep the existing assembly format:

```tablegen
let assemblyFormat = [{
   $sym_name `:` $type  attr-dict
}];
```

- [ ] **Step 4: Set attrs in importer**

In `ClassPropertyVisitor::visit(const slang::ast::ClassPropertySymbol &prop)` in `lib/Conversion/ImportVerilog/Structure.cpp`, compute:

```c++
auto unit = UnitAttr::get(context.getContext());
auto isRand = prop.flags.has(slang::ast::VariableFlags::Rand) ? unit : UnitAttr();
auto isRandC =
    prop.flags.has(slang::ast::VariableFlags::RandC) ? unit : UnitAttr();
```

Update automatic property creation:

```c++
moore::ClassPropertyDeclOp::create(builder, loc, prop.name, ty, isRand,
                                   isRandC);
```

- [ ] **Step 5: Update existing MLIR tests**

In `test/Dialect/Moore/classes.mlir`, update any manually written `moore.class.propertydecl` operations that should remain non-rand by leaving them unchanged. Add one roundtrip case:

```mlir
// CHECK-LABEL: moore.class.classdecl @RandPropertyCombo {
// CHECK-NEXT:    moore.class.propertydecl @len : !moore.i32 attributes {isRand}
// CHECK-NEXT:    moore.class.propertydecl @token : !moore.l8 attributes {isRandC}
// CHECK-NEXT:  }
moore.class.classdecl @RandPropertyCombo {
  moore.class.propertydecl @len : !moore.i32 attributes {isRand}
  moore.class.propertydecl @token : !moore.l8 attributes {isRandC}
}
```

- [ ] **Step 6: Run tests**

```bash
ninja -C build bin/circt-verilog bin/circt-opt
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/rand-properties.sv \
  | llvm/build/bin/FileCheck test/Conversion/ImportVerilog/rand-properties.sv
build/bin/circt-opt test/Dialect/Moore/classes.mlir \
  | llvm/build/bin/FileCheck test/Dialect/Moore/classes.mlir
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add include/circt/Dialect/Moore/MooreOps.td \
  lib/Conversion/ImportVerilog/Structure.cpp \
  lib/Dialect/Moore/MooreOps.cpp \
  test/Conversion/ImportVerilog/rand-properties.sv \
  test/Dialect/Moore/classes.mlir
git commit -m "[Moore] Preserve rand property metadata"
```

---

### Task 4: MooreToCore Randomize Skeleton and Local Descriptors

**Files:**
- Create: `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- Modify: `lib/Conversion/MooreToCore/CMakeLists.txt`
- Modify: `lib/Conversion/MooreToCore/MooreToCore.cpp`
- Test: `test/Conversion/MooreToCore/class-randomize-skeleton.mlir`

**Interfaces:**
- Consumes: Moore class declarations with `isRand`/`isRandC` property metadata.
- Produces:
  - local `RandFieldDesc`, `ConstraintBlockDesc`, and `ConstraintProblemDesc` structs in `LowerClassRandomize.cpp`.
  - `LogicalResult prepareClassRandomizeSupport(ModuleOp module, SymbolTable &symbolTable);`
  - generated hidden mode properties named `__circt_rand_mode__<field>` and `__circt_constraint_mode__<constraint>`.

- [ ] **Step 1: Write skeleton test**

Create `test/Conversion/MooreToCore/class-randomize-skeleton.mlir`:

```mlir
// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 attributes {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %true = moore.constant 1 : i1
    moore.yield %true : i1
  }
}

// CHECK: moore.class.propertydecl @__circt_rand_mode__len : !moore.i1
// CHECK: moore.class.propertydecl @__circt_constraint_mode__c : !moore.i1
```

- [ ] **Step 2: Add local descriptor code**

Create `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`:

```c++
#include "circt/Dialect/Moore/MooreOps.h"
#include "circt/Dialect/Moore/MooreTypes.h"
#include "mlir/IR/SymbolTable.h"

using namespace circt;
using namespace circt::moore;
using namespace mlir;

namespace {
struct RandFieldDesc {
  ClassPropertyDeclOp property;
  bool isRandC = false;
  bool isOneDimUnpackedArray = false;
  unsigned elementCount = 1;
  unsigned bitWidth = 0;
};

struct ConstraintBlockDesc {
  ClassConstraintDeclOp constraint;
  bool isStatic = false;
};

struct ConstraintProblemDesc {
  ClassDeclOp cls;
  SmallVector<RandFieldDesc> randFields;
  SmallVector<ConstraintBlockDesc> constraints;
};

static std::optional<unsigned> getIntegralWidth(Type type) {
  if (auto intTy = dyn_cast<IntType>(type))
    return intTy.getWidth();
  return std::nullopt;
}

static FailureOr<RandFieldDesc>
describeRandField(ClassPropertyDeclOp property) {
  auto type = property.getPropertyType();
  RandFieldDesc desc;
  desc.property = property;
  desc.isRandC = static_cast<bool>(property.getIsRandCAttr());

  if (auto width = getIntegralWidth(type)) {
    desc.bitWidth = *width;
    return desc;
  }

  if (auto arrayTy = dyn_cast<UnpackedArrayType>(type)) {
    auto width = getIntegralWidth(arrayTy.getElementType());
    if (!width)
      return property.emitError()
             << "only 1-dim unpacked arrays of integral elements are supported "
                "for randomization";
    desc.isOneDimUnpackedArray = true;
    desc.elementCount = arrayTy.getSize();
    desc.bitWidth = *width;
    return desc;
  }

  return property.emitError()
         << "only integral scalar rand fields and 1-dim unpacked arrays of "
            "integral elements are supported";
}

static FailureOr<ConstraintProblemDesc> collectConstraintProblem(ClassDeclOp cls) {
  ConstraintProblemDesc result;
  result.cls = cls;
  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>()) {
    if (!property.getIsRandAttr() && !property.getIsRandCAttr())
      continue;
    auto desc = describeRandField(property);
    if (failed(desc))
      return failure();
    result.randFields.push_back(*desc);
  }
  for (auto constraint : cls.getBody().getOps<ClassConstraintDeclOp>()) {
    if (constraint.getIsPure() || constraint.getIsExtern())
      continue;
    result.constraints.push_back({constraint,
                                  static_cast<bool>(constraint.getIsStatic())});
  }
  return result;
}

static void ensureModeProperty(OpBuilder &builder, ClassDeclOp cls,
                               StringRef name) {
  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>())
    if (property.getSymName() == name)
      return;
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&cls.getBody().front());
  auto type = IntType::getInt(builder.getContext(), 1);
  auto op = ClassPropertyDeclOp::create(builder, cls.getLoc(), name, type);
  op->setAttr("circt.randomize.mode", builder.getUnitAttr());
}
} // namespace

LogicalResult prepareClassRandomizeSupport(ModuleOp module,
                                           SymbolTable &symbolTable) {
  OpBuilder builder(module.getContext());
  bool failedAny = false;
  module.walk([&](ClassDeclOp cls) {
    auto problem = collectConstraintProblem(cls);
    if (failed(problem)) {
      failedAny = true;
      return;
    }
    for (auto field : problem->randFields)
      ensureModeProperty(builder, cls,
                         llvm::formatv("__circt_rand_mode__{0}",
                                       field.property.getSymName()));
    for (auto block : problem->constraints)
      ensureModeProperty(builder, cls,
                         llvm::formatv("__circt_constraint_mode__{0}",
                                       block.constraint.getSymName()));
  });
  return failure(failedAny);
}
```

- [ ] **Step 3: Wire helper into MooreToCore**

In `lib/Conversion/MooreToCore/CMakeLists.txt`, add:

```cmake
add_circt_conversion_library(CIRCTMooreToCore
  MooreToCore.cpp
  LowerClassRandomize.cpp
```

In `lib/Conversion/MooreToCore/MooreToCore.cpp`, forward declare and call the helper at the start of `MooreToCorePass::runOnOperation()`:

```c++
LogicalResult prepareClassRandomizeSupport(ModuleOp module,
                                           SymbolTable &symbolTable);
```

```c++
SymbolTable symbolTable(getOperation());
if (failed(prepareClassRandomizeSupport(getOperation(), symbolTable)))
  return signalPassFailure();
```

- [ ] **Step 4: Run test**

```bash
ninja -C build bin/circt-opt
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-skeleton.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-skeleton.mlir
```

- [ ] **Step 5: Commit**

```bash
git add lib/Conversion/MooreToCore/LowerClassRandomize.cpp \
  lib/Conversion/MooreToCore/CMakeLists.txt \
  lib/Conversion/MooreToCore/MooreToCore.cpp \
  test/Conversion/MooreToCore/class-randomize-skeleton.mlir
git commit -m "[MooreToCore] Prepare class randomize lowering"
```

---

### Task 5: Shared Constraint Expression Lowering and Solver Calls

**Files:**
- Modify: `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- Test: `test/Conversion/MooreToCore/class-randomize-solver-calls.mlir`
- Test: `test/Conversion/MooreToCore/class-randomize-unsupported.mlir`

**Interfaces:**
- Consumes: runtime C ABI from Task 1 and local descriptors from Task 4.
- Produces:
  - one shared expression walker for supported Moore constraint ops.
  - a solver emitter backend that builds Bitwuzla runtime calls.
  - diagnostics for unsupported Moore ops.

- [ ] **Step 1: Write solver call test**

Create `test/Conversion/MooreToCore/class-randomize-solver-calls.mlir`:

```mlir
// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 attributes {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %ref = moore.class.property_ref %this[@len] : <@Packet> -> <i32>
    %val = moore.read %ref : <i32>
    %ok = moore.sgt %val, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: arcRuntimeSolverCreate
// CHECK: arcRuntimeSolverBvVar
// CHECK: arcRuntimeSolverBvConst
// CHECK: arcRuntimeSolverSgt
// CHECK: arcRuntimeSolverAssert
// CHECK: arcRuntimeSolverCheck
```

- [ ] **Step 2: Write unsupported test**

Create `test/Conversion/MooreToCore/class-randomize-unsupported.mlir`:

```mlir
// RUN: not circt-opt --convert-moore-to-core %s 2>&1 | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 attributes {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %ref = moore.class.property_ref %this[@len] : <@Packet> -> <i32>
    %val = moore.read %ref : <i32>
    %ok = moore.case_eq %val, %val : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: unsupported randomize constraint operation
```

- [ ] **Step 3: Add shared expression walker**

In `LowerClassRandomize.cpp`, add one walker that delegates backend-specific work to an emitter:

```c++
template <typename Emitter>
struct ConstraintExprLowerer {
  Emitter &emitter;
  DenseMap<Value, Value> valueMap;

  FailureOr<Value> lower(Value value) {
    if (auto mapped = valueMap.lookup(value))
      return mapped;
    auto *def = value.getDefiningOp();
    if (!def)
      return failure();
    auto lowered = lowerOp(def);
    if (succeeded(lowered))
      valueMap[value] = *lowered;
    return lowered;
  }

  FailureOr<Value> lowerOp(Operation *op) {
    return TypeSwitch<Operation *, FailureOr<Value>>(op)
        .Case<ConstantOp>([&](auto op) { return emitter.emitConstant(op); })
        .Case<ClassPropertyRefOp>(
            [&](auto op) { return emitter.emitPropertyRead(op); })
        .Case<ReadOp>([&](auto op) { return lower(op.getInput()); })
        .Case<ExtractOp>([&](auto op) { return emitter.emitExtract(op); })
        .Case<SgtOp>([&](auto op) { return emitter.emitSgt(op); })
        .Case<EqOp>([&](auto op) { return emitter.emitEq(op); })
        .Case<NeOp>([&](auto op) { return emitter.emitNe(op); })
        .Case<AndOp>([&](auto op) { return emitter.emitAnd(op); })
        .Default([](Operation *op) -> FailureOr<Value> {
          return op->emitError("unsupported randomize constraint operation");
        });
  }
};
```

- [ ] **Step 4: Add solver emitter**

Add a `SolverEmitter` with fields `OpBuilder &builder`, `Location loc`, `Value solver`, scalar variable map, and array element variable map. It emits `func::CallOp` to:

```text
arcRuntimeSolverCreate
arcRuntimeSolverBvVar
arcRuntimeSolverBvConst
arcRuntimeSolverSgt
arcRuntimeSolverEq
arcRuntimeSolverAssert
arcRuntimeSolverCheck
arcRuntimeSolverGetBv
arcRuntimeSolverDestroy
```

- [ ] **Step 5: Run tests**

```bash
ninja -C build bin/circt-opt
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-solver-calls.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-solver-calls.mlir
not build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-unsupported.mlir 2>&1 \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-unsupported.mlir
```

- [ ] **Step 6: Commit**

```bash
git add lib/Conversion/MooreToCore/LowerClassRandomize.cpp \
  test/Conversion/MooreToCore/class-randomize-solver-calls.mlir \
  test/Conversion/MooreToCore/class-randomize-unsupported.mlir
git commit -m "[MooreToCore] Lower randomize constraints to solver calls"
```

---

### Task 6: 1-Dim Array Variables and Predicate Crosscheck

**Files:**
- Modify: `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- Test: `test/Conversion/MooreToCore/class-randomize-array.mlir`
- Test: `test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir`
- Test: `test/Conversion/MooreToCore/class-randomize-crosscheck.mlir`

**Interfaces:**
- Consumes: shared expression walker and solver emitter from Task 5.
- Produces:
  - per-element solver variables for 1-dim unpacked arrays.
  - generated predicate helper `__circt_randomize_check_<class-symbol>`.
  - commit gate: candidate values write back only if predicate helper returns true.

- [ ] **Step 1: Write array and crosscheck tests**

Create `test/Conversion/MooreToCore/class-randomize-array.mlir`:

```mlir
// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @words : !moore.uarray<4 x i32> attributes {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %ref = moore.class.property_ref %this[@words] : <@Packet> -> <uarray<4 x i32>>
    %array = moore.read %ref : <uarray<4 x i32>>
    %elem = moore.extract %array from 3 : uarray<4 x i32> -> i32
    %ok = moore.sgt %elem, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: arcRuntimeSolverBvVar
// CHECK-SAME: words[0]
// CHECK: arcRuntimeSolverBvVar
// CHECK-SAME: words[3]
```

Create `test/Conversion/MooreToCore/class-randomize-crosscheck.mlir`:

```mlir
// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 attributes {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %ref = moore.class.property_ref %this[@len] : <@Packet> -> <i32>
    %val = moore.read %ref : <i32>
    %ok = moore.sgt %val, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK-LABEL: func.func private @__circt_randomize_check_Packet
// CHECK: arith.cmpi sgt
// CHECK-LABEL: func.func private @__circt_randomize_Packet
// CHECK: func.call @__circt_randomize_check_Packet
// CHECK: cf.cond_br
```

- [ ] **Step 2: Write variable-index rejection test**

Create `test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir`:

```mlir
// RUN: not circt-opt --convert-moore-to-core %s 2>&1 | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @words : !moore.uarray<4 x i32> attributes {isRand}
  moore.class.propertydecl @idx : !moore.i32
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %ref = moore.class.property_ref %this[@words] : <@Packet> -> <uarray<4 x i32>>
    %idxRef = moore.class.property_ref %this[@idx] : <@Packet> -> <i32>
    %idx = moore.read %idxRef : <i32>
    %elem = moore.dyn_extract_ref %ref from %idx : <uarray<4 x i32>>, i32 -> <i32>
    %val = moore.read %elem : <i32>
    %zero = moore.constant 0 : i32
    %ok = moore.sgt %val, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: randomize constraints only support constant indices into 1-dim unpacked rand arrays
```

- [ ] **Step 3: Add array variable and predicate emitters**

In `SolverEmitter`, create one variable per element:

```c++
for (unsigned i = 0, e = field.elementCount; i != e; ++i) {
  auto name = llvm::formatv("{0}[{1}]", field.property.getSymName(), i).str();
  elementVars[{field.property.getOperation(), i}] =
      emitBvVarCall(builder, loc, solver, name, field.bitWidth);
}
```

Add a `PredicateEmitter` with scalar candidate map and array element candidate map as direct fields. Reuse `ConstraintExprLowerer<PredicateEmitter>` to generate `__circt_randomize_check_<class-symbol>`.

- [ ] **Step 4: Gate commit on crosscheck**

After solver SAT and model extraction, call the predicate helper:

```text
%crosscheck_ok = func.call @__circt_randomize_check_Packet(...)
cf.cond_br %crosscheck_ok, ^commit, ^fail
```

In `^fail`, destroy the solver and return false without writing candidate values to object fields.

- [ ] **Step 5: Run tests**

```bash
ninja -C build bin/circt-opt
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-array.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-array.mlir
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-crosscheck.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-crosscheck.mlir
not build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir 2>&1 \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir
```

- [ ] **Step 6: Commit**

```bash
git add lib/Conversion/MooreToCore/LowerClassRandomize.cpp \
  test/Conversion/MooreToCore/class-randomize-array.mlir \
  test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir \
  test/Conversion/MooreToCore/class-randomize-crosscheck.mlir
git commit -m "[MooreToCore] Crosscheck randomize solver models"
```

---

### Task 7: Import `randomize()` and Update Documentation

**Files:**
- Modify: `include/circt/Dialect/Moore/MooreOps.td`
- Modify: `lib/Conversion/ImportVerilog/Expressions.cpp`
- Modify: `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Test: `test/Conversion/ImportVerilog/randomize-supported.sv`
- Test: `test/Conversion/MooreToCore/randomize-call.mlir`

**Interfaces:**
- Consumes: generated randomize helper and predicate crosscheck gate from Tasks 5-6.
- Produces:
  - Moore representation for object `randomize()` call.
  - final support matrix documentation for the implemented subset.

- [ ] **Step 1: Write ImportVerilog test**

Create `test/Conversion/ImportVerilog/randomize-supported.sv`:

```systemverilog
// RUN: circt-verilog --ir-moore %s | FileCheck %s
// REQUIRES: slang

class Packet;
  rand int len;
  constraint c { len > 0; }
endclass

module m;
  initial begin
    Packet p = new;
    bit ok;
    ok = p.randomize();
  end
endmodule

// CHECK: moore.class.classdecl @Packet
// CHECK: moore.class.constraintdecl @c
// CHECK: moore.class.randomize
```

- [ ] **Step 2: Add Moore op and importer support**

In `include/circt/Dialect/Moore/MooreOps.td`, define:

```tablegen
def ClassRandomizeOp : MooreOp<"class.randomize", []> {
  let summary = "Randomize a class object";
  let arguments = (ins ClassHandleType:$object);
  let results = (outs TwoValuedI1:$result);
  let assemblyFormat = "$object attr-dict `:` type($object)";
}
```

In `lib/Conversion/ImportVerilog/Expressions.cpp`, replace the unsupported class method path for `randomize` with:

```c++
if (subroutine.name == "randomize" && thisRef &&
    isa<moore::ClassHandleType>(thisRef.getType()))
  return moore::ClassRandomizeOp::create(builder, loc, thisRef);
```

- [ ] **Step 3: Lower `moore.class.randomize`**

In `LowerClassRandomize.cpp`, lower `ClassRandomizeOp` to:

```text
@__circt_randomize_Packet(%object) : (!moore.class<@Packet>) -> i1
```

The helper returns false on solver UNSAT and on predicate crosscheck failure.

- [ ] **Step 4: Update docs**

In `docs/SystemVerilogConstraintSolving.md`, add:

```markdown
## Implemented Randomize Subset

- Backend: arcilator simulation runtime.
- Solver: optional Bitwuzla-backed runtime solver.
- Supported fields: integral scalar `rand` fields and 1-dim unpacked arrays of integral elements.
- Array lowering: one Bitwuzla bit-vector variable per unpacked array element.
- Result validation: solver models are checked by a generated runtime predicate evaluator before candidate values are committed to the object.
- Supported constraints: hard expression constraints over constants, class property reads, array element reads with constant indices, equality, inequality, signed comparisons, and boolean `and`.
- Runtime modes: `rand_mode` and `constraint_mode` are stored in class runtime state and read when `randomize()` is called.

Unsupported forms diagnose instead of being silently ignored.
```

- [ ] **Step 5: Run tests**

```bash
ninja -C build bin/circt-verilog bin/circt-opt
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/randomize-supported.sv \
  | llvm/build/bin/FileCheck test/Conversion/ImportVerilog/randomize-supported.sv
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/randomize-call.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/randomize-call.mlir
git diff -- docs/SystemVerilogConstraintSolving.md
```

- [ ] **Step 6: Commit**

```bash
git add include/circt/Dialect/Moore/MooreOps.td \
  lib/Conversion/ImportVerilog/Expressions.cpp \
  lib/Conversion/MooreToCore/LowerClassRandomize.cpp \
  docs/SystemVerilogConstraintSolving.md \
  test/Conversion/ImportVerilog/randomize-supported.sv \
  test/Conversion/MooreToCore/randomize-call.mlir
git commit -m "[ImportVerilog] Import class randomize calls"
```

---

## Validation Checklist

Run these before merging the series:

```bash
ninja -C build bin/circt-verilog bin/circt-opt CIRCTArcRuntime
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/constraints.sv \
  | llvm/build/bin/FileCheck test/Conversion/ImportVerilog/constraints.sv
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/randomize-supported.sv \
  | llvm/build/bin/FileCheck test/Conversion/ImportVerilog/randomize-supported.sv
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-skeleton.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-skeleton.mlir
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-solver-calls.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-solver-calls.mlir
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-array.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-array.mlir
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-crosscheck.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-crosscheck.mlir
build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/randomize-call.mlir \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/randomize-call.mlir
not build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-unsupported.mlir 2>&1 \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-unsupported.mlir
not build/bin/circt-opt --convert-moore-to-core \
  test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir 2>&1 \
  | llvm/build/bin/FileCheck test/Conversion/MooreToCore/class-randomize-array-unsupported.mlir
```

Run with Bitwuzla enabled in a separate build:

```bash
cmake -G Ninja llvm/llvm -B build-bitwuzla \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_EXTERNAL_PROJECTS=circt \
  -DLLVM_EXTERNAL_CIRCT_SOURCE_DIR=$PWD \
  -DLLVM_ENABLE_LLD=ON \
  -DCIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE=ON
ninja -C build-bitwuzla CIRCTArcRuntime bin/circt-opt
```

---

## Self-Review

- Spec coverage: covers SMT-backed runtime solving, arcilator-first backend scope, runtime `rand_mode` and `constraint_mode`, integral scalar fields, 1-dim unpacked arrays, per-element variables, generated predicate crosscheck before object commit, unsupported feature diagnostics, and optional Bitwuzla dependency.
- Placeholder scan: no `TBD` or open-ended implementation placeholders remain. The array test uses the current ImportVerilog IR shape for `rand int words[4]; constraint c { words[0] > 0; }`.
- Type consistency: runtime C ABI uses `void *` handles consistently; Moore descriptor names match task consumers; `RandFieldDesc` field names are reused consistently.
