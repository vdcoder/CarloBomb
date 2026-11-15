# Faster discovery: where the ladder's work goes

The mission: reduce the time to construct FixedRunBomb(X) without touching
the certification of its minima. This report is the audit of the ladder as
it runs today, what was measured about each way to make it cheaper, and one
result that changes the order of magnitude.

Measured on 14 November 2026 in the development container (4 virtual CPUs,
Intel Xeon under KVM, 15.7 GB, GCC 13.3 `-O3`), on the ladder of 17 to 24
bits built there, with the owner's reports of levels 25 and 26 for the
lengths beyond. The shared search was afterwards run in full at 25 and 26
bits, with the CTO's approval: `experiments/shared-search/FULL_RUNS.md` has
those runs, and `experiments/shared-search/PROPOSAL.md` what it would take
to make the method a construction.

## In short

**The ladder searches every string by itself, and almost all of that work
is shared between strings.** A search that walks the common part once
builds the same table from a fraction of a percent of the steps.

| Bits | Ladder, this level alone: logical steps | One shared search, the whole Bomb from nothing | | How the second number was obtained |
|---:|---:|---:|---:|---|
| 20 | 142,813,952,259 | 2,558,807,314 | 1/56 | run in full; every string compared with the Bomb |
| 22 | 2,356,037,716,175 | 11,511,995,978 | 1/205 | run in full; every string compared |
| 24 | 20,867,501,480,268 | 84,837,784,898 | 1/246 | run in full; every string compared |
| 25 | 63,140,927,147,534 | 223,274,268,884 | 1/283 | run in full; the histogram of the certified Bomb, every entry |
| 26 | 257,971,379,632,128 | 428,024,955,093 | 1/603 | run in full; the histogram of the certified Bomb, every entry |
| 27 | about 1.06·10^15 (projected) | about 1.06·10^12 | 1/1,000 | 64 of 2,048 classes |
| 28 | about 3.8·10^15 (projected) | about 3.0·10^12 | 1/1,300 | 64 of 4,096 classes |

- **It is the same table.** At 16, 18, 20, 22 and 24 bits the shared search
  finds the node count of the Bomb for every string, 8,388,608 of 8,388,608
  pairs at 24 bits, and for every string of 6 nodes or more the same REV,
  link for link and color for color. Run as a level of the ladder, on the
  Bomb below, it writes the ladder's table byte for byte at every length
  from 17 to 24 bits, and at 25 and 26 bits a table with the checksum of the
  owner's certified one.
- **It accounts for every search the ladder made.** Counting its steps by
  string, it arrives at the result and the exact number of logical steps of
  every search in the journals of levels 17 to 24, and at the totals of the
  owner's levels 25 and 26 to the last digit (`FULL_RUNS.md`).
- **It is a prototype in plain C**, a quarter to a fifth as fast per step as
  the assembly kernels, and it still builds level 24 in 140 seconds on the 4
  processors that took two hours for it, and all of 26 bits from nothing in
  11 minutes, where the ladder's level 26 took 9 hours on the owner's
  machine.
- **It is not in the construction.** It writes no journal and certifies
  nothing. What it needs to become the construction is in `PROPOSAL.md`;
  the decision is the CTO's.
- **The other ideas, measured the same way, are small next to it**: starting
  a string's first search at the REV the Bomb below already has saves 17 to
  25% of the steps; a REV built from the last bits' REV instead of searched
  for, 2 to 5%; the suffix bound, 0.1% (implemented, its own pull request).
- **The kernels are not where the time is lost.** They spend 99.7% of the
  wall clock and do it at 1.3 ns a step. They are asked to take a thousand
  times too many steps.

## What a level costs today

From the owner's reports, 10 worker processes:

| Level | Searches | Logical steps | Solver time, all workers | Wall clock | Share of wall clock in the solver | ns a step |
|---:|---:|---:|---:|---:|---:|---:|
| 25 | 11,833,078 | 63,140,927,147,534 | 82,376.8 s | 8,266.7 s | 99.65% | 1.30 |
| 26 | 21,924,419 | 257,971,379,632,128 | 331,904.5 s | 33,267.5 s | 99.77% | 1.29 |

Everything that is not the search kernel (replaying the journal, running
the base Bomb's REVs, planning, the journal's writes, writing and checking
the table) is under 0.4% of a level together. Nothing done to those parts
can be seen in the total, and none of them was changed.

Where the steps go, from the journal of level 24 built here (read with
`ladder_retro`, which reads a journal and writes nothing; the tool is in
pull request #10, with the suffix bound it was first written to measure):

| Node budget | Result | Searches | Logical steps | | ns a step | Steps a search |
|---:|---|---:|---:|---:|---:|---:|
| 6 | none (proof) | 206,840 | 100,372,659,795 | 0.5% | 1.58 | 485,267 |
| 7 | a REV found | 1,125,407 | 2,934,608,701,220 | 14.1% | 1.46 | 2,607,597 |
| 7 | none (proof) | 1,861,730 | 11,351,210,684,395 | 54.4% | 1.45 | 6,097,130 |
| 8 | a REV found | 3,061,990 | 6,434,958,533,683 | 30.8% | 1.38 | 2,101,560 |
| 8 | none (proof) | 899 | 41,748,215,437 | 0.2% | 1.32 | 46,438,504 |
| 9 | a REV found | 921 | 2,809,915,233 | 0.0% | 1.05 | 3,050,939 |

By the place of a search in its string's sequence, with P the string's
first L − 1 bits and M(P) their minimum:

| | Level 20 | Level 22 | Level 24 |
|---|---:|---:|---:|
| First budget, M(P): a REV found | 43.7% | 47.8% | 29.8% |
| First budget, M(P): none (proof) | 35.3% | 39.4% | 52.5% |
| A later budget: a REV found | 18.6% | 9.7% | 15.1% |
| A later budget: none (proof) | 2.4% | 3.2% | 2.6% |
| Strings searched once / twice / three times or more | 58% / 40% / 2% | 68% / 30% / 2% | 53% / 45% / 2% |

Four fifths of the work is the first search of each string.

## The audit, ranked

| Component | Share of a level | Verdict |
|---|---|---|
| Search kernel, logical steps | 99.7% of wall clock | The number of steps is the problem, not the cost of one |
| Search kernel, cost of a step | 1.29 ns on the owner's machine, 1.05 to 1.6 here by budget | Already 3.3 to 3.9 times the C solver. Earlier work tried packed nodes, register layouts, frames of 8 bytes, call/ret, bulk accounting in both link loops, and kept what won (`ASM_RESULTS.md`, `REPR_RESULTS.md`). A vector kernel across searches is designed, not built, estimated 2 to 3 times (`MULTI_SEARCH_SIMD.md`) |
| Node budgets searched for a string | 1.4 to 1.5 a string | The suffix bound removes 0.8% of them, the cheap ones |
| Scheduling, workers | 10 workers at 99.7% | Nothing to gain |
| Journal | 96 bytes a search; 1.1 GB and 2.1 GB at 25 and 26 bits | Under 0.1% of the time. Read back in 23 s at 24 bits |
| Base Bomb run, replay, table written and read back | A few minutes a level | Under 0.4% |
| Duplicate computation | **Over 99% of the steps** | See the next section |

No instruction-level profile of the 8-node kernel was taken. This machine
has no hardware counters and no `perf`, and the earlier experiments put what
is left in the kernel at tens of percent; the finding below is two to three
orders of magnitude and does not use that kernel.

## The duplicate computation: one search for all strings

### What the ladder repeats

The LevelSolver proves that no REV of N nodes emits S by running the
machine on a graph of which nothing is decided, trying in a fixed order
every link and color the machine meets undecided, and giving up a branch
when the bit it emits is not the next bit of S. Two strings that begin alike
are searched alike for as long as they agree. Where is the work? At 24
bits, the states of the search by the number of bits emitted so far:

    bits emitted     states (all strings together, each state once)
         8             435,977,731
        12           2,288,840,140
        13           2,302,891,286     the most
        16           1,656,372,268
        20             735,860,403
        23             202,433,502

Three quarters of the states lie in the first 16 bits. A state after 13
bits is common to the 2,048 strings of 24 bits that begin with those 13.
The ladder visits it once for every one of them that it searches.

### The shared search

`experiments/shared-search/shared_search.c` is the LevelSolver's search with
one change: it has no single target. A branch lives as long as some string
that is still wanted begins with what the branch has emitted; where the
reference gives an uncolored node the color of the target's bit, each bit is
tried. One pass per node budget N, smallest first; wanted in pass N are the
strings no smaller budget found, and a string that is found is not wanted
from that moment.

**Why it proves the same.** Take a string S and the states the shared
search visits whose emitted bits are a beginning of S. As long as S is
wanted, every one of S's beginnings is wanted, so no branch is given up that
the reference search for S would follow, and at a node without a color the
branch for S's bit is among those taken. Those states are therefore exactly
the states of the reference search for S at budget N, in the reference's
order. Two things follow. The first branch that emits all of S is the first
REV the LevelSolver finds for S at that budget. And if the pass ends with S
still wanted, the reference search for S has been walked to its end without
a REV: that is PROVEN_UNSAT for S at N. The shared search adds no way to
find a REV and no way to miss one; it only stops visiting a state once for
each string below it.

**Checked, not only argued.**

| Bits | Strings (one of each complement pair) | Node count as in the Bomb | REV as in the Bomb | Other REVs |
|---:|---:|---:|---:|---|
| 16 | 32,768 | 32,768 | 25,569 | 7,199, all of 5 nodes or fewer |
| 18 | 131,072 | 131,072 | 123,907 | 7,165, all of 5 nodes or fewer |
| 20 | 524,288 | 524,288 | 517,133 | 7,155, all of 5 nodes or fewer |
| 22 | 2,097,152 | 2,097,152 | 2,090,000 | 7,152, all of 5 nodes or fewer |
| 24 | 8,388,608 | 8,388,608 | 8,381,457 | 7,151, all of 5 nodes or fewer |
| 20, on the 19-bit Bomb | 524,288 | 524,288 | 524,288 | none |
| 24, on the 23-bit Bomb, 256 classes, 4 processes | 8,388,608 | 8,388,608 | 8,388,608 | none |

The REVs that differ from the Bomb's when the search starts from nothing are
those the 16-bit Bomb at the foot of the ladder took from its enumeration of
graphs of up to 5 nodes, which is another order than the search's. Run as a
level of the ladder, those strings come from the base as they do in the
ladder, and the table is the ladder's table.

At 25 and 26 bits, run in full, it finds the histogram of the owner's
certified Bombs in every entry: 61,704 strings of 9 nodes at 25 bits;
54,455,992 of 8, 1,187,852 of 9 and the 2 of 10 at 26. No string is left
without a REV, and every REV runs on the reference interpreter.

### What it costs

By node budget, the whole 24-bit Bomb from nothing:

| Budget | Strings wanted | Found | Steps |
|---:|---:|---:|---:|
| 6 | 8,379,047 | 209,251 | 198,135,821 |
| 7 | 8,169,796 | 3,905,704 | 7,474,111,741 |
| 8 | 4,264,092 | 4,263,149 | 75,881,055,179 |
| 9 | 943 | 943 | 1,278,324,141 |
| all | | 8,388,608 | 84,837,784,898 |

About 10,000 steps a string, where the ladder's level 24 took 5 million for
each string it searched. The cost grows by a factor of 2.1 to 2.8 a bit
between 24 and 28 bits, close to the number of strings; the ladder's grows
by 3 to 4.

Time, on this machine:

| | Ladder, assembly kernels, 4 workers | Shared search, plain C, 4 processes |
|---|---:|---:|
| Level 24 | 29,864 s of solver time, about 2 hours | 140 s |
| Steps a second, one process | 7·10^8 | 1.6·10^8 |

The full runs took 343 seconds at 25 bits and 643 at 26, on these 4
processors. At that rate 27 bits would be about half an hour and 28 an hour
and a half here; those two are the sampled step counts divided by a rate,
and no run of those lengths stands behind them.

### What it would take to make it the construction

`experiments/shared-search/PROPOSAL.md`. It replaces the sketch that stood
here, in one point above all: the sketch asked for a new rule of evidence
("a finished job excludes N for every string it did not find"). None is
needed. The shared search can write, for every string and budget, the
record the ladder writes, with the LevelSolver's own number of steps, and
the journal that results is read by the rules there are.

## Starting a search where the last one ended

Measured, not built, and made unnecessary by the shared search. Kept here
because it is the best that can be done for the ladder as it is.

The REV the Bomb below stores for P is the first REV the search finds for P
at budget M(P). The ladder's first search for S = P followed by the other
bit, at that same budget, goes through everything the search for P went
through before it found that REV, and nothing there can be a REV for S. It
could start at the stored REV: follow it as far as it goes, then go on as
the search does.

What that saves was counted from the journals, level by level: the steps of
the search that found each string's REV are in the journal of the level that
found it, and the first search of each of its extensions repeats them.

| Level | First searches with a known start | Steps they would not repeat | Of the level's steps |
|---:|---:|---:|---:|
| 18 | 32,768 | 2,855,312,234 | 16.9% |
| 20 | 229,376 | 30,915,163,890 | 21.6% |
| 22 | 1,015,808 | 524,221,557,373 | 22.3% |
| 23 | 2,064,384 | 1,877,406,591,959 | 24.3% |
| 24 | 4,161,536 | 5,167,601,906,680 | 24.8% |

Not one of the 8.1 million first searches of levels 18 to 24 took fewer
steps than the search it would have skipped, which is what the argument
requires. It would need a
way into the kernels at a given graph, a new kind of search record (the
steps differ), and the knowledge, for every REV of the base, that it is the
first in search order, which the table does not carry.

## A REV without a search

For a string S that the ladder searched, counted against the REV the search
found (`ladder_retro --witnesses`):

| | Level 20 | Level 22 | Level 24 |
|---|---:|---:|---:|
| The stored REV of the first bits emits the other bit, but by a link it has never followed before: pointed elsewhere, it emits S | 90 strings | 199 | 526 |
| in how many of those it is the REV the search found | all | all | all |
| A REV from the REV of the last L − 1 bits: one new root, or two new nodes | | | |
| of the minimum size | 38,583 (14.7%) | 131,472 (12.5%) | 814,526 (19.4%) |
| steps of the searches that found those strings' REVs | 5.1% | 2.3% | 5.3% |
| of the minimum size and equal to the lower bound: no search at all | 17,375 | 34,944 | 274,451 |

The first is one string in 8,000: a minimum-node REV has no link to spare.
The second replaces a few percent of the steps and puts REVs into the table
that are not the first the search finds.

## Companion data from Bomb(X − 1)

What could be kept beside a Bomb to make the next one cheaper. Each was
measured on finished levels or follows from a measurement above.

| Candidate | Cost to compute | Storage | Derivable from what exists? | Saving, ladder as it is | With the shared search |
|---|---|---|---|---:|---|
| Lower bound from the last bits (the suffix bound) | none | none | yes, from the base Bomb | 0.1% of steps | used; no effect on cost |
| The same over every cut | none | 1 byte a string in memory | yes | 0.11% | not needed |
| "This REV is the first in search order" | none: the construction knows where each REV came from | 1 bit a string | not from the table; from the journals of all levels below | enables the 17 to 25% above | not needed |
| Steps of the search that found each REV | none | 4 bytes a string | from the journals | nothing by itself: it measures the saving above | not needed |
| A second minimum-node REV with the other next bit | a search: it is the next level's first search, moved | 5 to 6 bytes a string as a rank | no | none: the work is the same, earlier | not needed |
| The same from the prepend construction | negligible | none | yes, from the base | 2 to 5% of steps, other REVs in the table | not needed |
| Links a stored REV never follows | none | none | yes, by running the REV | under 0.01% | not needed |
| Failed parts of a search, as constraints for the next | not estimated | not estimated | no | not measured | this is what the shared search is, without storing anything: the part two strings have in common is walked once |

The last column is the finding. Built on the Bomb below, the shared search
takes 2,596,446,091 steps for level 20; from nothing, 2,600,985,781. The
base Bomb saves it 0.2%. What a level needs from the level below is so
little that there is nothing left for a sidecar to carry, and the ladder
itself becomes a convenience (each level a certified Bomb on the way)
rather than a saving. No sidecar format is proposed.

## Recommendation

1. Merge the suffix bound if it is wanted for its own sake: it is correct,
   tested, changes no table, and is worth a thousandth (pull request #10
   and its `SUFFIX_BOUND.md`). Nothing in this report or in the experiment
   depends on it.
2. Do not build the seeded search or the prepend witness. They are the best
   improvements of the per-string search and are worth a quarter and a
   twentieth of a number that the shared search divides by hundreds.
3. Decide on the shared search as the construction of a level. The runs
   in full at 25 and 26 bits that this report asked for have been made
   (`FULL_RUNS.md`): the certified histograms, the certified tables'
   checksums, the ladder's searches to the last step. What remains is the
   proposal (`PROPOSAL.md`).
4. Leave the running ladder alone. Bomb(27) is certified by the journal it
   is writing; a table from a new method should be compared with it, not
   replace it.

## Reproducing

```bash
build/shared_search --bits 22 --check LADDER/fixed-run-22.frb
```

```bash
build/shared_search --bits 24 --prefix-bits 9 --jobs 4 --base LADDER/fixed-run-23.frb --check LADDER/fixed-run-24.frb
```

```bash
build/shared_search --bits 27 --prefix-bits 12 --sample 64 --seed 7 --jobs 4
```

```bash
build/shared_search --bits 24 --prefix-bits 10 --jobs 4 --base LADDER/fixed-run-23.frb --journal LADDER/work-24
```

The first takes 70 seconds on one processor, the second 140 seconds on
four. The third compares every search of the level's journal with the
shared search and prints the ladder's searches by budget and result, in
three minutes. The other tables of the audit (the place of a search in its
string's sequence, the seeded search, the REVs without a search) were read
from the journals with `ladder_retro`, which is in pull request #10.
