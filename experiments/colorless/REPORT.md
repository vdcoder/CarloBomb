# Colorless REVs: what the experiment found

The question from PR 8: can a compact Bomb store uncolored, canonically
numbered topologies, share them between strings, and find the colors again
from the string at lookup time?

Measured on the complete 24-bit Bomb (16,777,216 strings, SHA-256
`3f997696…ecd0c1`, table CRC-32 `dfd98e11`), with the Bombs of 16 to 23 bits
for the trend. Every count below is over all strings of a Bomb, not a
sample; times are on samples of 100,000 REVs per node count plus the hardest
cases. Development container: Intel Xeon 2.1 GHz under KVM, 4 virtual CPUs,
GCC 13.3 `-O3`. Nothing was converted, migrated or written next to a Bomb:
the tools read a fixed-width file and keep everything else in memory.

The 24-bit Bomb is the one built in the development container. The owner's
`fixed-run-24.frb` is the same Bomb, and these counts hold for it, if its
SHA-256 is the one above.

## Findings

1. **Topologies are not shared.** 14,112,223 different canonical topologies
   for 16,777,216 strings: 1.19 strings per topology, and 90% of the
   topologies serve one string. A dictionary of topologies is 38.6 MB
   *larger* than writing each topology out (103.5 MB against 64.9 MB).
2. **The colors can be found again, almost always in one way.** For
   16,766,920 strings (99.939%) exactly one coloring of the stored topology
   emits the string. 10,296 strings have two to four.
3. **Recoloring costs about one more verification.** 312 ns on average for
   an 8-node REV against 166 ns to verify a colored one; p99 642 ns; the
   worst found among all 16.8 million, 1.9 µs.
4. **Where colorings are several, the future can differ.** For 1,790
   strings some other coloring goes on differently after bit 24, for 626
   already at bit 25. Taking the first coloring found gives the stored REV
   for all but 5,148 strings; listing those (26 KB) makes it exact.
5. **The larger saving is elsewhere.** Writing a topology as its rank among
   all canonical topologies takes 28 and 34 bits for 7 and 8 nodes instead of
   42 and 48; that, the small index (A and B) and complement symmetry bring a
   24-bit Bomb from 217.7 MB as built to 47.8 MB with its colors, and to
   39.9 MB without. Dropping the colors is the smallest of these steps and
   the only one with a noticeable cost at lookup.
6. **Every stored REV can be given back exactly** from the canonical form,
   link for link and color for color: checked for all 16,777,216 strings in
   each candidate layout.

**Recommendation:** adopt the one-bit index (A), the node-count index (B)
and the rank of the canonical topology, keep the colors, and store one
string of each complement pair. Do not build a topology dictionary. Treat
colorless storage as an option for an archival variant, not the default:
it saves another 16% for 1.3 to 1.5 times the lookup time and a list of
exceptions. Details and the alternatives are in "Options" below.

## 1. Topologies

A REV's nodes were renumbered breadth-first from the root, port 0 before
port 1, root kept. Two REVs that differ only in numbering get the same
result, and nothing a REV emits changes (checked on the first 256 bits of
every REV: 0 changed). The method needs every node to be reachable from the
root: **none of the 16,777,216 REVs has an unreachable node**, and none has
a node the machine does not reach within its 24 bits. Links the machine
never follows are 1.12% of all links, and every one of them is 0.

| Nodes | Strings | Topologies | Strings per topology | Used once | Most used | Bits to name one in use | With its share of the dictionary | Rank of any topology |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 18,082 | 11,331 | 1.60 | 71.2% | 16 | 14 | 25.3 | 18 |
| 6 | 418,502 | 262,706 | 1.59 | 73.3% | 26 | 19 | 33.4 | 23 |
| 7 | 7,811,408 | 5,588,261 | 1.40 | 79.9% | 35 | 23 | 43.0 | 28 |
| 8 | 8,526,298 | 8,247,339 | 1.03 | 97.0% | 12 | 23 | 55.9 | 34 |
| 9 | 1,886 | 1,886 | 1.00 | 100% | 1 | 11 | 51.0 | 40 |
| all | 16,777,216 | 14,112,223 | 1.19 | 89.7% | | | | |

A topology in use is cheap to name (23 bits) only because there are few
strings; the dictionary it points into then costs 20 to 33 bits a string
more (the next column is the two together). The last column needs no dictionary: there are 256,182,290 canonical
topologies of 7 nodes and 12,665,445,248 of 8, so 28 and 34 bits name any
of them. The ratio does not move with the length: 1.14 strings per topology
at 16 bits, 1.16 at 20, 1.19 at 24.

Complements are a different matter. The REV of a complement string is the
same REV with colors inverted and ports exchanged, so its topology is the
port-exchanged twin. Every topology's twin is in the Bomb and is used by
exactly as many strings (0 exceptions). That is a factor of two, and it is
exact; it is the "complement symmetry" of the table in section 5.

## 2. Recoloring: how many colorings, and what they predict

The search of the brief: run the machine with all colors unknown; where the
color of fp is unknown, try 0 then 1; the node cp moves to must have the
color the string has at that bit, which sets the color or ends the attempt.
It was run to the end for every string, to find all colorings, and checked
against the brute force over all 2^N colorings: **0 disagreements in
16,777,216 strings.**

| Nodes | Strings | One coloring | Two | Three or four | A coloring differs later | at the next bit | First found is not the stored one |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 6 | 418,502 | 99.9455% | 224 | 4 | 10 | 8 | 114 |
| 7 | 7,811,408 | 99.9613% | 2,980 | 44 | 848 | 362 | 1,512 |
| 8 | 8,526,298 | 99.9175% | 6,942 | 90 | 932 | 256 | 3,516 |
| 9 | 1,886 | 99.8940% | 2 | 0 | 0 | 0 | 1 |
| all | 16,777,216 | 99.9386% | 10,158 | 138 | 1,790 | 626 | 5,148 |

- For 8,506 of the 10,296 strings with several colorings, all of them emit
  the same stream for ever (decided exactly, on the joint state of the two
  machines). For 1,790 they do not, and every such difference shows within
  64 bits; for 626 it is the very next bit. So the counterexample of the
  brief is real in the Bomb, at a rate of 1 string in 9,400, and 1 in
  26,800 for the next bit.
- If the first coloring found is simply taken, 5,148 strings (0.031%) get a
  REV that is not the stored one. It is still a minimum-node REV for the
  string. For 895 of them its stream differs from the stored REV's, for 313
  at the next bit.
- The share of strings with several colorings falls with the length: 0.140%
  at 16 bits, 0.086% at 20, 0.061% at 24.
- No coloring of any stored topology leaves a node unreached. One that did
  would be a smaller REV for the string; that none exists is what the
  certified minima require, seen here from another side.

## 3. What recoloring costs

Machine steps to the first coloring (24 is the least), for every string:

| Nodes | mean | p50 | p95 | p99 | p99.9 | worst | bound 24·2^N | all colorings, mean / worst |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 6 | 30.4 | 29 | 42 | 49 | 60 | 104 | 1,536 | 36.8 / 107 |
| 7 | 32.3 | 31 | 47 | 56 | 69 | 185 | 3,072 | 40.6 / 201 |
| 8 | 32.3 | 31 | 47 | 58 | 75 | 198 | 6,144 | 40.6 / 199 |
| 9 | 36.3 | 33 | 59 | 91 | 177 | 219 | 12,288 | 48.6 / 289 |

Time for one REV, in nanoseconds, the REV already in the processor's cache
(100,000 random strings per node count; all 1,886 of 9 nodes):

| Nodes | | mean | p50 | p95 | p99 | p99.9 | worst |
|---:|---|---:|---:|---:|---:|---:|---:|
| 7 | verify the colored REV | 168 | 168 | 172 | 173 | 173 | 177 |
| | recolor | 310 | 289 | 494 | 601 | 727 | 1,253 |
| | recolor, then verify | 446 | 426 | 629 | 734 | 866 | 1,391 |
| | all colorings | 420 | 406 | 628 | 750 | 921 | 1,501 |
| | brute force, 128 colorings | 1,600 | 1,599 | 2,071 | 2,292 | 2,553 | 3,167 |
| 8 | verify the colored REV | 166 | 166 | 167 | 168 | 171 | 174 |
| | recolor | 312 | 287 | 510 | 642 | 854 | 1,273 |
| | recolor, then verify | 455 | 430 | 652 | 784 | 1,001 | 1,446 |
| | all colorings | 412 | 391 | 636 | 800 | 1,121 | 1,657 |
| | brute force, 256 colorings | 2,503 | 2,492 | 3,277 | 3,671 | 4,126 | 5,593 |
| 9 | verify the colored REV | 161 | 161 | 162 | 163 | 166 | 167 |
| | recolor | 311 | 277 | 533 | 777 | 1,282 | 1,659 |
| | recolor, then verify | 454 | 415 | 691 | 958 | 1,540 | 1,875 |

The 1,000 strings with the most steps of each node count, out of all
16,777,216: recoloring takes 826 ns on average and 1,528 at worst for 7
nodes, 988 and 1,933 for 8, 1,627 at worst for 9.

Recoloring is 1.8 to 1.9 times a verification, and the search is itself a
run against the string, so a reader that trusts it needs no second run; one
that verifies independently pays 2.7 to 2.8 times. (The reference interpreter of
`core/rev`, which the solver uses today, takes 265 ns for the same
verification; the numbers here are with a tighter one, for both sides.)
Between builds of the same code the times for 7 and 8 nodes moved by 3 to
6%, those of the few 9-node REVs by more.

## 4. Getting the stored REV back

Canonical numbering changes a REV's bytes. Whether the original comes back
decides between "the same Bomb, smaller" and "an equivalent Bomb".

- **Numbering.** The LevelSolver numbers nodes in the order the machine
  first reaches them. So the stored numbering is found by running the
  canonical REV on its string: 16,763,488 REVs (99.918%) come back link for
  link, among them every REV of 6 or more nodes. The other 13,728 have 3 to
  5 nodes and were found by the enumerator of the 16-bit table, which
  numbers differently. They are 0.08% of the Bomb; the layouts below keep
  all 19,122 REVs of up to 5 nodes as the compact format has them, 93 KB.
- **Colors.** With colors stored there is nothing to do. Without, the 5,143
  strings of 6 or more nodes whose stored coloring is not the first found
  are listed with the number of their coloring: 26 KB.
- With those two rules, **each candidate layout returned the stored REV for
  all 16,777,216 strings**, link for link and color for color, and the walk
  along the one-bit decisions ended at the extension the ordinal rule of the
  compact format chooses for all 16,777,214 shorter strings.

What does not survive is nothing, then, but it survives by rule and not by
copy: an expansion to the fixed-width file would be byte-identical only as
long as those rules hold for every REV, which a converter has to check REV
by REV, with a class kept as it is for any REV that fails. The evidence for
minimality stays where it is, in the fixed-width files and their journals.

## 5. Sizes

Bits for one REV:

| Nodes | 5 | 6 | 7 | 8 | 9 |
|---|---:|---:|---:|---:|---:|
| Bit fields, color and two links per node (as built) | 40 | 48 | 56 | 56 | 88 |
| Colors as bits, links as one number in base N ("hyper-bit") | 29 | 38 | 47 | 56 | 67 |
| Rank of the canonical topology, and colors | 23 | 29 | 35 | 42 | 49 |
| Rank of the canonical topology | 18 | 23 | 28 | 34 | 40 |

The 24-bit Bomb, in bytes. The fixed-width file is 570,425,408.

| | | Bytes | of fixed-width |
|---|---|---:|---:|
| Index | an ordinal for every string of 1 to 24 bits (as built) | 100,663,290 | |
| | A: one bit for every shorter string | 2,097,152 | |
| | B: node count of every string in 4 bits, a checkpoint every 256 | 10,747,940 | |
| REVs | bit fields (as built) | 116,989,150 | |
| | colors, links in base N | 107,647,867 | |
| | rank and colors | 80,560,047 | |
| | rank, colors by search, 5,143 strings listed | 64,908,484 | |
| | dictionary of topologies, colors by search | 103,461,422 | |
| **V** | as built: ordinals, bit fields | 217,652,440 | 38.2% |
| **W** | A + B, bit fields | 129,834,242 | 22.8% |
| | A + B, base N | 120,492,959 | 21.1% |
| **Y** | A + B, rank and colors | 93,405,139 | 16.4% |
| **X** | A + B, rank, colors by search | 77,753,576 | 13.6% |
| | W, Y, X with one string of each complement pair | 65,965,715 / 47,751,163 / 39,925,382 | 11.6% / 8.4% / 7.0% |

V, W, Y and X were built in memory and are exact to the byte; the base-N and
complement rows are arithmetic on the same counts. With complement symmetry
A stays whole (2 MB), so the choice among equal extensions of a shorter
string is unchanged, and B and the REVs are halved.

Removing only the colors from the bit fields saves 16.8 MB of 117.0 with
records kept to whole bytes, as the brief estimated, and 22.9 MB with
records packed to the bit. The rank saves 36.4 MB with the colors kept and
52.1 without.

## 6. Lookups end to end

Each layout built in memory from the 24-bit Bomb; 2,000,000 random strings;
nanoseconds per lookup, including the memory the lookup has to reach, with
the REV verified against the string asked for.

| | Bytes | 24-bit string | 1 to 24-bit string |
|---|---:|---:|---:|
| F fixed-width, with A for shorter strings | 572.5 MB | 343 | 309 |
| V as built | 217.7 MB | 438 | 191 |
| W A + B, bit fields | 129.8 MB | 497 | 413 |
| Y A + B, rank and colors: the stored REV | 93.4 MB | 639 | 527 |
| Y the REV in canonical numbering | | 572 | 422 |
| X colorless: the stored REV, verified by a second run | 77.8 MB | 967 | 659 |
| X the stored REV, the search as verification | | 812 | 478 |

- **B costs 50 ns** for the ordinal of a 24-bit string (a direct 3-byte
  ordinal: 10 ns), and saves 39.6 MB. A checkpoint every 64 strings: 34 ns
  and 17.8 MB; every 1,024: 107 ns and 9.0 MB.
- **A + B make shorter strings slower**, 413 against 191 ns: an ordinal for a
  short string sits in a small, warm part of the index, while the walk ends
  at a string of 24 bits somewhere in the node counts.
- **The rank costs 110 ns to decode** against 30 to 60 for bit fields: a
  chain of 14 to 16 multiplications, each waiting for the one before.
  Base N decodes in 14 to 19 ns up to 8 nodes, faster than bit fields.
- **Colorless adds 330 ns** to a lookup (967 against 639), or 170 if the
  search is taken as the verification.
- In the 16-bit Bomb, where everything is in the cache, the same lookups
  take 105 (V), 121 (W), 188 (Y) and 327 ns (X).

With the files out of the page cache, a lookup costs what its reads cost:
29 µs median for the one read of a fixed-width entry, 56 µs for the two
reads of the compact file as built (index entry, then REV), 36 µs for one
REV when the index is in memory. A + B at 12.8 MB can be in memory; the
100.7 MB of ordinals is another matter.

## Options

| | 24-bit Bomb | Lookup, 24-bit string | Exact REV back | Notes |
|---|---:|---:|---|---|
| 0. As built (PR 8) | 217.7 MB | 438 ns | by copy | ordinals need 4 bytes from 25 bits |
| 1. A + B + complements, bit fields | 66.0 MB | about 500 ns | by copy | no canonical numbering |
| 2. A + B + complements, rank and colors | 47.8 MB | about 640 ns | by the numbering rule | **recommended** |
| 3. the same without colors | 39.9 MB | about 810 to 970 ns | by rule and a list of 5,143 strings | several colorings exist for 0.06% |
| Topology dictionary | 116.3 MB with A + B | one more memory access | | 38.6 MB more than writing each topology out; not worth building |

Options 1 to 3 use complement symmetry, which was measured exact but not
built as a layout here; its lookup adds one complement of a REV.

Why 2 and not 3: the colors are 15.7 MB of 93.4 (7.8 MB after complements),
and leaving them out makes every lookup a search, gives a worst case of 2 µs
instead of a fixed 170 ns, and adds a list without which 313 strings would
predict another next bit. For a Bomb used as a predictor that list is not
optional. Option 3 is sound and reversible, and is the right choice only
where the last 16% of size matters more than lookup time.

## What to keep in mind

- **A colorless Bomb without the list is a different Bomb** for 5,148
  strings: same minimum, same string, another REV, and for 895 another
  future. If that is ever accepted, it should be a stated mode, not a silent
  one, and its expansion is not byte-identical.
- **The numbering rule is a property of the solver**, verified for every REV
  of 6 or more nodes at 16 to 24 bits. A converter must still check it for
  each REV and keep a fallback.
- **The rank depends on fixed tables** (the counts of canonical topologies).
  A format using it has to state those numbers and a reader to check them.
- **Verification.** Today every answer is run against its string before it
  is returned. Recoloring is such a run; whether it may stand in for the
  independent one is a decision, worth 170 ns.
- The 25-bit Bomb would add little here: the ratios have been flat from 16
  to 24 bits. What changes at 25 bits is the index as built (4-byte
  ordinals), which A + B replace.

## Tests

`test_colorless` (3.8 million checks): the machine against the reference
interpreter; canonical numbering under random renumberings, and the 1, 12,
216 and 5,248 canonical topologies of 1 to 4 nodes by enumeration;
recoloring against brute force on random REVs, including strings no
coloring emits; rank and its inverse for every topology of up to 5 nodes;
the numbering rule on fresh LevelSolver witnesses. `colorless_smoke` runs
the tools on a 13-bit Bomb. The whole suite passes with GCC and Clang (25
tests), under AddressSanitizer and UndefinedBehaviorSanitizer (24) and
without the assembly engine (22).

## Not done

Complement symmetry as a built layout; a multi-word base-N code (not needed
if the largest minimum keeps to ⌈2√L⌉ − 1: 11 nodes at 31 bits, whose links
fit 128 bits; with colors mixed into the same number 9 nodes already need
128 bits and decode in 95 ns, so colors are better kept as bits beside it); per-class bitmaps as an alternative to B (18.9 MB against 8.4 MB of
node counts at 24 bits); the candidate layouts as files read cold. No format
was defined, and PR 8 is untouched.

One question the data raises and does not answer: sharing is low among the
witnesses the search happens to return. A string often has several
minimum-node REVs; whether choosing among them for a common topology would
share more is another experiment, and it would mean a new construction, not
a conversion.

## Reproducing

[README.md](README.md) has the commands; `results/` holds the output of
every run quoted here.
