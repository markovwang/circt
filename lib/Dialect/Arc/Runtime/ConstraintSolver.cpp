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

#ifdef CIRCT_ARC_HAS_BITWUZLA_RANDOMIZE

#include <bitwuzla/c/bitwuzla.h>

namespace {
struct SolverState {
  BitwuzlaTermManager *termManager = nullptr;
  Bitwuzla *solver = nullptr;
  BitwuzlaOptions *options = nullptr;

  SolverState() {
    termManager = bitwuzla_term_manager_new();
    options = bitwuzla_options_new();
    bitwuzla_set_option(options, BITWUZLA_OPT_PRODUCE_MODELS, 1);
    solver = bitwuzla_new(termManager, options);
  }

  ~SolverState() {
    if (solver)
      bitwuzla_delete(solver);
    if (options)
      bitwuzla_options_delete(options);
    if (termManager)
      bitwuzla_term_manager_delete(termManager);
  }
};

static SolverState *asState(void *solver) {
  return reinterpret_cast<SolverState *>(solver);
}

static BitwuzlaTerm asTerm(void *term) {
  return reinterpret_cast<BitwuzlaTerm>(term);
}

static void *asOpaque(BitwuzlaTerm term) {
  return reinterpret_cast<void *>(term);
}
} // namespace

bool arcRuntimeSolverIsAvailable(void) { return true; }

void *arcRuntimeSolverCreate(void) { return new SolverState(); }

void arcRuntimeSolverDestroy(void *solver) { delete asState(solver); }

void *arcRuntimeSolverBvVar(void *solver, const char *name, uint32_t width) {
  auto *state = asState(solver);
  auto sort = bitwuzla_mk_bv_sort(state->termManager, width);
  return asOpaque(bitwuzla_mk_const(state->termManager, sort, name));
}

void *arcRuntimeSolverBvConst(void *solver, uint64_t value, uint32_t width) {
  auto *state = asState(solver);
  auto sort = bitwuzla_mk_bv_sort(state->termManager, width);
  return asOpaque(bitwuzla_mk_bv_value_uint64(state->termManager, sort, value));
}

void *arcRuntimeSolverEq(void *solver, void *lhs, void *rhs) {
  auto *state = asState(solver);
  return asOpaque(bitwuzla_mk_term2(state->termManager, BITWUZLA_KIND_EQUAL,
                                    asTerm(lhs), asTerm(rhs)));
}

void *arcRuntimeSolverSgt(void *solver, void *lhs, void *rhs) {
  auto *state = asState(solver);
  return asOpaque(bitwuzla_mk_term2(state->termManager, BITWUZLA_KIND_BV_SGT,
                                    asTerm(lhs), asTerm(rhs)));
}

void arcRuntimeSolverAssert(void *solver, void *term) {
  auto *state = asState(solver);
  bitwuzla_assert(state->solver, asTerm(term));
}

bool arcRuntimeSolverCheck(void *solver) {
  auto *state = asState(solver);
  return bitwuzla_check_sat(state->solver) == BITWUZLA_SAT;
}

uint64_t arcRuntimeSolverGetBv(void *solver, void *term) {
  auto *state = asState(solver);
  BitwuzlaTerm value = bitwuzla_get_value(state->solver, asTerm(term));
  const char *bits = bitwuzla_term_value_get_str(value);
  uint64_t result = 0;
  for (const char *p = bits; *p; ++p)
    result = (result << 1) | static_cast<uint64_t>(*p == '1');
  return result;
}

#else

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

#endif
