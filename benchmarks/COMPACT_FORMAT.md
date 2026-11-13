# The compact format: what was built and what it measures

Format version 4 is the compact form of a complete FixedRunBomb, as the
owner and the CTO approved it on 13 November 2026: one REV for a string and
its complement, the graph of a REV stored as one number without its colors,
node counts in 4 bits, and one navigation bit for every shorter string. This
report says how it got there, what the Bombs of 16 to 24 bits measure in
it, what migrating them costs, and what was done to trust it.

The specification is [FORMAT.md, section 12](../bombs/fixed-run/FORMAT.md);
the commands are in the [README](../bombs/fixed-run/README.md#the-compact-format).

Measured on 13 November 2026 in the development container: Intel Xeon at
2.1 GHz under KVM, 4 virtual CPUs, 15.7 GB, GCC 13.3 `-O3`, files in the
page cache. The Bombs are those of the ladder built in that container with
the program of the 24-bit run; their SHA-256 are below, to compare with any
other copy.

## In short

| | 20 bits | 24 bits |
|---|---:|---:|
| Fixed-width file (version 2) | 35,651,648 bytes | 570,425,408 bytes |
| Compact file (version 4) | 2,138,680 bytes, 6.0% | 38,858,512 bytes, 6.8% |
| of which REVs / node counts / counts before / navigation bits | 1,745,008 / 262,144 / 65,536 / 65,536 | 32,435,528 / 4,194,304 / 1,179,648 / 1,048,576 |
| Bits per string of the table | 16.3 | 18.5 |
| Conversion, with every check | 1.8 s | 35 s |
| Open with full validation, fixed-width / compact | 0.41 s / 0.24 s | 7.5 s / 4.9 s |
| Memory of its own while open, fixed-width / compact | 4 MiB / 0 | 64 MiB / 0 |
| Node count of an L-bit string, fixed-width / compact | 9 ns / 5 ns | 22 ns / 9 ns |
| REV of an L-bit string, verified, fixed-width / compact | 381 ns / 674 ns | 505 ns / 1,029 ns |
| REV of a 1..L-bit string, verified, fixed-width / compact | 220 ns / 498 ns | 295 ns / 758 ns |
| Which extension needs fewer nodes, on demand | 47 ns | 78 ns |

- **The nine Bombs of 16 to 24 bits are 75.0 MB where they were 1,137.6
  MB**: 6.6%. One command migrated them in 67 seconds.
- **Nothing was lost.** Every one of the nine expands again to the file it
  was made from, byte for byte. The REVs a compact file returns are the REVs
  of the table, link for link and color for color, although the colors are
  not in it.
- **Every answer was compared.** For each of 16, 20 and 24 bits every string
  of every length from 1 to L was asked of both files: 33,554,430 strings at
  24 bits. Node counts and comparisons are identical. The extension chosen
  for a shorter string is identical where the string starts with 0; where it
  starts with 1 it is, for three in five of them, another of the extensions
  with the fewest nodes, by the rule that was approved (below).
- **A REV costs two to two and a half times a fixed-width lookup**: 0.2 to
  0.5 microseconds more on this machine, for finding the colors that are no
  longer stored. A node count costs about the same or less for a string of L
  bits and 10 to 15 ns more for a shorter one.
- **The comparison of the two extensions of a string is worked out on demand
  in 0.05 to 0.1 microseconds**, from two walks along the navigation bits
  and two node counts, and takes no storage.
- **Lengths beyond 24 bits were run without a Bomb to run them on**: a table
  of 25 bits that is not a Bomb, 1.48 GB in the version-3 format, converted,
  verified and expanded like the others; and lookups at 31 bits in a file
  laid out in empty memory. See "Beyond 24 bits".

## How the format got here

| | 24-bit file | of fixed-width |
|---|---:|---:|
| Fixed-width, version 2 | 570,425,408 | 100% |
| The first design (issue 6, built as the draft of this pull request) | 217,653,502 | 38.2% |
| The format as approved and built | 38,858,512 | 6.8% |

**The first design** kept every REV with its colors and links as bit fields,
sorted into classes by node count, and an index entry of 3 bytes for every
string of 1 to L bits: 117.0 MB of REVs and 100.7 MB of index at 24 bits.
Its review listed four things it left on the table.

**What changed**, and what each change does to the 24-bit file:

- *One REV for a string and its complement.* In every Bomb the ladder builds,
  the REV of a string's complement is the complement of the string's REV.
  Only the strings that start with 0 are stored: half the REVs, half the
  index.
- *The graph without its colors, as one number.* The colors of a
  minimum-node REV follow from its links and the string it has to emit: the
  machine is run on the graph, and a node gets the color the string asks for
  when the machine first emits from it. So only the links are stored, and
  they are stored as the rank of the graph among all graphs of its number
  of nodes numbered in one canonical way: 34 bits for a REV of 8 nodes that
  took 56 as fields. The 8,388,608 stored REVs take 32.4 MB, 30.9 bits each.
- *A node count instead of an ordinal.* A string's entry is its node count,
  4 bits. Where its REV is in its class is counted: how many strings before
  it have the same node count, from a count kept for every 256th string. 5.4
  MB where the ordinals of the L-bit strings took 50.3.
- *One navigation bit for a shorter string.* Which of its two extensions
  needs fewer nodes, the extension by 0 if they need the same. Following the
  bits leads to an L-bit string with the fewest nodes. 1.0 MB where the
  ordinals of the shorter strings took 50.3.

The owner decided on 13 November that REVs are stored without colors as the
rule, with no list of exceptions; the CTO decided the same day on one
navigation bit, with the comparison of two extensions worked out on demand
rather than stored in a second bit. The experiments those decisions rest on
(the colorless representation, twelve variants of the layout measured at 16,
20 and 24 bits, the two-bit record) are on the branch
`experiment/colorless-rev` with their own reports; they are not part of this
pull request.

**Why the colors come back as they were.** A graph can have several
colorings that emit the string. The search tries color 0 before color 1
wherever the string leaves a color open, and numbers the nodes in the order
the machine reaches them: the order in which the solver's own search arrives
at its REVs. So the REV that comes out is the one that went in, and for
every Bomb here that is checked: 0 REVs differ in the 33,488,896 strings of
the nine tables. The format does not rest on it: a file counts in its header
the strings whose REV it does not return as the table had it, and a table
with such REVs converts all the same.

## The Bombs of the ladder, migrated

`carlobomb bomb migrate --dir LADDER --output DIR` wrote the compact file of
every complete Bomb in the ladder's directory to a directory of its own:

| L | Fixed-width, bytes | Compact, bytes | | REVs not as in the source | Largest REV | Seconds |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 1,245,248 | 129,680 | 10.4% | 0 | 7 nodes | 0.1 |
| 17 | 4,456,512 | 253,192 | 5.7% | 0 | 8 | 0.2 |
| 18 | 8,912,960 | 508,872 | 5.7% | 0 | 8 | 0.4 |
| 19 | 17,825,856 | 1,042,448 | 5.8% | 0 | 8 | 0.9 |
| 20 | 35,651,648 | 2,138,680 | 6.0% | 0 | 8 | 1.8 |
| 21 | 71,303,232 | 4,386,800 | 6.2% | 0 | 9 | 3.7 |
| 22 | 142,606,400 | 9,000,848 | 6.3% | 0 | 9 | 8.1 |
| 23 | 285,212,736 | 18,673,000 | 6.5% | 0 | 9 | 16.5 |
| 24 | 570,425,408 | 38,858,512 | 6.8% | 0 | 9 | 35.0 |
| | 1,137,640,000 | 74,992,032 | 6.6% | | | 66.7 |

The 16-bit file is version 1 (19 bytes an entry), the others version 2 (34
bytes), which is why the share drops between 16 and 17 bits. It then rises
slowly, because the REVs grow: most strings of 16 bits need 6 nodes, most of
24 bits need 7 or 8. The time doubles with the length and is the time of the
checks: at 24 bits about 7 s to read and validate the source, and of the 28
s after that, 5 s to open the new file as any reader would and 13 s to
compare it with the table string for string, before it gets its name.

The command used 0.2 MiB of memory of its own. Both files are mapped: what
is resident are pages of the files, 583 MiB at 24 bits, which the system
can take back at any time and which are shared with any other process that
has the same file mapped.

Each compact file was then expanded with `bomb expand` and the result
compared with the fixed-width file it came from (`cmp`): **identical, all
nine**. A second migration into another directory gave the same nine files
byte for byte.

SHA-256 of the files. A table is the same bytes wherever it is built with
the same settings (FORMAT.md, section 10), and its compact file the same
bytes wherever it is made, so these can be compared with a migration
anywhere:

| L | Fixed-width | Compact (version 4) |
|---:|---|---|
| 16 | `0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14` | `c29f50ad6e4133ca06625f04e1f7acd6f0c7154c07d970d16637942e48e7f765` |
| 17 | `e6d4cc43d3edc56f88e96037cef81f7f4c0b6e88ecbaef5052339596726eecdb` | `3c4a7ba7eaa6737374e603413b1aa8b2136efc6e643cce9ef9cf98ca0cd0225b` |
| 18 | `e67fb5a7c813d5c6c2d18532e2eb8a12dbf16f5fe2cf10f89ad9c02578a011b1` | `29092d7c6712498ddabe9698d5f8695decb18bb2c58086e115624a899d0b8e9a` |
| 19 | `86e5000f3b81abb57de2d318e5ebad7454e1d6db4395187d10310853682efa25` | `9d5f3318f08b4ac72bd486a7392d4b4400579c1a02da85ccc940092621aa4b0b` |
| 20 | `5f5025bc00a26cad5153364a3bb3528c51d1d9cc9a7db16cf63df36b7393ca14` | `9133fe30de883b410d2b82d1a7c1d2c59681fc12d25b1b98ff0a70e9cf2f2117` |
| 21 | `1581fb018ee718e77fd77311eaba41a7304219b54cba4235c551dcdc920ed8cb` | `d550987671e3b08190d71d260433ee1b2a4fadde39910ca96379f0cda37fb0c7` |
| 22 | `189e6ea0cf71cbcadb1765a017bf0e611027c64684f7b53ce34165d5811dcb98` | `af09dfb44d7896f28b0972abb1a2affff47353c72d60333877c4a11904abf21f` |
| 23 | `61ee0779edc76e83da984d6970607cc3f5e333bdec267ca88209467daf491088` | `25a5152d501616dc682b2d307ecd0711e8b900743015fdec354500e56f6c3e67` |
| 24 | `3f99769699e24d97d7c15a3b305c3d47f1cb0496978ac71ef086c91457ecd0c1` | `72107474e1cd6a4a2843106d11ec2da59ce64869acd17274f042a63744ad7338` |

If a fixed-width file elsewhere has the checksum of the left column, its
migration must give the one on the right. If it has another, it is another
table of the same minima (built with other settings, section 10), and its
compact file is its own; the migration says how many of its REVs it returns
in another form, and that number is in the file.

Where the bytes go, at 24 bits:

    Nodes   Kept as             Bits per REV   Stored REVs            Bytes
        1   colors and links               1             1                8
        2   colors and links               6             3                8
        3   colors and links              15            32               64
        4   colors and links              20           484            1,216
        5   colors and links              35         9,041           39,560
        6   rank of its graph             23       209,251          601,600
        7   rank of its graph             28     3,905,704       13,669,968
        8   rank of its graph             34     4,263,149       18,118,384
        9   rank of its graph             40           943            4,720
    REVs                      32,435,528 bytes (one for each string that starts with 0)
    node counts                4,194,304 bytes (4 bits each)
    counts before              1,179,648 bytes (for every 256th string)
    navigation bits            1,048,576 bytes (1 bit for each shorter string that starts with 0)
    header, directory                456 bytes (with the 8 bytes after the last REV)
    file                      38,858,512 bytes

A stored REV takes 30.9 bits where the fixed-width entry has 34 bytes, and
everything that finds it takes 6.1 bits more.

## The same answers

`compact_bench` opens the fixed-width file and its compact file through
`rev_fixed_bomb_open`, the interface that reads both, and asks every string
of every length of both before it times anything.

| | 16 bits | 20 bits | 24 bits |
|---|---:|---:|---:|
| Strings of 1 to L bits asked of both files | 131,070 | 2,097,150 | 33,554,430 |
| Node counts that differ | 0 | 0 | 0 |
| Comparisons of two extensions that differ | 0 | 0 | 0 |
| Shorter strings that start with 0, another extension | 0 | 0 | 0 |
| Shorter strings that start with 1, another extension | 20,442 | 321,755 | 5,157,425 |
| of those, an extension that does not need the fewest nodes | 0 | 0 | 0 |

**The one difference, and why it is there.** For a string of fewer than L
bits a Bomb answers with the REV of one of its L-bit extensions that need
the fewest nodes. Where several need the same, the FixedSolver on a
fixed-width file takes the lowest. A compact file takes the lowest for a
string that starts with 0, and for a string that starts with 1 the
complement of what it takes for the complement, which is the highest: it
stores one half and derives the other. Both are minimum-node REVs of the
string; they are different REVs when the lowest and the highest differ,
which is so for 5,157,425 of the 8,388,607 shorter strings that start with 1
at 24 bits. This is the choice among equals that the Bomb contract leaves
open, and the CTO's decision of 13 November names it as acceptable. A string
of L bits has one REV in the table, and gets it from either file.

## Opening and looking up

Opening is a full validation either way: every REV is run.

| | Open | Memory of its own | File pages mapped |
|---|---:|---:|---:|
| 16 bits, fixed-width | 0.02 s | 0.1 MiB | 1.3 MiB |
| 16 bits, compact | 0.01 s | 0 | 0.1 MiB |
| 20 bits, fixed-width | 0.41 s | 4.0 MiB | 34 MiB |
| 20 bits, compact | 0.24 s | 0 | 2.0 MiB |
| 24 bits, fixed-width | 7.5 s | 64.0 MiB | 544 MiB |
| 24 bits, compact | 4.9 s | 0 | 37 MiB |

The memory of its own of the fixed-width solver is the prefix index it
builds at every opening, 4 bytes a string. The compact file brings its
navigation bits along, so nothing is built and nothing allocated.

Lookups in the opened Bomb, nanoseconds each: 2,000,000 random strings, the
best of five passes. "Node count" reads no REV. "REV" is
`rev_fixed_bomb_solve`: the REV, run against the target as the caller wrote
it before it is returned, as every answer of the FixedSolver is. "Reader
alone" is `frb_compact_graph`, the same REV without that run.

| | Node count, L bits | Node count, 1..L | REV, L bits | REV, 1..L |
|---|---:|---:|---:|---:|
| 16 bits, fixed-width | 3.8 | 6.2 | 222 | 129 |
| 16 bits, compact | 5.0 | 19.5 | 471 | 346 |
| 16 bits, compact, reader alone | | | 233 | 98 |
| 20 bits, fixed-width | 8.8 | 13.2 | 381 | 220 |
| 20 bits, compact | 4.7 | 23.5 | 674 | 498 |
| 20 bits, compact, reader alone | | | 352 | 180 |
| 24 bits, fixed-width | 22.3 | 28.8 | 505 | 295 |
| 24 bits, compact | 9.1 | 40.3 | 1,029 | 758 |
| 24 bits, compact, reader alone | | | 607 | 338 |

Single lookups, each timed by itself (200,000 of them, the clock's own cost
taken off), nanoseconds at the 50th, 95th and 99th percentile:

| | REV, L bits | REV, 1..L |
|---|---:|---:|
| 16 bits, fixed-width | 223 / 245 / 259 | 162 / 309 / 356 |
| 16 bits, compact | 502 / 637 / 722 | 368 / 677 / 815 |
| 20 bits, fixed-width | 357 / 513 / 618 | 202 / 518 / 676 |
| 20 bits, compact | 655 / 841 / 972 | 370 / 754 / 885 |
| 24 bits, fixed-width | 477 / 692 / 793 | 277 / 664 / 807 |
| 24 bits, compact | 922 / 1,210 / 1,386 | 651 / 1,404 / 1,641 |

What the numbers are made of:

- **A node count of an L-bit string is cheaper from the compact file from
  20 bits on**: one half-byte in 4 MB of node counts at 24 bits, against the
  last byte of a 34-byte entry somewhere in 570 MB. For a shorter string
  the compact file walks its navigation bits first, one bit a step, where
  the fixed-width solver reads one entry of the index it built in memory:
  10 to 15 ns more.
- **The reader's part of a REV of L bits is 0.2 to 0.6 microseconds.** It
  finds the string, counts its place in its class, takes the rank apart into
  links (multiplications by reciprocals worked out once; no division), finds
  the colors by running the machine against the string, and numbers the
  nodes. That is the price of storing neither colors nor numbering, and it
  grows with the number of nodes.
- **Through the solver a compact answer costs another 0.2 to 0.4
  microseconds.** The target is parsed and checked, and the reference
  interpreter runs the REV, about 10 ns a bit: for a compact answer all L
  bits whatever the length asked, to report the L-bit string the REV emits
  as the extension; for a fixed-width answer the bits asked. That is where
  the two formats differ most for shorter strings. Running a compact answer
  for the bits asked, as the fixed-width path does, would take about 0.1
  microseconds off the shorter strings at 24 bits. It is not done here: it
  would change what the solver verifies before it answers, and that is not
  a decision for a format change to take in passing.
- **What a lookup touches.** At 24 bits a node count comes out of 4 MiB of
  node counts, after up to 23 steps in 1 MiB of navigation bits for a
  shorter string; a REV out of 31 MiB of records, by way of 1 MiB of counts.
  The fixed-width solver has 544 MiB of entries and 64 MiB of index.

One answer straight from the file, nothing opened before, as `carlobomb bomb
lookup` and `bomb compare` do it (microseconds, 20,000 lookups):

| | REV, L bits | REV, 1..L | Comparison |
|---|---:|---:|---:|
| 16 bits, fixed-width | 3.5 | not possible: open the Bomb, 0.02 s | not possible |
| 16 bits, compact | 14.1 | 11.7 | 11.0 |
| 20 bits, fixed-width | 3.4 | not possible: open the Bomb, 0.41 s | not possible |
| 20 bits, compact | 13.5 | 14.7 | 12.0 |
| 24 bits, fixed-width | 3.9 | not possible: open the Bomb, 7.5 s | not possible |
| 24 bits, compact | 16.7 | 23.2 | 19.8 |

The compact lookup checks 448 bytes of header and directory, maps the file,
and touches the few pages its string stands on; the 10 to 20 microseconds
are mostly the mapping and its page faults on this virtual machine. A
fixed-width file answers an L-bit string from 64 bytes of header and one
entry, and cannot answer a shorter string or compare two extensions without
its whole table. The command itself takes 0.02 s to start.

## The comparison on demand

For a string S of 0 to L-1 bits: does S0 or S1 need fewer nodes, or neither?
The file does not store the answer. It follows the navigation bits from S0
to an L-bit string and reads that string's node count, does the same from
S1, and compares the two numbers. No REV is read.

| | In a row | Single: 50th / 95th / 99th percentile |
|---|---:|---:|
| 16 bits | 35 ns | 48 / 78 / 91 ns |
| 20 bits | 47 ns | 59 / 95 / 113 ns |
| 24 bits | 78 ns | 115 / 169 / 230 ns |

"In a row" is 2,000,000 random strings of 1 to L-1 bits, best of five
passes; the single lookups are 200,000, each timed by itself. For scale: the
fixed-width solver answers the same question from the index it holds in
memory in 17, 34 and 57 ns, and the two-bit record that the experiment
tried answered it in about 10 ns for 1 MiB more at 24 bits, doubling with
every bit.

Every comparison was checked against the node counts of the fixed-width
Bomb: at 24 bits all 16,777,215 strings of 0 to 23 bits when the file was
written, and those of 1 to 23 bits again in `compact_bench`. `carlobomb
bomb compare --file COMPACT --target S` gives one from the command line.

## The commands, on the 24-bit Bomb

| | Time | Memory of its own, peak | Resident with file pages, peak |
|---|---:|---:|---:|
| `bomb migrate`, the nine Bombs of 16 to 24 bits | 66.7 s | 0.2 MiB | 583 MiB |
| `bomb compact` | 35.1 s | 0.2 MiB | 583 MiB |
| `bomb verify` (compact) | 13.2 s | 0.2 MiB | 39 MiB |
| `bomb verify --against` the source | 32.8 s | 0.2 MiB | 583 MiB |
| `bomb expand` | 47.4 s | 4.2 MiB | 583 MiB |
| `bomb verify` (fixed-width) | 26.2 s | 0.1 MiB | 546 MiB |
| `fixed --target` on the fixed-width file | 7.2 s | 64 MiB | 610 MiB |
| `fixed --target` on the compact file | 4.6 s | 0.2 MiB | 39 MiB |
| `bomb lookup`, `bomb compare` on the compact file | 0.02 s | under 1 MiB | under 1 MiB |

`bomb verify` of a compact file validates it (about 5 s) and then
regenerates every entry of the fixed-width table it names to compare the
table checksum (about 8 s), so that the claim "this is that table" is
checked wherever the file is verified, without the source at hand.

## Beyond 24 bits

The Bombs that exist here end at 24 bits. The owner's ladder has a Bomb of
25 bits, in fixed-width version 3, and is building 26. Nothing in the format
changes at 25, but two things are new for its code: a source in version 3,
and a length that no file here has had. Both were run here before real data
meets them.

**A source in version 3.** The 24-bit table was written again as a
version-3 file (44 bytes an entry, 738,197,568 bytes) and converted. The
compact file is the one made from the version-2 file except for the three
header fields that name the source (its version, its table checksum, and
the header's own checksum). It verified against the version-3 file and
expanded to it byte for byte. Checked against the version-2 file instead,
`bomb verify` passes every answer and says that this is the same table in
another format than the one the file names.

**A table of 25 bits.** No Bomb of 25 bits is to be built here, and none was.
But a complete table of 25 bits can be made from the Bomb of 24 without any
search: a REV for the string *b* followed by the 24 bits of *u* is the REV
of *u* with a new root in front of it, and one more node when the old root
has the wrong color for *b*. Its node counts are not minima, it is not a
Bomb, it certifies nothing, and it was made outside the repository and
removed. As a table it is as large as a Bomb of 25 bits and somewhat harder
on the format: REVs of up to 11 nodes, 4.3 million of the stored ones of 10,
where a Bomb of 25 bits is expected to stop at 9 or 10.

| 25 bits, synthetic | |
|---|---:|
| Fixed-width file (version 3) | 1,476,395,072 bytes |
| Compact file | 97,018,264 bytes, 6.6% |
| `bomb compact` | 78.7 s, 0.2 MiB of memory of its own |
| `bomb verify --against` the source | 77.8 s, all pass, 0 REVs not as in the source |
| `bomb expand`, then `cmp` with the source | 111.3 s, identical |
| Every string of 1 to 25 bits asked of both files (67,108,862) | the same answers |
| Open with full validation, fixed-width / compact | 16.5 s (128 MiB of its own) / 11.9 s (0) |
| REV of a 25-bit string, verified, fixed-width / compact | 563 ns / 1,262 ns |
| Comparison on demand: in a row; single 50th / 95th / 99th | 104 ns; 151 / 266 / 410 ns |

From the growth of the last three levels (a factor of 2.05, 2.07 and 2.08 a
bit), a real compact Bomb of 25 bits will be about 81 MB and one of 26 bits
about 170 MB, where the fixed-width files are 1.48 GB and 2.95 GB. Those two
numbers are estimates. The migration of a 25-bit Bomb should take about as
long as the one above: 80 seconds on a machine like this one.

**31 bits.** By the same growth a compact file passes 2^31 bytes at 30 bits
and 2^32 at 31. There is no table to make such a file from, but a lookup
only ever touches the few bytes its string stands on, so the test suite
lays out a Bomb of 31 bits, 4.7 GB, in memory that nobody has to provide (a
mapping whose pages appear when they are written), puts three strings into
it, and looks them up: a navigation bit at a place beyond 2^30, a record
whose first bit is beyond bit 2^32 of its class, a class that begins beyond
byte 2^32 of the file. What that does not cover is the writer and the full
validation at those sizes; they use the same arithmetic, in 64 bits
throughout, and have run to 25 bits.

**More than 12 nodes.** The rank of a graph is defined up to 12 nodes (58
bits). The largest minimum so far is 7 nodes at 16 bits, 8 from 17 and 9
from 21 to 24; if it keeps to the 2·sqrt(L) − 1 it has followed, it reaches
11 at 31 bits, the last length the format has. That is an observation and
not a theorem, so the writer checks: a table with a REV of 13 nodes or more
is refused, nothing is written, and the fixed-width file remains the form of
that Bomb.

## What a conversion does and does not touch

- The source file is opened for reading and is never written. The SHA-256
  of every source above is the same after as before.
- `bomb migrate` reads `fixed-run-L.frb` in the directory it is given and
  nothing else there. It writes to another directory, which it refuses to be
  the same one, never replaces a file, and leaves a partial table alone. Run
  again, it does only what is missing.
- No command here opens a work directory, a journal or a lock of a
  construction. `--work-dir` and `--recertify` are refused for a compact
  file rather than reinterpreted.
- A construction does not take a compact file as its base: `bomb build
  --base` and `bomb ladder --from` refuse it and say how to get the
  fixed-width file back. **A ladder needs the fixed-width file of its
  highest complete level for as long as it builds on it.**
- An output name that exists is refused. A failed conversion leaves no file
  under the name asked for, and no temporary file.
- Nothing in this branch changes the fixed-width formats, the journal, the
  solvers or the construction: `git diff` against the branch of the ladder
  is empty for all of them. Converting a finished table while a higher level
  is built on it only reads the table: one core for the seconds above, and
  pages of the file that are the system's cache, not memory of the command.

The minimality of the node counts is not re-proved by a conversion and is
not claimed by a compact file. It is established by the construction of the
fixed-width file and recorded in its journal. What a conversion proves is
that the compact file holds that table.

## Decisions taken while building it

These were not in the approved design and had to be settled to write the
code. Each can be changed before the format is released; after that a change
is a new version number.

| Question | What was done |
|---|---|
| Node counts above 12 | No compact form; the writer refuses (above) |
| A table whose REVs are not in the form the reader arrives at (another numbering, another valid coloring, a pair that is not symmetric) | Accepted. The file returns a REV of the same size that emits the string, and its header counts the strings (offset 56). `expand` then gives a complete Bomb that is not the source, and says so. None of the Bombs here has any |
| A table in which a string and its complement need different node counts, or a REV has a node its string does not need | Refused: it is no table of minima |
| How a reader knows a file is right | CRC-32 of header, directory, index and each class; everything no string stands on must be zero; every REV decoded, colored and run; every navigation bit checked against the node counts under it; every class total |
| Files of the draft layout of this pull request | Recognized and refused with a notice. None was released |
| Where the migration writes | A directory of its own, as `fixed-run-L.v4.frb`. Nothing is added to a directory a ladder may be using |
| The experiments | Stay on `experiment/colorless-rev`; not merged here |

## The version number

Issue 6 reserves "v3" for the compact layout. By the time it was built, the
fixed-width format for 25 to 31 bits had been built as version 3 (Milestone
5), and the owner's ladder has been writing version-3 journals and tables
since. One number means one layout, so the compact format took the next:
**4**. Nothing about versions 1, 2 and 3 changed: not a byte of any file
means something else, and the readers of the fixed-width formats reject a
version-4 file at the version field (tested in both directions).

The number 4 was used for the first design while this pull request was a
draft. No file of it was released. A reader of the final format rejects
such a file at offset 13 of the header, and the commands say what it is and
that it has to be made again from its fixed-width file.

## Reproducing this

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
build/carlobomb bomb migrate --dir LADDER --output LADDER-v4
```

```bash
build/carlobomb bomb verify --file LADDER-v4/fixed-run-24.v4.frb --against LADDER/fixed-run-24.frb
```

```bash
build/carlobomb bomb expand --file LADDER-v4/fixed-run-24.v4.frb --output again.frb && cmp again.frb LADDER/fixed-run-24.frb
```

```bash
build/compact_bench --fixed LADDER/fixed-run-24.frb --compact LADDER-v4/fixed-run-24.v4.frb
```

```bash
build/carlobomb bomb compare --file LADDER-v4/fixed-run-24.v4.frb --target 0110100110
```

`LADDER` is the directory with the `fixed-run-L.frb` files. The migration of
the Bombs of 16 to 24 bits needs 75 MB of disk and about a minute; the
expansion writes the 570 MB file again.

## Tests

| Test | Covers |
|---|---|
| `test_topology`, `test_topology_portable` | The graph of a REV without its colors. The counts of the graphs of 1 to 12 nodes; every list of links of up to 5 nodes enumerated, the canonical ones being the graphs of the ranks in order; 480,000 ranks of 1 to 12 nodes to a graph and back, the ends of the range among them; the canonical form of 36,000 random graphs, the same from any numbering; the search for the colors on 42,000 random graphs and strings against a second implementation and against all colorings; 4,096 REVs of the solver, each back from its canonical graph as it was. With 128-bit integers and without: 1,087,066 checks each way |
| `test_compact`, `test_compact_unmapped` | Record widths, and the layout from the class counts; the worked example of the specification, all 536 bytes, with its lookups, its rank and its search; 29 tables of 1 to 11 bits in the three fixed-width versions converted and back, byte for byte, with every string of every length against a scan of the table: node counts, REVs, extensions, comparisons; rings of 6 to 12 nodes; tables whose REVs are in another form, counted; what the writer refuses; 1,220 single-bit changes; every field of the header and directory wrong by itself with the checksums made right; a wrong node count, navigation bit, count, rank, color and link; each link of a small REV; a graph with a node its string does not need; each of the 2,063 bits no string stands on in the Bombs of 1 to 7 bits; single questions put to damaged files. 443,976 checks, once with mapped files and once without |
| `test_compact_internal` | The compact code compiled into the test. Each of the 1,023 navigation bits of an 11-bit Bomb made wrong in memory: refused by the reader's validation and, independently, by the comparison with the table. A writer told to write another REV, or a wrong one, and a file that reads back as another: each found before a file gets its name, nothing left behind. Two REVs exchanged in memory. Headers for every length to 31 bits and every source version. Records of every width at every place of a byte. The lookups at 31 bits in a sparse mapping. 31,156 checks |
| `test_fixed_bomb_solver` | The 16-bit Bomb in both formats through the solver: the same node count for all 131,070 strings, and the same REV for all but the 20,442 shorter strings that start with 1 and get the complement of their complement's; budgets; longer targets; the fixed-width file back byte for byte |
| `test_fixed_bomb_internal` | A compact Bomb changed in memory after it was opened: no REV for the strings changed, the others as before |
| `bomb_small`, `bomb_16`, `bomb_16full` | The commands end to end: round trips from versions 1, 2 and 3; sizes and SHA-256 of the compact 16- and 17-bit Bombs; lookups and comparisons; a table with other REVs; a compact file that names a false source, and one checked against the same table in another format; a migration, repeated, with a file in its way, with a partial table, into the ladder's own directory; every refusal |

25 tests with GCC and with Clang, 24 under AddressSanitizer and
UndefinedBehaviorSanitizer, 22 without the assembly engine: all pass. Six
threads asking one opened compact Bomb 1.4 million questions at once, under
ThreadSanitizer: no report, and every thread the answers of one.

**Tests of the tests.** 112 faults were put into the new code one at a
time, in the rank and the search for the colors, the layout, the reader and
its validation, the writer, the comparison with the table, the expansion,
the solver and the commands: a rank taken apart with the wrong column, the
colors tried in the other order, a navigation bit followed the other way, a
checksum not compared, a tie written as a 1, the complement without its
links exchanged, a file replaced, and so on. The tests as first written
found 97 and let 15 through. Thirteen of those were real gaps, and each now
has a test that finds it:

- the link of a small REV on color 1 was never checked on its own;
- every unused bit that exists only in very short Bombs (navigation bits and
  node counts after the last, the bits after the last record of a class);
- the checksum of the directory wrong by itself, with a directory that is
  right;
- a graph that emits its string without one of its nodes, in a file and in a
  table (the reader now names that case apart from a REV that does not emit
  its string);
- a comparison asked of a file with a node count that is none;
- two REVs exchanged in memory after the file was accepted;
- a migration that meets a compact file of the right length made from
  another table;
- records wider than any the format has, which is where a record reaches a
  ninth byte.

The other two are checks that the checks before them imply, so that no file
can reach them: the totals of the classes at the end of the validation, and
that the answer for a shorter string extends it, in the comparison with the
table. They are kept. Against the final tests, 110 of the 112 are found.

One of the faults did more than fail a test: with the count of the graphs
wrong, the writer worked out a file of 13 TB and began to write it. No file
of the format can ask for that, since every size follows from at most 2^30
records of at most 58 bits, but the run of the faults now limits the size
of a file and the memory of a process.
