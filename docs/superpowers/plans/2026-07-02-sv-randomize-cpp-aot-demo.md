# SV Randomize C++ AOT Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **中文:** 给后续 agent 使用：必须使用 `superpowers:executing-plans` 按 task 执行。每一步用 checkbox 追踪。

**Goal:** Build one runnable demo that starts from a SystemVerilog class constraint file, lowers through MLIR, links with a C++ testbench, and runs as a native executable.

**目标:** 做出一个可运行 demo：从 SystemVerilog class constraint 文件开始，经 MLIR lowering，和 C++ testbench 链接，最终生成并运行 native executable。

**Architecture:** Keep the shortest path. The SV file owns only the class and constraint. The lit test appends a tiny Moore harness, generates a tiny C++ testbench in `%t`, lowers through MooreToCore, emits LLVM with arcilator, links with `clang++`/lld plus `CIRCTArcRuntime` and Bitwuzla, and runs the executable.

**架构:** 走最短路径。SV 文件只放 class 和 constraint。lit 测试在 `%t` 里追加一个很薄的 Moore harness，并生成一个很薄的 C++ testbench，然后跑 MooreToCore、arcilator emit LLVM、`clang++`/lld 链接 `CIRCTArcRuntime` 与 Bitwuzla，最后运行 executable。

**Tech Stack:** `circt-verilog --ir-moore`, `circt-opt --convert-moore-to-core`, arcilator `--emit-llvm`, C++ testbench, Arc runtime, Bitwuzla C API, clang++/lld, lit/FileCheck.

**技术栈:** `circt-verilog --ir-moore`、`circt-opt --convert-moore-to-core`、arcilator `--emit-llvm`、C++ testbench、Arc runtime、Bitwuzla C API、clang++/lld、lit/FileCheck。

## Global Constraints

- Do not add SV `initial` support to arcilator.
- 不给 arcilator 增加 SV `initial` 支持。
- Do not add generic `llhd.process` lowering.
- 不增加通用 `llhd.process` lowering。
- Do not add MooreToCore entry-generation pass options.
- 不增加 MooreToCore entry 生成 option。
- Do not add `arcilator --emit-executable`.
- 不增加 `arcilator --emit-executable`。
- Keep Bitwuzla optional through the existing `bitwuzla-randomize` lit feature.
- 继续通过已有 `bitwuzla-randomize` lit feature 控制 Bitwuzla 依赖。
- Use `build-bitwuzla`; build with at most `ninja -C build-bitwuzla -j14 ...`.
- 使用 `build-bitwuzla`；构建命令最多使用 `ninja -C build-bitwuzla -j14 ...`。

---

## Prerequisite / 前置条件

`circt-verilog` must exist in the active build directory.

当前 build 目录里必须有 `circt-verilog`。

- [x] **Step 1: Check tools / 检查工具**

  Run:

  ```sh
  ls build-bitwuzla/bin/circt-verilog build-bitwuzla/bin/circt-opt build-bitwuzla/bin/arcilator
  ```

  Result: all three files exist after enabling the slang frontend in
  `build-bitwuzla`.

  结果：在 `build-bitwuzla` 中启用 slang frontend 后，三个文件都存在。

- [x] **Step 2: Build `circt-verilog` if missing / 如果缺失则构建 `circt-verilog`**

  Run:

  ```sh
  ninja -C build-bitwuzla -j14 circt-verilog
  ```

  Result: `build-bitwuzla/bin/circt-verilog` exists. The build directory was
  reconfigured in place with `-DCIRCT_SLANG_FRONTEND_ENABLED=ON`.

  结果：`build-bitwuzla/bin/circt-verilog` 存在。该 build directory 已原地加入
  `-DCIRCT_SLANG_FRONTEND_ENABLED=ON`。

- [x] **Step 3: Verify existing SV randomize import / 验证已有 SV randomize import**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv
  ```

  Result: PASS.

  结果：PASS。

## Current State / 当前状态

- `test/arcilator/randomize-lowering-aot.mlir` proves `Moore MLIR -> MooreToCore -> arcilator LLVM -> native executable`.
- `test/arcilator/randomize-lowering-aot.mlir` 已证明 `Moore MLIR -> MooreToCore -> arcilator LLVM -> native executable`。
- `test/Conversion/ImportVerilog/randomize-flow.sv` proves `SV -> Moore MLIR -> MooreToCore` for randomize.
- `test/Conversion/ImportVerilog/randomize-flow.sv` 已证明 randomize 的 `SV -> Moore MLIR -> MooreToCore`。
- `test/arcilator/randomize-sv-cpp-aot.sv` now proves `SV -> Moore MLIR -> MooreToCore -> arcilator LLVM -> C++ testbench -> native executable`.
- `test/arcilator/randomize-sv-cpp-aot.sv` 现在已证明 `SV -> Moore MLIR -> MooreToCore -> arcilator LLVM -> C++ testbench -> native executable`。

## File Structure / 文件结构

- Create `test/arcilator/randomize-sv-cpp-aot.sv`: the only new test file. It contains the SV class input and all lit commands, and generates temporary harness/testbench files under `%t`.
- 新增 `test/arcilator/randomize-sv-cpp-aot.sv`：唯一新增测试文件。它包含 SV class 输入和全部 lit 命令，并在 `%t` 下生成临时 harness/testbench 文件。
- Modify `docs/SystemVerilogConstraintSolving.md`: record the completed SV-to-C++ AOT demo once tests pass.
- 修改 `docs/SystemVerilogConstraintSolving.md`：测试通过后记录 SV-to-C++ AOT demo 状态。

## Interfaces / 接口

The temporary Moore harness exports:

临时 Moore harness 导出：

```mlir
func.func @randomize_packet() -> i32
```

The temporary C++ testbench consumes:

临时 C++ testbench 消费：

```cpp
extern "C" int randomize_packet();
```

---

### Task 1: Add the SV-to-C++ AOT lit Demo

**Files:**
- Create: `test/arcilator/randomize-sv-cpp-aot.sv`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv`

**Interfaces:**
- Consumes `circt-verilog --ir-moore`.
- Generates `%t/randomize-cpp-harness.mlir`.
- Generates `%t/randomize-testbench.cpp`.
- Produces one native executable run.

**中文接口:**
- 使用 `circt-verilog --ir-moore`。
- 生成 `%t/randomize-cpp-harness.mlir`。
- 生成 `%t/randomize-testbench.cpp`。
- 产出并运行一个 native executable。

- [x] **Step 1: Write the failing e2e test / 写 failing e2e 测试**

  Create `test/arcilator/randomize-sv-cpp-aot.sv`:

  ```systemverilog
  // REQUIRES: slang
  // REQUIRES: bitwuzla-randomize
  // RUN: rm -rf %t && mkdir -p %t
  // RUN: circt-verilog --ir-moore %s -o %t/randomize-class.mlir
  // RUN: sed '$d' %t/randomize-class.mlir > %t/randomize-with-harness.mlir
  // RUN: printf '%%s\n' '  func.func @randomize_packet() -> i32 {' '    %%object = moore.class.new : !moore.class<@Packet>' '    %%ok = moore.class.randomize %%object : !moore.class<@Packet>' '    %%ok_builtin = moore.to_builtin_int %%ok : i1' '    %%zero = arith.constant 0 : i32' '    %%one = arith.constant 1 : i32' '    %%ret = arith.select %%ok_builtin, %%zero, %%one : i32' '    return %%ret : i32' '  }' '}' >> %t/randomize-with-harness.mlir
  // RUN: printf '%%s\n' 'extern "C" int randomize_packet();' 'int main() { return randomize_packet(); }' > %t/randomize-testbench.cpp
  // RUN: circt-opt --convert-moore-to-core %t/randomize-with-harness.mlir -o %t/randomize-core.mlir
  // RUN: FileCheck %s --input-file=%t/randomize-core.mlir --check-prefix=CORE --implicit-check-not=llhd.process --implicit-check-not=moore.procedure
  // RUN: arcilator %t/randomize-core.mlir --emit-llvm --no-runtime -o %t/randomize.ll
  // RUN: clang++ %t/randomize.ll %t/randomize-testbench.cpp %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/randomize.exe
  // RUN: %t/randomize.exe

  class Packet;
    rand int len;
    constraint c { len > 0; }
  endclass

  // CORE-LABEL: func.func private @__circt_randomize_Packet
  // CORE: call @arcRuntimeSolverCheck
  // CORE-LABEL: func.func @randomize_packet() -> i32
  // CORE: call @malloc
  // CORE: call @__circt_randomize_Packet
  // CORE: return
  ```

- [x] **Step 2: Run the new test / 运行新测试**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv
  ```

  Result: PASS.

  结果：PASS。

- [x] **Step 3: Run the arcilator subset / 运行 arcilator 子集**

  Run:

  ```sh
  build-bitwuzla/bin/llvm-lit -a test/arcilator
  ```

  Result: all 6 arcilator tests PASS.

  结果：6 个 arcilator tests 全部 PASS。

- [x] **Step 4: Commit / 提交**

  Result: batched with the docs update in one final commit.

  结果：与文档更新合并到最后一个提交中。

### Task 2: Update Status Docs

**Files:**
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Test: `git diff -- docs/SystemVerilogConstraintSolving.md`

**Interfaces:**
- Consumes passing `test/arcilator/randomize-sv-cpp-aot.sv`.
- Produces docs that identify the completed `SV -> MLIR -> C++ testbench -> executable` path.

**中文接口:**
- 消费已通过的 `test/arcilator/randomize-sv-cpp-aot.sv`。
- 产出文档，记录已完成的 `SV -> MLIR -> C++ testbench -> executable` 路径。

- [x] **Step 1: Update the current executable status / 更新当前 executable 状态**

  In `docs/SystemVerilogConstraintSolving.md`, update the arcilator executable status paragraph to say:

  ```markdown
  The current executable demo path now covers `SV -> Moore MLIR -> MooreToCore
  -> arcilator LLVM -> C++ testbench -> native executable`. The SV file owns the
  class and constraint, the lit test adds a thin Moore harness that exports
  `randomize_packet()`, and a temporary C++ testbench calls that symbol. The flow
  still avoids SV `initial`, generic `llhd.process` lowering, JIT, and demo-only
  MooreToCore entry-generation options.
  ```

- [x] **Step 2: Review docs diff / 检查文档 diff**

  Run:

  ```sh
  git diff -- docs/SystemVerilogConstraintSolving.md
  ```

  Result: docs mention the completed SV-to-C++ AOT demo.

  结果：文档提到已完成 SV-to-C++ AOT demo。

- [x] **Step 3: Commit / 提交**

  Result: batched with the test update in one final commit.

  结果：与测试更新合并到最后一个提交中。

## Verification Matrix / 验证矩阵

- `ls build-bitwuzla/bin/circt-verilog build-bitwuzla/bin/circt-opt build-bitwuzla/bin/arcilator`
  - Verifies all required tools exist.
  - 验证所需工具存在。
- `build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv`
  - Verifies existing SV randomize import and MooreToCore lowering.
  - 验证已有 SV randomize import 与 MooreToCore lowering。
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv`
  - Verifies the target `SV -> MLIR -> C++ testbench -> executable` flow.
  - 验证目标 `SV -> MLIR -> C++ testbench -> executable` flow。
- `build-bitwuzla/bin/llvm-lit -a test/arcilator`
  - Verifies the arcilator subset.
  - 验证 arcilator 子集。

## Explicit Non-Goals / 明确非目标

- No SV `initial` support in arcilator.
- 不在 arcilator 支持 SV `initial`。
- No generic `llhd.process` lowering.
- 不做通用 `llhd.process` lowering。
- No JIT work.
- 不做 JIT。
- No `arcilator --emit-executable`.
- 不做 `arcilator --emit-executable`。
- No MooreToCore entry-generation option.
- 不做 MooreToCore entry-generation option。
- No printed randomized values.
- 不打印随机结果。
- No `rand_mode` / `constraint_mode`.
- 不做 `rand_mode` / `constraint_mode`。
- No inline `with` constraints.
- 不做 inline `with` constraints。
- No seed/replay.
- 不做 seed/replay。

## Commit Order / 提交顺序

1. `[arcilator] Add SV randomize C++ AOT demo`
2. `[Docs] Document SV randomize C++ AOT demo`

## Success Definition / 完成定义

The milestone is done when this command passes:

当以下命令通过时，本 milestone 完成：

```sh
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv
```

The test must start from SV, import to Moore MLIR with `circt-verilog`, add a thin Moore harness, lower through MooreToCore, emit LLVM through arcilator, link a C++ testbench with `clang++`/lld, and run the native executable.

该测试必须从 SV 开始，用 `circt-verilog` import 到 Moore MLIR，加入薄 Moore harness，经 MooreToCore lowering，通过 arcilator emit LLVM，再用 `clang++`/lld 链接 C++ testbench，并运行 native executable。
