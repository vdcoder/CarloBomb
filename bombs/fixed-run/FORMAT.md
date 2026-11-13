# FixedRunBomb file format, versions 1 to 4

A FixedRunBomb is a table with one entry for every binary string of one fixed
length L. An entry holds a REV that emits the string as its first L output
bits, and says whether that REV is certified to have the minimum possible
number of nodes.

Versions 1, 2 and 3 are the **fixed-width** formats, specified in sections
1 to 11: every string has an entry of the same size, found by arithmetic.
They differ in the size of an entry and in nothing else. Version 4 is the
**compact** format of section 12: a complete table in a fifteenth of the
space, with the answer for every shorter string as well. It keeps one REV
for a string and its complement, the graph of a REV as one number, and no
colors where they can be found again from the string. A compact file is
made from a fixed-width one and, for the Bombs the construction builds,
gives it back byte for byte; it replaces nothing.

The fixed-width versions:

| | Version 1 | Version 2 | Version 3 |
|---|---:|---:|---:|
| Target length L | 1..16 | 1..24 | 1..31 |
| Node capacity of an entry, C | 16 | 24 | 31 |
| Bits per link | 4 | 5 | 5 |
| Bits per node, w | 9 | 11 | 11 |
| Packed graph, G bytes | 18 | 33 | 43 |
| Entry, E = G + 1 bytes | 19 | 34 | 44 |
| File for the longest L | 1,245,248 bytes | 570,425,408 bytes | 94,489,280,576 bytes |

**Versions 1 and 2 are unchanged.** Every file ever written in them reads
as it did. A table of up to 16 bits is still written as version 1 and one of
17 to 24 bits as version 2, byte for byte as before. Version 3 is what a
table of 25 to 31 bits is written in; its nodes are the nodes of version 2,
with room for 31 of them. A file of a later version for a shorter length is
valid and is read, but nothing here writes one unless asked to (`--format`,
for tests).

**32 bits are in no version.** A table index of 31 bits and a count of 2^31
entries are what the 32-bit fields of the header hold, 31 nodes are what a
5-bit node count and 5-bit links address, and the ring of section 8 needs as
many nodes as the target has bits. A longer target needs wider fields
throughout and is a matter for a version that does not exist.

This document specifies three things:

1. the **table file** (`.frb`), the product: portable, fixed-size, addressed
   directly by the string;
2. the **construction journal**, the working files of `carlobomb bomb build`
   and `bomb ladder`, from which a table is derived. A reader of tables can
   skip that part;
3. the **compact file** (also `.frb`, told apart by its version field), made
   from a complete table file (section 12).

`fixed_run.h` is the C interface to the table file, `fixed_run_build.h` to the
journal, `compact.h` to the compact file. Neither file is written by copying C structures: every field below
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
- **C, w, G, E** are the numbers of the table above for the version at hand.

## 2. Targets and their index

A target is a string of L characters `0` and `1`. Its **index** is the
integer whose L binary digits are the string, **first emitted bit most
significant**:

    index = Σ  s[i] · 2^(L−1−i)        for i = 0 .. L−1

`0100100011010110` (L = 16) has index `0x48D6` = 18646. `010010001` (L = 9)
has index 145. Lexicographic order of targets is numeric order of indices,
and the complement of a target has index `index XOR (2^L − 1)`.

An index fits in 32 bits for every L of any version: it has at most 31
bits. So does the number of entries, at most 2^31. No file offset is stored
anywhere, but a reader computes them, and must do so in 64 bits: a table of
26 bits is longer than 2^31 bytes and one of 27 bits longer than 2^32
(2,952,790,080 and 5,905,580,096 bytes; `frb_entry_offset`,
`frb_file_bytes`).

## 3. Layout

    offset 0        header, 64 bytes
    offset 64       entry 0, E bytes
    offset 64+E     entry 1
    ...
    offset 64+E·i   entry i                 (i = index of the target)
    ...
    end             64 + E · 2^L bytes exactly

For L = 16 in version 1 the file is 1,245,248 bytes; for L = 24 in version
2, 570,425,408; for L = 25, 26 and 31 in version 3, 1,476,395,072,
2,952,790,080 and 94,489,280,576. The entry of a target is found by
arithmetic alone; nothing has to be read or decompressed first, in a file of
any size.

## 4. Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `43 42 46 52 42 0D 0A 1A` (`CBFRB`, CR, LF, Ctrl-Z) |
| 8 | 2 | Format version: 1, 2 or 3 |
| 10 | 2 | Header size in bytes: 64 |
| 12 | 1 | L, the target length in bits: 1..16 (version 1), 1..24 (version 2), 1..31 (version 3) |
| 13 | 1 | Node capacity of an entry, C: 16, 24 or 31 |
| 14 | 1 | Bits per node, w: 9 or 11 |
| 15 | 1 | Entry size in bytes, E: 19, 34 or 44 |
| 16 | 4 | Number of entries: 2^L |
| 20 | 4 | Flags. Bit 0 = COMPLETE (section 7). All other bits are reserved |
| 24 | 4 | Number of entries that hold a witness (status WITNESS or MINIMAL) |
| 28 | 4 | Number of entries with status MINIMAL |
| 32 | 1 | Largest node count among MINIMAL entries; 0 if there are none |
| 33 | 1 | Largest node count among all entries that hold a witness; 0 if none |
| 34 | 2 | Reserved |
| 36 | 4 | CRC-32 of the table: all E · 2^L bytes from offset 64 to the end |
| 40 | 20 | Reserved |
| 60 | 4 | CRC-32 of header bytes 0..59 |

The header is the same in all versions. Fields 13 to 15 are determined by
the version, and a file in which they are anything else is rejected: they
are there so that a reader can check what it is about to assume.

The CR-LF and Ctrl-Z in the magic are there so that a transfer that
translates line endings, or a tool that treats the file as text, damages the
first bytes rather than something subtle.

The header carries no time stamp, host name or tool version: two
constructions with the same settings produce the same file, byte for byte
(section 10).

## 5. Entry

An entry is E bytes: a **packed graph** of G bytes followed by 1
**metadata** byte.

### 5.1 Metadata byte (the last byte of the entry: byte 18, 33 or 43)

| Bits | Field |
|---|---|
| 0–4 | Node count N of the stored graph: 1..C, or 0 when there is none |
| 5–7 | Status code |

| Code | Name | Meaning | Graph |
|---:|---|---|---|
| 0 | EMPTY | No witness; nothing is known | none |
| 1 | WITNESS | A valid witness. Its node count is not certified to be the minimum | N ≥ 1 |
| 2 | MINIMAL | A valid witness with a certified minimum node count | N ≥ 1 |
| 3 | UNRESOLVED | The construction worked on the target without a final answer (a search was stopped, or smaller node counts were excluded and no witness was found yet). No witness | none |
| 4 | UNSUPPORTED | Every node count up to C was recorded as excluded, so no witness fits this format (see section 8) | none |
| 5–7 | — | Reserved; a reader rejects them | |

An all-zero entry is EMPTY. The metadata byte is the same in all versions;
five bits hold a node count up to 31.

### 5.2 Packed graph (the first G bytes of the entry)

The G bytes are 8·G bits, numbered as in section 1. Node i (i = 0..C−1)
occupies the w bits w·i .. w·i + w − 1:

| | Version 1 | Versions 2 and 3 |
|---|---|---|
| Color: 0 white, 1 black | bit 0 of the node | bit 0 |
| Link 0: index of the node reached on color 0 | bits 1–4 | bits 1–5 |
| Link 1: index of the node reached on color 1 | bits 5–8 | bits 6–10 |
| The node's bits, read as a number | `color + 2·link0 + 32·link1` | `color + 2·link0 + 64·link1` |
| Bits in all | 16 · 9 = 144 | 24 · 11 = 264 (version 2), 31 · 11 = 341 (version 3) |

Only nodes 0..N−1 exist. The slots of nodes N..C−1 are **zero**. With status
EMPTY, UNRESOLVED or UNSUPPORTED all bits are zero. In version 3 the 43
bytes have three bits more than the 31 nodes use, bits 341 to 343: they are
zero as well.

A graph of up to 24 nodes has the same first 33 bytes in versions 2 and 3;
version 3 adds ten bytes, which are zero for such a graph.

A link is a node index and must be less than N. A 5-bit link field can hold
numbers that are no node of any graph of its version (24 to 31 in version
2, 31 in version 3); no graph has such a link, and a reader rejects it like
any other link to a node that is not there.

Only complete, executable graphs are stored: every node has a color and two
links to existing nodes. The undecided fields a search works with never
reach a file.

### 5.3 Worked example

The 3-node REV

    Node   Color   Link0   Link1
    0      B (1)   2       1
    1      W (0)   0       0
    2      W (0)   1       2

emits `010010001…`.

**Version 1.** Its nodes are the numbers 1 + 2·2 + 32·1 = 37, 0, and
0 + 2·1 + 32·2 = 66, placed at bits 0–8, 9–17 and 18–26:

    bit    0 1 2 3 4 5 6 7 | 8 9 ...        19 ...   24
    value  1 0 1 0 0 1 0 0 | 0 0 ... 0 ...   1 ...    1
           '-- byte 0 = 0x25 --'    byte 2 = 0x08   byte 3 = 0x01

As a certified minimum it is stored, in entry 145 of a 9-bit table (file
offset 64 + 19·145 = 2819), as

    25 00 08 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43

where `0x43` = status 2 (MINIMAL) in bits 5–7 and node count 3 in bits 0–4.
`tests/test_fixed_run.c` checks exactly these bytes.

**Version 2.** Its nodes are the numbers 1 + 2·2 + 64·1 = 69, 0, and
0 + 2·1 + 64·2 = 130, placed at bits 0–10, 11–21 and 22–32. 69 is bits 0, 2
and 6; 130, starting at bit 22, is bits 23 and 29:

    45 00 80 20 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43

33 bytes of graph and the same metadata byte.

**Version 3.** The same nodes at the same bits, in 43 bytes:

    45 00 80 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43

`tests/test_fixed_run_ladder.c` checks exactly these bytes in both versions,
and the place of every field of every node.

## 6. Validation

A reader that claims to have validated a file has checked all of the
following, and rejects the file if any fails. `frb_file_read` and
`carlobomb bomb verify` do.

1. The file is at least 64 bytes long and starts with the magic.
2. The format version is one the reader knows (this document: 1, 2 and 3).
3. The header size is 64 and the CRC-32 of bytes 0..59 equals the stored
   header CRC.
4. L is in the range of the version; node capacity, bits per node and entry
   size are those of the version; the number of entries is 2^L; no reserved
   flag bit is set and the reserved bytes are zero.
5. The file length is exactly 64 + E · 2^L.
6. The CRC-32 of the table equals the stored table CRC.
7. For every entry: the status code is 0..4. With status 1 or 2 the node
   count is 1..C, every link of nodes 0..N−1 is less than N, and the slots
   of nodes N..C−1 are zero. With any other status the node count is 0 and
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
- **Recomputation** (`--recertify`): the minimum of every target, or of a
  random sample (`--sample`), is computed again by the MinimalSolver from
  node budget 1, using neither the journal, nor enumeration, nor symmetry,
  nor any other table, and compared. `--hardest` recomputes exactly the
  targets certified at the largest minimum in the table. With every witness
  of a complete table run (check 8), that settles the table's largest
  minimum from two sides that owe nothing to the construction: no string
  needs more, because each has a REV that was just run; and these strings
  need as much, because a second search says so.
- **A shorter table** (`--against FILE`): two complete tables for different
  lengths must agree in three ways, each a consequence of what a REV is.
  The minimum of a short string is the smallest minimum among its
  extensions in the longer table (a REV for the short string goes on to
  emit one of them, and a REV for any of them emits the short string). No
  string of the longer table needs fewer nodes than its first bits. And no
  stretch of consecutive bits of a string needs more than one node over the
  string's own minimum: n nodes for a string give n + 1 for the string
  without its first j bits (`solvers/fixed/bomb/README.md` has the
  construction).

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

An entry holds at most C nodes. That is always enough for the lengths its
version allows: the **ring** of L nodes, in which both links of node i lead
to node (i + 1) mod L and node (i + 1) mod L carries bit i of the target,
emits the target. So no L-bit string needs more than L nodes, and L ≤ C in
every version. (`frb_ring_witness`; the test suite runs it for every string
of 1 to 20 bits and for samples at every length up to 31.)

A graph with more than C nodes is never truncated or stored partially:
`frb_graph_pack_as` refuses it. Status UNSUPPORTED exists for a construction
that has recorded every node count up to C as excluded for some target.
Given the ring, that cannot happen with a correct search engine; if it is
ever seen, the engine or the journal is at fault, and `carlobomb bomb
status` says so.

The ring is a witness that needs no storage until a table is written: a
construction that falls back on it keeps no copy of it, in memory or in the
journal, and generates it for the entries that have nothing better when a
table is made.

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

A journal has the version of the table format it serves: a construction of
a version-1 table writes journal version 1, with 64-byte records; of a
version-2 table, journal version 2, with 80-byte records; of a version-3
table, journal version 3, with 96-byte records. Work directories of the
earlier versions are read and continued as they are.

### 9.1 config.txt

Layout 1, written for a version-1 table that is built on no other table.
It is what was always written:

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

Layout 2, written in every other case:

    carlobomb-fixed-run-work 2
    journal_version 2
    search_revision 1
    bits 24
    symmetry 1
    enumerate_max 0
    format 2
    base_bits 23
    base_crc 0123abcd
    base_file /home/me/frb24/fixed-run-23.frb
    max_nodes 24
    fallback ring
    engine amd64-auto
    workers 10
    max_steps 0

One `name value` per line after the first; `#` starts a comment line. The
value of `base_file` is the rest of its line and may contain spaces. The
file is replaced atomically (temporary file, then rename).

`bits`, `symmetry`, `enumerate_max`, `format`, `base_bits` and `base_crc`
are fixed for the life of the directory. The first three shape the records;
every segment repeats them in its header and a segment that disagrees is
refused. `journal_version` must equal `format`, and `search_revision` that
of the program. The **search revision** identifies the LevelSolver's move
order (which decides the witness a search returns) and its definition of a
logical step; all engines in this repository implement revision 1
identically, which is why their records can be mixed. The other settings
may be changed when a construction is resumed.

**The base Bomb** (`base_bits` > 0) is a complete table for fewer bits that
the construction builds on (section 9.5). It is identified by `base_bits`
and `base_crc`, the table checksum from its header; `base_file` says where
it is and may be corrected if the file moves. `base_crc` is written as
eight hexadecimal digits; `base_crc` and `base_file` are present exactly
when `base_bits` is not 0.

### 9.2 Segments

A segment is a 64-byte header followed by records of 64 bytes (journal
version 1), 80 bytes (version 2) or 96 bytes (version 3). A segment is created by one process,
appended to by that process only, and never modified afterwards. A build
session creates one segment for itself and one per worker process per
round; segment numbers only grow.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `43 42 46 52 4A 0D 0A 1A` (`CBFRJ`, CR, LF, Ctrl-Z) |
| 8 | 2 | Journal format version: 1, 2 or 3 |
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
| 41 | G | Packed graph (section 5.2) in the table's format: 18, 33 or 43 bytes; zero if none |
| 41 + G | 1, 2 or 8 | Reserved: byte 59 (version 1), bytes 74–75 (version 2), bytes 84–91 (version 3) |
| last 4 | 4 | CRC-32 of all bytes before it: bytes 0..59 (version 1), 0..75 (version 2), 0..91 (version 3) |

The first 41 bytes are the same in all versions, and a record is the
smallest multiple of 8 bytes that holds them, the graph, a reserved byte and
the checksum. Step counts, step limits and times are 64-bit; target indices
and counts of targets are 32-bit, which holds the 31 bits of the longest
target and the 2^31 entries of the largest table.

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

There is no record for the ring and none for anything taken from the base
Bomb: both are regenerated from what they are whenever the journal is read.

### 9.4 Reading a journal

Segments are read in ascending number order, each from its start, one
record at a time: the memory a reader needs does not grow with the journal.

- A record whose CRC, type or reserved bytes are wrong ends its segment if
  no valid record follows it: that is the unfinished tail of a writer that
  was interrupted, and it is skipped (and counted in `bomb status`). The
  same goes for a segment whose header never got written. If a valid record
  does follow, the segment is damaged and the journal is refused.
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
   for the complement target. With a base Bomb, the REV it stores for the
   target's first bits is a witness if, run on, it emits the target
   (section 9.5). With `fallback ring` the ring of section 8 is a witness
   too. The best witness is the one with the fewest nodes; ties go to the
   base Bomb, then enumeration (lowest part first), then search, then the
   ring.
2. **Excluded node budgets.** Budget b is excluded for a target
   - if the enumerations of the graphs with 1, 2, …, b nodes are all
     complete (all parts present) and none produced the target; or
   - if a search at a budget ≥ b returned PROVEN_UNSAT. (A LevelSolver
     budget means "at most that many nodes", so one such result excludes
     every smaller budget as well.) Under symmetry a result for a target
     counts for its complement; or
   - if b is less than the certified minimum, in the base Bomb, of the
     target's first bits.

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
   budget up to C is excluded; UNRESOLVED if something is excluded or a
   search was stopped by a limit; EMPTY if nothing is known.

### 9.5 The base Bomb

A construction for L bits may build on a complete table for B < L bits.
Two facts about REVs are used, and nothing else:

- A REV that emits an L-bit string emits its first B bits. So the string
  needs at least as many nodes as those B bits do: every smaller budget is
  excluded (rule 2 above).
- The REV stored for a B-bit string q, a certified minimum of m nodes, goes
  on emitting. Its first L output bits are one L-bit string that starts
  with q, and that string therefore has a REV of m nodes, and by the first
  fact cannot have fewer: it is MINIMAL at once (rules 1 and 4).

Each time the journal is read the base is read with it. Its header, length
and both checksums are verified and its table checksum must be the
`base_crc` of `config.txt`; it must carry the COMPLETE flag and every entry
must have status MINIMAL. Every one of its REVs is then decoded and **run**
for L bits, and must emit, first, the string it is filed under. A base that
fails any of this is refused, and nothing is concluded from it. A record in
the journal that contradicts what the base gives (a PROVEN_UNSAT at the
node count of a REV that exists) is a contradiction like any other (rule 5).

The base may be of another format version than the construction built on
it, as a 24-bit table in version 2 is under a 25-bit construction in
version 3. Its REVs are decoded in its own format, run, and packed in the
construction's; the checksum that identifies it is the one in its own
header. Nothing is converted on disk and nothing about the base changes.

What a construction on a base certifies is therefore exactly as good as the
base. The base is not trusted for more than its entries can show when run;
that its minima are minima rests on its own journal, and so on down to a
table that was built on none.

With B = L − 1 the base settles one of the two extensions of every string,
half the table, without a search, and the other extension is searched from
the minimum of its first L − 1 bits upward. **The ladder**
(`carlobomb bomb ladder`) builds the tables for B + 1, B + 2, … in turn,
each on the one before:

    DIR/fixed-run-NN.frb   the complete Bomb for NN bits
    DIR/work-NN/           the work directory of level NN, built on NN − 1
    DIR/level-NN.txt       the report printed when level NN was finished
    DIR/lock               held while a ladder is running

A level's table is finalized, read back and validated before the next
level is started on it.

## 10. Determinism

Which minimum-node witness is stored for a target depends on the settings
(`bits`, `symmetry`, `enumerate_max`, and the base Bomb if there is one):
enumeration, search and a base REV run on generally give different graphs
of the same size. It does not depend on the engine, the number of worker
processes, which worker ran which search, the step limits used on the way,
or how often the construction was interrupted and resumed. With equal
settings the table file is identical byte for byte, so one checksum of the
file is enough to compare two machines' results. In particular every level
of a ladder from the same starting Bomb is the same file everywhere. The
node counts and statuses of a complete table do not depend on any setting.

## 11. Versions

Version 1 is the format of tables of up to 16 bits; version 2 that of
tables of up to 24; version 3 that of tables of up to 31; version 4 is the
compact format of section 12, for any of those lengths. A change that an
existing reader would misread increases the format version, and readers
reject versions they do not know rather than guess: a reader that knows
only versions 1 and 2 rejects a version-3 file at the version field, and a
reader of the fixed-width formats rejects a compact file there. The
header size field lets a later version add header fields without moving the
table; readers of versions 1 to 4 require it to be 64. The journal carries
the version of the table format it serves, which is 1, 2 or 3 and never 4,
and the search revision is a third number, for the reasons given in section
9.1.

One number means one layout. The proposal for the compact format (issue 6)
calls it "v3"; by the time it was built, tables of 25 bits and more were
being written as version 3 of the fixed-width format, and those files
exist. The compact format therefore has the next number, 4, and nothing was
renumbered: no byte of any file of versions 1 to 3 means anything else than
it did. The one exception to "one number, one layout" was never released:
the draft of version 4 had another layout, which readers recognize and
reject (section 12.14).

## 12. The compact format (version 4)

A complete FixedRunBomb(L) holds 2^L certified minimum-node REVs. In a
fixed-width file each takes the space of the largest REV the version allows:
19, 34 or 44 bytes for REVs that, up to 24 bits, nearly all have 8 nodes or
fewer. A compact file stores what is essential of the same table and derives
the rest, and adds what a fixed-width file does not have: the answer for
every string of fewer than L bits.

    fixed-width, 24 bits      570,425,408 bytes
    compact, 24 bits           38,858,512 bytes      6.8%

What it is and is not:

- It is made **from a complete fixed-width file**, after the construction,
  and never the other way round. A construction neither reads nor writes it.
  Journals, base Bombs and the tables a ladder builds on are fixed-width.
- **Only a complete table can be compact.** There is no status in it: every
  node count in the file is a certified minimum, and a table with any entry
  that is not has no compact form.
- It certifies nothing by itself that the fixed-width file did not. That the
  node counts are minima is established by the construction of the
  fixed-width file and recorded in its journal; a compact file names the
  table it was made from by version and checksum.
- Its node counts are those of the table, exactly. Its REVs are the REVs of
  the table **as far as the header says** (section 12.11): for every Bomb the
  construction of this project builds, all of them, link for link and color
  for color, and the fixed-width file can then be regenerated from it byte
  for byte.

### 12.1 What is stored, and what is derived

A string and its complement need the same number of nodes, and the
complement of a REV of the one (every color inverted, the two links of every
node exchanged) is a REV of the other. So of the 2^L strings only the
2^(L−1) that **start with 0** are stored. A stored string is addressed by its
index s, 0 .. 2^(L−1) − 1, which is its index of section 2.

Stored, for every stored string of L bits:

- its **node count**, in 4 bits;
- its **REV**, in the class of its node count. The REVs of N nodes are
  **class N**, kept in the order of their strings. A REV of 6 or more nodes
  is one number, the **rank of its graph** (section 12.7); its colors are
  not stored. A REV of up to 5 nodes is stored with its colors and links.

Stored, for every stored string of 1 to L−1 bits: one **navigation bit**
(section 12.5).

Stored to find a REV: for every 256th stored string, how many stored strings
before it have each node count (section 12.6).

Derived, and stored nowhere:

- the **place** of a REV in its class: the number of stored strings before
  it that have its node count;
- the **colors** of a REV of 6 or more nodes, and the numbering of its
  nodes: by running the machine on its graph against its string (section
  12.8);
- the **answer for a shorter string**: the navigation bits lead from it to
  one of its L-bit extensions with the fewest nodes (section 12.9);
- **everything about a string that starts with 1**: the complement of what
  the file gives for its complement;
- **which of the two extensions of a string needs fewer nodes**: the node
  counts of the two, compared (section 12.9).

### 12.2 Layout

With S = 2^(L−1), M the largest node count of any REV, and a8(x) = x rounded
up to a multiple of 8:

    offset 0        header, 64 bytes
    offset 64       class directory: 12 rows of 32 bytes, for N = 1 .. 12
    offset 448      navigation bits: a8(⌈S / 8⌉) bytes
    then            node counts: a8(⌈S / 2⌉) bytes
    then            counts before: a8(R · M · 4) bytes, R = ⌈S / 256⌉ rows
    then            class 1, class 2, ... class 12, each a8(⌈count[N] · bits[N] / 8⌉) bytes
    then            8 bytes of zero
    end

Every part starts at a multiple of 8, and every offset is a 64-bit number.
From 7 bits on, the navigation bits are 2^(L−4) bytes and the node counts
2^(L−2). The last 8 bytes let a reader take any record as 9 bytes from its
first byte without looking past the end of the file.

### 12.3 Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic: `43 42 46 52 42 0D 0A 1A`, as in every version |
| 8 | 2 | Format version: 4 |
| 10 | 2 | Header size in bytes: 64 |
| 12 | 1 | L, the target length in bits: 1..31 |
| 13 | 1 | Largest node count the format holds: 12 |
| 14 | 1 | 8: the counts before are kept for every 2^8th stored string |
| 15 | 1 | 0 (the entry size of the fixed-width versions; there are no entries here) |
| 16 | 4 | Number of strings answered: 2^L |
| 20 | 4 | Flags: 1 (COMPLETE) |
| 24 | 4 | 2^L (entries that hold a witness) |
| 28 | 4 | 2^L (entries with status MINIMAL) |
| 32 | 1 | M, the largest node count of any REV: the last class that is not empty |
| 33 | 1 | The same |
| 34 | 2 | Version of the fixed-width file this was made from: 1, 2 or 3, and one that holds L bits; 0 if it was made from a table that never was a file |
| 36 | 4 | CRC-32 of the class directory: the 384 bytes from offset 64 |
| 40 | 4 | CRC-32 of the index: navigation bits, node counts and counts before, the bytes from offset 448 to the first byte of class 1 |
| 44 | 4 | Table CRC-32 of the fixed-width file this was made from (its header field at offset 36) |
| 48 | 8 | Length of this file in bytes |
| 56 | 4 | Other REVs: the number of strings of L bits, of all 2^L, whose REV as read from this file is not the REV of the table it was made from (section 12.11) |
| 60 | 4 | CRC-32 of header bytes 0..59 |

Offsets 0 to 13 and 16 to 33 are the fields of the fixed-width header with
the values they have for a complete table, so that a tool that prints a
header prints the truth. Offset 14 holds the spacing of the counts where a
fixed-width header has the bits of a node, offset 15 is 0 where it has the
size of an entry, and offsets 34 to 59 are used differently: a fixed-width
header has one table checksum there and reserved bytes.

### 12.4 Class directory

Row N − 1, at offset 64 + 32 · (N − 1), describes class N:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | count[N]: the number of stored REVs of N nodes |
| 4 | 1 | bits[N]: bits of one record (section 12.7) |
| 5 | 1 | Kind of record: 0 colors and links (N up to 5), 1 the rank of the graph (N from 6) |
| 6 | 2 | Reserved |
| 8 | 8 | File offset of the class |
| 16 | 8 | Its length in bytes, a multiple of 8 |
| 24 | 4 | CRC-32 of those bytes; 0 for an empty class |
| 28 | 4 | Reserved |

The counts add up to 2^(L−1). **Everything else in a row follows from the
counts and L**, and a reader recomputes it and rejects a file in which a row
says otherwise. bits[N] is the same in every file:

| N | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| bits[N] | 1 | 6 | 15 | 20 | 35 | 23 | 28 | 34 | 40 | 46 | 52 | 58 |

A REV of more than 12 nodes has no class, and a table with one has no
compact form. Up to 24 bits no string needs more than 9 nodes.

### 12.5 Navigation bits

One bit for every stored string of 1 to L−1 bits: for the string of k bits
with index v (it starts with 0, so v < 2^(k−1)), the bit at **place
2^(k−1) + v**. Place 0 belongs to no string and is 0. The places are the
nodes of a binary tree in level order: the two extensions of the string at
place p are at places 2p and 2p + 1, and the strings of L bits would be at
places 2^(L−1) + s.

The bit of a string is

- **0** if an L-bit extension of the string with the fewest nodes lies under
  its extension by 0: if the fewest nodes under its extension by 0 are at
  most the fewest under its extension by 1;
- **1** if the fewest nodes under its extension by 1 are fewer.

Following the bits from a string, 2p + bit at every step, ends at place
2^(L−1) + s of an L-bit string s. That string is the **lowest of the
extensions of the string that need the fewest nodes**, and its node count is
the minimum of the string itself: a REV that emits the string goes on and
emits one of its L-bit extensions, so the fewest nodes of a string are the
fewest among those.

### 12.6 Node counts, and the counts before

The node count of stored string s is the 4-bit field at bit 4·s of the node
counts: the low half of byte s div 2 for an even s, the high half for an odd
one. It is 1 .. M.

The counts before are R rows of M numbers of 4 bytes. Number N − 1 of row r
is the number of stored strings **before string 256·r** that have N nodes.
Row 0 is all zero.

The **place of the REV of s** in its class of N nodes is therefore the
number N − 1 of row s div 256, plus the number of strings t with
256·(s div 256) ≤ t < s that have N nodes: at most 255 node counts to look
at, in 128 consecutive bytes.

### 12.7 The record of a REV

The records of class N are bits[N] bits each, one after the other without
gaps: record j of the class is the bit field at bit j·bits[N] of the bytes
of the class (section 1, bit order). The bits after the last record of a
class are zero.

**Up to 5 nodes: colors and links.** With w = ⌈log2 N⌉ (0, 1, 2, 2, 3 for N
= 1 .. 5), the record is, from its least significant bit:

    N bits      the colors: bit i is the color of node i
    then, for node 0, node 1, ... node N−1:
    w bits      its link on color 0
    w bits      its link on color 1

N + 2·N·w bits. A link is a node number below N; a record with a link that
is not is rejected. The REV is stored as it is in the table, with the
numbering it has there.

**From 6 nodes: the rank of the graph.** The graph of a REV is its links
without its colors. Its nodes are first numbered **canonically**: the root is
0; then the nodes in the order in which a breadth-first search meets them
that takes the nodes in the order of their new numbers and looks at the link
on color 0 before the link on color 1. Every node of a minimum-node REV is
reached, since a node no link leads to could be left out.

A canonically numbered graph of n nodes is the list of its 2n links in the
order node 0 on color 0, node 0 on color 1, node 1 on color 0, ...: link
number p belongs to node p div 2 and color p mod 2. Reading the list from
the front, with m the number of nodes that have a number so far (1 before
the first link: the root), every link leads either to one of those m nodes
or to the next new node, which is node m and makes m one more. A list is one
of a canonical graph of n nodes exactly if every link's node has a number by
the time the list comes to it (p div 2 < m) and m is n at the end.

Let ways(p, m) be the number of ways to finish such a list from link p on
when m nodes have a number:

    ways(2n, m) = 1 if m = n, else 0
    ways(p, m)  = 0                                      if m < 1, m > n or p div 2 ≥ m
    ways(p, m)  = m · ways(p+1, m) + ways(p+1, m+1)      if m < n
    ways(p, m)  = m · ways(p+1, m)                       if m = n

The number of canonical graphs of n nodes is ways(0, 1):

| n | Graphs | Bits of a rank |
|---:|---:|---:|
| 6 | 5,931,540 | 23 |
| 7 | 256,182,290 | 28 |
| 8 | 12,665,445,248 | 34 |
| 9 | 705,068,085,303 | 40 |
| 10 | 43,631,250,229,700 | 46 |
| 11 | 2,970,581,345,516,818 | 52 |
| 12 | 220,642,839,342,906,336 | 58 |

(1, 12, 216, 5,248 and 160,675 for 1 to 5 nodes.) The **rank** of a graph is
its number when all the lists are put in order, a link to a lower node
first: starting from rank = 0 and m = 1, for p = 0, 1, ... 2n − 1 and the
link `to` at that place,

    if to < m:   rank = rank + to · ways(p+1, m)
    else:        rank = rank + m · ways(p+1, m),  and m = m + 1

The graph of a rank is found the other way round: at each p, with w =
ways(p+1, m), if rank < m·w the link is rank div w and rank becomes rank mod
w; else the link is the new node m, rank becomes rank − m·w, and m one more.
A record that is not below the number of graphs is rejected.

### 12.8 The colors, and the numbering of the nodes

The colors of a REV of 6 or more nodes are found from its graph, as the rank
gives it, and its string, by running the machine of `core/rev/SPEC.md`
against the string. fp and cp start at the root. For each bit of the string
in turn, the first bit first:

1. For every bit but the first, fp moves along its link on the color of cp.
2. If the node under fp has no color yet, it gets color 0. This is a
   **choice**: the state of the search is remembered with color 1 for that
   node instead.
3. cp moves along its link on the color of fp.
4. If the node cp is on now has no color yet, it gets the color the string
   has at this bit. If it has that color, the search goes on. If it has the
   other color, the search goes back to the choice made last, takes color 1
   there, and goes on from step 3 of that bit. With no choice left there are
   no colors: the record is rejected.

When the last bit has been emitted the nodes have their colors. Every node
must have been reached, or the record is rejected: a node the string does
not need means that the REV is not one of the fewest nodes.

The nodes are then **renumbered in the order in which the machine reached
them**: the root is 0, and after it each node gets the next number when fp
or cp first comes to it, fp's move of a bit before cp's. The REV with those
numbers and colors is the REV the file gives for the string.

Why this order: it is the order in which the search of the solvers tries
colors (white first) and numbers the nodes of the REV it finds, and the
construction searches for the string of a pair that starts with 0 and gives
the other its complement. A REV that came from that search therefore comes
back as it was: any other coloring of its graph that emitted the string and
came earlier in this order would have been found by the search first. Where
several colorings of a graph emit the string, the one found first is the
answer; the others are REVs of as many nodes for the same string, which may
go on differently after bit L.

### 12.9 Lookup

**A string of L bits that starts with 0**, index s: its node count N from
the node counts; the place j of its REV from the counts before (section
12.6); record j of class N; for N up to 5 the record is the REV, from 6 on
its graph is that of the rank, colored and numbered against the string
(section 12.8).

**A string of fewer than L bits that starts with 0**: from its place
(section 12.5), 2p + bit until the place is 2^(L−1) + s. The node count of s
is the node count of the string, and the REV of s is its REV.

**A string that starts with 1**, of any length: the same for its complement,
and the complement of the result. Its node count is the same. The L-bit
string that answers it is the complement of the one that answers its
complement: it is one of its extensions with the fewest nodes, and the
**highest** of them, where a string that starts with 0 gets the lowest. Its
REV is the REV of its complement with every color inverted and the two links
of every node exchanged.

**The comparison.** Which of the two extensions by one bit of a string of 0
to L−1 bits needs fewer nodes: the node counts of the two extensions, each
looked up as above. No REV is read for it. For a string of L bits the
question belongs to the Bomb of L+1.

A reader that answers one question from a file checks the header and
directory and then trusts no number it meets: a node count outside 1 .. M, a
place outside its class, a record that is no REV, and a REV that does not
emit the string are all refused, and nothing outside the file is read.

### 12.10 Validation

A reader that validates a file in full checks, in this order:

1. Magic, version, header size, the header CRC; offsets 13, 14 and 15; the
   CRC of the directory.
2. The class counts add up to 2^(L−1); every other number of the directory
   and the length of the file are what section 12.2 gives; the counts and
   flags at offsets 16 to 33; a source version that could hold L bits.
3. The CRC of the index and of every class.
4. Everything no string stands on is zero: place 0 of the navigation bits
   and the places from 2^(L−1) on, the node counts after the last, the bytes
   after the last row, the bits after the last record of every class, the
   last 8 bytes.
5. For every stored string in order: its node count is 1 .. M; if it is the
   first of a block of 256, the row of the block is the number of strings
   of each node count so far; its record decodes, its colors are found
   where they are not stored, and the REV emits the string.
6. Every navigation bit is what the node counts under it call for, checked
   as the strings under it go by.
7. Each class has as many strings as its count.

A file that passes is consistent in itself. It is the file of a given table
only if, besides, its node counts are the table's and its REVs are the
table's but for as many as the header says: that is section 12.11.

### 12.11 Conversion, both ways

**To the compact format.** The table is validated as section 6 describes and
must be complete. It is refused if a REV has more than 12 nodes, if a string
and its complement have different node counts, or if a REV of 6 or more
nodes has a node its string does not need (section 12.8): the last two mean
that the table is no table of minima.

The file is written under a temporary name, opened and validated as any
reader would, and compared with the table before it gets its name:

- every string of L bits has the node count of the table and a REV of that
  many nodes that emits it;
- every string of 1 to L−1 bits is answered by the extension section 12.9
  names, worked out from the node counts of the table alone;
- every comparison of the two extensions of a string of 0 to L−1 bits is
  what the node counts of the table say;
- the number of strings of L bits whose REV is not the REV of the table is
  the number at offset 56 of the header.

**Other REVs.** The REV a file returns for a string can differ from the REV
of the table in three ways, and in no other: a REV of 6 or more nodes that
has, in the table, another numbering of its nodes than the search gives, or
another of the colorings that emit the string than the one found first; and
a string that starts with 1 whose REV in the table is not the complement of
its complement's. In each case the file returns a REV of the same number of
nodes that emits the string. The header counts the strings. For a table
built by `carlobomb bomb build` or `bomb ladder` with their defaults the
count is 0: REVs of up to 5 nodes are stored as they are, those of 6 or more
come from the search, and the second string of every pair has the complement
of the first's.

**Back.** The REVs a compact file returns for the 2^L strings, written as a
fixed-width table of the version named at offset 34 (or the one L calls for
if that is 0), with every entry MINIMAL. With 0 other REVs **that is the
file the compact one was made from, byte for byte**, and its table CRC is
the one at offset 44. With other REVs it is a complete Bomb with the same
node counts and not that file.

### 12.12 Worked example

The Bomb of the 32 strings of 5 bits, as the construction builds it. The
REVs of the 16 strings that start with 0 (color, link on 0, link on 1):

| String | Nodes | Node 0 | Node 1 | Node 2 | Node 3 |
|---|---:|---|---|---|---|
| 00000 | 1 | W 0 0 | | | |
| 00001 | 4 | B 2 1 | W 1 3 | W 0 0 | W 2 0 |
| 00010 | 3 | B 2 1 | W 1 2 | W 0 0 | |
| 00011 | 4 | B 0 1 | W 0 2 | W 0 3 | W 0 0 |
| 00100 | 3 | B 1 1 | W 2 0 | W 0 0 | |
| 00101 | 3 | B 1 1 | W 2 0 | W 0 2 | |
| 00110 | 3 | B 2 1 | W 0 1 | B 1 0 | |
| 00111 | 3 | B 0 1 | W 0 2 | W 0 0 | |
| 01000 | 3 | W 1 0 | W 2 0 | B 0 0 | |
| 01001 | 2 | B 1 1 | W 0 1 | | |
| 01010 | 2 | B 1 1 | W 0 0 | | |
| 01011 | 3 | W 1 2 | W 2 1 | B 0 2 | |
| 01100 | 3 | W 1 2 | W 2 0 | B 2 1 | |
| 01101 | 2 | B 0 1 | W 0 0 | | |
| 01110 | 3 | B 0 1 | W 0 2 | B 2 0 | |
| 01111 | 3 | W 1 0 | W 2 0 | B 2 0 | |

S = 16, M = 4. The file is 536 bytes: header and directory to 448; 8 bytes
of navigation bits; 8 of node counts; one row of 4 counts, 16 bytes; class
1, 8 bytes at 480; class 2, 8 bytes at 488; class 3, 24 bytes at 496; class
4, 8 bytes at 520; and 8 bytes of zero.

The header:

    43 42 46 52 42 0D 0A 1A  04 00 40 00 05 0C 08 00
    20 00 00 00 01 00 00 00  20 00 00 00 20 00 00 00
    04 04 01 00 49 86 8D 17  8A 5C 17 BA A9 6D 37 C9
    18 02 00 00 00 00 00 00  00 00 00 00 C9 93 07 1A

Version 4, header 64, L = 5, capacity 12, spacing 2^8, 32 strings, COMPLETE,
32 witnesses, 32 minima, largest 4 nodes, made from a version-1 file with
table CRC `C9376DA9`, 536 (`0x218`) bytes, 0 other REVs.

The rows of the directory for N = 1 to 4, and the row for 5 as the first of
the empty ones:

    01 00 00 00 01 00 00 00  E0 01 00 00 00 00 00 00  08 00 00 00 00 00 00 00  69 DF 22 65 00 00 00 00
    03 00 00 00 06 00 00 00  E8 01 00 00 00 00 00 00  08 00 00 00 00 00 00 00  A2 06 EC F0 00 00 00 00
    0A 00 00 00 0F 00 00 00  F0 01 00 00 00 00 00 00  18 00 00 00 00 00 00 00  92 4C 6B 43 00 00 00 00
    02 00 00 00 14 00 00 00  08 02 00 00 00 00 00 00  08 00 00 00 00 00 00 00  F6 14 1A 58 00 00 00 00
    00 00 00 00 23 00 00 00  10 02 00 00 00 00 00 00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00

The rest of the file, from offset 448:

    448   00 50 00 00 00 00 00 00      navigation bits
    456   41 43 33 33 23 32 23 33      node counts
    464   00 00 00 00 00 00 00 00      counts before: row 0
    472   00 00 00 00 00 00 00 00
    480   00 00 00 00 00 00 00 00      class 1
    488   6D 93 00 00 00 00 00 00      class 2
    496   B1 84 94 40 4A B0 46 11      class 3
    504   42 60 08 30 0D 99 62 25
    512   14 86 08 00 00 00 00 00
    520   61 0D 12 84 0C 00 00 00      class 4
    528   00 00 00 00 00 00 00 00

**Navigation bits.** Byte 1 is `50`: the bits at places 12 and 14. Place 12
= 2^3 + 4 is the string 0100: 01000 needs 3 nodes and 01001 needs 2. Place
14 is 0110: 01100 needs 3 and 01101 needs 2. Every other stored string has
0: for 010, at place 6, the fewest nodes under 0100 and under 0101 are both
2.

**Node counts.** `41`: string 00000 has 1 node (the low half), 00001 has 4.
`43`: 00010 has 3, 00011 has 4. And so on to `33`: 01110 and 01111 have 3.

**A lookup: 01101.** s = 13. Its node count is the high half of byte 6,
`23`: 2. Row 0 has 0 for 2 nodes, and of the strings 0 to 12 two have 2
nodes (01001 and 01010): its REV is record 2 of class 2. The records of
class 2 are 6 bits: `6D 93` is the number `0x936D`, and its bits 12 to 17
are 001001. The low 2 bits, 01, are the colors: node 0 black, node 1 white.
Then one bit a link: node 0 leads to 0 on color 0 and to 1 on color 1; node
1 to 0 and 0. That is the REV of the table.

**A shorter string: 011.** It starts with 0; its place is 2^2 + 3 = 7. The
bit at place 7 is 0: place 14. The bit at place 14 is 1: place 29 = 2^4 +
13. The answer is string 13, 01101, with 2 nodes: 011 needs 2 nodes, and
01101 is the lowest of its extensions that need no more.

**A string that starts with 1: 10.** Its complement is 01, at place 2^1 +
1 = 3. Bit 0: place 6. Bit 0: place 12. Bit 1: place 25 = 2^4 + 9, string
01001, the lowest of the three extensions of 01 that need 2 nodes (01001,
01010 and 01101). So 10 is answered by 10110, the highest of the three
extensions of 10 that need 2 (10010, 10101 and 10110), with the complement
of the REV of 01001: node 0 white with links 1 and 1, node 1 black with
links 1 and 0.

**The comparison for 01.** Its extensions are 010 and 011. From 010, place
6: bit 0, place 12; bit 1, place 25 = 2^4 + 9: string 01001, 2 nodes. From
011: string 01101, 2 nodes. Neither needs fewer.

**A record of class 3.** The first, of 00010: `B1 84` is `0x84B1`, of which
15 bits: `0x04B1`. Colors 001: node 0 black. Then 2 bits a link: node 0
leads to 2 and 1, node 1 to 1 and 2, node 2 to 0 and 0.

**A rank.** No string of 5 bits needs 6 nodes; the first that does at 11
bits is 00000000001. Its REV in the table, and the same nodes numbered
canonically (old 1 becomes 2, old 2 becomes 1):

    table:      0: B 2 1    1: W 1 4    2: W 3 3    3: W 0 5    4: W 2 0    5: W 4 0
    canonical:  0:   1 2    1:   3 3    2:   2 4    3:   0 5    4:   1 0    5:   4 0

The list of links is 1 2 3 3 2 4 0 5 1 0 4 0, and its rank among the
5,931,540 graphs of 6 nodes is **4,581,456**: the 23 bits of its record. A
reader gets the canonical graph from the rank, runs it against 00000000001,
and finds the colors black for node 0 and white for the others, the nodes
reached in the order 0, 2, 1, 3, 4, 5: renumbered in that order, the REV of
the table.

**The search, on a small graph.** The graph of 01001 above (node 0 leads to
1 and 1, node 1 to 0 and 1), against 01001. Bit 1: the root has no color and
gets 0, a choice; cp moves to node 1, which gets the bit, 0. Bit 2: fp moves
to node 1; cp moves from node 1 on color 0 to node 0, which has color 0
where the bit is 1. Back to the choice: the root is 1. Bit 1 again: cp moves
to node 1, which gets 0. Bit 2: fp to node 1; cp to node 0, color 1, as the
bit. Bits 3, 4 and 5 find the colors they need: 0 at node 1, 0 at node 1, 1
at node 0. Colors: black, white; the nodes were reached in the order 0, 1.

### 12.13 Sizes

For the tables of the ladder, built as OVERNIGHT.md describes:

| L | Fixed-width, bytes | Compact, bytes | | REVs | Node counts | Counts before | Navigation bits | Bits a string |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 | 1,245,248 | 129,680 | 10.4% | 105,160 | 16,384 | 3,584 | 4,096 | 15.8 |
| 17 | 4,456,512 | 253,192 | 5.7% | 203,584 | 32,768 | 8,192 | 8,192 | 15.5 |
| 18 | 8,912,960 | 508,872 | 5.7% | 410,112 | 65,536 | 16,384 | 16,384 | 15.5 |
| 19 | 17,825,856 | 1,042,448 | 5.8% | 845,384 | 131,072 | 32,768 | 32,768 | 15.9 |
| 20 | 35,651,648 | 2,138,680 | 6.0% | 1,745,008 | 262,144 | 65,536 | 65,536 | 16.3 |
| 21 | 71,303,232 | 4,386,800 | 6.2% | 3,583,528 | 524,288 | 147,456 | 131,072 | 16.7 |
| 22 | 142,606,400 | 9,000,848 | 6.3% | 7,394,760 | 1,048,576 | 294,912 | 262,144 | 17.2 |
| 23 | 285,212,736 | 18,673,000 | 6.5% | 15,461,280 | 2,097,152 | 589,824 | 524,288 | 17.8 |
| 24 | 570,425,408 | 38,858,512 | 6.8% | 32,435,528 | 4,194,304 | 1,179,648 | 1,048,576 | 18.5 |

The share is of the fixed-width file, whose entries are 19 bytes at 16 bits
and 34 from 17. A string costs 16 to 19 bits, growing with the node counts:
most strings of 16 bits need 6 nodes (23 bits a REV, for two strings), most
of 24 bits need 7 or 8 (28 and 34).

### 12.14 The layout of the draft

While this format was a draft (pull request 8, before 13 November 2026) the
number 4 was used for another layout: a prefix index of ordinals and REVs as
bit fields, with 31 at offset 13 and the bytes of an ordinal, 1 to 4, at
offset 14. No file of it was ever released. A reader of version 4 rejects
such a file at offset 13; the tool says what it is. It has to be made again
from its fixed-width file.
