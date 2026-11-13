/*
 * Colorless REVs: an experiment, not a format.
 *
 * A REV is a rooted graph whose nodes have a color and two outgoing links,
 * one per color.  This header separates the two: the TOPOLOGY (the links,
 * with their ports 0 and 1) and the COLORS (one bit per node), and provides
 * what the experiment of PR 8 needs to measure:
 *
 *   - a canonical numbering of the nodes of a topology (root-preserving
 *     breadth-first search, port 0 before port 1), so that the same topology
 *     is the same bytes whatever numbering its nodes had;
 *   - the reconstruction of the colors from a topology and the string the
 *     REV has to emit, by a search that follows the machine and lets the
 *     string decide every color it can ("recoloring");
 *   - the brute-force reference for that (all 2^N colorings);
 *   - the numbering a LevelSolver witness has (nodes in the order the machine
 *     first reaches them), to turn a canonical REV back into the original;
 *   - two whole-topology codes: links as digits in base N, and the exact rank
 *     of a canonical topology among all canonical topologies of N nodes.
 *
 * Nothing here reads or writes a Bomb file.  The fixed-width and compact
 * formats are untouched; this code only looks at REVs handed to it.
 *
 * The machine (core/rev/SPEC.md): fp = cp = root.  Bit 0: cp = link[cp] on
 * the color of fp; emit the color of cp.  Every later bit: fp = link[fp] on
 * the color of cp, then cp = link[cp] on the color of fp; emit the color of
 * cp.
 */
#ifndef CARLOBOMB_COLORLESS_H
#define CARLOBOMB_COLORLESS_H

#include <stdint.h>
#include <string.h>

#include "rev.h"

#ifdef __cplusplus
extern "C" {
#endif

__extension__ typedef unsigned __int128 ClU128; /* GCC and Clang; the experiment needs no other compiler */

#define CL_MAX_NODES 15 /* every certified Bomb to 31 bits has far fewer; a key holds 15 */
#define CL_MAX_BITS 32  /* a target is the low L bits of a 32-bit number, first bit highest */

typedef struct {
    uint8_t n;                      /* nodes, 1..CL_MAX_NODES */
    uint8_t link[2][CL_MAX_NODES];  /* link[c][i]: where node i leads on color c */
} ClTopo;

/* ---- conversion ----------------------------------------------------------- */

/* 0 if the graph has more nodes than this experiment handles. */
static inline int cl_from_graph(const RevGraph *g, ClTopo *t, uint32_t *colors) {
    uint32_t c = 0;
    if (g->node_count < 1 || g->node_count > CL_MAX_NODES) return 0;
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)g->node_count;
    for (int i = 0; i < g->node_count; i++) {
        t->link[0][i] = (uint8_t)g->nodes[i].links[0];
        t->link[1][i] = (uint8_t)g->nodes[i].links[1];
        c |= (uint32_t)(g->nodes[i].color & 1) << i;
    }
    *colors = c;
    return 1;
}

static inline void cl_to_graph(const ClTopo *t, uint32_t colors, RevGraph *g) {
    g->node_count = t->n;
    for (int i = 0; i < t->n; i++) {
        g->nodes[i].color = (int)((colors >> i) & 1u);
        g->nodes[i].links[0] = t->link[0][i];
        g->nodes[i].links[1] = t->link[1][i];
    }
}

static inline int cl_same_topo(const ClTopo *a, const ClTopo *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->link[0][i] != b->link[0][i] || a->link[1][i] != b->link[1][i]) return 0;
    return 1;
}

/* ---- the machine ---------------------------------------------------------- */

typedef struct {
    uint8_t fp, cp;
} ClState;

/* The first `bits` output bits (first bit highest), and the state the machine
 * is in after emitting them. */
static inline uint32_t cl_output(const ClTopo *t, uint32_t colors, int bits, ClState *after) {
    unsigned fp = 0, cp = 0;
    uint32_t out = 0;
    for (int k = 0; k < bits; k++) {
        if (k) fp = t->link[(colors >> cp) & 1u][fp];
        cp = t->link[(colors >> fp) & 1u][cp];
        out = (out << 1) | ((colors >> cp) & 1u);
    }
    if (after) {
        after->fp = (uint8_t)fp;
        after->cp = (uint8_t)cp;
    }
    return out;
}

/* One more bit from a state reached after at least one bit. */
static inline unsigned cl_next(const ClTopo *t, uint32_t colors, ClState *s) {
    unsigned fp = t->link[(colors >> s->cp) & 1u][s->fp];
    unsigned cp = t->link[(colors >> fp) & 1u][s->cp];
    s->fp = (uint8_t)fp;
    s->cp = (uint8_t)cp;
    return (colors >> cp) & 1u;
}

/* Ordinary verification of a colored REV: does it emit the L bits of target?
 * Stops at the first wrong bit. */
static inline int cl_verify(const ClTopo *t, uint32_t colors, uint32_t target, int L) {
    unsigned fp = 0, cp = 0;
    for (int k = 0; k < L; k++) {
        if (k) fp = t->link[(colors >> cp) & 1u][fp];
        cp = t->link[(colors >> fp) & 1u][cp];
        if (((colors >> cp) & 1u) != ((target >> (L - 1 - k)) & 1u)) return 0;
    }
    return 1;
}

/* What a run of L bits touches: the nodes fp or cp stood on, and the ports
 * followed (used[c] has bit i set if link[c][i] was followed). */
void cl_trace(const ClTopo *t, uint32_t colors, int L, uint32_t *visited, uint32_t used[2]);

/* Nodes that can be reached from the root along links, as a mask. */
uint32_t cl_reachable(const ClTopo *t);

/*
 * Position of the first bit at or after bit L in which the REVs (t, a) and
 * (t, b) differ, counted from L (0 = the very next bit), or -1 if their
 * streams are the same for ever.  Exact: the pair of machines has at most
 * n^4 joint states, so `n^4` equal bits prove equality.
 */
int cl_first_difference(const ClTopo *t, uint32_t a, uint32_t b, int L);

/* ---- numbering the nodes --------------------------------------------------- */

/* Apply a numbering: perm[old] = new, a permutation with perm[0] = 0. */
void cl_relabel(const ClTopo *in, uint32_t colors_in, const uint8_t perm[CL_MAX_NODES], ClTopo *out,
                uint32_t *colors_out);

/*
 * Canonical numbering: breadth-first from the root, port 0 before port 1; a
 * node gets the next number when it is first reached.  Two REVs that differ
 * only in the numbering of their nodes (root fixed) get the same result.
 * Returns the number of nodes numbered: t->n if every node is reachable,
 * else fewer, and then the others keep their relative order after them (a
 * fallback that is deterministic but not canonical).
 */
int cl_canonical(const ClTopo *in, uint32_t colors_in, ClTopo *out, uint32_t *colors_out,
                 uint8_t perm[CL_MAX_NODES]);

/*
 * The numbering of a LevelSolver witness: nodes in the order the machine
 * first reaches them while emitting L bits (root, then where cp goes for bit
 * 0, where fp goes for bit 1, where cp goes for bit 1, ...).  Returns the
 * number of nodes reached; perm[old] = new for those, and the others follow
 * in their old order.
 */
int cl_execution_order(const ClTopo *t, uint32_t colors, int L, uint8_t perm[CL_MAX_NODES]);

/* Exchange port 0 and port 1 at every node: the topology of the complement. */
void cl_swap_ports(const ClTopo *in, ClTopo *out);

/* A topology as a number: n in the top byte, then the 2n links in the order
 * node 0 port 0, node 0 port 1, node 1 port 0, ... as 4-bit digits. */
typedef struct {
    uint64_t hi, lo;
} ClKey;

static inline ClKey cl_key(const ClTopo *t) {
    ClKey k = {(uint64_t)t->n << 56, 0};
    for (int i = 0; i < t->n; i++) {
        for (int c = 0; c < 2; c++) {
            int d = 2 * i + c; /* digit number; 0..29 */
            uint64_t v = t->link[c][i];
            if (d < 16) k.lo |= v << (4 * d);
            else k.hi |= v << (4 * (d - 16));
        }
    }
    return k;
}

void cl_key_topo(ClKey k, ClTopo *t);

/* ---- recoloring ------------------------------------------------------------ */

typedef struct {
    uint32_t states;   /* machine steps tried: one per bit attempted */
    uint32_t branches; /* colors that had to be guessed */
    uint32_t free;     /* nodes the last solution never touched (0 for a minimum-node REV) */
} ClSearch;

/*
 * The first coloring, in the order of the search, with which the topology
 * emits the L bits of target; `first` (0 or 1) is the color tried first where
 * a color has to be guessed.  Returns 1 and the colors, or 0 if there is
 * none.  Nodes the solution never touches get color 0.
 *
 * The search runs the machine.  At each bit it needs the color of fp: if that
 * is not known yet it is guessed (first `first`, then the other).  The node
 * cp moves to must have the color the target has at this bit: that sets the
 * color if it is open, and ends the attempt if it is known and different.
 */
static inline int cl_recolor(const ClTopo *t, uint32_t target, int L, unsigned first, uint32_t *colors,
                             ClSearch *search) {
    struct {
        uint32_t known, col;
        uint8_t k, fp, cp;
    } stack[CL_MAX_NODES + 1];
    uint32_t known = 0, col = 0, states = 0, branches = 0;
    unsigned fp = 0, cp = 0, sp = 0;
    int k = 0;
    for (;;) {
        unsigned c, to, bit;
        if (!((known >> fp) & 1u)) { /* guess the color of fp; remember the other for later */
            stack[sp].known = known | (1u << fp);
            stack[sp].col = col | ((first ^ 1u) << fp);
            stack[sp].k = (uint8_t)k;
            stack[sp].fp = (uint8_t)fp;
            stack[sp].cp = (uint8_t)cp;
            sp++;
            branches++;
            known |= 1u << fp;
            col |= first << fp;
        }
        c = (col >> fp) & 1u;
        to = t->link[c][cp];
        bit = (target >> (L - 1 - k)) & 1u;
        states++;
        if ((known >> to) & 1u) {
            if (((col >> to) & 1u) != bit) { /* contradiction: back to the last guess */
                if (!sp) break;
                sp--;
                known = stack[sp].known;
                col = stack[sp].col;
                k = stack[sp].k;
                fp = stack[sp].fp;
                cp = stack[sp].cp;
                continue;
            }
        } else {
            known |= 1u << to;
            col |= bit << to;
        }
        cp = to;
        if (++k == L) {
            if (search) {
                search->states = states;
                search->branches = branches;
                search->free = (uint32_t)(t->n - __builtin_popcount(known));
            }
            *colors = col;
            return 1;
        }
        fp = t->link[(col >> cp) & 1u][fp];
    }
    if (search) {
        search->states = states;
        search->branches = branches;
        search->free = 0;
    }
    return 0;
}

/*
 * Every coloring that emits the target, in the order of the search (color 0
 * tried first): up to `max` of them are stored, all are counted.  A solution
 * that leaves f nodes untouched stands for 2^f colorings and is counted once,
 * with those nodes at color 0; search->free is the largest f seen.
 */
int cl_recolor_all(const ClTopo *t, uint32_t target, int L, uint32_t *solutions, int max, ClSearch *search);

/* The reference: every one of the 2^n colorings run for L bits.  Stores up to
 * max in increasing order, returns how many emit the target. */
int cl_brute(const ClTopo *t, uint32_t target, int L, uint32_t *solutions, int max);

/* ---- whole-topology codes ---------------------------------------------------- */

/* Links as 2n digits in base n: needs ceil(2n log2 n) bits.  n <= 10 fits
 * 128 bits here (n <= 9 fits 64). */
ClU128 cl_radix_pack(const ClTopo *t);
void cl_radix_unpack(ClU128 code, int n, ClTopo *t);

/* 64-bit version for n <= 9, with the divisions by a constant the compiler
 * turns into multiplications. */
static inline uint64_t cl_radix_pack64(const ClTopo *t) {
    uint64_t code = 0;
    for (int i = t->n - 1; i >= 0; i--) {
        code = code * t->n + t->link[1][i];
        code = code * t->n + t->link[0][i];
    }
    return code;
}

#define CL_RADIX_UNPACK(N) \
    static inline void cl_radix_unpack64_##N(uint64_t code, ClTopo *t) { \
        t->n = N; \
        for (int i = 0; i < N; i++) { \
            t->link[0][i] = (uint8_t)(code % N); \
            code /= N; \
            t->link[1][i] = (uint8_t)(code % N); \
            code /= N; \
        } \
    }
CL_RADIX_UNPACK(2)
CL_RADIX_UNPACK(3)
CL_RADIX_UNPACK(4)
CL_RADIX_UNPACK(5)
CL_RADIX_UNPACK(6)
CL_RADIX_UNPACK(7)
CL_RADIX_UNPACK(8)
CL_RADIX_UNPACK(9)
#undef CL_RADIX_UNPACK

static inline void cl_radix_unpack64(uint64_t code, int n, ClTopo *t) {
    switch (n) {
    case 1: t->n = 1; t->link[0][0] = t->link[1][0] = 0; break;
    case 2: cl_radix_unpack64_2(code, t); break;
    case 3: cl_radix_unpack64_3(code, t); break;
    case 4: cl_radix_unpack64_4(code, t); break;
    case 5: cl_radix_unpack64_5(code, t); break;
    case 6: cl_radix_unpack64_6(code, t); break;
    case 7: cl_radix_unpack64_7(code, t); break;
    case 8: cl_radix_unpack64_8(code, t); break;
    default: cl_radix_unpack64_9(code, t); break;
    }
}

/*
 * Canonical topologies of n nodes, counted and numbered.  A canonical
 * topology is a list of 2n links read in the order node 0 port 0, node 0
 * port 1, node 1 port 0, ...: each link leads to a node already numbered or
 * to the next new one, and a node's links are read only after the node has
 * been reached.  cl_canon_count(n) is how many there are (0 if the count
 * does not fit 64 bits: n > 12); the rank of one is its place among them in
 * the order of those lists, 0 .. count - 1.  log2(count) bits are what a
 * topology of n nodes takes when nothing but "all nodes reachable from the
 * root" is known about it.
 */
uint64_t cl_canon_count(int n);
uint64_t cl_canon_rank(const ClTopo *canonical);              /* of a canonical topology */
int cl_canon_unrank(int n, uint64_t rank, ClTopo *canonical); /* 0 if rank is out of range */
int cl_is_canonical(const ClTopo *t);

#ifdef __cplusplus
}
#endif

#endif
