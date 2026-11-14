# Shared search: one search for all strings of a length

An experiment. It builds nothing the project certifies: no journal, no
table. It measures what the ladder's search costs when it is not repeated
for every string, and checks the answer against tables the construction has
built. `benchmarks/DISCOVERY.md` has the measurements and what they mean.

## The idea

The LevelSolver proves "no REV of N nodes emits S" by trying, in a fixed
order, every way to decide the links and colors the machine meets while it
emits S. A branch dies when the bit it emits is not the next bit of S.

Two strings that begin alike are searched alike for as long as they agree.
The ladder searches every string by itself, so it walks the common part
once for each of them: at 24 bits, 4 million searches of 5 million steps
each, most of those steps in the first 16 bits.

`shared_search` walks it once. It has no target. A branch lives as long as
*some* string that is still wanted begins with what the branch has emitted,
and where the reference gives an uncolored node the target's bit, both bits
are tried. Seen from any one string, the states it visits are the states of
the reference search for that string, in the same order. So:

- the first REV it finds for a string is the REV the LevelSolver finds;
- a pass that ends without a REV for a string has gone through everything
  the LevelSolver would have gone through to say PROVEN_UNSAT.

One pass per node budget N, smallest first. Wanted in pass N: the strings no
smaller budget found. A string that is found is no longer wanted from that
moment, and a branch that only it kept alive dies.

## Jobs

The strings of X bits with one beginning of P bits are a class
(`--prefix-bits P`). A class is a job: all its passes, on its own strings.
Jobs do not depend on one another or on the order they run in, so the
outcome is the same with any number of processes (`--jobs J`), and a job is
the unit that would be journaled. Cutting the work into 256 classes costs 4%
at 24 bits: the first few bits of the search are repeated for each class.

Only strings that start with 0 are searched: one of each complement pair.

## A level of the ladder

With `--base BOMB`, a complete Bomb for X − 1 bits, the experiment does
what a level of the ladder does: the REV stored for the first X − 1 bits
settles one of the two strings that begin with them, and the other is wanted
from its lower bound on (its first bits, or its last bits less one node).
The table that comes out is the table the ladder writes, entry for entry.

## Running it

```bash
build/shared_search --bits 20 --check ladder/fixed-run-20.frb
```

```bash
build/shared_search --bits 24 --prefix-bits 9 --jobs 4 --base ladder/fixed-run-23.frb --check ladder/fixed-run-24.frb
```

```bash
build/shared_search --bits 27 --prefix-bits 12 --sample 64 --jobs 4
```

`--check` compares every string with a complete Bomb of X bits: its node
count, and its REV link for link and color for color. `--sample N` runs N
classes chosen at random and scales the steps to all of them. `--depths`
prints how many states there were at each number of bits emitted.

Memory: two bytes per string of X bits that starts with 0, shared by the
processes, and 2^X / 4 bytes per process.

## What it is not

- Not a construction. Nothing is written; an interrupted run is lost.
- Not fast code. It is the reference search in plain C with one more case,
  at about 1.6·10^8 steps a second where the assembly kernels make 7 to 9.
- Not evidence. The rule of evidence it would need ("a completed pass at
  budget N excludes N for every string of the class it did not produce") is
  proposed in `benchmarks/DISCOVERY.md`, not adopted.

## Test

`shared_search_small` (ctest) builds a 12-bit Bomb with the ordinary
construction by search alone, and a 13-bit level on it with the ladder, and
requires the experiment to find the same node count and the same REV for
every string of both, in one class and in several, in one process and in
several.
