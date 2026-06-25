// RUN: circt-verilog --ir-moore %s 2>&1 | FileCheck %s --check-prefix=IR
// REQUIRES: slang

// Same-class property access is the baseline case.
class T1;
  rand int x;
  constraint c { x > 0; }
endclass

// IR-LABEL: moore.class.classdecl @T1
// IR:       moore.class.propertydecl @x : !moore.i32
// IR:       moore.class.constraintdecl @c

// A derived class constraint can reference a base class property.
class T2Base;
  rand int x;
endclass
class T2Derived extends T2Base;
  constraint c { x > 0; }
endclass

// IR-LABEL: moore.class.classdecl @T2Derived extends @T2Base
// IR:       moore.class.constraintdecl @c
// IR:       moore.class.upcast {{.*}} : <@T2Derived> to <@T2Base>
// IR:       moore.class.property_ref {{.*}}[@x] : <@T2Base> -> <i32>

// A constraint can reference an inherited property through multiple bases.
class T3A;
  rand int x;
endclass
class T3B extends T3A;
endclass
class T3C extends T3B;
  constraint c { x > 0; }
endclass

// IR-LABEL: moore.class.classdecl @T3C extends @T3B
// IR:       moore.class.constraintdecl @c
// IR:       moore.class.upcast {{.*}} : <@T3C> to <@T3A>
// IR:       moore.class.property_ref {{.*}}[@x] : <@T3A> -> <i32>

// Same-name properties are resolved to the nearest declaration.
class T4Base;
  rand int x;
endclass
class T4Derived extends T4Base;
  rand int x;
  constraint c { x > 0; }
endclass

// IR-LABEL: moore.class.classdecl @T4Derived extends @T4Base
// IR:       moore.class.propertydecl @x : !moore.i32
// IR:       moore.class.constraintdecl @c
// IR:       moore.class.property_ref {{.*}}[@x] : <@T4Derived> -> <i32>

// Static properties are hoisted to globals and remain visible from constraints.
class T5;
  static int s = 5;
  rand int x;
  constraint c { x > s; }
endclass

// IR-LABEL: moore.class.classdecl @T5
// IR:       moore.class.constraintdecl @c
// IR:       moore.get_global_variable @"T5::s" : <i32>
// IR:       moore.global_variable @"T5::s" : !moore.i32
