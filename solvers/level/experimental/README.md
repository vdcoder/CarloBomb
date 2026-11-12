# LevelSolver — experimental

Challenger implementations of LevelSolver go here.

A challenger is promoted to `../fastest/` only after it

1. passes the independent verification and exhaustive-oracle tests
   (`tests/test_level_solver.c`, `tests/test_oracle.c`), and
2. beats the current champion in a reproducible run of the benchmark
   (`benchmarks/`), with the measurements recorded.

## Current challengers

| Directory | What | Status |
|---|---|---|
| `amd64/asm/` | Hand-written AMD64 assembly search kernel, Linux / System V ABI (engine `amd64-asm`) | Passes all equivalence and oracle tests; roughly 3.3x faster than GCC-built C and 2.7x faster than Clang-built C on the search-heavy benchmark targets, on one machine. Not promoted: awaiting measurement on a second processor family. |
| `amd64/repr/` | Graph-representation experiments on the same search: packed, register-resident and vector-resident graphs, frame size, bulk accounting (engines `amd64-NAME`; `amd64-auto` picks per node budget) | All pass the same tests. Best result: about 20% faster than `amd64/asm/` at node budgets 4 to 8 (about 3.9x over GCC-built C). Packed 16-bit nodes and vector registers are slower. Same promotion gate. |
