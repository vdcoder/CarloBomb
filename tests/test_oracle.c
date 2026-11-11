/*
 * Exhaustive completeness oracle.
 *
 * For each small node count N this enumerates every fully labeled REV with
 * exactly N nodes (2^N colorings x N^(2N) link assignments), runs each one
 * on the independent interpreter, and records every output prefix of length
 * 1..MAX_LEN.  A graph with unreachable nodes behaves like a smaller graph,
 * so the recorded set is exactly the set of targets solvable with at most N
 * nodes.
 *
 * LevelSolver must then answer SOLVED for precisely those targets and
 * PROVEN_UNSAT for all others, and MinimalSolver must report the smallest N
 * at which a target first appears.  No search code is shared with the
 * solvers: this file contains only counting loops.
 *
 * Usage: test_oracle [max_nodes]     (default 4; 5 takes roughly 100x longer)
 */
#include <stdlib.h>

#include "minimal_solver.h"
#include "test_util.h"

/* The same oracle checks either engine; the build selects one. */
#ifdef CARLOBOMB_ORACLE_AMD64
#include "asm_solver.h"
#define ENGINE_NAME "test_oracle_amd64"
#define LEVEL_SOLVE rev_level_solve_amd64
#define LEVEL_SOLVE_EX rev_level_solve_amd64_ex
#else
#define ENGINE_NAME "test_oracle"
#define LEVEL_SOLVE rev_level_solve
#define LEVEL_SOLVE_EX rev_level_solve_ex
#endif

#define MAX_LEN 10
#define ORACLE_MAX_N 5

/* reach[n][len][v]: the len-bit string with value v (first bit most
 * significant) is emitted by some graph with exactly n nodes. */
static unsigned char reach[ORACLE_MAX_N + 1][MAX_LEN + 1][1 << MAX_LEN];

static unsigned long long enumerate(int n) {
    RevGraph g;
    int digit[2 * ORACLE_MAX_N] = {0};
    unsigned long long graphs = 0;
    memset(&g, 0, sizeof g);
    g.node_count = n;

    for (;;) {
        for (int i = 0; i < n; i++) {
            g.nodes[i].links[0] = digit[2 * i];
            g.nodes[i].links[1] = digit[2 * i + 1];
        }
        for (int colors = 0; colors < (1 << n); colors++) {
            for (int i = 0; i < n; i++) g.nodes[i].color = (colors >> i) & 1;
            RevMachine m;
            unsigned v = 0;
            rev_machine_reset(&m);
            for (int len = 1; len <= MAX_LEN; len++) {
                v = (v << 1) | (unsigned)rev_machine_next_bit(&g, &m);
                reach[n][len][v] = 1;
            }
            graphs++;
        }
        /* next link assignment: increment a base-n counter of 2n digits */
        int k = 0;
        while (k < 2 * n && ++digit[k] == n) digit[k++] = 0;
        if (k == 2 * n) break;
    }
    return graphs;
}

static void to_string(unsigned v, int len, char *out) {
    for (int i = 0; i < len; i++) out[i] = (char)('0' + ((v >> (len - 1 - i)) & 1));
    out[len] = '\0';
}

int main(int argc, char **argv) {
    int max_n = argc > 1 ? atoi(argv[1]) : 4;
    if (max_n < 1 || max_n > ORACLE_MAX_N) {
        fprintf(stderr, "max_nodes must be 1..%d\n", ORACLE_MAX_N);
        return 2;
    }

    for (int n = 1; n <= max_n; n++) {
        unsigned long long count = enumerate(n);
        unsigned long long expect = 1ull << n;
        for (int i = 0; i < 2 * n; i++) expect *= (unsigned long long)n;
        CHECK(count == expect);
        printf("N=%d: %llu graphs enumerated\n", n, count);
    }

    /* Sanity of the oracle itself. */
    for (int len = 1; len <= MAX_LEN; len++) {
        int ones = 0;
        for (unsigned v = 0; v < (1u << len); v++) ones += reach[1][len][v];
        CHECK(ones == 2); /* one node emits only 00..0 or 11..1 */
        for (int n = 2; n <= max_n; n++)
            for (unsigned v = 0; v < (1u << len); v++)
                if (reach[n - 1][len][v]) CHECK(reach[n][len][v]);
    }

    /* LevelSolver: SOLVED exactly on the oracle's set. */
    char t[MAX_LEN + 1];
    for (int n = 1; n <= max_n; n++) {
        for (int len = 1; len <= MAX_LEN; len++) {
            int sat = 0, missed = 0, phantom = 0;
            for (unsigned v = 0; v < (1u << len); v++) {
                RevGraph g;
                to_string(v, len, t);
                RevSolveResult r = LEVEL_SOLVE(t, (size_t)len, n, &g);
                CHECK_MSG(r == REV_SOLVE_SOLVED || r == REV_SOLVE_PROVEN_UNSAT, "%s N=%d: %s", t, n,
                          rev_solve_result_str(r));
                if (r == REV_SOLVE_SOLVED) t_check_witness(&g, t, (size_t)len, n);
                if (reach[n][len][v]) {
                    sat++;
                    if (r != REV_SOLVE_SOLVED) missed++;
                } else if (r != REV_SOLVE_PROVEN_UNSAT) phantom++;
                CHECK_MSG((r == REV_SOLVE_SOLVED) == (reach[n][len][v] != 0),
                          "completeness: %s N=%d oracle=%d solver=%s", t, n, reach[n][len][v],
                          rev_solve_result_str(r));
            }
            if (len == MAX_LEN || missed || phantom)
                printf("N=%d L=%d: %d of %u targets solvable, solver missed %d, wrongly solved %d\n",
                       n, len, sat, 1u << len, missed, phantom);
        }
    }

    /* MinimalSolver: reports the first N at which the oracle sees the target. */
    for (int len = 1; len <= MAX_LEN; len++) {
        for (unsigned v = 0; v < (1u << len); v++) {
            RevGraph g;
            RevMinimalInfo info;
            int oracle_min = 0;
            for (int n = 1; n <= max_n && !oracle_min; n++)
                if (reach[n][len][v]) oracle_min = n;
            to_string(v, len, t);
            RevMinimalStatus s =
                rev_minimal_solve_with(LEVEL_SOLVE_EX, t, (size_t)len, max_n, NULL, &g, &info);
            if (oracle_min) {
                CHECK_MSG(s == REV_MIN_MINIMAL && info.node_count == oracle_min,
                          "minimal: %s oracle=%d solver=%s/%d", t, oracle_min,
                          rev_minimal_status_str(s), info.node_count);
                if (s == REV_MIN_MINIMAL) t_check_witness(&g, t, (size_t)len, oracle_min);
            } else {
                CHECK_MSG(s == REV_MIN_BUDGET_EXHAUSTED && info.lower_bound == max_n + 1,
                          "minimal: %s oracle=none solver=%s", t, rev_minimal_status_str(s));
            }
        }
    }

    return t_report(ENGINE_NAME);
}
