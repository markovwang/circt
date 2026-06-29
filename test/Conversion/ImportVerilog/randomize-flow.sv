// RUN: circt-verilog --ir-moore %s 2>&1 | FileCheck %s --check-prefix=IMPORT --implicit-check-not="Class builtin functions"
// RUN: circt-verilog --ir-moore %s | circt-opt --convert-moore-to-core | FileCheck %s --check-prefix=LOWER
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

// IMPORT: moore.class.classdecl @Packet
// IMPORT: moore.class.constraintdecl @c
// IMPORT: moore.class.randomize

// LOWER-LABEL: func.func private @__circt_randomize_Packet
// LOWER: call @arcRuntimeSolverCreate
// LOWER: call @arcRuntimeSolverBvVar
// LOWER: call @arcRuntimeSolverAssert
// LOWER: call @arcRuntimeSolverCheck
// LOWER: call @arcRuntimeSolverGetBv
// LOWER: llvm.store
// LOWER: call @arcRuntimeSolverDestroy
// LOWER-LABEL: hw.module @m
// LOWER: call @__circt_randomize_Packet
