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
