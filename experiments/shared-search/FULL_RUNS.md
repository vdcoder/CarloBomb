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
other is wanted from what its first bits need. That is how the ladder
builds a level, the owner's 25 and 26 among them. The 25-bit level
stands on the 24-bit Bomb of the cloud ladder; the 26-bit level on
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

The counting costs time: without it, and with every stretch of a string as
its bound, the 25-bit level searched in 341 s and wrote the same file
(`runs/b25-every-stretch.txt`). The 26-bit level was also run in 32 classes
instead of 512 (`runs/p26-6.txt`): 410,479,869,979 steps, and the same
21,924,419 searches with the same steps and the same record checksum. How
the work is cut changes the cost and nothing else (`PROPOSAL.md`, section
4).

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
levels 25 and 26 and fails a run on any entry that differs. Every run
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

| Level | Searches in the journal | Their logical steps | With the same result and the same steps in the shared search | Table written | Record checksum, both |
|---:|---:|---:|---:|---|---|
| 17 | 44,810 | 4,778,891,950 | 44,810 | the ladder's file | `cfe3ca6b14751c64` |
| 18 | 96,250 | 16,902,096,386 | 96,250 | the ladder's file | `4ea7faef84ba8fa9` |
| 19 | 199,403 | 47,431,541,189 | 199,403 | the ladder's file | `fd24fd5780e06b84` |
| 20 | 378,045 | 142,813,952,259 | 378,045 | the ladder's file | `e29bf41eabad617f` |
| 21 | 699,890 | 568,481,317,406 | 699,890 | the ladder's file | `b2693d7177dfcf46` |
| 22 | 1,408,471 | 2,356,037,716,175 | 1,408,471 | the ladder's file | `e73d6c02528dd0d9` |
| 23 | 3,052,702 | 7,720,456,914,039 | 3,052,702 | the ladder's file | `8ae472a0d5f8feb9` |
| 24 | 6,273,837 | 20,867,501,480,268 | 6,273,837 | the ladder's file | `53103f47cac5e204` |
| 17 to 24 | 12,153,408 | 31,724,403,909,672 | 12,153,408 | | |

And on the ladder of 17 to 21 bits that was built with the suffix rule of
pull request #10, which makes fewer searches. These five runs were made
with a build that had that pull request's reader and began each string
where that ladder began it; `main` by itself refuses a work directory made
with the rule, as it should.

| Level | Searches in the journal | Their logical steps | With the same result and the same steps | Table written |
|---:|---:|---:|---:|---|
| 17 | 44,292 | 4,773,128,585 | 44,292 | the ladder's file |
| 18 | 95,227 | 16,877,923,030 | 95,227 | the ladder's file |
| 19 | 197,184 | 47,359,451,842 | 197,184 | the ladder's file |
| 20 | 373,907 | 142,659,443,316 | 373,907 | the ladder's file |
| 21 | 693,662 | 568,152,225,420 | 693,662 | the ladder's file |

No search of any of the thirteen journals has another result or another
number of steps, none is missing, and the shared search makes none that the
journal does not have. "The ladder's file" is `cmp`: the table the
experiment wrote and the ladder's `fixed-run-L.frb` are the same bytes.

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

For the shorter Bombs, from the ladder built in this container
(`runs/digests.txt`); a Bomb also answers every shorter length
(`nodes_digest BOMB --bits L`), so the 26-bit Bomb alone can be asked for
all of these:

| Bits | Node-count digest |
|---:|---|
| 16 | `cc777ba8d8c38146430078c452747cc0e7d5778190c5d517c5e30f1783910ef0` |
| 17 | `dbf2d4c513a636c86fe19154af5e69e66be3f6e291ad43916336c5f4681039f6` |
| 18 | `be95c4cd534f9972ff2a2d5fff1e8fed5e6fa56443276c77f2d43742431e32ac` |
| 19 | `6506e865206d9debe51aa33a72f9249d78744b0a26f18d015f20fdc50f5565a7` |
| 20 | `8359b7d4f21702dbd57c59d270bbaf98db39887eeae931f075334db12a2b5cee` |
| 21 | `06c0019a9ad13f233fb79d3da070f7f0f0abd489e0eb1172ff45e1106a13b684` |
| 22 | `318611e761cc5450008dcdf86a021eb8207271c161ae9f8be1cf111401044954` |
| 23 | `1adab1801ff31793c66e7c41dacc1ee778417ed9e1ce1bf162b33a60ca870557` |
| 24 | `8263472edc2707da5dc156a1ff066d67c3e5f28c3fefcda419e064fa079a7599` |

Each was obtained four ways that agree: by the search from nothing; by the
search as a level; by `nodes_digest` reading the table the experiment wrote
through the library's reader; and, for 25 bits, by `nodes_digest --bits 25`
on the 26-bit table, which answers shorter strings as well.
`nodes_digest` gives the same from the compact files made of the two
tables, where a node count is not stored as in the fixed-width file but
read from 4-bit fields and, for strings that start with 1, taken from the
complement.

**The file**: the SHA-256 of the Bomb's file. Of the experiment's
fixed-width files, which have the certified table checksums, and of the
compact files `carlobomb bomb compact` makes of them (a compact file is
determined by its source: the 24-bit one made today is, byte for byte, the
one made by the migration):

| File | Bytes | SHA-256 |
|---|---:|---|
| `fixed-run-25.frb` | 1,476,395,072 | `95d708b2d1dcb1ffe4e9f1fdf1a9eb4119a2c636e0476bf477c218de17c76c9e` |
| `fixed-run-26.frb` | 2,952,790,080 | `302eefa4d7ba2abedf61dca4c7b8559c8c29eaa3517ec815ffa65739d77228c2` |
| `fixed-run-25.v4.frb` | 80,252,120 | `eb4d46c79194d02a855e8e61303999beb4edad968d8c295abb5d5be8ce4bbef9` |
| `fixed-run-26.v4.frb` | 164,839,968 | `a269cf909135658f1fcab6243364405d6c622745aa5fdd1b7d313bb2d7b4c943` |

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

The paths are those of `bombs/fixed-run/BEYOND_24.md`.

The original files against the experiment's, byte for byte, with nothing
but the system's own tool, on the fixed-width files or on the compact ones,
wherever they are kept:

```bash
sha256sum ~/frb31/fixed-run-25.frb ~/frb31/fixed-run-26.frb
```

The node counts of every string, from a Bomb of either format:

```bash
build/nodes_digest ~/frb31/fixed-run-26.frb
```

Every search of a certified journal (for a level built on a base it also
says which bound the level was built with):

```bash
build/journal_digest ~/frb31/work-26
```

Here, `nodes_digest` took 36 seconds on the 26-bit fixed-width file and 23
on its compact file (17 and 11 at 25 bits); `journal_digest` took 1.4
seconds on the journal of level 24, half a gigabyte.

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

   | Check of `bomb verify` | 25 bits | 26 bits |
   |---|---|---|
   | header, length, checksums, counts and flags | pass | pass |
   | every entry decoded and its REV run | pass, 33,554,432 REVs | pass, 67,108,864 REVs |
   | complement symmetry of REVs and node counts | pass | pass |
   | against the Bomb one bit shorter: the minimum of every shorter string is the smallest among its extensions, and no stretch of a string needs over one node more than the string | pass, against the 24-bit Bomb of the cloud ladder | pass, against the 25-bit table |
   | strings drawn at random and solved again from one node up by the production solver, with no use of the table, a journal or the symmetry (`--recertify --sample`) | 2,000 drawn, 2,000 reproduced (1.8·10^10 steps of the production solver) | 2,000 drawn, 2,000 reproduced (3.0·10^10 steps) |
   | the strings of the largest minimum solved again (`--recertify --hardest`) | 200 of the 61,704 of 9 nodes, all reproduced | both strings of 10 nodes, reproduced |

   The verifier ends with "COMPLETE: every target has a certified minimum".
   That line repeats what the file says of its entries, which is what the
   experiment made it say (section 5); it is not a finding of the verifier.
   Its findings are the lines above.

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
| Those sizes are the minima | certified by the owner's journals for the Bombs of 25 and 26 bits. Here: **experimental agreement**, in every one of 100,663,296 node counts, by a search of another shape, once on the ladder and once from nothing. For the 4,202 strings that `bomb verify` drew or picked, also recomputed from one node up by the production solver |
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
  changed by this work: it adds files under `experiments/shared-search`,
  their build rules, and paragraphs to two reports. No line of the library
  is touched.
- The 25- and 26-bit tables the experiment wrote are in the scratch
  directory only. Their entries say "certified minimum", which they must in
  order to be comparable byte for byte with the certified files; the
  program says so when it writes one. They are not Bombs of this project,
  are not in the repository and should not be taken for more than copies
  to compare with.

## By the way: the suffix rule at 25 and 26 bits

The report of the suffix rule (pull request #10) could not say what the
rule saves at 25 and 26 bits, the journals being on the owner's machine.
Counting the ladder's searches gives it:

| Level | Searches of the ladder | below the bound from the last bits | | Their steps | |
|---:|---:|---:|---:|---:|---:|
| 24 | 6,273,837 | 51,118 | 0.8% | 19,368,798,269 | 0.093% |
| 25 | 11,833,078 | 93,659 | 0.8% | 40,605,145,322 | 0.064% |
| 26 | 21,924,419 | 139,269 | 0.6% | 82,380,515,737 | 0.032% |

The line for 24 bits is, to the digit, what that report read from the
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
