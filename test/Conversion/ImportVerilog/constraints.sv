// RUN: circt-verilog --ir-moore %s | FileCheck %s
// RUN: circt-verilog --ir-moore %s | FileCheck %s --check-prefix=MOORE
// REQUIRES: slang

class Packet;
  rand int len;
  constraint c_len { len > 0; }
endclass

// CHECK-LABEL: moore.class.classdecl @Packet
// CHECK:   moore.class.propertydecl @len : !moore.i32 {isRand}
// CHECK:   moore.class.constraintdecl @c_len {
// CHECK:     moore.yield

// MOORE-LABEL: moore.class.classdecl @Packet
// MOORE:   moore.class.propertydecl @len : !moore.i32 {isRand}
// MOORE:   moore.class.constraintdecl @c_len {
// MOORE:     moore.yield

class Ordered;
  rand int a;
  constraint first { a > 0; }
  virtual function int middle();
    return a;
  endfunction
  constraint second { a < 10; }
endclass

// CHECK-LABEL: moore.class.classdecl @Ordered
// CHECK:   moore.class.propertydecl @a : !moore.i32 {isRand}
// CHECK:   moore.class.constraintdecl @first
// CHECK:   moore.class.methoddecl @middle
// CHECK:   moore.class.constraintdecl @second

// MOORE-LABEL: moore.class.classdecl @Ordered
// MOORE:   moore.class.propertydecl @a : !moore.i32 {isRand}
// MOORE:   moore.class.constraintdecl @first
// MOORE:   moore.class.methoddecl @middle
// MOORE:   moore.class.constraintdecl @second

virtual class FlagsBase;
  pure constraint pure_c;
endclass

class Flags extends FlagsBase;
  rand int b;
  static constraint static_c { 1; }
  constraint pure_c { b != 4; }
endclass

// CHECK-LABEL: moore.class.classdecl @FlagsBase
// CHECK:   moore.class.constraintdecl @pure_c attributes {isExtern, isPure} {
// CHECK-NEXT:   }

// CHECK-LABEL: moore.class.classdecl @Flags
// CHECK:   moore.class.constraintdecl @static_c attributes {isStatic} {
// CHECK:   moore.class.constraintdecl @pure_c {

// MOORE-LABEL: moore.class.classdecl @FlagsBase
// MOORE:   moore.class.constraintdecl @pure_c attributes {isExtern, isPure} {
// MOORE-NEXT:   }

// MOORE-LABEL: moore.class.classdecl @Flags
// MOORE:   moore.class.constraintdecl @static_c attributes {isStatic} {
// MOORE:   moore.class.constraintdecl @pure_c {

class RandCProp;
  randc bit [7:0] id;
endclass

// CHECK-LABEL: moore.class.classdecl @RandCProp
// CHECK:   moore.class.propertydecl @id : !moore.i8 {isRandC}

// MOORE-LABEL: moore.class.classdecl @RandCProp
// MOORE:   moore.class.propertydecl @id : !moore.i8 {isRandC}
