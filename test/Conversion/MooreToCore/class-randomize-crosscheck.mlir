// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

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

// CHECK-LABEL: func.func private @__circt_randomize_check_Packet
// CHECK: arith.cmpi sgt
// CHECK-LABEL: func.func private @__circt_randomize_Packet
// CHECK: call @__circt_randomize_check_Packet
// CHECK: cf.cond_br

