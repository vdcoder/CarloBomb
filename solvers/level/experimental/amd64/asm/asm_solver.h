/*
 * LevelSolver, AMD64 assembly challenger (Linux / System V ABI).
 *
 * Same contract as rev_level_solve_ex in solvers/level/fastest/c: same
 * inputs, same result codes, same first solution, same step and peak-stack
 * counts.  Only the search loop is assembly (search_sysv.S); validation,
 * graph export, completion and independent verification are C.
 *
 * Experimental.  The C solver remains the default engine.
 * Not reentrant and not thread-safe: search state is static.
 */
#ifndef CARLOBOMB_ASM_SOLVER_H
#define CARLOBOMB_ASM_SOLVER_H

#include "asm_layout.h"
#include "level_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

RevSolveResult rev_level_solve_amd64(const char *target, size_t target_len, int max_nodes,
                                     RevGraph *result);

RevSolveResult rev_level_solve_amd64_ex(const char *target, size_t target_len, int max_nodes,
                                        const RevLevelOptions *opt, RevGraph *result,
                                        RevLevelStats *stats);

/*
 * Kernel registry.
 *
 * Kernel 0, "asm", is the reference kernel in search_sysv.S and the default.
 * The others are the graph-representation experiments in ../repr.  All of
 * them implement the same search; the solve functions above use whichever
 * kernel is selected.  A kernel that only handles small node budgets names a
 * fallback for larger ones, so a selection is valid for every budget.
 */
#define CB_ISA_BMI2 1u   /* BMI1 and BMI2 */
#define CB_ISA_AVX512 2u /* AVX-512 F, BW and VL */

typedef int (*CbAmd64KernelFn)(CbAsmState *state, int max_nodes);

typedef struct {
    const char *name;   /* the CLI engine is "amd64-" followed by this */
    CbAmd64KernelFn fn;
    int max_nodes;      /* largest node budget the kernel handles itself */
    int fallback;       /* registry index used above max_nodes, or -1 */
    unsigned isa;       /* CB_ISA_* bits needed beyond baseline x86-64 */
    const char *what;   /* one line */
} CbAmd64Kernel;

int cb_amd64_kernel_count(void);
const CbAmd64Kernel *cb_amd64_kernel_info(int index); /* NULL if out of range */
int cb_amd64_kernel_available(int index);             /* 1 if this CPU can run it */
int cb_amd64_kernel_find(const char *name);           /* index, or -1 */
int cb_amd64_kernel_select(int index);                /* 0 on success, -1 if unavailable */
int cb_amd64_kernel_selected(void);
/* The kernel function the current selection uses for this node budget. */
CbAmd64KernelFn cb_amd64_kernel_resolve(int max_nodes);

/*
 * Test and benchmark interface.
 *
 * rev_level_solve_amd64_cap: as rev_level_solve_amd64_ex with an explicit
 * search-stack capacity in frames (1..REV_LEVEL_MAX_STACK), so that stack
 * boundaries can be tested at any depth.
 *
 * cb_amd64_guards_intact: 1 if the guard bytes around the kernel's node and
 * frame arrays still hold their pattern.  Checked after every search by the
 * solve functions, which return REV_SOLVE_INTERNAL_ERROR if it fails.
 *
 * cb_amd64_kernel_only: run just the selected assembly kernel on an already
 * validated target, with no export or verification.  max_steps 0 means no
 * limit.  Returns the kernel's result code; *steps receives the step count.
 */
RevSolveResult rev_level_solve_amd64_cap(const char *target, size_t target_len, int max_nodes,
                                         const RevLevelOptions *opt, int stack_frames,
                                         RevGraph *result, RevLevelStats *stats);
int cb_amd64_guards_intact(void);
int cb_amd64_kernel_only(const char *target, size_t target_len, int max_nodes,
                         uint64_t max_steps, uint64_t *steps);

/* The reference assembly kernel.  Returns CB_RES_*. */
int cb_amd64_search(CbAsmState *state, int max_nodes);

#ifdef __cplusplus
}
#endif

#endif
