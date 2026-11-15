# The previous Bomb's scratchpad: notes and measurements

A continuing research note. Nothing in it is built into the construction,
nothing is certified by it, and no format is proposed. It asks what a
construction of Bomb(X − 1) could leave behind, beyond the Bomb, that makes
Bomb(X) cheaper, and keeps what was measured about each idea, the ones
that failed included.

Measured on 15 November 2026 in the development container, on the ladder
of 16 to 24 bits built there and the 25- and 26-bit tables of the
shared-search experiment, with the SharedSolver of the construction by
shared search. `free_fields.c` and `proof_only.c` in this directory are the
programs; they read Bombs and write nothing.

## Where it stands

Three ideas measured, one observation that explains all three, and one
place left to look.

| Idea | What a scratchpad would hold | Derivable from the Bomb? | Measured worth |
|---|---|---|---|
| The freedom of the stored REV | which fields of each REV the machine never read | yes, by running the REV | 0.02% of the strings it could settle: nothing |
| Every REV known beforehand | a REV for every string of X bits, by any means | no | at most a factor of 2.3 to 2.9, less with length |
| A stored frontier of the search | the partly decided graphs at some depth | no | near the top: reusable and worth little; near the bottom: worth nearly everything, as large as the search, and not what the next level needs |

**The observation.** The cost of a pass hardly depends on how many strings
it is for. At 20 bits, the pass at 7 nodes searches 231,286 strings and
finds a REV for all but 1,406 of them; searching only those 1,406, to prove
they have none, costs 41% of the whole pass. At 22 bits the 176,055
strings without a 7-node REV cost 87% of the pass for 1,009,543. The
search walks the partly decided graphs of N nodes down along the beginnings
of the strings that are still wanted, and a few hundred strings spread
over the table keep nearly all of the upper part of that walk alive.
**What costs is proving absence, and the proof for one string is almost
the proof for all its neighbors.** That is why the shared search is cheap,
why building on the Bomb below saves it only 0.2% (the strings the base
settles would have cost nothing extra), and why supplying REVs saves so
little.

**What is left.** A level is passes, and a pass at budget N exists for two
reasons: to find the REVs of N nodes, and to prove that the strings which
need more have none. Knowing the REVs beforehand removes the first reason;
only a **lower bound** removes the second: something that says "this string
needs more than its first bits do" without walking graphs. With both, a
level would need no search at all; with either alone, a factor of two or
three. The bounds the Bomb below gives (its first bits; one node less than
any later stretch) reach 0.1% of the proofs. So the place to look is not in
the search's by-products but in a statement about strings: which
extensions of q need one node more than q. That is a question for the
mathematics, and the data for it already exists (below).

## 1. The freedom of the stored REV

The search decides a color or a link only when the machine first reads it.
A REV as it is found is a partly decided graph, and the fields it never
read are free: any value in them, and the graph still emits its string. The
Bomb stores them as 0. A REV of q with free fields is a REV, of the same
M(q) nodes, of every longer string some filling of them emits; and since no
extension of q can need fewer nodes than q, each such string would be
certified at once. Which fields are free is not lost with the Bomb: running
the stored REV for its string and marking what the machine reads gives them
back. So this scratchpad would cost nothing to keep.

`free_fields` runs every REV of the shorter Bomb, marks the fields read,
and asks for the next bit: over every filling of the free fields that the
next step reads, which of q0 and q1 comes out.

| Bombs | Strings q (one of each pair) | REVs with no free field | with one | with more | Next step reads a free field | Both of q0, q1 by a filling | The other extension has M(q) nodes | of those, by a filling |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 → 17 | 32,768 | 76.7% | 19.2% | 4.1% | 15 | 10 | 21,663 | 10 |
| 19 → 20 | 262,144 | 80.7% | 16.5% | 2.8% | 126 | 90 | 152,299 | 90 |
| 21 → 22 | 1,048,576 | 83.0% | 14.9% | 2.1% | 296 | 199 | 709,932 | 199 |
| 23 → 24 | 4,194,304 | 84.5% | 13.8% | 1.8% | 738 | 526 | 2,207,046 | 526 |
| 25 → 26 | 16,777,216 | 85.7% | 12.8% | 1.5% | 3,514 | 2,456 | 11,841,340 | 2,456 |

One REV in six or seven has a free field, and it is almost never one the
machine is about to read: after 25 bits a machine of 8 nodes has been everywhere it is
going next. Of the 11.8 million strings of 25 bits whose other extension
needs no more nodes, the stored REV shows it for 2,456. The other
extension is emitted by *another* REV of q, not by this one with its gaps
filled. (No filling ever emitted a string that the longer Bomb gives more
nodes: the reasoning holds; it just applies to almost nothing.)

The same count answers a second hope: that where the other extension needs
one node more, the stored REV with one new node at the end of a free link
would be its REV. The link the control pointer takes in the next step was
free in none of the 4.9 million such cases at 26 bits.

## 2. If every REV were known beforehand

Suppose something supplied, for free, a REV of the right size for every
string of X bits. The lower bounds would still have to be proven: a pass at
budget N for the strings that have no REV of N nodes, which finds nothing.
`proof_only` builds a level both ways with the SharedSolver and counts the
steps; the longer Bomb is the oracle for which strings those are.

| Level | Strings searched | need what their first bits need | need more | Steps, as built | Steps, proofs only | Left |
|---:|---:|---:|---:|---:|---:|---:|
| 18 | 65,536 | 37,007 | 28,529 | 319,942,697 | 111,246,455 | 34.8% |
| 20 | 262,144 | 152,299 | 109,845 | 2,563,737,705 | 1,085,723,383 | 42.3% |
| 22 | 1,048,576 | 709,932 | 338,644 | 11,509,625,738 | 5,086,723,030 | 44.2% |
@@PROOF24@@

By pass, level 22:

| Budget | Searched | Found | Steps | Proofs only: strings | Steps |
|---:|---:|---:|---:|---:|---:|
| 6 | 188,563 | 14,623 | 173,786,411 | 173,940 | 172,241,831 |
| 7 | 1,009,543 | 833,488 | 5,583,276,572 | 176,055 | 4,881,742,910 |
| 8 | 200,393 | 200,392 | 5,742,570,339 | 1 | 26,719,798 |

A pass that finds nearly everything (budget 8) is nearly saved. A pass
after which a sixth of its strings are still open (budget 7) costs 87% of
itself for that sixth alone. A perfect source of REVs is worth a factor of
two to three and no more, and what it leaves is the part that grows.

This also bounds from above everything that finds REVs cheaply: the
prepend construction, a search begun at the REV of the Bomb below, repairs
of a neighbor's REV. None of them can be worth more than this column, and
they would change which REV a table stores.

## 3. Where the states are

The steps of a level, by how many bits had been emitted (the "states" are
the steps at which the search is about to emit a bit; a level built on the
Bomb below, all budgets together; level 22 as one job, level 24 as eight):

| Bits emitted | Level 22: states | above this depth | Level 24: states | above this depth |
|---:|---:|---:|---:|---:|
| 8 | 66,958,036 | 1.2% | 435,977,731 | 0.7% |
| 12 | 314,489,381 | 25.4% | 2,287,928,292 | 24.9% |
| 13 | 323,181,086 | 36.5% | 2,302,399,008 | 35.9% |
| 16 | 246,681,134 | 68.9% | 1,656,158,395 | 66.5% |
| 20 | 86,528,859 | 95.3% | 735,176,352 | 91.1% |
| X − 2 | 86,528,859 | 95.3% | 366,804,172 | 97.3% |
| X − 1 | 46,703,847 | 98.3% | 202,433,500 | 99.0% |
| all | 2,827,145,152 | | 20,819,626,920 | |

A frontier is the set of states at one depth; a search that started from a
stored frontier would skip everything above it.

- **At the last depth** a frontier would save 98 to 99% of the walk: 45 to
  48 states for every string searched, where the whole walk is 2,700 to
  5,000. It is also 47 and 202 million partly decided graphs, several
  times the Bomb of that length. And it is the frontier of *this* level:
  the states that emit X − 1 bits while one of the two extensions is still
  wanted. The level above needs the states that emit X bits, for strings
  this level stopped looking at when it found their first REV. A level
  does not pass through the frontier the next one needs; producing it is
  the next level's own search.
- **Near the top** the walk does not depend on the level: it is the tree of
  all partly decided graphs of N nodes, to that depth. That part could be
  stored once and reused by every longer level. It is also the cheap part,
  a quarter of the states above depth 12, and a smaller share with every
  bit of length.
- The states at a given depth are not the same from level to level even
  near the top (six to seven times more at depth 8 for 24 bits than for
  22): the count follows which budgets are busy, and at 24 bits the 8-node
  pass is.

There is no depth at which a frontier is small, reusable and worth much.

## 4. What the data already has

For every string q and each of its two extensions, whether the extension
needs more nodes than q: that is the difference between two consecutive
Bombs, and the compact format keeps it for every length in one file
(`carlobomb bomb compare --file COMPACT --target q` answers it from the
navigation bits). At 26 bits 4,935,876 of 16,777,216 strings q have an
extension that needs more nodes than q; at 22 bits, 338,644 of 1,048,576. A
rule that predicted those from q, or from the shorter strings inside q,
would be a lower bound of exactly the kind section "Where it stands" says
is missing, and every Bomb built so far is a table to test it against.

Two bounds of that kind are known and measured (`benchmarks/SUFFIX_BOUND.md`
in pull request #10): a string needs at least what its first X − 1 bits
need, which is where every search begins, and at least one node less than
its last X − 1 bits need, which decides 0.1% of the proofs.

## Not measured yet

- `proof_only` at 25 and 26 bits, where the 9-node pass takes over.
- Whether the strings that need one node more than their beginning cluster
  under common beginnings. If they do, the proofs could be confined to
  parts of the tree; the observation above says they are spread widely
  enough to keep the top alive, but not how the deeper levels divide.
- Bounds from shorter strings other than a beginning and an end: a string
  with a hard stretch in its middle is bounded by it less one node for
  every cut (the every-stretch bound, measured: a tenth more than the
  suffix bound), and nothing else has been tried.
- Whether the *number* of REVs of q (how many graphs of M(q) nodes emit it)
  says anything about its extensions. A level could count them for the
  strings whose first REV it finds late; that would be information a
  construction has and a Bomb does not.
