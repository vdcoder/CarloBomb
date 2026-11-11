/*
 * CarloBomb REV core: graph representation, execution semantics and
 * independent verification.  See SPEC.md in this directory.
 *
 * Nothing in this module shares code with any solver.  It is the reference
 * against which solver output is checked.
 */
#ifndef CARLOBOMB_REV_H
#define CARLOBOMB_REV_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REV_WHITE 0
#define REV_BLACK 1
#define REV_UNDECIDED (-1) /* only legal while a graph is being synthesized */
#define REV_ROOT 0
#define REV_MAX_NODES 64 /* capacity of RevGraph, not a property of REVs */

typedef struct {
    int color;    /* REV_WHITE or REV_BLACK */
    int links[2]; /* node indices, selected by a color */
} RevNode;

typedef struct {
    int node_count; /* 1..REV_MAX_NODES */
    RevNode nodes[REV_MAX_NODES];
} RevGraph;

typedef enum {
    REV_OK = 0,
    REV_ERR_NULL,       /* NULL argument */
    REV_ERR_NODE_COUNT, /* node_count outside 1..REV_MAX_NODES */
    REV_ERR_COLOR,      /* a color is not 0 or 1 */
    REV_ERR_LINK,       /* a link does not point to an existing node */
    REV_ERR_TARGET,     /* target is empty or has a character other than '0'/'1' */
    REV_ERR_MISMATCH,   /* graph is valid but does not emit the target prefix */
    REV_ERR_FORMAT,     /* malformed graph text */
    REV_ERR_IO
} RevStatus;

const char *rev_status_str(RevStatus s);

/* A graph is executable iff this returns REV_OK. */
RevStatus rev_graph_validate(const RevGraph *g);

/* A target is a nonempty sequence of the characters '0' and '1'. */
RevStatus rev_target_validate(const char *target, size_t len);

/*
 * Bit-at-a-time interpreter.  The graph must already be valid; these two
 * functions do no checking.
 */
typedef struct {
    int fp, cp;
    size_t emitted;
} RevMachine;

void rev_machine_reset(RevMachine *m);
int rev_machine_next_bit(const RevGraph *g, RevMachine *m); /* returns 0 or 1 */

/*
 * Validate g, then write its first nbits output bits to out as '0'/'1'
 * characters followed by a NUL.  out must hold nbits + 1 bytes.
 */
RevStatus rev_run(const RevGraph *g, size_t nbits, char *out);

/*
 * Validate g and the target, then check that the first len output bits of g
 * equal target exactly.  On REV_ERR_MISMATCH, *mismatch_at (if not NULL)
 * receives the index of the first differing bit.
 */
RevStatus rev_verify_prefix(const RevGraph *g, const char *target, size_t len,
                            size_t *mismatch_at);

/*
 * Text format (see SPEC.md):
 *
 *     REV 1
 *     nodes 3
 *     0 B 2 1
 *     1 W 0 0
 *     2 W 1 2
 *
 * rev_graph_read validates the graph it returns.
 */
RevStatus rev_graph_write(FILE *f, const RevGraph *g);
RevStatus rev_graph_read(FILE *f, RevGraph *g);

#ifdef __cplusplus
}
#endif

#endif
