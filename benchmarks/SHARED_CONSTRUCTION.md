# The construction by shared search, against the ladder

`carlobomb bomb ladder --search shared` builds a level by classes with the
SharedSolver instead of string by string with the LevelSolver
(`bombs/fixed-run/README.md`, "Shared search: a level in passes";
`FORMAT.md`, section 9.6). This report holds it against the ladder that
searches string by string, which is the reference: its files, its
searches, and what happens when the construction is killed.

Measured on 15 November 2026 in the development container (4 virtual CPUs,
Intel Xeon under KVM, 15.7 GB, GCC 13.3 `-O3`), with 4 workers, 128 classes
(`class_bits` 8, the default) and other jobs running beside it, so the
times are upper limits. The reference is the ladder of 17 to 24 bits built
there string by string, and for 25 and 26 bits the owner's certified
levels, known here by the reports the owner posted.

## In short

- **The same files.** Levels 17 to 24: each `fixed-run-L.frb` is the
  ladder's file, byte for byte. Levels 25 and 26: the table checksums of
  the owner's certified Bombs.
- **The same searches.** At every level the journal holds the ladder's
  searches: the same number, the same results, the same logical steps, the
  same checksum over all of them. 46 million searches in ten levels.
- **A hundredth of the work and less.** One step made for every 29 to 600
  logical steps of those searches. Level 24: 389 seconds, where the ladder
  took two hours on the same machine.
- **Killed and gone on with**, at full size, eight times in one level: the
  same file and the same searches.
- **Passes made again**: every search record of every pass of level 20 by
  the LevelSolver, 378,045 calls, none with another result, REV or step
  count; every pass of levels 22 and 24 by the SharedSolver.

## Level by level

| Level | File | Searches | Logical steps of the searches | Checksum of the searches | Passes | Steps made | | Wall clock |
|---:|---|---:|---:|---|---:|---:|---:|---:|
| 17 | the ladder's | 44,810 | 4,778,891,950 | `cfe3ca6b14751c64` | 546 | 167,470,800 | 1/29 | 1 s |
| 18 | the ladder's | 96,250 | 16,902,096,386 | `4ea7faef84ba8fa9` | 546 | 360,384,848 | 1/47 | 1 s |
| 19 | the ladder's | 199,403 | 47,431,541,189 | `fd24fd5780e06b84` | 563 | 1,004,501,423 | 1/47 | 3 s |
| 20 | the ladder's | 378,045 | 142,813,952,259 | `e29bf41eabad617f` | 658 | 2,742,655,266 | 1/52 | 7 s |
| 21 | the ladder's | 699,890 | 568,481,317,406 | `b2693d7177dfcf46` | 674 | 5,624,738,006 | 1/101 | 14 s |
| 22 | the ladder's | 1,408,471 | 2,356,037,716,175 | `e73d6c02528dd0d9` | 674 | 11,715,390,256 | 1/201 | 30 s |
| 23 | the ladder's | 3,052,702 | 7,720,456,914,039 | `8ae472a0d5f8feb9` | 681 | 29,659,989,869 | 1/260 | 126 s |
| 24 | the ladder's | 6,273,837 | 20,867,501,480,268 | `53103f47cac5e204` | 753 | 85,542,988,516 | 1/244 | 389 s |
| 25 | checksum `7dfdbcd7`, the certified one | 11,833,078 | 63,140,927,147,534 | `9adfee923c31de0a` | 801 | 207,444,842,522 | 1/304 | 1,394 s |
| 26 | checksum `8fc90cf7`, the certified one | 21,924,419 | 257,971,379,632,128 | `bc94191e6133ba1b` | 802 | 411,376,993,036 | 1/627 | 1,427 s |

"The ladder's" is `cmp` against the file of the ladder built string by
string. The searches, their logical steps and their checksum are what
`bomb status` prints for the work directory of the level; for 17 to 24 they
were compared with what it prints for the ladder's work directory, and are
equal in every figure, by node budget and in all. For 25 and 26 the
owner's level reports give the number of searches and their logical steps
(11,833,078 and 63,140,927,147,534; 21,924,419 and 257,971,379,632,128),
and those are equal; the checksums are for the owner to compare
(`bomb status` of this revision prints one for any work directory).

The file of level 25 has SHA-256
`95d708b2d1dcb1ffe4e9f1fdf1a9eb4119a2c636e0476bf477c218de17c76c9e`, that
of level 26 `302eefa4d7ba2abedf61dca4c7b8559c8c29eaa3517ec815ffa65739d77228c2`:
the files the shared-search experiment had written for those levels, and
what `sha256sum` of the owner's `fixed-run-25.frb` and `fixed-run-26.frb`
should give.

The wall-clock times of levels 23 to 26 are with two to nine other
processes competing for the four processors.

## Killed at full size

Level 22, 4 workers. The whole process group was killed with SIGKILL after
2 to 10 seconds, eight times in a row, and the same command started again
each time. The ninth run completed.

- `fixed-run-22.frb`: the ladder's file, byte for byte.
- 674 passes admitted, as in the undisturbed construction; 1,408,471
  searches with the ladder's logical steps and checksum.
- 8 sessions of the journal never ended; nothing of a pass that was cut off
  is among the evidence.

The tests do the same in small, at chosen places instead of chosen
moments: a journal cut after every record and inside every record, and
constructions that kill themselves after a given number of steps
(`bombs/fixed-run/tests/test_shared_build.c`).

## Passes made again

`bomb verify --work-dir DIR --replay-passes level` calls the LevelSolver
for every search record of a pass, that string at that budget, and
requires the record's result, REV and logical steps.
`--replay-passes shared` makes the pass again with the SharedSolver.

| Level | Passes | By | Search records | Not reproduced | Steps of the replay | Time |
|---:|---|---|---:|---:|---:|---:|
| 20 | all 658 | the LevelSolver (`amd64-auto`) | 378,045 | 0 | 142,813,952,259 | 224 s |
| 24 | 6 at random | the LevelSolver | 43,321 | 0 | 1.83·10^11 | 291 s |
| 22 | all 674 | the SharedSolver | 1,408,471 | 0 | 11,715,390,256 | 81 s |
| 24 | all 753 | the SharedSolver | 6,273,837 | 0 | 85,542,988,516 | 600 s |

The replay of level 20 by the LevelSolver is the ladder's level 20 made
again, search for search, in one process: it takes exactly the ladder's
steps.

## What it does not show

- Nothing about lengths beyond 26 bits as a certified construction. (The
  experiment of pull request #11 has run 27 and 28 bits; that is an
  experiment.)
- The owner's side of the comparison at 25 and 26 bits: the SHA-256 of the
  owner's files and the checksums of the owner's journals.
- Speed. The SharedSolver is portable C, about a fifth as fast per step as
  the assembly kernels; the times above are what that gives on a busy
  machine.
- Memory: the coordinating process holds the state of a level as before
  (2.6 GB at 25 bits, 5.3 GB at 26).

## Reproducing

```bash
build/carlobomb bomb ladder --dir shared --from LADDER/fixed-run-16.frb --to 24 --workers 4 --search shared
```

```bash
cmp shared/fixed-run-24.frb LADDER/fixed-run-24.frb
```

```bash
build/carlobomb bomb status --work-dir shared/work-24
```

```bash
build/carlobomb bomb verify --file shared/fixed-run-20.frb --work-dir shared/work-20 --replay-passes level
```
