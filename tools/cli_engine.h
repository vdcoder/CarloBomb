/*
 * Engine names of the command-line tool: "c", or "amd64-KERNEL" in builds
 * that have the AMD64 assembly solver.  Shared by carlobomb.c and
 * fixed_run_cli.c.
 */
#ifndef CARLOBOMB_CLI_ENGINE_H
#define CARLOBOMB_CLI_ENGINE_H

#include <string.h>

#include "minimal_solver.h"
#ifdef CARLOBOMB_HAVE_AMD64_ASM
#include "asm_solver.h"
#endif

enum {
    CLI_ENGINE_OK = 0,
    CLI_ENGINE_UNKNOWN,   /* no engine of that name */
    CLI_ENGINE_NOT_BUILT, /* an amd64 engine in a build without the assembly solver */
    CLI_ENGINE_NO_ISA     /* the kernel needs an instruction set this CPU lacks */
};

/*
 * Make `name` the engine and return its solve function in *fn.  The amd64
 * engines share one solve function and a process-wide kernel selection, so
 * a successful call for one of them changes that selection.
 */
static int cli_engine_select(const char *name, RevLevelSolveFn *fn) {
    if (!strcmp(name, "c")) {
        *fn = rev_level_solve_ex;
        return CLI_ENGINE_OK;
    }
#ifdef CARLOBOMB_HAVE_AMD64_ASM
    if (!strncmp(name, "amd64-", 6)) {
        int k = cb_amd64_kernel_find(name + 6);
        if (k < 0) return CLI_ENGINE_UNKNOWN;
        if (cb_amd64_kernel_select(k) != 0) return CLI_ENGINE_NO_ISA;
        *fn = rev_level_solve_amd64_ex;
        return CLI_ENGINE_OK;
    }
#else
    if (!strncmp(name, "amd64-", 6)) return CLI_ENGINE_NOT_BUILT;
#endif
    return CLI_ENGINE_UNKNOWN;
}

#endif
