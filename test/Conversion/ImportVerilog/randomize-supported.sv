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

