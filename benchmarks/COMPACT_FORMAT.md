# The compact format: design review and measurements

Issue 6 asks for a smaller form of a complete FixedRunBomb: the REVs sorted
by node count and packed by size class, a class directory, and an index
that answers every string of 1 to L bits. This is the review of that
design, what was built, and what it measures on the real Bombs of 16 to 24
bits.

The specification is [FORMAT.md, section 12](../bombs/fixed-run/FORMAT.md);
the commands are in the [README](../bombs/fixed-run/README.md#the-compact-format).

Measured on 13 November 2026 in the development container: Intel Xeon at
2.1 GHz under KVM, 4 virtual CPUs, 15.7 GB, GCC 13.3 `-O2`, files in the
page cache. The Bombs are the ones of the ladder built in that container
with the program of the 24-bit run; their SHA-256 are below, to compare
with any other copy.

## In short

| | 23 bits | 24 bits |
|---|---:|---:|
| Fixed-width file (version 2) | 285,212,736 bytes | 570,425,408 bytes |
| Compact file (version 4, 3-byte index) | 108,606,260 bytes, 38.1% | TBD24 |
| of which REVs / prefix index | 58,273,556 / 50,331,642 | TBD24 |
| Compact, 4-byte index | 125,383,468 bytes, 44.0% | TBD24 |
| Open with full validation, fixed-width / compact | 3.23 s / 2.68 s | TBD24 |
| Memory of its own while open, fixed-width / compact | 32 MiB / 0 | TBD24 |
| Node count of an L-bit string, fixed-width / compact | 27 ns / 25 ns | TBD24 |
| REV of a 1..L-bit string, verified, fixed-width / compact | 314 ns / 468 ns | TBD24 |
| REV of an L-bit string, verified, fixed-width / compact | 468 ns / 717 ns | TBD24 |

- **The design is correct**, and was built as approved: every one of the
  real Bombs of 16 to 24 bits converts, every converted REV emits its
  string, every node count is unchanged, every shorter string gets the
  answer the FixedSolver gives from the fixed-width file, and each
  fixed-width file comes back from its compact file byte for byte.
- **It is format version 4, not 3.** Version 3 is the fixed-width format for
  25 to 31 bits that Milestone 5 asked for under that number, and the
  owner's ladder has been writing it since. Details and the alternative are
  in "The version number" below.
- **A compact file is 38% of a fixed-width one, and nearly half of it is the
  prefix index.** The REVs alone shrink to a fifth.
- **A lookup of a REV costs one more memory access** than in a fixed-width
  file, about 0.2 microseconds on this machine. Node counts cost the same
  or less.
- **The design leaves more on the table than it takes.** Three changes that
  were measured but are outside what was approved would bring the 23-bit
  file from 108.6 MB to 39 MB. They are listed under "Beyond the approved
  design" for a decision; none of them is in this code.

## Review of the design

### Is it correct?

Yes. The one step that needs an argument is the index rule, "the entry of a
shorter string is the smaller of the entries of its two extensions".

Ordinals are positions in the order (node count, string). So a smaller
ordinal never has more nodes, and among REVs of one node count the smaller
ordinal is the smaller string. The minimum over the two extensions by one
bit, applied all the way down, is the minimum over all extensions to L bits:
the REV with the fewest nodes among them, and of those the one of the
lowest string.

That REV is a minimum-node REV *for the shorter string*: it emits the
string as its first bits, and a REV that emits the shorter string emits one
of its L-bit extensions when run on, so none can have fewer nodes than the
least of their minima. This is the argument the Bomb-backed FixedSolver
already rests on, and the choice among equals is the same as its choice:
lowest index. The compact file therefore gives, for every string, the REV
the FixedSolver gives from the fixed-width file, not merely one as small.

Checked, not only argued:

- for every table of 1 to 10 bits, in each of the three fixed-width
  versions, every string of every length against a scan of the table that
  uses no index (`test_compact`);
- for the 16-bit Bomb, all 131,070 strings through the solver, from both
  files (`test_fixed_bomb_solver`);
- for the real Bombs of 16 to 24 bits, every string of every length from
  both files: node count and extension chosen agree (`compact_bench`,
  33,554,430 strings at 24 bits).

### What the proposal left open, and what was done

| Question | Decision |
|---|---|
| Version number: "v3" was taken | Version 4. See below |
| Tables that are not complete | Have no compact form. There is no status in the format; `compact` refuses, writing nothing |
| The directory holds start, offset and stride, which follow from the counts | A reader recomputes all of them and rejects a file that says otherwise. A directory is never trusted to say where a class is |
| Width of an index entry: 3 or 4 bytes at 24 bits | A header field. The least that holds an ordinal by default (2, 3, 4 bytes up to 16, 24, 31 bits), up to 4 on request. Measured below: 4 bytes cost 15% more file and buy nothing |
| How a reader knows the ordinals are a permutation in the right order | One pass over the L-bit entries in string order: each must be the next unused ordinal of its class. No memory beyond 31 counters |
| Damage | CRC-32 of the header, of the directory, of the index and of each class separately; the length of the file in the header. 1,123 single-bit changes tried in the tests, all refused |
| Provenance | Version and table checksum of the source in the header, and the checksum can be recomputed from the compact file alone, so the claim is checked wherever the file is verified |
| Reversibility | The REVs are stored node for node as they are in the source: no renumbering, no canonical form. So `expand` gives the source file back byte for byte, and a conversion is checked by `cmp` or SHA-256 in both directions |
| Safety of a conversion | Source opened read-only and validated in full first; output built under a temporary name, opened by the ordinary reader, compared with the source string for string, then renamed; an existing file is never replaced |
| Size of memory | Files are mapped; the writer and the expander stream. Converting the 23-bit Bomb allocates nothing to speak of (below) |

Two things follow from the design itself and cannot be decided away:

- **The index names a REV, not a string.** For a shorter string the file
  says which REV answers it, but not which L-bit string that REV stands
  for. The FixedSolver reports that extension, and gets it by running the
  REV for L bits: about 10 ns per bit in the reference interpreter.
- **Two memory accesses per REV instead of one.** The index entry, then the
  REV. In a fixed-width file the place of an L-bit string is arithmetic.

### The version number

The issue reserves "v3" for this layout and says, rightly, that two
incompatible formats must not ship under one number. The Milestone 5 brief
of the same evening asked for the fixed-width format for 25 to 31 bits as
version 3; it was built as that, and the owner's machine has been writing
version-3 journals and tables with it since. So one of the two had to take
another number, and it could only be the one with no files yet: **the
compact layout is format version 4.**

Nothing about versions 1, 2 and 3 changed: not a byte of any file means
something else, and the readers of the fixed-width formats reject a
version-4 file at the version field (tested in both directions).

The other way round is possible and costs more than it gives: to call the
compact layout 3, the fixed-width format of 25 to 31 bits would need a new
number, and every journal segment, `config.txt` and table the running ladder
has written would have to be rewritten with it, headers and checksums
included, after the run. The number is one constant
(`FRB_FORMAT_COMPACT`), so the decision is cheap to change before anything
is published and expensive after.

### Beyond the approved design

Measured on the real 23-bit Bomb, and not adopted, because each changes the
layout that was approved. Sizes are exact, from the class counts.

| | 23-bit file | of fixed-width |
|---|---:|---:|
| As approved and built | 108,606,260 | 38.1% |
| A. One bit per shorter string instead of an ordinal | 84,489,012 | 29.6% |
| D. Complement symmetry: half the REVs, half the index | 54,303,655 | 19.0% |
| A + D | 42,245,034 | 14.8% |
| A + D + C (links in base N) | 39,252,387 | 13.8% |

**A. One bit per shorter string.** The index entries of the strings of
fewer than L bits are half the index. Each could be one bit instead of an
ordinal: which of the two extensions holds the answer. A lookup then walks
from the string down to its L-bit extension, one bit per step, and reads
that string's ordinal. `compact_bench --alternatives` builds this index in
memory from the compact file and measures it against the one in the format:

| 23 bits | Index, bytes | Node count, 1..L | REV with its extension, 1..L |
|---|---:|---:|---:|
| An ordinal for every string | 50,331,642 | 26 ns | 352 ns |
| Ordinals for L bits, a bit for shorter | 26,214,400 | 81 ns | 357 ns |

Half the index for 55 ns on a node count of a shorter string, and nothing
measurable on a REV: the walk ends at the L-bit string, so the extension is
known without running the REV on, which pays for the walk. The walk reaches
the REV the ordinal names for every one of the 8,388,606 shorter strings.
From 25 bits, where an ordinal is 4 bytes, this saves nearly 2^(L+2) bytes:
130 MB at 25 bits, 260 MB at 26.

**D. Complement symmetry.** In every Bomb the ladder has built, the REV of
a string's complement is exactly the complement of the string's REV, node
for node: 4,194,304 of 4,194,304 pairs at 23 bits, and the same at 16, 17,
18 and 20. Half the REVs can therefore be derived from the other half by
exchanging the colors, and the file needs only the strings that start with
0. It also brings the ordinals of a 25-bit Bomb back to 3 bytes. The price
is in the rule for equals: the mirror image of "lowest string" is "highest
string", so either the strings that start with 1 get a second rule, or the
answer for such a string becomes the complement of the answer for its
complement, which is deterministic but is not always the REV the
FixedSolver returns today. It also needs a flag for tables that are not
exactly symmetric, which then stay as they are.

**C. Links in base N.** A class of 7 nodes spends 3 bits on a link that
has 7 values. Colors and links as one number of N + 2N·log2 N bits need 4,
5, 6, 9 and 10 bytes for N = 5, 6, 7, 9 and 10 where the format takes 5, 6,
7, 11 and 12 (8 nodes are exact at 7 bytes). On the 23-bit Bomb: 52,288,262
bytes of REVs instead of 58,273,556, 10% less, for a division per link when
a REV is read.

**B. Ordinals of L-bit strings by counting** (not measured). A string's
entry could be its node count alone, 4 bits, with its position in the class
found by counting earlier strings of that class from a sampled total. That
would take the remaining half of the index from 3 bytes a string to under
1.

Not worth having: storing REVs in a canonical numbering. It would make
equal REVs equal bytes, but the file would no longer be the source, and
`expand` could not give the source back.

**Recommendation.** Merge the format as approved: it is correct, verified
on real data, and reversible, so nothing is lost by having it. Decide on D
and A before compact Bombs of 25 bits and more are published, because that
is where they count most (from 25 bits an ordinal is 4 bytes, and the index
is then as large as the REVs or larger), and because either would be a new
version number rather than a change to this one.

## The real Bombs, converted

Each Bomb of the ladder was converted with `carlobomb bomb compact`,
verified with `bomb verify --against` its source, and expanded again with
`bomb expand`. For every one: all checks passed, the expansion is
byte-identical to the source (`cmp`), and the SHA-256 of the source is the
same after as before.

| L | Fixed-width, bytes | Compact, bytes | | Largest REV | compact / verify / expand |
|---:|---:|---:|---:|---:|---:|
| 16 | 1,245,248 | 640,084 | 51.4% | 7 nodes | 0.1 / 0.1 / 0.1 s |
| 17 | 4,456,512 | 1,566,366 | 35.1% | 8 | 0.2 / 0.1 / 0.2 s |
| 18 | 8,912,960 | 3,194,136 | 35.8% | 8 | 0.3 / 0.3 / 0.3 s |
| 19 | 17,825,856 | 6,524,810 | 36.6% | 8 | 0.5 / 0.6 / 0.6 s |
| 20 | 35,651,648 | 13,278,592 | 37.2% | 8 | 1.1 / 1.1 / 1.3 s |
| 21 | 71,303,232 | 26,865,794 | 37.7% | 9 | 2.2 / 2.2 / 2.7 s |
| 22 | 142,606,400 | 54,099,256 | 37.9% | 9 | 4.7 / 4.6 / 5.4 s |
| 23 | 285,212,736 | 108,606,260 | 38.1% | 9 | 9.3 / 9.7 / 11.1 s |
| 24 | 570,425,408 | TBD24 | TBD24 | TBD24 | TBD24 |

The 16-bit file is version 1 (19 bytes an entry), the others version 2 (34
bytes), which is why the ratio drops between 16 and 17 bits and then rises
slowly: the REVs grow with L, the index is a constant 6 bytes a string.

SHA-256 of the files. A compact file is the same bytes wherever it is made
from the same table, so these can be compared with a conversion anywhere:

| L | Fixed-width | Compact |
|---:|---|---|
| 16 | `0394d4d358312ae740ec1c05fbd0c18ca1416dfec3a2fcc400148a9b5e5e6a14` | `51b3e8c8c363a9d2f881f7bf4d326c82d735092031f6f49c62b82e3bacb93a13` |
| 17 | `e6d4cc43d3edc56f88e96037cef81f7f4c0b6e88ecbaef5052339596726eecdb` | `a48a56e4325ebe856c901704290fc6d2261521dd5246410400e099ff24cf9914` |
| 18 | `e67fb5a7c813d5c6c2d18532e2eb8a12dbf16f5fe2cf10f89ad9c02578a011b1` | `c8e5d4fb0822dfdb21aa89d210b662a1f085bffa647142957a127d2bbd58970a` |
| 19 | `86e5000f3b81abb57de2d318e5ebad7454e1d6db4395187d10310853682efa25` | `c9fd9da7f440219efb708d7fe93ed9aec9d2e5413b179783b2ee8125aed52052` |
| 20 | `5f5025bc00a26cad5153364a3bb3528c51d1d9cc9a7db16cf63df36b7393ca14` | `7392db19ab194a5fb075fd96efa9a1bb4243e10ff584bf6de7108003fc4fce44` |
| 21 | `1581fb018ee718e77fd77311eaba41a7304219b54cba4235c551dcdc920ed8cb` | `0e3db6319b79323c50b2b05a26ca7e7c28be0dd9d7e450e0d67cd409282b9f62` |
| 22 | `189e6ea0cf71cbcadb1765a017bf0e611027c64684f7b53ce34165d5811dcb98` | `474574cc992ba6dfc81f8785dcfbc247d424336bac65d5a8531f09c3b17a5365` |
| 23 | `61ee0779edc76e83da984d6970607cc3f5e333bdec267ca88209467daf491088` | `6a6661305e1456f1b290901589467570245c2c3b23b3e6d6b3d2aeadf6dceebc` |
| 24 | TBD24 | TBD24 |

Where the bytes go, at 23 bits:

    Nodes   Link bits   Bytes per REV          REVs            Bytes
        1           0               1             2                2
        2           1               1             6                6
        3           2               2            64              128
        4           2               3           968            2,904
        5           3               5        18,024           90,120
        6           3               6       406,588        2,439,528
        7           3               7     5,560,594       38,924,158
        8           3               7     2,402,318       16,816,226
        9           4              11            44              484
    REVs                      58,273,556 bytes
    prefix index              50,331,642 bytes (16,777,214 strings of 1 to 23 bits, 3 bytes each)
    header, directory              1,062 bytes (with the padding before the REVs)
    file                     108,606,260 bytes

TBD24-LAYOUT

## Opening and looking up

`compact_bench` opens the fixed-width file and its compact file through
`rev_fixed_bomb_open`, the interface that reads both, asks every string of
every length of both and requires the same node count and extension, and
then times 2,000,000 random lookups, best of five passes.

Opening is a full validation either way: every REV is run.

| | Open | Memory of its own | File pages mapped |
|---|---:|---:|---:|
| 20 bits, fixed-width | 0.36 s | 4.0 MiB | 34 MiB |
| 20 bits, compact | 0.29 s | 0 | 13 MiB |
| 23 bits, fixed-width | 3.23 s | 32.0 MiB | 272 MiB |
| 23 bits, compact | 2.68 s | 0 | 104 MiB |
| 23 bits, compact, 4-byte index | 2.74 s | 0 | 120 MiB |
| 24 bits, fixed-width | TBD24 | TBD24 | TBD24 |
| 24 bits, compact | TBD24 | TBD24 | TBD24 |
| 24 bits, compact, 4-byte index | TBD24 | TBD24 | TBD24 |

The memory of its own of the fixed-width solver is the prefix index it
builds at every opening, 4 bytes a string. The compact file brings its
index along, so nothing is built and nothing allocated; what is resident is
pages of the file, shared with the page cache and with every other process
that has it open.

Lookups in the opened Bomb, nanoseconds each. "Node count" reads the index
and no REV. "REV" is `rev_fixed_bomb_solve`: the REV decoded, run, and
compared with the target as the caller wrote it, as every answer of the
FixedSolver is.

| | Node count, L bits | Node count, 1..L | REV, L bits | REV, 1..L |
|---|---:|---:|---:|---:|
| 16 bits, fixed-width | 3.9 | 6.5 | 201.8 | 121.9 |
| 16 bits, compact | 6.9 | 6.7 | 262.2 | 255.8 |
| 20 bits, fixed-width | 13.0 | 22.0 | 410.1 | 244.6 |
| 20 bits, compact | 20.2 | 14.5 | 459.2 | 361.5 |
| 23 bits, fixed-width | 27.0 | 35.3 | 468.3 | 314.1 |
| 23 bits, compact | 24.7 | 25.5 | 717.3 | 467.8 |
| 23 bits, compact, 4-byte index | 30.6 | 29.6 | 700.3 | 477.1 |
| 24 bits, fixed-width | TBD24 | TBD24 | TBD24 | TBD24 |
| 24 bits, compact | TBD24 | TBD24 | TBD24 | TBD24 |
| 24 bits, compact, 4-byte index | TBD24 | TBD24 | TBD24 | TBD24 |

What the numbers are made of, from the same lookups in sorted order, where
nothing misses the cache: decoding a REV from the compact file takes 32 ns
(index entry, class, unpacking) against 42 to 54 ns from a fixed-width
entry, and running it for 23 bits takes 230 ns, the same for both. The rest
is memory: a random access into a file of 100 to 300 MB costs 150 to 200 ns
on this virtual machine, the fixed-width lookup of an L-bit string makes
one, and the compact lookup two.

- **Node counts cost the same, and for shorter strings less.** One access:
  the compact index entry, or the metadata byte of the fixed-width entry,
  which for a shorter string comes after the solver's own index.
- **A REV costs one more access from the compact file**, 0.2 microseconds
  here. For a shorter string the compact lookup also runs the REV for all L
  bits, to say which extension it stands for, where the fixed-width solver
  runs it for the length asked.
- **3 or 4 bytes per index entry makes no difference to the time of a REV**
  (within 3%, in either direction) and 16.8 MB at 23 bits, 33.5 MB at 24.
  The default is 3.
- **In a Bomb that fits the cache** (16 bits) a REV of an L-bit string costs
  0.20 against 0.26 microseconds, and that is the running of the REV: 10 ns
  a bit. The compact reader runs it for L bits whatever the length asked.

One answer straight from the file, nothing opened before, as `carlobomb
bomb lookup` does it (microseconds, 20,000 lookups):

| | REV, L bits | REV, 1..L |
|---|---:|---:|
| 16 bits, fixed-width | 2.7 | not possible: open the Bomb, 0.02 s |
| 16 bits, compact | 5.7 | 5.6 |
| 23 bits, fixed-width | 3.3 | not possible: open the Bomb, 3.23 s |
| 23 bits, compact | 7.0 | 6.2 |
| 24 bits, fixed-width | TBD24 | TBD24 |
| 24 bits, compact | TBD24 | TBD24 |

The compact lookup reads and checks 1,056 bytes of header and directory,
one index entry and one REV. A fixed-width file answers an L-bit string
from 64 bytes of header and one entry, and cannot answer a shorter string
without its whole table; from a compact file that is the same few
microseconds.

## The commands, on the 23-bit Bomb

| | Time | Memory of its own, peak | Resident with file pages, peak |
|---|---:|---:|---:|
| `bomb compact` | 10.2 s | under 1 MiB | 377 MiB |
| `bomb verify` (compact) | 6.4 s | under 1 MiB | 105 MiB |
| `bomb verify --against` the source | 9.9 s | under 1 MiB | 377 MiB |
| `bomb expand` | 11.7 s | 4 MiB | 377 MiB |
| `bomb verify` (fixed-width) | 11.9 s | under 1 MiB | 273 MiB |
| `fixed --target` on the fixed-width file | 3.5 s | 32 MiB | 297 MiB |
| `fixed --target` on the compact file | 3.1 s | under 1 MiB | 105 MiB |
| `bomb lookup` on the compact file | under 0.01 s | under 1 MiB | under 1 MiB |

Both files are mapped, not read, and the new file is written through a
mapping or entry by entry, so the resident pages are the files themselves
and can be reclaimed by the system at any time. A Bomb larger than memory
converts the same way.

## What a conversion does and does not touch

- The source file is opened for reading and is never written. Its SHA-256
  was taken before and after each conversion above and is the same.
- No command here opens a work directory, a journal or a lock of a
  construction. `--work-dir` and `--recertify` are refused for a compact
  file rather than reinterpreted.
- A construction does not take a compact file as its base: `bomb build
  --base` and `bomb ladder --from` refuse it and say how to get the
  fixed-width file back.
- An output name that exists is refused. A failed conversion leaves no file
  under the name asked for.
- The owner's running ladder was not involved in any of this: it runs on
  another machine from its own checkout, and nothing in this branch changes
  the fixed-width formats, the journal or the construction code. Converting
  a finished table while a higher level is built on it only reads the table.

The minimality of the node counts is not re-proved by a conversion and is
not claimed by a compact file. It is established by the construction of the
fixed-width file and recorded in its journal. What a conversion proves is
that the compact file holds that table: the same REV for every string.

## Reproducing this

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
build/carlobomb bomb compact --file fixed-run-24.frb --output fixed-run-24.compact.frb
```

```bash
build/carlobomb bomb verify --file fixed-run-24.compact.frb --against fixed-run-24.frb
```

```bash
build/carlobomb bomb expand --file fixed-run-24.compact.frb --output again.frb && cmp again.frb fixed-run-24.frb
```

```bash
build/carlobomb bomb compact --file fixed-run-24.frb --output fixed-run-24.wide.frb --index-bytes 4
```

```bash
build/compact_bench --fixed fixed-run-24.frb --compact fixed-run-24.compact.frb --wide fixed-run-24.wide.frb --alternatives
```

The first four need about 1.2 GB of disk for a 24-bit Bomb and under a
minute. None of them needs the work directory, and all of them can run
while a ladder builds the next level on the same table.

## Tests

| Test | Covers |
|---|---|
| `test_compact`, `test_compact_unmapped` | Strides and packing against bytes worked out by hand; the layout arithmetic to 31 bits; the worked example of the specification, all 1,082 bytes; conversion of every table of 1 to 10 bits from each fixed-width version and back, byte for byte; every string of every length against a scan of the table; all 31 classes; a table that differs in one REV, one link or one color; 1,123 single-bit changes; every field of the header and directory wrong by itself with the checksums made right; structural damage; version confusion in both directions. 104,935 checks, once with mapped files and once without |
| `test_compact_internal` | The compact code compiled into the test. The two independent checks of the index, each refusing a wrong answer for a shorter string on its own. A writer told to write another REV, or a wrong one, and a file that reads back as another: each found before a file gets its name, nothing left behind |
| `test_fixed_bomb_solver` | The 16-bit Bomb in both formats through the solver: the same answer for all 131,070 strings, budgets, longer targets; a compact file whose node counts are not symmetric refused |
| `test_fixed_bomb_internal` | A compact Bomb changed in memory after it was opened: no REV for the strings changed, the others as before |
| `bomb_small`, `bomb_16`, `bomb_16full` | The commands end to end: round trips from versions 1, 2 and 3, sizes and SHA-256 of the compact 16- and 17-bit Bombs, lookups of shorter strings, a compact file that names a false source, and every refusal |

23 tests with GCC and with Clang, 22 under AddressSanitizer and
UndefinedBehaviorSanitizer, 20 without the assembly engine: all pass.

**Tests of the tests.** 62 bugs were put into the new code one at a time,
in the reader, the writer, the comparison, the solver and the commands: a
link one bit too wide, a checksum not compared, the order of the ordinals
not checked, the larger extension written for a shorter string, an existing
file replaced, and so on. The tests as first written let six through: the
reserved bytes of the header, the version an expansion goes to, the
read-back of an expansion, the opening of a new file before it gets its
name, the removal of the temporary file after a failed conversion, and the
solver's last comparison of a compact answer with its target. Four of them
are checks that only a fault elsewhere can trip, which is why nothing
exercised them; each now has a test that injects the fault. 61 of the 62
are caught now. The one that is not removes a second check of a REV's links
in the solver, behind the same check in the unpacker; it is kept because
the code after it follows those links.
