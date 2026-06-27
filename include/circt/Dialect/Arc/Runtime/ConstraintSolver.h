//===- ConstraintSolver.h - Arc constraint solver runtime API ---*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H
#define CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H

#include "circt/Dialect/Arc/Runtime/ArcRuntime.h"

#include <stdbool.h>
#include <stdint.h>

ARC_RUNTIME_EXPORT bool arcRuntimeSolverIsAvailable(void);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverCreate(void);
ARC_RUNTIME_EXPORT void arcRuntimeSolverDestroy(void *solver);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverBvVar(void *solver, const char *name,
                                               uint32_t width);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverBvConst(void *solver, uint64_t value,
                                                 uint32_t width);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverEq(void *solver, void *lhs, void *rhs);
ARC_RUNTIME_EXPORT void *arcRuntimeSolverSgt(void *solver, void *lhs,
                                             void *rhs);
ARC_RUNTIME_EXPORT void arcRuntimeSolverAssert(void *solver, void *term);
ARC_RUNTIME_EXPORT bool arcRuntimeSolverCheck(void *solver);
ARC_RUNTIME_EXPORT uint64_t arcRuntimeSolverGetBv(void *solver, void *term);

#endif // CIRCT_DIALECT_ARC_RUNTIME_CONSTRAINTSOLVER_H
