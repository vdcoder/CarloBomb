# Proposal: the shared search as a construction that certifies and resumes

A proposal for review. Nothing in it is built except the experiment that
measured it (`shared_search.c`), which writes no journal and certifies
nothing. It covers what the CTO asked to be investigated before any
assembly work: the journal, the classes, the witnesses, recovery and
memory. The measurements are from 14 November 2026, in the development
container (4 virtual CPUs, Intel Xeon under KVM, 15.7 GB, GCC 13.3 `-O3`);
`FULL_RUNS.md` has the runs at 25 and 26 bits.

## In short

1. **No new kind of evidence is needed.** For every string and node budget
   it covers, the shared search can say exactly what the ladder writes in
   its journal for that string and budget: the result, the REV, and the
   number of logical steps the LevelSolver takes for that one string. Not
   an equivalent: the same numbers. This was checked on every search of
   every journal at hand, @@JOURNALS@@, and on the totals of the owner's
   levels 25 and 26.
2. **So the construction it becomes can write the journal the ladder
   writes.** The rules of evidence, the code that derives a table from a
   journal, the table writer, `bomb verify` and the compact format read
   that journal as they read any other, unchanged and already reviewed.
3. **The certificate that an excluded target was covered** is then the
   record the project already trusts, one for each target and budget: "the
   search of S with N nodes ended without a REV after s steps". It is
   independently verifiable as it is today, by making that search of that
   one string with the LevelSolver, which shares no code with the shared
   search and must arrive at s. A record for each finished pass of a class
   ties those records to a search that was completed.
4. **A job is a class**: the strings with one beginning of P bits. @@P@@
5. **Witnesses are deterministic**: the REV is the first the LevelSolver
   finds, whatever the classes, the number of processes or the order.
6. **An interruption loses seconds**: the passes that were running.
7. **A worker needs a few megabytes.**
8. **Order of work**: the engine in portable C with its tests; the journal
   and the driver; acceptance on the certified levels 17 to 26, table for
   table and record for record; and only then a kernel.

## 1. What a pass claims, and why it is the ladder's claim

Fix a node budget N and a set W of strings of X bits, the *wanted*. The
shared search is the LevelSolver's loop with two differences.

- Where the LevelSolver compares the bit a branch emits with the next bit
  of its target, the shared search asks whether some wanted string begins
  with what the branch has emitted, that bit included.
- Where the LevelSolver gives a node that has no color the next bit of its
  target, the shared search tries 0 and then 1, each only if some wanted
  string goes on with it. The choice costs three steps of the loop that
  the LevelSolver does not have.

When a branch has emitted all X bits of a string, the string is recorded
with the graph as it stands and is not wanted from that moment.

For a string S, call *a step of S* a step of the shared search that is
made while the bits emitted so far are a beginning of S, the steps of a
choice not counted.

**Theorem.** If S is wanted when the pass starts, the steps of S, up to
the one in which S is found, or to the end of the pass if it is not, are
the steps of the LevelSolver's search for S with N nodes, in the same
order: before each, the graph and the stack are the same in both.

*Proof.* Between two steps of S the shared search does nothing, or it makes
an excursion: a branch that has emitted a bit S does not have. An excursion
starts either in a step of S, at a node whose color is not the next bit of
S but is that of another wanted string, or in a choice, with the bit that
is not S's. While it lasts, what has been emitted is not a beginning of S,
so none of its steps is a step of S. And like every branch of a depth-first
search, it ends having undone all it did to the graph and to the stack. (If
it started at a colored node, its last step drops the frame it started
from, which is what the LevelSolver's step did at once on seeing the wrong
bit. If it started in a choice, the frame of the choice is dropped by the
last step of the choice, where the LevelSolver dropped its frame when the
branch of S's bit came back.) So before the next step of S both programs
are in the same state. In a step of S itself both execute the same code on
the same state, except in the two places above. In the first, for the next
bit of S both go on: S is wanted, so every beginning of S is. In the
second, the LevelSolver colors the node with the bit of S in that step, and
the shared search does it in the choice that follows. ∎

Four things follow, for every wanted S.

1. S is found in the pass exactly if the LevelSolver finds a REV for S with
   N nodes, and it is the same REV, node for node.
2. If the pass ends with S still wanted, the LevelSolver's search for S has
   been made to its end: PROVEN_UNSAT for S at N.
3. The number of steps of S is the number of logical steps of that
   LevelSolver call: the number the ladder journals.
4. None of this depends on which other strings are wanted, on how the
   strings are divided into jobs, or on the order anything runs in.

The proof uses one fact about the bookkeeping: *a beginning is marked
wanted as long as a wanted string begins with it*. That is the one place
where a mistake in the shared search could leave a string uncovered
without finding anything wrong. The engine will check it at the end of
every pass (section 2).

**Checked.** The experiment counts steps by what had been emitted when
they were made (one counter for each beginning in the job's part of the
tree). The sum along the beginnings of S, when S is found or the pass
ends, is the number of steps of S.

@@JOURNALTABLE@@

## 2. The certificate

What has to be certified does not change. An entry of a Bomb is a certified
minimum if its REV runs and every smaller budget is excluded: by a bound
from the Bomb below, or by a search of that string that ended without a
REV (`bombs/fixed-run/FORMAT.md`, section 9).

Today one LevelSolver call writes one record. In the proposal one pass
writes one record for each string that was wanted in it, **each saying
what that LevelSolver call would have said**: the string, the budget, the
result, the logical steps, and the REV if there is one. Then one more
record, for the pass.

A target excluded at budget N is covered by a completed class search if:

1. the journal has the record of the pass of its class at budget N;
2. the target was wanted in that pass, which follows from the Bomb below
   and the records of smaller budgets, and is recomputed by the reader;
3. the records the pass wrote are all there (their number and their
   checksum are in the pass record), and the target's record among them
   says "no REV, s steps".

Who can check that independently, and at what price:

| | Who | What | Cost |
|---|---|---|---|
| A | the reader, at every load | 1 to 3 above for every pass; every REV run; every existing check | seconds to minutes |
| B | the LevelSolver, on any one record | makes the search of that one string and must get the same result, the same REV and the same number of steps | the search of one string: 5 to 15 million steps at 24 to 26 bits |
| C | a second implementation of the shared search | makes a whole pass again and must get the same records | the pass: seconds |
| D | the certified journals of levels 17 to 26 | every record the ladder wrote must come out of the shared search | once, as the acceptance test: about an hour of 4 processors for all ten levels |

B is what `bomb verify` does today for the strings it draws, with programs
that have no line in common with the shared search: the reference
LevelSolver in C and the register kernels. Because the step count of a
record is the count of that program, a record that does not come from a
complete walk of the string's search is one whose number B does not
reproduce. C is what makes a full recheck affordable: the whole level
again costs what it cost to build.

What is not offered, here or in the ladder: a proof that a search found
nothing which can be checked faster than by searching. The journal's
records have always been claims that are checked by doing the work again.
What changes is that doing it all again takes minutes, and that it can be
done one string at a time by an unrelated program.

**Checks the engine makes on itself**, each cheap next to the pass:

- at the end of a pass, that every beginning in the job's part of the tree
  is marked wanted exactly if a string below it is, and that the strings
  marked are those that were wanted less those that were found;
- that the steps counted by beginning add up to the steps made, less three
  for every choice;
- that every REV runs on the reference interpreter, emits its string and
  has the nodes it says, before its record is written (as the ladder does).

## 3. The journal

A work directory of a construction by shared search says so: a new layout
of `config.txt` (`carlobomb-fixed-run-work 4`) with the lines `search
shared` and `class_bits P`. Layouts 1 to 3 keep their meaning, and a
program that does not know layout 4 refuses the directory, as a program
that does not know the suffix rule refuses layout 3. No existing journal
changes its meaning.

Records:

- **The search record, as it is** (type 5, 96 bytes): string, budget,
  result, logical steps, REV. From a pass it carries no duration and no
  peak of the stack (both zero); the time is in the pass record.
- **A pass record** (new, type 6, the same 96 bytes): the class, the
  budget, how many strings were wanted and how many found, the steps the
  shared search made, the sum of the steps of the strings, a checksum over
  the pass's search records, and the duration.

A worker keeps the records of a pass in memory while the pass runs, at most
@@BATCH@@. When the pass has ended it appends them to its segment, then the
pass record, and flushes. **A search record of a layout-4 directory is
evidence only together with the pass record that follows it**: records of a
pass that was cut off while writing are there and count for nothing.

What the reader checks in addition to everything it checks today: that
each pass record has its records, in number and checksum; that the strings
of those records are the strings wanted in that pass, recomputed from the
Bomb below and the REVs found at smaller budgets; that the passes of a
class are of consecutive budgets, from the smallest any of its strings is
wanted at.

The rules of evidence (`FORMAT.md` 9.4 and 9.5) stay as they are, and so
does the code that applies them.

**Size.** As many search records as the ladder writes, because they are the
same records: 2.1 GB at 26 bits, growing by 1.9 a bit. A denser encoding of
a pass's records (the strings in order need no index; the steps fit a few
bytes) would come to about @@DENSE@@. That is a change of layout that can be
made later and separately, and should not ride on this one.

**The alternative that was considered** is the one `benchmarks/DISCOVERY.md`
sketched: pass records only, and a new rule of evidence, "a completed pass
excludes N for every string it wanted and did not find". The journal would
be a tenth of the size. It is not proposed: it needs a new rule where none
is needed, its claims cannot be checked one string at a time by the
LevelSolver, and it could not be compared with the certified journals
record for record. The per-string records cost disk space and buy all three.

## 4. Classes

A class is the set of strings of X bits with one beginning of P bits. Only
beginnings that start with 0: the other half of the table follows by
complement, as in the ladder. A job is one class with all its passes; jobs
share nothing.

What P costs and buys:

@@PTABLE@@

## 5. Witnesses

By the theorem, the REV recorded for S is the first the LevelSolver finds
for S at the budget that is its minimum. It does not depend on P, on the
number of processes, on the order of the jobs, on which other strings are
wanted, or on which lower bound a string was begun at. Fields of a graph
that the search never decided are written as 0, as the LevelSolver writes
them.

@@WITNESS@@

## 6. Recovery

The unit that is lost is a pass. A worker has nothing on disk for a pass
until it has ended.

- **Stopped during a pass:** nothing of it is in the journal. It is made
  again.
- **Stopped while writing:** some of its search records are in the journal
  and its pass record is not. The existing reader already skips a torn end
  of a segment. The records that are whole count for nothing without their
  pass record. The pass is made again and written again, in a new segment;
  what it writes is, by section 5, what the first attempt was writing.
- **On start:** the journal is read as today. For each class, the budgets
  that have a pass record are done; the next pass is at the next budget,
  with the strings wanted that the Bomb below and the REVs found so far
  leave. A class is finished when a pass leaves none.
- **Settings** (P among them) are fixed for the life of a work directory,
  like every other setting, and the lock that keeps two processes out of
  one directory stays.

@@RECOVERY@@

## 7. Memory

For one worker, with classes of 2^c strings (c = X − P):

| What | Bytes | c = 15 |
|---|---:|---:|
| which beginnings are wanted | 2^c / 4 | 8 KB |
| steps by beginning | 16 · 2^c | 512 KB |
| the records of the pass being made | at most 96 · 2^c | 3 MB |
| graph and stack of the search | | under 1 KB |

It does not grow with X. The Bomb below is mapped once and read by all
workers, as in the ladder: 39 MB at 24 bits in the compact format.

@@MEMORY@@

Deriving the table from the finished journal is the existing code and has
its existing appetite, about 75 bytes for each string of the level. Because
the records of a class are together and classes are ranges of the table,
that step could later be done one class at a time; it is not part of this
proposal.

## 8. What stays as it is

- The table formats, fixed-width and compact, and every file written so
  far.
- The rules of evidence and the code that applies them.
- Journals and work directories of layouts 1 to 3, and the ladder that
  writes them, which remains the reference a shared-search level is
  compared with.
- `bomb verify`, the chain check against the Bomb below, `--recertify`.
- The Captain's Bomb(27): its work directory, its executable, its journal.
  A table from the new method is to be compared with it, never to replace
  it.

## 9. Order of work

Each stage is a pull request that can be reviewed by itself.

1. **The engine**, in portable C, in the library: the experiment's search,
   with the counts by beginning and the checks of section 2. Tested against
   the LevelSolver string by string (result, REV, steps) on whole small
   levels and on random classes of large ones, under both compilers, the
   sanitizers and the build without assembly. The construction is not
   touched.
2. **The journal and the driver**: layout 4, the pass record, `bomb build`
   and `bomb ladder` with `--search shared`, recovery, `bomb status`,
   `bomb verify` reading passes and making them again. Tested by stopping a
   construction at random moments, in a pass and in a write, and requiring
   the same journal content and the same table as an undisturbed one.
3. **Acceptance.** Levels 17 to 26 built again from the 16-bit Bomb. Each
   table must be the certified file, byte for byte, and each journal must
   hold the searches of the certified journal, record for record
   (`journal_digest` gives the same three numbers for both).
4. **A kernel.** Only now. The plain C engine makes about 1.6·10^8 steps a
   second and process; the register kernels make 7 to 9 on the loop this
   one is derived from. A kernel is validated against the C engine by the
   records of whole passes, which have to be the same.
5. Later and separately, if wanted: the denser records of section 3, and
   the derivation of the table class by class.

## 10. Questions for the CTO

1. **Search records from a pass: the existing type under a new layout
   (proposed), or a type of their own?** The existing type keeps the
   derivation code untouched and says the same thing; the layout line and
   the segment's engine name say where the records come from. A type of
   its own would say it in every record and would need the derivation to
   learn it.
2. **Per-string records at all?** Proposed: yes, for the reasons in
   section 3. If journal size is the greater concern beyond 28 bits, the
   denser encoding is the answer I would give before giving up the records.
3. **A forecast of Bomb(27) and Bomb(28)?** The experiment as it stands
   would take about half an hour for 27 bits and an hour and a half for 28
   in this container, and would give the histogram, the node-count digest
   and the table checksum that the Captain's certified Bombs can be
   compared with the moment they are finished. It would be a prediction,
   marked as such, and it needs approval: only 25 and 26 bits were approved.
