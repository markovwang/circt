// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %true = moore.constant 1 : i1
    moore.yield %true : i1
  }
}

// CHECK-LABEL: func.func @packet_new
// CHECK: llvm.getelementptr {{.*}} !llvm.struct<"Packet", (struct<(ptr, ptr)>, i32, i1, i1)>
func.func @packet_new() {
  %obj = moore.class.new : <@Packet>
  return
}
