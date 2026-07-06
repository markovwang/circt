// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %ref = moore.class.property_ref %this[@len] : <@Packet> -> !moore.ref<i32>
    %val = moore.read %ref : <i32>
    %ok = moore.sgt %val, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: func.func @__circt_randomize_Packet
// CHECK: call @arcRuntimeSolverCreate
// CHECK: call @arcRuntimeSolverBvVar
// CHECK: call @arcRuntimeSolverBvConst
// CHECK: call @arcRuntimeSolverSgt
// CHECK: call @arcRuntimeSolverAssert
// CHECK: call @arcRuntimeSolverCheck
