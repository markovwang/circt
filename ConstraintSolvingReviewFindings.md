# Constraint Solving Review Findings

This note captures the current review findings for the
`codex-sv-class-constraints` branch. It is meant as a checklist for deciding
what to fix first.

## Scope Confirmed

The last two commits implement a first Moore IR preservation step for
SystemVerilog class constraint blocks.

They do not implement:

- Constraint solving.
- `randomize()` semantics.
- `constraint_mode`.
- Inline `with` constraints.
- Runtime or arcilator behavior.

The branch documentation says this explicitly in
`docs/SystemVerilogConstraintSolving.md`.

## Implemented Today

- Adds `moore.class.constraintdecl`.
- Allows `moore.class.constraintdecl` inside `moore.class.classdecl`.
- Allows `moore.yield` inside constraint declarations and verifies that it
  yields `i1`.
- Imports simple class constraint blocks into Moore IR.
- Preserves simple expression constraints as predicate regions.
- Combines constraint lists with `moore.and`.
- Adds a `%this : !moore.class<@C>` block argument for non-static constraint
  bodies when slang provides `constraint.thisVar`.
- Preserves constraint declaration flags such as `isStatic`, `isPure`,
  `isInitial`, `isExtends`, `isFinal`, and `isExtern`.
- Diagnoses unsupported constraint kinds instead of silently accepting them.
- Keeps `randomize()` unsupported and emits `unsupported system call
  'randomize'`.

## P0 Findings

### Constraint Body Symbol Visibility Is Incomplete

Constraint bodies sit in nested `moore.class.constraintdecl` operations, and
`moore.class.constraintdecl` is `IsolatedFromAbove`.

This currently breaks some symbol users inside constraint bodies.

Observed failures from
`test/Conversion/ImportVerilog/constraints-symbol-visibility.sv`:

- A derived-class constraint referencing a base-class property generates
  `moore.class.upcast`, but `ClassUpcastOp::verifySymbolUses` cannot resolve
  the base class symbol from inside the nested constraint body.
- A multi-level derived-class constraint has the same problem.
- A constraint referencing a static class property generates
  `moore.get_global_variable @"Class::field"`, but
  `GetGlobalVariableOp::verifySymbolUses` cannot resolve the module-level
  global from inside the nested constraint body.

Important detail:

- `ClassPropertyRefOp` has a local fallback for resolving the current parent
  class, but this only covers one op kind and only one class case.
- The documentation already warns against relying on one-off fallbacks.

Suggested direction:

- Add a general symbol-resolution strategy for class-body nested regions, or
  change the constraint body representation so module-level and class-level
  symbols remain visible to all relevant `SymbolUserOpInterface` users.
- Cover at least `ClassPropertyRefOp`, `ClassUpcastOp`, and
  `GetGlobalVariableOp`.

### Missing Regression Test for Symbol Visibility

There is an untracked probe file:

`test/Conversion/ImportVerilog/constraints-symbol-visibility.sv`

It currently documents the intended coverage:

- Same-class property access.
- Inherited property access.
- Multi-level inherited property access.
- Shadowed property access.
- Static class property access.

Suggested direction:

- Turn this into a committed regression test.
- Keep failing cases marked `XFAIL` only while actively fixing the underlying
  symbol visibility issue.
- Remove `XFAIL` when P0 symbol visibility is fixed.

## P0/P1 Findings

### Extern Constraint Definitions Are Not Preserved

Importer behavior today:

- `pure constraint` and `extern constraint` declarations create
  `moore.class.constraintdecl`.
- If `isPure` or `isExtern` is set, the importer returns without creating a
  body.

Risk:

- Out-of-block definitions such as `constraint Packet::c_len { ... }` may be
  silently represented as an empty extern declaration rather than preserving the
  actual body.

Current working-tree update:

- Non-pure `extern constraint` declarations now emit a precise unsupported
  diagnostic instead of being preserved as empty declarations.
- Pure constraint declarations are still preserved as body-less declarations.
- A debug assert records the current slang assumption that pure constraint
  declarations are body-less extern declarations.

This keeps extern constraint definitions unsupported until there is an explicit
body-attachment strategy.

## Narrow Feature Coverage

The importer currently supports only:

- Constraint lists.
- Hard expression constraints.

The importer currently rejects or does not implement:

- `soft` expression constraints.
- Implication constraints.
- Conditional constraints.
- `foreach`.
- `unique`.
- `dist`.
- `solve before`.
- `disable soft`.
- Full inheritance and override semantics.

This is acceptable for the current preservation milestone, but it should remain
explicit in tests and documentation.

## Solver Work Not Started

No dedicated solver problem representation exists yet.

Suggested future architecture from the branch documentation:

- Keep parsed constraints in Moore IR as source-level representation.
- Add a separate lowering or analysis step that extracts a solver problem from
  a class type and a specific `randomize()` call site.
- Keep solver-facing concepts separate:
  - Random variables and domains.
  - Hard constraints.
  - Soft constraints and disable rules.
  - Solve ordering constraints.
  - Distribution weights.
  - Inheritance and override metadata.
  - Runtime inputs and inline `with` constraints.

## Suggested Work Order

1. Fix constraint body symbol visibility.
2. Commit and stabilize the symbol-visibility regression test.
3. Handle extern constraint definitions by preserving the body or diagnosing
   unsupported syntax.
4. Add a solver problem representation independent of arcilator/runtime.
5. Lower a small subset of Moore constraints into that representation.
6. Implement deterministic solving for a tiny finite integer subset.
7. Wire `randomize()` to build and solve a problem for one object instance.
8. Add inline `with` constraints.
9. Add solve ordering, soft constraints, distribution weights, inheritance
   rules, and deterministic seed behavior.
10. Decide whether arcilator should interpret solver IR directly or call into a
    runtime helper.

## Verification Performed

Commands run after rebuilding relevant tools:

```sh
ninja -C build bin/circt-verilog
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/constraints.sv
build/bin/circt-verilog --ir-moore test/Conversion/ImportVerilog/constraints-symbol-visibility.sv
ninja -C build bin/circt-opt
build/bin/circt-opt test/Dialect/Moore/classes.mlir
```

Results:

- `bin/circt-verilog` rebuilt successfully.
- `constraints.sv` imports successfully and emits `moore.class.constraintdecl`
  regions.
- A minimal `randomize()` input emits `unsupported system call 'randomize'`.
- `constraints-symbol-visibility.sv` fails as expected, reproducing the symbol
  visibility bug.
- `bin/circt-opt` rebuilt successfully.
- `test/Dialect/Moore/classes.mlir` parses and verifies successfully.

## Workspace State Noted During Review

The only untracked file observed was:

`test/Conversion/ImportVerilog/constraints-symbol-visibility.sv`
