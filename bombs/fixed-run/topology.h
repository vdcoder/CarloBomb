/*
 * The graph of a REV without its colors, and what the compact format
 * (version 4, compact.h) needs of it:
 *
 *   - the canonical numbering of its nodes: breadth-first from the root,
 *     port 0 before port 1;
 *   - the rank of a canonically numbered graph among all such graphs of the
 *     same number of nodes, and the graph of a rank: the compact format
 *     stores a REV of 6 or more nodes as that one number;
 *   - the search for the colors: given the links and the string the REV has
 *     to emit, the colors with which it does, found by running the machine
 *     and letting the string decide every color it can.
 *
 * The machine (core/rev/SPEC.md): fp = cp = root.  Bit 0: cp = link[cp] on
 * the color of fp; emit the color of cp.  Every later bit: fp = link[fp] on
 * the color of cp, then cp = link[cp] on the color of fp; emit the color of
 * cp.
 *
 * FORMAT.md, section 12, specifies the numbering, the rank and the search:
 * a reader in another language has to arrive at the same REVs.
 */
#ifndef CARLOBOMB_FIXED_RUN_TOPOLOGY_H
#define CARLOBOMB_FIXED_RUN_TOPOLOGY_H

#include <stdint.h>

#include "rev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRB_TOPOLOGY_MAX_NODES 12 /* the rank of a graph of 12 nodes still fits 58 bits */

typedef struct {
    uint8_t n;                               /* nodes, 1..FRB_TOPOLOGY_MAX_NODES; node 0 is the root */
    uint8_t link[2][FRB_TOPOLOGY_MAX_NODES]; /* link[c][i]: where node i leads on color c */
} FrbTopology;

/*
 * Fills the tables behind the count, the rank and the graph of a rank.  The
 * functions below do it themselves on first use, which is not safe if that
 * first use happens on two threads at once: a program with several threads
 * calls this once before they start (frb_compact_open and frb_compact_write
 * do).
 */
void frb_topology_init(void);

/* How many canonically numbered graphs of that many nodes there are, with
 * every node reachable from the root; 0 outside 1..12. */
uint64_t frb_topology_count(int nodes);

/* Bits that hold any rank below that count; -1 outside 1..12. */
int frb_topology_rank_bits(int nodes);

/* Links and colors apart.  0 if the graph has no nodes or more than 12. */
int frb_topology_from_graph(const RevGraph *g, FrbTopology *t, uint32_t *colors);
void frb_topology_to_graph(const FrbTopology *t, uint32_t colors, RevGraph *g);

/*
 * A numbering applied: perm[old] = new, a permutation of 0..n-1 that keeps
 * the root (perm[0] = 0).  colors_in and colors_out may be NULL together.
 */
void frb_topology_renumber(const FrbTopology *in, uint32_t colors_in, const uint8_t perm[FRB_TOPOLOGY_MAX_NODES],
                           FrbTopology *out, uint32_t *colors_out);

/*
 * The canonical numbering: the root is 0; then the nodes in the order a
 * breadth-first search meets them that looks at the nodes in the order of
 * their new numbers and at port 0 before port 1.  Returns the number of
 * nodes numbered.  If that is t->n, *out is the graph so numbered and
 * perm[old] = new (perm may be NULL).  If it is less, some node cannot be
 * reached from the root, there is no canonical numbering, and *out is not
 * written.
 */
int frb_topology_canonical(const FrbTopology *in, FrbTopology *out, uint8_t perm[FRB_TOPOLOGY_MAX_NODES]);

/* 1 if the graph is numbered that way already. */
int frb_topology_is_canonical(const FrbTopology *t);

/* The rank of a canonically numbered graph: 0 .. frb_topology_count(n) - 1,
 * each graph another one. */
uint64_t frb_topology_rank(const FrbTopology *t);

/* The graph of a rank.  0 if nodes is outside 1..12 or the rank is not below
 * the count. */
int frb_topology_unrank(int nodes, uint64_t rank, FrbTopology *t);

/*
 * The colors.  Runs the machine on the links for `bits` bits (1..32) against
 * target, whose first bit is its highest of those.  Where the color of the
 * node under fp is not known yet, 0 is tried, and 1 only if 0 leads to a
 * contradiction further on; the node cp moves to gets the color the target
 * has at that bit, or, if it has a color and it is the other one, the search
 * goes back to the last color it tried 0 for.
 *
 * Returns 0 if no coloring emits the target.  Otherwise the number of nodes
 * the machine reached with the coloring found, which is the first one in
 * that order: *colors has it (bit i for node i; nodes not reached are 0),
 * and order[i], for a node that was reached, is its place in the order in
 * which the machine reached the nodes: the root 0, then at each bit first
 * the node fp moved to, then the node cp moved to.
 *
 * That is the order in which the search of the solvers tries colors (white
 * first) and numbers the nodes of the REV it finds, so for a REV found by
 * that search for a string that starts with 0 the colors found are its
 * colors and the order is its numbering.
 */
int frb_topology_find_colors(const FrbTopology *t, uint32_t target, int bits, uint32_t *colors,
                             uint8_t order[FRB_TOPOLOGY_MAX_NODES]);

/* The first `bits` bits (1..32) the REV emits, the first one highest. */
uint32_t frb_topology_output(const FrbTopology *t, uint32_t colors, int bits);

#ifdef __cplusplus
}
#endif

#endif
