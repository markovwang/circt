// RUN: not circt-opt --convert-moore-to-core %s 2>&1 | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %ref = moore.class.property_ref %this[@len] : <@Packet> -> !moore.ref<i32>
    %val = moore.read %ref : <i32>
    %ok = moore.case_eq %val, %val : i32
    moore.yield %ok : i1
  }
}

// CHECK: unsupported randomize constraint operation
