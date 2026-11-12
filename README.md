# CarloBomb

Research code for Runnable Existential Volumes (REVs): small two-pointer
graph machines that emit binary strings, and exact search for the smallest
REV that emits a given string.

**Status: Milestone 2.** Milestone 0 delivered the REV execution model, the
preserved historical C solver, a reusable C LevelSolver, a MinimalSolver,
independent verification and baseline benchmarks. Milestone 1 added
experimental hand-written AMD64 assembly search kernels that perform the
same search 3 to 4 times faster on the machine measured; `amd64-auto` is the
preferred experimental AMD64 engine and the C solver is still the default.
Milestone 2 adds the first Bomb: FixedRunBomb, a table of minimum-node REVs
for every binary string of one length up to 16 bits, with the software to
build it restartably and in parallel and to check it. Every 16-bit string
turns out to need at most 7 nodes (`benchmarks/FIXED_RUN_16.md`). No neural
guidance and no LevelSearchBomb yet.

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

Developed and tested on Linux x86-64 with GCC 13 and Clang 18. Building a
FixedRunBomb needs POSIX (worker processes, file locks); the rest of the code
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

`solve`, `level` and `bench` accept `--engine c` (default) or, in Linux
x86-64 builds, an experimental assembly engine:

```bash
./build/carlobomb engines                                              # list them
./build/carlobomb level --target 010010001 --nodes 4 --engine amd64-asm   # reference assembly kernel
./build/carlobomb solve --target 010010001 --engine amd64-auto            # fastest kernel for the budget
```

All engines give the same answers, graphs and step counts.

`--max-steps K` caps search effort per node budget. Exit status: 0 success;
1 no solution within the budget, or verification failed; 2 bad usage or
input; 3 a resource limit stopped the search, so the answer is unknown.

Graph files are plain text; the format is described in `core/rev/SPEC.md`.

### FixedRunBomb

```bash
./build/carlobomb bomb build    --bits 16 --workers 8 --work-dir bomb-work      # build the table
./build/carlobomb bomb resume   --work-dir bomb-work                            # go on after an interruption
./build/carlobomb bomb status   --work-dir bomb-work                            # progress, histogram, effort
./build/carlobomb bomb finalize --work-dir bomb-work --output fixed-run-16.frb  # write the table
./build/carlobomb bomb verify   --file fixed-run-16.frb --work-dir bomb-work --recertify
./build/carlobomb bomb lookup   --file fixed-run-16.frb --target 0100100011010110
```

The whole 16-bit table takes seconds to build. What the commands do, the
options, and what is and is not certified: `bombs/fixed-run/README.md`. The
file format: `bombs/fixed-run/FORMAT.md`.

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
solvers/level/experimental/  challengers
  amd64/asm/                 AMD64 assembly search kernel (Linux, System V ABI)
  amd64/repr/                graph-representation experiments built on it
solvers/minimal/             MinimalSolver: LevelSolver at budgets 1, 2, 3, ...
solvers/fixed/               FixedSolver (planned)
bombs/fixed-run/             FixedRunBomb: table format (FORMAT.md), enumerator,
                             journaled construction, reports, and their tests
bombs/level-search/          LevelSearchBomb (planned)
tests/                       unit tests, exhaustive oracle, legacy and assembly
                             equivalence, CLI checks
benchmarks/                  baseline.c (BASELINE.md), asm_comparison.c (ASM_RESULTS.md),
                             repr_comparison.c (REPR_RESULTS.md),
                             characterize.c (WORKLOAD_RESULTS.md), measure_machine.sh,
                             FIXED_RUN_16.md
tools/                       carlobomb.c, the command-line tool; fixed_run_cli.c,
                             its bomb commands
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
5. **Assembly equivalence** (`tests/test_asm_equivalence.c`, Linux x86-64).
   Requires every assembly kernel to match the C LevelSolver on status,
   graph, step count, peak stack depth and limit behavior, and checks each
   kernel's memory use with guard bytes, guard pages and a
   register-preservation check. The oracle is also run against every
   assembly kernel (`test_oracle_amd64`).

6. **FixedRunBomb** (`bombs/fixed-run/tests/test_fixed_run.c`,
   `tests/bomb_smoke.cmake`). A table entry counts as a certified minimum
   only when its witness has been run on the interpreter and every smaller
   node count has been excluded by a completed exhaustive enumeration or a
   `PROVEN_UNSAT` search; a search that hit a limit excludes nothing. The
   tests build small tables several ways and compare every minimum with the
   MinimalSolver, interrupt and kill constructions and resume them, damage
   and forge journal records, feed in engines that lie, and build the whole
   16-bit table and compare a checksum of the file.

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

```bash
./build/asm_comparison
```

Compares the C LevelSolver with the AMD64 assembly engine (Linux x86-64).
Recorded results in `benchmarks/ASM_RESULTS.md`; design and optimization log
in `solvers/level/experimental/amd64/asm/README.md`.

```bash
./build/repr_comparison
```

Compares the assembly kernels with each other by node budget and target
length. Recorded results in `benchmarks/REPR_RESULTS.md`; the experiments
and what they showed in `solvers/level/experimental/amd64/repr/README.md`.

```bash
./build/characterize
sh benchmarks/measure_machine.sh
```

`benchmarks/FIXED_RUN_16.md` records the construction of the 16-bit
FixedRunBomb: coverage by node count, cost, and the commands to repeat it.

`characterize` measures completed and cut-off searches, short and long,
separately, reports how many search steps were executed and how many were
accounted for in bulk, and reads hardware counters where the machine has
them (`benchmarks/WORKLOAD_RESULTS.md`). `measure_machine.sh` runs
everything on a new machine and writes one report.
