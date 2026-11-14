# Every string of 25 and of 26 bits: the shared search against the certified Bombs

The CTO approved running the shared-search experiment in full at 25 and 26
bits, on five conditions. This is the report on them, in their order.

Run on 14 November 2026 in the development container (4 virtual CPUs, Intel
Xeon under KVM, 15.7 GB, GCC 13.3 `-O3`), 4 processes, 512 classes
(`--prefix-bits 10`). The logs of every run are in `runs/`.

## In short

| | 25 bits | 26 bits |
|---|---:|---:|
| Strings searched (one of each complement pair) | 16,777,216 | 33,554,432 |
| Left without a REV | **0** | **0** |
| Histogram, against the certified Bomb | every entry agrees | every entry agrees |
| REVs run on the reference interpreter, and correct | 16,777,216 of 16,777,216 | 33,554,432 of 33,554,432 |
| **From nothing:** logical steps | 223,274,268,884 | 428,024,955,093 |
| searching, 4 processes | 343 s | 643 s |
| **As the ladder's level,** on the Bomb below: logical steps | 223,173,123,256 | 427,828,430,118 |
| searching, 4 processes | 365 s | 676 s |
| table checksum (CRC-32) | `7dfdbcd7` | `8fc90cf7` |
| the certified table's checksum | `7dfdbcd7` | `8fc90cf7` |
| The ladder's searches, counted out of the shared search | 11,833,078 | 21,924,419 |
| their logical steps | 63,140,927,147,534 | 257,971,379,632,128 |
| the certified level report | 11,833,078 searches, 63,140,927,147,534 steps | 21,924,419 searches, 257,971,379,632,128 steps |
| The ladder's level, on the owner's machine | 8,267 s, 82,377 s of solver time | 33,268 s, 331,905 s of solver time |

- **Zero unresolved, and the certified histogram in every entry**, at both
  lengths, from nothing and as a level: 61,704 strings of 9 nodes at 25
  bits; 54,455,992 of 8, 1,187,852 of 9 and exactly 2 of 10 at 26.
- **The tables are the certified tables.** Built as the ladder builds a
  level, the experiment's 25- and 26-bit tables have the table checksums the
  owner's reports give for the certified Bombs. A table checksum covers
  every REV of every entry, so this is more than the node counts. The
  SHA-256 of the two files is below, for a comparison that leaves no room
  at all.
- **Every search the ladder made is accounted for.** The shared search
  counts, for each string and budget, the steps that the LevelSolver's own
  search of that one string takes. Added up over the level, that is the
  number of searches and the number of logical steps in the owner's
  certified reports, to the last of 14 and 15 digits, with no access to the
  owner's journals. At 17 to 24 bits, where the journals are at hand, it is
  every single record (section 2).
- **From nothing, with no Bomb to lean on**, the node count of every string
  of 25 and of 26 bits comes out the same as in the tables built on the
  ladder: the digests are equal. That is an independent recomputation of
  all 100 million certified node counts of the two Bombs.
- **What this is not**: a certificate. Section 4.

## 1. The runs

### From nothing

No Bomb is read. Every string that starts with 0 is wanted from one node
on.

25 bits (`runs/n25.txt`):

| Budget | Wanted | Found | Steps |
|---:|---:|---:|---:|
| 1 | 16,777,216 | 1 | 13,364 |
| 2 | 16,777,215 | 3 | 56,224 |
| 3 | 16,777,212 | 32 | 289,296 |
| 4 | 16,777,180 | 484 | 2,209,920 |
| 5 | 16,776,696 | 9,063 | 23,800,138 |
| 6 | 16,767,633 | 213,058 | 385,499,498 |
| 7 | 16,554,575 | 4,835,970 | 9,893,053,074 |
| 8 | 11,718,605 | 11,687,753 | 195,867,879,154 |
| 9 | 30,852 | 30,852 | 17,101,468,216 |
| all | | 16,777,216 | 223,274,268,884 |

Unresolved: 0. Searching: 343.2 s (6.5·10^8 steps a second, four processes
together); running the 16,777,216 REVs on the reference interpreter
afterwards: 9.5 s; 353 s from start to end.

26 bits (`runs/n26.txt`):

| Budget | Wanted | Found | Steps |
|---:|---:|---:|---:|
| 1 | 33,554,432 | 1 | 13,368 |
| 2 | 33,554,431 | 3 | 56,256 |
| 3 | 33,554,428 | 32 | 289,760 |
| 4 | 33,554,396 | 484 | 2,220,736 |
| 5 | 33,553,912 | 9,074 | 24,123,166 |
| 6 | 33,544,838 | 215,555 | 397,720,926 |
| 7 | 33,329,283 | 5,507,360 | 10,522,352,241 |
| 8 | 27,821,923 | 27,227,996 | 300,854,316,753 |
| 9 | 593,927 | 593,926 | 116,187,867,699 |
| 10 | 1 | 1 | 35,994,188 |
| all | | 33,554,432 | 428,024,955,093 |

Unresolved: 0. Searching: 643.1 s; the 33,554,432 REVs on the reference
interpreter: 18.9 s; 663 s from start to end.

### As the ladder's level

`--base` and `--bound prefix`: the REV the Bomb below stores for the first
X − 1 bits settles one of the two strings that begin with them, and the
other is wanted from what its first bits need. That is how the owner's
levels 25 and 26 were built, before the suffix rule existed. The 25-bit
level stands on the 24-bit Bomb of the cloud ladder; the 26-bit level on
the 25-bit table the experiment had just written, whose checksum is the
certified one.

Left: the shared search. Right: the same work counted as the ladder does
it, one search for each string and budget, with the steps the LevelSolver
takes for it.

25 bits (`runs/c25.txt`):

| Budget | Wanted | Found | Steps | | Searches of the ladder | their steps where a REV was found | where none |
|---:|---:|---:|---:|---|---:|---:|---:|
| 1 | 1 | 0 | 114 | | 1 | 0 | 114 |
| 2 | 4 | 0 | 1,082 | | 4 | 0 | 1,058 |
| 3 | 36 | 0 | 31,424 | | 36 | 0 | 30,884 |
| 4 | 520 | 0 | 1,510,145 | | 520 | 0 | 2,497,400 |
| 5 | 9,561 | 22 | 23,777,710 | | 9,561 | 292,037 | 409,817,150 |
| 6 | 218,790 | 3,807 | 385,133,982 | | 218,790 | 824,990,818 | 104,804,532,640 |
| 7 | 4,120,687 | 930,266 | 9,838,657,059 | | 4,120,687 | 2,661,410,755,687 | 20,104,285,189,823 |
| 8 | 7,453,570 | 7,424,604 | 195,822,543,524 | | 7,453,570 | 38,471,201,998,633 | 1,709,098,647,695 |
| 9 | 29,909 | 29,909 | 17,101,468,216 | | 29,909 | 88,888,393,595 | 0 |
| all | | 8,388,608 | 223,173,123,256 | | 11,833,078 | | 63,140,927,147,534 in all |

8,388,608 strings found by search, 8,388,608 settled by the REV of the
base, 0 unresolved. Setting up (reading the base and running its REVs):
15.9 s; searching: 364.5 s; 409 s from start to end. One step of the shared
search does the work of 283 steps of the ladder.

26 bits (`runs/c26.txt`):

| Budget | Wanted | Found | Steps | | Searches of the ladder | their steps where a REV was found | where none |
|---:|---:|---:|---:|---|---:|---:|---:|
| 1 | 1 | 0 | 118 | | 1 | 0 | 118 |
| 2 | 4 | 0 | 1,114 | | 4 | 0 | 1,090 |
| 3 | 36 | 0 | 31,888 | | 36 | 0 | 31,348 |
| 4 | 520 | 0 | 1,520,961 | | 520 | 0 | 2,508,216 |
| 5 | 9,583 | 11 | 24,101,106 | | 9,583 | 177,997 | 411,668,746 |
| 6 | 222,630 | 2,497 | 397,489,206 | | 222,630 | 559,361,100 | 107,564,272,738 |
| 7 | 5,056,103 | 671,390 | 10,465,169,815 | | 5,056,103 | 2,024,002,994,606 | 28,133,484,634,390 |
| 8 | 16,072,466 | 15,540,243 | 300,716,254,023 | | 16,072,466 | 187,419,528,945,761 | 38,185,363,168,617 |
| 9 | 563,075 | 563,074 | 116,187,867,699 | | 563,075 | 2,100,075,538,954 | 350,378,576 |
| 10 | 1 | 1 | 35,994,188 | | 1 | 35,949,871 | 0 |
| all | | 16,777,216 | 427,828,430,118 | | 21,924,419 | | 257,971,379,632,128 in all |

16,777,216 found by search, 16,777,216 settled by the base, 0 unresolved.
Setting up: 36.2 s; searching: 675.5 s; 766 s from start to end. One step
does the work of 603.

(The counting costs time: the same level without it searched in 341 s at
25 bits.)

### Histograms

Both strings of a pair counted. From nothing and as a level, the same:

| Nodes | 25 bits | 26 bits |
|---:|---:|---:|
| 1 | 2 | 2 |
| 2 | 6 | 6 |
| 3 | 64 | 64 |
| 4 | 968 | 968 |
| 5 | 18,126 | 18,148 |
| 6 | 426,116 | 431,110 |
| 7 | 9,671,940 | 11,014,720 |
| 8 | 23,375,506 | 54,455,992 |
| 9 | 61,704 | 1,187,852 |
| 10 | | 2 |
| all | 33,554,432 | 67,108,864 |

### Memory

The largest process of the 26-bit level had 2.8 GB resident: 1.2 GB are the
experiment's own tables (two bytes a string, and 36 for its REV, kept so
that every REV can be run afterwards), the rest is the 25-bit Bomb below,
read in full. From nothing, 356 MB at 26 bits. None of this is what a
construction would need (`PROPOSAL.md`, section 7).

## 2. Against the certified results

**Histograms.** `--expect` was given the histogram of the owner's reports of
levels 25 and 26 and fails a run on any entry that differs. All four runs
passed: every entry agrees, the 61,704 of 9 nodes at 25 bits, and the
54,455,992, the 1,187,852 and the 2 at 26.

**Tables.** The owner's reports give the table checksum of the certified
Bombs: `7dfdbcd7` for 25 bits and `8fc90cf7` for 26. The tables the
experiment wrote as levels have exactly those (`--expect-table-crc`). The
checksum is over the table as stored: the status, the node count and the
packed REV of all 2^X entries.

**Journals, without the journals.** The owner's reports also give, for each
level, how many searches its journal holds and their logical steps. The
shared search, counting each string's own steps (the theorem of
`PROPOSAL.md`, section 1), arrives at both numbers for both levels:

| Level | Searches | Logical steps | |
|---:|---:|---:|---|
| 25, certified report | 11,833,078 | 63,140,927,147,534 | |
| 25, counted here | 11,833,078 | 63,140,927,147,534 | equal |
| 26, certified report | 21,924,419 | 257,971,379,632,128 | |
| 26, counted here | 21,924,419 | 257,971,379,632,128 | equal |

**Journals, record for record**, where the journals are here: the ladder of
17 to 24 bits built in this container. `--journal` requires every search of
a level's journal to have the same result and the same number of logical
steps in the shared search.

@@JOURNALTABLE@@

## 3. Digests

Three numbers for each Bomb, each stronger than a histogram, none of which
needs a large file to travel.

**The node-count digest**: the SHA-256 of 2^X bytes, byte i being the
minimum number of nodes of the string with index i (the string's bits read
as a binary number, first bit most significant), for i from 0 to 2^X − 1.

| Bits | Node-count digest |
|---:|---|
| 25 | `1c84b6016d8870d69b561256eabb8d67060b199395479d4330b3946aa49e96e6` |
| 26 | `170e5bd475113e1b71c986f328e7d3c059fb1f47ba035cbb5d2ca1e23be2496e` |

Each was obtained four ways that agree: by the search from nothing; by the
search as a level; by `nodes_digest` reading the table the experiment wrote
through the library's reader; and, for 25 bits, by `nodes_digest --bits 25`
on the 26-bit table, which answers shorter strings as well.
@@COMPACT@@

**The file**: the SHA-256 of the fixed-width file `fixed-run-L.frb`. The
experiment's files, which have the certified table checksums:

| Bits | Bytes | SHA-256 of the file |
|---:|---:|---|
| 25 | 1,476,395,072 | `95d708b2d1dcb1ffe4e9f1fdf1a9eb4119a2c636e0476bf477c218de17c76c9e` |
| 26 | 2,952,790,080 | `302eefa4d7ba2abedf61dca4c7b8559c8c29eaa3517ec815ffa65739d77228c2` |

**The record checksum**: one number over every search of a level's journal
(string, budget, result, logical steps), with the number of searches and
their steps (`record_check.h`).

| Level | Searches | Logical steps | Record checksum |
|---:|---:|---:|---|
| 25 | 11,833,078 | 63,140,927,147,534 | `9adfee923c31de0a` |
| 26 | 21,924,419 | 257,971,379,632,128 | `bc94191e6133ba1b` |

### What the owner can run

All three read and write nothing. They are built in a checkout of this
branch, in a build directory of its own, and touch neither the running
construction nor its executable. `journal_digest` takes no lock and is for
the directory of a finished level, never the one being built.

The original files against the experiment's, byte for byte, with nothing
but the system's own tool:

```bash
sha256sum /home/vdcoder/frb31/fixed-run-25.frb /home/vdcoder/frb31/fixed-run-26.frb
```

The node counts of every string, from a Bomb of either format:

```bash
build/nodes_digest /home/vdcoder/frb31/fixed-run-26.frb
```

Every search of a certified journal (for a level built on a base it also
says which bound the level was built with):

```bash
build/journal_digest /home/vdcoder/frb31/work-26
```

@@TIMES@@

If the SHA-256 of a file agrees, the other two follow for that level; they
are there for the case that a file was built another way (the compact
files, a Bomb whose foot of the ladder was another 16-bit table) and for
the journals, which no file digest reaches.

## 4. Witnesses, and what is proven

**Every REV was verified independently of the search**, three times over:

1. *By the experiment, on the reference interpreter.* The REVs are kept as
   recorded and, after the search, each is run on the interpreter of
   `core/rev`. It must be a valid REV of the number of nodes recorded, emit
   its string, visit every one of its nodes while doing so, and its
   complement must emit the complement. 16,777,216 of 16,777,216 at 25
   bits and 33,554,432 of 33,554,432 at 26, in each of the runs.
2. *By the library's writer*, which runs every REV again before it writes
   an entry and refuses a table with one that does not emit its string:
   all 2^25 and 2^26 entries.
3. *By the production verifier*, `carlobomb bomb verify`, on the files
   written:

@@VERIFY@@

**What that proves.** A REV that runs is a proof that its string needs *at
most* that many nodes. For every string of 25 and 26 bits there is such a
proof, checked by three programs that share nothing with the search.

**What it does not prove.** That no string needs *fewer*. That is the claim
that a search which found nothing was complete, and the experiment leaves
no record of its searches that another program could take up: no journal.
So the status of the results is:

| Claim | Status |
|---|---|
| Every string of 25 and 26 bits has a REV of the stated size | proven here, by the REVs |
| Those sizes are the minima | certified by the owner's journals for the Bombs of 25 and 26 bits. Here: **experimental agreement**, in every one of 100,663,296 node counts, by a search of another shape, once on the ladder and once from nothing |
| The experiment's tables are the certified tables | by checksum, 32 bits; by SHA-256 as soon as the owner's `sha256sum` is compared |
| The shared search makes the ladder's searches | shown for every record at 17 to 24 bits; for the totals at 25 and 26 |
| A journal-backed proof of minimality by the shared search | **does not exist.** It is what `PROPOSAL.md` proposes to build |

One more thing the agreement is not: independent in every part. As a
level, the experiment starts from the Bomb below, like the ladder; if that
Bomb were wrong, both would inherit it. The runs from nothing do not have
that dependence, and they give the same node counts.

## 5. Isolation

- Everything ran in the development container, in a scratch directory, from
  a build of the branch `experiment/shared-search`.
- Nothing was read from or written to the owner's machine. The certified
  figures used are those of the two level reports the owner posted.
- The production ladder, the construction code and the formats are not
  changed by this branch: it adds files under `experiments/shared-search`
  and their build rules.
- The 25- and 26-bit tables the experiment wrote are in the scratch
  directory only. Their entries say "certified minimum", which they must in
  order to be comparable byte for byte with the certified files; the
  program says so when it writes one. They are not Bombs of this project,
  are not in the repository and should not be taken for more than copies
  to compare with.

## By the way: the suffix rule at 25 and 26 bits

`benchmarks/SUFFIX_BOUND.md` could not say what the suffix rule saves at 25
and 26 bits, the journals being on the owner's machine. Counting the
ladder's searches gives it:

| Level | Searches of the ladder | below the bound from the last bits | | Their steps | |
|---:|---:|---:|---:|---:|---:|
| 24 | 6,273,837 | 51,118 | 0.8% | 19,368,798,269 | 0.093% |
| 25 | 11,833,078 | 93,659 | 0.8% | 40,605,145,322 | 0.064% |
| 26 | 21,924,419 | 139,269 | 0.6% | 82,380,515,737 | 0.032% |

The line for 24 bits is, to the digit, what `ladder_retro` read from that
level's journal. With every stretch instead of the last bits: 102,292
searches and 44,733,587,220 steps at 25 bits, 151,838 and 89,359,038,840 at
26. The rule stays correct and stays small.

## Reproducing

From nothing:

```bash
build/shared_search --bits 26 --prefix-bits 10 --jobs 4 --expect 1:2,2:6,3:64,4:968,5:18148,6:431110,7:11014720,8:54455992,9:1187852,10:2
```

As the level, with the certified figures to meet:

```bash
build/shared_search --bits 26 --prefix-bits 10 --jobs 4 --base fixed-run-25.frb --bound prefix --expect-totals 21924419:257971379632128 --out table-26.frb --expect-table-crc 8fc90cf7
```

Against a journal:

```bash
build/shared_search --bits 24 --prefix-bits 10 --jobs 4 --base LADDER/fixed-run-23.frb --journal LADDER/work-24
```

A run exits with 0 only if nothing is unresolved, every REV runs, and every
figure it was given to expect is met.
