# Bomb-backed FixedSolver: results

The solver of `solvers/fixed/bomb/`: minimum-node REVs for every binary
string of 1 to 16 bits, read out of the FixedRunBomb(16) of
`FIXED_RUN_16.md`.

Measured in the development container: Intel Xeon (family 6, model 173) at
2.3 GHz, 4 virtual CPUs, Linux x86-64, GCC 13.3, Release build, on the Bomb
built with the default settings (SHA-256 `0394d4d3…6a14`). It is a shared
cloud machine; repeated runs differed by a few percent.

## 1. Every string of up to 16 bits

Number of strings of each length by their minimum node count. Every figure
is a certified minimum.

| Bits | Strings | 1 | 2 | 3 | 4 | 5 | 6 | 7 | Largest | Mean |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2 | 2 | | | | | | | 1 | 1.000 |
| 2 | 4 | 2 | 2 | | | | | | 2 | 1.500 |
| 3 | 8 | 2 | 4 | 2 | | | | | 3 | 2.000 |
| 4 | 16 | 2 | 6 | 8 | | | | | 3 | 2.375 |
| 5 | 32 | 2 | 6 | 20 | 4 | | | | 4 | 2.812 |
| 6 | 64 | 2 | 6 | 38 | 18 | | | | 4 | 3.125 |
| 7 | 128 | 2 | 6 | 50 | 68 | 2 | | | 5 | 3.484 |
| 8 | 256 | 2 | 6 | 60 | 176 | 12 | | | 5 | 3.742 |
| 9 | 512 | 2 | 6 | 62 | 348 | 94 | | | 5 | 4.027 |
| 10 | 1,024 | 2 | 6 | 64 | 562 | 388 | 2 | | 6 | 4.303 |
| 11 | 2,048 | 2 | 6 | 64 | 726 | 1,226 | 24 | | 6 | 4.582 |
| 12 | 4,096 | 2 | 6 | 64 | 830 | 2,994 | 200 | | 6 | 4.809 |
| 13 | 8,192 | 2 | 6 | 64 | 910 | 5,836 | 1,372 | 2 | 7 | 5.038 |
| 14 | 16,384 | 2 | 6 | 64 | 946 | 9,292 | 6,066 | 8 | 7 | 5.304 |
| 15 | 32,768 | 2 | 6 | 64 | 956 | 12,210 | 19,430 | 100 | 7 | 5.565 |
| 16 | 65,536 | 2 | 6 | 64 | 962 | 14,348 | 48,956 | 1,198 | 7 | 5.767 |
| all | 131,070 | 32 | 84 | 688 | 6,506 | 46,402 | 76,050 | 1,308 | 7 | 5.537 |

```bash
build/carlobomb fixed --bomb fixed-run-16.frb --stats
```

The 16-bit row is the Bomb itself. The other fifteen rows come from the
prefix index and cost nothing: the one table is, in effect, a complete
certified table for each shorter length as well. Rows 10, 12 and 14 had been
computed separately, by running the MinimalSolver over all strings of those
lengths before this solver existed, and came out the same; the test suite
now re-derives every row that way (section 3).

### The strings that need the most nodes

| Bits | Largest minimum | Strings that need it | First in order |
|---:|---:|---:|---|
| 1 | 1 | 2 | `0` |
| 2 | 2 | 2 | `01` |
| 3 | 3 | 2 | `001` |
| 4 | 3 | 8 | `0001` |
| 5 | 4 | 4 | `00001` |
| 6 | 4 | 18 | `000001` |
| 7 | 5 | 2 | `0000001` |
| 8 | 5 | 12 | `00000001` |
| 9 | 5 | 94 | `000000001` |
| 10 | 6 | 2 | `0000000001` |
| 11 | 6 | 24 | `00000000001` |
| 12 | 6 | 200 | `000000000001` |
| 13 | 7 | 2 | `0000000000001` |
| 14 | 7 | 8 | `00000000000001` |
| 15 | 7 | 100 | `000000000000001` |
| 16 | 7 | 1,198 | `0000000000000001` |

Two things can be read off this table, and they are of different standing.

**Established, for lengths 1 to 16.** The largest minimum over all strings
of L bits is ⌈2√L⌉ − 1, and the string of L − 1 zeros followed by a one
attains it. Put the other way round: n nodes are enough for every string of
up to ⌊(n + 1)²/4⌋ bits (1, 2, 4, 6, 9, 12, 16 bits for n = 1 … 7), and for
n up to 6 some string one bit longer needs n + 1. This is a statement about
all 131,070 strings and follows from the table above.

**Observed, for one family of strings, up to 28 bits.** The string 0…01
keeps to the same formula beyond 16 bits: searched with the MinimalSolver it
needs 8 nodes at 17 to 20 bits, 9 at 21 to 25 and 10 at 26 to 28, the values
of ⌈2√L⌉ − 1, with the steps at 17, 21 and 26 where ⌊(n + 1)²/4⌋ says
(`build/fixed_bomb_longer`, part 2).

**Not known.** Whether ⌈2√L⌉ − 1 is the largest minimum for all strings
beyond 16 bits. What the search shows for 17 to 28 bits is that at least
that many nodes are needed by some string, since 0…01 needs them. Whether
some other string needs more would take the table for that length.

A table for 20-bit strings would settle it for 17 to 20 bits, and through
the same prefix index answer every length up to 20. As an estimate only:
searching all strings of 12, 14 and 16 bits took 0.03 s, 0.36 s and 4.8 s on
one core, a factor of 11 to 13 for every two bits, which puts 20 bits at a
quarter to half an hour of search on one core and the table at 20 MB. It
has not been built; the table format is specified for up to 16 bits.

## 2. Speed

```bash
build/fixed_bomb_lookup fixed-run-16.frb
```

| | Time |
|---|---:|
| Loading: read 1.2 MB, both checksums, every entry decoded, every graph run for 16 bits | 18.5 ms |
| Index construction: 65,534 prefixes, 128 KiB | 0.1 ms |
| Lookup: which 16-bit entry answers a string | 5.2 ns |
| Lookup: minimum node count of a string | 5.4 ns |
| Solve: text in; graph unpacked, run against the target, returned | 0.21 µs |
| `carlobomb fixed`, one question, whole process | 22 ms |

Lookups are timed over all 131,070 strings of 1 to 16 bits in a shuffled
order, best of 9 passes. 4.7 million verified answers per second on one
core.

Loading is 99.5% of opening a Bomb and is validation: nothing is trusted
that has not been run. Building the index is 0.5%. **A stored index would
save 0.1 ms of 18.6 and add a file format, so there is none.** A one-off
command-line question is dominated by loading; a program that keeps the Bomb
open pays it once.

The same answers by search, MinimalSolver from one node up, mean per string:

| Bits | Search, `amd64-auto` | Search, C | Lookup |
|---:|---:|---:|---:|
| 4 | 0.64 µs | 0.83 µs | 0.21 µs |
| 8 | 1.46 µs | 4.38 µs | 0.21 µs |
| 12 | 7.82 µs | 34.9 µs | 0.21 µs |
| 16 | 73.7 µs | 319 µs | 0.21 µs |
| all 1–16 | 49.7 µs (6.5 s in all) | 230 µs (30 s in all) | 0.21 µs (0.03 s in all) |

At 16 bits the lookup is about 350 times faster than the assembly search and
1,500 times faster than the C search; for short strings the search is quick
as well and the factor is small. The comparison is of two ways to obtain the
same certified answer, not of two algorithms for the same work: the search's
work was done once, when the Bomb was built.

## 3. Checks

All in `ctest`; details in `solvers/fixed/bomb/README.md`.

| Check | Cases | Result |
|---|---:|---|
| Answer against a direct scan of the table for the fewest nodes and lowest index | 131,070 strings | all agree |
| Returned graph run independently against the target | 131,070 | all emit it |
| Minimum against the MinimalSolver, searched from one node up (6.9·10^9 steps) | 131,070 | all agree |
| Strings with more than one smallest extension get the lowest | 40,884 | all |
| A Bomb with other graphs of the same sizes selects the same extensions | 131,070 | all |
| The same exhaustive scan in a program with no search solver linked in | 131,070 | all agree |
| Bombs refused: missing, cut, lengthened, bit flipped, other version, not a Bomb, damaged behind repaired checksums, partial, partial with forged flag, self-contradicting | 22 kinds | each refused, each for its own reason |
| Suffix fact: the constructed REV of n + 1 nodes emits the remainder | 983,040 cuts | all |
| Lower bound on longer targets: against the exact minimum of the strings the stored graphs go on to emit, and against the search on others | 983,040 + 767 | equal on the first, never above on the rest |

## 4. Longer targets

The solver does not answer targets of more than 16 bits. It reports a
certified lower bound (`solvers/fixed/bomb/README.md`, "Longer targets").
Two questions were measured: how good is the bound, and would it pay to
start a search at it.

```bash
build/fixed_bomb_longer fixed-run-16.frb
```

Random targets, MinimalSolver with `amd64-auto` for the true minimum:

| Bits | Targets | Mean minimum | Mean bound | Bound is the minimum | Stored REV of the first 16 bits emits the whole target | Search steps spent below the bound |
|---:|---:|---:|---:|---:|---:|---:|
| 18 | 300 | 6.20 | 5.77 | 187 | 67 | 16.4% |
| 22 | 300 | 7.04 | 5.85 | 22 | 5 | 1.8% |
| 26 | 300 | 7.84 | 5.87 | 1 | 0 | 0.2% |
| 30 | 60 | 8.58 | 5.80 | 0 | 0 | 0.0% |

The bound never exceeded the minimum, here or in the tests. For random
targets it stays near 6 while the minimum grows, as it must: it is drawn
from 16-bit stretches, and no 16-bit string needs more than 7.

**An idea measured and not built.** For a longer target one could let the
Bomb help the search: try the REV stored for the first 16 bits, and if it
does not emit the whole target, start the MinimalSolver at the bound instead
of at one node. Both steps are sound. The last two columns are what that
would gain. Two bits beyond the Bomb it is something: the stored REV already
answers 22% of random targets and the search would skip 16% of its steps.
At 22 bits it is 2%, and from 26 bits on nothing, because a search spends
nearly all its steps at the last node budgets and the bound is far below
them. For targets that are not random the picture is different (a periodic
target is answered by the stored REV at any length), but that is a narrow
case to build a solver mode on. The lower bound is reported; the search is
not started from it.

**The suffix fact, in numbers.** Cutting the first j bits off a 16-bit
string leaves a string that needs one node more than the whole in 5,924 of
983,040 cuts (0.6%), and never two more. So a string can be harder than a
string it is the tail of, by exactly one node.

## 5. What this does not cover

- Targets of more than 16 bits, beyond the lower bound.
- A Bomb whose entries are not minima although it is complete and
  self-consistent cannot be told from a correct one by reading it. The
  solver's answers are as good as the Bomb's construction; the Bomb used
  here was recomputed in full (`FIXED_RUN_16.md`, section 6).
- Timings are from one shared virtual machine.
