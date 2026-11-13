# The v4 candidates, measured

> **Added 13 November 2026, after this report was read.** The CTO decided to
> store one navigation bit for every stored shorter string instead of the
> two bits of section 3, and to work out "which extension needs fewer nodes,
> or neither" on demand from the node counts of the two. The approved design
> with its validation and measurements is in [DESIGN.md](DESIGN.md). What
> the decision changes in this report:
>
> - every halved candidate is 1 MiB smaller at 24 bits than in the tables
>   below, every whole one 2 MiB;
> - the times of section 3 are those of the stored two bits; on demand the
>   comparison takes COMPARE_NS;
> - among equally small extensions of a string that starts with 1, a halved
>   file now answers with the highest, the complement of its complement's
>   answer, where it answered with the lowest.
>
> The rest stands. The tables below are of the two-bit prototype (commit
> `aa075b9`), whose outputs are kept in `results/two-bit/`.

**What was asked (13 November 2026).** The owner: uncolored REVs are a
compression feature of v4 and the default; no list of exceptions, since two
colorings of the same graph are equally good answers; no deduplication of
graphs, so that every string keeps a record of its own and the small index
can address it; the other features of the shortlist with it; and the point
of a Bomb is to pick the clearly smaller of two extensions. The CTO: nothing
may slow the construction of a Bomb; everything compact is made afterwards,
offline, from a Bomb that is only read; a performance matrix before anything
is decided; no migration, no format freeze, PR 8 on hold.

**What was done.** A prototype container with the features as switches, a
program that makes every candidate from a Bomb, checks it for every string
of every length and measures it, and tests for both. Twelve candidates were
made from the Bombs of 16, 20 and 24 bits. Nothing of it is a format, and
outside `experiments/` only the build script was changed
([README.md](README.md)).

Measured on the complete 24-bit Bomb (16,777,216 strings, SHA-256
`3f997696…ecd0c1`, table CRC-32 `dfd98e11`), in the development container:
Intel Xeon 2.1 GHz under KVM, 4 virtual CPUs, GCC 13.3 `-O3`. Every count is
over all strings; every timing is the quietest of three rounds through all
files in turn. **These are not the owner's Intel machine**: the one command
that repeats all of it there is in the README, to be run when the ladder is
idle.

## Findings

1. **7.0% of the fixed-width file.** With uncolored REVs, the rank of the
   canonical graph, and one string of each complement pair, the 24-bit Bomb
   is 39,907,200 bytes: 19 bits a string, against 570.4 MB fixed-width and
   217.7 MB for the compact file of PR 8 (18.3% of that).
2. **Nothing is lost, without a list.** In all six candidates without
   colors, every one of the 16,777,216 strings gets back the REV the Bomb
   stores, link for link and color for color. The same holds at 16 and 20
   bits. The reader guesses colors in the order the search did; section 2.
3. **The question a Bomb is asked takes one read.** "Which of the two
   extensions of this string needs fewer nodes, or neither?" is answered
   from two bits in about 10 ns, without reading a REV or finding a color
   (PR 8: 35 ns). In the halved candidates those two bits cost no more space
   than the one bit they replace.
4. **Uncolored REVs cost one run of the machine in the reader**, about
   270 ns at 24 bits. So does the rank with colors. Together they share that
   run: the smallest candidate hands over a REV in 578 ns, against 232 ns
   for the fastest candidate and 200 ns for the fixed-width file. A REV from
   an uncolored candidate has been seen to emit its string; one from the
   fixed-width file has not, and costs 393 ns once it is run.
5. **Halving costs nothing.** Every halved candidate returns a REV faster
   than its whole twin; the question of finding 3 takes 10 ns instead of 7.
6. **Conversion is offline and quick**: 8 to 16 seconds at 24 bits, 7 of
   them the full validation of the source, with 54 to 135 MiB of memory
   beside the source. The finder is untouched: same sources, same search
   steps, same tables, same speed (section 7).

**Proposal for the review.** The index of this prototype (A and B below),
one record per string, complement halving, and the three switches as fields
of the header rather than as separate formats. Default as the owner
decided: `rank,colorless,half`. The same reader then also reads a file
written with colors, for whoever wants REV lookups at fixed-width speed
(61.3 MB instead of 39.9 MB). Two decisions are open and listed under
"Open questions".

## 1. Sizes

All twelve combinations, 24 bits:

| Candidate | Bytes | Of the fixed-width file | Of the compact file of PR 8 |
|---|---:|---:|---:|
| fixed-width file | 570,425,408 | 100% | |
| compact file of PR 8 | 217,653,502 | 38.2% | 100% |
| fields,colors,whole | 124,769,648 | 21.9% | 57.3% |
| fields,colors,half | 62,385,136 | 10.9% | 28.7% |
| base-n,colors,whole | 122,604,248 | 21.5% | 56.3% |
| base-n,colors,half | 61,302,440 | 10.7% | 28.2% |
| rank,colors,whole | 95,491,056 | 16.7% | 43.9% |
| rank,colors,half | 47,745,832 | 8.4% | 21.9% |
| fields,colorless,whole | 109,092,376 | 19.1% | 50.1% |
| fields,colorless,half | 54,546,496 | 9.6% | 25.1% |
| base-n,colorless,whole | 106,926,968 | 18.7% | 49.1% |
| base-n,colorless,half | 53,463,792 | 9.4% | 24.6% |
| rank,colorless,whole | 79,813,776 | 14.0% | 36.7% |
| **rank,colorless,half** | **39,907,200** | **7.0%** | **18.3%** |

These are the sizes of files on disk. The four the CTO's note lists as
estimates are among them: A+B with bit fields is 124.8 MB (estimated 129.8;
the stored counts are now every 256 strings instead of every 64, and A has
two bits), with rank and colors 95.5 MB (93.4), and their halved forms 62.4
and 47.7 MB (66.0 and 47.8).

What each switch does to the size:

| Switch | Effect at 24 bits |
|---|---|
| whole → half | exactly half, header apart |
| colors → colorless | 7.8 MB off a halved file: 13% of the fields and base-n files, 16% of the rank file |
| fields → base-n | 1.1 MB off a halved file, 2% |
| fields → rank | 14.6 MB off a halved file, 23% with colors and 27% without |

Where the 39.9 MB are:

| Part | Bytes | Share |
|---|---:|---:|
| A: two bits for every string of 1 to 23 bits that starts with 0 | 2,097,152 | 5.3% |
| B: node counts, four bits for every stored string | 4,194,304 | 10.5% |
| B: counts of each node count before every 256th string | 1,179,688 | 3.0% |
| REVs: 8,388,608 records, of 23 to 40 bits but for a thousandth | 32,435,528 | 81.3% |
| header and padding | 528 | |

A record is the rank of the graph: 23, 28, 34 and 40 bits for 6, 7, 8 and 9
nodes, against 36, 42, 48 and 72 as bit fields, plus as many bits as nodes
for the colors where they are kept. REVs of up to 5 nodes (a thousandth of
the Bomb) are bit fields with colors in every candidate.

With the length:

| Bomb | Fixed-width | Compact file of PR 8 | rank,colorless,half | Bits a string |
|---|---:|---:|---:|---:|
| 16 bits | 1,245,248 | 640,084 | 133,880 | 16.3 |
| 20 bits | 35,651,648 | 13,278,592 | 2,204,320 | 16.8 |
| 24 bits | 570,425,408 | 217,653,502 | 39,907,200 | 19.0 |

The bits a string grow with the node counts. If they go on growing as from
20 to 24 bits, the 26-bit Bomb comes to about 170 MB against 2.95 GB
fixed-width. That is an extrapolation; the matrix run on the real Bomb
replaces it.

## 2. What comes back

For each candidate, every string of 24 bits was looked up and the REV
compared with the one in the Bomb; every shorter string was looked up and
its answer compared with what the table says it must be.

| | All twelve candidates, at 16, 20 and 24 bits |
|---|---|
| strings with a wrong node count | 0 |
| strings without a REV that emits them | 0 |
| strings whose REV is not the stored one | 0 |
| of those, REVs that go on with another next bit | 0 |
| shorter strings answered by another extension than PR 8's rule gives | 0 |
| shorter strings with another answer to "which extension needs fewer nodes" | 0 |

For the candidates with colors that is by construction. For those without
it is the finding. The colorless experiment had counted 5,148 strings at 24
bits for which the first coloring found is not the stored one. They came
from the order of that experiment's reader, which tried color 0 first for
every string. The search that builds a Bomb tries white first on the string
of a pair that starts with 0, and gives the other string the complement:
its colors inverted. A reader that tries **the first bit of the string
first** therefore goes the way the search went, and the first coloring it
finds is the one the search found.

The argument, for a REV found by the search for a string that starts with 0:
suppose another coloring of the same links emitted the string and came
earlier in the reader's order. At the first guess where the two differ, the
earlier one is white. The search stood at that same point, with the same
graph so far, and tried white first; a REV of no more nodes lay that way, so
the search would have returned one from there and not the stored REV. The
same holds for a REV taken over from a shorter Bomb, since every coloring
that emits the longer string emits the shorter. REVs that did not come from
the search are the ones the enumerator found, of up to 5 nodes with the
default settings; those keep their colors.

This is a property of how these Bombs were built, checked here on three of
them in full, not a theorem about every table. The matrix makes the check
on whatever Bomb it is given. And where it should fail one day, the reader
still returns a REV with the stored links that emits the string: by the
owner's decision a good answer, counted in the column "not as stored".

One consequence for a later format: a converter that reads its file back
before giving it its name, as PR 8's does, knows this count and can write it
into the header. A file with a count of 0 can be expanded to the fixed-width
file it came from, byte for byte, although it holds no colors.

## 3. The question a Bomb is asked

Of the two extensions of a string by one bit, which needs fewer nodes? A
holds the answer for every string of 0 to L-1 bits in two bits: 0, 1, or 2
for "the same". No REV is read and no color is found for it. Followed from a
string, taking 0 among equals, the same records lead to the L-bit extension
with the fewest nodes and the lowest index, which is the rule of PR 8; so
one structure serves both.

A string and its complement need the same number of nodes, so a halved file
keeps A for the strings that start with 0 only: two bits for half the
strings, the size of one bit for all.

Times in nanoseconds, 24 bits, a million random strings of 1 to 23 bits:

| | In a row | One at a time: mean | p50 | p95 | p99 |
|---|---:|---:|---:|---:|---:|
| compact file of PR 8 (two lookups in its index) | 39.6 | 62 | 35 | 162 | 300 |
| any halved candidate | 3.6 to 4.0 | 13 to 15 | 10 | 36 to 42 | 45 to 57 |
| any whole candidate | 2.2 to 2.4 | 12 to 14 | 6 to 7 | 38 to 43 | 48 to 101 |

How often the answer is not "the same" (`results/choices-24.txt`): of the
16,777,215 strings of 0 to 23 bits, 7,250,240 (43.2%) have one extension
that needs fewer nodes: 95.1% of those by one node, 4.7% by two, 0.2% by
three or more. Among the strings of 23 bits it is 47.4%. These numbers do
not depend on the length of the Bomb they are read from.

The other index question, the minimum node count of a string: 5 ns for a
string of 24 bits (PR 8: 17 ns; the fixed-width file: 20 ns). For a shorter
string the candidates walk down A, 74 to 82 ns; PR 8 has an entry for every
shorter string and answers in 19 ns, at 100.7 MB for that index against
2.1 MB for A in a halved file.

## 4. REVs

One lookup at a time, each timed by itself, a million random strings;
nanoseconds; 24 bits. "As returned" is what the reader of the file does to
hand over a REV. "Verified" adds a run of that REV against the string by
code outside the reader, the same for every file.

| | As returned: mean | p50 | p95 | p99 | Verified: mean | p50 | p95 | p99 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fixed-width file | 200 | 169 | 367 | 448 | 393 | 352 | 559 | 654 |
| compact file of PR 8 | 309 | 274 | 491 | 584 | 491 | 450 | 671 | 770 |
| fields,colors,whole | 252 | 214 | 439 | 562 | 448 | 399 | 630 | 755 |
| fields,colors,half | 232 | 200 | 408 | 518 | 420 | 382 | 568 | 670 |
| base-n,colors,whole | 291 | 253 | 466 | 583 | 481 | 438 | 670 | 795 |
| base-n,colors,half | 241 | 216 | 407 | 507 | 422 | 387 | 593 | 710 |
| rank,colors,whole | 548 | 510 | 744 | 874 | 765 | 722 | 986 | 1,142 |
| rank,colors,half | 502 | 468 | 674 | 792 | 698 | 660 | 883 | 1,013 |
| fields,colorless,whole | 583 | 536 | 824 | 990 | 719 | 676 | 962 | 1,130 |
| fields,colorless,half | 512 | 460 | 726 | 883 | 652 | 608 | 875 | 1,050 |
| base-n,colorless,whole | 542 | 493 | 769 | 932 | 748 | 706 | 997 | 1,166 |
| base-n,colorless,half | 500 | 457 | 710 | 861 | 693 | 643 | 917 | 1,085 |
| rank,colorless,whole | 677 | 630 | 939 | 1,125 | 871 | 816 | 1,108 | 1,281 |
| **rank,colorless,half** | **578** | **538** | **784** | **934** | **766** | **724** | **977** | **1,131** |

Read with this in mind: between the three rounds the mean of a row moved by
up to 15% (`results/two-bit/matrix-24.txt`, table 3, last columns), and single runs
at other times of the day lay as far apart. Differences of less than about
40 ns between two rows mean nothing.

What the rows say:

- **Bit fields or base N with colors, halved: the speed of the fixed-width
  file** (232 and 241 ns against 200; verified 420 and 422 against 393) at
  a ninth of its size, and faster than the compact file of PR 8.
- **One run of the machine inside the reader costs about 270 ns.** A reader
  without colors runs the machine to find them (512 against 232). A reader
  with rank and colors runs it to give the nodes back their numbering (502
  against 232). A reader with rank and without colors does both in the one
  run: 578, only 66 to 76 ns above either. That is why the smallest
  candidate is not the sum of the two costs.
- **What such a reader returns has been run.** The REV of an uncolored
  candidate emits its string, or the reader would not have found colors. To
  know the same of a REV from the fixed-width file takes 393 ns. On that
  footing the smallest candidate is 1.5 times the fixed-width file (578
  against 393); taking every reader at its word it is 2.9 times (578
  against 200); with an outside verification for all, 1.9 times (766
  against 393).
- **Base N buys little here.** It is 1.1 MB smaller than bit fields and, in
  these runs, no faster in a way the noise lets one see.
- **Whole against half:** the halved twin is faster in every pair, by 20 to
  105 ns. It has half the memory to miss in.

Shorter strings (1 to 23 bits, every length as likely), mean and median:

| | As returned | p50 | Verified | p50 |
|---|---:|---:|---:|---:|
| compact file of PR 8 | 120 | 50 | 209 | 134 |
| fields,colors,half | 253 | 213 | 354 | 318 |
| base-n,colors,half | 247 | 202 | 337 | 310 |
| rank,colors,half | 344 | 237 | 459 | 346 |
| rank,colorless,half | 383 | 248 | 485 | 348 |

Here PR 8 is ahead: its index gives the place of a shorter string's REV
directly, where the candidates walk down A first. It pays 100.7 MB for that.

The spacing of the stored counts in B, on `rank,colorless,half`, measured
in the same rounds:

| A count every | File | REV as returned | Verified |
|---:|---:|---:|---:|
| 64 strings | 43,446,144 | 698 | 902 |
| 256 strings | 39,907,200 | 578 | 766 |
| 1,024 strings | 39,022,464 | 618 | 778 |

More counts made the lookup slower, not faster, in each of the three rounds
(the verified mean was 902 to 939 ns at 64 and 766 to 815 at 256). A likely
reason, not established: at 64 the table of counts is 4.7 MB and its row a
cache miss of its own, while the node counts a lookup adds up lie next to
the one it has to read anyway. 256 stays the default.

## 5. Several readers; cold files

Verified REVs of 24-bit strings, all readers on one mapping of the file:

| | 1 reader | 2 | 4 | p50 / p99 of one lookup with 4 readers |
|---|---:|---:|---:|---:|
| fixed-width file | 2.18 M/s | 4.47 | 8.32 | 343 / 621 ns |
| compact file of PR 8 | 1.82 | 3.62 | 7.10 | 448 / 769 |
| fields,colors,half | 2.14 | 4.10 | 8.18 | 373 / 658 |
| base-n,colors,half | 2.01 | 3.89 | 8.08 | 388 / 679 |
| rank,colors,half | 1.35 | 2.65 | 5.02 | 650 / 990 |
| rank,colorless,half | 1.24 | 2.28 | 4.43 | 735 / 1,193 |

Readers share nothing but read-only memory. The throughput grows with their
number on all four processors and the latency of a lookup hardly moves.
More than four readers could not be tried here.

With the file out of the page cache a lookup costs one read of the disk for
each page it touches that is not in memory. On this machine's virtual disk,
medians in microseconds: 65 to 100 for a 24-bit string with nothing in
memory (two to three reads), 24 to 44 once the index is in memory (one
read, the REV), in every candidate alike; the 99th percentile of the latter
45 to 126. The index of a halved candidate is 7.5 MB and was read in 2 to
6 ms. REVs of shorter strings lie on few pages and are soon all in memory
(median 1 to 2 µs).

## 6. Conversion

Offline, from the fixed-width file, which is opened for reading:

| | Seconds | Of which validating the source | Peak memory | Of its own |
|---|---:|---:|---:|---:|
| bit fields or base N, any | 8.3 to 9.2 | 6.8 | 613 to 681 MiB | 67 to 135 MiB |
| rank, halved | 11.8 to 12.2 | 6.8 | 600 to 607 MiB | 54 to 62 MiB |
| rank, whole | 14.2 to 15.8 | 6.8 | 638 to 653 MiB | 92 to 107 MiB |

The validation is the ordinary reader of a Bomb, which runs every REV
against its string. The memory of its own is the file being built plus a
byte a string. Two conversions of the same Bomb give the same bytes. The
check of the finished file for every string of every length took 5 to 11
seconds more.

## 7. The finder

Nothing in the construction path was changed, and nothing of this runs
during a construction (`results/finder-unchanged.txt`):

- The sources of the solvers, the core, the journal, the construction and
  the fixed-width table are identical, byte for byte, to the branch the
  owner's ladder runs (`origin/feature/fixed-run-31`).
- The Bombs of 17 to 20 bits were built again from the 16-bit Bomb with the
  build of this branch: the same numbers of searches and of logical steps
  as recorded (142,813,952,259 steps at level 20), and tables with the same
  SHA-256.
- The build of this branch and a build of the ladder's branch, run in turn
  three times on the ladder from 16 to 19 bits, take the same time: 69 to
  73 seconds of solver time at level 19 for this branch, 72 to 75 for the
  other.

## Open questions

Two choices the numbers do not make by themselves:

| | Option | 24-bit file | REV as returned | For it |
|---|---|---:|---:|---|
| Links, without colors | rank | 39.9 MB | 578 ns | a quarter smaller |
| | bit fields | 54.5 MB | 512 ns | 66 ns faster, and a simpler reader |
| Switches | fields of the header | | | one reader for all; a file with colors stays possible |
| | one combination, fixed | | | less to specify and to test |

My proposal is rank, and the switches as fields of the header. The owner's
default is then what `bomb compact` writes, and a file with colors is one
option away for whoever wants REV lookups at the speed of the fixed-width
file: `base-n,colors,half` or `fields,colors,half` at 61 to 62 MB.

Not proposed: a dictionary of graphs (the colorless experiment found 1.19
strings a graph: it would be larger), a list of exceptions, voting among
colorings, or anything during construction.

## Tests

- `test_cbx`: 84,762 checks. All twelve combinations on tables of 2 to 10
  bits made the way the construction makes them: every string its node
  count and its stored REV, every shorter string its extension and its
  choice against a scan of the table; a table of 12-node REVs for records
  wider than a word; what the writer and the opener refuse (a pair that is
  not a REV and its complement, an incomplete table, an existing file, a
  changed byte anywhere, a short file, another kind of file).
- `v4_smoke`: the matrix end to end on a 13-bit Bomb built for the
  occasion; the Bomb's SHA-256 before and after; no file left behind.
- The whole suite passes with GCC and with Clang, with the assembly turned
  off, and under the address, undefined-behaviour and leak sanitizers.
- Once, outside the suite: the matrix of all twelve candidates on the
  16-bit Bomb under the thread sanitizer, with 1, 2 and 4 readers. No race
  was reported.
- The timings: the answers of every timed loop are added up and must agree
  between all files, or the program fails; see the README.

## Not done

- **The owner's machine and the longer Bombs.** Everything here is from the
  development container and Bombs of up to 24 bits. The 25- and 26-bit
  Bombs have REVs of more nodes and files that no longer fit a cache.
- **A format.** The prototype is not specified byte by byte, has the byte
  order of its machine, and no reader in the library or command in the
  tool. No Bomb was converted for keeping, and PR 8 was not touched.
- **Expansion** back to the fixed-width file, and the read-back with its
  count in the header (section 2).
- **Tuning.** The search for colors and the unranking are the first
  versions that were correct. The stored counts could lie next to the node
  counts they belong to, which would save a cache miss per lookup.
- **More than four readers**, and a disk other than this container's.

## Reproducing

With the prototype as it was when these tables were made (`git checkout
aa075b9`; the tool has since changed to one navigation bit):

```bash
build/v4_matrix run --bomb BOMB --scratch SCRATCH --compact COMPACT --variants all > matrix-24.txt
```

```bash
build/v4_matrix choices --bomb BOMB > choices-24.txt
```

The counts are the same on every machine for the same Bomb; the times are
of the machine they were taken on. Details in the [README](README.md).
