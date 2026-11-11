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
 * cb_amd64_kernel_only: run just the assembly kernel on an already
 * validated target, with no export or verification.  Returns the kernel's
 * result code; *steps receives the step count.
 */
RevSolveResult rev_level_solve_amd64_cap(const char *target, size_t target_len, int max_nodes,
                                         const RevLevelOptions *opt, int stack_frames,
                                         RevGraph *result, RevLevelStats *stats);
int cb_amd64_guards_intact(void);
int cb_amd64_kernel_only(const char *target, size_t target_len, int max_nodes, uint64_t *steps);

/* The assembly kernel.  Returns CB_RES_*. */
int cb_amd64_search(CbAsmState *state, int max_nodes);

#ifdef __cplusplus
}
#endif

#endif
