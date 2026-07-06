// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 {isRand}
  moore.class.propertydecl @addr : !moore.i32 {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %len_ref = moore.class.property_ref %this[@len] : <@Packet> -> !moore.ref<!moore.i32>
    %len = moore.read %len_ref : <i32>
    %ok = moore.sgt %len, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

func.func @smoke() -> !moore.i1 {
  %object = moore.class.new : !moore.class<@Packet>
  %ok = moore.class.randomize %object : !moore.class<@Packet>
  return %ok : !moore.i1
}

// CHECK: circt.arc.class_info
// CHECK-DAG: name = "Packet"
// CHECK-DAG: newFn = "__circt_new_Packet"
// CHECK-DAG: deleteFn = "__circt_delete_Packet"
// CHECK-DAG: randomizeFn = "__circt_randomize_Packet"
// CHECK-DAG: name = "len"
// CHECK-DAG: cppType = "int32_t"
// CHECK-DAG: name = "addr"
// CHECK-DAG: cppType = "int32_t"

// CHECK-LABEL: func.func @__circt_delete_Packet
// CHECK-SAME: (%[[OBJECT:.*]]: !llvm.ptr)
// CHECK: call @free(%[[OBJECT]])
// CHECK: return

// CHECK-LABEL: func.func @__circt_new_Packet() -> !llvm.ptr
// CHECK: call @malloc
// CHECK: llvm.store
// CHECK: return

// CHECK-LABEL: func.func @__circt_randomize_Packet
// CHECK-SAME: (%{{.*}}: !llvm.ptr) -> i1
// CHECK: call @arcRuntimeSolverCheck
