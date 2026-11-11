#include "minimal_solver.h"

const char *rev_minimal_status_str(RevMinimalStatus s) {
    switch (s) {
    case REV_MIN_MINIMAL: return "MINIMAL";
    case REV_MIN_UNKNOWN: return "UNKNOWN";
    case REV_MIN_BUDGET_EXHAUSTED: return "BUDGET_EXHAUSTED";
    case REV_MIN_INVALID_INPUT: return "INVALID_INPUT";
    case REV_MIN_INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "?";
}

RevMinimalStatus rev_minimal_solve(const char *target, size_t target_len, int max_nodes,
                                   const RevLevelOptions *opt, RevGraph *result,
                                   RevMinimalInfo *info) {
    return rev_minimal_solve_with(rev_level_solve_ex, target, target_len, max_nodes, opt, result,
                                  info);
}

RevMinimalStatus rev_minimal_solve_with(RevLevelSolveFn level, const char *target,
                                        size_t target_len, int max_nodes,
                                        const RevLevelOptions *opt, RevGraph *result,
                                        RevMinimalInfo *info) {
    RevMinimalInfo local;
    if (!info) info = &local;
    info->node_count = 0;
    info->lower_bound = 1;
    info->last_budget = 0;
    info->limit = REV_LIMIT_NONE;
    info->steps = 0;
    info->peak_stack = 0;

    if (max_nodes == 0) max_nodes = REV_LEVEL_MAX_NODES;
    if (max_nodes < 1 || max_nodes > REV_LEVEL_MAX_NODES) {
        if (result) result->node_count = 0;
        return REV_MIN_INVALID_INPUT;
    }

    for (int n = 1; n <= max_nodes; n++) {
        RevLevelStats st;
        RevSolveResult r = level(target, target_len, n, opt, result, &st);
        info->last_budget = n;
        info->steps += st.steps;
        if (st.peak_stack > info->peak_stack) info->peak_stack = st.peak_stack;

        switch (r) {
        case REV_SOLVE_SOLVED:
            info->node_count = result->node_count;
            return REV_MIN_MINIMAL;
        case REV_SOLVE_PROVEN_UNSAT:
            info->lower_bound = n + 1;
            break;
        case REV_SOLVE_RESOURCE_LIMIT:
            info->limit = st.limit;
            return REV_MIN_UNKNOWN;
        case REV_SOLVE_INVALID_INPUT:
            return REV_MIN_INVALID_INPUT;
        default:
            return REV_MIN_INTERNAL_ERROR;
        }
    }
    return REV_MIN_BUDGET_EXHAUSTED;
}
