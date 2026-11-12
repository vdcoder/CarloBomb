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
_Static_assert(offsetof(CbAsmState, scratch) + 8 == CB_ST_COUNTERS, "layout");
_Static_assert(offsetof(CbAsmCounters, bulk_steps) == CB_CNT_BULK_STEPS, "counters");
_Static_assert(offsetof(CbAsmCounters, bulk_scans) == CB_CNT_BULK_SCANS, "counters");
_Static_assert(offsetof(CbAsmCounters, bulk_loops) == CB_CNT_BULK_LOOPS, "counters");
_Static_assert(offsetof(CbAsmCounters, fallback_steps) == CB_CNT_FALLBACK_STEPS, "counters");
_Static_assert(offsetof(CbAsmCounters, fallback_stack) == CB_CNT_FALLBACK_STACK, "counters");
_Static_assert(offsetof(CbAsmCounters, fallback_uncolored) == CB_CNT_FALLBACK_UNCOLORED,
               "counters");

#define GUARD_BYTE 0xA5

static CbAsmState state;
static CbAsmCounters counters;
static int guards_ready;

/* ---- kernel registry ---------------------------------------------------- */

int cb_amd64_k_b4(CbAsmState *, int);
int cb_amd64_k_hyb(CbAsmState *, int);
int cb_amd64_k_n8g(CbAsmState *, int);
int cb_amd64_k_n8g_bmi2(CbAsmState *, int);
int cb_amd64_k_p16m(CbAsmState *, int);
int cb_amd64_k_p16g(CbAsmState *, int);
int cb_amd64_k_p16x(CbAsmState *, int);
int cb_amd64_k_hyb_f8(CbAsmState *, int);
int cb_amd64_k_n8g_f8(CbAsmState *, int);
int cb_amd64_k_n8s(CbAsmState *, int);
int cb_amd64_k_n8s_bmi2(CbAsmState *, int);
int cb_amd64_k_n8x(CbAsmState *, int);
int cb_amd64_k_hybx(CbAsmState *, int);
int cb_amd64_k_n8x_count(CbAsmState *, int);
int cb_amd64_k_hybx_count(CbAsmState *, int);

enum {
    K_ASM, K_B4, K_HYB, K_N8G, K_N8G_BMI2, K_P16M, K_P16G, K_P16X, K_HYB_F8, K_N8G_F8,
    K_N8S, K_N8S_BMI2, K_N8X, K_HYBX, K_AUTO, K_N8X_COUNT, K_HYBX_COUNT
};

static const CbAmd64Kernel kernels[] = {
    [K_ASM] = {"asm", cb_amd64_search, 64, -1, 0, "reference kernel: byte nodes in memory"},
    [K_B4] = {"b4", cb_amd64_k_b4, 64, -1, 0,
              "control: reference representation on the shared body"},
    [K_HYB] = {"hyb", cb_amd64_k_hyb, 64, -1, 0,
               "colors in register bitmaps, links as bytes in memory"},
    [K_N8G] = {"n8g", cb_amd64_k_n8g, 8, K_HYB, 0,
               "whole graph in registers for budgets up to 8, baseline instructions"},
    [K_N8G_BMI2] = {"n8g-bmi2", cb_amd64_k_n8g_bmi2, 8, K_HYB, CB_ISA_BMI2,
                    "whole graph in registers for budgets up to 8, BMI2 shifts"},
    [K_P16M] = {"p16m", cb_amd64_k_p16m, 64, -1, 0, "16-bit packed nodes in memory"},
    [K_P16G] = {"p16g", cb_amd64_k_p16g, 4, K_ASM, CB_ISA_BMI2,
                "16-bit packed nodes in one GPR for budgets up to 4"},
    [K_P16X] = {"p16x", cb_amd64_k_p16x, 8, K_ASM, CB_ISA_BMI2 | CB_ISA_AVX512,
                "16-bit packed nodes in an XMM register for budgets up to 8"},
    [K_HYB_F8] = {"hyb-f8", cb_amd64_k_hyb_f8, 64, -1, 0, "hyb with 8-byte search frames"},
    [K_N8G_F8] = {"n8g-f8", cb_amd64_k_n8g_f8, 8, K_HYB_F8, 0, "n8g with 8-byte search frames"},
    [K_N8S] = {"n8s", cb_amd64_k_n8s, 8, K_HYB, 0,
               "whole graph in registers, links split by link number, baseline instructions"},
    [K_N8S_BMI2] = {"n8s-bmi2", cb_amd64_k_n8s_bmi2, 8, K_HYB, CB_ISA_BMI2,
                    "whole graph in registers, links split by link number, BMI2"},
    [K_N8X] = {"n8x", cb_amd64_k_n8x, 8, K_HYBX, CB_ISA_BMI2,
               "n8s-bmi2 with wrong-color CTRL link attempts accounted for in bulk"},
    [K_HYBX] = {"hybx", cb_amd64_k_hybx, 64, -1, CB_ISA_BMI2,
                "hyb with wrong-color CTRL link attempts accounted for in bulk (BMI2)"},
    /* Not a kernel of its own: picks one per search from the node budget and
     * the CPU (see pick_auto). */
    [K_AUTO] = {"auto", NULL, 64, -1, 0, "best measured kernel for the node budget and CPU"},
    /* Benchmark only: n8x and hybx with counters at the bulk-accounting sites. */
    [K_N8X_COUNT] = {"n8x-count", cb_amd64_k_n8x_count, 8, K_HYBX_COUNT, CB_ISA_BMI2,
                     "benchmark only: n8x with search-accounting counters"},
    [K_HYBX_COUNT] = {"hybx-count", cb_amd64_k_hybx_count, 64, -1, CB_ISA_BMI2,
                      "benchmark only: hybx with search-accounting counters"},
};
#define N_KERNELS ((int)(sizeof kernels / sizeof kernels[0]))

static int selected;
static unsigned isa_allowed = ~0u;

static unsigned cpu_isa(void) {
    static int known;
    static unsigned isa;
    if (!known) {
        __builtin_cpu_init();
        if (__builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2")) isa |= CB_ISA_BMI2;
        if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
            __builtin_cpu_supports("avx512vl"))
            isa |= CB_ISA_AVX512;
        known = 1;
    }
    return isa & isa_allowed;
}

unsigned cb_amd64_isa(void) { return cpu_isa(); }

void cb_amd64_isa_limit(unsigned allowed) {
    isa_allowed = allowed;
    if (!cb_amd64_kernel_available(selected)) selected = K_ASM;
}

const CbAsmCounters *cb_amd64_counters(void) { return &counters; }

void cb_amd64_counters_reset(void) { memset(&counters, 0, sizeof counters); }

int cb_amd64_kernel_count(void) { return N_KERNELS; }

const CbAmd64Kernel *cb_amd64_kernel_info(int i) {
    return i >= 0 && i < N_KERNELS ? &kernels[i] : NULL;
}

int cb_amd64_kernel_available(int i) {
    for (; i >= 0 && i < N_KERNELS; i = kernels[i].fallback) {
        if ((kernels[i].isa & cpu_isa()) != kernels[i].isa) return 0;
        if (kernels[i].max_nodes >= REV_LEVEL_MAX_NODES) return 1;
    }
    return 0; /* out of range, or a chain that does not cover every budget */
}

int cb_amd64_kernel_find(const char *name) {
    for (int i = 0; name && i < N_KERNELS; i++)
        if (strcmp(kernels[i].name, name) == 0) return i;
    return -1;
}

int cb_amd64_kernel_select(int i) {
    if (!cb_amd64_kernel_available(i)) return -1;
    selected = i;
    return 0;
}

int cb_amd64_kernel_selected(void) { return selected; }

/* The measured choice (solvers/level/experimental/amd64/repr/README.md).
 * With BMI2: the all-register kernel up to 8 nodes, the bitmap-plus-memory
 * kernel above, both with bulk accounting.  Without BMI2: the baseline
 * builds, the all-register one only where it measured ahead (4 to 8). */
static int pick_auto(int max_nodes) {
    if (cpu_isa() & CB_ISA_BMI2) return max_nodes <= 8 ? K_N8X : K_HYBX;
    return max_nodes >= 4 && max_nodes <= 8 ? K_N8S : K_HYB;
}

int cb_amd64_kernel_used(int max_nodes) {
    int i = selected == K_AUTO ? pick_auto(max_nodes) : selected;
    while (max_nodes > kernels[i].max_nodes) i = kernels[i].fallback;
    return i;
}

CbAmd64KernelFn cb_amd64_kernel_resolve(int max_nodes) {
    return kernels[cb_amd64_kernel_used(max_nodes)].fn;
}

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
    state.scratch[1] = (uint64_t)(uintptr_t)&counters;

    int r = cb_amd64_kernel_resolve(max_nodes)(&state, max_nodes);

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

int cb_amd64_kernel_only(const char *tgt, size_t tgt_len, int max_nodes, uint64_t max_steps,
                         uint64_t *steps) {
    if (!guards_ready) guards_init();
    state.target = tgt;
    state.target_len = tgt_len;
    state.step_limit = max_steps ? max_steps : UINT64_MAX;
    state.max_stack = REV_LEVEL_MAX_STACK;
    state.scratch[1] = (uint64_t)(uintptr_t)&counters;
    int r = cb_amd64_kernel_resolve(max_nodes)(&state, max_nodes);
    if (steps) *steps = state.steps;
    return r;
}
