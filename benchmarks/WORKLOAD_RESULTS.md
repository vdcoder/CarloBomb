# Workload characterization — results

Recorded 2026-11-12 with `./build/characterize -r 11`, two runs, on the
machine of `REPR_RESULTS.md` (Intel Xeon, Emerald Rapids, shared VM; BMI2 and
AVX-512 present). The kernels measured are byte-identical to the ones in
`REPR_RESULTS.md`; nothing in them was changed for this. (The runs were made
from a working tree on top of commit `3f73dee` before the tool itself was
committed, which is why the output says `3f73dee-dirty`.)

This file answers two of the review questions (workload classes, search
accounting). The other two measurements asked for could not be made here:

- **Hardware counters.** This VM exposes no hardware performance counters
  (`perf_event_open` fails with "No such file or directory"; there is no CPU
  event source). `characterize` reads cycles, instructions, branches and
  branch misses per step and prints them when a machine has them; its
  counter code is exercised here with software events only
  (`characterize --perf-selftest`, part of the test suite). No such numbers
  exist yet, and the 55–70% misprediction share in `REPR_RESULTS.md` remains
  a hypothesis from a timing experiment.
- **A second processor family.** No AMD machine was available to this work.
  `benchmarks/measure_machine.sh` runs the tests, reports the instruction
  sets found and the kernel `auto` picks, and runs the benchmarks, in one
  command, for whoever has one. Kernel selection for a CPU without BMI2 is
  tested here by masking BMI2 off, which checks the selection logic and the
  baseline kernels but says nothing about speed on such a CPU.

## Classes

For each node budget, pseudo-random targets of 8 to 48 bits are sorted by
how their search ends and how long it is. "Completed" means the search ran
to its own end (SOLVED or PROVEN_UNSAT); "cut off" means it was stopped by a
step limit, with only targets whose search is longer than the limit. Each
class is a set of distinct targets totalling at least 1.5 million steps per
pass (so 750 searches for the 2,000-step limit and thousands for the short
completed class), which keeps the branch predictor from memorizing it. A
class that does not occur at a budget is left blank; at N = 3 every search
completes within 2,000 steps.

`asm` is the reference assembly kernel, `auto` the per-budget choice (here
`n8x` for all six budgets), C the portable solver. "Through the API"
includes each engine's validation, export and verification; "kernel only"
times the assembly routine alone.

## `auto` against `asm`

Kernel only (run 1 / run 2):

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 1.11 / 1.09 | 1.22 / 1.20 | 1.27 / 1.23 | 1.28 / 1.31 | 1.34 / 1.32 | 1.40 / 1.34 |
| completed, 2,001 to 100,000 | | 1.28 / 1.27 | 1.25 / 1.27 | 1.25 / 1.25 | 1.24 / 1.22 | 1.26 / 1.25 |
| completed, over 100,000 | | | 1.20 / 1.20 | 1.19 / 1.22 | 1.21 / 1.19 | 1.20 / 1.21 |
| cut off at 2,000 steps | | 1.18 / 1.18 | 1.21 / 1.26 | 1.29 / 1.28 | 1.31 / 1.33 | 1.25 / 1.30 |
| cut off at 100,000 | | | 1.21 / 1.18 | 1.21 / 1.22 | 1.22 / 1.21 | 1.15 / 1.19 |
| cut off at 2,000,000 | | | | | 1.20 / 1.17 | 1.20 / 1.17 |

Through the API (run 1 / run 2):

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 1.10 / 1.06 | 1.20 / 1.12 | 1.19 / 1.19 | 1.23 / 1.24 | 1.25 / 1.24 | 1.25 / 1.24 |
| completed, 2,001 to 100,000 | | 1.26 / 1.23 | 1.26 / 1.24 | 1.24 / 1.25 | 1.23 / 1.23 | 1.25 / 1.25 |
| completed, over 100,000 | | | 1.22 / 1.21 | 1.23 / 1.22 | 1.22 / 1.20 | 1.21 / 1.22 |
| cut off at 2,000 steps | | 1.19 / 1.17 | 1.25 / 1.25 | 1.25 / 1.28 | 1.29 / 1.26 | 1.29 / 1.28 |
| cut off at 100,000 | | | 1.20 / 1.21 | 1.22 / 1.22 | 1.19 / 1.20 | 1.20 / 1.22 |
| cut off at 2,000,000 | | | | | 1.17 / 1.16 | 1.15 / 1.17 |

- `auto` is faster in every class at every budget. For budgets 4 to 8 it is
  1.15–1.40 kernel only and 1.12–1.29 through the API; at N = 3,
  1.09–1.11 and 1.06–1.10.
- These ratios depend on where the linker happens to place the kernels by
  a few percent, most at the small budgets, which run closest to the
  processor's instruction throughput. An earlier build of this benchmark
  (same kernels, different reporting code) measured the short completed
  class at 1.02–1.03 for N = 3 and 1.14–1.15 for N = 4; the other cells
  agreed with the table within 0.07.
- Completed and cut-off searches behave alike. Length matters a little: the
  gain is largest on short searches at larger budgets (up to 1.3–1.4 kernel
  only) and settles at about 1.15–1.2 on searches of 100,000 steps and more.
- This corrects a statement in `REPR_RESULTS.md` and the PR text, that
  `auto` is 4–10% slower than `asm` on searches of a few hundred steps. That
  came from three single targets each repeated many thousands of times,
  which the branch predictor memorizes. On thousands of *different* short
  searches `auto` is faster. The earlier figure is right for one tiny search
  repeated and wrong as a description of short searches in general.

## Both against the C solver

`auto` vs C, through the API (run 1 / run 2):

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 4.48 / 4.43 | 4.17 / 4.10 | 3.26 / 3.28 | 3.07 / 3.15 | 3.11 / 3.10 | 3.00 / 3.07 |
| completed, 2,001 to 100,000 | | 3.78 / 3.71 | 3.54 / 3.55 | 3.72 / 3.64 | 3.73 / 3.74 | 3.88 / 3.84 |
| completed, over 100,000 | | | 4.02 / 3.99 | 3.77 / 3.76 | 3.86 / 3.95 | 3.86 / 3.91 |
| cut off at 2,000 steps | | 3.63 / 3.60 | 3.37 / 3.37 | 3.35 / 3.38 | 3.43 / 3.35 | 3.42 / 3.48 |
| cut off at 100,000 | | | 3.93 / 3.99 | 3.60 / 3.67 | 3.84 / 3.86 | 4.07 / 3.99 |
| cut off at 2,000,000 | | | | | 3.97 / 3.88 | 4.03 / 3.99 |

`asm` vs C, through the API (run 1 / run 2):

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 4.02 / 4.19 | 3.53 / 3.65 | 2.66 / 2.67 | 2.53 / 2.54 | 2.49 / 2.44 | 2.45 / 2.48 |
| completed, 2,001 to 100,000 | | 2.97 / 2.96 | 2.83 / 2.83 | 2.98 / 2.92 | 3.05 / 3.01 | 3.10 / 3.09 |
| completed, over 100,000 | | | 3.29 / 3.32 | 3.14 / 3.07 | 3.20 / 3.26 | 3.21 / 3.19 |
| cut off at 2,000 steps | | 3.09 / 3.10 | 2.71 / 2.71 | 2.68 / 2.65 | 2.66 / 2.63 | 2.63 / 2.64 |
| cut off at 100,000 | | | 3.26 / 3.28 | 2.98 / 3.02 | 3.15 / 3.23 | 3.35 / 3.22 |
| cut off at 2,000,000 | | | | | 3.39 / 3.38 | 3.54 / 3.42 |

- The speedup over C depends on the class more than on the budget. For
  searches of 100,000 steps and more, and for the long cut-offs, `asm` is
  2.98–3.54 times faster than C, in line with the 3.3 measured on the
  fixed workloads of `ASM_RESULTS.md`.
- For short completed searches at budgets 5 to 8 (which are solved in a few
  hundred steps) `asm` is 2.44–2.67 times faster: each search also pays
  the wrapper's validation, export and independent verification, which the
  C solver's API does too but which weighs more against a faster kernel.
- `auto` is 3.00–4.48 times faster than C across all classes and budgets.

## Search accounting

The logical step count is exact and identical for every engine. The
benchmark-only kernels `n8x-count` and `hybx-count` (the `auto` kernels with
counters added; the ordinary kernels contain no counting code) report how
those steps were arrived at: executed one at a time, or charged
arithmetically for link attempts that were certain to fail.

Share of logical steps accounted for in bulk by `auto` (identical in both
runs; it is a property of the searches, not a timing):

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 11.2% | 12.8% | 13.3% | 13.6% | 13.8% | 14.0% |
| completed, 2,001 to 100,000 |  | 11.4% | 11.9% | 11.9% | 12.2% | 12.7% |
| completed, over 100,000 |  |  | 6.6% | 11.2% | 12.6% | 11.8% |
| cut off at 2,000 steps |  | 10.3% | 10.6% | 10.5% | 10.6% | 10.9% |
| cut off at 100,000 |  |  | 6.8% | 11.8% | 10.9% | 12.1% |
| cut off at 2,000,000 |  |  |  |  | 11.8% | 12.3% |

- Between 6.6% and 14.0% of the logical steps are never executed; the
  other 86–93% are executed one at a time, as in every other kernel.
- That share is fairly even across classes and budgets. It is what the
  7–10% gain of bulk accounting (measured in `REPR_RESULTS.md` as `n8x`
  against `n8s-bmi2`) was bought with: about one step in eight, but steps
  that each ended in a branch the processor could not predict.
- Bulk accounting is switched off for a link loop whenever its result could
  be observed: fewer than 256 steps left before the limit, the stack full,
  or an existing node without a color. CTRL link loops that fell back to
  the per-node code, per 1,000 that started in bulk mode (identical in both
  runs):

  | class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
  |---|---|---|---|---|---|---|
  | completed, up to 2,000 steps | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
  | completed, 2,001 to 100,000 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
  | completed, over 100,000 |  |  | 0.0 | 0.0 | 0.0 | 0.0 |
  | cut off at 2,000 steps |  | 141.2 | 144.1 | 144.6 | 137.6 | 140.9 |
  | cut off at 100,000 |  |  | 4.0 | 3.6 | 3.8 | 3.6 |
  | cut off at 2,000,000 |  |  |  |  | 0.2 | 0.1 |

  Completed searches never fall back. A search cut off at 2,000 steps
  spends its last 256 steps inside the margin, which is why about one loop
  in seven falls back there and why that class has the lowest bulk share.
- The counting kernels keep three more counters (bit scans, loops started
  in bulk mode, and the reason for each fallback); see `asm_layout.h`.

## Raw output, run 1

```
CarloBomb workload characterization
revision:   3f73dee-dirty
processor:  Intel(R) Xeon(R) Processor @ 2.10GHz
ISA found:  BMI1/BMI2 yes, AVX-512 F/BW/VL yes
counters:   no hardware counters on this machine (perf_event_open: No such file or directory)
method:     11 rounds, round-robin; at least 1500000 steps per pass; ticks = time-stamp counter

### Node budget 3  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 2851 | 1500306 | 4% | 11.03 | 4.02 | 4.48 | 1.10 | 1.11 | 88.8% | 11.2% |

### Node budget 4  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 2129 | 1500160 | 87% | 12.10 | 3.53 | 4.17 | 1.20 | 1.22 | 87.2% | 12.8% |
| completed, 2,001 to 100,000 | 388 | 1507155 | 5% | 13.28 | 2.97 | 3.78 | 1.26 | 1.28 | 88.6% | 11.4% |
| cut off at 2,000 steps | 750 | 1500000 | - | 13.31 | 3.09 | 3.63 | 1.19 | 1.18 | 89.7% | 10.3% |

### Node budget 5  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 3154 | 1500040 | 100% | 13.82 | 2.66 | 3.26 | 1.19 | 1.27 | 86.7% | 13.3% |
| completed, 2,001 to 100,000 | 50 | 1529455 | 22% | 12.83 | 2.83 | 3.54 | 1.26 | 1.25 | 88.1% | 11.9% |
| completed, over 100,000 | 13 | 1592003 | 0% | 12.89 | 3.29 | 4.02 | 1.22 | 1.20 | 93.4% | 6.6% |
| cut off at 2,000 steps | 750 | 1500000 | - | 13.58 | 2.71 | 3.37 | 1.25 | 1.21 | 89.4% | 10.6% |
| cut off at 100,000 | 15 | 1500000 | - | 12.88 | 3.26 | 3.93 | 1.20 | 1.21 | 93.2% | 6.8% |

### Node budget 6  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 3561 | 1500662 | 100% | 14.52 | 2.53 | 3.07 | 1.23 | 1.28 | 86.4% | 13.6% |
| completed, 2,001 to 100,000 | 84 | 1506894 | 100% | 12.64 | 2.98 | 3.72 | 1.24 | 1.25 | 88.1% | 11.9% |
| completed, over 100,000 | 4 | 1515685 | 0% | 12.39 | 3.14 | 3.77 | 1.23 | 1.19 | 88.8% | 11.2% |
| cut off at 2,000 steps | 750 | 1500000 | - | 13.98 | 2.68 | 3.35 | 1.25 | 1.29 | 89.5% | 10.5% |
| cut off at 100,000 | 15 | 1500000 | - | 12.39 | 2.98 | 3.60 | 1.22 | 1.21 | 88.2% | 11.8% |

### Node budget 7  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 3563 | 1500172 | 100% | 14.40 | 2.49 | 3.11 | 1.25 | 1.34 | 86.2% | 13.8% |
| completed, 2,001 to 100,000 | 73 | 1508001 | 100% | 12.42 | 3.05 | 3.73 | 1.23 | 1.24 | 87.8% | 12.2% |
| completed, over 100,000 | 3 | 2711337 | 100% | 11.81 | 3.20 | 3.86 | 1.22 | 1.21 | 87.4% | 12.6% |
| cut off at 2,000 steps | 750 | 1500000 | - | 14.04 | 2.66 | 3.43 | 1.29 | 1.31 | 89.4% | 10.6% |
| cut off at 100,000 | 15 | 1500000 | - | 12.46 | 3.15 | 3.84 | 1.19 | 1.22 | 89.1% | 10.9% |
| cut off at 2,000,000 | 3 | 6000000 | - | 12.01 | 3.39 | 3.97 | 1.17 | 1.20 | 88.2% | 11.8% |

### Node budget 8  (auto runs n8x)

| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C | auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |
|---|---|---|---|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 3790 | 1500035 | 100% | 14.87 | 2.45 | 3.00 | 1.25 | 1.40 | 86.0% | 14.0% |
| completed, 2,001 to 100,000 | 73 | 1505960 | 100% | 12.28 | 3.10 | 3.88 | 1.25 | 1.26 | 87.3% | 12.7% |
| completed, over 100,000 | 3 | 2101068 | 100% | 12.38 | 3.21 | 3.86 | 1.21 | 1.20 | 88.2% | 11.8% |
| cut off at 2,000 steps | 750 | 1500000 | - | 14.03 | 2.63 | 3.42 | 1.29 | 1.25 | 89.1% | 10.9% |
| cut off at 100,000 | 15 | 1500000 | - | 12.05 | 3.35 | 4.07 | 1.20 | 1.15 | 87.9% | 12.1% |
| cut off at 2,000,000 | 3 | 6000000 | - | 11.65 | 3.54 | 4.03 | 1.15 | 1.20 | 87.7% | 12.3% |

### Summary: auto vs asm, kernel only

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 1.11 | 1.22 | 1.27 | 1.28 | 1.34 | 1.40 |
| completed, 2,001 to 100,000 | | 1.28 | 1.25 | 1.25 | 1.24 | 1.26 |
| completed, over 100,000 | | | 1.20 | 1.19 | 1.21 | 1.20 |
| cut off at 2,000 steps | | 1.18 | 1.21 | 1.29 | 1.31 | 1.25 |
| cut off at 100,000 | | | 1.21 | 1.21 | 1.22 | 1.15 |
| cut off at 2,000,000 | | | | | 1.20 | 1.20 |

### Summary: auto vs asm, through the API

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 1.10 | 1.20 | 1.19 | 1.23 | 1.25 | 1.25 |
| completed, 2,001 to 100,000 | | 1.26 | 1.26 | 1.24 | 1.23 | 1.25 |
| completed, over 100,000 | | | 1.22 | 1.23 | 1.22 | 1.21 |
| cut off at 2,000 steps | | 1.19 | 1.25 | 1.25 | 1.29 | 1.29 |
| cut off at 100,000 | | | 1.20 | 1.22 | 1.19 | 1.20 |
| cut off at 2,000,000 | | | | | 1.17 | 1.15 |

### Summary: auto vs the C solver, through the API

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 4.48 | 4.17 | 3.26 | 3.07 | 3.11 | 3.00 |
| completed, 2,001 to 100,000 | | 3.78 | 3.54 | 3.72 | 3.73 | 3.88 |
| completed, over 100,000 | | | 4.02 | 3.77 | 3.86 | 3.86 |
| cut off at 2,000 steps | | 3.63 | 3.37 | 3.35 | 3.43 | 3.42 |
| cut off at 100,000 | | | 3.93 | 3.60 | 3.84 | 4.07 |
| cut off at 2,000,000 | | | | | 3.97 | 4.03 |

### Summary: asm vs the C solver, through the API

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 4.02 | 3.53 | 2.66 | 2.53 | 2.49 | 2.45 |
| completed, 2,001 to 100,000 | | 2.97 | 2.83 | 2.98 | 3.05 | 3.10 |
| completed, over 100,000 | | | 3.29 | 3.14 | 3.20 | 3.21 |
| cut off at 2,000 steps | | 3.09 | 2.71 | 2.68 | 2.66 | 2.63 |
| cut off at 100,000 | | | 3.26 | 2.98 | 3.15 | 3.35 |
| cut off at 2,000,000 | | | | | 3.39 | 3.54 |

### Summary: share of logical steps accounted for in bulk by auto

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 11.2% | 12.8% | 13.3% | 13.6% | 13.8% | 14.0% |
| completed, 2,001 to 100,000 | | 11.4% | 11.9% | 11.9% | 12.2% | 12.7% |
| completed, over 100,000 | | | 6.6% | 11.2% | 12.6% | 11.8% |
| cut off at 2,000 steps | | 10.3% | 10.6% | 10.5% | 10.6% | 10.9% |
| cut off at 100,000 | | | 6.8% | 11.8% | 10.9% | 12.1% |
| cut off at 2,000,000 | | | | | 11.8% | 12.3% |

### Summary: CTRL link loops that fell back to per-node code, per 1,000 started in bulk mode

| class | N = 3 | N = 4 | N = 5 | N = 6 | N = 7 | N = 8 |
|---|---|---|---|---|---|---|
| completed, up to 2,000 steps | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| completed, 2,001 to 100,000 | | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| completed, over 100,000 | | | 0.0 | 0.0 | 0.0 | 0.0 |
| cut off at 2,000 steps | | 141.2 | 144.1 | 144.6 | 137.6 | 140.9 |
| cut off at 100,000 | | | 4.0 | 3.6 | 3.8 | 3.6 |
| cut off at 2,000,000 | | | | | 0.2 | 0.1 |
```
