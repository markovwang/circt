//===- LowerClassRandomize.cpp - Class randomize prep ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Moore/MooreOps.h"
#include "circt/Dialect/Moore/MooreTypes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace circt;
using namespace circt::moore;
using namespace mlir;

namespace circt {
LogicalResult prepareClassRandomizeSupport(ModuleOp module,
                                           SymbolTable &symbolTable);
} // namespace circt

namespace {
struct RandFieldDesc {
  ClassPropertyDeclOp property;
  bool isRandC = false;
  bool isOneDimUnpackedArray = false;
  unsigned elementCount = 1;
  unsigned bitWidth = 0;
};

struct ConstraintBlockDesc {
  ClassConstraintDeclOp constraint;
  bool isStatic = false;
};

struct ConstraintProblemDesc {
  ClassDeclOp cls;
  SmallVector<RandFieldDesc> randFields;
  SmallVector<ConstraintBlockDesc> constraints;
};

static std::string getRandomizeHelperName(ClassDeclOp cls) {
  return (Twine("__circt_randomize_") + cls.getSymName()).str();
}

static std::optional<unsigned> getIntegralWidth(Type type) {
  if (auto intTy = dyn_cast<IntType>(type))
    return intTy.getWidth();
  return std::nullopt;
}

static FailureOr<RandFieldDesc>
describeRandField(ClassPropertyDeclOp property) {
  RandFieldDesc desc;
  desc.property = property;
  desc.isRandC = static_cast<bool>(property.getIsRandCAttr());

  auto type = property.getPropertyType();
  if (auto width = getIntegralWidth(type)) {
    desc.bitWidth = *width;
    return desc;
  }

  if (auto arrayTy = dyn_cast<UnpackedArrayType>(type)) {
    auto width = getIntegralWidth(arrayTy.getElementType());
    if (!width)
      return property.emitError()
             << "only 1-dim unpacked arrays of integral elements are supported "
                "for randomization";
    desc.isOneDimUnpackedArray = true;
    desc.elementCount = arrayTy.getSize();
    desc.bitWidth = *width;
    return desc;
  }

  return property.emitError()
         << "only integral scalar rand fields and 1-dim unpacked arrays of "
            "integral elements are supported";
}

static FailureOr<ConstraintProblemDesc>
collectConstraintProblem(ClassDeclOp cls) {
  ConstraintProblemDesc result;
  result.cls = cls;

  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>()) {
    if (!property.getIsRandAttr() && !property.getIsRandCAttr())
      continue;
    auto desc = describeRandField(property);
    if (failed(desc))
      return failure();
    result.randFields.push_back(*desc);
  }

  for (auto constraint : cls.getBody().getOps<ClassConstraintDeclOp>()) {
    if (constraint.getIsPure() || constraint.getIsExtern())
      continue;
    result.constraints.push_back(
        {constraint, static_cast<bool>(constraint.getIsStatic())});
  }

  return result;
}

static void ensureModeProperty(OpBuilder &builder, ClassDeclOp cls,
                               StringRef name) {
  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>())
    if (property.getSymName() == name)
      return;

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&cls.getBody().front());
  auto type = IntType::getInt(builder.getContext(), 1);
  auto mode = ClassPropertyDeclOp::create(builder, cls.getLoc(), name, type,
                                          UnitAttr(), UnitAttr());
  mode->setAttr("circt.randomize.mode", builder.getUnitAttr());
}

static func::FuncOp getOrCreateRuntimeFunc(ModuleOp module, OpBuilder &builder,
                                           StringRef name, TypeRange inputs,
                                           TypeRange results) {
  if (auto fn = module.lookupSymbol<func::FuncOp>(name))
    return fn;

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto fn = func::FuncOp::create(builder, module.getLoc(), name,
                                 builder.getFunctionType(inputs, results));
  fn.setPrivate();
  return fn;
}

struct SolverRuntime {
  func::FuncOp create;
  func::FuncOp bvVar;
  func::FuncOp bvConst;
  func::FuncOp eq;
  func::FuncOp sgt;
  func::FuncOp assertFn;
  func::FuncOp check;
};

static SolverRuntime getOrCreateSolverRuntime(ModuleOp module,
                                              OpBuilder &builder) {
  auto *ctx = module.getContext();
  auto ptrTy = LLVM::LLVMPointerType::get(ctx);
  auto i1Ty = IntegerType::get(ctx, 1);
  auto i32Ty = IntegerType::get(ctx, 32);
  auto i64Ty = IntegerType::get(ctx, 64);

  return {
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverCreate", {},
                             {ptrTy}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverBvVar",
                             {ptrTy, ptrTy, i32Ty}, {ptrTy}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverBvConst",
                             {ptrTy, i64Ty, i32Ty}, {ptrTy}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverEq",
                             {ptrTy, ptrTy, ptrTy}, {ptrTy}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverSgt",
                             {ptrTy, ptrTy, ptrTy}, {ptrTy}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverAssert",
                             {ptrTy, ptrTy}, {}),
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverCheck", {ptrTy},
                             {i1Ty}),
  };
}

struct SolverEmitter {
  ModuleOp module;
  OpBuilder &builder;
  Location loc;
  ConstraintProblemDesc &problem;
  SolverRuntime runtime;
  Value solver;
  llvm::StringMap<Value> scalarVariables;

  SolverEmitter(ModuleOp module, OpBuilder &builder, Location loc,
                ConstraintProblemDesc &problem, SolverRuntime runtime,
                Value solver)
      : module(module), builder(builder), loc(loc), problem(problem),
        runtime(runtime), solver(solver) {}

  Value getI32(uint32_t value) {
    auto type = IntegerType::get(builder.getContext(), 32);
    return LLVM::ConstantOp::create(builder, loc, type,
                                    builder.getI32IntegerAttr(value));
  }

  Value getI64(uint64_t value) {
    auto type = IntegerType::get(builder.getContext(), 64);
    return LLVM::ConstantOp::create(builder, loc, type,
                                    builder.getI64IntegerAttr(value));
  }

  Value getNullPtr() {
    return LLVM::ZeroOp::create(
        builder, loc, LLVM::LLVMPointerType::get(builder.getContext()));
  }

  Value callValue(func::FuncOp fn, ValueRange args) {
    return func::CallOp::create(builder, loc, fn, args).getResult(0);
  }

  void callVoid(func::FuncOp fn, ValueRange args) {
    func::CallOp::create(builder, loc, fn, args);
  }

  FailureOr<Value> emitConstant(ConstantOp op) {
    auto value = op.getValue();
    if (value.hasUnknown())
      return op.emitError()
             << "unsupported randomize constraint constant with X/Z bits";

    auto bits = value.toAPInt(false);
    if (bits.getBitWidth() > 64)
      return op.emitError()
             << "unsupported randomize constraint constant wider than 64 bits";

    return callValue(runtime.bvConst, {solver, getI64(bits.getZExtValue()),
                                       getI32(op.getType().getWidth())});
  }

  FailureOr<Value> emitPropertyRead(ClassPropertyRefOp op) {
    auto name = op.getProperty();
    if (auto it = scalarVariables.find(name); it != scalarVariables.end())
      return it->second;

    const RandFieldDesc *field = nullptr;
    for (auto &candidate : problem.randFields) {
      if (candidate.property.getSymName() == name) {
        field = &candidate;
        break;
      }
    }

    if (!field)
      return op.emitError()
             << "only rand fields are supported in randomize constraints";
    if (field->isOneDimUnpackedArray)
      return op.emitError()
             << "array rand fields are not supported by scalar solver lowering";

    Value term = callValue(runtime.bvVar,
                           {solver, getNullPtr(), getI32(field->bitWidth)});
    scalarVariables[name] = term;
    return term;
  }

  FailureOr<Value> emitEq(EqOp, Value lhs, Value rhs) {
    return callValue(runtime.eq, {solver, lhs, rhs});
  }

  FailureOr<Value> emitSgt(SgtOp, Value lhs, Value rhs) {
    return callValue(runtime.sgt, {solver, lhs, rhs});
  }

  void emitAssert(Value term) { callVoid(runtime.assertFn, {solver, term}); }
};

template <typename Emitter>
struct ConstraintExprLowerer {
  Emitter &emitter;
  DenseMap<Value, Value> valueMap;

  ConstraintExprLowerer(Emitter &emitter) : emitter(emitter) {}

  FailureOr<Value> lower(Value value) {
    if (auto mapped = valueMap.lookup(value))
      return mapped;
    auto *def = value.getDefiningOp();
    if (!def)
      return failure();
    auto lowered = lowerOp(def);
    if (succeeded(lowered))
      valueMap[value] = *lowered;
    return lowered;
  }

  FailureOr<Value> lowerOp(Operation *op) {
    return TypeSwitch<Operation *, FailureOr<Value>>(op)
        .template Case<ConstantOp>(
            [&](auto op) { return emitter.emitConstant(op); })
        .template Case<ClassPropertyRefOp>(
            [&](auto op) { return emitter.emitPropertyRead(op); })
        .template Case<ReadOp>([&](auto op) { return lower(op.getInput()); })
        .template Case<EqOp>([&](auto op) -> FailureOr<Value> {
          auto lhs = lower(op.getLhs());
          if (failed(lhs))
            return failure();
          auto rhs = lower(op.getRhs());
          if (failed(rhs))
            return failure();
          return emitter.emitEq(op, *lhs, *rhs);
        })
        .template Case<SgtOp>([&](auto op) -> FailureOr<Value> {
          auto lhs = lower(op.getLhs());
          if (failed(lhs))
            return failure();
          auto rhs = lower(op.getRhs());
          if (failed(rhs))
            return failure();
          return emitter.emitSgt(op, *lhs, *rhs);
        })
        .Default([](Operation *op) -> FailureOr<Value> {
          return op->emitError("unsupported randomize constraint operation");
        });
  }
};

static LogicalResult emitRandomizeHelper(ModuleOp module, OpBuilder &builder,
                                         ConstraintProblemDesc &problem) {
  if (problem.randFields.empty())
    return success();

  auto name = getRandomizeHelperName(problem.cls);
  if (module.lookupSymbol<func::FuncOp>(name))
    return success();

  auto *ctx = module.getContext();
  auto ptrTy = LLVM::LLVMPointerType::get(ctx);
  auto i1Ty = IntegerType::get(ctx, 1);
  auto runtime = getOrCreateSolverRuntime(module, builder);

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto helper = func::FuncOp::create(builder, problem.cls.getLoc(), name,
                                     builder.getFunctionType({ptrTy}, {i1Ty}));
  helper.setPrivate();
  auto *body = helper.addEntryBlock();
  builder.setInsertionPointToEnd(body);

  auto solver = func::CallOp::create(builder, problem.cls.getLoc(),
                                     runtime.create, ValueRange{})
                    .getResult(0);
  SolverEmitter emitter(module, builder, problem.cls.getLoc(), problem, runtime,
                        solver);
  ConstraintExprLowerer<SolverEmitter> lowerer(emitter);

  for (auto block : problem.constraints) {
    auto *terminator = block.constraint.getBody().front().getTerminator();
    auto yield = dyn_cast<YieldOp>(terminator);
    if (!yield)
      return block.constraint.emitError()
             << "randomize constraint block must terminate with moore.yield";

    auto term = lowerer.lower(yield.getOperand());
    if (failed(term))
      return failure();
    emitter.emitAssert(*term);
  }

  auto solved =
      func::CallOp::create(builder, problem.cls.getLoc(), runtime.check, solver)
          .getResult(0);
  func::ReturnOp::create(builder, problem.cls.getLoc(), solved);
  return success();
}
} // namespace

LogicalResult circt::prepareClassRandomizeSupport(ModuleOp module,
                                                  SymbolTable &symbolTable) {
  (void)symbolTable;
  OpBuilder builder(module.getContext());
  bool failedAny = false;

  module.walk([&](ClassDeclOp cls) {
    auto problem = collectConstraintProblem(cls);
    if (failed(problem)) {
      failedAny = true;
      return;
    }

    for (auto field : problem->randFields) {
      auto name =
          (Twine("__circt_rand_mode__") + field.property.getSymName()).str();
      ensureModeProperty(builder, cls, name);
    }

    for (auto block : problem->constraints) {
      auto name =
          (Twine("__circt_constraint_mode__") + block.constraint.getSymName())
              .str();
      ensureModeProperty(builder, cls, name);
    }

    if (failed(emitRandomizeHelper(module, builder, *problem)))
      failedAny = true;
  });

  return failure(failedAny);
}
