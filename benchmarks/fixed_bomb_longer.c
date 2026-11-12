/*
 * What a 16-bit Bomb knows about targets longer than 16 bits, measured.
 *
 *   fixed_bomb_longer BOMB.frb [targets-per-length]
 *
 * Unlike fixed_bomb_lookup this program does search: it needs the true
 * minimum of each longer target to compare the Bomb's lower bound with, and
 * gets it from the MinimalSolver (the assembly engine where there is one).
 *
 * Part 1, random targets of 18 to 30 bits.  For each: the Bomb's certified
 * lower bound against the minimum; whether the REV stored for the first 16
 * bits happens to emit the whole target (it is then a certified minimum for
 * it without any search); and how many of the search's steps were spent on
 * node budgets below the bound, which a search started at the bound would
 * not have to spend.
 *
 * Part 2, the strings 00...01.  For every length up to 16 they are among the
 * strings that need the most nodes, and that number is ceil(2*sqrt(L)) - 1.
 * Here the same strings are searched beyond 16 bits.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"
#include "minimal_solver.h"
#ifdef CARLOBOMB_HAVE_AMD64_ASM
#include "asm_solver.h"
#endif

static uint64_t rng_state = 88172645463325252ull;

static int random_bit(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (int)((rng_state >> 40) & 1u);
}

int main(int argc, char **argv) {
    RevLevelSolveFn engine = rev_level_solve_ex;
    const char *engine_name = "c";
    RevFixedBomb *bomb = NULL;
    int per_length = argc > 2 ? atoi(argv[2]) : 300;
    char target[64];
    FrbError e;

    if (argc < 2 || argc > 3 || per_length < 1) {
        fprintf(stderr, "usage: fixed_bomb_longer BOMB.frb [targets-per-length]\n");
        return 2;
    }
    e = rev_fixed_bomb_open(argv[1], &bomb, NULL);
    if (e != FRB_OK || rev_fixed_bomb_bits(bomb) != 16) {
        fprintf(stderr, "%s: need a complete FixedRunBomb(16) (%s)\n", argv[1], frb_error_str(e));
        return 1;
    }
#ifdef CARLOBOMB_HAVE_AMD64_ASM
    if (cb_amd64_kernel_select(cb_amd64_kernel_find("auto")) == 0) {
        engine = rev_level_solve_amd64_ex;
        engine_name = "amd64-auto";
    }
#endif

    printf("Random targets (search engine %s)\n", engine_name);
    printf("  Bits  Targets  Mean minimum  Mean bound  Bound exact  Stored REV fits  Steps below the bound\n");
    for (int len = 18; len <= 30; len += 4) {
        int count = len <= 26 ? per_length : (per_length + 4) / 5, exact = 0, fits = 0;
        unsigned long long total = 0, below = 0;
        double sum_min = 0, sum_bound = 0;
        for (int k = 0; k < count; k++) {
            RevFixedBombInfo info;
            RevMinimalInfo mi;
            RevLevelStats st;
            RevGraph g;
            int bound;
            for (int i = 0; i < len; i++) target[i] = (char)('0' + random_bit());
            target[len] = '\0';
            if (rev_minimal_solve_with(engine, target, (size_t)len, 0, NULL, &g, &mi) != REV_MIN_MINIMAL)
                return 1;
            bound = rev_fixed_bomb_lower_bound(bomb, target, (size_t)len);
            if (bound > mi.node_count) {
                printf("UNSOUND BOUND for %s: %d > %d\n", target, bound, mi.node_count);
                return 1;
            }
            total += mi.steps;
            sum_min += mi.node_count;
            sum_bound += bound;
            exact += bound == mi.node_count;
            for (int n = 1; n < bound; n++) { /* what the search spent below the bound */
                engine(target, (size_t)len, n, NULL, &g, &st);
                below += st.steps;
            }
            if (rev_fixed_bomb_solve(bomb, target, 16, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL &&
                rev_verify_prefix(&g, target, (size_t)len, NULL) == REV_OK)
                fits++;
        }
        printf("  %4d  %7d  %12.2f  %10.2f  %11d  %15d  %20.1f%%\n", len, count, sum_min / count,
               sum_bound / count, exact, fits, 100.0 * (double)below / (double)total);
    }

    printf("\nThe strings 00...01\n");
    printf("  Bits  ceil(2*sqrt(L))-1  Minimum          Source  Bomb's bound\n");
    for (int len = 1; len <= 28; len++) {
        int formula = (int)ceil(2.0 * sqrt((double)len) - 1e-9) - 1, nodes;
        memset(target, '0', (size_t)len);
        target[len - 1] = '1';
        target[len] = '\0';
        if (len <= 16) {
            nodes = rev_fixed_bomb_min_nodes(bomb, len, 1);
        } else {
            RevMinimalInfo mi;
            RevGraph g;
            if (rev_minimal_solve_with(engine, target, (size_t)len, 0, NULL, &g, &mi) != REV_MIN_MINIMAL)
                return 1;
            nodes = mi.node_count;
        }
        printf("  %4d  %17d  %7d  %14s  %12d%s\n", len, formula, nodes, len <= 16 ? "Bomb" : "search",
               rev_fixed_bomb_lower_bound(bomb, target, (size_t)len), nodes == formula ? "" : "   <-- differs");
    }
    rev_fixed_bomb_close(bomb);
    return 0;
}
