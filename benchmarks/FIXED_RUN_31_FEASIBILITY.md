# FixedRunBomb(25–31): feasibility

What it takes to continue the ladder above 24 bits: time, memory, disk, and
whether anything about the program has to change.

Measured on 12 and 13 November 2026 in the development container (Intel
Xeon, family 6 model 173, 2.3 GHz, 4 virtual CPUs, 15.7 GB of memory, GCC
13.3, `amd64-auto`). No 25-bit or 26-bit table was built here. Sizes are in
GB of 2^30 bytes.

## 1. Answers

- **25 bits: yes, tonight.** About 6.4·10^13 search steps, three times
  level 24. Three to four hours on the owner's machine.
- **26 bits: yes, in a day.** About 2.6·10^14 steps, twelve times level 24.
  Twelve to fifteen hours.
- **Memory and disk are not the obstacle** for either: 2.6 and 5.2 GB of
  memory, 2.5 and 5.1 GB of disk.
- **The AMD64 search stays efficient.** At 26 bits 99% of the steps are
  still made with at most 8 nodes, in the kernel that keeps the whole graph
  in registers; the kernel for more nodes makes as many steps per second.
- **27 and 28 bits need no other program, only time:** about 2 days and
  about a week. The state of the construction, 10 and 21 GB, fits the
  machine.
- **29 bits is the last length the present storage strategy reaches** (42 GB
  of state on a 64 GB machine) and takes about three weeks.
- **30 and 31 bits need the state on disk instead of in memory**, and two to
  five months of search on one machine. The format is ready for them; the
  search time, not the storage, is what would have to change first.

## 2. Search effort, 24 to 30 bits

`benchmarks/ladder_pilot.c`, as for the 24-bit study
(`FIXED_RUN_24_FEASIBILITY.md`): random strings of L − 1 bits, each solved
for its minimum, then the extension its REV does not emit searched from
that minimum up, which is what the ladder does at level L. A level has
2^(L−2) such searches.

| Level L | Sample | Steps per search, mean | median | p99 | max | Level, steps | Against the level below |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 | 1,500 | 5.00·10^6 ± 1.1·10^5 | 4.5·10^6 | 1.8·10^7 | 3.8·10^7 | 2.10·10^13 | |
| 25 | 1,500 | 7.60·10^6 ± 2.3·10^5 | 6.0·10^6 | 5.2·10^7 | 1.0·10^8 | 6.4·10^13 | 3.0 |
| 26 | 1,500 | 1.56·10^7 ± 5.3·10^5 | 9.0·10^6 | 1.1·10^8 | 1.6·10^8 | 2.6·10^14 | 4.1 |
| 27 | 1,000 | 3.16·10^7 ± 1.1·10^6 | 1.9·10^7 | 1.3·10^8 | 2.7·10^8 | 1.06·10^15 | 4.1 |
| 28 | 600 | 5.70·10^7 ± 2.0·10^6 | 4.6·10^7 | 2.0·10^8 | 2.5·10^8 | 3.8·10^15 | 3.6 |
| 29 | 300 | 7.5·10^7 ± 6.7·10^6 | 5.9·10^7 | 3.0·10^8 | 1.8·10^9 | 1.0·10^16 | 2.6 |
| 30 | 160 | 1.03·10^8 ± 9.8·10^6 | 8.2·10^7 | 4.4·10^8 | 1.3·10^9 | 2.8·10^16 | 2.7 |

The ± is one standard error of the mean. The samples shrink with the length
because each string costs more; from 29 bits the level totals are good to
about a fifth.

**The 24-bit baseline.** The pilot of the 24-bit study projected levels 20
and 22 at 1.43·10^11 and 2.35·10^12 steps; the constructions measured
1.428·10^11 and 2.356·10^12. Level 23, built here since, cost
7,720,456,914,039 steps. For level 24 two independent samples give
2.11·10^13 and 2.10·10^13. Step counts do not depend on the machine: the
owner's 24-bit run gives the exact figure for level 24
(`level-24.txt`, "searches … logical steps"), and the projections for 25
and 26 scale with whatever it shows against 2.10·10^13.

**By node budget**, sibling searches of the samples:

| Level | Budget 6 and below | 7 | 8 | 9 | 10 | Strings found at 9 or 10 nodes |
|---:|---:|---:|---:|---:|---:|---:|
| 24 | 0.5% | 67.7% | 31.8% | 0.0% | | 0.1% |
| 25 | 0.2% | 37.2% | 62.5% | 0.1% | | 0.2% |
| 26 | 0.1% | 11.4% | 87.7% | 0.8% | | 3.2% |
| 27 | 0.0% | 3.3% | 94.5% | 2.2% | | 14% |
| 28 | | 0.9% | 89.8% | 9.4% | | 43% |
| 29 | | | 69% | 31% | 0.1% | 66% |
| 30 | | | 45% | 55% | 0.1% | 86% |

Shares of the search steps. At 25 bits the work moves from 7 nodes to 8; it
stays at 8 through 28 bits and moves to 9 around 30.

Finding the REV takes 65% of the steps at 25 bits and 75% at 26; proving
the smaller budgets empty takes the rest.

**The hardest strings.** Of the structured families of the 24-bit study the
costliest at 25 bits is `0…011` at 4.2·10^7 steps (9 nodes); at 26 bits it
is `0…01`, which needs **10 nodes** there, 4.25·10^8 steps, 0.43 s. That is
⌈2√26⌉ − 1. Random strings reached 1.6·10^8 steps. Nothing seen at these two
lengths costs more than half a second.

## 3. Time

Steps per second depend on the machine. The owner's 24-bit run gives its
rate directly; before it is in, the two rates used in the 24-bit study:

| Level | Steps | At 5.1·10^9 steps/s (6 workers, measured on 16 bits) | At 6.5·10^9 (10 workers, assumed) |
|---:|---:|---:|---:|
| 24 | 2.1·10^13 | 1.1 h | 0.9 h |
| 25 | 6.4·10^13 | 3.5 h | 2.7 h |
| 26 | 2.6·10^14 | 14 h | 11 h |
| 27 | 1.06·10^15 | 2.4 days | 1.9 days |
| 28 | 3.8·10^15 | 8.7 days | 6.8 days |
| 29 | 1.0·10^16 | 23 days | 18 days |
| 30 | 2.8·10^16 | 63 days | 49 days |
| 31 | about 7·10^16 | 5 months | 4 months |

Level 31 is extrapolated at the growth of the last two levels measured. Each
level costs two to three times as much as all the levels below it together,
so a ladder to L is 1.3 to 1.5 times its last level.

Simplest rule: **level 25 takes three times as long as level 24 took on the
same machine, level 26 twelve and a half times.** To this come, per level,
about a minute to replay the journal and run the REVs of the table below,
and a minute or two to write, read back and validate the table.

## 4. Memory

A level's coordinating process holds the **state**: 40 bytes of bookkeeping
and the packed witness for every string, 83 bytes each in format version 3.
It is held while a round is planned and while the table is written, and
released while the workers search. A worker holds the list of jobs (8 bytes
per job, shared) and a few megabytes.

Measured on constructions of 2^25 and 2^26 entries that were started,
stopped after a few hundred strings, finalized as partial tables and
verified:

| | 25 bits | 26 bits |
|---|---:|---:|
| A session: state and job list | 2.7 GB | 5.6 GB |
| Writing the table, program of the 24-bit run | 4.0 GB, 66 s | 7.9 GB, 138 s |
| Writing the table, now | 2.6 GB, 43 s | 5.2 GB, 102 s |
| Verifying a table against its journal, program of the 24-bit run | 4.0 GB, 81 s | 7.9 GB, 168 s |
| Verifying a table against its journal, now | 2.6 GB, 70 s | 5.2 GB, 167 s |
| Verifying a table by itself, every REV run, now | none to speak of, 57 s | none to speak of, 139 s |

Memory is what the process itself holds (its heap), at the peak. The program
of the 24-bit run built the table in memory beside the state and read it
back into memory again. Now a table is written entry by entry from the
state and read back through a mapping of its file (section 6); the pages of
a mapped file are the system's to keep or drop, and with them the largest
figure was 4.0 GB at 25 bits and 7.9 GB at 26. These constructions had no
table below them, so their job lists were twice those of a ladder level
(0.25 GB at 26 bits instead of 0.13).

For any length, in advance:

| Bits | State | Fits |
|---:|---:|---|
| 25 | 2.6 GB | anything |
| 26 | 5.2 GB | 8 GB |
| 27 | 10.4 GB | 16 GB |
| 28 | 20.8 GB | 32 GB: WSL's default share of a 64 GB machine |
| 29 | 41.5 GB | 64 GB, with WSL allowed more than its default |
| 30 | 83 GB | no |
| 31 | 166 GB | no |

The program computes this before it starts a level and refuses one the
machine cannot hold.

## 5. Disk

| Bits | Table | Journal, about | Level in all | Ladder from 24 up to here |
|---:|---:|---:|---:|---:|
| 25 | 1.4 GB | 1.1 GB | 2.5 GB | 3 GB |
| 26 | 2.8 GB | 2.3 GB | 5.1 GB | 8 GB |
| 27 | 5.5 GB | 4.5 GB | 10 GB | 18 GB |
| 28 | 11 GB | 9 GB | 20 GB | 38 GB |
| 29 | 22 GB | 18 GB | 40 GB | 78 GB |
| 30 | 44 GB | 36 GB | 80 GB | 158 GB |
| 31 | 88 GB | 72 GB | 160 GB | 318 GB |

A table is 44 bytes per string. A journal record is 96 bytes in version 3,
and levels 22 and 23 wrote 1.34 and 1.46 records per search job; 1.5 is
assumed. The ladder column counts every table and journal on the way and
the 0.5 GB copy of the 24-bit Bomb. A table is written to a temporary file
and renamed, so nothing is needed twice.

Journals can go once a level is verified, if space matters: they are the
evidence for the level's minima and are needed to check it against
(`--work-dir`), not to use the table or to build the next level.

## 6. What was changed for this, and what was not

The construction of the 24-bit run was kept. Three things were required and
are done; the rest is listed with the reason it was left.

**Required, done**

1. *Format version 3* (`bombs/fixed-run/FORMAT.md`): 31 nodes of 11 bits,
   43 bytes, 44 with the metadata byte; versions 1 and 2 unchanged. A level
   in version 3 is built on a base in version 2: the base's REVs are
   decoded in their format, run, and packed in the new one.
2. *Sizes and positions in 64 bits.* A 26-bit table is longer than 2^31
   bytes and a 27-bit one longer than 2^32. File lengths, entry offsets and
   seeks are 64-bit everywhere; a table index and the number of entries stay
   32-bit, which holds 31 bits and 2^31. Verified on a 2,952,790,080-byte
   file, with lookups at both ends.
3. *No table in memory beside the state.* Finalization built the table in
   memory beside the state and then read it back into memory a second
   time. Now a streaming writer (`FrbWriter`) takes the
   entries one at a time, runs each witness, keeps the summary and the
   checksum as they pass and writes the header last. And a table that is
   read is a private mapping of its file, not a copy: every reader
   (validation, symmetry, the comparison with a shorter Bomb, the evidence
   check, the FixedSolver, the base of a construction) works on a table
   larger than memory without a second code path.

Also done because an unattended run needs it: memory is checked against the
state before a level starts, disk against the table and the expected
journal.

**Looked at, not required up to 29 bits, not done**

| Question | Finding |
|---|---|
| Bounded-memory journal replay | Replay already reads one record at a time; its memory is the state it builds, not the journal. Level 22 (1.4 million records and 2.1 million base REVs) replays in 2.5 s; level 26 is 16 times that, under a minute; level 31 would be about twenty minutes |
| Checkpoints, so as not to replay everything | Replay is run when a session starts and ends. At under a minute against hours of search, and twenty minutes against months, it is below a thousandth of the work at every length. A checkpoint would be a second record of the state to keep in step with the journal, which the design avoids on purpose. Not worth it |
| Incremental checksums | The table checksum is taken piece by piece on the way in and on the way out; nothing needs the table in one piece. One CRC-32 over 88 GB is a weak guard by itself, but it is not by itself: every entry is run and must emit its own index. A list of checksums per block beside the file would let a damaged block be located; optional |
| Random-access reads | One seek, 44 bytes, one run of the REV: `bomb lookup` on the 2.95 GB file takes as long as on a small one. Nothing to do |
| Smaller state | 28 of the 40 bookkeeping bytes per string describe its own searches (count, steps, time, the step limit that stopped one), most of it kept for `bomb status`. Dropping them from the state would bring 83 bytes to 55: a third less memory, 29 bits in 28 GB. It moves no length from impossible to possible on the machine at hand. Optional |
| Smaller journal records | A record carries a 43-byte graph of which a 9-node REV uses 13. A record of 64 bytes would hold every REV of up to 13 nodes and cut the journal by a third. It needs a second record size for larger REVs. Optional; worth having from 28 bits |
| Prefix index above 16 bits | The FixedSolver's index is one table index per prefix: 2 bytes up to 16 bits, 4 above. That is 256 MB at 26 bits and 8 GB at 31. One bit per prefix (which of the two extensions leads to the fewest nodes) is 8 MB and 256 MB, at the price of following up to L bits per question. In a ladder directory no index is needed at all: a string of ℓ bits is one entry of the ℓ-bit table. Optional; not needed for 25 and 26 |

**Required from 30 bits, designed, not built**

At 30 and 31 bits the state does not fit memory. What would replace it:

- *No job list.* The searches of a level are known from the table below:
  string number j of it gives one job. Workers read the base through its
  mapping and take ranges of j from the shared counter they already use.
- *One or two bytes per job* instead of 83 per string: how far its budgets
  are excluded and whether it is done. 0.5 GB at 31 bits.
- *The table by merging.* A worker takes jobs in ascending order, so every
  journal segment is already sorted by target. The table is the merge of
  the base, read once from front to back, with the segments; the half of
  the table that complement symmetry gives is the other half read backwards.
  No witness is held in memory.
- *Blocks that are closed.* The table written in blocks of, say, 2^20
  strings, each with a checksum, each closed when all its jobs are done. A
  closed block is never replayed; this is the checkpoint that the present
  design does not need and that one would.

That is a different construction, and its results would have to be shown
equal, byte for byte, to the present one on every table both can build.
It is worth building when 30 bits is within reach of the search, and it is
not: see the next section.

## 7. Does the AMD64 search remain efficient?

`amd64-auto` uses two kernels: up to 8 nodes the whole graph is in
registers (`n8x`); above 8, colors are in register bitmaps and links in
memory (`hybx`).

One process, nothing else running:

| Target | Minimum | Steps | `amd64-auto` | steps/s | `hybx` alone | C engine |
|---|---:|---:|---:|---:|---:|---:|
| 24 bits, random-looking | 7 nodes | 1.30·10^6 | 2.10 ms | 6.2·10^8 | 2.89 ms | 7.75 ms |
| `0…01`, 24 bits | 9 | 3.84·10^7 | 39.3 ms | 9.8·10^8 | 38.5 ms | 229 ms |
| `0…01`, 25 bits | 9 | 4.09·10^7 | 42.1 ms | 9.7·10^8 | 40.6 ms | 246 ms |
| `0…01`, 26 bits | 10 | 4.25·10^8 | 406 ms | 1.05·10^9 | 405 ms | 2,547 ms |

- Above 8 nodes the search makes 10^9 steps per second, more than below.
  The switch of kernel costs nothing per step.
- On the 7-node target the register kernel is 1.4 times as fast as `hybx`,
  and that is where it matters: budgets up to 8 are 99.9% of the steps at
  25 bits, 99.2% at 26, 90% at 28 and still 45% at 30 (section 2).
- Against the C engine: 3.7 times on the 7-node target, 6 times on the
  9- and 10-node ones.

So the representation is the right one for 25 and 26 bits and stays
reasonable to 31. What grows is not the cost of a step but the number of
steps: a factor of 3 to 4 per bit up to 28 bits, 2.7 after. A kernel that
is twice as fast buys less than one bit. Reaching 30 bits on one machine
needs fewer steps, not faster ones: a stronger lower bound than the minimum
of the first L − 1 bits, a search that shares work between sibling strings,
or the vectorized multi-search of `bombs/fixed-run/MULTI_SEARCH_SIMD.md`.

## 8. What was tested

- Format version 3 in the test suites: the worked example byte for byte,
  every field of every node, 31-node graphs, the three spare bits, links to
  nodes that do not exist, files that claim another version; the limits of
  every version computed a second way.
- Ladders in version 2, in version 3 and in version 3 on a version-2 base,
  on short strings: entry for entry the graphs of the version-1 ladder, and
  every minimum equal to the MinimalSolver's.
- A real base: level 23 started in version 3 on the 22-bit version-2 Bomb
  built for the 24-bit study, 30,000 searches, its partial table written,
  verified in full and against its journal.
- Full size: constructions of 2^25 and 2^26 entries as in section 4; the
  26-bit table is 2,952,790,080 bytes, longer than 2^31, and was written,
  read back, verified with all 67,108,864 REVs run, and read at its first
  and last entries.
- Beyond what can be constructed here (`benchmarks/big_table.c`): tables of
  27, 28 and 29 bits, empty but for the ring of a few strings at the ends,
  in the middle and on both sides of every power-of-two byte offset from
  2^31 up. Each was written entry by entry, read back by position at those
  places, and read whole through a mapping with every entry decoded:

  | Bits | File | Written in | Read whole and validated in | Heap |
  |---:|---:|---:|---:|---:|
  | 27 | 5,905,580,096 bytes | 24 s | 19 s | 4 MB |
  | 28 | 11,811,160,128 bytes | 45 s | 37 s | 4 MB |
  | 29 | 23,622,320,192 bytes | 91 s | 93 s | 4 MB |

  The 29-bit file is one and a half times the memory of the machine it was
  read on. The first attempt at it failed, usefully: a private mapping
  larger than memory is refused unless it is made without a reservation
  of memory, which it now is.
- 19 tests with GCC and Clang, 18 under AddressSanitizer and UBSan, 16
  without the assembly solver; the C and the AMD64 engine both.
- Unchanged: the checksums of the 16-bit and 17-bit tables, and with them
  versions 1 and 2.

Not done here, by instruction: a 25-bit or 26-bit construction.
