# FixedRunBomb

A FixedRunBomb is a table with one entry for every binary string of one fixed
length L (1 to 24 bits). Each entry holds a REV that emits the string, packed
into 19 bytes (up to 16 bits) or 34 bytes (17 to 24 bits), and says whether
its node count is certified to be the minimum. The entry of a string is
found by arithmetic on the string alone.

This directory holds the file format, the software that builds a table, and
the checks. `carlobomb bomb` is the command-line front end.

For L = 16 the table has 65,536 entries and is 1,245,248 bytes. Built here,
every one of its entries is a certified minimum, and the largest is 7 nodes.
The measurements, and the commands to reproduce the table, are in
[`benchmarks/FIXED_RUN_16.md`](../../benchmarks/FIXED_RUN_16.md).

Tables for 17 to 24 bits are built as a **ladder**, each from the complete
table for one bit fewer ([below](#longer-tables-the-ladder)). How to run the
ladder to 24 bits, and how to check the result, is in
[`OVERNIGHT.md`](OVERNIGHT.md); what it costs, in
[`benchmarks/FIXED_RUN_24_FEASIBILITY.md`](../../benchmarks/FIXED_RUN_24_FEASIBILITY.md).

| File | Content |
|---|---|
| [`FORMAT.md`](FORMAT.md) | Specification of the table file and of the construction journal |
| `fixed_run.h`, `fixed_run.c` | Entries, table, file: packing, reading, writing, validation. Plain C11 |
| `enumerate.c` | Exhaustive enumeration of labeled REVs. Plain C11 |
| `fixed_run_build.h` | Interface of the construction code below. POSIX |
| `journal.c` | Journal segments and records; the replay that derives the state of a construction and decides what is certified |
| `construct.c` | Work directory, job planning, worker processes, resume, finalize, recomputation |
| `statistics.c` | The reports of `bomb status` |
| `tests/test_fixed_run.c` | Tests of all of the above |
| `tests/test_fixed_run_ladder.c` | Tests of format version 2 and of the ladder |
| [`OVERNIGHT.md`](OVERNIGHT.md) | Running the ladder to 24 bits unattended, and checking the result |
| `MULTI_SEARCH_SIMD.md` | Design note on a possible vectorized search. Not implemented |

## Building the 16-bit table

```bash
cmake -S . -B build
```

```bash
cmake --build build -j
```

```bash
build/carlobomb bomb build --bits 16 --max-nodes 16 --engine amd64-auto --workers 8 --work-dir bomb-work
```

```bash
build/carlobomb bomb finalize --work-dir bomb-work --output fixed-run-16.frb
```

```bash
build/carlobomb bomb verify --file fixed-run-16.frb --work-dir bomb-work --recertify
```

```bash
build/carlobomb bomb lookup --file fixed-run-16.frb --target 0100100011010110
```

Set `--workers` to the number of processor cores. `--engine` may be left out:
the default is `amd64-auto` where this build and this processor have it and
the C engine elsewhere, and a named assembly engine that is not available
falls back to C with a notice. All engines return the same results, graphs
and step counts, so the choice affects speed only.

To stop a build, press Ctrl-C, or kill it, or lose power. To go on:

```bash
build/carlobomb bomb resume --work-dir bomb-work
```

To see where a construction stands, at any time, also while it runs:

```bash
build/carlobomb bomb status --work-dir bomb-work
```

## Commands

| Command | Does |
|---|---|
| `bomb build --bits L --work-dir DIR` | Creates the work directory and builds until done or stopped |
| `bomb resume --work-dir DIR` | Continues a construction from its journal |
| `bomb status --work-dir DIR` | Settings, both stages, the minimum-node histogram, search effort, sizes |
| `bomb status --work-dir DIR --target S` | One target: status, witness, what excludes smaller node counts, and every journal record about it |
| `bomb finalize --work-dir DIR --output FILE` | Writes the table. Refuses, writing nothing, unless every target has a certified minimum |
| `bomb finalize ... --partial` | Writes the table as it stands, without the complete flag |
| `bomb verify --file FILE` | Checks structure, checksums and header, runs every stored graph, checks complement symmetry |
| `bomb verify ... --work-dir DIR` | Also checks every certified minimum against the journal |
| `bomb verify ... --recertify` | Also recomputes every minimum from node budget 1 with the MinimalSolver (`--engine`, `--sample K`, `--max-steps K`) |
| `bomb verify ... --against SHORTER.frb` | Also checks the table against a complete table for fewer bits, and reports the hardest strings |
| `bomb lookup --file FILE --target S` | Prints the stored REV after validating and running it; `--out GRAPH` writes it in the REV text format |
| `bomb ladder --dir DIR --to L [--from FILE]` | Builds complete tables for every length up to L, each on the one before; run again to continue |
| `bomb ladder --dir DIR --status` | The tables a ladder directory holds, and the level under construction |

Settings of `build`:

| Option | Default | Meaning |
|---|---|---|
| `--bits L` | required | Target length, 1..24 |
| `--base FILE` | none | Build on this complete table for fewer bits; turns enumeration off |
| `--max-nodes N` | 16, or 24 above 16 bits | Largest node budget searched. Changeable on resume |
| `--engine E` | `amd64-auto`, else `c` | LevelSolver engine. Changeable on resume |
| `--workers W` | 1 | Worker processes. Changeable on resume |
| `--enumerate-max K` | 5 | Exhaustive enumeration for node counts 1..K; 0 = none, 6 is the most allowed |
| `--symmetry on\|off` | on | Work on one string of each complement pair and derive the other |
| `--fallback ring\|none` | ring | Let the ring stand in as a witness until a better one is known. Changeable on resume |
| `--max-steps K[,K...]` | 0 | Step limit per LevelSolver call; 0 = none. Several values make several passes (below). Changeable on resume |

Options of `build` and `resume` that affect one session only: `--sample K
[--seed S]` searches only K randomly chosen open targets (a pilot run);
`--limit-jobs K` stops after K jobs; `--time-limit SECONDS` stops at the
first job boundary after that time; `--progress SECONDS` sets the interval
of progress lines (0 = one line per round); `--min-free MIB` stops the
session cleanly when the file system has less free space than that;
`--quiet` prints nothing. `bomb ladder` takes the same, and `--workers`,
`--engine`, `--max-steps` and `--max-nodes` for the levels it builds.

Exit status: 0 success and, where the question arises, a complete certified
table; 1 a check failed or the construction broke; 2 usage, invalid input or
an incompatible work directory; 3 valid but not complete.

## How a table is built

**Stage A, exhaustive enumeration.** For node counts 1, 2, … up to
`--enumerate-max`, every labeled REV with that many nodes is run on the
interpreter of `core/rev` for L bits, and the string it emits is recorded
with the graph if it is new. This is counting loops and the interpreter, with
no search code in it. When all graphs of n nodes have been run, every string
they emit has a witness, and every other string is known to need more than n
nodes. The graphs of n nodes are cut into n² parts so that the work can be
spread over processes and resumed part by part.

**Stage B, search.** For each string Stage A left open, the LevelSolver is
run at the first node budget that is not excluded, then the next, until it
finds a REV. A PROVEN_UNSAT result excludes that budget; the first SOLVED
result is then a minimum, since every smaller budget is excluded.

**Complement symmetry.** Flipping every color and exchanging the two links
of every node turns a REV for a string into a REV for the complement string
with the same node count. With `--symmetry on` Stage A runs only graphs with
a white root and Stage B searches only strings that start with 0; the other
half is derived, and each derived graph is run before it is accepted. The
test suite checks the transformation on every labeled graph of up to 4 nodes
and on random larger ones. `--symmetry off` works on every string and is a
check of the shortcut: the two constructions certify the same minima.

**The ring.** A ring of L nodes, each linked to the next with both links,
emits any L-bit string when colored with it. So no L-bit string needs more
than L nodes, every string has a valid witness before any search has run,
and no 16-bit string overflows a 16-node entry. With `--fallback ring` the
table uses the ring until something smaller is known; it is never called a
minimum unless every smaller budget has been excluded.

**Step limits.** `--max-steps` bounds each LevelSolver call. A search that
hits the limit is recorded as RESOURCE_LIMIT and excludes nothing; the
string stays open with whatever was proven before. A later session, or a
later pass of the same one, retries it only with a larger limit:
`--max-steps 1000000,100000000,0` makes three passes, the last without
limit. Budgets already excluded are never searched again.

**Workers.** With `--workers W` each round forks W processes. Job i of the
round goes to worker i mod W, each worker writes its own journal segment,
and the coordinating process only waits, prints progress and replays. The
LevelSolver's static search state is never shared, and no more processes
are started than requested. `--workers 1` runs everything in one process.

## What is certified, and on what

A string is marked MINIMAL when a witness has exactly as many nodes as the
first node budget that is not excluded. A budget is excluded only by a
completed enumeration that did not produce the string or by a PROVEN_UNSAT
search result. Nothing else counts: not a step limit, not a time limit, not
an interrupted search. The rules are in one function, `derive()` in
`journal.c`, and spelled out in `FORMAT.md` section 9.4.

Every witness is run on the interpreter three times on its way into a file:
where its journal record is made, each time the journal is replayed, and
when the table is assembled. `bomb verify` runs it a fourth time.

With the default settings the evidence for a 16-bit table is:

- node counts 1 to 5 are settled by enumeration alone, for all strings;
- a string with minimum 6 has a 6-node witness (run) and no graph of 5 or
  fewer nodes (enumeration). The search contributed only the witness;
- a string with minimum 7 has a 7-node witness (run), no graph of 5 or fewer
  nodes (enumeration), and a PROVEN_UNSAT search result at budget 6. This
  last item is the only place where the table relies on the LevelSolver
  being exhaustive.

Three independent checks of that reliance are available. `bomb verify
--recertify` recomputes every minimum by search alone, from budget 1, with
any engine. Building with `--enumerate-max 0` or `--symmetry off` in another
work directory derives the same minima along a different route. And
`--enumerate-max 6` removes the reliance altogether, at the price of
running 70 billion graphs. `benchmarks/FIXED_RUN_16.md` reports all three.

If the records of a journal contradict each other (a witness inside an
excluded budget) nothing is certified for that string, `build` and `resume`
stop with an error naming it, and `finalize` writes nothing.

`bomb status` states the maximum of the minimum node counts as exact only
when every string has a certified minimum. Until then it gives proven
bounds: at least the largest proven lower bound, at most the largest
witness.

## Interrupting and resuming

A construction stores one thing: an append-only journal of checksummed
64-byte records, one per enumeration part, per string found by enumeration
and per LevelSolver call. Each process appends to a segment file of its own
and no file is ever rewritten. The table, the statistics and the list of
open work are recomputed from the journal whenever they are needed.

So there is no state that an interruption can leave half-updated. A process
that dies leaves, at worst, an unfinished record at the end of its segment;
the reader recognizes it by its checksum and ignores it, and the work it
stood for is done again. A damaged record in the middle of a segment is
not ignored: the journal is refused, with the file and offset.

`resume` reuses every certified result, every witness and every excluded
budget, and repeats no search that reached a conclusion. What an
interruption does cost is the search or enumeration part in progress, which
starts over: the search stack is not saved. For 16-bit strings a search
takes at most a few milliseconds and a 5-node enumeration part about half a
second.

Ctrl-C and SIGTERM end a session at the next job boundary and write its end
record. A lock file keeps two builds out of one directory. If the
coordinating process is killed outright, its workers notice at their next
progress report and stop; until they have, `resume` reports that the
directory is in use.

`bits`, `symmetry` and `enumerate-max` are fixed for the life of a work
directory, and a journal written by another search revision or journal
version is refused, with the reason.

Work directories are not for version control: `bomb-work/` holds 8 MB for
the default 16-bit build. The table file is reproducible from the source in
seconds, bit for bit.

## Longer tables: the ladder

Searching every 24-bit string from one node up would cost about 8·10^13
search steps. The ladder needs 3·10^13, and every intermediate result is a
complete table.

The table for L bits is built on the complete table for L − 1 bits
(`bomb build --base FILE`, or `bomb ladder` for the whole climb):

- A REV that emits an L-bit string emits its first L − 1 bits. The string
  cannot need fewer nodes than they do, and their minimum is certified in
  the table below.
- The REV stored there for those L − 1 bits goes on to emit one more bit.
  That is a REV for one of the two extensions, with as many nodes as the
  bound just given: a certified minimum, without a search. Half the table
  is settled this way.
- The other extension is searched from that bound upward. Usually the first
  or the second budget tried has a REV.

The base table is evidence, not an assumption: each time a journal is
replayed, every REV of the base is run again, and the base must be the one
the construction was started on (it is identified by its checksum).
`FORMAT.md` section 9.5 has the rules.

```bash
build/carlobomb bomb ladder --dir ladder --from fixed-run-16.frb --to 20 --workers 4
```

leaves `ladder/fixed-run-17.frb` … `ladder/fixed-run-20.frb`, the work
directory of each level, and a report of each. Stop it whenever; the same
command continues. In the development container, with 4 processes, the
levels 17 to 20 take 1.4, 5, 14 and 39 seconds, and the files are the same
bytes whichever engine builds them, with however many workers and
interruptions (`OVERNIGHT.md` lists their checksums).

Workers take jobs from a counter they share, a few at a time, so a slower
processor core does fewer jobs and delays nothing. The state of a
24-bit construction is 1.2 GB in the coordinating process while it plans
and replays, and is released before the workers start; a worker holds the
job list and little else.

## Using a table

`carlobomb bomb lookup` reads one entry: the REV for one string of exactly L
bits. The Bomb-backed FixedSolver (`solvers/fixed/bomb/`, `carlobomb fixed`)
answers every string of 1 to L bits from the same table, through an index of
prefixes built when the table is opened.

## Limits

- L is at most 24: an entry of format version 2 holds 24 nodes, which is
  what the ring needs at 24 bits, and the table is 544 MiB. Version 1, for
  up to 16 bits, is unchanged (`FORMAT.md`).
- `--recertify` runs in one process. For a table of more than about 18 bits
  use `--sample`.
- Building needs POSIX (fork, flock). Reading, validating and looking up
  work wherever C11 does.
- Enumeration is capped at 6 nodes. Graphs of 7 nodes number 8.7·10^13.
- Progress is reported per finished job. A 6-node enumeration part runs for
  about three minutes between reports.
- A file proves that its graphs emit their strings. That no smaller graph
  exists is evidence of the construction, kept in the journal and
  reproducible with `--recertify`; see `FORMAT.md` section 7.

## Not part of this milestone

LevelSearchBomb, neural guidance and multi-search SIMD. The last has a
design note here, `MULTI_SEARCH_SIMD.md`, and no code.
