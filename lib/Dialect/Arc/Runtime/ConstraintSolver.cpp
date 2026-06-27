//===- ConstraintSolver.cpp - Arc constraint solver runtime ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define ARC_RUNTIME_ENABLE_EXPORT

#include "circt/Dialect/Arc/Runtime/ConstraintSolver.h"

#include <cassert>

#ifndef CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE

bool arcRuntimeSolverIsAvailable(void) { return false; }

void *arcRuntimeSolverCreate(void) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}

void arcRuntimeSolverDestroy(void *) {}

void *arcRuntimeSolverBvVar(void *, const char *, uint32_t) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}

void *arcRuntimeSolverBvConst(void *, uint64_t, uint32_t) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}

void *arcRuntimeSolverEq(void *, void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}

void *arcRuntimeSolverSgt(void *, void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return nullptr;
}

void arcRuntimeSolverAssert(void *, void *) {}

bool arcRuntimeSolverCheck(void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return false;
}

uint64_t arcRuntimeSolverGetBv(void *, void *) {
  assert(false && "Arc constraint solver runtime was built without Bitwuzla");
  return 0;
}

#endif // CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE
