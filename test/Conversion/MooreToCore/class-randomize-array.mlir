// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @words : !moore.uarray<4 x i32> {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %ref = moore.class.property_ref %this[@words] : <@Packet> -> !moore.ref<!moore.uarray<4 x i32>>
    %array = moore.read %ref : <uarray<4 x i32>>
    %elem = moore.extract %array from 3 : !moore.uarray<4 x i32> -> !moore.i32
    %ok = moore.sgt %elem, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK-DAG: llvm.mlir.global private constant @__circt_randomize_name_Packet_words_0("words[0]\00")
// CHECK-DAG: llvm.mlir.global private constant @__circt_randomize_name_Packet_words_3("words[3]\00")
// CHECK: llvm.mlir.addressof @__circt_randomize_name_Packet_words_0
// CHECK: call @arcRuntimeSolverBvVar
// CHECK: llvm.mlir.addressof @__circt_randomize_name_Packet_words_3
// CHECK: call @arcRuntimeSolverBvVar
