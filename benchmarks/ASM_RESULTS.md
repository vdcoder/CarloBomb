# AMD64 assembly vs C LevelSolver — results

Recorded 2026-11-11 from source revision `b64e08f` with
`./build/asm_comparison` (method in `README.md` in this directory and in the
header of `asm_comparison.c`).

- Machine: shared cloud VM, 4 virtual CPUs, Intel Xeon @ 2.10 GHz, Linux
  6.18, x86-64. ISA present: SSE4.2, POPCNT, AVX, AVX2, BMI1, BMI2, AVX-512F.
  The assembly kernel uses none of these.
- Both builds: `-O3 -DNDEBUG` (CMake Release). The assembly object is
  byte-identical in the two builds; only the C baseline and the C wrapper
  differ.
- Each engine runs node budgets 2..10 through its public API and stops at
  the first success. 15 samples per engine, interleaved, each at least 40 ms.
- Speedup = T_C / T_ASM. Above 1 means the assembly is faster.
- "ticks" are time-stamp-counter ticks per search step (minimum sample), not
  core cycles. Hardware performance counters are not exposed in this VM, so
  no cycle, branch or cache counts could be recorded.
- "ASM kernel" is the assembly routine without the C wrapper.
- Times are microseconds per run.

These are observations on one machine. The VM is shared; repeated runs agree
to within about 2% on the search-heavy rows.

## Agreement

On every workload and every budget the two engines returned the same status,
the same graph, the same step count and the same peak stack depth (column
`agree`; the benchmark exits non-zero otherwise).

## GCC 13.3.0 C baseline

```
CarloBomb C vs AMD64 assembly LevelSolver
revision:   b64e08f
C compiler: 13.3.0
C flags:    -O3 -DNDEBUG
platform:   Linux x86_64
processor:  Intel(R) Xeon(R) Processor @ 2.10GHz
ISA (of those checked): sse2 sse4_2 popcnt avx avx2 bmi1 bmi2 avx512f 
protocol:   budgets 2..10, stop at first success; 15 interleaved samples of >= 40 ms
times in microseconds per run; ticks = TSC ticks per search step (minimum)

| target | nodes | steps | peak | agree | C min | C med | ASM min | ASM med | speedup min | speedup med | ASM kernel min | C ticks | ASM ticks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 3 | 709 | 25 | yes | 3.024 | 3.144 | 0.720 | 0.757 | 4.20 | 4.15 | 0.494 | 9.0 | 2.1 |
| 00000000 | 1 | 19 | 17 | yes | 0.219 | 0.222 | 0.174 | 0.180 | 1.26 | 1.23 | 0.031 | 24.2 | 19.2 |
| 01010101 | 2 | 72 | 20 | yes | 0.409 | 0.416 | 0.200 | 0.205 | 2.05 | 2.03 | 0.062 | 11.9 | 5.8 |
| 0110100110010110 | 6 | 119584 | 46 | yes | 695.514 | 706.886 | 178.199 | 183.867 | 3.90 | 3.84 | 175.787 | 12.2 | 3.1 |
| 001011011101111011111 | 7 | 502057 | 57 | yes | 2842.741 | 2916.448 | 892.988 | 906.722 | 3.18 | 3.22 | 882.983 | 11.9 | 3.7 |
| 110100100010000100000 | 7 | 507515 | 57 | yes | 2912.457 | 3112.493 | 895.636 | 961.072 | 3.25 | 3.24 | 876.821 | 12.1 | 3.7 |
| 0100011011000001010011 | 7 | 596603 | 60 | yes | 3414.310 | 3538.465 | 1099.372 | 1118.429 | 3.11 | 3.16 | 1109.259 | 12.0 | 3.9 |
| 01101001100101101001011001101001 | 8 | 14538819 | 83 | yes | 82957.452 | 85120.807 | 24505.868 | 25260.199 | 3.39 | 3.37 | 24031.511 | 12.0 | 3.5 |
| 0001101100101110100111 | 7 | 599361 | 58 | yes | 3349.644 | 3435.917 | 1003.930 | 1043.929 | 3.34 | 3.29 | 1007.870 | 11.7 | 3.5 |
| 000001000110010100111010 | 8 | 4294651 | 69 | yes | 23165.597 | 24135.250 | 6447.776 | 6742.313 | 3.59 | 3.58 | 6350.625 | 11.3 | 3.2 |

geometric mean speedup (median times, 7 workloads with >= 100000 steps): 3.38
```

Second run: geometric mean 3.40; per-row speedups within 0.14 of the above.

## Clang 18.1.3 C baseline

```
CarloBomb C vs AMD64 assembly LevelSolver
revision:   b64e08f
C compiler: Ubuntu Clang 18.1.3 (1ubuntu1)
C flags:    -O3 -DNDEBUG
platform:   Linux x86_64
processor:  Intel(R) Xeon(R) Processor @ 2.10GHz
ISA (of those checked): sse2 sse4_2 popcnt avx avx2 bmi1 bmi2 avx512f 
protocol:   budgets 2..10, stop at first success; 15 interleaved samples of >= 40 ms
times in microseconds per run; ticks = TSC ticks per search step (minimum)

| target | nodes | steps | peak | agree | C min | C med | ASM min | ASM med | speedup min | speedup med | ASM kernel min | C ticks | ASM ticks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 010010001 | 3 | 709 | 25 | yes | 2.460 | 2.544 | 0.593 | 0.620 | 4.15 | 4.10 | 0.512 | 7.3 | 1.8 |
| 00000000 | 1 | 19 | 17 | yes | 0.155 | 0.160 | 0.099 | 0.101 | 1.57 | 1.58 | 0.031 | 17.2 | 10.9 |
| 01010101 | 2 | 72 | 20 | yes | 0.320 | 0.326 | 0.127 | 0.132 | 2.51 | 2.47 | 0.062 | 9.3 | 3.7 |
| 0110100110010110 | 6 | 119584 | 46 | yes | 590.238 | 656.308 | 180.411 | 201.559 | 3.27 | 3.26 | 180.081 | 10.4 | 3.2 |
| 001011011101111011111 | 7 | 502057 | 57 | yes | 2450.299 | 2492.755 | 903.406 | 925.317 | 2.71 | 2.69 | 901.445 | 10.2 | 3.8 |
| 110100100010000100000 | 7 | 507515 | 57 | yes | 2395.851 | 2478.234 | 891.939 | 924.274 | 2.69 | 2.68 | 899.172 | 9.9 | 3.7 |
| 0100011011000001010011 | 7 | 596603 | 60 | yes | 2854.708 | 2985.790 | 1069.484 | 1127.396 | 2.67 | 2.65 | 1093.438 | 10.0 | 3.8 |
| 01101001100101101001011001101001 | 8 | 14538819 | 83 | yes | 69194.098 | 71080.375 | 24512.150 | 24954.007 | 2.82 | 2.85 | 24732.539 | 10.0 | 3.5 |
| 0001101100101110100111 | 7 | 599361 | 58 | yes | 2842.919 | 2969.166 | 1012.907 | 1063.339 | 2.81 | 2.79 | 1001.112 | 10.0 | 3.5 |
| 000001000110010100111010 | 8 | 4294651 | 69 | yes | 19291.746 | 20533.170 | 6653.515 | 6904.657 | 2.90 | 2.97 | 6667.661 | 9.4 | 3.3 |

geometric mean speedup (median times, 7 workloads with >= 100000 steps): 2.84
```

Second run: geometric mean 2.82; per-row speedups within 0.09 of the above.

## Summary

| Workload | Steps | Speedup vs GCC C (min / median) | Speedup vs Clang C (min / median) |
|---|---|---|---|
| `010010001` | 709 | 4.20 / 4.15 | 4.15 / 4.10 |
| `00000000` | 19 | 1.26 / 1.23 | 1.57 / 1.58 |
| `01010101` | 72 | 2.05 / 2.03 | 2.51 / 2.47 |
| `0110100110010110` | 119,584 | 3.90 / 3.84 | 3.27 / 3.26 |
| `001011011101111011111` | 502,057 | 3.18 / 3.22 | 2.71 / 2.69 |
| `110100100010000100000` | 507,515 | 3.25 / 3.24 | 2.69 / 2.68 |
| `0100011011000001010011` | 596,603 | 3.11 / 3.16 | 2.67 / 2.65 |
| 32-bit Thue–Morse prefix | 14,538,819 | 3.39 / 3.37 | 2.82 / 2.85 |
| `0001101100101110100111` | 599,361 | 3.34 / 3.29 | 2.81 / 2.79 |
| `000001000110010100111010` | 4,294,651 | 3.59 / 3.58 | 2.90 / 2.97 |
| **Geometric mean, 7 search-heavy rows (median)** | | **3.38** | **2.84** |

- No workload is slower in assembly. The smallest gain is on `00000000`
  (19 steps), where wrapper overhead dominates both engines.
- On the search-heavy rows the kernel runs at 3.1–3.9 ticks per step against
  11.3–12.2 for GCC's C and 9.4–10.5 for Clang's.
- Peak stack use is the same as the C solver's by construction (83 frames at
  most in these workloads).

## Progression

Speedup against GCC-built C, geometric mean over the seven search-heavy
workloads, median times. Stages 1 and 2 were measured at their own commits;
only stage 1 and the final kernel were also measured against Clang.

| Stage | Commit | Speedup vs GCC C | Speedup vs Clang C | ASM ticks/step (heavy rows) |
|---|---|---|---|---|
| 1. Direct translation | `9c61fcf` | 1.52, 1.54 (two runs) | 1.25 | 7.3–8.2 |
| 2. Frame state as code position | `de93d84` | 2.66, 2.68, 2.69 | not measured | 4.0–4.8 |
| 2b. `call`/`ret` variant (rejected) | `de93d84` | 2.72, 2.72, 2.74 | not measured | 3.9–4.6 |
| 3. Inline FLOW INIT, peek CTRL INIT | `b821530` | 3.25 | not measured | 3.2–3.7 |
| 4. Batched step counting (final) | `b64e08f` | 3.38, 3.40 | 2.84, 2.82 | 3.1–3.9 |

Explanations of each stage are in
`solvers/level/experimental/amd64/asm/README.md`.

## Caveats

- One processor model, in a VM. Branch prediction behavior, which dominates
  this workload, differs between processor families.
- The C baseline pays for per-step dispatch that a restructured C solver
  could also avoid. These numbers compare the assembly kernel with the
  existing C solver, not with the best C that could be written.
- Search-heavy targets top out at 8 nodes and 32 bits here.
