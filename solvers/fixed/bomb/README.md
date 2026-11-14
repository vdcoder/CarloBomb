# Bomb-backed FixedSolver

The exact minimum-node REV for any binary string of 1 to 16 bits, read out
of the complete FixedRunBomb(16) of Milestone 2. No search is run. With a
longer complete Bomb the same solver answers strings up to that length
([below](#longer-bombs)).

```bash
build/carlobomb fixed --bomb fixed-run-16.frb --target 01001000
```

```
Bomb: fixed-run-16.frb (FixedRunBomb(16), complete: 65,536 certified minima)
Target: 01001000 (8 bits)
Result: MINIMAL (certified minimum node count; read from the Bomb, no search)
Nodes: 3
Extension: 0100100010100100 (entry 18596): the first of the 16-bit strings starting with the
           target that need no more than 3 nodes

Node   Color   Link0   Link1
0      B (1)   1       2
1      W (0)   2       1
2      W (0)   0       0

Verification: PASS
```

| File | Content |
|---|---|
| `fixed_bomb_solver.h`, `fixed_bomb_solver.c` | The solver: open a Bomb, build the prefix index, answer. Plain C11 |
| `tests/test_fixed_bomb_solver.c` | Its tests |
| `../../../tools/fixed_bomb_cli.c` | `carlobomb fixed` |
| `../../../benchmarks/fixed_bomb_lookup.c` | Exhaustive self-check and timings, with no search solver linked in |
| `../../../benchmarks/FIXED_BOMB_SOLVER.md` | Measurements, and the minimum-node counts for every length |

This is not the FixedSolver(N, L) planned in `../README.md`, a search
specialized for one node capacity and one length. That plan stands, and
nothing here takes its place or its names.

## Why a table of 16-bit strings answers shorter ones

Let S be a string of L < 16 bits.

- A REV that emits S does not stop there: its first 16 output bits are some
  16-bit string T that starts with S. So it is a REV for T, and has at least
  as many nodes as T needs.
- A REV that emits a 16-bit string starting with S emits S.

So the minimum node count of S is the smallest minimum among the 2^(16−L)
strings of 16 bits that start with S, and the REV stored for that one is a
minimum-node REV for S. Since every entry of the Bomb is a certified
minimum, so is every answer derived this way. Nothing in the argument is
special to 16: a complete FixedRunBomb(B) answers every length up to B, and
the solver accepts any B.

## The prefix index

Scanning up to 32,768 entries per question would work. Instead the choice is
made once for every string of fewer than 16 bits, when the Bomb is opened.

The strings form a binary tree: the children of a string are the string with
0 appended and with 1 appended. Stored as a heap, the string of L bits with
value v (first bit most significant) is node 2^L + v, the children of node k
are 2k and 2k + 1, and the 16-bit strings are the leaves, which are the
Bomb's own entries. For each inner node the index holds one 16-bit number:
the table index of the chosen 16-bit extension.

    best(S) = better(best(S0), best(S1))        from the longest strings down

`better` takes the extension with fewer nodes, and on a tie the one through
`S0`. Every extension through `S0` has a lower table index than every
extension through `S1`, so **ties go to the lowest index**, which is the
lexicographically first extension. The choice depends on the node counts
alone, so two Bombs that hold different minimum-node graphs select the same
extensions.

That is 65,536 numbers of two bytes, 128 KiB, filled in one pass of 65,535
comparisons: 0.1 ms. A lookup is then one array read, or none for a 16-bit
target. The graphs are not copied; they stay in the Bomb's entries. There is
no index file, since building the index costs less than a hundredth of
reading the Bomb.

## Longer Bombs

The solver opens a complete FixedRunBomb of any length the format has: up
to 16 bits in format version 1, up to 24 in version 2
(`bombs/fixed-run/FORMAT.md`). Nothing changes for a Bomb of up to 16 bits.
Above that a table index no longer fits in 16 bits and the prefix index
holds 32-bit numbers: 0.5 MiB for 17 bits, 64 MiB for 24, next to a table
of 544 MiB. Opening validates the whole table as before, which for 24 bits
means running 16,777,216 REVs.

```bash
build/carlobomb fixed --bomb ladder/fixed-run-20.frb --stats
```

prints the minimum-node distribution of every length from 1 to 20 bits. Its
rows for 1 to 16 bits are those of the 16-bit Bomb, as they must be: a
complete Bomb for more bits answers everything a shorter one does.

## Compact Bombs

`rev_fixed_bomb_open` also opens a Bomb in the compact format, version 4
(`bombs/fixed-run/FORMAT.md`, section 12), and everything here is answered
from it. The file is validated in full by its own reader when it is opened.
No prefix index is built, because the file carries one navigation bit for
every shorter string, and nothing is allocated: the 24-bit Bomb is 37 MiB of
mapped file, where the fixed-width one is 544 MiB and 64 MiB of index.

Node counts are the same from both files, for every string of every length.
The REV of a B-bit string is the same REV, for every Bomb this project has
built. For a shorter string there is one difference. A compact file stores
the strings that start with 0 and answers a string that starts with 1 with
the complement of the answer for its complement. So a tie goes to the lowest
index for a string that starts with 0, as above, and to the **highest** for
one that starts with 1. Where the two differ the solver returns another REV
than it does from the fixed-width file: of the same certified minimum
number of nodes, for the same string.

A REV from a compact file is not stored as it is returned: its colors and
the numbering of its nodes are worked out from its graph and its string.
That, and the run for all B bits that tells which extension the REV stands
for, make an answer cost about twice what it costs from a fixed-width file;
[`benchmarks/COMPACT_FORMAT.md`](../../../benchmarks/COMPACT_FORMAT.md) has
the numbers.

## Interface

```c
RevFixedBomb *bomb;
RevFixedBombInfo info;
RevGraph g;

if (rev_fixed_bomb_open("fixed-run-16.frb", &bomb, NULL) != FRB_OK) ...
switch (rev_fixed_bomb_solve(bomb, "01001000", 8, 0, &g, &info)) ...
rev_fixed_bomb_close(bomb);
```

| Result | Meaning |
|---|---|
| `REV_FIXED_BOMB_MINIMAL` | The returned REV is a certified minimum for the target. `info.extension` is the 16-bit entry it came from |
| `REV_FIXED_BOMB_PROVEN_UNSAT` | A node budget was given and it is below the target's certified minimum, which is in `info.min_nodes`. This is a proof: the minimum is exact |
| `REV_FIXED_BOMB_UNSUPPORTED` | The target is longer than the Bomb covers. Nothing is said about its minimum; `info.lower_bound` is what the Bomb does know (below) |
| `REV_FIXED_BOMB_INVALID_INPUT` | No target, an empty one, a character other than `0` and `1`, a negative budget |
| `REV_FIXED_BOMB_INTERNAL_ERROR` | The stored graph did not verify: the Bomb's memory was damaged after it was opened |

`rev_fixed_bomb_min_nodes` and `rev_fixed_bomb_extension` answer for a
string given as a number, without text and without unpacking a graph.
`rev_fixed_bomb_histogram` counts the strings of one length by minimum.
`rev_fixed_bomb_lower_bound` is described below.

An open Bomb is never written to. Any number of threads may query the same
one, which the search solvers, with their static state, do not allow.

The solver's library links the FixedRunBomb format code and the REV
interpreter and nothing else. `benchmarks/fixed_bomb_lookup` is built from
exactly that: there is no search in the program that could be run.

## What opening checks, and what it cannot

`rev_fixed_bomb_open` refuses a Bomb unless

1. it passes every check of `bombs/fixed-run/FORMAT.md` section 6: header,
   length, both checksums, every entry well-formed, every stored graph run on
   the interpreter and found to emit the string it is filed under, header
   counts and flags true;
2. every entry is a certified minimum. A partial Bomb, in which some entries
   are valid witnesses of unknown quality or empty, is refused
   (`FRB_ERR_INCOMPLETE`), whatever its header claims;
3. every string has the same node count as its complement, as complement
   symmetry requires (`FRB_ERR_EVIDENCE`).

Each answer is checked again when it is given: the graph is unpacked and run
against the target as the caller wrote it.

What no reader can check from the file is that a stored node count is in
fact the minimum. A table in which a string and its complement both hold a
graph larger than necessary is consistent with itself and opens. That its
entries are minima is the evidence of the Bomb's construction: the journal,
`carlobomb bomb verify --work-dir`, and `--recertify`, which recomputes every
minimum by search (`bombs/fixed-run/README.md`). Use Bombs you built or
verified.

## Longer targets

A target of more than 16 bits is `UNSUPPORTED`, with any budget. The Bomb is
never used to call a REV for a longer string minimal: the REV stored for its
first 16 bits may or may not go on to emit the rest, and if it does not,
some other REV of the same size might.

What the Bomb does establish for a longer target is a **lower bound**, and
the solver reports it (`info.lower_bound`, `rev_fixed_bomb_lower_bound`, and
the `fixed` command). It is a bound and not an answer. It rests on two
facts.

**Prefix.** A REV that emits S emits the first 16 bits of S. So S needs at
least as many nodes as those do.

**Suffix.** If a REV of n nodes emits S, then for every j a REV of n + 1
nodes emits S without its first j bits.

*Proof.* Run the REV for j bits. Just before it emits bit j, its flow
pointer is at some node f and its control pointer at some node c. Its next
two operations are determined: CTRL moves the control pointer to
c′ = link(c, color f) and emits b = color c′, which is bit j of S; FLOW then
moves the flow pointer to f′ = link(f, b). Add one node r and make it the
root. Give r the color opposite to b, let its link for that color lead to
c′, and let its other link, the one for b, lead to f′. Started at r, the
first CTRL reads the color of r and follows r's link for it: the control
pointer arrives at c′ and b is emitted. The first FLOW reads b and follows
r's link for b: the flow pointer arrives at f′. Both pointers are now where
the original has them after bit j, no link leads back to r, and so the new
REV goes on to emit exactly what the original emits from bit j + 1 on. ∎

So for every j ≥ 1 the first 16 bits of what remains of S after j bits (or
all of it, if fewer remain) need at most one node more than S does. The
bound is the largest of: the minimum of the first 16 bits of S, and, for
each j ≥ 1, the minimum of that stretch less one. It costs one lookup per
character.

The "one more" is not slack. Of the 983,040 ways to cut the front off a
16-bit string, 5,924 leave a remainder that needs one node more than the
whole string. The test suite builds the graph of the proof for every one of
those 983,040 cuts and runs it.

How good the bound is depends on the target. For random targets it is
usually 6, and so far from the minimum of a long string; it is exact for
every one of the 983,040 strings of 17 to 31 bits that the Bomb's own graphs
go on to emit. `benchmarks/FIXED_BOMB_SOLVER.md` has the figures, and the
measurement of whether starting a search at the bound would be worth
building (for random targets: no).

## Tests

`ctest` runs three things for this solver.

`test_fixed_bomb_solver`:

- Bombs of 1, 2, 5, 8 and 10 bits built straight from the C MinimalSolver:
  every string of every length each covers, compared with the MinimalSolver
  one by one; node budgets one below, at and above the minimum, with the
  search confirming each refusal; invalid questions; targets a few bits too
  long.
- Bombs that must be refused, each for its own reason: missing, cut short,
  too long, a flipped bit, a foreign format version, not a Bomb at all,
  damage behind repaired checksums, entries exchanged, partial (four kinds),
  partial with a forged "complete" flag, and a string whose stored node
  count its complement contradicts. And an open Bomb whose memory is then
  overwritten: the solver returns `INTERNAL_ERROR`, not a wrong graph.
- On the 16-bit Bomb, all 131,070 strings of 1 to 16 bits: the answer
  against a direct scan of the table for the fewest nodes and the lowest
  index; the graph run independently against the target; the graph
  identical to the Bomb's entry; complement and prefix consistency; the
  per-length counts against the published table.
- The same 131,070 strings against the MinimalSolver, each searched from one
  node up.
- Tie-breaking: 40,884 strings have more than one smallest extension and get
  the lowest; a second Bomb with 65,528 different graphs of the same sizes
  selects the same extension for every string.
- The suffix fact on all 983,040 cuts, and the lower bound on longer
  targets: never above the MinimalSolver's minimum on random and regular
  targets of 17 to 35 bits, and exact on the 983,040 continuations.

- A 17-bit Bomb, built in the test as one rung of the ladder above the
  16-bit one (format version 2, 32-bit index): all 262,142 strings of 1 to
  17 bits against a direct scan of its table, every graph run, and for 1 to
  16 bits the same minimum as the 16-bit Bomb gives.

`fixed_smoke` runs the `fixed` command end to end on a Bomb built for the
occasion, including refusals and exit codes, and runs
`fixed_bomb_lookup --check`, the exhaustive scan in a program that has no
search solver in it.

The tests were tested: 16 bugs were put into the solver one at a time (ties
to the higher index, the minus one dropped from the bound, a partial Bomb
accepted, a budget ignored, and so on). Fourteen fail the tests in an
ordinary build, one after a test was added for it. One reads past the end of
the index and is caught by the sanitizer build. One changes nothing that can
be observed on a valid Bomb.
