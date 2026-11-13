# The v4 candidates, as files

The compact Bomb of v4 as real files: the approved design, and the candidate
layouts it was chosen from, each made from a Bomb, checked for every string
and measured.

- [DESIGN.md](DESIGN.md): the approved design, what it stores and what it
  derives, with its validation and its measurements.
- [REPORT.md](REPORT.md): the twelve candidates side by side, as measured
  before the design was decided.

**This is a prototype, not format version 4.** The files it writes (`.cbx`)
have their own magic, are laid out for the machine they are made on, and
exist only to be measured. Nothing here changes a published format, PR 8,
the construction code, a journal or a Bomb: the tools open a complete
fixed-width Bomb for reading and write only into the scratch directory they
are given.

| File | What it is |
|---|---|
| `cbx.h`, `cbx.c` | The prototype container: writer, reader, lookups, and the check of a file against the Bomb it was made from |
| `v4_matrix.c` | Makes each candidate from a Bomb, checks it for every string of every length, and measures it; and counts how often a Bomb picks a next bit |
| `test_cbx.c` | Tests of the container (`ctest -R test_cbx`): 84,762 checks |
| `smoke.cmake` | The twelve candidates on a 13-bit Bomb built for the occasion (`ctest -R v4_smoke`) |
| `results/` | The output of every run the report quotes |

## What a file holds

For a Bomb of L-bit strings:

- **A, the navigation bits.** One bit for every stored string of 1 to L-1
  bits: 0 if an extension with the fewest nodes lies under its extension by
  0, 1 if only under its extension by 1. Followed from a string, the bits
  lead to an L-bit extension of it with the fewest nodes, the lowest of
  them. Nothing else is stored about shorter strings: their node count is
  read where the bits lead, and which of the two extensions of a string
  needs fewer nodes, or neither, is worked out on demand by following both
  and comparing.
- **B, the node counts.** Four bits for every stored string of L bits, and
  for every 256th string the number of earlier strings of each node count.
  The REVs are kept by node count in the order of their strings, so the
  place of a REV is a stored number plus a count within one block. No table
  of positions, no ordinals.
- **The REVs.** One record per stored string. No two strings share a
  record, so nothing has to be looked up through a dictionary.

and three switches:

| Switch | Values | Meaning |
|---|---|---|
| links | `fields`, `base-n`, `rank` | 2N numbers of ⌈log2 N⌉ bits; the 2N links as one number in base N; the rank of the canonical topology among all canonical topologies of N nodes |
| colors | `colors`, `colorless` | N bits with each REV; or none, the colors being found from the links and the string when the REV is asked for |
| strings | `whole`, `half` | every string; or only those that start with 0, a string that starts with 1 being answered by the complement of the answer of its complement |

A candidate is named by its three values. **The approved design is
`rank,colorless,half`**; the others are kept as references to measure
against.

### Uncolored REVs

With `colorless` the file keeps the links of a REV and not its colors. The
reader finds the colors by running the machine on the links against the
string: a node the machine lands on must have the color of the bit it has to
emit, and where a color has to be guessed the reader tries one, goes on, and
comes back if a later bit contradicts it. The REV it returns emits the
string by construction.

Where several colorings of the same links emit the string, the first one
found is returned. No list of such strings is kept and nothing is voted on:
by the owner's decision of 13 November 2026 either coloring is a good
answer, since both have the same nodes and the same links. The reader
guesses the first bit of the string first. That is the order of the search
that built the Bomb (white first, and a string that starts with 1 holds the
complement of its complement's REV), so the first coloring found is the
stored one for every REV that came from the search. REVs of up to 5 nodes
keep their colors in every candidate: many of them came from the enumerator,
whose order the reader does not follow, and they are a thousandth of a Bomb.

The check counts, for every string, whether the REV returned is the stored
one and whether it predicts the same next bit. With the colors kept it
always is. Without them see the report: on the Bombs measured it also is,
for every string.

## Running it

Build as usual; the experiment is part of the default build with GCC or
Clang on 64-bit Linux, WSL included (`-DCARLOBOMB_EXPERIMENTS=OFF` leaves it
out).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

Then one command. `BOMB` is a complete fixed-width Bomb, for example
`~/frb24/fixed-run-24.frb`; `SCRATCH` is an existing directory with room for
the files made, which are all there until the end: a fourteenth of the size
of the Bomb for the approved design, at most 1.7 times for all twelve. Under
WSL take a directory on the Linux side (under your home directory), not
under `/mnt/c`: files there are not mapped and cached the way the
measurement assumes.

```bash
build/v4_matrix run --bomb BOMB --scratch SCRATCH > matrix.txt
```

It measures the fixed-width Bomb itself and the approved design, which it
makes from the Bomb and checks for every string of every length.
`--variants all` takes all twelve combinations, `--variants short` the nine
that were on the shortlist, `--variants "rank,colorless,half;fields,colors,whole"`
any list. To have the compact file of PR 8 in the comparison, make one first
(a new file of your choosing; the Bomb is not touched) and name it:

```bash
build/carlobomb bomb compact --file BOMB --output COMPACT
```

```bash
build/v4_matrix run --bomb BOMB --scratch SCRATCH --compact COMPACT > matrix.txt
```

**Run it when the ladder is not running.** It only reads the Bomb, so no
file is at risk, but it uses up to four cores and the Bomb twice over in
memory, which the ladder would feel; and its times mean little on a machine
that is busy with something else.

What it needs, measured at 24 bits and scaled by the size of the Bomb:

| Bomb | Fixed-width file | Memory at the peak | Time, approved design | Time, all twelve | Scratch, all twelve |
|---|---|---|---|---|---|
| 24 bits | 0.57 GB | about 1.3 GB | FINAL_MINUTES | a quarter of an hour | 1.0 GB |
| 25 bits | 1.5 GB | about 3.3 GB | twice that | about half an hour | 2.5 GB |
| 26 bits | 3.0 GB | about 6.5 GB | four times | about an hour | 5 GB |

The rows for 25 and 26 bits are scaled from the first and not measured.

The Bomb is read into memory once by the program and once more by each
conversion, which runs as a process of its own so that its time and memory
can be read off. The lookups are measured in three rounds (`--rounds R`),
each going through all files in turn, and of every timing the quietest
round is kept: a minute in which the machine was busy with something else
then costs every file one round, and no file its place in the table. The
candidate files are removed at the end (`--keep` leaves them).

`--no-verify` skips the check of every string, `--queries N` sets the number
of lookups per measurement (a million), `--threads 1,2,4` the numbers of
concurrent readers, `--cold N` the number of lookups with the file out of
the page cache (0 leaves them out), `--checkpoint B` the spacing of the
stored counts in B (256); a variant written `rank,colorless,half,c64` has
its own.

How often the Bomb picks a next bit, length by length, from the fixed-width
file alone (seconds):

```bash
build/v4_matrix choices --bomb BOMB
```

A single file, to look at or to keep, and one string asked of it:

```bash
build/v4_matrix convert --bomb BOMB --output FILE.cbx --variant rank,colorless,half
```

```bash
build/v4_matrix ask --file FILE.cbx --target 0101010
```

An existing file is never replaced. `ask` prints the node count of the
string, the extension the navigation bits lead to, the node counts of its
two extensions by one bit and which needs fewer, and the REV with its
verification.

## What the output says

A line as each candidate is made and checked, then a paragraph for each,
then the matrix: six tables with a row for each file. For each candidate:

- **size**, and the share of the fixed-width file;
- **conversion**: wall time from the fixed-width file, of which the full
  validation of the source and the building and writing of the file; peak
  memory of the converting process, with the source it holds, and the part
  that is the candidate's own;
- **every string**: of all 2^L strings, how many get a wrong node count,
  how many get no REV that emits them, how many get a REV other than the
  stored one and how many of those predict another next bit; of all shorter
  strings, how many are not answered by an extension with the fewest nodes,
  how many by another such extension than the rule gives (the lowest; in a
  halved file the highest for a string that starts with 1), and how many
  get another answer to "which extension needs fewer nodes, or neither".
  The expected values are taken from the fixed-width table alone;
- **node count** and **comparison**: the two questions answered by the
  index alone, without reading a REV; the comparison is worked out on
  demand from the node counts of the two extensions;
- **REV**: the REV of a string as the reader of the file returns it, its
  colors found if they are not stored; and **verified REV**: that REV then
  run against the string by code outside the reader, the same for every
  file. One lookup at a time, each timed by itself: mean, median, 95th and
  99th percentile; for strings of L bits and for strings of 1 to L-1 bits;
- **readers**: verified REVs with 1, 2 and 4 threads on one mapping of the
  file: lookups per second in all, and the percentiles of a single lookup;
- **out of the cache**: lookups after the file has been dropped from the
  page cache, with nothing in memory and with the index read in.

The last line is `PASS` if no candidate gave anything the Bomb does not
give. A REV other than the stored one is not a failure for a `colorless`
candidate; it is counted and shown.

The counts are the same on every machine for the same Bomb: compare the
table CRC-32 in the first line (`dfd98e11` for the 24-bit Bomb). The times
are of the machine they were taken on.

## How the times are taken

A timed loop on an inlined lookup is easy for a compiler to remove or to
move out from between the two readings of the clock. Here the answers of
every timed loop are added up and the sums compared between the files (they
must agree, and the program fails if they do not), and a single timed
question is hidden from the compiler until the clock has been read. An
earlier version of the program lacked this and reported 0.3 ns for a node
count, which is the time of doing nothing.
