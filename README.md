# CarloBomb

Research code for Runnable Existential Volumes (REVs): small two-pointer
graph machines that emit binary strings, and exact search for the smallest
REV that emits a given string.

**Status: Milestone 3.** Milestone 0 delivered the REV execution model, the
preserved historical C solver, a reusable C LevelSolver, a MinimalSolver,
independent verification and baseline benchmarks. Milestone 1 added
experimental hand-written AMD64 assembly search kernels that perform the
same search 3 to 4 times faster on the machine measured; `amd64-auto` is the
preferred experimental AMD64 engine and the C solver is still the default.
Milestone 2 added the first Bomb: FixedRunBomb, a table of minimum-node REVs
for every binary string of one length up to 16 bits, with the software to
build it restartably and in parallel and to check it. Milestone 3 adds the
solver that reads it: for any target of 1 to 16 bits it returns the certified
minimum-node REV by lookup, with no search. Every string of L ≤ 16 bits
needs at most ⌈2√L⌉ − 1 nodes, 7 at 16 bits (`benchmarks/FIXED_RUN_16.md`,
`benchmarks/FIXED_BOMB_SOLVER.md`). A longer table is built as a ladder,
each length from the complete table for one bit fewer: to 24 bits in format
version 2 (`bombs/fixed-run/OVERNIGHT.md`,
`benchmarks/FIXED_RUN_24_FEASIBILITY.md`), and from 25 to 31 bits in format
version 3 (`bombs/fixed-run/BEYOND_24.md`,
`benchmarks/FIXED_RUN_31_FEASIBILITY.md`). The format reaches 31 bits; what
one machine can compute is 26 in a day and 28 in a week. A complete Bomb can
also be written in a compact format, version 4: the same REVs in 38% of the
space, with the answer for every shorter string in the file
(`benchmarks/COMPACT_FORMAT.md`). No neural guidance and no LevelSearchBomb
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

```bash
./build/carlobomb bomb ladder --dir ladder --from fixed-run-16.frb --to 20 --workers 8   # 17, 18, 19, 20 bits
./build/carlobomb bomb ladder --dir ladder --status                                       # what is there
./build/carlobomb bomb verify --file ladder/fixed-run-20.frb --work-dir ladder/work-20 --against fixed-run-16.frb
```

Longer tables, up to 31 bits, are built one length at a time on the table
below; each finished length is a complete certified Bomb, and the command
continues where it stopped. To 20 bits takes about a minute; to 24, hours
(`bombs/fixed-run/OVERNIGHT.md`); 25 and 26 on a finished 24-bit Bomb, a
night and a day (`bombs/fixed-run/BEYOND_24.md`).

A complete table in the compact format, and back:

```bash
./build/carlobomb bomb compact --file ladder/fixed-run-20.frb --output fixed-run-20.compact.frb   # 35.7 MB -> 13.3 MB
./build/carlobomb bomb verify  --file fixed-run-20.compact.frb --against ladder/fixed-run-20.frb
./build/carlobomb bomb lookup  --file fixed-run-20.compact.frb --target 01001000        # any length up to 20 bits
./build/carlobomb bomb expand  --file fixed-run-20.compact.frb --output again.frb       # the same bytes as the source
```

The source is only read, and no file is ever replaced. The compact file
holds the same REVs and is derived from the fixed-width one, which stays the
certified artifact (`bombs/fixed-run/README.md`, "The compact format").

### Bomb-backed FixedSolver

```bash
./build/carlobomb fixed --bomb fixed-run-16.frb --target 01001000             # minimum-node REV, no search
./build/carlobomb fixed --bomb fixed-run-16.frb --target 01001000 --max-nodes 2   # proven impossible
./build/carlobomb fixed --bomb fixed-run-16.frb --target 0100100011010110 --profile
./build/carlobomb fixed --bomb fixed-run-16.frb --stats                       # every length, 1 to 16 bits
./build/carlobomb fixed --bomb ladder/fixed-run-20.frb --stats                # every length, 1 to 20 bits
```

Any target of 1 to 16 bits is answered from the table: the certified
minimum-node REV, in a fifth of a microsecond once the Bomb is loaded. A
longer target is reported as unsupported, with the lower bound the Bomb does
establish for it. How it works and why it is exact:
`solvers/fixed/bomb/README.md`.

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
solvers/fixed/               FixedSolver(N, L) (planned)
solvers/fixed/bomb/          Bomb-backed FixedSolver: targets of 1 to 16 bits by
                             lookup in a FixedRunBomb, with its tests
bombs/fixed-run/             FixedRunBomb: table format (FORMAT.md), enumerator,
                             journaled construction, reports, and their tests
bombs/level-search/          LevelSearchBomb (planned)
tests/                       unit tests, exhaustive oracle, legacy and assembly
                             equivalence, CLI checks
benchmarks/                  baseline.c (BASELINE.md), asm_comparison.c (ASM_RESULTS.md),
                             repr_comparison.c (REPR_RESULTS.md),
                             characterize.c (WORKLOAD_RESULTS.md), measure_machine.sh,
                             FIXED_RUN_16.md, fixed_bomb_lookup.c and
                             fixed_bomb_longer.c (FIXED_BOMB_SOLVER.md)
tools/                       carlobomb.c, the command-line tool; fixed_run_cli.c,
                             its bomb commands; fixed_bomb_cli.c, its fixed command
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
   `tests/test_fixed_run_ladder.c` does the same for format versions 2 and
   3 and the ladder, levels of one version built on tables of another
   included: every level of small ladders against the MinimalSolver string
   by string, stopped and resumed, on base tables that are partial, missing,
   exchanged or forged; and one level above the 16-bit table, whose checksum
   is compared.

7. **Bomb-backed FixedSolver** (`solvers/fixed/bomb/tests/`,
   `tests/fixed_smoke.cmake`). All 131,070 strings of 1 to 16 bits are
   answered and each answer is compared with a direct scan of the table and
   with the MinimalSolver searching from one node up; the returned graph is
   run against the target. A Bomb that is missing, damaged, partial or
   self-contradicting is refused. The lookup library links no search solver.

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

```bash
./build/fixed_bomb_lookup fixed-run-16.frb
./build/fixed_bomb_longer fixed-run-16.frb
```

`fixed_bomb_lookup` checks the Bomb-backed FixedSolver on every string of 1
to 16 bits and times loading, index construction and lookups; it has no
search solver in it. `fixed_bomb_longer` measures what the Bomb knows about
longer targets. Results, and the minimum-node counts for every length, in
`benchmarks/FIXED_BOMB_SOLVER.md`.

`characterize` measures completed and cut-off searches, short and long,
separately, reports how many search steps were executed and how many were
accounted for in bulk, and reads hardware counters where the machine has
them (`benchmarks/WORKLOAD_RESULTS.md`). `measure_machine.sh` runs
everything on a new machine and writes one report.
