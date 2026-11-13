# Beyond 24 bits: the next stage of the ladder

How to climb from a finished FixedRunBomb(24) to 25 and 26 bits, what that
takes, and what the lengths after them would take. The measurements behind
the numbers are in
[benchmarks/FIXED_RUN_31_FEASIBILITY.md](../../benchmarks/FIXED_RUN_31_FEASIBILITY.md).

Tables of 25 to 31 bits are written in format version 3 (44 bytes per
entry, REVs of up to 31 nodes; [FORMAT.md](FORMAT.md)). Nothing about the
24-bit Bomb changes: it stays a version-2 file, and the level above it is
built on it as it is.

## What the run does

```
~/frb24/fixed-run-24.frb   read, validated in full, never written to
~/frb31/fixed-run-24.frb   a copy of it: the bottom rung of the new ladder
~/frb31/work-25/           journal of level 25 (format version 3)
~/frb31/fixed-run-25.frb   complete Bomb for 25 bits, 1,476,395,072 bytes
~/frb31/work-26/
~/frb31/fixed-run-26.frb   complete Bomb for 26 bits, 2,952,790,080 bytes
~/frb31/level-NN.txt       report of each finished level
```

The ladder is the one of [OVERNIGHT.md](OVERNIGHT.md): each level is built
on the complete table below it, is finalized and read back before the next
starts, and continues from its journal after any interruption. Everything
said there about looking in on a run, stopping it and resuming it holds
here without change.

The new ladder lives in a directory of its own and only reads the 24-bit
Bomb. The directory of the 24-bit construction, its journals and its files
are not touched, and can be verified with the program that built them at
any time, before, during or after.

## Before starting

1. **Use a separate checkout**, so the program that is running, or that
   built the 24-bit Bomb, stays as it is:

   ```bash
   git fetch origin && git worktree add ../CarloBomb-31 origin/feature/fixed-run-31
   ```

   ```bash
   cd ../CarloBomb-31 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
   ```

   ```bash
   ctest --test-dir build -j 4
   ```

   18 tests. Leave the tests for a moment when no construction is running:
   several of them time things.

2. **Disk:** 15 GB free where `~/frb31` will be. The copy of the 24-bit
   Bomb is 0.5 GB, the tables of 25 and 26 bits are 1.4 and 2.8 GB, their
   journals about 1.1 and 2.3 GB, and 2 GB are kept free. The run says at
   the start of each level what it will write and what is free, and does
   not start a level whose table could not be written.

3. **Memory:** the coordinating process holds the state of a level while it
   plans it and while it writes its table: 2.6 GB at 25 bits, 5.2 GB at 26.
   Workers hold a few megabytes. A machine that has less than a level needs
   is told so before the level is started.

4. Windows awake and the WSL window open, as before.

## The run

Verify the 24-bit Bomb first, with the program that built it
([OVERNIGHT.md](OVERNIGHT.md), "Afterwards"). Then:

```bash
nohup build/carlobomb bomb ladder --dir ~/frb31 --from ~/frb24/fixed-run-24.frb --to 26 --workers 10 > ~/frb31.log 2>&1 &
```

```bash
tail -f ~/frb31.log
```

The first minute goes into reading the 24-bit Bomb: every one of its
16,777,216 REVs is run before anything is built on it. Use the number of
workers that was best for the 24-bit run.

`--to 25` stops after the 25-bit Bomb; `--to 26` goes on. Either way the
same command, without `--from`, continues later:

```bash
nohup build/carlobomb bomb ladder --dir ~/frb31 --to 26 --workers 10 >> ~/frb31.log 2>&1 &
```

## How long

Measured on samples in the development container (1,500 random strings per
length): a search at level 25 costs 1.5 times a search at level 24, and
there are twice as many; at level 26 a search costs twice what it does at
level 25, because the first strings that need 10 nodes appear there.

| Level | Search steps, expected | Against level 24 |
|---:|---:|---:|
| 24 | 2.1·10^13 | 1 |
| 25 | 6.4·10^13 | 3.0 |
| 26 | 2.6·10^14 | 12.5 |

So level 25 takes about three times as long as level 24 took on the same
machine with the same workers, and level 26 about twelve times. If level 24
took 70 minutes: three and a half hours for 25, fourteen and a half for 26.
The log gives its own estimate after each level, from that machine's
measured cost.

A level that is not finished by morning is not lost and not a Bomb: its
journal holds every search made, the command above continues it, and
`fixed-run-25.frb` is complete and usable in the meantime.

## Afterwards

The same checks as for 24 bits, with the new program:

```bash
build/carlobomb bomb verify --file ~/frb31/fixed-run-25.frb --work-dir ~/frb31/work-25 --against ~/frb24/fixed-run-24.frb
```

```bash
build/carlobomb bomb verify --file ~/frb31/fixed-run-25.frb --hardest --engine c
```

```bash
build/carlobomb bomb verify --file ~/frb31/fixed-run-25.frb --recertify --sample 5000 --engine c
```

```bash
build/carlobomb fixed --bomb ~/frb31/fixed-run-25.frb --stats
```

```bash
sha256sum ~/frb31/fixed-run-*.frb
```

The first needs the memory of the level's state (2.6 GB at 25 bits, 5.2 GB
at 26) because it replays the journal; without `--work-dir` a table is
verified with next to none, however large it is. `--against` with the
24-bit Bomb checks that the minimum of every 24-bit string is the smaller
of the minima of its two extensions.

## The lengths after 26

The format holds tables up to 31 bits. What a length costs is another
matter; this is where each resource stands (GB = 2^30 bytes):

| Bits | Table | Journal, about | State in memory | Search steps, about | At 5 to 6.5·10^9 steps per second |
|---:|---:|---:|---:|---:|---:|
| 25 | 1.4 GB | 1.1 GB | 2.6 GB | 6.4·10^13 | 3 to 3.5 hours |
| 26 | 2.8 GB | 2.3 GB | 5.2 GB | 2.6·10^14 | 11 to 14 hours |
| 27 | 5.5 GB | 4.5 GB | 10.4 GB | 1.1·10^15 | 2 to 2.5 days |
| 28 | 11 GB | 9 GB | 20.8 GB | 3.8·10^15 | 7 to 9 days |
| 29 | 22 GB | 18 GB | 41.5 GB | 1.0·10^16 | about 3 weeks |
| 30 | 44 GB | 36 GB | 83 GB | 2.8·10^16 | about 2 months |
| 31 | 88 GB | 72 GB | 166 GB | 7·10^16 | 4 to 5 months |

The steps are from samples of 1,500 strings at 25 and 26 bits down to 160 at
30; 31 is extrapolated. The rate is what the 16-bit run of the machine in
question showed with 6 workers, and what 10 might give.

Up to 28 bits the program as it is fits a machine with 32 GB of memory, and
29 bits one with 64 GB; disk is no obstacle anywhere. From 30 bits the
state no longer fits, and a construction would have to keep it on disk
instead of in memory. That is designed in the report and not built: at
those lengths the search time is the larger problem by far, and it is the
one to solve first.

## A checksum to compare

The 23-bit table was built in the development container too, with the
program of the 24-bit run, interrupted once and resumed. Tables are the same
bytes on every machine, so `fixed-run-23.frb` of the 24-bit ladder must have
this SHA-256:

    61ee0779edc76e83da984d6970607cc3f5e333bdec267ca88209467daf491088

Largest minimum 9 nodes, needed by 44 strings; 7,720,456,914,039 search
steps in 3,052,702 searches.
