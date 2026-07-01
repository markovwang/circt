// REQUIRES: bitwuzla-randomize
// RUN: rm -rf %t && mkdir -p %t
// RUN: arcilator %s --emit-llvm --no-runtime -o %t/randomize.ll
// RUN: clang++ %t/randomize.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/randomize.exe
// RUN: %t/randomize.exe

module {
  func.func private @malloc(i64) -> !llvm.ptr
  llvm.mlir.global internal constant @"Packet::typeinfo"() {addr_space = 0 : i32} : !llvm.struct<(ptr)> {
    %0 = llvm.mlir.undef : !llvm.struct<(ptr)>
    %1 = llvm.mlir.zero : !llvm.ptr
    %2 = llvm.insertvalue %1, %0[0] : !llvm.struct<(ptr)>
    llvm.return %2 : !llvm.struct<(ptr)>
  }
  llvm.mlir.global private constant @__circt_randomize_name_Packet_len("len\00") {addr_space = 0 : i32}

  func.func private @__circt_randomize_check_Packet(%arg0: !llvm.ptr, %arg1: i64) -> i1 {
    %0 = arith.trunci %arg1 : i64 to i32
    %c0_i32 = arith.constant 0 : i32
    %1 = arith.cmpi sgt, %0, %c0_i32 : i32
    return %1 : i1
  }

  func.func private @__circt_randomize_Packet(%arg0: !llvm.ptr) -> i1 {
    %0 = call @arcRuntimeSolverCreate() : () -> !llvm.ptr
    %1 = llvm.mlir.addressof @__circt_randomize_name_Packet_len : !llvm.ptr
    %2 = llvm.mlir.constant(32 : i32) : i32
    %3 = call @arcRuntimeSolverBvVar(%0, %1, %2) : (!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
    %4 = llvm.mlir.constant(0 : i64) : i64
    %5 = llvm.mlir.constant(32 : i32) : i32
    %6 = call @arcRuntimeSolverBvConst(%0, %4, %5) : (!llvm.ptr, i64, i32) -> !llvm.ptr
    %7 = call @arcRuntimeSolverSgt(%0, %3, %6) : (!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
    call @arcRuntimeSolverAssert(%0, %7) : (!llvm.ptr, !llvm.ptr) -> ()
    %8 = call @arcRuntimeSolverCheck(%0) : (!llvm.ptr) -> i1
    cf.cond_br %8, ^bb1, ^bb3
  ^bb1:
    %9 = call @arcRuntimeSolverGetBv(%0, %3) : (!llvm.ptr, !llvm.ptr) -> i64
    %10 = call @__circt_randomize_check_Packet(%arg0, %9) : (!llvm.ptr, i64) -> i1
    cf.cond_br %10, ^bb2, ^bb3
  ^bb2:
    %11 = arith.trunci %9 : i64 to i32
    %12 = llvm.mlir.constant(0 : i32) : i32
    %13 = llvm.getelementptr %arg0[%12, 1] : (!llvm.ptr, i32) -> !llvm.ptr, !llvm.struct<"Packet", (struct<(ptr, ptr)>, i32, i1, i1)>
    llvm.store %11, %13 : i32, !llvm.ptr
    call @arcRuntimeSolverDestroy(%0) : (!llvm.ptr) -> ()
    %true = arith.constant true
    return %true : i1
  ^bb3:
    call @arcRuntimeSolverDestroy(%0) : (!llvm.ptr) -> ()
    %false = arith.constant false
    return %false : i1
  }

  func.func private @arcRuntimeSolverGetBv(!llvm.ptr, !llvm.ptr) -> i64
  func.func private @arcRuntimeSolverCheck(!llvm.ptr) -> i1
  func.func private @arcRuntimeSolverAssert(!llvm.ptr, !llvm.ptr)
  func.func private @arcRuntimeSolverSgt(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
  func.func private @arcRuntimeSolverEq(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
  func.func private @arcRuntimeSolverBvConst(!llvm.ptr, i64, i32) -> !llvm.ptr
  func.func private @arcRuntimeSolverBvVar(!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
  func.func private @arcRuntimeSolverDestroy(!llvm.ptr)
  func.func private @arcRuntimeSolverCreate() -> !llvm.ptr

  func.func @main() -> i32 {
    %size = llvm.mlir.constant(24 : i64) : i64
    %packet = call @malloc(%size) : (i64) -> !llvm.ptr
    %typeinfo = llvm.mlir.addressof @"Packet::typeinfo" : !llvm.ptr
    %c0_i32 = llvm.mlir.constant(0 : i32) : i32
    %typeinfoField = llvm.getelementptr %packet[%c0_i32, 0] : (!llvm.ptr, i32) -> !llvm.ptr, !llvm.struct<"Packet", (struct<(ptr, ptr)>, i32, i1, i1)>
    llvm.store %typeinfo, %typeinfoField : !llvm.ptr, !llvm.ptr
    %ok = call @__circt_randomize_Packet(%packet) : (!llvm.ptr) -> i1
    cf.cond_br %ok, ^bb1, ^bb2
  ^bb1:
    %lenField = llvm.getelementptr %packet[%c0_i32, 1] : (!llvm.ptr, i32) -> !llvm.ptr, !llvm.struct<"Packet", (struct<(ptr, ptr)>, i32, i1, i1)>
    %len = llvm.load %lenField : !llvm.ptr -> i32
    %zero = arith.constant 0 : i32
    %one = arith.constant 1 : i32
    %positive = arith.cmpi sgt, %len, %zero : i32
    %ret = arith.select %positive, %zero, %one : i32
    return %ret : i32
  ^bb2:
    %fail = arith.constant 1 : i32
    return %fail : i32
  }
}
