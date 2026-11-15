# Shared search: one search for all strings of a length

An experiment. It builds nothing the project certifies: it writes no
journal, and the tables it can write are for comparison only. It measures
what the ladder's search costs when it is not repeated for every string, and
checks what it finds against tables and journals the construction has built.

- `FULL_RUNS.md`: every string of 25 and of 26 bits, against the certified
  Bombs.
- `PROPOSAL.md`: what it would take to make this a construction that
  certifies and resumes.
- `benchmarks/DISCOVERY.md`: the audit it came out of.

## The idea

The LevelSolver proves "no REV of N nodes emits S" by trying, in a fixed
order, every way to decide the links and colors the machine meets while it
emits S. A branch dies when the bit it emits is not the next bit of S.

Two strings that begin alike are searched alike for as long as they agree.
The ladder searches every string by itself, so it walks the common part
once for each of them.

`shared_search` walks it once. It has no target. A branch lives as long as
*some* string that is still wanted begins with what the branch has emitted,
and where the reference gives an uncolored node the target's bit, both bits
are tried. Seen from any one string, the states it visits are the states of
the reference search for that string, in the same order. So:

- the first REV it finds for a string is the REV the LevelSolver finds;
- a pass that ends without a REV for a string has gone through everything
  the LevelSolver would have gone through to say PROVEN_UNSAT;
- the steps it makes while the bits emitted are a beginning of S are the
  steps of the LevelSolver's search for S, one for one.

One pass per node budget N, smallest first. Wanted in pass N: the strings no
smaller budget found. A string that is found is no longer wanted from that
moment, and a branch that only it kept alive dies.

## Jobs

The strings of X bits with one beginning of P bits are a class
(`--prefix-bits P`). A class is a job: all its passes, on its own strings.
Jobs do not depend on one another or on the order they run in, so the
outcome is the same with any number of processes (`--jobs J`) and any P.

Only strings that start with 0 are searched: one of each complement pair.

## A level of the ladder

With `--base BOMB`, a complete Bomb for X − 1 bits, the experiment does
what a level of the ladder does: the REV stored for the first X − 1 bits
settles one of the two strings that begin with them, and the other is wanted
from a lower bound on. `--bound` says which:

| `--bound` | A string begins at | Which ladder |
|---|---|---|
| `prefix` | what its first X − 1 bits need | the ladder |
| `suffix` | that, or one node less than its last X − 1 bits need | a ladder with the suffix rule of pull request #10 |
| `all` (default) | the most any stretch of it gives | none |

The table that comes out is the same under all three, and it is the table
the ladder writes, byte for byte (`--out`).

## What is checked

**Every REV, on the reference interpreter.** Each REV is kept as it was
recorded and, when the search is over, run on the interpreter of `core/rev`,
which shares nothing with the search. It must be a REV of the number of
nodes recorded, emit the string it is recorded for, visit every one of its
nodes on the way, and its complement must emit the complement. A run fails
if one does not. (`--no-verify` keeps no REVs, for samples of lengths whose
REVs do not fit in memory.)

**Every string resolved.** Strings left without a REV are counted, and a
run with any fails.

**The histogram.** `--expect 1:2,2:6,3:64,...` takes the histogram of a
certified Bomb and fails on any entry that differs.

**Every node count.** A run of all classes prints the node-count digest:
the SHA-256 of 2^X bytes, byte i the number of nodes found for the string
with index i (the string's bits as a binary number, first bit most
significant), i from 0 to 2^X − 1. `nodes_digest BOMB` prints the same for
a Bomb of either format, and `nodes_digest BOMB --bits L` for the strings
of a shorter length, which a Bomb also answers. Equal digests say the two
agree on the node count of every single string.

**Every REV of a table.** `--check BOMB` compares every string with a
complete Bomb of X bits in memory, link for link and color for color.
`--out FILE` (with `--base`) writes the fixed-width table of the level
through the library's writer, which runs every REV once more, and prints
its checksum; `--expect-table-crc` takes the checksum of a certified table.
**The file says of every entry that it is a certified minimum**, because
only then can its bytes be compared with a certified file's. The experiment
certifies nothing: such a file is for comparison, and belongs nowhere a
Bomb is taken from.

**Every search of a ladder's journal.** With `--count` the search counts
its steps by what had been emitted when they were made, and so knows, for
every string and budget, how many logical steps the LevelSolver takes for
that string at that budget: the number the ladder journals. `--journal
WORK` takes the work directory of a ladder level and requires every search
in its journal to have the same result and the same number of steps here.
`--expect-totals SEARCHES:STEPS` requires the two totals that the report
of a finished level prints. `journal_digest WORK` reads a journal by itself
and prints its searches, their steps and a checksum over every search;
`shared_search --count` prints the same three numbers, so a journal can be
compared with a run on another machine.

## What agreement means, and what it does not

A REV that runs is a proof that its string needs **at most** that many
nodes. Nothing else is needed to believe it, and every REV here is run.

That a string needs **no fewer** is another kind of claim: that a search
which found nothing was complete. A certified Bomb has, for every such
search, a journal record that says which search it was and how many steps
it took, and anyone can make that search again. This experiment writes no
such record. When its table is byte for byte a certified table, the
minimality of those entries is certified by the journal of the
construction that built the certified table, and by nothing here. What the
experiment adds is evidence about the *method*: that a search of another
shape arrives at the same REVs, and that it accounts for every search of
the ladder to the last step.

## Running it

A length from nothing, with the certified histogram:

```bash
build/shared_search --bits 25 --prefix-bits 10 --jobs 4 --expect 1:2,2:6,3:64,4:968,5:18126,6:426116,7:9671940,8:23375506,9:61704
```

A level of the ladder against its journal and its table:

```bash
build/shared_search --bits 24 --prefix-bits 10 --jobs 4 --base ladder/fixed-run-23.frb --journal ladder/work-24 --check ladder/fixed-run-24.frb
```

A sample of a length nobody has built:

```bash
build/shared_search --bits 28 --prefix-bits 12 --sample 64 --jobs 4 --no-verify
```

What a Bomb says, as one number:

```bash
build/nodes_digest ladder/fixed-run-24.frb
```

What a journal says, as three:

```bash
build/journal_digest ladder/work-24
```

`--sample N` runs N classes chosen at random and scales the steps to all of
them. `--class K` runs one. `--jobs-out FILE` writes one line for each pass
of each job: class, budget, wanted, found, steps. `--depths` prints how
many states there were at each number of bits emitted.

Memory: two bytes per string of X bits that starts with 0 and, unless
`--no-verify`, 36 more for its REV, shared by the processes; 2^X / 4 bytes
per process; with `--journal`, 16 bytes per search of the journal. 26 bits:
1.3 GB.

## What it is not

- Not a construction. It writes no journal; an interrupted run is lost.
- Not fast code. It is the reference search in plain C with one more case,
  at about 1.6·10^8 steps a second and process where the assembly kernels
  make 7 to 9.
- Not evidence, as said above.

## Test

`shared_search_small` (ctest) builds a 12-bit Bomb with the ordinary
construction by search alone, and a 13-bit level on it with the ladder. It
requires the experiment to find the same node count and the same REV for
every string of both, in one class and in several, in one process and in
several; to write the ladder's 13-bit table byte for byte, from each of the
three bounds; to reproduce every search of the ladder's journal with its
result and its number of steps; to print the digests and checksums that
`nodes_digest` and `journal_digest` print; and to fail on a histogram, a
total or a budget that is wrong.
