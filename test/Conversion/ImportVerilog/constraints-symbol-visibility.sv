// P0a 边界探查:约束体符号可见性
// 目的:验证约束体引用类属性时,现有通用机制(getImplicitThisRef +
//       getAncestorClassWithProperty + materializeConversion)能否覆盖
//       本类属性 / 继承属性 / 多层继承 / 同名遮蔽 / 静态属性。
// T1(本类属性)通过;T2/T3/T4/T5 暴露已知缺陷(见 XFAIL),待 P0a 修复。
// T6(static constraint 无 this)已迁移到 constraints-errors.sv 并修复。
//
// 继承属性(T2/T3/T4)静默丢类、静态属性(T5)符号未注册 —— P0a 待修
// XFAIL: *
// RUN: circt-verilog --ir-moore %s 2>&1 | FileCheck %s --check-prefix=IR
// REQUIRES: slang

// T1: 本类属性(基线,应已通过)
class T1;
  rand int x;
  constraint c { x > 0; }
endclass
// IR-LABEL: moore.class.classdecl @T1
// IR:       moore.class.propertydecl @x : !moore.i32
// IR:       moore.class.constraintdecl @c

// T2: 继承属性 —— 派生类约束体引用基类 rand 属性
class T2Base;
  rand int x;
endclass
class T2Derived extends T2Base;
  constraint c { x > 0; }
endclass
// IR-LABEL: moore.class.classdecl @T2Derived extends @T2Base
// IR:       moore.class.constraintdecl @c

// T3: 多层继承属性 —— C 的约束体引用 A 的属性,中间隔 B
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

// T4: 同名属性遮蔽 —— 本类与基类都有 x,约束体引用应解析到本类(就近)
class T4Base;
  rand int x;
endclass
class T4Derived extends T4Base;
  rand int x;
  constraint c { x > 0; }
endclass
// IR-LABEL: moore.class.classdecl @T4Derived extends @T4Base
// IR:       moore.class.constraintdecl @c

// T5: 静态属性 —— 约束体引用类静态属性(不依赖 this)
class T5;
  static int s = 5;
  rand int x;
  constraint c { x > s; }
endclass
// IR-LABEL: moore.class.classdecl @T5
// IR:       moore.class.constraintdecl @c

