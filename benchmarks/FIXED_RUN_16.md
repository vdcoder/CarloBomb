# FixedRunBomb(16): construction record

The table of minimum-node REVs for all 65,536 binary strings of 16 bits,
built with `carlobomb bomb` (`bombs/fixed-run/README.md`).

Everything here was measured in the development container: Intel Xeon
(family 6, model 173) at 2.3 GHz, 4 virtual CPUs, Linux x86-64, GCC 13.3,
Release build. It is a shared cloud machine, and wall-clock times varied by
some tenths of a second between runs. The construction is small enough that
it was run in full here, several times and several ways; the project owner's
run on other hardware should reproduce the table bit for bit (section 7).

## 1. Result

| Nodes | Strings with that minimum | Cumulative | Share |
|---:|---:|---:|---:|
| 1 | 2 | 2 | 0.00% |
| 2 | 6 | 8 | 0.01% |
| 3 | 64 | 72 | 0.11% |
| 4 | 962 | 1,034 | 1.58% |
| 5 | 14,348 | 15,382 | 23.47% |
| 6 | 48,956 | 64,338 | 98.17% |
| 7 | 1,198 | 65,536 | 100.00% |
| 8–16 | 0 | 65,536 | 100.00% |

Every one of the 65,536 strings has a certified minimum. The largest is 7
nodes, so **7 is the exact maximum, over all 16-bit strings, of the minimum
node count**: an exact, exhaustively established figure, not an estimate. No
string is unresolved, no witness is uncertified, and nothing comes near the
16-node capacity of an entry.

The table file is 1,245,248 bytes.

## 2. Stage A: exhaustive enumeration through five nodes

Every labeled REV with 1 to 5 nodes, run on the interpreter for 16 bits.
"Graphs" counts all labeled graphs of that size; with complement symmetry on
(the default) the half with a white root is run and stands for the rest.

| Nodes | Graphs | Run with symmetry | Strings first emitted | Cumulative | Still open |
|---:|---:|---:|---:|---:|---:|
| 1 | 2 | 1 | 2 | 2 | 65,534 |
| 2 | 64 | 32 | 6 | 8 | 65,528 |
| 3 | 5,832 | 2,916 | 64 | 72 | 65,464 |
| 4 | 1,048,576 | 524,288 | 962 | 1,034 | 64,502 |
| 5 | 312,500,000 | 156,250,000 | 14,348 | 15,382 | 50,154 |

**After N = 5 exactly 50,154 of the 65,536 strings remain open.** Each of
them is known, from the enumeration alone, to need at least 6 nodes. The
other 15,382 have a certified minimum from the enumeration alone.

The same enumeration without the symmetry shortcut (all 313,554,474 graphs)
gives the same sets, and the same counts came out of a separate scratch
program before any of this code existed.

Cost: 91 ns per graph. The five-node round is 14.2 s of processor time; with
4 worker processes it took 4.1 s of wall-clock time.

## 3. Pilot

Before the full run, a sample: Stage A as above, then 2,000 of the 25,077
open complement pairs chosen at random.

```bash
build/carlobomb bomb build --bits 16 --work-dir pilot --sample 2000 --seed 1
```

| | Pilot (2,000 pairs) | Extrapolated to 25,077 pairs | Full run |
|---|---:|---:|---:|
| Solver time | 0.10 s | 1.3 s | 1.13 s |
| Logical search steps | 97,491,136 | 1.22·10^9 | 1,117,938,995 |
| Per pair, mean | 51 µs, 48,746 steps | | 45 µs, 44,580 steps |
| Per pair, median / p90 / p99 / max steps | 17,763 / 130,067 / 441,543 / 693,937 | | 16,686 / 118,815 / 387,706 / 1,537,313 |
| Pairs solved at 6 nodes | 1,945 | | 24,478 |
| Pairs needing 7 nodes | 55 (2.75%) | | 599 (2.39%) |
| Pairs needing more than 7 | 0 | | 0 |

The pilot's work is not thrown away: `bomb resume` on the same directory
finished the remaining 23,077 pairs in 1.1 s and produced the same file as
an undisturbed build.

**Estimates the pilot allowed, as estimates.** From 55 of 2,000: between
2.0% and 3.5% of the open strings need 7 nodes (95% interval), that is 1,020
to 1,740 strings. No sampled pair needed more than 7, from which one may say
with 95% confidence that fewer than 0.15% of the open pairs do (fewer than
38 of 25,077). Neither statement is a bound on the maximum; a sample cannot
give one. The exact figures in section 1 came from the full run: 1,198
strings at 7, none above.

## 4. Stage B: search

For the 25,077 open pairs, LevelSolver at node budget 6 and, where that is
proven to have no solution, at 7. Engine `amd64-auto`, no step limit.

| Budget | Searches | Solved | Proven unsat | Stopped by a limit | Logical steps | Solver time |
|---:|---:|---:|---:|---:|---:|---:|
| 6 | 25,077 | 24,478 | 599 | 0 | 1,101,722,898 | 1.12 s |
| 7 | 599 | 599 | 0 | 0 | 16,216,097 | 0.02 s |
| total | 25,676 | 25,077 | 599 | 0 | 1,117,938,995 | 1.13 s |

About 10^9 logical steps per second. The most expensive pair,
`0001111111111101` and its complement, took 1,537,313 steps, about 1.5 ms.
No search is long enough for a step limit to matter, and none was set.

## 5. Whole construction, by settings

Wall-clock time of `bomb build` from an empty directory to a complete
journal, 16 bits. "Enumeration" is `--enumerate-max`.

| Enumeration | Symmetry | Engine | Workers | Time | Journal |
|---:|---|---|---:|---:|---:|
| 5 (default) | on | amd64-auto | 1 | 15.5 s | 8.2 MB |
| 5 | on | amd64-auto | 2 | 8.4 s | 8.2 MB |
| 5 | on | amd64-auto | 4 | 4.6 s | 8.2 MB |
| 5 | on | c | 1 | 19.5 s | 8.2 MB |
| 5 | off | amd64-auto | 4 | 8.7 s | 16.7 MB |
| 4 | on | amd64-auto | 1 | 2.1 s | 3.9 MB |
| 4 | on | amd64-auto | 4 | 0.7 s | 3.9 MB |
| 0 (search only) | on | amd64-auto | 1 | 2.3 s | 12.1 MB |
| 0 | on | amd64-auto | 4 | 0.7 s | 12.1 MB |
| 0 | on | c | 1 | 10.3 s | 12.1 MB |
| 0 | off | amd64-auto | 1 | 4.8 s | 24.2 MB |
| 0 | off | amd64-auto | 4 | 1.4 s | 24.2 MB |
| 0 | off | c | 1 | 21.6 s | 24.2 MB |
| 6 | on | amd64-auto | 4 | 29 min | 62.0 MB |

Two things stand out.

The search is the cheap part. With the default settings the five-node
enumeration is 14 of the 15.5 seconds; the LevelSolver settles node budgets
1 to 5 for all strings in less time than the enumeration needs for five
nodes alone (rows "0" against rows "5"). Enumeration is kept as the default
for what it adds as evidence, not for speed: it is a second, independent
derivation of every minimum up to 5 and of the lower bound 6 for everything
else (section 6).

The work divides well: 1, 2 and 4 workers give 15.5, 8.4 and 4.6 seconds.

All fourteen constructions certify the same minimum for every string
(section 6). Constructions with the same enumeration and symmetry settings
produce the same file, whatever the engine and the number of workers.

## 6. Checks

**The test suite** (`ctest`): 14 tests. `test_fixed_run` covers the format,
the enumerator and the constructor on small tables, including interruption
at arbitrary moments, damaged and forged journals and engines that lie; its
checks were themselves checked by introducing 16 different bugs into the
code, one at a time, each of which made it fail. `bomb_16` and `bomb_16full`
build the 16-bit table and compare the file's SHA-256.

**Every witness run.** `bomb verify` decodes all 65,536 entries and runs
each graph on the interpreter for 16 bits against its own index.

**Complement symmetry.** For every entry the transformed graph emits the
complement string, and the two entries of every pair hold the same node
count. A construction with `--symmetry off`, which searches all 65,536
strings and never uses the transformation, certifies the same minima.

**Minima against other constructions.** `bomb verify --file A --work-dir B`
checks every certified minimum of table A against the journal of
construction B. Done for the default table against the journals of the
constructions with enumeration 4, with enumeration 0, with enumeration 0 and
symmetry off, with enumeration 5 and symmetry off, and with enumeration 6,
and in the other direction; 65,536 of 65,536 agree each time. So the minima of 1 to 5 nodes
and the lower bound 6 come out the same from enumeration and from search.

**Recomputation.** `bomb verify --recertify` solves every string again with
the MinimalSolver from node budget 1, with no journal, enumeration or
symmetry involved:

| Engine | Strings | Minima reproduced | Differ | Steps | Time |
|---|---:|---:|---:|---:|---:|
| amd64-auto | 65,536 | 65,536 | 0 | 4.86·10^9 | 4.5 s |
| c | 65,536 | 65,536 | 0 | 4.86·10^9 | 21.5 s |

**Engines.** Constructions with the C engine and with `amd64-auto` produce
the same table file, byte for byte.

**Six-node enumeration** (`--enumerate-max 6`). Not part of the default
construction and not needed to repeat; run once here as a check that owes
nothing to the search. All 139,314,069,504 labeled six-node graphs, of which
the 69,657,034,752 with a white root were run, took 6,966 s of processor
time: 29 minutes with 4 workers. 48,956 strings are first emitted at six
nodes, 64,338 are emitted by some graph of at most six nodes, and **the
other 1,198 are emitted by none**. That is the split the search found,
obtained without any search. In that construction every minimum from 1 to 6
and the lower bound 7 for the remaining 1,198 strings rest on enumeration
alone; the search supplied only the 599 seven-node witnesses, each of which
is run. Its table holds the same node count in every entry as the default
table (each checked against the other's journal).

### What rests on what

With the default settings:

| Strings | Minimum | Evidence |
|---:|---:|---|
| 15,382 | 1–5 | A witness, run. Enumeration: no smaller graph emits it. No search involved |
| 48,956 | 6 | A 6-node witness from the search, run. Enumeration: no graph of 5 or fewer nodes emits it |
| 1,198 | 7 | A 7-node witness from the search, run. Enumeration for 5 or fewer nodes; for 6 nodes, a PROVEN_UNSAT result of the LevelSolver |

The LevelSolver's exhaustiveness is relied on only in the last row, for 599
searches. It is tested against exhaustive enumeration for up to 5 nodes in
`tests/test_oracle.c`, and for these 599 searches specifically by the
six-node enumeration above.

## 7. Reproducing it

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --parallel
```

```bash
build/carlobomb bomb build --bits 16 --max-nodes 16 --engine amd64-auto --workers 8 --work-dir bomb-work
```

```bash
build/carlobomb bomb finalize --work-dir bomb-work --output fixed-run-16.frb
```

```bash
build/carlobomb bomb verify --file fixed-run-16.frb --work-dir bomb-work --recertify
```

```bash
sha256sum fixed-run-16.frb
```

Set `--workers` to the number of cores. Expect about 15 seconds of
processor time for the enumeration, divided by the number of workers, plus a
second or two; under 10 MB of disk; a few megabytes of memory per process.
No step limit is needed. To see the restart work, interrupt the build with
Ctrl-C during the five-node enumeration and continue with:

```bash
build/carlobomb bomb resume --work-dir bomb-work
```

The table is a function of the settings, not of the machine. SHA-256 of
`fixed-run-16.frb`:

| Settings | SHA-256 |
|---|---|
| default (`--enumerate-max 5`, symmetry on) | `0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14` |
| `--enumerate-max 4` | `3580e791c1b2985d595296c04ff0ed66d405019be9ee3f9d46c5b43e897b6e59` |
| `--enumerate-max 0` | `3973b488a4ff8223d18da2516ddfcabb8b7b0aa0d03ad657d331f91701d31134` |
| `--enumerate-max 0 --symmetry off` | `2682a811a0c61e1fa336ce5b8e02638b35b1bd067ad121e2d1b8a0673c3b30f5` |
| `--symmetry off` | `38171274ea282f159117b91f2dfacdfb44e12396de500cb631f3039f168305ae` |
| `--enumerate-max 6` | `58ca15bd5db4311e61ae180efbb5e171387cda91a69cf2500bb3e0b12c187487` |

The files differ only in which minimum-node witness they hold for a string:
enumeration and search find different graphs of the same size. Node counts
and statuses are the same in all of them.

The table is not committed to the repository: it is 1.2 MB of binary that
the source reproduces in seconds, and the checksums above identify it.

## 8. Two points where the plan and the measurements differ

The milestone plan was written before these numbers existed, and two of its
working assumptions turned out differently. Both were followed as written;
the measurements are recorded here for the next plan.

**Whether 16 nodes suffice.** The plan asked that this not be assumed. It
does not need to be: a ring of L nodes emits any L-bit string, which the
test suite verifies for every string of every length from 1 to 16 by
running them. The software uses the ring as the starting witness of every
string, so "a valid witness within 16 nodes for every string" holds from
the first second of a build, verified rather than assumed. The measured
answer is far below the bound: 7 nodes.

**Enumeration for small node counts, search for large ones.** The plan
expected enumeration to be the cheap way to settle 1 to 5 nodes. On this
problem the search is cheaper at every node count: section 5. What
enumeration provides is independence, and the default keeps it for that
reason. A construction that has to be fast rather than doubly derived can
pass `--enumerate-max 0`, and for longer strings, where the table of
strings grows and the number of small graphs does not, the balance will
shift again.

## 9. What this does not cover

- Lengths above 16 bits need another table format (more than 16 nodes may
  be needed, and a table indexed by the string grows as 2^L).
- Timings are from one shared virtual machine.
- The hardware-counter and second-processor measurements requested after
  Milestone 1 are still open (`benchmarks/measure_machine.sh`); nothing in
  this milestone depended on them.
