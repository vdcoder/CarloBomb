# CarloBomb

Research code for Runnable Existential Volumes (REVs): small two-pointer
graph machines that emit binary strings, and exact search for the smallest
REV that emits a given string.

**Status: Milestone 0.** The REV execution model, the preserved historical C
solver, a reusable C LevelSolver, a MinimalSolver, independent verification
and baseline benchmarks. No neural guidance, assembly, parallelism or Bombs
yet.

## Build and test

Requires CMake 3.16+ and a C11 compiler. No other dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

With AddressSanitizer and UBSan (GCC or Clang):

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DCARLOBOMB_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

`-DCARLOBOMB_WERROR=ON` turns warnings into errors.

Developed and tested on Linux x86-64 with GCC 13 and Clang 18. The code
avoids POSIX-only APIs apart from the timer (which has a Windows branch), but
a Windows/MSVC build has not been tried.

## Command-line tool

```bash
./build/carlobomb solve  --target 010010001 --max-nodes 10   # minimum-node REV
./build/carlobomb level  --target 010010001 --nodes 4        # any REV within a budget
./build/carlobomb solve  --target 010010001 --out g.rev      # save the graph
./build/carlobomb verify --target 010010001 --graph g.rev    # check a saved graph
./build/carlobomb run    --graph g.rev --bits 20             # print its output
./build/carlobomb bench  --target 010010001 --max-nodes 10   # time a minimal solve
```

```
REV solution (minimum: budgets 1..2 proven to have none)
Search steps: 731
Nodes: 3
Target: 010010001

Node   Color   Link0   Link1
0      B (1)   2       1
1      W (0)   0       0
2      W (0)   1       2

Verification: PASS
```

`--max-steps K` caps search effort per node budget. Exit status: 0 success;
1 no solution within the budget, or verification failed; 2 bad usage or
input; 3 a resource limit stopped the search, so the answer is unknown.

Graph files are plain text; the format is described in `core/rev/SPEC.md`.

## What the answers mean

| Answer | Meaning |
|---|---|
| `SOLVED` | The returned REV is fully specified and has been run on the independent interpreter. |
| `PROVEN_UNSAT` | The search was exhaustive: no REV within the node budget emits the target. |
| `RESOURCE_LIMIT` / `UNKNOWN` | The search stopped early. Nothing is claimed about whether a solution exists. |
| `MINIMAL` | `SOLVED`, and every smaller node budget was `PROVEN_UNSAT` in the same call. |
| `BUDGET_EXHAUSTED` | Every budget up to the cap was `PROVEN_UNSAT`; a solution needs more nodes than the cap. |

LevelSolver treats its budget as an upper bound and returns the first
solution in its move order, which is not always the smallest. Use `solve`
(MinimalSolver) for a certified minimum.

## Layout

```
core/rev/                    REV model: SPEC.md, interpreter, validator, text format
solvers/level/fastest/c/     legacy_original.c (historical, unmodified) and the
                             parameterized level_solver built from it
solvers/level/experimental/  challengers (empty)
solvers/minimal/             MinimalSolver: LevelSolver at budgets 1, 2, 3, ...
solvers/fixed/               FixedSolver (planned)
bombs/                       FixedRunBomb, LevelSearchBomb (planned)
tests/                       unit tests, exhaustive oracle, legacy equivalence, CLI check
benchmarks/                  baseline.c and recorded results (BASELINE.md)
tools/carlobomb.c            command-line tool
```

`fastest/` holds the verified champion for a solver; `experimental/` holds
challengers. A challenger is promoted only after it passes the correctness
tests and wins a reproducible benchmark.

## How correctness is established

1. **Independent interpreter** (`core/rev/rev.c`), written from the
   specification and sharing no code with the solvers. Its tests use output
   sequences traced by hand.
2. **Witness checks.** Every solver result in the tests is checked for node
   count within budget, binary colors, in-range links, and an exact prefix
   match when executed. LevelSolver also runs the interpreter on its own
   result before returning.
3. **Exhaustive oracle** (`tests/test_oracle.c`). Enumerates every labeled
   REV with 1 to 4 nodes, collects every output prefix up to 10 bits, and
   requires LevelSolver to answer `SOLVED` on exactly that set and
   `PROVEN_UNSAT` elsewhere, and MinimalSolver to report the same minimum.
   This tests completeness, not just validity. `./build/test_oracle 5`
   extends it to 5 nodes (312.5 million graphs, about 20 seconds).
4. **Legacy equivalence** (`tests/test_legacy_equivalence.c`). Runs the
   unmodified legacy kernel and LevelSolver side by side and requires the
   same verdict and the same graph.

## Limits

- Node budget: 1 to 64.
- Search stack: 10000 frames (`REV_LEVEL_MAX_STACK`, compile-time). Targets
  up to 1666 bits always fit; longer ones are accepted and return
  `RESOURCE_LIMIT` if the stack fills.
- Search time grows steeply with the number of nodes required. On the
  development machine an 8-node, 32-bit target takes about 75 ms; see
  `benchmarks/BASELINE.md`.
- The solvers keep their state in static storage and are not reentrant or
  thread-safe.

## Benchmarks

```bash
./build/baseline
```

Compares the unmodified legacy kernel with LevelSolver on the same targets.
Method in `benchmarks/README.md`, recorded results in
`benchmarks/BASELINE.md`.
