/*
 * C wrapper for the AMD64 assembly search kernel.  See asm_solver.h.
 */
#include "asm_solver.h"

#include <stddef.h>
#include <string.h>

/* The numbers the assembler uses must describe this structure. */
_Static_assert(offsetof(CbAsmState, target) == CB_ST_TARGET, "layout");
_Static_assert(offsetof(CbAsmState, target_len) == CB_ST_TARGET_LEN, "layout");
_Static_assert(offsetof(CbAsmState, step_limit) == CB_ST_STEP_LIMIT, "layout");
_Static_assert(offsetof(CbAsmState, steps) == CB_ST_STEPS, "layout");
_Static_assert(offsetof(CbAsmState, peak_stack) == CB_ST_PEAK_STACK, "layout");
_Static_assert(offsetof(CbAsmState, limit_hit) == CB_ST_LIMIT_HIT, "layout");
_Static_assert(offsetof(CbAsmState, node_count) == CB_ST_NODE_COUNT, "layout");
_Static_assert(offsetof(CbAsmState, max_stack) == CB_ST_MAX_STACK, "layout");
_Static_assert(offsetof(CbAsmState, scratch) == CB_ST_SCRATCH, "layout");
_Static_assert(offsetof(CbAsmState, nodes) == CB_ST_NODES, "layout");
_Static_assert(offsetof(CbAsmState, frames) == CB_ST_FRAMES, "layout");
_Static_assert(sizeof(((CbAsmState *)0)->nodes[0]) == CB_ASM_NODE_SIZE, "layout");
_Static_assert(CB_ASM_MAX_NODES == REV_LEVEL_MAX_NODES, "layout");
_Static_assert(CB_ASM_MAX_NODES <= 127, "node indices must fit a signed byte");
_Static_assert((int)REV_SOLVE_SOLVED == CB_RES_SOLVED, "result codes");
_Static_assert((int)REV_SOLVE_PROVEN_UNSAT == CB_RES_UNSAT, "result codes");
_Static_assert((int)REV_SOLVE_RESOURCE_LIMIT == CB_RES_LIMIT, "result codes");
_Static_assert((int)REV_LIMIT_NONE == CB_LIM_NONE, "limit codes");
_Static_assert((int)REV_LIMIT_STACK == CB_LIM_STACK, "limit codes");
_Static_assert((int)REV_LIMIT_STEPS == CB_LIM_STEPS, "limit codes");

#define GUARD_BYTE 0xA5

static CbAsmState state;
static int guards_ready;

static void guards_init(void) {
    memset(state.guard0, GUARD_BYTE, CB_ASM_GUARD);
    memset(state.guard1, GUARD_BYTE, CB_ASM_GUARD);
    memset(state.guard2, GUARD_BYTE, CB_ASM_GUARD);
    guards_ready = 1;
}

static int guard_ok(const uint8_t *g) {
    for (int i = 0; i < CB_ASM_GUARD; i++)
        if (g[i] != GUARD_BYTE) return 0;
    return 1;
}

int cb_amd64_guards_intact(void) {
    if (!guards_ready) guards_init();
    return guard_ok(state.guard0) && guard_ok(state.guard1) && guard_ok(state.guard2);
}

/* Same completion rule as the C solver: undecided links -> node 0,
 * undecided colors -> WHITE. */
static void export_graph(RevGraph *out) {
    int n = (int)state.node_count;
    out->node_count = n;
    for (int i = 0; i < n; i++) {
        int c = state.nodes[i][0], l0 = state.nodes[i][1], l1 = state.nodes[i][2];
        out->nodes[i].color = c < 0 ? REV_WHITE : c;
        out->nodes[i].links[0] = l0 < 0 ? REV_ROOT : l0;
        out->nodes[i].links[1] = l1 < 0 ? REV_ROOT : l1;
    }
}

RevSolveResult rev_level_solve_amd64_cap(const char *tgt, size_t tgt_len, int max_nodes,
                                         const RevLevelOptions *opt, int stack_frames,
                                         RevGraph *result, RevLevelStats *stats) {
    if (stats) { stats->steps = 0; stats->peak_stack = 0; stats->limit = REV_LIMIT_NONE; }
    if (result) result->node_count = 0;
    if (!result || rev_target_validate(tgt, tgt_len) != REV_OK) return REV_SOLVE_INVALID_INPUT;
    if (max_nodes < 1 || max_nodes > REV_LEVEL_MAX_NODES) return REV_SOLVE_INVALID_INPUT;
    if (stack_frames < 1 || stack_frames > REV_LEVEL_MAX_STACK) return REV_SOLVE_INVALID_INPUT;
    if (!guards_ready) guards_init();

    state.target = tgt;
    state.target_len = tgt_len;
    state.step_limit = (opt && opt->max_steps) ? opt->max_steps : UINT64_MAX;
    state.max_stack = (uint32_t)stack_frames;

    int r = cb_amd64_search(&state, max_nodes);

    if (!cb_amd64_guards_intact() || r < CB_RES_SOLVED || r > CB_RES_LIMIT)
        return REV_SOLVE_INTERNAL_ERROR;
    if (stats) {
        stats->steps = state.steps;
        stats->peak_stack = (int)state.peak_stack;
        stats->limit = (RevLevelLimit)state.limit_hit;
    }
    if (r != CB_RES_SOLVED) return (RevSolveResult)r;

    export_graph(result);
    if (rev_verify_prefix(result, tgt, tgt_len, NULL) != REV_OK) {
        result->node_count = 0;
        return REV_SOLVE_INTERNAL_ERROR;
    }
    return REV_SOLVE_SOLVED;
}

RevSolveResult rev_level_solve_amd64_ex(const char *tgt, size_t tgt_len, int max_nodes,
                                        const RevLevelOptions *opt, RevGraph *result,
                                        RevLevelStats *stats) {
    return rev_level_solve_amd64_cap(tgt, tgt_len, max_nodes, opt, REV_LEVEL_MAX_STACK, result,
                                     stats);
}

RevSolveResult rev_level_solve_amd64(const char *tgt, size_t tgt_len, int max_nodes,
                                     RevGraph *result) {
    return rev_level_solve_amd64_ex(tgt, tgt_len, max_nodes, NULL, result, NULL);
}

int cb_amd64_kernel_only(const char *tgt, size_t tgt_len, int max_nodes, uint64_t *steps) {
    if (!guards_ready) guards_init();
    state.target = tgt;
    state.target_len = tgt_len;
    state.step_limit = UINT64_MAX;
    state.max_stack = REV_LEVEL_MAX_STACK;
    int r = cb_amd64_search(&state, max_nodes);
    if (steps) *steps = state.steps;
    return r;
}
