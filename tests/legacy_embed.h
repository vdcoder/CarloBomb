/*
 * Embeds the unmodified legacy_original.c into the including translation
 * unit so its search kernel can be called directly.
 *
 * The source file itself is not changed.  Two macros are in effect while it
 * is included: its main() is renamed, and printf is discarded so that
 * print_sol() produces no output.  Afterwards the including file can
 *   - point `target_str` / `target_len` at another target (they are ordinary
 *     static variables in the original), and
 *   - call solve(max) and read the result from G[] and Gn.
 *
 * Limits inherited from the original: max <= 10 nodes, a 10000-frame stack
 * with no overflow check.  Callers must stay inside them.
 *
 * This header defines many short global names (G, S, Gn, Sp, gen, CTRL,
 * FLOW, target(), ...).  Include it last.
 */
#ifndef CARLOBOMB_LEGACY_EMBED_H
#define CARLOBOMB_LEGACY_EMBED_H

#include <stdio.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

#define main legacy_original_main
#define printf(...) ((void)0)
#include "../solvers/level/fastest/c/legacy_original.c"
#undef printf
#undef main

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Longest target the original's unchecked stack is guaranteed to hold. */
#define LEGACY_SAFE_TARGET_LEN (MAX_STACK / 6)

static int legacy_solve_target(const char *t, int len, int max) {
    target_str = t;
    target_len = len;
    return solve(max);
}

#endif
