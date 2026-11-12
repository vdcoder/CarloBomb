/*
 * FixedRunBomb: a table that maps every binary string of one fixed length
 * L (1..16 bits) to a REV that emits it.
 *
 * FORMAT.md in this directory is the specification of the file.  This header
 * is the C interface to it: packing and unpacking of entries, the table in
 * memory, reading, writing and full validation of a file, and the exhaustive
 * graph enumerator.  Nothing here searches; every graph that enters a table
 * is run on the independent interpreter of core/rev first.
 *
 * Building a table (journal, restart, worker processes, statistics) is in
 * fixed_run_build.h.
 */
#ifndef CARLOBOMB_FIXED_RUN_H
#define CARLOBOMB_FIXED_RUN_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRB_FORMAT_VERSION 1
#define FRB_MAX_BITS 16      /* longest target the format indexes */
#define FRB_NODE_CAPACITY 16 /* nodes an entry can hold */
#define FRB_NODE_BITS 9      /* 1 color bit, 4 bits link 0, 4 bits link 1 */
#define FRB_GRAPH_BYTES 18   /* 16 nodes x 9 bits */
#define FRB_ENTRY_BYTES 19   /* packed graph + 1 metadata byte */
#define FRB_HEADER_BYTES 64

#define FRB_FLAG_COMPLETE 1u /* every entry is a certified minimum */

typedef enum {
    FRB_OK = 0,
    FRB_ERR_ARG,          /* invalid argument */
    FRB_ERR_MEMORY,
    FRB_ERR_IO,           /* cannot open, read, write or rename */
    FRB_ERR_MAGIC,        /* not a FixedRunBomb file */
    FRB_ERR_VERSION,      /* a format version this code does not read */
    FRB_ERR_HEADER,       /* a header field is impossible, or the header checksum fails */
    FRB_ERR_SIZE,         /* file length differs from what the header implies */
    FRB_ERR_CHECKSUM,     /* table checksum mismatch */
    FRB_ERR_ENTRY,        /* an entry is malformed: metadata, link or padding */
    FRB_ERR_OUTPUT,       /* a stored graph does not emit the string it is filed under */
    FRB_ERR_SUMMARY,      /* header counts or flags disagree with the table */
    FRB_ERR_CAPACITY,     /* a graph has more than 16 nodes: not representable */
    FRB_ERR_STOPPED,      /* stopped on request before finishing */
    FRB_ERR_INCOMPLETE,   /* the table is not complete and a complete one was required */
    FRB_ERR_JOURNAL,      /* construction journal is damaged */
    FRB_ERR_INCOMPATIBLE, /* work directory made with other settings or another revision */
    FRB_ERR_EVIDENCE,     /* recorded results contradict each other */
    FRB_ERR_BUSY,         /* work directory is in use by another process */
    FRB_ERR_EXISTS,       /* work directory already holds a construction */
    FRB_ERR_UNSUPPORTED   /* not available on this platform */
} FrbError;

const char *frb_error_str(FrbError e);

/* What an entry holds.  The numbers are the on-disk status codes. */
typedef enum {
    FRB_EMPTY = 0,      /* no witness, nothing known */
    FRB_WITNESS = 1,    /* valid witness; minimality not certified */
    FRB_MINIMAL = 2,    /* valid witness with a certified minimum node count */
    FRB_UNRESOLVED = 3, /* searched without a final answer; no witness */
    FRB_UNSUPPORTED = 4 /* shown to need more than 16 nodes; no witness */
} FrbStatus;

const char *frb_status_str(FrbStatus s);

/* ---- targets ------------------------------------------------------------
 *
 * A target of L bits is filed under the integer whose binary digits are the
 * target, first emitted bit most significant.  "0100100011010110" is index
 * 0x48D6 of a 16-bit table.
 */
int frb_target_parse(const char *text, int *bits, uint32_t *index); /* 1 if valid */
void frb_target_format(uint32_t index, int bits, char out[FRB_MAX_BITS + 1]);

/* The first `bits` output bits of a valid graph, as a table index. */
uint32_t frb_graph_output(const RevGraph *g, int bits);

/*
 * Complement symmetry.  Flipping every color and exchanging link 0 with
 * link 1 at every node gives a REV with the same node count whose output is
 * the bitwise complement of the original's.  (tests/test_fixed_run.c checks
 * this on every labeled graph of up to 4 nodes and on random larger ones.)
 */
void frb_graph_complement(const RevGraph *in, RevGraph *out);

/*
 * The ring: L nodes, both links of node i lead to node (i + 1) mod L, and
 * node (i + 1) mod L carries bit i of the target.  It emits the target, so
 * no L-bit string needs more than L nodes.  The same test checks it for
 * every string of every length 1..16.
 */
void frb_ring_witness(uint32_t index, int bits, RevGraph *out);

/* ---- entries ------------------------------------------------------------ */

/* 9 bits per node, 18 bytes; see FORMAT.md.  g must be valid, 1..16 nodes. */
FrbError frb_graph_pack(const RevGraph *g, uint8_t out[FRB_GRAPH_BYTES]);

/* Rejects a link to a missing node and nonzero bits in unused node slots. */
FrbError frb_graph_unpack(const uint8_t in[FRB_GRAPH_BYTES], int node_count, RevGraph *g);

/* g is required for FRB_WITNESS and FRB_MINIMAL and must be NULL otherwise. */
FrbError frb_entry_encode(FrbStatus status, const RevGraph *g, uint8_t out[FRB_ENTRY_BYTES]);

/* Checks everything that can be checked without knowing the entry's index.
 * g->node_count is 0 when the entry holds no witness. */
FrbError frb_entry_decode(const uint8_t in[FRB_ENTRY_BYTES], FrbStatus *status, RevGraph *g);

/* ---- table in memory ---------------------------------------------------- */

typedef struct {
    int bits;         /* L */
    uint32_t count;   /* 2^L */
    uint8_t *entries; /* count * FRB_ENTRY_BYTES bytes, exactly as in the file */
} FrbTable;

FrbError frb_table_create(FrbTable *t, int bits); /* every entry FRB_EMPTY */
void frb_table_destroy(FrbTable *t);

/* Store an entry.  A witness is run on the interpreter and must emit `index`. */
FrbError frb_table_put(FrbTable *t, uint32_t index, FrbStatus status, const RevGraph *g);

/* Decode an entry, validate it and, if it holds a witness, run it. */
FrbError frb_table_get(const FrbTable *t, uint32_t index, FrbStatus *status, RevGraph *g);

typedef struct {
    uint32_t count;
    uint32_t by_status[5];                             /* indexed by FrbStatus */
    uint32_t minimal_by_nodes[FRB_NODE_CAPACITY + 1];  /* FRB_MINIMAL entries */
    uint32_t witness_by_nodes[FRB_NODE_CAPACITY + 1];  /* FRB_WITNESS entries */
    int max_minimal_nodes; /* largest node count among FRB_MINIMAL entries, 0 if none */
    int max_witness_nodes; /* largest among FRB_WITNESS and FRB_MINIMAL, 0 if none */
    int complete;          /* 1 iff every entry is FRB_MINIMAL */
} FrbSummary;

/* Validate every entry as frb_table_get does.  On failure *bad_index (if not
 * NULL) is the first offending entry. */
FrbError frb_table_validate(const FrbTable *t, FrbSummary *summary, uint32_t *bad_index);

/*
 * Complement symmetry of a valid table.  For every stored witness the
 * complement graph must emit the complement string; a certified minimum must
 * equal the certified minimum of the complement string, and may not exceed
 * the node count of any witness stored for it.  On FRB_ERR_EVIDENCE
 * *bad_index is the first entry that breaks the rule.
 */
FrbError frb_table_check_symmetry(const FrbTable *t, uint32_t *bad_index);

/* ---- file --------------------------------------------------------------- */

typedef struct {
    int version, bits, node_capacity;
    uint32_t count, flags, witnesses, minimal, table_crc;
    int max_minimal_nodes, max_witness_nodes;
    int complete; /* the FRB_FLAG_COMPLETE bit */
} FrbFileInfo;

/*
 * The file as a byte image.  frb_image_build validates the table and
 * computes the header from it; the complete flag is set iff every entry is a
 * certified minimum, never on request.  frb_image_parse performs every check
 * in FORMAT.md, including running every stored graph, and fills t (which the
 * caller destroys).
 */
FrbError frb_image_build(const FrbTable *t, uint8_t **image, size_t *size);
FrbError frb_image_parse(const uint8_t *image, size_t size, FrbTable *t, FrbFileInfo *info,
                         uint32_t *bad_index);

/* Write through a temporary file and a rename, so that `path` is either the
 * old file or the complete new one. */
FrbError frb_file_write(const char *path, const FrbTable *t);
FrbError frb_file_read(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index);

/*
 * Random access: read the header and one entry.  Validates the header, the
 * file length and that entry (including running its graph); does not read
 * the rest of the table, so it does not check the table checksum.
 */
FrbError frb_file_lookup(const char *path, uint32_t index, FrbFileInfo *info, FrbStatus *status,
                         RevGraph *g);

/* The header alone: validates it and the file length, reads no entry. */
FrbError frb_file_info(const char *path, FrbFileInfo *info);

/* The minimum-node histogram of a table and what may be concluded from it. */
void frb_report_summary(FILE *out, const FrbSummary *summary, int bits);

/* CRC-32 (IEEE 802.3, reflected, as in zlib and PNG).  Start with crc = 0. */
uint32_t frb_crc32(uint32_t crc, const void *data, size_t size);

/* Replace `path` with `size` bytes atomically (temporary file, then rename). */
FrbError frb_write_atomic(const char *path, const void *data, size_t size);

/* ---- exhaustive enumeration ---------------------------------------------
 *
 * Every labeled REV with exactly `nodes` nodes is one assignment of 2*nodes
 * links and nodes colors: nodes^(2*nodes) * 2^nodes graphs.  The enumeration
 * is cut into nodes^2 parts by the two links of node 0, so that parts can be
 * run separately; part p fixes link 0 of node 0 to p % nodes and link 1 to
 * p / nodes.  Within a part the remaining links count up as a base-`nodes`
 * number (link 0 of node 1 fastest) and, for each link assignment, the
 * colorings count up as a binary number (node 0 = bit 0).
 *
 * Each graph is run on the interpreter of core/rev for `bits` output bits.
 * The first graph of the part to emit a given string is reported through
 * `found`.  With root_white_only the part runs only colorings whose node 0
 * is white, and reports the first graph to emit a string or its complement:
 * by complement symmetry the skipped half adds no new pairs.
 */
typedef struct {
    int nodes;           /* 1..FRB_NODE_CAPACITY (large values are not practical) */
    int bits;            /* 1..FRB_MAX_BITS */
    int part;            /* 0 .. nodes*nodes - 1 */
    int root_white_only; /* 0 or 1 */
} FrbEnumPart;

/* Return nonzero to abort the part (it then returns FRB_ERR_STOPPED). */
typedef int (*FrbEnumFound)(void *ctx, uint32_t index, const RevGraph *g);

int frb_enum_part_count(int nodes);                             /* nodes * nodes */
uint64_t frb_enum_part_graphs(int nodes, int root_white_only);  /* graphs in one part; 0 if it overflows */

/*
 * Run one part.  *stop, if not NULL, is polled and a nonzero value ends the
 * part early with FRB_ERR_STOPPED.  *graphs receives the number of graphs
 * run, which on success equals frb_enum_part_graphs().
 */
FrbError frb_enumerate_part(const FrbEnumPart *part, FrbEnumFound found, void *ctx,
                            const volatile sig_atomic_t *stop, uint64_t *graphs);

#ifdef __cplusplus
}
#endif

#endif
