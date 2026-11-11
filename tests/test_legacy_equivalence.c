/*
 * Differential test: the parameterized LevelSolver against the unmodified
 * legacy_original.c search kernel, on identical (target, budget) inputs.
 *
 * Both must agree on solvable / unsolvable, and because the move order is
 * meant to be preserved, on the exact first solution found.  The only
 * permitted difference is that fields the legacy solver leaves UNDECIDED are
 * filled in by LevelSolver (links -> node 0).
 */
#include "level_solver.h"
#include "test_util.h"

#include "legacy_embed.h" /* last: defines short global names */

static int cases, solved_cases, undecided_fields;

/* Returns 1 if both solvers found a solution. */
static int compare(const char *t, int len, int max) {
    RevGraph g;
    RevSolveResult r = rev_level_solve(t, (size_t)len, max, &g);
    int legacy = legacy_solve_target(t, len, max);
    cases++;

    CHECK_MSG((r == REV_SOLVE_SOLVED) == (legacy == 1), "%.*s max %d: level %s, legacy %d", len, t,
              max, rev_solve_result_str(r), legacy);
    CHECK(r == REV_SOLVE_SOLVED || r == REV_SOLVE_PROVEN_UNSAT);
    if (r != REV_SOLVE_SOLVED || !legacy) return 0;
    solved_cases++;

    int same = g.node_count == Gn;
    for (int i = 0; same && i < Gn; i++) {
        /* Colors are always decided by the time a solution is found. */
        if (G[i].color == UNDECIDED || G[i].color != g.nodes[i].color) same = 0;
        for (int k = 0; k < 2; k++) {
            if (G[i].links[k] == UNDECIDED) {
                undecided_fields++;
                if (g.nodes[i].links[k] != 0) same = 0;
            } else if (G[i].links[k] != g.nodes[i].links[k]) same = 0;
        }
    }
    CHECK_MSG(same, "%.*s max %d: graphs differ", len, t, max);
    return 1;
}

int main(void) {
    char t[64];

    /* Every target of length 1..10 at budgets 1..5. */
    for (int len = 1; len <= 10; len++)
        for (unsigned v = 0; v < (1u << len); v++) {
            for (int i = 0; i < len; i++) t[i] = (char)('0' + ((v >> (len - 1 - i)) & 1));
            t[len] = '\0';
            for (int max = 1; max <= 5; max++) compare(t, len, max);
        }

    /* Pseudo-random longer targets at budgets up to the legacy maximum. */
    unsigned long long x = 0x9E3779B97F4A7C15ull;
    for (int n = 0; n < 200; n++) {
        int len = 11 + n % 8;
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            t[i] = (char)('0' + (int)((x >> 32) & 1));
        }
        t[len] = '\0';
        for (int max = 1; max <= MAX_NODES; max++)
            if (compare(t, len, max)) break; /* larger budgets add nothing new */
    }

    /* The historical run exactly as legacy main() performs it. */
    for (int max = 2; max <= MAX_NODES; max++) compare("010010001", 9, max);

    printf("%d cases, %d solved, %d legacy fields left UNDECIDED and completed\n", cases,
           solved_cases, undecided_fields);
    return t_report("test_legacy_equivalence");
}
