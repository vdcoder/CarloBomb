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

## Workload characterization

`characterize.c` separates the kinds of search and measures each on its own.

    ./build/characterize                 # budgets 3-8
    ./build/characterize -n 6,8 -r 21
    ./build/characterize --perf-selftest

- For each node budget, pseudo-random targets are sorted into six classes:
  searches that complete (SOLVED or PROVEN_UNSAT) in up to 2,000 steps, in
  2,001 to 100,000, and in more; and searches cut off by a step limit of
  2,000, of 100,000 and of 2,000,000. Each class is a set of distinct
  targets of at least 1.5 million steps per pass.
- Per class it reports the C solver's time per step, the speedup of the
  reference assembly kernel (`asm`) and of `auto` over it through the API,
  and `auto` against `asm` through the API and kernel only.
- **Search accounting.** The kernels `n8x-count` and `hybx-count` are `n8x`
  and `hybx` with counters at the bulk-accounting sites; they are for
  benchmarks only, and the ordinary kernels contain no counting code. The
  tool reports the share of logical steps executed one at a time and the
  share accounted for in bulk, and how often a link loop fell back to the
  per-node code. The logical step count itself is unchanged and exact.
- **Hardware counters.** Where `perf_event_open` allows hardware events, the
  tool adds cycles, instructions, branches and branch misses per step for
  the two kernels. Most virtual machines, containers and WSL2 do not allow
  them; the tool says so and carries on. `--perf-selftest` runs the same
  counter code on software events and is part of the test suite.

Recorded results are in `WORKLOAD_RESULTS.md`.

## Measuring another machine

    sh benchmarks/measure_machine.sh

builds, runs the tests, prints the processor, the instruction sets found and
the kernel `auto` selects for each budget, runs the three assembly
benchmarks, and writes everything to one report file. It is the intended way
to check a second processor family (AMD Zen, for example) or to collect
hardware counter readings. For counters use bare-metal Linux with
`/proc/sys/kernel/perf_event_paranoid` at 2 or lower; under WSL2 or in a
virtual machine the report will normally contain timings and instruction-set
selection but no counters.

## The compact format

`compact_bench.c` puts the fixed-width file of a Bomb and its compact file
(format version 4) side by side through the solver that reads both: sizes,
opening, lookups, the comparison of two extensions, memory, and that every
string of every length gets the answer it has to get from both.

    ./build/carlobomb bomb compact --file BOMB.frb --output COMPACT.frb
    ./build/compact_bench --fixed BOMB.frb --compact COMPACT.frb

How the format was chosen and the results on the real Bombs of 16 to 24 bits
are in `COMPACT_FORMAT.md`.
