# Arcilator Randomize Executable Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal arcilator-oriented execution demo for SystemVerilog class randomize helpers where ahead-of-time compiled executables are the primary path and JIT is an additional smoke-test path.

**Architecture:** Keep SystemVerilog `initial` unsupported in arcilator. Do not add a general LLHD process lowering. Instead, use an arcilator/native `func.func @main` harness that calls the generated `__circt_randomize_<Class>` helper, emit LLVM IR with `arcilator --emit-llvm`, link it with `CIRCTArcRuntime` and Bitwuzla into an executable, and run it. JIT support uses the same generated runtime ABI and only adds symbol binding for `arcRuntimeSolver*`.

**Tech Stack:** CIRCT MooreToCore randomize helper generation, arcilator LLVM emission, Arc runtime, optional Bitwuzla C API, clang++/lld AOT linking, MLIR ExecutionEngine JIT, lit/FileCheck.

## Global Constraints

- Do not add support for SV `initial` blocks to arcilator for this demo.
- Do not add a generic `llhd.process` to Arc/arcilator lowering.
- AOT executable support is the primary acceptance path.
- JIT support must share the same `arcRuntimeSolver*` ABI and must not become the only supported execution path.
- Keep Bitwuzla optional through `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE`.
- Keep normal arcilator builds working when Bitwuzla support is disabled.
- Keep development builds in the existing `build-bitwuzla` directory unless explicitly reconfiguring is required.
- Use `ninja -C build-bitwuzla -j14 ...` at most; lower the job count if link memory pressure returns.
- The first demo may start from MooreToCore MLIR. Do not require the current `build-bitwuzla` cache to have `circt-verilog` or slang enabled.

---

## Current Facts

- `build-bitwuzla/bin/arcilator` is already built.
- `build-bitwuzla` currently has Bitwuzla enabled but does not build `circt-verilog`.
- The existing `test/Conversion/MooreToCore/randomize-call.mlir` path lowers to generated solver helper calls.
- Feeding the current randomize-call output into arcilator succeeds through `arc-opt`.
- It fails at `state-lowering` only because that test input uses a Moore `initial` procedure, which MooreToCore lowers to `llhd.process`.
- This failure should not be fixed by teaching arcilator to support SV `initial`.
- `tools/arcilator/arcilator.cpp` currently emits LLVM IR for non-JIT output and does not directly produce native executables.
- `tools/arcilator/arcilator.cpp` currently JIT-binds Arc runtime IR callbacks, but not `arcRuntimeSolver*`.

## File Structure

- Modify `test/lit.site.cfg.py.in`: expose whether `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE` is enabled and provide paths/substitutions needed by executable tests.
- Modify `test/lit.cfg.py`: add a `bitwuzla-randomize` feature and substitutions for the Arc runtime library and Bitwuzla link flags.
- Modify `include/circt/Dialect/Arc/Runtime/JITBind.h`: extend the internal JIT callback table with solver runtime functions.
- Modify `lib/Dialect/Arc/Runtime/ArcRuntime.cpp`: populate the JIT callback table with `arcRuntimeSolver*` function pointers when JIT binding is enabled.
- Modify `tools/arcilator/arcilator.cpp`: register solver runtime symbols with the MLIR ExecutionEngine.
- Create `test/arcilator/randomize-solver-aot.mlir`: minimal arcilator LLVM-emission and native executable link/run test for `arcRuntimeSolver*`.
- Create `test/arcilator/randomize-solver-jit.mlir`: minimal JIT test for the same solver ABI.
- Create `test/arcilator/randomize-helper-aot.mlir`: minimal harness with `func.func @main` calling `__circt_randomize_Packet`, with no `llhd.process`.
- Update `docs/SystemVerilogConstraintSolving.md`: record that AOT executable is the primary demo target and JIT is secondary.
- Update `docs/superpowers/plans/2026-06-27-sv-constraint-solving-arcilator.md`: replace the stale `llhd.process` adapter idea with the no-`initial`, AOT-first plan.

## Design Choice

Use `arcilator --emit-llvm` plus `clang++` for executable generation:

```sh
arcilator input.mlir --emit-llvm -o %t/model.ll
clang++ %t/model.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/model.exe
%t/model.exe
```

Do not add an `arcilator --emit-executable` option in this milestone.

**Benefits:**
- Keeps arcilator driver changes small.
- Exercises the real AOT linking surface.
- Keeps generated executable support independent of JIT.
- Avoids committing to a larger driver/linker abstraction.

**Costs:**
- Lit needs stable substitutions for compiler/runtime/Bitwuzla link flags.
- The executable test must be gated behind a Bitwuzla-enabled feature.
- A future `--emit-executable` convenience path may still be useful, but it is not needed for the first working demo.

---

### Task 1: Expose Bitwuzla AOT Test Substitutions

**Files:**
- Modify: `test/lit.site.cfg.py.in`
- Modify: `test/lit.cfg.py`
- Test: `ninja -C build-bitwuzla -j14 check-circt`

**Interfaces:**
- Consumes:
  - `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE` CMake option.
  - Existing CMake variables `HOST_CXX`, `HOST_LDFLAGS`, `LLVM_LIBRARY_OUTPUT_INTDIR`.
- Produces:
  - lit feature `bitwuzla-randomize`.
  - lit substitution `%host_cxx`.
  - lit substitution `%circt_arc_runtime_lib`.
  - lit substitution `%bitwuzla_randomize_link_flags`.

- [x] **Step 1: Add CMake values to lit site config**

  In `test/lit.site.cfg.py.in`, after `config.libfst_enabled = @CIRCT_LIBFST_ENABLED@`, add:

  ```python
  config.arc_bitwuzla_randomize_enabled = @CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE@
  config.circt_arc_runtime_lib = "@LLVM_LIBRARY_OUTPUT_INTDIR@/libCIRCTArcRuntime.a"
  config.bitwuzla_randomize_link_flags = "@CIRCT_ARC_RANDOMIZE_TEST_LINK_FLAGS@"
  ```

- [x] **Step 2: Define the CMake link flag string**

  In top-level `CMakeLists.txt`, after Bitwuzla discovery, set a test-only link flag variable:

  ```cmake
  set(CIRCT_ARC_RANDOMIZE_TEST_LINK_FLAGS "")
  if(CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE)
    if(BitwuzlaPkg_FOUND)
      string(REPLACE ";" " " CIRCT_ARC_RANDOMIZE_TEST_LINK_FLAGS
        "${BitwuzlaPkg_LDFLAGS}")
    elseif(Bitwuzla_LIBRARY)
      set(CIRCT_ARC_RANDOMIZE_TEST_LINK_FLAGS "${Bitwuzla_LIBRARY}")
    endif()
  endif()
  ```

  The current local `/usr/local` install is expected to satisfy the pkg-config
  path. If the fallback `Bitwuzla_LIBRARY` path is used and the executable link
  fails on transitive static dependencies, stop and switch the build to the
  `/usr/local` pkg-config discovery path instead of hard-coding dependency
  guesses into CIRCT.

- [x] **Step 3: Add lit feature and substitutions**

  In `test/lit.cfg.py`, after the `libfst` feature block, add:

  ```python
  if config.arc_bitwuzla_randomize_enabled:
    config.available_features.add('bitwuzla-randomize')
    config.substitutions.append(('%host_cxx', config.host_cxx))
    config.substitutions.append(
        ('%circt_arc_runtime_lib', config.circt_arc_runtime_lib))
    config.substitutions.append(
        ('%bitwuzla_randomize_link_flags',
         config.bitwuzla_randomize_link_flags))
  ```

- [x] **Step 4: Build just the lit config dependencies**

  Run:

  ```sh
  ninja -C build-bitwuzla -j14 arcilator
  ```

  Expected: CMake regenerates the lit config if needed, then `arcilator`
  remains up to date or relinks.

- [x] **Step 5: Verify lit sees the feature**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator --show-suites
  ```

  Expected: no Python config error. If the command does not print available features, run one temporary local `.test` file manually and remove it before commit.

- [x] **Step 6: Commit**

  ```sh
  git add CMakeLists.txt test/lit.site.cfg.py.in test/lit.cfg.py
  git commit -m "[test] Expose Bitwuzla randomize link flags to lit"
  ```

### Task 2: Add an AOT Solver Runtime Smoke Test

**Files:**
- Create: `test/arcilator/randomize-solver-aot.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir`

**Interfaces:**
- Consumes:
  - `clang++` available on `PATH`.
  - `%circt_arc_runtime_lib`
  - `%bitwuzla_randomize_link_flags`
  - `arcRuntimeSolver*` runtime ABI.
- Produces:
  - A native executable smoke test proving AOT code can call the solver runtime.

- [x] **Step 1: Write the failing AOT smoke test**

  Create `test/arcilator/randomize-solver-aot.mlir`:

  ```mlir
  // REQUIRES: bitwuzla-randomize
  // RUN: rm -rf %t && mkdir -p %t
  // RUN: arcilator %s --emit-llvm --no-runtime -o %t/solver.ll
  // RUN: clang++ %t/solver.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/solver.exe
  // RUN: %t/solver.exe

  module {
    llvm.func @arcRuntimeSolverCreate() -> !llvm.ptr
    llvm.func @arcRuntimeSolverDestroy(!llvm.ptr)
    llvm.func @arcRuntimeSolverBvVar(!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
    llvm.func @arcRuntimeSolverBvConst(!llvm.ptr, i64, i32) -> !llvm.ptr
    llvm.func @arcRuntimeSolverSgt(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
    llvm.func @arcRuntimeSolverAssert(!llvm.ptr, !llvm.ptr)
    llvm.func @arcRuntimeSolverCheck(!llvm.ptr) -> i1

    llvm.func @main() -> i32 {
      %c0_i64 = llvm.mlir.constant(0 : i64) : i64
      %c8_i32 = llvm.mlir.constant(8 : i32) : i32
      %true = llvm.mlir.constant(true) : i1
      %name = llvm.mlir.addressof @name : !llvm.ptr
      %solver = llvm.call @arcRuntimeSolverCreate() : () -> !llvm.ptr
      %x = llvm.call @arcRuntimeSolverBvVar(%solver, %name, %c8_i32) : (!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
      %zero = llvm.call @arcRuntimeSolverBvConst(%solver, %c0_i64, %c8_i32) : (!llvm.ptr, i64, i32) -> !llvm.ptr
      %gt = llvm.call @arcRuntimeSolverSgt(%solver, %x, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
      llvm.call @arcRuntimeSolverAssert(%solver, %gt) : (!llvm.ptr, !llvm.ptr) -> ()
      %ok = llvm.call @arcRuntimeSolverCheck(%solver) : (!llvm.ptr) -> i1
      llvm.call @arcRuntimeSolverDestroy(%solver) : (!llvm.ptr) -> ()
      %fail = llvm.xor %ok, %true : i1
      %ret = llvm.zext %fail : i1 to i32
      llvm.return %ret : i32
    }

    llvm.mlir.global internal constant @name("x\00") : !llvm.array<2 x i8>
  }
  ```

  Keep the behavior exactly this small: create a solver, assert `x > 0`,
  return `0` on SAT, return `1` otherwise.

- [x] **Step 2: Run the test and observe the current failure**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir
  ```

  Expected before fixes: either LLVM dialect syntax adjustment is needed, or native link fails because test substitutions/link flags are incomplete.

- [x] **Step 3: Fix only the test harness syntax/link flags**

  Keep the test semantically minimal. Do not add arcilator driver features. Do not add `initial` or `llhd.process`.

- [x] **Step 4: Verify**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir
  ```

  Expected: PASS.

- [x] **Step 5: Commit**

  ```sh
  git add test/arcilator/randomize-solver-aot.mlir
  git commit -m "[arcilator] Test AOT constraint solver runtime calls"
  ```

### Task 3: Bind Solver Runtime Symbols for JIT

**Files:**
- Modify: `include/circt/Dialect/Arc/Runtime/JITBind.h`
- Modify: `lib/Dialect/Arc/Runtime/ArcRuntime.cpp`
- Modify: `tools/arcilator/arcilator.cpp`
- Create: `test/arcilator/randomize-solver-jit.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-jit.mlir`

**Interfaces:**
- Consumes:
  - Existing `runtime::APICallbacks`.
  - Existing helper `bindExecutionEngineSymbol`.
  - Existing `arcRuntimeSolver*` ABI.
- Produces:
  - JIT registration for all solver runtime functions.

- [ ] **Step 1: Add the failing JIT smoke test**

  Create `test/arcilator/randomize-solver-jit.mlir`:

  ```mlir
  // REQUIRES: bitwuzla-randomize
  // RUN: arcilator %s --run --no-runtime --jit-entry=main

  module {
    llvm.func @arcRuntimeSolverCreate() -> !llvm.ptr
    llvm.func @arcRuntimeSolverDestroy(!llvm.ptr)
    llvm.func @arcRuntimeSolverBvVar(!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
    llvm.func @arcRuntimeSolverBvConst(!llvm.ptr, i64, i32) -> !llvm.ptr
    llvm.func @arcRuntimeSolverSgt(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
    llvm.func @arcRuntimeSolverAssert(!llvm.ptr, !llvm.ptr)
    llvm.func @arcRuntimeSolverCheck(!llvm.ptr) -> i1

    llvm.func @main() {
      // Same body as the AOT smoke test, but return void because arcilator JIT
      // packed invocation currently ignores process exit status.
      llvm.return
    }
  }
  ```

  During implementation, copy the working solver body from Task 2 and replace the final integer return with `llvm.return`.

- [ ] **Step 2: Run the test and confirm the symbol failure**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-jit.mlir
  ```

  Expected before implementation: JIT lookup or execution fails on unresolved `arcRuntimeSolver*`.

- [ ] **Step 3: Extend `APICallbacks`**

  In `include/circt/Dialect/Arc/Runtime/JITBind.h`, add fields:

  ```c++
  bool (*fnSolverIsAvailable)();
  void *(*fnSolverCreate)();
  void (*fnSolverDestroy)(void *solver);
  void *(*fnSolverBvVar)(void *solver, const char *name, uint32_t width);
  void *(*fnSolverBvConst)(void *solver, uint64_t value, uint32_t width);
  void *(*fnSolverEq)(void *solver, void *lhs, void *rhs);
  void *(*fnSolverSgt)(void *solver, void *lhs, void *rhs);
  void (*fnSolverAssert)(void *solver, void *term);
  bool (*fnSolverCheck)(void *solver);
  uint64_t (*fnSolverGetBv)(void *solver, void *term);
  ```

  Add matching symbol names:

  ```c++
  static constexpr char symNameSolverIsAvailable[] =
      "arcRuntimeSolverIsAvailable";
  static constexpr char symNameSolverCreate[] = "arcRuntimeSolverCreate";
  static constexpr char symNameSolverDestroy[] = "arcRuntimeSolverDestroy";
  static constexpr char symNameSolverBvVar[] = "arcRuntimeSolverBvVar";
  static constexpr char symNameSolverBvConst[] = "arcRuntimeSolverBvConst";
  static constexpr char symNameSolverEq[] = "arcRuntimeSolverEq";
  static constexpr char symNameSolverSgt[] = "arcRuntimeSolverSgt";
  static constexpr char symNameSolverAssert[] = "arcRuntimeSolverAssert";
  static constexpr char symNameSolverCheck[] = "arcRuntimeSolverCheck";
  static constexpr char symNameSolverGetBv[] = "arcRuntimeSolverGetBv";
  ```

- [ ] **Step 4: Populate callbacks**

  In `lib/Dialect/Arc/Runtime/ArcRuntime.cpp`, include the solver header:

  ```c++
  #include "circt/Dialect/Arc/Runtime/ConstraintSolver.h"
  ```

  Extend `apiCallbacksGlobal`:

  ```c++
  static const APICallbacks apiCallbacksGlobal{
      &arcRuntimeIR_allocInstance,
      &arcRuntimeIR_deleteInstance,
      &arcRuntimeIR_onEval,
      &arcRuntimeIR_onInitialized,
      &arcRuntimeIR_format,
      &arcRuntimeIR_swapTraceBuffer,
      &arcRuntimeSolverIsAvailable,
      &arcRuntimeSolverCreate,
      &arcRuntimeSolverDestroy,
      &arcRuntimeSolverBvVar,
      &arcRuntimeSolverBvConst,
      &arcRuntimeSolverEq,
      &arcRuntimeSolverSgt,
      &arcRuntimeSolverAssert,
      &arcRuntimeSolverCheck,
      &arcRuntimeSolverGetBv};
  ```

- [ ] **Step 5: Register symbols in arcilator**

  In `tools/arcilator/arcilator.cpp`, inside `bindArcRuntimeSymbols`, add one `bindExecutionEngineSymbol` call for each new solver symbol.

  Example:

  ```c++
  bindExecutionEngineSymbol(symbolMap, interner,
                            runtimeCallbacks.symNameSolverCreate,
                            runtimeCallbacks.fnSolverCreate);
  ```

  Register all ten solver functions, including `arcRuntimeSolverIsAvailable`.

- [ ] **Step 6: Format**

  Run:

  ```sh
  clang-format -i include/circt/Dialect/Arc/Runtime/JITBind.h lib/Dialect/Arc/Runtime/ArcRuntime.cpp tools/arcilator/arcilator.cpp
  ```

- [ ] **Step 7: Build**

  Run:

  ```sh
  ninja -C build-bitwuzla -j14 arcilator
  ```

  Expected: `build-bitwuzla/bin/arcilator` relinks.

- [ ] **Step 8: Verify**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-jit.mlir
  ```

  Expected: PASS.

- [ ] **Step 9: Commit**

  ```sh
  git add include/circt/Dialect/Arc/Runtime/JITBind.h lib/Dialect/Arc/Runtime/ArcRuntime.cpp tools/arcilator/arcilator.cpp test/arcilator/randomize-solver-jit.mlir
  git commit -m "[arcilator] Bind constraint solver symbols for JIT"
  ```

### Task 4: Add a Non-Initial Randomize Helper Harness

**Files:**
- Create: `test/arcilator/randomize-helper-aot.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-aot.mlir`

**Interfaces:**
- Consumes:
  - Generated-helper shape from `test/Conversion/MooreToCore/randomize-call.mlir`.
  - AOT executable link substitutions from Task 1.
- Produces:
  - A native executable test that calls `__circt_randomize_Packet` from `func.func @main` without `llhd.process`.

- [x] **Step 1: Generate a working helper baseline**

  Run:

  ```sh
  build-bitwuzla/bin/circt-opt --convert-moore-to-core \
    test/Conversion/MooreToCore/randomize-call.mlir \
    -o /tmp/randomize-core.mlir
  ```

  Expected: `/tmp/randomize-core.mlir` contains `func.func private @__circt_randomize_Packet`.

- [x] **Step 2: Create the harness test**

  Create `test/arcilator/randomize-helper-aot.mlir` by copying the generated helper functions and class storage type from `/tmp/randomize-core.mlir`, but replacing the `hw.module` and `llhd.process` section with a plain `func.func @main() -> i32`.

  The harness body must:

  ```mlir
  // Allocate Packet storage.
  // Initialize the typeinfo pointer exactly like the generated object allocation path.
  // Call @__circt_randomize_Packet(%packet).
  // Return 0 if the helper returns true.
  // Return 1 if the helper returns false.
  ```

  Keep the test free of `moore.procedure`, `llhd.process`, and SV `initial`.

- [x] **Step 3: Add AOT RUN lines**

  At the top of `test/arcilator/randomize-helper-aot.mlir`, add:

  ```mlir
  // REQUIRES: bitwuzla-randomize
  // RUN: rm -rf %t && mkdir -p %t
  // RUN: arcilator %s --emit-llvm --no-runtime -o %t/randomize.ll
  // RUN: clang++ %t/randomize.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/randomize.exe
  // RUN: %t/randomize.exe
  ```

- [x] **Step 4: Run and fix only arcilator-compatible issues**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-aot.mlir
  ```

  Expected before final fixes: failures may expose missing LLVM lowering for the copied helper shape. Fix those by narrowing the helper/harness or by fixing existing randomize lowering, not by adding `initial` support.

- [x] **Step 5: Verify no `llhd.process` dependency remains**

  Run:

  ```sh
  rg -n "llhd.process|moore.procedure|initial" test/arcilator/randomize-helper-aot.mlir
  ```

  Expected: no matches.

- [x] **Step 6: Commit**

  ```sh
  git add test/arcilator/randomize-helper-aot.mlir
  git commit -m "[arcilator] Add AOT randomize helper execution demo"
  ```

### Task 5: Add a JIT Randomize Helper Smoke Test

**Files:**
- Create: `test/arcilator/randomize-helper-jit.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-jit.mlir`

**Interfaces:**
- Consumes:
  - JIT solver symbol binding from Task 3.
  - The non-initial helper harness from Task 4.
- Produces:
  - A JIT smoke test for the same helper/harness.

- [ ] **Step 1: Copy the AOT helper harness**

  Create `test/arcilator/randomize-helper-jit.mlir` by copying `test/arcilator/randomize-helper-aot.mlir`.

- [ ] **Step 2: Replace RUN lines**

  Replace the AOT RUN lines with:

  ```mlir
  // REQUIRES: bitwuzla-randomize
  // RUN: arcilator %s --run --no-runtime --jit-entry=main
  ```

- [ ] **Step 3: Adjust `@main` return shape if needed**

  If arcilator JIT rejects `@main() -> i32`, add a wrapper:

  ```mlir
  func.func @main() {
    %rc = func.call @main_impl() : () -> i32
    func.return
  }
  ```

  Keep the AOT test using `main_impl` or `main` with an integer process exit status. Keep the JIT test focused on successful execution, not process return code.

- [ ] **Step 4: Verify**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-jit.mlir
  ```

  Expected: PASS.

- [ ] **Step 5: Commit**

  ```sh
  git add test/arcilator/randomize-helper-jit.mlir
  git commit -m "[arcilator] Add JIT randomize helper execution smoke test"
  ```

### Task 6: Document the Correct Execution Boundary

**Files:**
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Modify: `docs/superpowers/plans/2026-06-27-sv-constraint-solving-arcilator.md`
- Test: documentation-only diff review.

**Interfaces:**
- Consumes:
  - Passing AOT helper demo from Task 4.
  - Passing JIT helper smoke test from Task 5.
- Produces:
  - Updated docs stating that executable generation is primary and JIT is secondary.
  - Updated docs stating that SV `initial` remains unsupported for arcilator.

- [ ] **Step 1: Update user-facing docs**

  In `docs/SystemVerilogConstraintSolving.md`, add a section named `Arcilator Execution Demo Status`:

  ````markdown
  ## Arcilator Execution Demo Status

  The first executable demo does not rely on SystemVerilog `initial` blocks.
  Arcilator remains focused on its existing simulation boundary and does not
  grow generic `llhd.process` support for this feature.

  The primary execution path is AOT:

  ```sh
  arcilator input.mlir --emit-llvm --no-runtime -o model.ll
  clang++ model.ll libCIRCTArcRuntime.a <bitwuzla link flags> -fuse-ld=lld -o model.exe
  ./model.exe
  ```

  JIT uses the same `arcRuntimeSolver*` ABI and is covered as a smoke test.
  ````

- [ ] **Step 2: Update the long plan**

  In `docs/superpowers/plans/2026-06-27-sv-constraint-solving-arcilator.md`:

  - Remove or mark obsolete any wording suggesting a `llhd.process` adapter.
  - State that the previous `state-lowering` failure came from an unsuitable `initial`-based spike input.
  - Add the new AOT-first plan file path.
  - Mark the next task as "AOT randomize helper executable demo".

- [ ] **Step 3: Review docs diff**

  Run:

  ```sh
  git diff -- docs/SystemVerilogConstraintSolving.md docs/superpowers/plans/2026-06-27-sv-constraint-solving-arcilator.md
  ```

  Expected: docs describe no-`initial`, AOT-first execution.

- [ ] **Step 4: Commit**

  ```sh
  git add docs/SystemVerilogConstraintSolving.md docs/superpowers/plans/2026-06-27-sv-constraint-solving-arcilator.md
  git commit -m "[Docs] Document AOT randomize execution boundary"
  ```

## Verification Matrix

- `ninja -C build-bitwuzla -j14 arcilator`
  - Verifies arcilator relinks with JIT solver binding.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir`
  - Verifies native executable link and run for direct solver runtime calls.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-jit.mlir`
  - Verifies JIT solver symbol binding.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-aot.mlir`
  - Verifies native executable run for generated randomize helper.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-jit.mlir`
  - Verifies JIT run for generated randomize helper.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator`
  - Verifies all arcilator tests in the Bitwuzla-enabled build.

## Risks and Mitigations

- **Risk:** Bitwuzla static link requires transitive dependencies not visible through `Bitwuzla_LIBRARY`.
  - **Mitigation:** Prefer pkg-config link flags for tests when available. If only `find_library` succeeds, document and append the exact transitive flags discovered from the failing link line.
- **Risk:** LLVM dialect hand-written smoke tests are brittle.
  - **Mitigation:** Keep Task 2 as small as possible and reuse its working body for JIT. The helper demo is the real behavior test.
- **Risk:** JIT `@main() -> i32` return handling differs from native executable `main`.
  - **Mitigation:** Use `@main_impl() -> i32` plus a void `@main()` wrapper for JIT if needed.
- **Risk:** Helper harness must know object storage layout.
  - **Mitigation:** Generate the helper baseline from the existing MooreToCore test and copy only the minimum storage setup needed by the generated helper. Do not generalize object allocation in this milestone.
- **Risk:** The first demo does not start from SV.
  - **Mitigation:** This is intentional because the current `build-bitwuzla` cache lacks `circt-verilog`. The first milestone proves runtime execution; SV-to-harness integration is a later task.

## Explicit Non-Goals

- No SV `initial` support in arcilator.
- No generic LLHD process lowering.
- No `arcilator --emit-executable` driver option yet.
- No dynamic arrays, queues, multidimensional arrays, strings, reals, handles, or object graph constraints.
- No inline `with` constraints.
- No `rand_mode` / `constraint_mode` runtime semantics in this milestone.
- No deterministic seed/replay work in this milestone.

## Commit Order

1. `[test] Expose Bitwuzla randomize link flags to lit`
2. `[arcilator] Test AOT constraint solver runtime calls`
3. `[arcilator] Bind constraint solver symbols for JIT`
4. `[arcilator] Add AOT randomize helper execution demo`
5. `[arcilator] Add JIT randomize helper execution smoke test`
6. `[Docs] Document AOT randomize execution boundary`

## Success Definition

The milestone is done when a Bitwuzla-enabled `build-bitwuzla` can run:

```sh
ninja -C build-bitwuzla -j14 arcilator
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-aot.mlir
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-jit.mlir
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-helper-jit.mlir
```

and all four tests pass without adding arcilator support for SV `initial` or generic `llhd.process`.
