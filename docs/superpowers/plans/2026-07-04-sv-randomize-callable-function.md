# SV Randomize Callable Function Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the hand-written Moore harness from the randomize AOT demo by generating `randomize_packet()` from SystemVerilog.

**Architecture:** Keep the demo small and AOT-only. First prove exactly where the source-level flow fails; only then fix the smallest lowering gap. The shell script should eventually only run tools and link the C++ testbench.

**Tech Stack:** `circt-verilog --ir-moore`, SystemVerilog functions, DPI-C export metadata, `circt-opt --convert-moore-to-core`, arcilator `--emit-llvm`, C++ testbench, `CIRCTArcRuntime`, Bitwuzla, clang++/lld, lit.

## Global Constraints

- Do not add SV `initial` support to arcilator.
- Do not add generic `llhd.process` lowering.
- Do not add a MooreToCore entry-generation pass option.
- Do not add `arcilator --emit-executable`.
- Do not keep hand-written Moore MLIR in `run.sh` after the source-level path works.
- Keep the demo to one SV input, one C++ testbench, one shell script, and one lit wrapper.

---

## Current Finding

The frontend already imports this shape:

```systemverilog
function int randomize_packet();
  automatic Packet p = new;
  bit ok;
  ok = p.randomize();
  return ok ? 0 : 1;
endfunction

export "DPI-C" randomize_packet = function randomize_packet;
```

It produces a public `func.func @randomize_packet` with
`circt.dpi.export = "randomize_packet"` and a `moore.class.randomize` op.

The current blocker is not DPI export. The blocker is MooreToCore lowering of
class-handle storage produced by source SV:

- A function-local `Packet p = new;` lowers through `moore.variable` into
  `llhd.sig`, `llhd.drv`, and `llhd.constant_time`.
- A compilation-unit `Packet p = new;` lowers into `llhd.global_signal` with an
  initializer that arcilator AOT does not accept.

The existing demo works only because `run.sh` appends SSA-style Moore IR:

```mlir
%object = moore.class.new : !moore.class<@Packet>
%ok = moore.class.randomize %object : !moore.class<@Packet>
```

The real fix should make source-generated class handle storage lower cleanly for
this AOT function case.

## File Structure

- Modify `test/Conversion/ImportVerilog/randomize-flow.sv`
  - Add checks proving the SV callable function imports correctly.
- Modify `test/Conversion/MooreToCore/randomize-call.mlir` or add a focused new
  MooreToCore test
  - Capture the class-handle local variable pattern that currently leaves LLHD
    ops in a `func.func`.
- Modify `lib/Conversion/MooreToCore/MooreToCore.cpp`
  - Lower this class-handle local variable case without LLHD signal ops.
- Modify `test/arcilator/Inputs/randomize-sv-cpp-aot/packet.sv`
  - Add the SV `randomize_packet()` function only after the lowering test is
    green.
- Modify `test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
  - Delete the `sed`/`cat` Moore harness once `packet.sv` owns the entry.
- Modify `docs/SystemVerilogConstraintSolving.md`
  - Record that the demo no longer appends Moore IR.

---

### Task 1: Lock Down Frontend Callable Function Import

**Files:**
- Modify: `test/Conversion/ImportVerilog/randomize-flow.sv`
- Test: `build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv`

**Interfaces:**
- Produces: public `func.func @randomize_packet` with
  `circt.dpi.export = "randomize_packet"`.
- Produces: `moore.class.new` and `moore.class.randomize` inside that function.

- [x] **Step 1: Add the SV callable function**

  Add this after the `Packet` class:

  ```systemverilog
  function int randomize_packet();
    automatic Packet p = new;
    bit ok;
    ok = p.randomize();
    return ok ? 0 : 1;
  endfunction

  export "DPI-C" randomize_packet = function randomize_packet;
  ```

- [x] **Step 2: Add frontend checks**

  Add these checks:

  ```text
  // IMPORT-LABEL: func.func @randomize_packet
  // IMPORT-SAME: circt.dpi.export = "randomize_packet"
  // IMPORT: moore.class.new
  // IMPORT: moore.class.randomize
  ```

- [x] **Step 3: Run the test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv
  ```

  Expected: PASS for the import checks. If it fails here, fix only the frontend
  import/export issue shown by the failure.

### Task 2: Lower Source-Generated Class Handle Locals Without LLHD Ops

**Files:**
- Create or modify: `test/Conversion/MooreToCore/randomize-call.mlir`
- Modify: `lib/Conversion/MooreToCore/MooreToCore.cpp`
- Test: `build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/randomize-call.mlir`

**Interfaces:**
- Consumes: a `func.func` containing `moore.class.new`, a local class handle
  variable, `moore.read`, and `moore.class.randomize`.
- Produces: Core/LLVM-compatible IR without `llhd.sig`, `llhd.drv`,
  `llhd.prb`, or `llhd.constant_time` in the function.

- [x] **Step 1: Add the failing MooreToCore check**

  Add a focused function to `test/Conversion/MooreToCore/randomize-call.mlir`:

  ```mlir
  func.func @randomize_packet_from_local() -> !moore.i32 {
    %one = moore.constant 1 : i32
    %zero = moore.constant 0 : i32
    %object = moore.class.new : !moore.class<@Packet>
    %p = moore.variable : !moore.ref<!moore.class<@Packet>>
    moore.blocking_assign %p, %object : !moore.class<@Packet>
    %read = moore.read %p : !moore.ref<!moore.class<@Packet>>
    %ok = moore.class.randomize %read : !moore.class<@Packet>
    %ok_i32 = moore.sext %ok : i1 -> i32
    %ok_i1 = moore.trunc %ok_i32 : i32 -> i1
    %ret = moore.conditional %ok_i1 : i1 -> i32 {
      moore.yield %zero : i32
    } {
      moore.yield %one : i32
    }
    return %ret : !moore.i32
  }
  ```

  Add checks:

  ```text
  // CHECK-LABEL: func.func @randomize_packet_from_local
  // CHECK: call @malloc
  // CHECK: call @__circt_randomize_Packet
  // CHECK-NOT: llhd.sig
  // CHECK-NOT: llhd.drv
  // CHECK-NOT: llhd.prb
  // CHECK-NOT: llhd.constant_time
  ```

- [x] **Step 2: Run the test and confirm failure**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/randomize-call.mlir
  ```

  Expected: FAIL because local class handle storage currently lowers to LLHD
  signal operations.

- [x] **Step 3: Implement the smallest MooreToCore lowering fix**

  In `lib/Conversion/MooreToCore/MooreToCore.cpp`, special-case function-local
  `moore.variable` storage for class handles so it lowers to ordinary
  function-local storage or direct SSA value flow, not LLHD signal ops.

  Keep the scope narrow:

  - Only handle function-local class handle variables.
  - Do not change module/procedure signal semantics.
  - Do not add general SV `initial` or `llhd.process` support.

- [x] **Step 4: Run the MooreToCore test**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/randomize-call.mlir
  ```

  Expected: PASS.

### Task 3: Remove the Demo's Hand-Written Moore Harness

**Files:**
- Modify: `test/arcilator/Inputs/randomize-sv-cpp-aot/packet.sv`
- Modify: `test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
- Test: `test/arcilator/randomize-sv-cpp-aot.sv`

**Interfaces:**
- Consumes: source-generated `randomize_packet()`.
- Produces: native executable linked with `testbench.cpp`.

- [x] **Step 1: Add the source entry to `packet.sv`**

  Final file:

  ```systemverilog
  class Packet;
    rand int len;
    constraint c { len > 0; }
  endclass

  function int randomize_packet();
    automatic Packet p = new;
    bit ok;
    ok = p.randomize();
    return ok ? 0 : 1;
  endfunction

  export "DPI-C" randomize_packet = function randomize_packet;
  ```

- [x] **Step 2: Delete MLIR splicing from `run.sh`**

  Replace the `circt-verilog`/`sed`/`cat`/`circt-opt` block with:

  ```sh
  "$circt_verilog" --ir-moore "$script_dir/packet.sv" \
    -o "$OUT_DIR/randomize-moore.mlir"

  "$circt_opt" --convert-moore-to-core "$OUT_DIR/randomize-moore.mlir" \
    -o "$OUT_DIR/randomize-core.mlir"
  ```

- [x] **Step 3: Run the manual demo**

  Run:

  ```sh
  OUT_DIR=/tmp/circt-randomize-sv-cpp-aot-demo \
    test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh
  ```

  Expected output contains:

  ```text
  randomize demo executable passed: /tmp/circt-randomize-sv-cpp-aot-demo/randomize.exe
  ```

- [x] **Step 4: Run the lit wrapper and arcilator subset**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv
  build-bitwuzla/bin/llvm-lit -a test/arcilator
  ```

  Expected: PASS.

### Task 4: Update Status Docs

**Files:**
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Test: `git diff -- docs/SystemVerilogConstraintSolving.md`

**Interfaces:**
- Consumes: passing source-level callable function demo.
- Produces: docs that say the script no longer appends Moore IR.

- [x] **Step 1: Update the demo status paragraph**

  Use this wording:

  ```markdown
  The current executable demo path now covers `SV -> Moore MLIR -> MooreToCore
  -> arcilator LLVM -> C++ testbench -> native executable`. The SV file owns the
  class, constraint, object allocation, `randomize()` call, and exported
  `randomize_packet()` entry function. The shell script only invokes tools and
  links the C++ testbench; it no longer appends hand-written Moore MLIR.
  ```

- [x] **Step 2: Run docs diff**

  Run:

  ```sh
  git diff -- docs/SystemVerilogConstraintSolving.md
  ```

  Expected: diff mentions source-generated `randomize_packet()`.

## Verification Matrix

- `build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv`
- `build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/randomize-call.mlir`
- `OUT_DIR=/tmp/circt-randomize-sv-cpp-aot-demo test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv`
- `build-bitwuzla/bin/llvm-lit -a test/arcilator`
