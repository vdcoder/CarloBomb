# FixedRunBomb file format, version 1

A FixedRunBomb is a table with one entry for every binary string of one fixed
length L, 1 ≤ L ≤ 16. An entry holds a REV that emits the string as its first
L output bits, and says whether that REV is certified to have the minimum
possible number of nodes.

This document specifies two things:

1. the **table file** (`.frb`), the product: portable, fixed-size, addressed
   directly by the string;
2. the **construction journal**, the working files of `carlobomb bomb build`,
   from which a table is derived. A reader of tables can skip that part.

`fixed_run.h` is the C interface to the table file, `fixed_run_build.h` to the
journal. Neither file is written by copying C structures: every field below
is produced and consumed byte by byte.

## 1. Conventions

- **Byte order.** Every multi-byte integer is unsigned and little-endian:
  the least significant byte comes first.
- **Bit order.** Where a sequence of bytes is used as a sequence of bits, bit
  number k is bit (k mod 8) of byte (k div 8), and bit 0 of a byte is its
  least significant bit. A field of w bits starting at bit number p has its
  least significant bit at bit p.
- **Reserved** bytes and bits are written as zero and a reader rejects a file
  in which they are not.
- **CRC-32** is the common one (IEEE 802.3, as in zlib and PNG): reflected
  polynomial `0xEDB88320`, initial value and final XOR `0xFFFFFFFF`. The
  CRC-32 of the nine ASCII bytes `123456789` is `0xCBF43926`.
- **REV** is the model of `core/rev/SPEC.md`: nodes 0..N−1, node 0 the root,
  each node a color (0 white, 1 black) and two links. "Emits" always refers
  to the interpreter described there.

## 2. Targets and their index

A target is a string of L characters `0` and `1`. Its **index** is the
integer whose L binary digits are the string, **first emitted bit most
significant**:

    index = Σ  s[i] · 2^(L−1−i)        for i = 0 .. L−1

`0100100011010110` (L = 16) has index `0x48D6` = 18646. `010010001` (L = 9)
has index 145. Lexicographic order of targets is numeric order of indices,
and the complement of a target has index `index XOR (2^L − 1)`.

## 3. Layout

    offset 0        header, 64 bytes
    offset 64       entry 0, 19 bytes
    offset 64+19    entry 1
    ...
    offset 64+19·i  entry i                 (i = index of the target)
    ...
    end             64 + 19 · 2^L bytes exactly

For L = 16 the file is 1,245,248 bytes. The entry of a target is found by
arithmetic alone; nothing has to be read or decompressed first.

## 4. Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `43 42 46 52 42 0D 0A 1A` (`CBFRB`, CR, LF, Ctrl-Z) |
| 8 | 2 | Format version: 1 |
| 10 | 2 | Header size in bytes: 64 |
| 12 | 1 | L, the target length in bits: 1..16 |
| 13 | 1 | Node capacity of an entry: 16 |
| 14 | 1 | Bits per node: 9 |
| 15 | 1 | Entry size in bytes: 19 |
| 16 | 4 | Number of entries: 2^L |
| 20 | 4 | Flags. Bit 0 = COMPLETE (section 7). All other bits are reserved |
| 24 | 4 | Number of entries that hold a witness (status WITNESS or MINIMAL) |
| 28 | 4 | Number of entries with status MINIMAL |
| 32 | 1 | Largest node count among MINIMAL entries; 0 if there are none |
| 33 | 1 | Largest node count among all entries that hold a witness; 0 if none |
| 34 | 2 | Reserved |
| 36 | 4 | CRC-32 of the table: all 19 · 2^L bytes from offset 64 to the end |
| 40 | 20 | Reserved |
| 60 | 4 | CRC-32 of header bytes 0..59 |

The CR-LF and Ctrl-Z in the magic are there so that a transfer that
translates line endings, or a tool that treats the file as text, damages the
first bytes rather than something subtle.

The header carries no time stamp, host name or tool version: two
constructions with the same settings produce the same file, byte for byte
(section 10).

## 5. Entry

An entry is 19 bytes: an 18-byte **packed graph** followed by 1 **metadata**
byte.

### 5.1 Metadata byte (byte 18 of the entry)

| Bits | Field |
|---|---|
| 0–4 | Node count N of the stored graph: 1..16, or 0 when there is none |
| 5–7 | Status code |

| Code | Name | Meaning | Graph |
|---:|---|---|---|
| 0 | EMPTY | No witness; nothing is known | none |
| 1 | WITNESS | A valid witness. Its node count is not certified to be the minimum | N ≥ 1 |
| 2 | MINIMAL | A valid witness with a certified minimum node count | N ≥ 1 |
| 3 | UNRESOLVED | The construction worked on the target without a final answer (a search was stopped, or smaller node counts were excluded and no witness was found yet). No witness | none |
| 4 | UNSUPPORTED | Every node count up to 16 was recorded as excluded, so no witness fits this format (see section 8) | none |
| 5–7 | — | Reserved; a reader rejects them | |

An all-zero entry is EMPTY.

### 5.2 Packed graph (bytes 0–17 of the entry)

The 18 bytes are 144 bits, numbered as in section 1. Node i (i = 0..15)
occupies the nine bits 9i .. 9i+8:

| Bit of the node | Width | Field |
|---|---:|---|
| 0 | 1 | Color: 0 white, 1 black |
| 1–4 | 4 | Link 0: index of the node reached on color 0 |
| 5–8 | 4 | Link 1: index of the node reached on color 1 |

so that a node's nine bits, read as a number, are
`color + 2·link0 + 32·link1`.

Only nodes 0..N−1 exist. The slots of nodes N..15 are **zero**. With status
EMPTY, UNRESOLVED or UNSUPPORTED all 144 bits are zero.

Only complete, executable graphs are stored: every node has a color and two
links to existing nodes. The undecided fields a search works with never
reach a file.

### 5.3 Worked example

The 3-node REV

    Node   Color   Link0   Link1
    0      B (1)   2       1
    1      W (0)   0       0
    2      W (0)   1       2

emits `010010001…`. Its nodes are the numbers 1 + 2·2 + 32·1 = 37,
0, and 0 + 2·1 + 32·2 = 66, placed at bits 0–8, 9–17 and 18–26:

    bit    0 1 2 3 4 5 6 7 | 8 9 ...        19 ...   24
    value  1 0 1 0 0 1 0 0 | 0 0 ... 0 ...   1 ...    1
           '-- byte 0 = 0x25 --'    byte 2 = 0x08   byte 3 = 0x01

As a certified minimum it is stored, in entry 145 of a 9-bit table (file
offset 64 + 19·145 = 2819), as

    25 00 08 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43

where `0x43` = status 2 (MINIMAL) in bits 5–7 and node count 3 in bits 0–4.
`tests/test_fixed_run.c` checks exactly these bytes.

## 6. Validation

A reader that claims to have validated a file has checked all of the
following, and rejects the file if any fails. `frb_file_read` and
`carlobomb bomb verify` do.

1. The file is at least 64 bytes long and starts with the magic.
2. The format version is one the reader knows (this document: 1).
3. The header size is 64 and the CRC-32 of bytes 0..59 equals the stored
   header CRC.
4. L is in 1..16, node capacity is 16, bits per node is 9, entry size is 19,
   the number of entries is 2^L, no reserved flag bit is set and the reserved
   bytes are zero.
5. The file length is exactly 64 + 19 · 2^L.
6. The CRC-32 of the table equals the stored table CRC.
7. For every entry: the status code is 0..4. With status 1 or 2 the node
   count is 1..16, every link of nodes 0..N−1 is less than N, and the slots
   of nodes N..15 are zero. With any other status the node count is 0 and
   the packed graph is all zero.
8. For every entry with a graph: the graph, run on the REV interpreter for L
   output bits, emits exactly the target whose index is the entry's position.
9. The header's two counts, its two largest-node-count fields and its
   COMPLETE flag equal what the entries say (section 7 for the flag).

Random access (`frb_file_lookup`, `carlobomb bomb lookup`) performs 1–5 and,
for the one entry it reads, 7 and 8. It does not read the rest of the table
and therefore does not perform 6.

Checks beyond the file itself, done by `carlobomb bomb verify`:

- **Complement symmetry.** Flipping every color and exchanging the two links
  of every node turns a REV for a target into a REV for its complement with
  the same node count. So for every stored witness the transformed graph
  must emit the complement target (it is run), a MINIMAL entry and the
  MINIMAL entry of its complement must have the same node count, and a
  MINIMAL entry may not have more nodes than any witness stored for its
  complement.
- **Evidence** (`--work-dir`): every MINIMAL entry is certified, with the
  same node count, by the journal the table came from (section 9.4).
- **Recomputation** (`--recertify`): the minimum of every target is computed
  again by the MinimalSolver from node budget 1, using neither the journal,
  nor enumeration, nor symmetry, and compared.

## 7. Complete and partial tables

The COMPLETE flag is set if and only if every entry has status MINIMAL. A
writer computes it from the entries; it cannot be requested. A file without
the flag is a **partial** table: every graph in it is still a valid witness
for its target, but some targets have no certified minimum, or no witness.

What a valid file proves by itself, to a reader who trusts only the REV
interpreter: each stored graph emits its target. What it does **not** prove
by itself is the word "minimum": that no smaller graph exists is a statement
about all smaller graphs and rests on how the table was built. That evidence
is in the journal (section 9), and can be regenerated with `--recertify`.

Only for a COMPLETE table is the largest node count in it (header offset 32)
the exact maximum, over all L-bit strings, of the minimum node count. In a
partial table the same field is a lower bound on that maximum and nothing
more.

## 8. Capacity

An entry holds at most 16 nodes. That is always enough for L ≤ 16: the
**ring** of L nodes, in which both links of node i lead to node (i + 1) mod L
and node (i + 1) mod L carries bit i of the target, emits the target. So no
L-bit string needs more than L nodes. (`frb_ring_witness`; the test suite
runs it for all 131,070 strings of 1 to 16 bits.)

A graph with more than 16 nodes is never truncated or stored partially:
`frb_graph_pack` refuses it. Status UNSUPPORTED exists for a construction
that has recorded every node count up to 16 as excluded for some target.
Given the ring, that cannot happen with a correct search engine; if it is
ever seen, the engine or the journal is at fault, and `carlobomb bomb
status` says so.

## 9. Construction journal

The working files of a construction. They live in a **work directory**:

    config.txt            settings (text)
    journal/000001.frj    journal segments
    journal/000002.frj
    ...
    lock                  held (flock) while a build is running

The journal is the only thing a construction stores. The table, the
statistics and the list of work still to do are all recomputed from it each
time they are needed, so no second file has to be kept in step with it.

### 9.1 config.txt

    carlobomb-fixed-run-work 1
    journal_version 1
    search_revision 1
    bits 16
    symmetry 1
    enumerate_max 5
    max_nodes 16
    fallback ring
    engine amd64-auto
    workers 4
    max_steps 0

One `name value` per line after the first; `#` starts a comment line. The
file is replaced atomically (temporary file, then rename).

`bits`, `symmetry` and `enumerate_max` shape the records and are fixed for
the life of the directory; every segment repeats them in its header and a
segment that disagrees is refused. `journal_version` and `search_revision`
must equal those of the program. The **search revision** identifies the
LevelSolver's move order (which decides the witness a search returns) and
its definition of a logical step; all engines in this repository implement
revision 1 identically, which is why their records can be mixed. The other
settings may be changed when a construction is resumed.

### 9.2 Segments

A segment is a 64-byte header followed by 64-byte records. A segment is
created by one process, appended to by that process only, and never
modified afterwards. A build session creates one segment for itself and one
per worker process per round; segment numbers only grow.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `43 42 46 52 4A 0D 0A 1A` (`CBFRJ`, CR, LF, Ctrl-Z) |
| 8 | 2 | Journal format version: 1 |
| 10 | 2 | Header size: 64 |
| 12 | 1 | L |
| 13 | 1 | Symmetry: 0 or 1 |
| 14 | 1 | Largest node count enumerated (`enumerate_max`) |
| 15 | 1 | Reserved |
| 16 | 4 | Search revision |
| 20 | 4 | Segment number (the number in the file name) |
| 24 | 4 | Session number |
| 28 | 4 | Worker number; 0 = the coordinating process |
| 32 | 8 | Creation time, Unix seconds |
| 40 | 16 | Engine name, NUL-padded, cut to 16 bytes. Informational |
| 56 | 4 | Reserved |
| 60 | 4 | CRC-32 of bytes 0..59 |

### 9.3 Records

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Type (below) |
| 1 | 1 | Result |
| 2 | 1 | Node budget, or node count |
| 3 | 1 | Limit kind |
| 4 | 4 | Target index, or a count |
| 8 | 8 | Steps, or graphs |
| 16 | 8 | Step limit in force; 0 = none |
| 24 | 8 | Elapsed time in nanoseconds |
| 32 | 4 | Unix time in seconds (low 32 bits) when the record was written |
| 36 | 4 | Auxiliary |
| 40 | 1 | Node count of the graph that follows; 0 = no graph |
| 41 | 18 | Packed graph (section 5.2); zero if none |
| 59 | 1 | Reserved |
| 60 | 4 | CRC-32 of bytes 0..59 |

| Type | Name | Fields used |
|---:|---|---|
| 1 | SESSION_BEGIN | target = session number; auxiliary = worker processes; step limit of the first pass |
| 2 | SESSION_END | target = session number; elapsed = wall-clock time of the session; result = 0 if the table was complete |
| 3 | ENUM_WITNESS | node count n; auxiliary = part; target; graph (n nodes). The first graph of that part of the enumeration to emit the target (or, under symmetry, the target or its complement) |
| 4 | ENUM_PART | node count n; auxiliary = part; steps = graphs run; target = number of ENUM_WITNESS records the part wrote; elapsed. That part of the enumeration ran to completion |
| 5 | SEARCH | One LevelSolver call: target; node budget; result (0 SOLVED, 1 PROVEN_UNSAT, 2 RESOURCE_LIMIT); steps; step limit; elapsed; auxiliary = peak stack depth. SOLVED: the witness graph. RESOURCE_LIMIT: limit kind (1 stack, 2 steps) |

The enumeration of the graphs with exactly n nodes is cut into n² parts by
the two links of node 0: part p fixes link 0 of node 0 to p mod n and link 1
to p div n. Under symmetry a part runs only graphs with a white root.

### 9.4 Reading a journal

Segments are read in ascending number order, each from its start.

- A record whose CRC, type or reserved byte is wrong ends its segment if no
  valid record follows it: that is the unfinished tail of a writer that was
  interrupted, and it is skipped (and counted in `bomb status`). The same
  goes for a segment whose header never got written. If a valid record does
  follow, the segment is damaged and the journal is refused.
- A segment whose header names another journal version, search revision, L,
  symmetry or `enumerate_max` is refused.
- An ENUM_PART record must be preceded, in its segment, by exactly as many
  ENUM_WITNESS records of the same node count and part as it states, and
  must state the number of graphs that part consists of. Otherwise the
  journal is refused: the part's claim to have found nothing else is only as
  good as the list of what it found.
- The graph of every ENUM_WITNESS and SOLVED record is unpacked, validated
  and run. A graph that does not emit its target is refused, as is a SOLVED
  graph with more nodes than its budget.

What is then concluded, per target (`derive()` in `journal.c`):

1. **Witnesses.** Every graph admitted above is a witness for its target.
   Under symmetry its complement graph, which is run as well, is a witness
   for the complement target. With `fallback ring` the ring of section 8 is
   a witness too. The best witness is the one with the fewest nodes; ties go
   to enumeration (lowest part first), then search, then the ring.
2. **Excluded node budgets.** Budget b is excluded for a target
   - if the enumerations of the graphs with 1, 2, …, b nodes are all
     complete (all parts present) and none produced the target; or
   - if a search at a budget ≥ b returned PROVEN_UNSAT. (A LevelSolver
     budget means "at most that many nodes", so one such result excludes
     every smaller budget as well.) Under symmetry a result for a target
     counts for its complement.

   **A RESOURCE_LIMIT record excludes nothing**, whatever its step count.
3. **Lower bound.** The smallest budget that is not excluded.
4. **Certified minimum.** A target is MINIMAL exactly when its best witness
   has as many nodes as its lower bound.
5. **Contradiction.** If a witness has fewer nodes than the lower bound, or
   a search found a witness smaller than anything a complete enumeration of
   that size produced, the records contradict each other. Nothing is
   certified for that target, `build` and `resume` stop with an error, and
   `finalize` refuses to write any table, partial or not.
6. Otherwise the target is WITNESS if it has a witness; UNSUPPORTED if every
   budget up to 16 is excluded; UNRESOLVED if something is excluded or a
   search was stopped by a limit; EMPTY if nothing is known.

## 10. Determinism

Which minimum-node witness is stored for a target depends on the settings
(`bits`, `symmetry`, `enumerate_max`): enumeration and search generally find
different graphs of the same size. It does not depend on the engine, the
number of worker processes, the step limits used on the way, or how often
the construction was interrupted and resumed. With equal settings the table
file is identical byte for byte, so one checksum of the file is enough to
compare two machines' results. The node counts and statuses of a complete
table do not depend on any setting.

## 11. Versions

Version 1 is the format described here. A change that an existing reader
would misread increases the format version, and readers reject versions they
do not know rather than guess. The header size field lets a later version
add header fields without moving the table; version 1 readers still require
it to be 64. The journal has its own version number, and the search revision
a third, for the reasons given in section 9.1.
