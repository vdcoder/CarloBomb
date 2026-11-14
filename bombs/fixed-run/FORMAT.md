# FixedRunBomb file format, versions 1, 2 and 3

A FixedRunBomb is a table with one entry for every binary string of one fixed
length L. An entry holds a REV that emits the string as its first L output
bits, and says whether that REV is certified to have the minimum possible
number of nodes.

There are three versions of the format. They differ in the size of an entry
and in nothing else:

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

This document specifies two things:

1. the **table file** (`.frb`), the product: portable, fixed-size, addressed
   directly by the string;
2. the **construction journal**, the working files of `carlobomb bomb build`
   and `bomb ladder`, from which a table is derived. A reader of tables can
   skip that part.

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
tables of up to 24; version 3 that of tables of up to 31. A change that an
existing reader would misread increases the format version, and readers
reject versions they do not know rather than guess: a reader that knows
only versions 1 and 2 rejects a version-3 file at the version field. The
header size field lets a later version add header fields without moving the
table; readers of versions 1 to 3 require it to be 64. The journal carries the version of the table format it serves, and
the search revision is a third number, for the reasons given in section
9.1.
