/*
 * SharedSolver: the LevelSolver's search for many strings at once.
 *
 * The LevelSolver decides, for one string S and a node budget N, whether a
 * REV of at most N nodes emits S, by a depth-first search over partly
 * decided graphs.  Two strings that begin alike are searched alike for as
 * long as they agree.  This solver walks the common part once: it has no
 * single target.  A branch lives as long as some string that is still
 * wanted begins with what the branch has emitted, and where the LevelSolver
 * gives an uncolored node the next bit of its target, both bits are tried.
 *
 * What it reports is, for every wanted string, exactly what the LevelSolver
 * reports for that string alone at that budget:
 *
 *   - whether a REV exists, and if so the REV the LevelSolver returns: the
 *     first in its order;
 *   - the number of logical steps the LevelSolver takes for it.
 *
 * The second is counted, not estimated.  The steps this search makes while
 * the bits emitted are a beginning of S (the three steps of each choice of
 * bit left out) are the steps of the LevelSolver's search for S, one for
 * one: until S is found if it is found, to the end of the pass if not.
 * tests/test_shared_solver.c holds this solver to that against the
 * LevelSolver, string by string.
 *
 * The strings of one call have a common beginning: a class is the set of
 * strings of `bits` bits whose first `class_bits` bits are given.  A pass
 * searches the wanted strings of one class at one node budget.
 *
 * No static state: every RevShared is independent of every other.  This
 * file and its implementation use the REV core and nothing of the
 * LevelSolver.
 */
#ifndef CARLOBOMB_SHARED_SOLVER_H
#define CARLOBOMB_SHARED_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "rev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REV_SHARED_MAX_BITS 31
#define REV_SHARED_MAX_NODES 16
/* A class holds at most 2^REV_SHARED_MAX_FREE strings: bits - class_bits may not exceed it. */
#define REV_SHARED_MAX_FREE 26

typedef struct RevShared RevShared;

typedef enum {
    REV_SHARED_DONE = 0, /* the pass ran to its end: every wanted string was found or has no REV */
    REV_SHARED_STOPPED,  /* a callback asked to stop: nothing may be concluded for strings not found */
    REV_SHARED_INVALID,  /* an argument is impossible */
    REV_SHARED_INTERNAL  /* a check the solver makes on itself failed; a bug, never expected */
} RevSharedResult;

/*
 * Called when a wanted string is found, at that moment.  string: its bits as
 * a number, first bit most significant.  rev: the REV, complete (what the
 * search never decided is 0, as the LevelSolver writes it) and already run
 * on the interpreter of the REV core.  steps: the logical steps of the
 * LevelSolver's search for this string at this budget.  A nonzero return
 * stops the pass.
 */
typedef int (*RevSharedFound)(void *ctx, uint32_t string, const RevGraph *rev, uint64_t steps);

/* Called every poll_every steps; a nonzero return stops the pass. */
typedef int (*RevSharedPoll)(void *ctx);

typedef struct {
    RevSharedFound found;
    void *found_ctx;
    RevSharedPoll poll;
    void *poll_ctx;
    uint64_t poll_every; /* 0: every 2^20 steps */
} RevSharedHooks;

typedef struct {
    uint64_t wanted;       /* strings wanted when the pass began */
    uint64_t found;        /* of them, found */
    uint64_t steps;        /* steps of this search */
    uint64_t choices;      /* nodes that were given each bit in turn */
    uint64_t string_steps; /* the steps of the LevelSolver's searches for the wanted strings, added up */
    int peak_stack;        /* deepest stack use, in frames */
} RevSharedStats;

/*
 * A solver for classes of strings of `bits` bits (1..REV_SHARED_MAX_BITS)
 * with a common beginning of class_bits bits (0..bits, and at least
 * bits - REV_SHARED_MAX_FREE).  Memory: 9 bytes for each string of a class.
 * NULL if the arguments are impossible or memory is short.
 */
RevShared *rev_shared_create(int bits, int class_bits);
void rev_shared_destroy(RevShared *s);

/* Strings in a class: 2^(bits - class_bits). */
uint32_t rev_shared_class_size(const RevShared *s);

/*
 * One pass.  klass: the common beginning, class_bits bits as a number.
 * max_nodes: the node budget, 1..REV_SHARED_MAX_NODES.  wanted: a bitmap of
 * rev_shared_class_size bits; bit j (byte j / 8, bit j % 8) stands for the
 * string whose bits after the common beginning are j.  hooks and stats may
 * be NULL.
 *
 * On REV_SHARED_DONE every wanted string either was reported to `found`, or
 * has no REV of at most max_nodes nodes; rev_shared_unfound and
 * rev_shared_unfound_steps then say which, and how many steps the
 * LevelSolver's search for it takes.
 */
RevSharedResult rev_shared_pass(RevShared *s, uint32_t klass, int max_nodes, const uint8_t *wanted,
                                const RevSharedHooks *hooks, RevSharedStats *stats);

/* After a pass that ended with REV_SHARED_DONE: 1 if string j of the class
 * was wanted and has no REV of at most max_nodes nodes. */
int rev_shared_unfound(const RevShared *s, uint32_t j);
/* The logical steps of the LevelSolver's search for such a string. */
uint64_t rev_shared_unfound_steps(const RevShared *s, uint32_t j);

const char *rev_shared_result_str(RevSharedResult r);

#ifdef __cplusplus
}
#endif

#endif
