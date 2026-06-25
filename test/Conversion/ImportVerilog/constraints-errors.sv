// RUN: split-file %s %t
// RUN: not circt-verilog --ir-moore %t/unsupported.sv 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not circt-verilog --ir-moore %t/randomize.sv 2>&1 | FileCheck %s --check-prefix=RANDOMIZE
// RUN: not circt-verilog --ir-moore %t/extern.sv 2>&1 | FileCheck %s --check-prefix=EXTERN
// REQUIRES: slang

//--- unsupported.sv
class Bad;
  rand int a;
  constraint bad { (a > 0) -> (a < 10); }
endclass

// BAD: unsupported constraint kind: Implication

//--- randomize.sv
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

// RANDOMIZE: unsupported system call `randomize`

//--- extern.sv
class ExternPacket;
  rand int len;
  extern constraint c_len;
endclass

constraint ExternPacket::c_len { len > 0; }

// EXTERN: unsupported extern constraint declaration
