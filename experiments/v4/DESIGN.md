# FixedRunBomb v4: the approved design

**Status, 13 November 2026.** This is the design of the compact Bomb as
approved: by the owner (uncolored REVs as the default, no list of
exceptions, every string a record of its own) and by the CTO (nothing in the
construction path; canonical ranks, complement symmetry and the class and
rank index; one navigation bit for every stored shorter string; optional
answers derived on demand).

It is implemented as a prototype (`cbx.h`, `cbx.c`; the variant
`rank,colorless,half`) and validated for every string of the Bombs of 16, 20
and 24 bits. It is **not yet format version 4**: there is no byte-by-byte
specification, no reader or writer in the library and no command in the
tool; no Bomb has been converted for keeping, and PR 8 is as it was. That
is the next step and waits for a decision of its own (last section).

The principle, in the CTO's words: *the Bomb should store essential
information and derive optional answers efficiently; discovery speed
remains non-negotiable.*

## What a v4 Bomb stores

A v4 Bomb is made from a complete fixed-width Bomb of L-bit strings, after
the construction, by reading it. Of every string and its complement it
stores the one that starts with 0. For those 2^(L-1) strings:

| Part | Size | What it is |
|---|---|---|
| Navigation bits | 1 bit for every stored string of 1 to L-1 bits | 0: an extension of the string with the fewest nodes lies under its extension by 0. 1: only under its extension by 1 |
| Node counts | 4 bits for every stored string of L bits | its certified minimum |
| Counts before | one number for each node count, every 256 strings | how many earlier strings have that node count |
| REVs | one record for every stored string of L bits, kept by node count in the order of the strings | 6 or more nodes: the rank of its graph among all canonical graphs of that many nodes, in 23, 28, 34, 40 bits for 6, 7, 8, 9 nodes. Up to 5 nodes: links and colors as they are |

It does not store: the colors of a REV of 6 or more nodes; anything for a
string that starts with 1; anything about a shorter string beyond its one
bit; the place of a REV; one graph for several strings; a list of
exceptions.

## What it derives

| Asked for | How it is answered |
|---|---|
| The node count of an L-bit string | Its 4 bits. |
| The node count of a shorter string | Follow the navigation bits from the string to an L-bit string; that one's 4 bits. |
| The REV of an L-bit string | Its place: the count before its block plus the strings of its node count before it within the block. Its graph: from the rank. Its numbering and colors: by running the machine on the graph against the string; where a color is open the first bit of the string is tried first. |
| The REV of a shorter string | The REV of the L-bit string the navigation bits lead to. |
| Which of the two extensions of a string needs fewer nodes, or neither | From each of the two extensions follow the navigation bits to an L-bit string, read the two node counts, compare. |
| Anything about a string that starts with 1 | Ask its complement and complement the answer: the same node count, the complement of the extension, the REV with its colors inverted and its ports exchanged. |

Following the bits works because the fewest nodes of a string are the
fewest among its L-bit extensions: a REV that emits the string goes on and
emits one of them.

## What a caller may rely on

1. **Node counts are exact**: the certified minimum of the Bomb the file
   was made from, for every string of 1 to L bits.
2. **A REV emits its string with the minimum number of nodes.** It has the
   links of the REV in the Bomb. Its colors are found, not stored; where
   several colorings of those links emit the string, the first one found is
   returned, and no list says which strings those are. On every Bomb checked
   the REV returned is the stored one, colors included.
3. **A shorter string is answered by one of its L-bit extensions with the
   fewest nodes.** Which one is fixed: the lowest of them for a string that
   starts with 0, the highest for a string that starts with 1. The compact
   file of PR 8 takes the lowest everywhere, so for some strings that start
   with 1 the two differ; always between extensions that need the same
   number of nodes.
4. **Complement in, complement out**, for node counts, extensions and REVs.
5. **The comparison is exact** for strings of 0 to L-1 bits: 0, 1, or
   neither. For a string of L bits it takes the Bomb of L+1.

## Sizes

| Bomb | Fixed-width | Compact file of PR 8 | v4 | Of the fixed-width file | Of PR 8's | Bits a string |
|---|---:|---:|---:|---:|---:|---:|
| 16 bits | 1,245,248 | 640,084 | 129,784 | 10.4% | 20.3% | 15.8 |
| 20 bits | 35,651,648 | 13,278,592 | 2,138,784 | 6.0% | 16.1% | 16.3 |
| 24 bits | 570,425,408 | 217,653,502 | 38,858,624 | 6.8% | 17.9% | 18.5 |

The 24-bit file, part by part:

| Part | Bytes | Share |
|---|---:|---:|
| Navigation bits | 1,048,576 | 2.7% |
| Node counts | 4,194,304 | 10.8% |
| Counts before | 1,179,688 | 3.0% |
| REVs | 32,435,528 | 83.5% |
| Header and padding | 528 | |

One navigation bit instead of the two bits of the earlier prototype saves
2^(L-4) bytes: 1 MiB at 24 bits, 2 MiB at 25, 4 MiB at 26.

## Validation

Every string of every length, against the fixed-width Bomb the file was made
from; the expected answers are worked out from that table alone
(`results/final-16.txt`, `final-20.txt`, `final-24.txt`).

| | 16 bits | 20 bits | 24 bits |
|---|---:|---:|---:|
| Strings of L bits | 65,536 | 1,048,576 | 16,777,216 |
| with a wrong node count | 0 | 0 | 0 |
| without a REV that emits them | 0 | 0 | 0 |
| with a REV other than the stored one | 0 | 0 | 0 |
| Shorter strings | 65,534 | 1,048,574 | 16,777,214 |
| not answered by an extension with the fewest nodes | 0 | 0 | 0 |
| answered by another such extension than rule 3 gives | 0 | 0 | 0 |
| with a wrong comparison (the empty string included) | 0 | 0 | 0 |
| For information: answered by another extension than the lowest | 20,442 | 321,755 | 5,157,425 |

The last row is the visible effect of rule 3: about three in ten shorter
strings, six in ten of those that start with 1, get another of their equally
small extensions than the compact file of PR 8 gives them. Their node
counts are the same and so is every comparison.

Beyond that:

- All twelve combinations of the prototype were made again with one
  navigation bit and checked the same way at 16, 20 and 24 bits: no error in
  any (`results/recheck-all-twelve.txt`).
- `test_cbx`, 170,163 checks: tables of 2 to 10 bits, where every answer is
  compared with a scan of the table: the extension the bits lead to (lowest,
  and highest in the mirror), complement in and complement out for
  extensions and REVs, the comparison with its two node counts.
- `v4_smoke`: the tool end to end on a 13-bit Bomb, the command that asks
  one string included.
- The whole suite with GCC and Clang, without the assembly, and under the
  address, undefined-behaviour and leak sanitizers; the concurrent readers
  once more under the thread sanitizer.

## What the operations cost

24 bits; nanoseconds; development container (Intel Xeon 2.1 GHz under KVM,
4 virtual CPUs), the quietest of three rounds. Not the owner's machine.

| | Fixed-width file | Compact file of PR 8 | v4 |
|---|---:|---:|---:|
| Node count of a 24-bit string, in a row | 20.6 | 16.1 | 5.2 |
| Node count of a shorter string, in a row | | 16.0 | 40.9 |
| **Comparison on demand**, in a row | | 37.2 | 80.7 |
| one at a time: mean | | 59 | 113 |
| p50 / p95 / p99 | | 38 / 152 / 254 | 109 / 160 / 214 |
| REV of a 24-bit string as the reader returns it: mean | 215 | 327 | 578 |
| p50 / p95 / p99 | 174 / 379 / 479 | 288 / 512 / 612 | 537 / 790 / 940 |
| the same, then run against the string: mean | 393 | 507 | 731 |
| p50 / p95 / p99 | 352 / 562 / 654 | 459 / 682 / 786 | 689 / 933 / 1,081 |
| REV of a shorter string as returned: mean | | 128 | 339 |
| p50 / p95 / p99 | | 54 / 370 / 526 | 170 / 788 / 982 |
| the same, then run against the string: mean | | 218 | 414 |
| p50 / p95 / p99 | | 140 / 524 / 685 | 259 / 890 / 1,090 |
| Verified REVs a second, 1 / 2 / 4 readers | 2.15 / 4.53 / 9.08 M | 1.82 / 3.59 / 7.29 M | 1.20 / 2.31 / 4.71 M |
| One lookup with the REVs out of the page cache, median | | | 35 µs |

The comparison on demand, with the length of the Bomb: mean 53 ns at 16
bits, 69 at 20, 113 at 24 (median 51, 68, 109).

**What the decision costs and gives.** With two stored bits the comparison
took 10 ns (median; 13 to 15 on average); on demand it takes 113. That buys
1 MiB at 24 bits and twice as much with every further bit. PR 8's compact
file answers the same question in 59 ns, from an index of 100.7 MB.

**Ordinary lookups are not slower for it.** The lookup of a 24-bit string
does not touch the navigation bits. Shorter strings came out faster than
with two bits in the earlier run: 339 against 383 ns for a REV as returned,
41 against 79 ns for a node count. The walk reads half as much memory and
has less to decide. (Two runs at two times of the day; the node count is
beyond what the noise explains, the REV is not.)

Nothing was tuned for the comparison. One shortcut would cost no storage and
is not used: where the navigation bit of the string itself is 1, its
extension by 1 needs strictly fewer nodes, and the two walks could be left
out.

## Conversion, and the finder

The 24-bit Bomb is converted in 11.9 seconds, 6.8 of them the full
validation of the source, with 53 MiB of memory beside the source it reads;
two conversions give the same bytes. It happens after a Bomb is complete
and writes one new file.

Nothing in the construction path is changed by any of this. The sources of
the solvers, the core, the journal, the construction and the fixed-width
table are byte for byte those of the branch the ladder runs; the evidence,
with search steps, tables and speed side by side, is in
`results/finder-unchanged.txt`.

## What the prototype has beside the design

The other switches of the prototype (colors kept, links as bit fields or in
base N, every string stored) are references to measure against, compared in
[REPORT.md](REPORT.md). They are not part of the design.

A Bomb whose pairs are not a REV and its exact complement is refused by the
prototype's writer. Every Bomb built so far qualifies, because the
construction makes the second REV of a pair from the first. What a format
should do with one that does not is to be decided with the format.

## Next, when authorized

1. The format, specified byte by byte: byte order, header, checksums, as
   the fixed-width formats and PR 8's are.
2. Reader and writer in the library, in place of the layout PR 8 built,
   with the commands of the tool: `bomb compact`, `bomb lookup`,
   `bomb verify`, and one for the comparison. The writer reads its file
   back in full before it gets its name, and records in the header how many
   REVs came back with other colors than the stored ones; with 0 there, the
   fixed-width file can be made again from it byte for byte.
3. Its tests, of the kind PR 8 has: every byte of a file damaged, faults
   injected into the writer, mutations.
4. Then, and separately: converting the Bombs that exist.

## Trying it

```bash
build/v4_matrix run --bomb BOMB --scratch SCRATCH > final.txt
```

makes the design's file from a Bomb, checks it for every string of every
length and measures it (2 to 3 minutes at 24 bits). One string asked of a
file:

```bash
build/v4_matrix convert --bomb BOMB --output FILE.cbx --variant rank,colorless,half
```

```bash
build/v4_matrix ask --file FILE.cbx --target 0101010
```

More in the [README](README.md).
