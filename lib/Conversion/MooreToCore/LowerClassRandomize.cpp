//===- LowerClassRandomize.cpp - Class randomize prep ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/Moore/MooreOps.h"
#include "circt/Dialect/Moore/MooreTypes.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/FormatVariadic.h"

#include <algorithm>

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

static std::string getRandomizeCheckHelperName(ClassDeclOp cls) {
  return (Twine("__circt_randomize_check_") + cls.getSymName()).str();
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
  func::FuncOp destroy;
  func::FuncOp bvVar;
  func::FuncOp bvConst;
  func::FuncOp eq;
  func::FuncOp sgt;
  func::FuncOp assertFn;
  func::FuncOp check;
  func::FuncOp getBv;
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
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverDestroy",
                             {ptrTy}, {}),
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
      getOrCreateRuntimeFunc(module, builder, "arcRuntimeSolverGetBv",
                             {ptrTy, ptrTy}, {i64Ty}),
  };
}

static LLVM::LLVMStructType getClassObjectHeaderType(MLIRContext *ctx) {
  return LLVM::LLVMStructType::getLiteral(
      ctx, SmallVector<Type>{LLVM::LLVMPointerType::get(ctx),
                             LLVM::LLVMPointerType::get(ctx)});
}

static Type getStorageType(Type type) {
  if (auto intTy = dyn_cast<IntType>(type))
    return IntegerType::get(type.getContext(), intTy.getWidth());
  if (auto arrayTy = dyn_cast<UnpackedArrayType>(type)) {
    auto elementType = getStorageType(arrayTy.getElementType());
    if (!elementType)
      return {};
    return hw::ArrayType::get(elementType, arrayTy.getSize());
  }
  return {};
}

static LLVM::LLVMStructType getClassStorageType(ClassDeclOp cls) {
  auto *ctx = cls.getContext();
  auto storageType =
      LLVM::LLVMStructType::getIdentified(ctx, cls.getSymNameAttr());
  if (!storageType.isOpaque())
    return storageType;

  SmallVector<Type> members;
  members.push_back(getClassObjectHeaderType(ctx));
  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>()) {
    auto type = getStorageType(property.getPropertyType());
    if (!type)
      continue;
    members.push_back(type);
  }
  (void)storageType.setBody(members, /*isPacked=*/false);
  return storageType;
}

static std::optional<unsigned> getScalarPropertyIndex(ClassDeclOp cls,
                                                      StringRef propertyName) {
  unsigned index = 1;
  for (auto property : cls.getBody().getOps<ClassPropertyDeclOp>()) {
    if (!getStorageType(property.getPropertyType()))
      continue;
    if (property.getSymName() == propertyName)
      return index;
    ++index;
  }
  return std::nullopt;
}

class SolverStringCache {
public:
  Value getOrCreate(OpBuilder &builder, Location loc, ClassDeclOp cls,
                    StringRef name) {
    if (auto it = cache.find(name); it != cache.end())
      return LLVM::AddressOfOp::create(builder, loc, it->second);

    auto *ctx = builder.getContext();
    auto symbol =
        (Twine("__circt_randomize_name_") + cls.getSymName() + "_" + name)
            .str();
    std::replace(symbol.begin(), symbol.end(), '[', '_');
    std::replace(symbol.begin(), symbol.end(), ']', '\0');
    symbol.erase(std::remove(symbol.begin(), symbol.end(), '\0'), symbol.end());

    SmallVector<char> bytes(name.begin(), name.end());
    bytes.push_back(0);

    LLVM::GlobalOp global;
    {
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(
          cls->getParentOfType<ModuleOp>().getBody());
      auto type =
          LLVM::LLVMArrayType::get(IntegerType::get(ctx, 8), bytes.size());
      global = LLVM::GlobalOp::create(
          builder, loc, type, /*isConstant=*/true, LLVM::Linkage::Private,
          symbol, builder.getStringAttr(bytes), /*alignment=*/0);
    }

    cache[name] = global;
    return LLVM::AddressOfOp::create(builder, loc, global);
  }

private:
  llvm::StringMap<LLVM::GlobalOp> cache;
};

struct SolverEmitter {
  ModuleOp module;
  OpBuilder &builder;
  Location loc;
  ConstraintProblemDesc &problem;
  SolverRuntime runtime;
  SolverStringCache &strings;
  Value solver;
  llvm::StringMap<Value> scalarVariables;
  llvm::StringMap<SmallVector<Value>> arrayVariables;

  SolverEmitter(ModuleOp module, OpBuilder &builder, Location loc,
                ConstraintProblemDesc &problem, SolverRuntime runtime,
                SolverStringCache &strings, Value solver)
      : module(module), builder(builder), loc(loc), problem(problem),
        runtime(runtime), strings(strings), solver(solver) {}

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
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";

    Value namePtr = strings.getOrCreate(builder, loc, problem.cls, name);
    Value term =
        callValue(runtime.bvVar, {solver, namePtr, getI32(field->bitWidth)});
    scalarVariables[name] = term;
    return term;
  }

  FailureOr<Value> emitExtract(ExtractOp op) {
    auto read = op.getInput().getDefiningOp<ReadOp>();
    if (!read)
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";
    auto property = read.getInput().getDefiningOp<ClassPropertyRefOp>();
    if (!property)
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";

    const RandFieldDesc *field = nullptr;
    for (auto &candidate : problem.randFields) {
      if (candidate.property.getSymName() == property.getProperty()) {
        field = &candidate;
        break;
      }
    }
    if (!field || !field->isOneDimUnpackedArray)
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";

    auto index = op.getLowBit();
    if (index >= field->elementCount)
      return op.emitError()
             << "randomize constraints only support in-bounds constant indices "
                "into 1-dim unpacked rand arrays";

    auto fieldName = property.getProperty();
    auto &variables = arrayVariables[fieldName];
    if (variables.empty()) {
      variables.reserve(field->elementCount);
      for (unsigned i = 0, e = field->elementCount; i != e; ++i) {
        auto elementName = llvm::formatv("{0}[{1}]", fieldName, i).str();
        Value namePtr =
            strings.getOrCreate(builder, loc, problem.cls, elementName);
        variables.push_back(callValue(
            runtime.bvVar, {solver, namePtr, getI32(field->bitWidth)}));
      }
    }
    return variables[index];
  }

  void emitModelValues(SmallVectorImpl<Value> &values) {
    for (auto &field : problem.randFields) {
      if (!field.isOneDimUnpackedArray) {
        auto name = field.property.getSymName();
        if (!scalarVariables.count(name)) {
          auto namePtr = strings.getOrCreate(builder, loc, problem.cls, name);
          scalarVariables[name] = callValue(
              runtime.bvVar, {solver, namePtr, getI32(field.bitWidth)});
        }
        values.push_back(
            callValue(runtime.getBv, {solver, scalarVariables[name]}));
        continue;
      }

      auto fieldName = field.property.getSymName();
      auto &variables = arrayVariables[fieldName];
      if (variables.empty()) {
        variables.reserve(field.elementCount);
        for (unsigned i = 0, e = field.elementCount; i != e; ++i) {
          auto elementName = llvm::formatv("{0}[{1}]", fieldName, i).str();
          Value namePtr =
              strings.getOrCreate(builder, loc, problem.cls, elementName);
          variables.push_back(callValue(
              runtime.bvVar, {solver, namePtr, getI32(field.bitWidth)}));
        }
      }
      for (auto variable : variables)
        values.push_back(callValue(runtime.getBv, {solver, variable}));
    }
  }

  FailureOr<Value> emitEq(EqOp, Value lhs, Value rhs) {
    return callValue(runtime.eq, {solver, lhs, rhs});
  }

  FailureOr<Value> emitSgt(SgtOp, Value lhs, Value rhs) {
    return callValue(runtime.sgt, {solver, lhs, rhs});
  }

  void emitAssert(Value term) { callVoid(runtime.assertFn, {solver, term}); }

  void emitCommitScalarFields(Value object, ArrayRef<Value> modelValues) {
    auto ptrTy = LLVM::LLVMPointerType::get(builder.getContext());
    auto i32Ty = IntegerType::get(builder.getContext(), 32);
    auto structTy = getClassStorageType(problem.cls);

    unsigned modelIndex = 0;
    for (auto &field : problem.randFields) {
      if (field.isOneDimUnpackedArray) {
        modelIndex += field.elementCount;
        continue;
      }

      auto propertyName = field.property.getSymName();
      auto propertyIndex = getScalarPropertyIndex(problem.cls, propertyName);
      if (!propertyIndex) {
        ++modelIndex;
        continue;
      }

      Value value = modelValues[modelIndex++];
      if (field.bitWidth < 64)
        value = arith::TruncIOp::create(
            builder, loc,
            IntegerType::get(builder.getContext(), field.bitWidth), value);

      Value zero = LLVM::ConstantOp::create(builder, loc, i32Ty,
                                            builder.getI32IntegerAttr(0));
      Value fieldIdx = LLVM::ConstantOp::create(
          builder, loc, i32Ty, builder.getI32IntegerAttr(*propertyIndex));
      auto fieldPtr = LLVM::GEPOp::create(builder, loc, ptrTy, structTy, object,
                                          ValueRange{zero, fieldIdx});
      LLVM::StoreOp::create(builder, loc, value, fieldPtr);
    }
  }
};

struct PredicateEmitter {
  OpBuilder &builder;
  Location loc;
  ConstraintProblemDesc &problem;
  ArrayRef<Value> candidates;
  llvm::StringMap<Value> scalarCandidates;
  llvm::StringMap<SmallVector<Value>> arrayCandidates;

  PredicateEmitter(OpBuilder &builder, Location loc,
                   ConstraintProblemDesc &problem, ArrayRef<Value> candidates)
      : builder(builder), loc(loc), problem(problem), candidates(candidates) {
    unsigned next = 0;
    for (auto &field : problem.randFields) {
      if (!field.isOneDimUnpackedArray) {
        scalarCandidates[field.property.getSymName()] =
            narrow(candidates[next++], field.bitWidth);
        continue;
      }

      auto &elements = arrayCandidates[field.property.getSymName()];
      elements.reserve(field.elementCount);
      for (unsigned i = 0, e = field.elementCount; i != e; ++i)
        elements.push_back(narrow(candidates[next++], field.bitWidth));
    }
  }

  Value narrow(Value value, unsigned width) {
    auto type = IntegerType::get(builder.getContext(), width);
    if (width == 64)
      return value;
    return arith::TruncIOp::create(builder, loc, type, value);
  }

  FailureOr<Value> emitConstant(ConstantOp op) {
    auto value = op.getValue();
    if (value.hasUnknown())
      return op.emitError()
             << "unsupported randomize constraint constant with X/Z bits";

    auto bits = value.toAPInt(false);
    auto type = IntegerType::get(builder.getContext(), op.getType().getWidth());
    return arith::ConstantOp::create(builder, loc, IntegerAttr::get(type, bits))
        .getResult();
  }

  FailureOr<Value> emitPropertyRead(ClassPropertyRefOp op) {
    if (auto it = scalarCandidates.find(op.getProperty());
        it != scalarCandidates.end())
      return it->second;
    return op.emitError()
           << "only rand fields are supported in randomize constraints";
  }

  FailureOr<Value> emitExtract(ExtractOp op) {
    auto read = op.getInput().getDefiningOp<ReadOp>();
    if (!read)
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";
    auto property = read.getInput().getDefiningOp<ClassPropertyRefOp>();
    if (!property)
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";

    auto it = arrayCandidates.find(property.getProperty());
    if (it == arrayCandidates.end())
      return op.emitError()
             << "randomize constraints only support constant indices into "
                "1-dim unpacked rand arrays";

    auto index = op.getLowBit();
    if (index >= it->second.size())
      return op.emitError()
             << "randomize constraints only support in-bounds constant indices "
                "into 1-dim unpacked rand arrays";
    return it->second[index];
  }

  FailureOr<Value> emitEq(EqOp, Value lhs, Value rhs) {
    return arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::eq, lhs,
                                 rhs)
        .getResult();
  }

  FailureOr<Value> emitSgt(SgtOp, Value lhs, Value rhs) {
    return arith::CmpIOp::create(builder, loc, arith::CmpIPredicate::sgt, lhs,
                                 rhs)
        .getResult();
  }
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
        .template Case<ExtractOp>(
            [&](auto op) { return emitter.emitExtract(op); })
        .template Case<DynExtractRefOp>([](auto op) -> FailureOr<Value> {
          return op.emitError()
                 << "randomize constraints only support constant indices into "
                    "1-dim unpacked rand arrays";
        })
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

static unsigned getCandidateCount(ConstraintProblemDesc &problem) {
  unsigned count = 0;
  for (auto &field : problem.randFields)
    count += field.elementCount;
  return count;
}

static func::FuncOp emitRandomizeCheckHelper(ModuleOp module,
                                             OpBuilder &builder,
                                             ConstraintProblemDesc &problem) {
  auto name = getRandomizeCheckHelperName(problem.cls);
  if (auto fn = module.lookupSymbol<func::FuncOp>(name))
    return fn;

  auto *ctx = module.getContext();
  auto ptrTy = LLVM::LLVMPointerType::get(ctx);
  auto i1Ty = IntegerType::get(ctx, 1);
  auto i64Ty = IntegerType::get(ctx, 64);
  SmallVector<Type> inputs(1, ptrTy);
  inputs.append(getCandidateCount(problem), i64Ty);

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto helper = func::FuncOp::create(builder, problem.cls.getLoc(), name,
                                     builder.getFunctionType(inputs, {i1Ty}));
  helper.setPrivate();
  auto *body = helper.addEntryBlock();
  builder.setInsertionPointToEnd(body);

  SmallVector<Value> candidates(body->args_begin() + 1, body->args_end());
  PredicateEmitter emitter(builder, problem.cls.getLoc(), problem, candidates);
  ConstraintExprLowerer<PredicateEmitter> lowerer(emitter);

  Value result = arith::ConstantOp::create(builder, problem.cls.getLoc(),
                                           builder.getBoolAttr(true));
  for (auto block : problem.constraints) {
    auto *terminator = block.constraint.getBody().front().getTerminator();
    auto yield = dyn_cast<YieldOp>(terminator);
    if (!yield) {
      block.constraint.emitError()
          << "randomize constraint block must terminate with moore.yield";
      return {};
    }

    auto term = lowerer.lower(yield.getOperand());
    if (failed(term))
      return {};
    result = *term;
  }

  func::ReturnOp::create(builder, problem.cls.getLoc(), result);
  return helper;
}

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
  auto checkHelper = emitRandomizeCheckHelper(module, builder, problem);
  if (!checkHelper)
    return failure();

  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointAfter(checkHelper);
  auto helper = func::FuncOp::create(builder, problem.cls.getLoc(), name,
                                     builder.getFunctionType({ptrTy}, {i1Ty}));
  helper.setPrivate();
  auto *body = helper.addEntryBlock();
  builder.setInsertionPointToEnd(body);

  auto solver = func::CallOp::create(builder, problem.cls.getLoc(),
                                     runtime.create, ValueRange{})
                    .getResult(0);
  SolverStringCache strings;
  SolverEmitter emitter(module, builder, problem.cls.getLoc(), problem, runtime,
                        strings, solver);
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
  auto *entryBlock = builder.getBlock();
  auto *crosscheckBlock = builder.createBlock(&helper.getBody());
  auto *successBlock = builder.createBlock(&helper.getBody());
  auto *failBlock = builder.createBlock(&helper.getBody());
  builder.setInsertionPointToEnd(entryBlock);
  cf::CondBranchOp::create(builder, problem.cls.getLoc(), solved,
                           crosscheckBlock, failBlock);

  builder.setInsertionPointToEnd(crosscheckBlock);
  SmallVector<Value> modelValues;
  modelValues.push_back(body->getArgument(0));
  emitter.emitModelValues(modelValues);
  auto crosschecked = func::CallOp::create(builder, problem.cls.getLoc(),
                                           checkHelper, modelValues)
                          .getResult(0);
  cf::CondBranchOp::create(builder, problem.cls.getLoc(), crosschecked,
                           successBlock, failBlock);

  builder.setInsertionPointToEnd(successBlock);
  emitter.emitCommitScalarFields(body->getArgument(0),
                                 ArrayRef<Value>(modelValues).drop_front());
  emitter.callVoid(runtime.destroy, solver);
  auto trueValue = arith::ConstantOp::create(builder, problem.cls.getLoc(),
                                             builder.getBoolAttr(true))
                       .getResult();
  func::ReturnOp::create(builder, problem.cls.getLoc(), trueValue);

  builder.setInsertionPointToEnd(failBlock);
  emitter.callVoid(runtime.destroy, solver);
  auto falseValue = arith::ConstantOp::create(builder, problem.cls.getLoc(),
                                              builder.getBoolAttr(false))
                        .getResult();
  func::ReturnOp::create(builder, problem.cls.getLoc(), falseValue);
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
