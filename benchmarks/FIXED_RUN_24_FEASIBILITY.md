# FixedRunBomb(24): feasibility

**Answer: yes.** Building the complete, certified table for all 24-bit
strings is expected to take **about 2 hours of search on the owner's
machine with 6 worker processes**, and less with 10. Even at three times
that it fits the 17-hour window with room for validation.

Measured on 12 November 2026 in the development container (Intel Xeon,
family 6 model 173, 2.3 GHz, 4 virtual CPUs, GCC 13.3, `amd64-auto`) with
`benchmarks/ladder_pilot.c`, and converted to the owner's machine with the
throughput that machine showed in its own 16-bit run.

## 1. How the table would be built: the ladder

Not each 24-bit string from scratch. The table for L bits is built from the
complete table for L − 1 bits:

- A string T of L bits starts with a string p of L − 1 bits whose minimum
  m(p) is already certified. T cannot need fewer nodes than p.
- The REV stored for p goes on to emit one more bit. That settles one of
  the two extensions of p at once: it has a REV of m(p) nodes and cannot
  have fewer. No search.
- The other extension, the sibling, is searched from node budget m(p)
  upward instead of from one node.

So each level searches half the strings, and each search skips every budget
below m(p). Starting from the verified 16-bit table, the levels 17, 18, …,
24 are built in turn, **and each finished level is a complete certified
Bomb in its own right**. If the night ended at level 23, FixedRunBomb(23)
would be there, whole. Nothing is thrown away between levels: each is the
input of the next.

The pilot measures both this and the direct method (every string from one
node up). The ladder needs about 2.5 times fewer search steps in all.

## 2. Measurements

4,000 random strings per length (one of each complement pair), 4 processes:

| Level L | Sibling search, mean steps | median | p90 | p99 | max | REV found at m(p) | one node more | two more | three more |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20 | 546,000 ± 11,000 | 415,000 | 1.08·10^6 | 3.2·10^6 | 1.8·10^7 | 58% | 40% | 1.6% | 0.15% |
| 22 | 2.24·10^6 ± 36,000 | 1.32·10^6 | 5.7·10^6 | 9.3·10^6 | 2.3·10^7 | 68% | 30% | 1.9% | 0.05% |
| 24 | 5.04·10^6 ± 68,000 | 4.59·10^6 | 1.0·10^7 | 1.9·10^7 | 5.4·10^7 | 51% | 46% | 2.5% | 0.08% |

The cost has no heavy tail: the costliest of 4,000 random jobs at 24 bits
is 11 times the mean, 54 ms.

**Finding the REV and certifying it**, level 24: 44% of the steps go into
the search that finds the REV, 56% into proving the smaller budgets empty
(2.3 ms and 3.1 ms per job here).

**By node budget**, level 24, 4,000 jobs:

| Budget | Searches | Proven unsat | Solved | Steps | Steps per second, per process |
|---:|---:|---:|---:|---:|---:|
| 5 | 4 | 4 | 0 | 1.5·10^5 | 8.2·10^8 |
| 6 | 215 | 211 | 4 | 1.0·10^8 | 8.7·10^8 |
| 7 | 2,913 | 1,846 | 1,067 | 1.39·10^10 | 9.2·10^8 |
| 8 | 2,929 | 0 | 2,929 | 6.2·10^9 | 1.0·10^9 |

**Structured strings** (18 families at each length: single ones at the end
and in the middle, halves, thirds, periodic strings with a late change,
growing gaps and runs, Thue–Morse, Fibonacci, squares). Random samples do
not show the hardest strings; these are the candidates:

| Length | Largest minimum found | Strings that need it | Costliest, from one node up |
|---:|---:|---|---:|
| 20 | 8 | `0…01`, `0…011`, `0…010`, `0…0101`, halves with last bit flipped | 3.5·10^6 steps, 3 ms |
| 22 | 9 | `0…01`, `0…011`, `0…010` | 3.3·10^7 steps, 34 ms |
| 24 | 9 | `0…01`, `0…011`, `0…010`, `0…0101`, halves with last bit flipped | 3.8·10^7 steps, 41 ms |

No random string in the samples needed 9 nodes; the structured ones do, and
9 is ⌈2√L⌉ − 1 for L = 21 … 25. The hardest string found costs 41 ms. At
node budget 9, where `amd64-auto` switches kernels, the searches ran at
about 10^9 steps per second and more, as at 8 and below.

**Throughput here:** 9.5·10^8 steps per second with one process,
3.57·10^9 with four (94% of four times one).

## 3. Projection

Sibling jobs at level L: 2^(L−2), one per complement pair of siblings.

| Level | Jobs | Steps, expected | Sampling range |
|---:|---:|---:|---:|
| 20 | 262,144 | 1.43·10^11 | 1.38 – 1.50·10^11 |
| 22 | 1,048,576 | 2.35·10^12 | 2.28 – 2.45·10^12 |
| 24 | 4,194,304 | 2.11·10^13 | 2.06 – 2.18·10^13 |
| ladder 17 … 24 | | 3.17·10^13 | level 24 is 67% of it |

The cost grows by a factor of 3.0 per level between 22 and 24 bits. The
direct method would need about 7.9·10^13 steps for 24 bits.

The range allows for sampling error (two standard errors) and for a tail the
sample missed (up to 3 in 4,000 jobs as costly as the hardest structured
string). Both are small. The real uncertainty is the machine.

**On the owner's machine.** Its 16-bit construction ran the same search at
8.4·10^8 steps per second per process with 6 processes (5.1·10^9 in all), and
at 1.22·10^9 with one.

| Workers | Assumed throughput | Level 24 | Whole ladder 17 … 24 |
|---|---:|---:|---:|
| 6 (measured rate) | 5.1·10^9 steps/s | 1.2 h | **1.7 h** |
| 10 (4 efficiency cores at about half a performance core; an assumption) | 6.5·10^9 | 0.9 h | 1.4 h |
| slow case: half the measured 6-worker rate | 2.5·10^9 | 2.3 h | 3.5 h |

Add a few minutes per level for writing and validating the table, and for
replaying the journal on restart.

**Estimate: 1.5 to 2.5 hours for the complete FixedRunBomb(24), with 3.5
hours as a pessimistic case.** What could make it wrong: throughput under
WSL2 with all cores busy (the pilot measures it in two minutes, see below);
and a level whose cost departs from the trend, which the run itself reports
level by level, long before level 24 starts.

## 4. Resources

| | Estimate |
|---|---:|
| Table, 24 bits (34 bytes per entry) | 544 MiB |
| Tables of levels 17 … 23, kept | 540 MiB together |
| Journal, level 24: 80 bytes per search, about 1.5 searches per job | 0.5 GB |
| Journals of all levels | 0.8 GB |
| Disk in all | about 2.5 GB |
| Memory, coordinating process at level 24 | about 2 GB |
| Memory, each worker | a few MB |
| Journal overhead | below 1% of a job at 5 ms per job (it was 5% at 45 µs per job in the 16-bit build) |

Rings are not stored anywhere: an entry that has no better REV yet gets its
ring generated when a table is written.

## 5. Recommendation

1. Build the ladder from the verified 16-bit table up to 24 bits, 10
   workers. Expect 1.5 to 2.5 hours.
2. Each level is finalized and validated before the next starts. The largest
   complete Bomb is always on disk; an interruption costs at most the
   searches in progress.
3. Afterwards: full validation of the 24-bit table (every REV run), the
   prefix statistics for every length up to 24, and recomputation of a
   random sample by the direct method with the C engine.

At the measured growth of 3 per level, level 25 would take about three and
a half hours more on the same machine and would also fit the night. The
format specified for this experiment stops at 24 bits and 24 nodes, so that
is a decision, not a plan.

## 6. Checking the estimate on the machine that will do the work

Two minutes, no files written:

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 6
```

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 10
```

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 16
```

The last lines of each run project the ladder at that machine's measured
throughput for that number of processes; the fastest of the three is the
worker count to use.
