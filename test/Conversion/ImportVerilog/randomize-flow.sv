// RUN: circt-verilog --ir-moore %s 2>&1 | FileCheck %s --check-prefix=IMPORT --implicit-check-not="Class builtin functions"
// RUN: circt-verilog --ir-moore %s | circt-opt --convert-moore-to-core | FileCheck %s --check-prefix=LOWER
// REQUIRES: slang

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

module m;
  initial begin
    automatic Packet p = new;
    bit ok;
    ok = p.randomize();
  end
endmodule

// IMPORT: moore.class.classdecl @Packet
// IMPORT: moore.class.constraintdecl @c
// IMPORT-LABEL: func.func @randomize_packet
// IMPORT-SAME: circt.dpi.export = "randomize_packet"
// IMPORT: moore.class.new
// IMPORT: moore.class.randomize

// LOWER-LABEL: func.func @__circt_randomize_Packet
// LOWER: call @arcRuntimeSolverCreate
// LOWER: call @arcRuntimeSolverBvVar
// LOWER: call @arcRuntimeSolverAssert
// LOWER: call @arcRuntimeSolverCheck
// LOWER: call @arcRuntimeSolverGetBv
// LOWER: llvm.store
// LOWER: call @arcRuntimeSolverDestroy
// LOWER-LABEL: hw.module @m
// LOWER: call @__circt_randomize_Packet
