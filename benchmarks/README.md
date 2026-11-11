# Benchmarks

`baseline.c` times the unmodified legacy search kernel against the
parameterized C LevelSolver on the same targets.

    ./build/baseline                 # built-in workload list
    ./build/baseline 010010001 0110  # your own targets

Method:

- Both solvers follow the protocol of the legacy `main`: node budgets
  2, 3, ..., 10, stopping at the first success. Only that loop is timed.
- The legacy source is included as is (`tests/legacy_embed.h`); its printing
  is compiled out and its target variables are pointed at each workload.
- LevelSolver's time includes input validation, graph completion and the
  independent verification of the result, since they are part of its API.
- Each sample repeats the run until it lasts at least 50 ms; 9 samples are
  taken and the minimum and median time per run are reported.
- `steps` and `peak stack` come from LevelSolver's counters. The legacy code
  has no counters and none were added.

Recorded results are in `BASELINE.md`. They are observations on one machine,
not evidence that either implementation is faster in general.

## Assembly comparison

`asm_comparison.c` times the C LevelSolver against the AMD64 assembly engine
(built on Linux x86-64 only).

    ./build/asm_comparison

- Same protocol as above (budgets 2..10, stop at first success), through each
  engine's public API.
- Before timing, the engines are compared budget by budget: status, graph,
  step count and peak stack must agree, or the run is marked and the program
  exits non-zero.
- Engines are sampled alternately, 15 samples each of at least 40 ms, after a
  warm-up. Minimum and median are reported.
- Time-stamp-counter ticks per search step are reported as a rough,
  frequency-independent figure. They are not core cycles.
- A third column times the assembly kernel without its C wrapper.
- To compare against both compilers, build and run once with GCC and once
  with Clang; the assembly object is the same in both.

Recorded results are in `ASM_RESULTS.md`.
