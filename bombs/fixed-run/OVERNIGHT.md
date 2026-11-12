# FixedRunBomb(24): running the ladder

How to build the complete, certified table for every 24-bit string on one
machine, unattended, and how to check it afterwards. The estimate and the
measurements behind it are in
[benchmarks/FIXED_RUN_24_FEASIBILITY.md](../../benchmarks/FIXED_RUN_24_FEASIBILITY.md):
about 2 hours on an Intel Core i5-14400F, with 3.5 hours as a pessimistic
case.

## What runs

`carlobomb bomb ladder` builds the table for 17 bits from the verified
16-bit table, the table for 18 bits from that, and so on up to 24. Each
finished level is a complete, certified Bomb, written to disk and read back
before the next level starts. If the run stops at any point, for any reason,
the longest complete Bomb is in the directory and the same command continues
from the last recorded search.

```
DIR/fixed-run-16.frb     the starting Bomb, copied in after full validation
DIR/fixed-run-17.frb     complete Bomb for 17 bits
...
DIR/fixed-run-24.frb     complete Bomb for 24 bits (570,425,408 bytes)
DIR/work-NN/             journal of level NN: the evidence for its table
DIR/level-NN.txt         report of level NN: distribution, maximum, cost
```

## Before starting

1. Work inside the Linux file system of WSL (`~/...`), not under `/mnt/c`.
2. Free disk space: 6 GB. The tables take 1.1 GB and the journals about
   0.8 GB; each table is written to a temporary file first, 0.6 GB for the
   last one. The run stops cleanly if less than 2 GiB are free
   (`--min-free MIB` changes that).
3. Memory: the coordinating process needs about 2 GB at level 24; each
   worker a few MB.
4. Keep Windows awake: Settings, System, Power, Sleep: Never. A sleeping
   host stops WSL.
5. Keep the WSL terminal window open for the night. When the last WSL
   window is closed, Windows shuts the distribution down after a short idle
   time, background processes included, `nohup` or not.

If either happens anyway, or Windows restarts for an update, nothing is
lost: see "If it stops".

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
ctest --test-dir build -j 4
```

17 tests. `test_fixed_run_ladder` and `bomb_16full` cover what is used here;
`bomb_16full` builds the 16-bit table and one level above it and compares
both with the checksums below.

## Choose the worker count (two minutes each, optional)

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 6
```

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 10
```

```bash
build/ladder_pilot --bits 20,22,24 --samples 4000 --workers 16
```

The last lines of each run project the whole ladder at this machine's
measured throughput for that many processes. Use the fastest. The
i5-14400F has 6 performance cores with two threads each and 4 efficiency
cores: 6, 10 and 16 are one process per performance core, per core, and per
hardware thread. Workers take jobs from a shared counter, in the pilot as
in the construction, so a slow core or a second thread does fewer jobs and
holds nothing up; more workers should not be slower. If in doubt: 10.

## The run

The starting Bomb is the verified 16-bit table,
SHA-256 `0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14`.
If it is not at hand, it takes four seconds to make:

```bash
build/carlobomb bomb build --bits 16 --workers 6 --work-dir work-16
```

```bash
build/carlobomb bomb finalize --work-dir work-16 --output fixed-run-16.frb
```

Then, so that it survives a closed terminal:

```bash
nohup build/carlobomb bomb ladder --dir ~/frb24 --from fixed-run-16.frb --to 24 --workers 10 > ~/frb24.log 2>&1 &
```

```bash
tail -f ~/frb24.log
```

Every 30 seconds the log shows the level, the searches done and to do, the
throughput, the time left for the level, how many strings have a certified
minimum, how many are open, and the minimum-node distribution of those
settled so far. After each level it prints the level's report and an
estimate for the levels that remain.

**After the first minutes**, compare with the checksums below. Levels 17 to
20 take about a minute in all, 21 and 22 a few minutes more, and their
files are the same bytes on every machine, with every engine and any number
of workers:

```bash
sha256sum ~/frb24/fixed-run-*.frb
```

| Bits | SHA-256 | Largest minimum | Strings that need it |
|---:|---|---:|---:|
| 16 | `0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14` | 7 | 1,198 |
| 17 | `e6d4cc43d3edc56f88e96037cef81f7f4c0b6e88ecbaef5052339596726eecdb` | 8 | 2 |
| 18 | `e67fb5a7c813d5c6c2d18532e2eb8a12dbf16f5fe2cf10f89ad9c02578a011b1` | 8 | 6 |
| 19 | `86e5000f3b81abb57de2d318e5ebad7454e1d6db4395187d10310853682efa25` | 8 | 118 |
| 20 | `5f5025bc00a26cad5153364a3bb3528c51d1d9cc9a7db16cf63df36b7393ca14` | 8 | 3,048 |
| 21 | `1581fb018ee718e77fd77311eaba41a7304219b54cba4235c551dcdc920ed8cb` | 9 | 2 |
| 22 | `189e6ea0cf71cbcadb1765a017bf0e611027c64684f7b53ce34165d5811dcb98` | 9 | 6 |

(Levels 17 to 20 were built twice in the development container: with
`amd64-auto` and 4 workers, interrupted by Ctrl-C and by `kill -9` and
resumed; and with the C engine and 3 workers, uninterrupted. Byte-identical
at every level. Levels 21 and 22 were built once, with `amd64-auto`, and
verified in full.)

The time left that a progress line shows for its level is a straight
extrapolation from the jobs done. Strings early in the table are cheaper
than the rest, so early in a level it runs 10 to 20% low. The estimate
printed after each level, for the levels still to come, does not have that
bias.

## Looking in on it

```bash
build/carlobomb bomb ladder --dir ~/frb24 --status
```

lists the complete Bombs and names the level under construction.

```bash
cat ~/frb24/level-*.txt
```

shows the reports of the finished levels.

```bash
build/carlobomb bomb status --work-dir ~/frb24/work-24
```

replays the journal of a level, running or finished: progress, histogram,
search effort by node budget and per string. It reads; it disturbs nothing.

## If it stops

Ctrl-C or `kill` (SIGINT, SIGTERM): every worker finishes the search it is
in, records it, and the program says where it stands. A crash, `kill -9`, a
closed WSL session or a power cut lose at most the searches not yet on disk
(the journal is forced to disk every 10 seconds); a half-written record at
the end of a journal file is recognized by its checksum and ignored. In
every case:

```bash
nohup build/carlobomb bomb ladder --dir ~/frb24 --to 24 --workers 10 >> ~/frb24.log 2>&1 &
```

The same command, without `--from`. It finds the longest complete Bomb,
replays the journal of the level above it (2.5 s at level 22; expect about
ten seconds at level 24) and searches only what is still open. The worker count and the engine may
be different each time.

A level is never reported complete, and its table never written, unless
every one of its strings has a certified minimum. A search that was
interrupted is not a result and is not recorded as one.

`--time-limit SECONDS` stops the ladder cleanly after that time, if the
machine is needed back at a fixed hour.

## If 24 bits turn out too slow

Nothing needs to be decided in advance. `--to 24` passes through 22 and 23,
and each is a complete Bomb when its level ends. The log says after every
level how long the rest will take at the growth measured so far. Stop
whenever the estimate is too long: the files are there.

## Afterwards: checking the result

Everything in the table, the journal behind it, and its agreement with the
16-bit table (8 s and 0.5 GB of memory at 22 bits; expect about 40 s and
2 GB at 24):

```bash
build/carlobomb bomb verify --file ~/frb24/fixed-run-24.frb --work-dir ~/frb24/work-24 --against ~/frb24/fixed-run-16.frb
```

This checks the header, length and checksums; decodes every entry and runs
every REV on the interpreter for 24 bits; checks complement symmetry;
checks that every certified minimum is backed by the journal of the level
(which in turn runs every REV of the 23-bit table again); checks that the
minimum of every 16-bit string is the smallest among its 256 extensions,
and that no 16 consecutive bits of any string need over one node more than
the string. Then it prints the exact distribution, the largest minimum, the
number of strings that need it, the comparison with ⌈2√L⌉ − 1, and where
`0…01` stands.

A sample recomputed from nothing, by the other method (every node budget
from 1 up, no base table, no symmetry) and the other engine. Expect roughly
30 ms a string at 24 bits, a few minutes for this sample:

```bash
build/carlobomb bomb verify --file ~/frb24/fixed-run-24.frb --recertify --sample 5000 --engine c
```

The minimum of every string of every length from 1 to 24 bits, read out of
the table by the FixedSolver (the 16-bit row must be 2, 6, 64, 962, 14,348,
48,956, 1,198):

```bash
build/carlobomb fixed --bomb ~/frb24/fixed-run-24.frb --stats
```

Search effort, by node budget and per string, with the costliest string:

```bash
build/carlobomb bomb status --work-dir ~/frb24/work-24
```

And the checksum of the artifact:

```bash
sha256sum ~/frb24/fixed-run-24.frb
```

## What is and is not claimed

A level's table says MINIMAL for a string when a REV of n nodes for it has
been run on the interpreter and every budget below n is excluded: by the
certified minimum of its first L − 1 bits in the table below (a REV for the
string emits those bits), and by searches that ran to the end and returned
PROVEN_UNSAT. The table below was built the same way, down to the 16-bit
table, which was built from complete enumeration and search from one node
and recomputed in full with two engines.

So the chain is: 16 bits verified independently; each level certified by
its own journal on top of the one before; the top level cross-checked
against the 16-bit table and by sample against a direct search. A complete
24-bit table is an exact result for all 16,777,216 strings. Nothing short
of complete is called that by any command here.
