/*
 * FixedRunBomb, compact format (format version 4): the size-class graph pool
 * with a global-ordinal prefix index of issue #6.
 *
 * A complete FixedRunBomb(L) holds one certified minimum-node REV for each of
 * the 2^L strings of L bits.  The fixed-width formats (versions 1 to 3) give
 * every one of them an entry large enough for the largest REV the format
 * allows.  The compact format stores the same REVs in the space they need:
 *
 *   - The REVs are sorted by (node count N, string).  The place of a REV in
 *     that order is its ordinal, 0 .. 2^L - 1.
 *   - All REVs of one node count form a class.  A REV of class N is N nodes
 *     of 1 color bit and two links of ceil(log2 N) bits, padded to a whole
 *     number of bytes: the stride of the class.  Nothing else is stored with
 *     it, not even N.
 *   - A class directory gives, for N = 1 .. 31, the number of REVs, the first
 *     ordinal, the place of the class in the file and its stride.
 *   - A prefix index gives, for every string of 1 to L bits, the ordinal of
 *     the REV that answers it: for a string of L bits its own, for a shorter
 *     one the smallest ordinal among its extensions, which by the order of
 *     the ordinals is the extension with the fewest nodes and, among those,
 *     the first.
 *
 * A compact file is made from a complete fixed-width file and holds exactly
 * its REVs: frb_compact_to_table gives the fixed-width table back, byte for
 * byte.  Only a complete table can be compact (there is no place for an entry
 * without a certified minimum), and the evidence for the minima stays where
 * it was: in the journal of the construction of the fixed-width file.
 *
 * FORMAT.md, section 12, is the specification.  Version 4 is the number of
 * this layout in the header of a .frb file; versions 1, 2 and 3 are the
 * fixed-width layouts and are not touched by anything here.
 */
#ifndef CARLOBOMB_FIXED_RUN_COMPACT_H
#define CARLOBOMB_FIXED_RUN_COMPACT_H

#include "fixed_run.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRB_FORMAT_COMPACT 4
#define FRB_COMPACT_CLASSES 31    /* node counts 1 .. 31 */
#define FRB_COMPACT_ROW_BYTES 32  /* one row of the class directory */
#define FRB_COMPACT_INDEX_OFFSET (FRB_HEADER_BYTES + FRB_COMPACT_CLASSES * FRB_COMPACT_ROW_BYTES) /* 1056 */

/* ---- one REV of a class -------------------------------------------------- */

int frb_compact_link_bits(int nodes); /* ceil(log2 nodes); 0 for one node; -1 outside 1..31 */
int frb_compact_stride(int nodes);    /* bytes of a REV of that many nodes; 0 outside 1..31 */

/* g must be valid, with 1..31 nodes; out receives frb_compact_stride(nodes)
 * bytes.  With one node its two links, which can only lead to itself, are
 * not stored. */
FrbError frb_compact_pack(const RevGraph *g, uint8_t *out);

/* Rejects a link to a missing node and nonzero bits after the last node. */
FrbError frb_compact_unpack(const uint8_t *in, int nodes, RevGraph *g);

/* ---- the file ------------------------------------------------------------ */

typedef struct {
    int bits;        /* L */
    int index_bytes; /* bytes per ordinal in the prefix index: 1..4 */
    int max_nodes;   /* the largest node count that has a REV */
    uint32_t count;  /* 2^L */
    uint32_t class_count[FRB_COMPACT_CLASSES + 1];  /* [N]: REVs of N nodes; [0] unused */
    uint32_t class_start[FRB_COMPACT_CLASSES + 1];  /* [N]: ordinal of the first */
    uint32_t class_stride[FRB_COMPACT_CLASSES + 1]; /* [N]: bytes per REV */
    uint64_t class_offset[FRB_COMPACT_CLASSES + 1]; /* [N]: file offset of the first */
    uint32_t class_crc[FRB_COMPACT_CLASSES + 1];    /* [N]: CRC-32 of the class's bytes */
    uint64_t index_entries;                         /* 2^(L+1) - 2 */
    uint64_t index_offset, pool_offset, file_bytes;
    uint32_t index_crc, directory_crc;
    int source_version;        /* format version of the fixed-width file it was made from */
    uint32_t source_table_crc; /* and that file's table checksum */
} FrbCompactInfo;

/* Sizes of a compact file before it exists: fills count, strides, starts,
 * offsets and file_bytes from the class counts in info->class_count, which
 * must add up to 2^bits.  FRB_ERR_ARG if they do not, or index_bytes cannot
 * hold an ordinal of that many bits (0 = the fewest bytes that can). */
FrbError frb_compact_layout(FrbCompactInfo *info, int bits, int index_bytes);

/* 1 if the file starts with the FixedRunBomb magic and says version 4;
 * 0 if it is a FixedRunBomb file of another version; -1 if it is neither
 * or cannot be read. */
int frb_file_is_compact(const char *path);

/* Header and class directory: checksums, every number recomputed from the
 * class counts and compared, the file length.  Reads 1056 bytes. */
FrbError frb_compact_file_info(const char *path, FrbCompactInfo *info);

/*
 * One question answered from the file, without reading the rest of it: the
 * REV for the string of `bits` bits (1..L) whose value is `value`.  Checks
 * the header and directory as frb_compact_file_info does, reads one index
 * entry and one REV, and runs the REV: it must emit the string.  The index
 * and pool checksums are not verified (that takes the whole file).
 * *extension, if not NULL, receives the L-bit string the REV stands for.
 */
FrbError frb_compact_file_lookup(const char *path, int bits, uint32_t value, FrbCompactInfo *info,
                                 RevGraph *g, uint32_t *extension);

/* ---- a compact Bomb in use ----------------------------------------------- */

typedef struct FrbCompact FrbCompact;

/*
 * Open and validate in full: header, directory, length and all checksums;
 * every ordinal of the leaves in its class and in the order the format
 * prescribes; every REV decoded and run for L bits against the string whose
 * leaf points to it; every inner index entry the smaller of its two
 * children.  The file is mapped where the system can, else read.  On
 * FRB_ERR_ENTRY or FRB_ERR_OUTPUT *bad_index (if not NULL) is the first
 * string of L bits at which the file is wrong.
 */
FrbError frb_compact_open(const char *path, FrbCompact **bomb, uint32_t *bad_index);
void frb_compact_close(FrbCompact *bomb);
const FrbCompactInfo *frb_compact_info(const FrbCompact *bomb);

/* For the string of `bits` bits (1..L) with value `value`: the ordinal of
 * its REV, the node count of that REV (no REV is touched for it), and the
 * REV itself.  bits and value must be in range. */
uint32_t frb_compact_ordinal(const FrbCompact *bomb, int bits, uint32_t value);
int frb_compact_nodes(const FrbCompact *bomb, int bits, uint32_t value);
FrbError frb_compact_graph(const FrbCompact *bomb, int bits, uint32_t value, RevGraph *g);
int frb_compact_nodes_of_ordinal(const FrbCompact *bomb, uint32_t ordinal);

/* A string and its complement need the same number of nodes, and the
 * complement of a REV emits the complement string (it is run). */
FrbError frb_compact_check_symmetry(const FrbCompact *bomb, uint32_t *bad_index);

/* ---- to and from the fixed-width formats ---------------------------------- */

/*
 * Write the compact file for a complete fixed-width table.  The table is
 * validated in full first (FRB_ERR_INCOMPLETE if any entry is not a
 * certified minimum).  The file is built under a temporary name, opened as
 * frb_compact_open opens any file, and compared with the table, REV for REV
 * and prefix for prefix; only then does it get its name.  `path` is the old
 * file or a complete, verified new one.  source_version and
 * source_table_crc are recorded in the header: the format version and table
 * checksum of the file the table was read from (0 and the table's own
 * checksum if it never was a file).  index_bytes: 0 for the fewest bytes
 * that hold an ordinal, or up to 4.
 */
FrbError frb_compact_write(const char *path, const FrbTable *table, int source_version,
                           uint32_t source_table_crc, int index_bytes);

/* The same REVs, the same order of preference: every string of L bits has
 * the REV the table has for it (FRB_ERR_EVIDENCE and *bad_index otherwise),
 * and every shorter string is answered by its extension with the fewest
 * nodes and, among those, the lowest index, worked out from the table
 * alone.  One pass over both, in a constant amount of memory. */
FrbError frb_compact_equals_table(const FrbCompact *bomb, const FrbTable *table, uint32_t *bad_index);

/* The fixed-width table a compact Bomb stands for, on the heap, in the
 * given format (NULL: the format of the file it was made from, or the one
 * its length calls for).  Written with frb_file_write it is that file. */
FrbError frb_compact_to_table(const FrbCompact *bomb, const FrbFormat *format, FrbTable *table);

/* The table checksum that file has, without making it: for a compact file
 * that names its source, this must be the checksum it names. */
FrbError frb_compact_table_crc(const FrbCompact *bomb, const FrbFormat *format, uint32_t *table_crc);

/*
 * That file, written entry by entry without holding the table, then read
 * back as any reader would and compared with the compact Bomb, REV for REV
 * and prefix for prefix.  A file that fails the comparison is removed.
 * *table_crc, if not NULL, receives its table checksum.
 */
FrbError frb_compact_expand(const FrbCompact *bomb, const FrbFormat *format, const char *path,
                            uint32_t *table_crc);

#ifdef __cplusplus
}
#endif

#endif
