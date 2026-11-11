/*
 * LevelSolver, C baseline: given a binary string S and a node budget N, find
 * a REV with at most N nodes whose first strlen(S) output bits equal S, or
 * prove that none exists.
 *
 * This is a parameterized adaptation of legacy_original.c in this directory.
 * The search algorithm and move order are unchanged.
 *
 * Not reentrant and not thread-safe: like the original, the search state
 * lives in static storage.
 */
#ifndef CARLOBOMB_LEVEL_SOLVER_H
#define CARLOBOMB_LEVEL_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "rev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest supported node budget. */
#define REV_LEVEL_MAX_NODES REV_MAX_NODES

/*
 * Capacity of the static search stack, in frames (the original used 10000).
 * The search uses at most 6 frames per target bit, so targets of up to
 * REV_LEVEL_MAX_STACK / 6 bits can never exhaust it.  Longer targets are
 * accepted and report REV_SOLVE_RESOURCE_LIMIT if the stack actually fills.
 */
#ifndef REV_LEVEL_MAX_STACK
#define REV_LEVEL_MAX_STACK 10000
#endif

typedef enum {
    REV_SOLVE_SOLVED = 0,     /* *result is a valid, fully specified, verified REV */
    REV_SOLVE_PROVEN_UNSAT,   /* exhaustive: no REV with <= max_nodes nodes matches */
    REV_SOLVE_RESOURCE_LIMIT, /* search abandoned; says nothing about existence */
    REV_SOLVE_INVALID_INPUT,
    REV_SOLVE_INTERNAL_ERROR  /* solver produced a graph the independent verifier
                                 rejected; a bug, never expected */
} RevSolveResult;

typedef enum {
    REV_LIMIT_NONE = 0,
    REV_LIMIT_STACK, /* static search stack full */
    REV_LIMIT_STEPS  /* caller's step limit reached */
} RevLevelLimit;

typedef struct {
    uint64_t max_steps; /* search-loop iterations allowed; 0 = unlimited */
} RevLevelOptions;

typedef struct {
    uint64_t steps;      /* search-loop iterations performed */
    int peak_stack;      /* deepest stack use, in frames */
    RevLevelLimit limit; /* why REV_SOLVE_RESOURCE_LIMIT was returned */
} RevLevelStats;

const char *rev_solve_result_str(RevSolveResult r);
const char *rev_level_limit_str(RevLevelLimit l);

/*
 * target: target_len characters, each '0' or '1' (need not be NUL-terminated).
 * max_nodes: 1..REV_LEVEL_MAX_NODES.
 * result: receives the REV on REV_SOLVE_SOLVED; node_count is 0 otherwise.
 */
RevSolveResult rev_level_solve(const char *target, size_t target_len, int max_nodes,
                               RevGraph *result);

/* Same, with optional limits (opt) and optional effort counters (stats). */
RevSolveResult rev_level_solve_ex(const char *target, size_t target_len, int max_nodes,
                                  const RevLevelOptions *opt, RevGraph *result,
                                  RevLevelStats *stats);

#ifdef __cplusplus
}
#endif

#endif
