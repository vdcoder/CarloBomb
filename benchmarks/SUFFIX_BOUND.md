# The suffix bound: what it is worth

The ladder searches a string from the minimum of its first L − 1 bits
upward. The suffix theorem gives a second bound from the Bomb below: if n
nodes emit S, n + 1 nodes emit S without its first bit, so

    LB(S) = max( m(first L−1 bits of S), m(last L−1 bits of S) − 1 )

This is now a rule of evidence of the construction (`FORMAT.md`, section
9.5). This report says what it saves. It was measured before the rule was
written, on finished levels, and afterwards by building levels with it.

**In one line: the rule is true, costs nothing, changes no table, and saves
one search in a hundred and one logical step in a thousand.**

Measured on 14 November 2026 in the development container (4 virtual CPUs,
Intel Xeon under KVM, GCC 13.3 `-O3`, engine `amd64-auto`), on the ladder of
17 to 24 bits built there with the program of the 24-bit run.

## Counted on finished levels

`benchmarks/ladder_retro` reads the journal of a finished level and the
Bomb it was built on. The journal has every search that was made, with its
steps and its time; the searches at budgets below the bound are the ones
the rule would not have made. Nothing is estimated.

| Level | Searches made | below the bound | | Logical steps | in those searches | | Strings that begin higher | of them, bound = minimum |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 17 | 44,810 | 518 | 1.2% | 4,778,891,950 | 5,763,365 | 0.12% | 473 | 42 |
| 18 | 96,250 | 1,023 | 1.1% | 16,902,096,386 | 24,173,356 | 0.14% | 939 | 137 |
| 19 | 199,403 | 2,219 | 1.1% | 47,431,541,189 | 72,089,347 | 0.15% | 2,065 | 248 |
| 20 | 378,045 | 4,138 | 1.1% | 142,813,952,259 | 154,508,943 | 0.11% | 3,895 | 359 |
| 21 | 699,890 | 6,228 | 0.9% | 568,481,317,406 | 329,091,986 | 0.06% | 5,854 | 403 |
| 22 | 1,408,471 | 9,870 | 0.7% | 2,356,037,716,175 | 1,298,802,272 | 0.06% | 9,333 | 1,011 |
| 23 | 3,052,702 | 21,990 | 0.7% | 7,720,456,914,039 | 6,146,153,034 | 0.08% | 20,981 | 3,521 |
| 24 | 6,273,837 | 51,118 | 0.8% | 20,867,501,480,268 | 19,368,798,269 | 0.09% | 49,076 | 6,948 |

At 24 bits the rule spares 30 seconds of 29,864 seconds of solver time.

**Why so little.** About 1.2% of the searched strings begin above what
their first bits need. For most of them the bound is still below the
minimum, so the search that costs, the last one that finds nothing, is made
all the same; what is spared are the searches below it, each a small
fraction of that one. Only where the bound is the minimum itself (6,948
strings at 24 bits, one in 600) does a string lose its whole proof, and
those are strings whose proof was cheap: their last bits need more nodes
than they do.

**The bound holds.** Every string of every level was checked against it:
16,711,680 strings of 17 to 24 bits, none with a bound above its certified
minimum, and no search that found a REV below it. The test suite checks it
against the MinimalSolver for every string of up to 11 bits on a base of
every shorter length.

**Every cut instead of one.** The theorem holds for any number of bits cut
from the front, and a Bomb knows the minimum of every shorter string. The
bound over all cuts (the one `rev_fixed_bomb_lower_bound` computes) was
evaluated as well:

| Level | Searches below the bound, one cut | every cut | Steps, one cut | every cut |
|---:|---:|---:|---:|---:|
| 20 | 4,138 | 4,483 | 154,508,943 | 173,460,889 |
| 22 | 9,870 | 10,521 | 1,298,802,272 | 1,375,755,386 |
| 24 | 51,118 | 55,266 | 19,368,798,269 | 21,134,322,011 |

A tenth more of a thousandth. It needs the minima of the shorter strings in
memory, which the construction does not otherwise hold, and is not
implemented: the rule as built uses the stretches of full base length, which
on the ladder is the one cut of the formula above.

## Built with it

Levels 17 to 21 were built again with the rule, from the same 16-bit Bomb,
and compared with the ladder built without it.

| Level | Searches, without / with | Logical steps, without / with | Steps saved | Table |
|---:|---:|---:|---:|---|
| 17 | 44,810 / 44,292 | 4,778,891,950 / 4,773,128,585 | 5,763,365 | the same file |
| 18 | 96,250 / 95,227 | 16,902,096,386 / 16,877,923,030 | 24,173,356 | the same file |
| 19 | 199,403 / 197,184 | 47,431,541,189 / 47,359,451,842 | 72,089,347 | the same file |
| 20 | 378,045 / 373,907 | 142,813,952,259 / 142,659,443,316 | 154,508,943 | the same file |
| 21 | 699,890 / 693,662 | 568,481,317,406 / 568,152,225,420 | 329,091,986 | the same file |

The searches and steps saved are, to the last digit, what the journals of
the ladder built without the rule said they would be. Each table is the
same file as before (`cmp`; the 21-bit one has SHA-256 `1581fb01…20ed8cb`,
as `OVERNIGHT.md` lists it). The difference in time, a thousandth, is far
below what two runs on this shared machine differ by, and is not claimed.

## What changes, and what does not

- **Tables: nothing.** The REV of a string is the first one the search finds
  at the budget that is the string's minimum. Neither that budget nor that
  search depends on where the searching began.
- **Journals: fewer records**, one for each budget the rule excludes.
- **Evidence.** A work directory says in the first line of its `config.txt`
  and in a `base_bounds` line whether it was started with the rule, and
  keeps to that. A directory made before the rule, or with `--suffix-bound
  off`, is read and continued without it, by this program as by the old one:
  no existing journal changes its meaning. A directory made with the rule is
  refused by a program that does not know it, which is necessary: the
  strings whose bound is their minimum have a REV in such a journal and no
  search below it.
- **`bomb status --target`** shows the stretch that gives the bound, and
  `bomb status` and the level report count the strings and the budgets.
- **Overhead:** one more byte read per string each time a journal is
  replayed, in a pass that already runs every REV of the base. Not
  measurable.

## The prepend construction as a witness

The brief also asks whether the REV that the prepend theorem constructs
(m(S) ≤ m(last L−1 bits) + 2: a new root, and a second new node where the
old root has the wrong color) could stand in for a search. Counted on the
same journals, for every string that was searched:

| Level | Strings searched | The prepend REV has the minimum size | Steps of the searches that found those | | of them: equal to the lower bound, no search needed at all |
|---:|---:|---:|---:|---:|---:|
| 20 | 262,144 | 38,583 | 7,256,024,789 | 5.1% | 17,375 |
| 22 | 1,048,576 | 131,472 | 53,052,291,109 | 2.3% | 34,944 |
| 24 | 4,194,304 | 814,526 | 1,110,357,728,558 | 5.3% | 274,451 |

So it would replace 2 to 5% of the logical steps, and it would change the
REVs of a fifth of the searched strings to ones the search does not find
first, which the compact format then counts as REVs in another form. It is
not implemented. `benchmarks/DISCOVERY.md`, on the branch of the search
experiments, puts it next to what else was measured; next to those it is
small.

## Reproducing

```bash
build/ladder_retro --work LADDER/work-24 --base LADDER/fixed-run-23.frb --witnesses
```

reads the journal of level 24 and the 23-bit Bomb (either format) and
prints the tables above for that level. It writes nothing. Level 24 takes
about a minute and 60 MB; level 26, with a journal of 2.1 GB, a few minutes.
