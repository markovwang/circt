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
