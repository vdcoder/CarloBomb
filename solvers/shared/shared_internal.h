/*
 * What the walks of the SharedSolver have in common, and nothing the rest of
 * the library may use: the solver's storage, the beginning of a pass (what
 * is wanted, with every beginning of it), the end of a pass (the checks
 * everything concluded from a pass rests on), and the two things done when
 * a string is found.  The walks themselves share nothing:
 *
 *   shared_solver.c  the reference walk, one step of the search at a time;
 *   shared_run.c     the same walk, with the stretches where nothing is to
 *                    be decided run without frames.
 */
#ifndef CARLOBOMB_SHARED_INTERNAL_H
#define CARLOBOMB_SHARED_INTERNAL_H

#include "shared_solver.h"

#define REVSH_UND 255 /* a color or a link that is not decided */

enum { REVSH_STACK_FRAMES = 6 * REV_SHARED_MAX_BITS + 16 };

typedef struct { uint8_t color, link[2]; } RevSharedNode;
typedef struct { uint8_t fn, st, fp, cp, it, dir, nxt; } RevSharedFrame;

/*
 * The beginnings of the strings of a class form a binary tree.  Its nodes
 * are numbered as a heap: the class's own beginning is 1, the beginnings one
 * bit longer are 2 and 3, and string j of the class is size + j.  `tree`
 * has a bit for each: set while a wanted string begins with it.  `count`
 * has a number for each beginning that is not a whole string: the steps made
 * while exactly that had been emitted.  The beginnings shorter than the
 * class's own lie on one path and are counted in count_top.
 */
struct RevShared {
    int bits, class_bits, free_bits;
    uint32_t size;    /* strings in a class */
    uint8_t *tree;    /* a bit for heap numbers 1 .. 2 * size - 1 */
    uint8_t *asked;   /* a bit for each string of the class: wanted when the pass began */
    uint64_t *count;  /* [1 .. size - 1] */
    uint64_t count_top[REV_SHARED_MAX_BITS + 1];
    int done;         /* the last pass ran to its end */
    /* The reference walk's graph and stack. */
    RevSharedNode G[REV_SHARED_MAX_NODES];
    RevSharedFrame S[REVSH_STACK_FRAMES];
};

static inline int revsh_bit(const uint8_t *map, uint32_t i) { return (map[i >> 3] >> (i & 7)) & 1; }
static inline void revsh_bit_clear(uint8_t *map, uint32_t i) { map[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static inline void revsh_bit_set(uint8_t *map, uint32_t i) { map[i >> 3] |= (uint8_t)(1u << (i & 7)); }

int revsh_begin(RevShared *s, uint32_t klass, int max_nodes, const uint8_t *wanted, RevSharedStats *stats);
RevSharedResult revsh_end(RevShared *s, uint64_t steps, uint64_t choices, int peak, RevSharedStats *stats);

/* The steps counted so far at the beginnings of string j of the class. */
uint64_t revsh_steps_of(const RevShared *s, uint32_t j);

/* The graph as it stands, for a string all of whose bits it has emitted.
 * 1 if the interpreter of the REV core gets that string out of it. */
int revsh_export_checked(const RevSharedNode *G, int nodes, int bits, uint32_t string, RevGraph *g);

#endif
