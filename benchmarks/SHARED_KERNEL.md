# The run kernel of the shared search, against the reference walk and the ladder

`--engine shared-run` makes the passes of a construction by shared search
with the run kernel (`solvers/shared/shared_run.c`) instead of the
reference walk of the SharedSolver (`shared_solver.c`, `--engine shared-c`,
the default). It is the same search and has to give the same passes:
the same strings found, with the same REVs and the same logical steps, and
so the same journal and the same table. This report holds it to that, and
says how much faster it is.

Measured on 15 November 2026 in the development container (4 virtual CPUs,
Intel Xeon under KVM at 2.1 GHz, 15.7 GB, GCC 13.3 `-O3`). Other work was
running beside every measurement here, more than four busy processes at
most times; wall-clock times are upper limits, and the comparison of speed
is by processor time of one process.

## In short

- **The same passes.** Every pass made by both walks reports the same:
  strings found, in the order found, their REVs, their steps, the steps of
  the strings not found, the counts.
- **The same files and the same searches** at full size: levels 17 to 24,
  each the certified ladder's file byte for byte, with the certified
  ladder's searches; levels 25 and 26, the table checksums, the number of
  searches and the logical steps of the owner's certified levels.
- **3.2 to 3.4 times the steps a second** of the reference walk: about
  5·10^8 a second on one processor, against 1.55·10^8.
- **Killed and gone on with**, at full size, ten times in one level, the
  two walks taking turns: the certified file and searches.
- **Checked by the engines that did not build it**: every search record of
  level 20 as the run kernel built it by the LevelSolver, none with another
  result, REV or step count; every pass of level 24 by the reference walk.

## What is the same and what is not

The logical step is the unit the project counts in, and the run kernel does
not change what one is or how many a search has. It makes most of them
without the work the reference walk does for each. In the reference walk a
step is one turn of a loop: a frame is read, its state looked at, a counter
of the beginning the walk stands at is incremented. Most steps of a pass
are of one kind: the links and colors the graph needs next are set, it
emits a bit, the bit is looked up among the beginnings that are wanted, and
two frames come and go for it, each with a step on the way down and one on
the way back. The run kernel emits such a bit in one turn of a small loop
that keeps no frames, with the graph in four registers, and adds four to
the count of that beginning when the stretch ends. Measured on two classes
of 24 bits at 8 nodes: four fifths of the steps are those four steps of a
bit; a pass has one such stretch for every 24 steps, 4.8 bits long on
average; and 88 in 100 of them end because the next bit is not wanted, 7 at
a link that is not set, 5 at something to decide.

Everything else it does as the reference walk does it, in the same order.
So of what a pass reports only one thing differs: the reference walk says
how deep its stack of frames went, and the run kernel, which has none, says
0. That number is in no record.

Node budgets above 15 are passed on to the reference walk: the run kernel
keeps a link in four bits. (No string of the certified Bombs, which go to
26 bits, needs more than 10 nodes.)

## Speed

`shared_comparison BITS CLASS_BITS CLASSES LOWEST HIGHEST` makes the passes
of some classes of a length, from nothing, budget after budget, by both
walks, compares everything each pass reports, and times each walk by the
processor time of the process.

| Strings | Classes | Steps | Reference walk | Run kernel | Ratio |
|---|---:|---:|---:|---:|---:|
| 22 bits, classes of 16,384 | 4 | 308,976,978 | 1.993 s, 155.0 M steps/s | 0.614 s, 503.1 M steps/s | 3.25 |
| 24 bits, classes of 65,536 | 2 | 1,430,709,693 | 9.253 s, 154.6 M steps/s | 2.854 s, 501.3 M steps/s | 3.24 |
| 26 bits, classes of 262,144 | 1 | 2,148,408,210 | 13.951 s, 154.0 M steps/s | 4.096 s, 524.5 M steps/s | 3.41 |

By node budget, at 26 bits: 2.1 at 6 nodes, 3.7 at 7, 3.5 at 8, 3.2 at 9.
Below 6 nodes a pass is over before either walk has warmed up.

Built with Clang 18 the 24-bit classes take 8.121 s and 3.559 s: 176 and
402 M steps a second, a ratio of 2.28.

For comparison, the LevelSolver's assembly kernels make 7 to 9·10^8 steps a
second on this machine. The run kernel is portable C.

## Levels 17 to 26

`bomb ladder --search shared --engine shared-run`, 3 workers, on the 16-bit
Bomb of the certified cloud ladder, with the other work mentioned above
running beside it.

| Level | File | Checksum of the searches | Passes | Steps made | Wall clock |
|---:|---|---|---:|---:|---:|
| 17 | the ladder's | `cfe3ca6b14751c64` | 546 | 167,470,800 | 0.4 s |
| 18 | the ladder's | `4ea7faef84ba8fa9` | 546 | 360,384,848 | 0.9 s |
| 19 | the ladder's | `fd24fd5780e06b84` | 563 | 1,004,501,423 | 2.1 s |
| 20 | the ladder's | `e29bf41eabad617f` | 658 | 2,742,655,266 | 4.7 s |
| 21 | the ladder's | `b2693d7177dfcf46` | 674 | 5,624,738,006 | 9.1 s |
| 22 | the ladder's | `e73d6c02528dd0d9` | 674 | 11,715,390,256 | 20.2 s |
| 23 | the ladder's | `8ae472a0d5f8feb9` | 681 | 29,659,989,869 | 66.2 s |
| 24 | the ladder's | `53103f47cac5e204` | 753 | 85,542,988,516 | 180.2 s |
| 25 | table checksum `7dfdbcd7`, the certified one | `9adfee923c31de0a` | 801 | 207,444,842,522 | 417.5 s |
| 26 | table checksum `8fc90cf7`, the certified one | `bc94191e6133ba1b` | 802 | 411,376,993,036 | 772.2 s |

"The ladder's" is `cmp` against the file of the certified ladder built
string by string. The checksum is what `bomb status` prints for the work
directory, and for each level it is what `bomb status` prints for the
certified ladder's work directory of that level. The passes and the steps
made are, to the last step, those of the same levels built by the reference
walk (`SHARED_CONSTRUCTION.md`): the run kernel makes the reference walk's
steps, faster.

For 25 and 26 bits the certified Bombs are on the owner's machine, and
what is compared is what the owner posted of them: the table checksums,
and the searches and logical steps of the two level reports (11,833,078
searches with 63,140,927,147,534 steps; 21,924,419 with
257,971,379,632,128), which the journals of these two levels have to the
last digit. The files have the SHA-256 of the files the reference walk
built (`95d708b2…c76c9e` and `302eefa4…28c2`, in full in
`SHARED_CONSTRUCTION.md`), and the checksums of the searches are the
reference walk's.

On time: the reference walk took 389 s, 1,394 s and 1,427 s for levels 24,
25 and 26 with 4 workers; these are 180 s, 418 s and 772 s with 3. Both
were measured beside other work, the second set beside more of it, so the
ratio of the walks is the one of the section above and not the one of
these numbers.

## Killed at full size, the walks taking turns

Level 22, 3 workers. The whole process group was killed with SIGKILL after
2 to 5 seconds, ten times in a row, the runs made by `shared-run` and by
`shared-c` in turn; the eleventh run, by `shared-run`, completed.

- `fixed-run-22.frb`: the certified ladder's file, byte for byte.
- 674 passes admitted, 1,408,471 searches with 2,356,037,716,175 logical
  steps and the checksum `e73d6c02528dd0d9`: the certified ladder's level 22.
- The journal has 8 sessions, 7 of which never ended. (Three of the kills
  came before the program, on a busy machine, had begun a session; they
  left nothing.) Nothing of a pass that was cut off is among the evidence,
  and passes admitted from one walk and from the other stand side by side.

The tests do the same in small at chosen places: journals cut after every
record and inside records, constructions that kill themselves after a
given number of steps, each walk finishing what the other was cut off in
(`bombs/fixed-run/tests/test_shared_build.c`).

## Passes made again

| Level, built by the run kernel | Passes | By | Search records | Not reproduced | Steps of the replay | Time |
|---:|---|---|---:|---:|---:|---:|
| 20 | all 658 | the LevelSolver (`amd64-auto`) | 378,045 | 0 | 142,813,952,259 | 233 s |
| 24 | all 753 | the reference walk | 6,273,837 | 0 | 85,542,988,516 | 673 s |

`--replay-passes level` and `--replay-passes shared` use the LevelSolver
and the reference walk whichever walk built the level. The replay of level
20 is the certified ladder's level 20 made again search for search: it
takes exactly that ladder's steps.

## How it is tested

- `tests/test_shared_solver.c` runs twice, once for each walk: every string
  of 1 to 12 bits (13 with `--deep`) at every budget, however cut into
  classes, random sets of wanted strings, classes of long strings, each
  against the LevelSolver string by string: result, REV, steps. Then the
  two walks against one another on whole passes, where it also requires the
  same order of finding. 3.8 million checks, 6.5 million with `--deep`.
- `bombs/fixed-run/tests/test_shared_build.c` builds levels by each walk
  with one worker and requires the same journal record by record, search
  records and pass records; builds the ladder by the run kernel in five
  ways of cutting and with 1 to 3 workers and requires the ladder's files
  and searches; and repeats the tests of interruption with the run kernel,
  and with each walk finishing what the other began.
- All of it under GCC and Clang, without the assembly kernels, and with
  AddressSanitizer and UndefinedBehaviorSanitizer.

## What it does not show

- A certified Bomb of a new length. This is a faster way to make the passes
  whose records certify; what a level built with it is worth is decided by
  its journal and by the replays, as for the reference walk.
- Behavior on another processor family. The gain is from doing less for
  each step, not from anything this processor has in particular, but only
  this one was measured.
- An assembly kernel. None was written.

## Reproducing

```bash
build/shared_comparison 24 8 2 1 9
```

```bash
build/carlobomb bomb ladder --dir run --from LADDER/fixed-run-16.frb --to 24 --workers 4 --search shared --engine shared-run
```

```bash
cmp run/fixed-run-24.frb LADDER/fixed-run-24.frb
```

```bash
build/carlobomb bomb verify --file run/fixed-run-20.frb --work-dir run/work-20 --replay-passes level
```
