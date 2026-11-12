/*
 * MinimalSolver: find a REV with the fewest nodes that emits a given binary
 * string, by running LevelSolver with budgets 1, 2, 3, ...
 *
 * A result is called minimal only when every smaller budget was exhaustively
 * excluded in this same call.
 */
#ifndef CARLOBOMB_MINIMAL_SOLVER_H
#define CARLOBOMB_MINIMAL_SOLVER_H

#include "level_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REV_MIN_MINIMAL = 0,      /* *result is a certified minimum-node REV */
    REV_MIN_UNKNOWN,          /* a budget hit a resource limit; see lower_bound */
    REV_MIN_BUDGET_EXHAUSTED, /* every budget 1..max_nodes proven UNSAT, so any
                                 matching REV has more than max_nodes nodes */
    REV_MIN_INVALID_INPUT,
    REV_MIN_INTERNAL_ERROR
} RevMinimalStatus;

typedef struct {
    int node_count;      /* nodes in the witness (REV_MIN_MINIMAL only) */
    int lower_bound;     /* every budget below this was proven UNSAT, so the
                            minimum node count is >= lower_bound.  Valid for
                            every status except INVALID_INPUT. */
    int last_budget;     /* last budget handed to LevelSolver */
    RevLevelLimit limit; /* reason, when status is REV_MIN_UNKNOWN */
    uint64_t steps;      /* LevelSolver iterations summed over all budgets */
    int peak_stack;      /* deepest LevelSolver stack use over all budgets */
} RevMinimalInfo;

const char *rev_minimal_status_str(RevMinimalStatus s);

/*
 * max_nodes: largest budget to try, 1..REV_LEVEL_MAX_NODES; 0 means
 *            REV_LEVEL_MAX_NODES.
 * opt:       passed to every LevelSolver call (max_steps is per budget); may
 *            be NULL.
 * info:      may be NULL.
 */
RevMinimalStatus rev_minimal_solve(const char *target, size_t target_len, int max_nodes,
                                   const RevLevelOptions *opt, RevGraph *result,
                                   RevMinimalInfo *info);

/*
 * Same, with the LevelSolver implementation supplied by the caller.  `level`
 * must honor the contract of rev_level_solve_ex.  rev_minimal_solve uses the
 * C baseline, rev_level_solve_ex itself.
 */
typedef RevSolveResult (*RevLevelSolveFn)(const char *target, size_t target_len, int max_nodes,
                                          const RevLevelOptions *opt, RevGraph *result,
                                          RevLevelStats *stats);

RevMinimalStatus rev_minimal_solve_with(RevLevelSolveFn level, const char *target,
                                        size_t target_len, int max_nodes,
                                        const RevLevelOptions *opt, RevGraph *result,
                                        RevMinimalInfo *info);

#ifdef __cplusplus
}
#endif

#endif
