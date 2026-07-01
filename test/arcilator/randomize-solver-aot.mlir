// REQUIRES: bitwuzla-randomize
// RUN: rm -rf %t && mkdir -p %t
// RUN: arcilator %s --emit-llvm --no-runtime -o %t/solver.ll
// RUN: clang++ %t/solver.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/solver.exe
// RUN: %t/solver.exe

module {
  llvm.func @arcRuntimeSolverCreate() -> !llvm.ptr
  llvm.func @arcRuntimeSolverDestroy(!llvm.ptr)
  llvm.func @arcRuntimeSolverBvVar(!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
  llvm.func @arcRuntimeSolverBvConst(!llvm.ptr, i64, i32) -> !llvm.ptr
  llvm.func @arcRuntimeSolverSgt(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
  llvm.func @arcRuntimeSolverAssert(!llvm.ptr, !llvm.ptr)
  llvm.func @arcRuntimeSolverCheck(!llvm.ptr) -> i1

  llvm.func @main() -> i32 {
    %c0_i64 = llvm.mlir.constant(0 : i64) : i64
    %c8_i32 = llvm.mlir.constant(8 : i32) : i32
    %true = llvm.mlir.constant(true) : i1
    %name = llvm.mlir.addressof @name : !llvm.ptr
    %solver = llvm.call @arcRuntimeSolverCreate() : () -> !llvm.ptr
    %x = llvm.call @arcRuntimeSolverBvVar(%solver, %name, %c8_i32) : (!llvm.ptr, !llvm.ptr, i32) -> !llvm.ptr
    %zero = llvm.call @arcRuntimeSolverBvConst(%solver, %c0_i64, %c8_i32) : (!llvm.ptr, i64, i32) -> !llvm.ptr
    %gt = llvm.call @arcRuntimeSolverSgt(%solver, %x, %zero) : (!llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.ptr
    llvm.call @arcRuntimeSolverAssert(%solver, %gt) : (!llvm.ptr, !llvm.ptr) -> ()
    %ok = llvm.call @arcRuntimeSolverCheck(%solver) : (!llvm.ptr) -> i1
    llvm.call @arcRuntimeSolverDestroy(%solver) : (!llvm.ptr) -> ()
    %fail = llvm.xor %ok, %true : i1
    %ret = llvm.zext %fail : i1 to i32
    llvm.return %ret : i32
  }

  llvm.mlir.global internal constant @name("x\00") : !llvm.array<2 x i8>
}
