# AMD64 assembly vs C LevelSolver — results

Recorded 2026-11-11 with `./build/asm_comparison` (method in `README.md` in
this directory and in the header of `asm_comparison.c`).

- **Kernel measured:** source revision `d65b75e`. Later commits change only
  documentation and the size attribute of the kernel's symbol; the kernel's
  machine code is the same (2,043 bytes of `.text`, SHA-256 beginning
  `813d603827156ce1` as assembled by GNU as).
- **Machine:** shared cloud VM, 4 virtual CPUs, Intel Xeon @ 2.10 GHz, Linux
  6.18, x86-64. ISA present: SSE4.2, POPCNT, AVX, AVX2, BMI1, BMI2, AVX-512F.
  The assembly kernel uses none of these.
- **Builds:** `-O3 -DNDEBUG` (CMake Release) with GCC 13.3.0 and with Clang
  18.1.3. The assembly kernel is the same instructions in both builds; only
  the C baseline and the C wrapper differ.
- **Protocol:** each engine runs node budgets 2..10 through its public API
  and stops at the first success. 15 samples per engine, interleaved, each
  at least 40 ms, after a warm-up.
- **Speedup** = T_C / T_ASM. Above 1 means the assembly is faster.
- **Ticks** are time-stamp-counter ticks per search step (minimum sample),
  not core cycles. Hardware performance counters are not exposed in this VM,
  so no cycle, branch or cache counts could be recorded.
- **ASM kernel** is the assembly routine without the C wrapper.
- Times are microseconds per run.

These are observations on one machine, and that machine is noisy; see
"Repeatability" below before reading anything into a few percent.

## Agreement

On every workload and every budget, in every run recorded here, the two
engines returned the same status, the same graph, the same step count and the
same peak stack depth (column `agree`; the benchmark exits non-zero
otherwise).

## GCC 13.3.0 C baseline

```
CarloBomb C vs AMD64 assembly LevelSolver
revision:   d65b75e
C compiler: 13.3.0
C flags:    -O3 -DNDEBUG
platform:   Linux x86_64
processor:  Intel(R) Xeon(R) Processor @ 2.10GHz
ISA (of those checked): sse2 sse4_2 popcnt avx avx2 bmi1 bmi2 avx512f 
protocol:   budgets 2..10, stop at first success; 15 interleaved samples of >= 40 ms
times in microseconds per run; ticks = TSC ticks per search step (minimum)

| target | nodes | steps | peak | agree | C min | C med | ASM min | ASM med | speedup min | speedup med | ASM kernel min | C ticks | ASM ticks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 3 | 709 | 25 | yes | 2.881 | 3.005 | 0.719 | 0.779 | 4.01 | 3.86 | 0.499 | 8.5 | 2.1 |
| 00000000 | 1 | 19 | 17 | yes | 0.200 | 0.213 | 0.161 | 0.172 | 1.24 | 1.24 | 0.035 | 22.1 | 17.8 |
| 01010101 | 2 | 72 | 20 | yes | 0.362 | 0.402 | 0.197 | 0.203 | 1.83 | 1.98 | 0.069 | 10.6 | 5.8 |
| 0110100110010110 | 6 | 119584 | 46 | yes | 653.037 | 700.312 | 171.289 | 185.498 | 3.81 | 3.78 | 167.426 | 11.5 | 3.0 |
| 001011011101111011111 | 7 | 502057 | 57 | yes | 2744.558 | 2900.419 | 891.497 | 934.828 | 3.08 | 3.10 | 884.892 | 11.5 | 3.7 |
| 110100100010000100000 | 7 | 507515 | 57 | yes | 2811.153 | 2946.228 | 898.349 | 946.467 | 3.13 | 3.11 | 915.903 | 11.6 | 3.7 |
| 0100011011000001010011 | 7 | 596603 | 60 | yes | 3187.853 | 3508.887 | 1061.384 | 1129.624 | 3.00 | 3.11 | 1061.707 | 11.2 | 3.7 |
| 01101001100101101001011001101001 | 8 | 14538819 | 83 | yes | 79287.941 | 83452.106 | 22253.230 | 24530.704 | 3.56 | 3.40 | 22707.104 | 11.5 | 3.2 |
| 0001101100101110100111 | 7 | 599361 | 58 | yes | 3224.735 | 3425.662 | 980.609 | 1059.082 | 3.29 | 3.23 | 988.061 | 11.3 | 3.4 |
| 000001000110010100111010 | 8 | 4294651 | 69 | yes | 20190.659 | 22830.090 | 5822.574 | 6480.130 | 3.47 | 3.52 | 6206.000 | 9.9 | 2.8 |

geometric mean speedup (median times, 7 workloads with >= 100000 steps): 3.31
```

## Clang 18.1.3 C baseline

```
CarloBomb C vs AMD64 assembly LevelSolver
revision:   d65b75e
C compiler: Ubuntu Clang 18.1.3 (1ubuntu1)
C flags:    -O3 -DNDEBUG
platform:   Linux x86_64
processor:  Intel(R) Xeon(R) Processor @ 2.10GHz
ISA (of those checked): sse2 sse4_2 popcnt avx avx2 bmi1 bmi2 avx512f 
protocol:   budgets 2..10, stop at first success; 15 interleaved samples of >= 40 ms
times in microseconds per run; ticks = TSC ticks per search step (minimum)

| target | nodes | steps | peak | agree | C min | C med | ASM min | ASM med | speedup min | speedup med | ASM kernel min | C ticks | ASM ticks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 3 | 709 | 25 | yes | 2.183 | 2.402 | 0.545 | 0.601 | 4.00 | 4.00 | 0.463 | 6.5 | 1.6 |
| 00000000 | 1 | 19 | 17 | yes | 0.143 | 0.149 | 0.087 | 0.095 | 1.64 | 1.57 | 0.032 | 15.8 | 9.6 |
| 01010101 | 2 | 72 | 20 | yes | 0.308 | 0.323 | 0.119 | 0.125 | 2.59 | 2.58 | 0.067 | 9.0 | 3.5 |
| 0110100110010110 | 6 | 119584 | 46 | yes | 516.433 | 554.719 | 171.018 | 187.174 | 3.02 | 2.96 | 168.266 | 9.1 | 3.0 |
| 001011011101111011111 | 7 | 502057 | 57 | yes | 2269.897 | 2483.252 | 858.823 | 936.820 | 2.64 | 2.65 | 895.966 | 9.5 | 3.6 |
| 110100100010000100000 | 7 | 507515 | 57 | yes | 2251.129 | 2319.635 | 817.523 | 905.335 | 2.75 | 2.56 | 840.674 | 9.3 | 3.4 |
| 0100011011000001010011 | 7 | 596603 | 60 | yes | 2749.438 | 2848.133 | 1070.506 | 1136.459 | 2.57 | 2.51 | 1086.564 | 9.7 | 3.8 |
| 01101001100101101001011001101001 | 8 | 14538819 | 83 | yes | 64700.815 | 67383.709 | 23557.770 | 24895.450 | 2.75 | 2.71 | 23003.299 | 9.3 | 3.4 |
| 0001101100101110100111 | 7 | 599361 | 58 | yes | 2567.668 | 2796.361 | 998.526 | 1086.351 | 2.57 | 2.57 | 998.627 | 9.0 | 3.5 |
| 000001000110010100111010 | 8 | 4294651 | 69 | yes | 18267.417 | 19059.422 | 6500.942 | 6652.814 | 2.81 | 2.86 | 6437.178 | 8.9 | 3.2 |

geometric mean speedup (median times, 7 workloads with >= 100000 steps): 2.69
```

## Summary

Speedups from the two runs above (minimum / median times).

| Workload | Steps | vs GCC C (min / median) | vs Clang C (min / median) |
|---|---|---|---|
| `010010001` | 709 | 4.01 / 3.86 | 4.00 / 4.00 |
| `00000000` | 19 | 1.24 / 1.24 | 1.64 / 1.57 |
| `01010101` | 72 | 1.83 / 1.98 | 2.59 / 2.58 |
| `0110100110010110` | 119,584 | 3.81 / 3.78 | 3.02 / 2.96 |
| `001011011101111011111` | 502,057 | 3.08 / 3.10 | 2.64 / 2.65 |
| `110100100010000100000` | 507,515 | 3.13 / 3.11 | 2.75 / 2.56 |
| `0100011011000001010011` | 596,603 | 3.00 / 3.11 | 2.57 / 2.51 |
| 32-bit Thue–Morse prefix | 14,538,819 | 3.56 / 3.40 | 2.75 / 2.71 |
| `0001101100101110100111` | 599,361 | 3.29 / 3.23 | 2.57 / 2.57 |
| `000001000110010100111010` | 4,294,651 | 3.47 / 3.52 | 2.81 / 2.86 |
| **Geometric mean, 7 search-heavy rows (median)** | | **3.31** | **2.69** |

- No workload is slower in assembly in any run. The smallest gain is on
  `00000000` (19 steps), where wrapper overhead dominates both engines:
  1.24x against GCC, 1.57x against Clang.
- On the search-heavy rows the kernel runs at 2.8–3.8 ticks per step
  against 9.9–12.4 for GCC's C and 8.9–9.8 for Clang's.
- Peak stack use is the same as the C solver's by construction (83 frames at
  most in these workloads).

## Repeatability

Geometric mean speedup over the seven search-heavy workloads (median times):

| Kernel revision | C built with | Run 1 | Run 2 |
|---|---|---|---|
| `d65b75e` (final) | GCC 13.3 | 3.31 | 3.29 |
| `d65b75e` (final) | Clang 18.1 | 2.69 | 2.67 |
| `b64e08f` (previous) | GCC 13.3 | 3.38 | 3.40 |
| `b64e08f` (previous) | Clang 18.1 | 2.84 | 2.82 |

Per-workload median speedups on the search-heavy rows, over both runs of the
final revision: 3.03 – 3.93 against GCC, 2.51 – 2.96 against Clang.

The two revisions differ only by the removal of code that no path reached
(see the assembly README), yet the later measurements are lower by about 3%
(GCC) and 5% (Clang). To find out whether that is the kernel or the machine,
the previous revision was rebuilt and both binaries were run back to back,
twice, on four of the heavy workloads:

- The *same binary* differed between rounds a few minutes apart by up to 14%
  in absolute time (previous revision, GCC build, `001011011101111011111`:
  assembly 780.5 then 889.9 µs; C 2580 then 2830 µs).
- Per-workload speedups (ratio of minimum times) of the two revisions
  overlapped: against GCC, 3.15–4.18 for the previous revision and 2.96–3.97
  for the final one; against Clang, 2.63–3.42 and 2.58–3.17.

So the drop is within what this shared VM does on its own. There is no
evidence that the cleanup changed performance, though an effect of a few
percent on individual workloads from the changed code placement cannot be
ruled out at this noise level. Two consecutive runs of one binary agree to
about 1% in the geometric mean; runs separated by more time or by a rebuild
can differ by 5% or more. **Read the result as roughly 3.3x against GCC-built
C and 2.7x against Clang-built C, and treat differences of a few percent
between revisions as noise** unless they survive back-to-back repetition.

## Progression

Speedup against GCC-built C, geometric mean over the seven search-heavy
workloads, median times. Each stage was measured at its own commit. Only
stage 1 and the last two stages were also measured against Clang.

| Stage | Commit | Speedup vs GCC C | Speedup vs Clang C | ASM ticks/step (heavy rows) |
|---|---|---|---|---|
| 1. Direct translation | `9c61fcf` | 1.52, 1.54 (two runs) | 1.25 | 7.3–8.2 |
| 2. Frame state as code position | `de93d84` | 2.66, 2.68, 2.69 | not measured | 4.0–4.8 |
| 2b. `call`/`ret` variant (rejected) | `de93d84` | 2.72, 2.72, 2.74 | not measured | 3.9–4.6 |
| 3. Inline FLOW INIT, peek CTRL INIT | `b821530` | 3.25 | not measured | 3.2–3.7 |
| 4. Batched step counting | `b64e08f` | 3.38, 3.40 | 2.84, 2.82 | 3.1–3.9 |
| 5. Unreachable FLOW color path removed (final) | `d65b75e` | 3.31, 3.29 | 2.69, 2.67 | 2.8–3.8 |

Stage 5 is a cleanup, not an optimization; its lower figures are discussed
under "Repeatability". Explanations of each stage are in
`solvers/level/experimental/amd64/asm/README.md`.

## Caveats

- One processor model, in a shared VM, with drift of several percent between
  measurement sessions. Branch prediction behavior, which dominates this
  workload, differs between processor families.
- The C baseline pays for per-step dispatch that a restructured C solver
  could also avoid. These numbers compare the assembly kernel with the
  existing C solver, not with the best C that could be written.
- Search-heavy targets top out at 8 nodes and 32 bits here.
