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
| `amd64/asm/` | Hand-written AMD64 assembly search kernel, Linux / System V ABI | Passes all equivalence and oracle tests; roughly 3.3x faster than GCC-built C and 2.7x faster than Clang-built C on the search-heavy benchmark targets, on one machine. Not promoted: awaiting measurement on a second processor family. |
