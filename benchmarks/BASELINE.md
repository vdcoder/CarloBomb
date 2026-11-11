# Baseline measurements — Milestone 0

Recorded 2026-11-11 from source revision `6e6fda5`, using `./build/baseline`
(method in `README.md` in this directory). Kernel: Linux 6.18, x86-64, a
shared cloud VM with 4 virtual CPUs; single-threaded runs.

These are baseline observations on one machine. They are not proof that
either implementation is faster. The VM is shared, so expect a few percent of
run-to-run variation; differences of that size below are not meaningful.

Times are microseconds per complete run (budgets 2..10, stop at first
success). `nodes`, `steps` and `peak stack` are for the whole run; `steps`
and `peak stack` (frames) come from LevelSolver's counters.

## GCC 13.3.0, `-O3 -DNDEBUG`

```
CarloBomb baseline benchmark
revision:  6e6fda5
compiler:  13.3.0
flags:     -O3 -DNDEBUG
platform:  Linux x86_64
processor: Intel(R) Xeon(R) Processor @ 2.10GHz
protocol:  budgets 2..10, stop at first success; 9 samples of >= 50 ms each
legacy:    solvers/level/fastest/c/legacy_original.c (unmodified, printing disabled)
level:     solvers/level/fastest/c/level_solver.c

| target | len | status | nodes | steps | peak stack | legacy min us | legacy med us | level min us | level med us | level/legacy (min) |
|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 9 | SOLVED | 3 | 709 | 25 | 2.952 | 3.047 | 2.920 | 3.142 | 0.99 |
| 00000000 | 8 | SOLVED | 1 | 19 | 17 | 0.137 | 0.145 | 0.197 | 0.203 | 1.44 |
| 01010101 | 8 | SOLVED | 2 | 72 | 20 | 0.329 | 0.366 | 0.368 | 0.418 | 1.12 |
| 0110100110010110 | 16 | SOLVED | 6 | 119584 | 46 | 671.934 | 708.472 | 652.527 | 683.042 | 0.97 |
| 001011011101111011111 | 21 | SOLVED | 7 | 502057 | 57 | 2581.766 | 2721.228 | 2634.441 | 2737.964 | 1.02 |
| 110100100010000100000 | 21 | SOLVED | 7 | 507515 | 57 | 2579.628 | 2679.447 | 2713.341 | 2773.696 | 1.05 |
| 0100011011000001010011 | 22 | SOLVED | 7 | 596603 | 60 | 3164.024 | 3415.058 | 3197.329 | 3760.910 | 1.01 |
| 01101001100101101001011001101001 | 32 | SOLVED | 8 | 14538819 | 83 | 74249.592 | 79516.827 | 72674.191 | 77733.869 | 0.98 |
| 0001101100101110100111 | 22 | SOLVED | 7 | 599361 | 58 | 3250.339 | 3267.624 | 3362.689 | 3501.616 | 1.03 |
| 000001000110010100111010 | 24 | SOLVED | 8 | 4294651 | 69 | 21890.974 | 23462.550 | 21792.634 | 22167.383 | 1.00 |
```

## Clang 18.1.3, `-O3 -DNDEBUG`

```
CarloBomb baseline benchmark
revision:  6e6fda5
compiler:  Ubuntu Clang 18.1.3 (1ubuntu1)
flags:     -O3 -DNDEBUG
platform:  Linux x86_64
processor: Intel(R) Xeon(R) Processor @ 2.10GHz
protocol:  budgets 2..10, stop at first success; 9 samples of >= 50 ms each
legacy:    solvers/level/fastest/c/legacy_original.c (unmodified, printing disabled)
level:     solvers/level/fastest/c/level_solver.c

| target | len | status | nodes | steps | peak stack | legacy min us | legacy med us | level min us | level med us | level/legacy (min) |
|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 9 | SOLVED | 3 | 709 | 25 | 2.204 | 2.280 | 2.490 | 2.553 | 1.13 |
| 00000000 | 8 | SOLVED | 1 | 19 | 17 | 0.089 | 0.092 | 0.159 | 0.160 | 1.78 |
| 01010101 | 8 | SOLVED | 2 | 72 | 20 | 0.238 | 0.243 | 0.315 | 0.319 | 1.33 |
| 0110100110010110 | 16 | SOLVED | 6 | 119584 | 46 | 526.827 | 543.191 | 546.291 | 578.489 | 1.04 |
| 001011011101111011111 | 21 | SOLVED | 7 | 502057 | 57 | 2135.883 | 2243.689 | 2308.219 | 2384.525 | 1.08 |
| 110100100010000100000 | 21 | SOLVED | 7 | 507515 | 57 | 2203.504 | 2309.739 | 2338.190 | 2425.882 | 1.06 |
| 0100011011000001010011 | 22 | SOLVED | 7 | 596603 | 60 | 2478.825 | 2718.982 | 2666.607 | 2809.453 | 1.08 |
| 01101001100101101001011001101001 | 32 | SOLVED | 8 | 14538819 | 83 | 57329.713 | 62225.338 | 59617.208 | 62104.833 | 1.04 |
| 0001101100101110100111 | 22 | SOLVED | 7 | 599361 | 58 | 2237.736 | 2389.142 | 2441.553 | 2644.767 | 1.09 |
| 000001000110010100111010 | 24 | SOLVED | 8 | 4294651 | 69 | 15552.770 | 16216.595 | 17101.704 | 19520.062 | 1.10 |
```

## Reading the numbers

- **Results agree.** On every workload both solvers report the same outcome
  and node count. (The benchmark exits non-zero and prints `DISAGREE` if they
  do not.)
- **Search-dominated workloads (16 bits and up):** with GCC the adapted solver
  is within about ±5% of the legacy kernel, which is inside the noise of this
  machine. With Clang it is consistently slower, by about 4–10%. The adapted
  loop does extra work per iteration that the original does not: a step
  counter and limit test, a stack-capacity test and a peak-depth update on
  each push, and a `size_t` rather than `int` position counter. This is the
  cost of the safety and reporting requirements; no attempt was made to
  recover it.
- **Tiny workloads (8–9 bits):** the adapted solver is up to about 1.8x slower
  in relative terms, 0.03–0.3 µs in absolute terms. That is fixed per-call
  cost (input validation, copying and completing the graph, independent
  verification), paid once per budget tried.
- **Throughput:** roughly 5 ns per search step with GCC and 4 ns with Clang
  on this processor.
- **Stack:** peak use was 83 frames for the 32-bit target, against a capacity
  of 10000.

## Caveats

- The legacy kernel is timed inside the benchmark executable with its two
  target variables assigned at run time. In the standalone `legacy_original`
  executable the compiler can treat them as constants, so that binary may
  perform slightly differently. Its whole run for the built-in target takes a
  few microseconds, too short to time meaningfully as a process.
- The legacy protocol starts at budget 2, so the `nodes` column is not a
  certified minimum for targets one node can produce (none of the workloads
  above except `00000000`, where it happens to be 1).
- The revision string is captured when CMake configures. Re-run `cmake`
  after committing, before recording new numbers.
