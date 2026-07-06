// REQUIRES: bitwuzla-randomize
// RUN: rm -rf %t && mkdir -p %t
// RUN: circt-opt --convert-moore-to-core %s -o %t/randomize-core.mlir
// RUN: FileCheck %s --input-file=%t/randomize-core.mlir --check-prefix=CORE --implicit-check-not=llhd.process --implicit-check-not=moore.procedure
// RUN: arcilator %t/randomize-core.mlir --emit-llvm --no-runtime -o %t/randomize.ll
// RUN: clang++ %t/randomize.ll %circt_arc_runtime_lib %bitwuzla_randomize_link_flags -Wno-override-module -fuse-ld=lld -o %t/randomize.exe
// RUN: %t/randomize.exe

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

func.func @main() -> i32 {
  %object = moore.class.new : !moore.class<@Packet>
  %ok = moore.class.randomize %object : !moore.class<@Packet>
  %ok_builtin = moore.to_builtin_int %ok : i1
  %zero = arith.constant 0 : i32
  %one = arith.constant 1 : i32
  %ret = arith.select %ok_builtin, %zero, %one : i32
  return %ret : i32
}

// CORE-LABEL: func.func @__circt_randomize_Packet
// CORE: call @arcRuntimeSolverCheck
// CORE-LABEL: func.func @main() -> i32
// CORE: call @malloc
// CORE: call @__circt_randomize_Packet
// CORE: return
