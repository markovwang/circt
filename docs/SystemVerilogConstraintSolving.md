# SystemVerilog Constraint Solving

This note records follow-up work for full SystemVerilog class constraint
support in the Verilog front-end. The current first milestone imports class
constraint blocks into Moore IR and preserves simple predicate bodies, but it
does not implement `randomize()`, constraint solving, or runtime behavior.

## Current Milestone

The importer accepts class constraint declarations and represents them as
`moore.class.constraintdecl` operations inside the owning class declaration.
Simple expression constraints and lists are preserved as predicate regions.
Unsupported constraint forms are diagnosed instead of being accepted silently.

This is intentionally an IR-preservation step. It should not be interpreted as
solver support, arcilator support, or SystemVerilog randomization semantics.

## Known Follow-Up Gaps

Before building solver behavior on top of the first milestone, resolve these
IR preservation issues:

- Constraint body symbol visibility: constraint bodies currently sit in their
  own nested operation. Any representation that isolates the body must still let
  all symbol users resolve class-level and module-level symbols, including
  static class properties, inherited properties, globals, and helper ops such as
  class upcasts. Do not rely on one-off fallbacks for only one op kind.
- Extern constraint definitions: out-of-block definitions such as
  `constraint Packet::c_len { ... }` need an explicit import strategy. Either
  attach the external body to the class constraint declaration or diagnose that
  this form is not supported yet. Silently keeping only an empty extern
  declaration is incomplete IR.

These are preservation problems, not solver problems. They should be fixed
before another thread starts implementing `randomize()` or solver lowering.

## Solver Scope

A real solver should model the SystemVerilog constraint system rather than
lowering constraints directly to ordinary procedural code. At minimum it should
handle:

- `rand` and `randc` class properties.
- Constraint enablement through `constraint_mode`.
- Object randomization through `randomize()`.
- Inline `with` constraints.
- Constraint inheritance and override specifiers.
- `solve before` ordering.
- `soft` constraints and `disable soft`.
- `dist` weights.
- `foreach`, implication, conditional, and uniqueness constraints.
- Deterministic seeding and reproducibility.

These features interact, so they should be designed together before extending
the first milestone beyond simple predicate preservation.

## Suggested Architecture

Keep parsed constraints in Moore IR as the source-level representation, then add
a separate lowering or analysis step that extracts a solver problem from a class
type and a specific `randomize()` call site.

The solver-facing representation should separate:

- Random variables and their domains.
- Hard constraints.
- Soft constraints and their disable rules.
- Solve ordering constraints.
- Distribution weights.
- Inheritance and override metadata.
- Runtime inputs, including existing object field values and inline `with`
  constraints.

The lowering from Moore IR to this representation should be explicit and
testable. Avoid embedding solver assumptions in the importer; the importer
should continue to preserve source structure and report unsupported source
forms precisely.

## Implementation Stages

1. Add a solver problem representation independent of any backend runtime.
2. Lower a small subset of Moore class constraints into that representation:
   integer `rand` fields, equality, relational operators, and ranges.
3. Implement deterministic candidate generation for simple finite integer
   domains.
4. Wire `randomize()` to build and solve a problem for one object instance.
5. Add support for inline `with` constraints.
6. Add solve ordering, soft constraints, distribution weights, and inheritance
   rules.
7. Decide whether arcilator should interpret solver IR directly or call into a
   runtime helper.

Each stage should include negative tests for unsupported SystemVerilog features
so unsupported forms do not silently degrade into incomplete behavior.

## Testing Expectations

Solver work should include both import tests and behavioral tests. Useful
coverage includes:

- Generated Moore IR for constraint declarations remains stable.
- Unsupported constraint kinds produce specific diagnostics.
- `randomize()` remains unsupported until the stage that implements it.
- Once `randomize()` is implemented, repeated runs with the same seed are
  deterministic.
- Failed randomization leaves object state consistent with SystemVerilog
  semantics.
- Inherited constraints and inline constraints compose in the expected order.

Behavioral tests should avoid relying on one arbitrary random value. Prefer
checking invariants over many seeds, deterministic seed replay, and failure
cases where the constraint system is unsatisfiable.
