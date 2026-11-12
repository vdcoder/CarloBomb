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

## Kernel comparison by budget and length

`repr_comparison.c` compares the assembly kernels with one another.

    ./build/repr_comparison                         # all kernels, budgets 3-8
    ./build/repr_comparison -k hyb,n8x -n 4,8,12 -l 16,32

- A cell is one node budget and one target length. Its workload is a fixed
  set of pseudo-random targets, each searched with a step cap (200,000), so
  every kernel performs the same steps; result codes and step counts are
  checked to agree.
- The set is grown until one pass is at least 1.5 million steps. A short
  workload repeated many times is memorized by the branch predictor and runs
  two to three times faster than the same searches would in real use; `-s`
  sets the size, and a small value reproduces that effect on purpose.
- Only the kernel is timed (no validation, export or verification).
- Timing is round-robin with rotating order; a kernel's speedup is the
  median over rounds of (reference time / kernel time) within the same
  round, which cancels slow drift of the machine. The reference is kernel 0,
  the unmodified kernel of `search_sysv.S`.
- `asm_comparison -k NAME` runs the C-versus-assembly benchmark above with
  another kernel, for example `-k auto`.

Recorded results are in `REPR_RESULTS.md`.
