// RUN: not circt-opt --convert-moore-to-core %s 2>&1 | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @words : !moore.uarray<4 x i32> {isRand}
  moore.class.propertydecl @idx : !moore.i32
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %ref = moore.class.property_ref %this[@words] : <@Packet> -> !moore.ref<!moore.uarray<4 x i32>>
    %idxRef = moore.class.property_ref %this[@idx] : <@Packet> -> !moore.ref<!moore.i32>
    %idx = moore.read %idxRef : <i32>
    %elem = moore.dyn_extract_ref %ref from %idx : !moore.ref<!moore.uarray<4 x i32>>, !moore.i32 -> !moore.ref<!moore.i32>
    %val = moore.read %elem : <i32>
    %zero = moore.constant 0 : i32
    %ok = moore.sgt %val, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

// CHECK: randomize constraints only support constant indices into 1-dim unpacked rand arrays

