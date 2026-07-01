# Arcilator Randomize AOT Lowering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove a regular MooreToCore-to-arcilator AOT flow can build and run a randomize executable without hand-copying the generated helper.

**Architecture:** Keep the executable harness in the test input as a normal `func.func @main`; let existing MooreToCore lowering handle `moore.class.new`, `moore.class.randomize`, and generated solver helpers. Add no pass options unless this direct path fails for a concrete lowering limitation.

**Tech Stack:** CIRCT MooreToCore conversion, arcilator `--emit-llvm`, Arc runtime, Bitwuzla C API, clang++/lld, lit/FileCheck.

## Global Constraints

- Do not implement or rely on arcilator support for SV `initial`.
- Do not add generic `llhd.process` lowering.
- Do not add JIT support in this plan.
- Do not add an `arcilator --emit-executable` driver option in this plan.
- Do not add MooreToCore pass options for demo-only entry generation unless Task 1 proves the direct harness cannot work.
- Keep Bitwuzla optional through `CIRCT_ARC_ENABLE_BITWUZLA_RANDOMIZE`.
- Keep the development build in `build-bitwuzla`.
- Use `ninja -C build-bitwuzla -j14 ...` at most.
- The first regular lowering demo starts from Moore MLIR, not SV, because the current `build-bitwuzla` cache does not build `circt-verilog`.

---

## Current State

- `test/arcilator/randomize-solver-aot.mlir` proves LLVM emitted by arcilator can link against `CIRCTArcRuntime` and Bitwuzla.
- `test/arcilator/randomize-helper-aot.mlir` proves a native executable can call a generated-style randomize helper, but the helper body is hand-copied.
- `test/Conversion/MooreToCore/randomize-call.mlir` proves MooreToCore already lowers `moore.class.randomize` to a generated helper call.
- Existing MooreToCore tests already use `moore.class.new` inside `func.func`, so a normal `func.func @main` harness is the shortest path.

## File Structure

- Create `test/arcilator/randomize-lowering-aot.mlir`: regular Moore IR input with class declaration plus `func.func @main`.
- Delete `test/arcilator/randomize-helper-aot.mlir` after the new regular lowering test passes.
- Modify `docs/SystemVerilogConstraintSolving.md`: record the current AOT demo status.

## Interfaces

No new production interface is planned.

The test input owns the harness:

```mlir
func.func @main() -> i32 {
  %object = moore.class.new : !moore.class<@Packet>
  %ok = moore.class.randomize %object : !moore.class<@Packet>
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  %ret = arith.select %ok, %zero, %one : i32
  return %ret : i32
}
```

MooreToCore should lower this to:

- generated `func.func private @__circt_randomize_Packet`;
- lowered allocation/typeinfo initialization from `moore.class.new`;
- call to `@__circt_randomize_Packet`;
- no `moore.procedure`;
- no `llhd.process`;
- no SV `initial`.

---

### Task 1: Add the Regular Lowering AOT Executable Test

**Files:**
- Create: `test/arcilator/randomize-lowering-aot.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir`

**Interfaces:**
- Consumes existing `moore.class.new` and `moore.class.randomize` lowering.
- Produces one runnable executable test for MooreToCore -> arcilator -> clang++/lld.

- [ ] **Step 1: Write the failing/pass-through e2e test**

  Create `test/arcilator/randomize-lowering-aot.mlir`:

  ```mlir
  // REQUIRES: bitwuzla-randomize
  // RUN: rm -rf %t && mkdir -p %t
  // RUN: circt-opt --convert-moore-to-core %s -o %t/randomize-core.mlir
  // RUN: FileCheck %s --input-file=%t/randomize-core.mlir --check-prefix=CORE --implicit-check-not=llhd.process --implicit-check-not=moore.procedure
  // RUN: arcilator %t/randomize-core.mlir --emit-llvm --no-runtime -o %t/randomize.ll
  // RUN: clang++ %t/randomize.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/randomize.exe
  // RUN: %t/randomize.exe

  moore.class.classdecl @Packet {
    moore.class.propertydecl @len : !moore.i32 {isRand}
    moore.class.constraintdecl @c {
    ^bb0(%this: !moore.class<@Packet>):
      %zero = moore.constant 0 : i32
      %ref = moore.class.property_ref %this[@len] : <@Packet> -> !moore.ref<!moore.i32>
      %val = moore.read %ref : <i32>
      %ok = moore.sgt %val, %zero : i32 -> i1
      moore.yield %ok : i1
    }
  }

  func.func @main() -> i32 {
    %object = moore.class.new : !moore.class<@Packet>
    %ok = moore.class.randomize %object : !moore.class<@Packet>
    %zero = arith.constant 0 : i32
    %one = arith.constant 1 : i32
    %ret = arith.select %ok, %zero, %one : i32
    return %ret : i32
  }

  // CORE-LABEL: func.func private @__circt_randomize_Packet
  // CORE: call @arcRuntimeSolverCheck
  // CORE-LABEL: func.func @main() -> i32
  // CORE: call @malloc
  // CORE: call @__circt_randomize_Packet
  // CORE: return
  ```

- [ ] **Step 2: Run the single test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir
  ```

  Expected: PASS if existing lowering already supports the direct harness.

- [ ] **Step 3: If it fails, fix only the concrete lowering gap**

  Use this rule:

  - If `moore.class.new` fails in `func.func`, fix `ClassNewOpConversion`.
  - If `moore.class.randomize` fails in `func.func`, fix `ClassRandomizeOpConversion`.
  - If arcilator rejects the lowered `func.func @main`, fix the smallest arcilator lowering issue shown by the diagnostic.
  - Do not add `randomize-entry-class` unless the failure proves a normal `func.func @main` cannot express the required harness.

- [ ] **Step 4: Re-run the single test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir
  ```

  Expected: PASS.

- [ ] **Step 5: Run the arcilator subset**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator
  ```

  Expected: all arcilator tests PASS in the Bitwuzla-enabled build.

- [ ] **Step 6: Commit**

  ```sh
  git add test/arcilator/randomize-lowering-aot.mlir
  git commit -m "[arcilator] Add randomize lowering AOT demo"
  ```

### Task 2: Retire the Hand-Copied Helper Demo

**Files:**
- Delete: `test/arcilator/randomize-helper-aot.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir`

**Interfaces:**
- Consumes passing `test/arcilator/randomize-lowering-aot.mlir`.
- Produces no duplicated hand-copied generated helper in arcilator tests.

- [ ] **Step 1: Delete the old helper test**

  Delete:

  ```sh
  test/arcilator/randomize-helper-aot.mlir
  ```

- [ ] **Step 2: Verify the replacement test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir
  ```

  Expected: PASS.

- [ ] **Step 3: Verify the direct runtime smoke test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir
  ```

  Expected: PASS.

- [ ] **Step 4: Commit**

  ```sh
  git add test/arcilator/randomize-helper-aot.mlir
  git commit -m "[arcilator] Retire hand-copied randomize helper demo"
  ```

### Task 3: Update Minimal Status Docs

**Files:**
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Test: `git diff -- docs/SystemVerilogConstraintSolving.md`

**Interfaces:**
- Consumes passing regular AOT lowering demo from Task 1.
- Produces docs that say the mainline executable demo uses regular lowering, not hand-copied helper IR or JIT.

- [ ] **Step 1: Update user-facing status**

  In `docs/SystemVerilogConstraintSolving.md`, add or update the arcilator status paragraph:

  ```markdown
  The current executable demo starts from Moore IR with a normal `func.func @main`,
  lowers `moore.class.new` and `moore.class.randomize` through MooreToCore, emits
  LLVM with arcilator, links against `CIRCTArcRuntime` and Bitwuzla, and runs the
  resulting executable. It does not rely on SV `initial`, generic `llhd.process`
  lowering, JIT, or a hand-copied randomize helper.
  ```

- [ ] **Step 2: Review docs diff**

  Run:

  ```sh
  git diff -- docs/SystemVerilogConstraintSolving.md
  ```

  Expected: docs mention regular AOT lowering and say JIT is not part of the current demo.

- [ ] **Step 3: Commit**

  ```sh
  git add docs/SystemVerilogConstraintSolving.md
  git commit -m "[Docs] Document regular randomize AOT demo"
  ```

## Verification Matrix

- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir`
  - Verifies MooreToCore -> arcilator -> executable.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-solver-aot.mlir`
  - Verifies direct solver runtime executable smoke test still works.
- `build-bitwuzla/bin/llvm-lit -a test/arcilator`
  - Verifies the arcilator subset in the Bitwuzla-enabled build.

## Fallback Only If Task 1 Fails

Do not start here.

If a normal `func.func @main` cannot express the demo after fixing the concrete failure, add the narrowest temporary hook:

- one MooreToCore option: `randomize-entry-class`;
- fixed generated entry name: `main`;
- no `randomize-entry-name`;
- one positive test;
- one missing-class diagnostic if needed.

Delete this fallback once SV/front-end integration can generate an equivalent `func.func @main`.

## Explicit Non-Goals

- No `randomize-entry-name`.
- No custom entry naming.
- No JIT binding.
- No JIT smoke test.
- No SV `initial` support.
- No generic `llhd.process` lowering.
- No `arcilator --emit-executable` driver option.
- No printed demo output.
- No `rand_mode` / `constraint_mode`.
- No inline `with` constraints.
- No seed/replay.

## Commit Order

1. `[arcilator] Add randomize lowering AOT demo`
2. `[arcilator] Retire hand-copied randomize helper demo`
3. `[Docs] Document regular randomize AOT demo`

## Success Definition

The milestone is done when this command passes:

```sh
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-lowering-aot.mlir
```

The input must start from Moore IR, include a normal `func.func @main`, contain no SV `initial` or `llhd.process`, emit LLVM through arcilator, link a native executable, and run it successfully.
