/*
 * FixedRunBomb, compact format (format version 4).
 *
 * A complete FixedRunBomb(L) holds one certified minimum-node REV for each of
 * the 2^L strings of L bits.  The fixed-width formats (versions 1 to 3) give
 * every one of them an entry large enough for the largest REV the format
 * allows.  The compact format stores what is essential and derives the rest:
 *
 *   - Of every string and its complement only the one that starts with 0 is
 *     stored.  A string that starts with 1 is answered by the complement of
 *     the answer of its complement.
 *   - For every stored string of L bits: its node count in 4 bits, and its
 *     REV.  The REVs are kept by node count, in the order of the strings, so
 *     the place of a REV is the number of stored strings of its node count
 *     before it.  That number is kept for every 256th string and counted
 *     from there.
 *   - A REV of 6 or more nodes is one number: the rank of its graph among all
 *     graphs of that many nodes, numbered canonically (topology.h).  Its
 *     colors are not stored.  They are found by running the machine on the
 *     graph against the string.  A REV of up to 5 nodes is stored with its
 *     links and colors as they are.
 *   - For every stored string of 1 to L-1 bits: one navigation bit.  0: an
 *     extension of the string with the fewest nodes lies under its extension
 *     by 0.  1: only under its extension by 1.  Following the bits from a
 *     string leads to an L-bit extension of it with the fewest nodes, and the
 *     node count read there is the minimum of the string itself.
 *
 * Nothing else is in the file: no colors for the larger REVs, nothing for a
 * string that starts with 1, no positions, no graph for several strings.
 *
 * What a caller may rely on:
 *
 *   1. Node counts are exact: the certified minimum of the Bomb the file was
 *      made from, for every string of 1 to L bits.
 *   2. The REV of a string emits it, with that many nodes.  It has the graph
 *      of the REV in the source.  Where several colorings of that graph emit
 *      the string, the first one the search finds is returned.  The header
 *      says for how many strings the REV returned is not, link for link and
 *      color for color, the REV of the source; for Bombs built by the
 *      construction of this project that number is 0.
 *   3. A string of fewer than L bits is answered by one of its L-bit
 *      extensions with the fewest nodes: the lowest of them if the string
 *      starts with 0, the highest if it starts with 1.
 *   4. Complement in, complement out: for node counts, extensions and REVs.
 *   5. Which of the two extensions of a string by one bit needs fewer nodes,
 *      or neither, is worked out on demand from the node counts of the two
 *      (frb_compact_compare).  Nothing is stored for it.
 *
 * A compact file is made from a complete fixed-width file, after the
 * construction, and never the other way round: only a complete table can be
 * compact, and the evidence for the minima stays where it was, in the
 * journal of the construction.  If the header's number of other REVs is 0,
 * frb_compact_expand gives the fixed-width file back, byte for byte.
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
#define FRB_COMPACT_MAX_NODES 12  /* the largest node count a compact file can hold */
#define FRB_COMPACT_PLAIN_NODES 5 /* up to here a REV is stored with links and colors */
#define FRB_COMPACT_ROW_BYTES 32  /* one row of the class directory */
#define FRB_COMPACT_INDEX_OFFSET (FRB_HEADER_BYTES + FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES) /* 448 */
#define FRB_COMPACT_BLOCK_SHIFT 8 /* the counts before a string are kept for every 2^8th */
#define FRB_COMPACT_TIE 2         /* frb_compact_compare: the two extensions need the same */

/* Bits of the record of a REV of that many nodes: its rank from 6 nodes on,
 * its colors and links below; 0 outside 1..12. */
int frb_compact_record_bits(int nodes);

/* ---- the file ------------------------------------------------------------ */

typedef struct {
    int bits;         /* L */
    int max_nodes;    /* the largest node count that has a REV */
    int block_shift;  /* FRB_COMPACT_BLOCK_SHIFT */
    uint32_t count;   /* 2^L: the strings answered */
    uint32_t stored;  /* 2^(L-1): those that start with 0 */
    uint32_t class_count[FRB_COMPACT_MAX_NODES + 1]; /* [N]: stored REVs of N nodes; [0] unused */
    uint32_t class_bits[FRB_COMPACT_MAX_NODES + 1];  /* [N]: bits of one record */
    uint64_t class_offset[FRB_COMPACT_MAX_NODES + 1]; /* [N]: file offset of the class */
    uint64_t class_bytes[FRB_COMPACT_MAX_NODES + 1];  /* [N]: its length, a multiple of 8 */
    uint32_t class_crc[FRB_COMPACT_MAX_NODES + 1];    /* [N]: CRC-32 of those bytes */
    uint64_t nav_offset, nav_bytes;     /* the navigation bits */
    uint64_t nodes_offset, nodes_bytes; /* the node counts */
    uint64_t blocks_offset, blocks_bytes; /* the counts before every 256th string */
    uint32_t block_rows;
    uint64_t file_bytes;
    uint32_t index_crc, directory_crc;
    int source_version;        /* format version of the fixed-width file it was made from */
    uint32_t source_table_crc; /* and that file's table checksum */
    uint32_t other_revs;       /* strings of L bits whose REV is not the REV of the source */
} FrbCompactInfo;

/* Sizes of a compact file before it exists: fills everything but the
 * checksums and the source from the class counts in info->class_count, which
 * must add up to 2^(bits-1).  FRB_ERR_ARG if they do not. */
FrbError frb_compact_layout(FrbCompactInfo *info, int bits);

/* 1 if the file starts with the FixedRunBomb magic and says version 4;
 * 0 if it is a FixedRunBomb file of another version; -1 if it is neither
 * or cannot be read. */
int frb_file_is_compact(const char *path);

/* 1 if the file is a version-4 file of the layout this format had while it
 * was a draft (a prefix index of ordinals, never released).  Such a file is
 * refused by everything here with FRB_ERR_HEADER and has to be made again
 * from its fixed-width file. */
int frb_compact_file_is_draft(const char *path);

/* Header and class directory: checksums, every number recomputed from the
 * class counts and compared, the file length.  Reads 448 bytes. */
FrbError frb_compact_file_info(const char *path, FrbCompactInfo *info);

/*
 * One question answered from the file without validating the rest of it: the
 * REV for the string of `bits` bits (1..L) whose value is `value`.  Checks
 * the header and directory as frb_compact_file_info does, maps the file
 * (where it cannot be mapped, reads it), looks the string up, and runs the
 * REV: it must emit the string.  The checksums of the index and the classes
 * are not verified (that takes the whole file).  *extension, if not NULL,
 * receives the L-bit string the REV stands for.
 */
FrbError frb_compact_file_lookup(const char *path, int bits, uint32_t value, FrbCompactInfo *info,
                                 RevGraph *g, uint32_t *extension);

/*
 * frb_compact_compare for one string, put to the file the same way: which of
 * the two extensions by one bit of the string of `bits` bits (0..L-1) needs
 * fewer nodes.  *which receives 0, 1 or FRB_COMPACT_TIE, and nodes, if not
 * NULL, the two node counts.
 */
FrbError frb_compact_file_compare(const char *path, int bits, uint32_t value, FrbCompactInfo *info, int *which,
                                  int nodes[2]);

/* ---- a compact Bomb in use ----------------------------------------------- */

typedef struct FrbCompact FrbCompact;

/*
 * Open and validate in full: header, directory, length and all checksums;
 * every node count in range and the counts of the classes and of the blocks
 * what the node counts add up to; every navigation bit what the node counts
 * under it call for; every REV decoded, its colors found, and run for L bits
 * against its string.  The file is mapped where the system can, else read.
 * On FRB_ERR_ENTRY or FRB_ERR_OUTPUT *bad_index (if not NULL) is the first
 * string of L bits at which the file is wrong.  FRB_ERR_OUTPUT: a REV that
 * does not emit its string, with any colors.  FRB_ERR_ENTRY: a node count, a
 * count of a block or a navigation bit that is not what the others make it;
 * a record that is no graph; a graph that emits the string without one of
 * its nodes, which is the REV of that string for no number of nodes.
 *
 * A file that passes is consistent in itself.  That its node counts are
 * minima is certified by the construction of its source and by nothing in
 * the file.
 */
FrbError frb_compact_open(const char *path, FrbCompact **bomb, uint32_t *bad_index);
void frb_compact_close(FrbCompact *bomb);
const FrbCompactInfo *frb_compact_info(const FrbCompact *bomb);

/*
 * For the string of `bits` bits (1..L) with value `value`:
 *
 * frb_compact_extension: the L-bit string that answers it: itself, or the
 * extension the navigation bits lead to.
 * frb_compact_nodes: its minimum node count.  No REV is touched for it.
 * frb_compact_graph: its REV.
 *
 * bits and value must be in range.  A Bomb is read-only once opened: any
 * number of threads may ask at once.
 */
uint32_t frb_compact_extension(const FrbCompact *bomb, int bits, uint32_t value);
int frb_compact_nodes(const FrbCompact *bomb, int bits, uint32_t value);
FrbError frb_compact_graph(const FrbCompact *bomb, int bits, uint32_t value, RevGraph *g);

/*
 * On demand: of the two extensions by one bit of the string of `bits` bits
 * (0..L-1), which needs fewer nodes?  0 or 1, or FRB_COMPACT_TIE if they need
 * the same; -1 if bits or value is out of range.  nodes, if not NULL,
 * receives the two node counts.
 *
 * From each of the two extensions the navigation bits are followed to an
 * L-bit string and the node count read there.  No REV is decoded and no
 * color searched.  For a string of L bits the question takes the Bomb of
 * L+1.
 */
int frb_compact_compare(const FrbCompact *bomb, int bits, uint32_t value, int nodes[2]);

/* ---- to and from the fixed-width formats ---------------------------------- */

/*
 * Write the compact file for a complete fixed-width table.  The table is
 * validated in full first (FRB_ERR_INCOMPLETE if any entry is not a
 * certified minimum).  Refused, with nothing written:
 *
 *   FRB_ERR_CAPACITY   a REV of more than 12 nodes;
 *   FRB_ERR_EVIDENCE   a string and its complement with different node
 *                      counts, or a REV with a node its string does not need:
 *                      such a table is no table of minima.
 *
 * The file is built under a temporary name, opened as frb_compact_open opens
 * any file, and compared with the table (frb_compact_equals_table); only then
 * does it get its name.  `path` is the old file or a complete, verified new
 * one.  source_version and source_table_crc are recorded in the header: the
 * format version and table checksum of the file the table was read from (0
 * and the table's own checksum if it never was a file).
 */
FrbError frb_compact_write(const char *path, const FrbTable *table, int source_version, uint32_t source_table_crc);

/* Where a compact Bomb and a table of the same strings differ without either
 * being wrong. */
typedef struct {
    uint64_t other_revs;      /* strings of L bits: another REV than the table's (same graph, same node count) */
    uint64_t other_next_bit;  /* of those: REVs that go on with another bit L+1 */
    uint64_t other_extension; /* shorter strings answered by another extension than the lowest of the fewest-nodes ones */
} FrbCompactDifference;

/*
 * A compact Bomb against a complete table of the same strings, worked out
 * from the table alone.  One pass over both, in a constant amount of memory.
 * FRB_ERR_EVIDENCE, with *bad_index the first string of L bits at or under
 * which it happens, if
 *
 *   - a string of L bits has another node count than in the table, or a REV
 *     that does not emit it;
 *   - a shorter string is not answered by one of its extensions with the
 *     fewest nodes, or not by the one rule 3 above names;
 *   - frb_compact_compare says something else than the node counts of the
 *     table do, for any string of 0 to L-1 bits.
 *
 * Otherwise FRB_OK, and *difference (if not NULL) says how many REVs are
 * not the table's own.
 */
FrbError frb_compact_against_table(const FrbCompact *bomb, const FrbTable *table, FrbCompactDifference *difference,
                                   uint32_t *bad_index);

/* The same, and the number of other REVs must be the one the header of the
 * compact file gives: for a file whose header says 0, every string has the
 * REV the table has for it. */
FrbError frb_compact_equals_table(const FrbCompact *bomb, const FrbTable *table, uint32_t *bad_index);

/* The fixed-width table of the REVs a compact Bomb returns, on the heap, in
 * the given format (NULL: the format of the file it was made from, or the
 * one its length calls for).  If the header's number of other REVs is 0,
 * written with frb_file_write it is the source file. */
FrbError frb_compact_to_table(const FrbCompact *bomb, const FrbFormat *format, FrbTable *table);

/* The table checksum that file has, without making it: for a compact file
 * that names its source and has no other REVs, this must be the checksum it
 * names. */
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
