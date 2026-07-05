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

func.func @randomize_packet_from_local() -> !moore.i32 {
  %one = moore.constant 1 : i32
  %zero = moore.constant 0 : i32
  %object = moore.class.new : !moore.class<@Packet>
  %p = moore.variable : <class<@Packet>>
  moore.blocking_assign %p, %object : class<@Packet>
  %read = moore.read %p : <class<@Packet>>
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

moore.module @m() {
  moore.procedure initial {
    %object = moore.class.new : !moore.class<@Packet>
    %ok = moore.class.randomize %object : !moore.class<@Packet>
    moore.return
  }
}

// CHECK-LABEL: func.func private @__circt_randomize_Packet
// CHECK: call @arcRuntimeSolverGetBv
// CHECK: llvm.getelementptr
// CHECK: llvm.store
// CHECK: call @arcRuntimeSolverDestroy
// CHECK: return %true
// CHECK: call @arcRuntimeSolverDestroy
// CHECK: return %false
// CHECK-LABEL: func.func @randomize_packet_from_local
// CHECK: call @malloc
// CHECK-NOT: llhd.sig
// CHECK-NOT: llhd.drv
// CHECK-NOT: llhd.prb
// CHECK-NOT: llhd.constant_time
// CHECK: call @__circt_randomize_Packet
// CHECK-NOT: llhd.sig
// CHECK-NOT: llhd.drv
// CHECK-NOT: llhd.prb
// CHECK-NOT: llhd.constant_time
// CHECK-LABEL: hw.module @m
// CHECK: call @__circt_randomize_Packet
