//===- LowerClassRandomize.cpp - Class randomize prep ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/Moore/MooreOps.h"
#include "circt/Dialect/Moore/MooreTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"

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
  });

  return failure(failedAny);
}
