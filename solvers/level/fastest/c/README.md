# LevelSolver — C baseline

| File | Role |
|---|---|
| `legacy_original.c` | The historical solver, preserved as supplied. Do not edit. |
| `level_solver.h/.c` | Parameterized adaptation of the same algorithm, as a library. |

## legacy_original.c

A historical snapshot and the project's first performance baseline. It is
built as its own executable (`legacy_original`), and its search kernel is also
included, unmodified, into the equivalence test and the benchmark through
`tests/legacy_embed.h`.

**Changes from the supplied source:** none to the code. The text was saved as
UTF-8/ASCII with Unix line endings and a final newline.

**Characteristics that the adaptation keeps:**

- static node storage and a static explicit-frame search stack;
- no recursion, no dynamic allocation;
- demand-driven node creation, next unused index;
- automatic color assignment while matching output;
- links tried against every existing node in index order, then one new node
  if the budget allows;
- explicit undo states for backtracking.

**Known limitations of the original (left as they are):**

1. The target `010010001` and its length are compiled in.
2. At most 10 nodes (`MAX_NODES`).
3. The stack holds 10000 frames and `push` does not check for overflow. The
   search uses up to 6 frames per target bit, so a sufficiently long target
   would write past the array. The compiled-in target is far below that.
4. Results are printed, not returned; fields the search never decided print
   as `?`.
5. `main` starts at a budget of 2 nodes, never 1. Combined with the move
   order (WHITE before BLACK before a new node), a target that one node can
   produce may be reported with 2 nodes: for a target of all `1`s, budget 2
   yields a 2-node graph. For targets needing 2 or more nodes the first
   success of the 2, 3, 4, ... loop is a true minimum.
6. `print_sol` takes a `max` parameter it does not use (a compiler warning).

## level_solver.c

`search()` is the original `solve()` with these differences only:

- target and length are runtime arguments;
- `push` checks capacity and the search returns `RESOURCE_LIMIT` when full;
- a loop-iteration counter, a peak-stack counter and an optional iteration
  limit;
- success returns to the caller, which copies the graph out, fills fields the
  search left undecided, and verifies the result with the independent
  interpreter in `core/rev`.

States, transitions, undo actions and move order are identical.
`tests/test_legacy_equivalence.c` checks that both produce the same first
solution on every target up to 10 bits at budgets 1–5 and on random longer
targets.

**Limits:** node budget 1..64 (`REV_LEVEL_MAX_NODES`); search stack
`REV_LEVEL_MAX_STACK` frames (default 10000, a compile-time setting), which
guarantees targets up to 1666 bits and reports `RESOURCE_LIMIT` rather than
failing beyond what fits; not reentrant or thread-safe (static state).

A budget is an upper bound. The returned graph may use fewer nodes than the
budget, and is not necessarily the smallest one that fits it; see
`solvers/minimal` for certified minima.
