/*
 * proof_only: what a level costs when every REV is already known.
 *
 *   proof_only SHORTER LONGER [--class-bits P] [--jobs J]
 *
 * SHORTER is a complete Bomb for X - 1 bits, LONGER one for X bits.  The
 * level X is built on SHORTER twice with the SharedSolver, and the steps are
 * counted:
 *
 *   as it is built    a pass at budget N searches the strings open at N; a
 *                     string leaves when its REV is found;
 *   proofs only       a pass at budget N searches only the strings that the
 *                     longer Bomb says have no REV of N nodes.  It finds
 *                     nothing and proves that.  A string whose minimum is its
 *                     lower bound is not searched at all.
 *
 * The second is what would be left to do if something else supplied the
 * REVs: the lower bounds still have to be proven, the REVs do not have to be
 * found.  LONGER is used as an oracle for which strings those are; nothing
 * is certified by this program.  It reads the Bombs and writes nothing.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"
#include "shared_solver.h"

enum { MAXB = 32 };

typedef struct {
    uint64_t wanted[MAXB], found[MAXB], steps[MAXB];           /* as it is built */
    uint64_t proof_wanted[MAXB], proof_steps[MAXB];            /* proofs only */
    uint64_t same, more, settled;
} Sums;

static const char *commas(uint64_t v) {
    static char bufs[16][32];
    static int next;
    char digits[24], *out = bufs[next];
    int n, k = 0;
    next = (next + 1) % 16;
    n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    for (int i = 0; i < n; i++) {
        out[k++] = digits[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[k++] = ',';
    }
    out[k] = '\0';
    return out;
}

static uint8_t *found_map;
static uint32_t found_first;
static int on_found(void *ctx, uint32_t string, const RevGraph *rev, uint64_t steps) {
    uint32_t j = string - found_first;
    (void)ctx;
    (void)rev;
    (void)steps;
    found_map[j >> 3] |= (uint8_t)(1u << (j & 7));
    return 0;
}

int main(int argc, char **argv) {
    const char *paths[2] = {NULL, NULL};
    RevFixedBomb *shorter = NULL, *longer = NULL;
    int X, P = 8, jobs = 4, npaths = 0, fds[2];
    uint32_t classes, size;
    Sums total;
    double t0 = (double)time(NULL);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--class-bits") && i + 1 < argc) P = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (npaths < 2) paths[npaths++] = argv[i];
    }
    if (npaths != 2 || jobs < 1 || jobs > 64) return fprintf(stderr, "usage: proof_only SHORTER LONGER [--class-bits P] [--jobs J]\n"), 2;
    if (rev_fixed_bomb_open(paths[0], &shorter, NULL) != FRB_OK || rev_fixed_bomb_open(paths[1], &longer, NULL) != FRB_OK)
        return fprintf(stderr, "proof_only: cannot open the Bombs\n"), 1;
    X = rev_fixed_bomb_bits(longer);
    if (rev_fixed_bomb_bits(shorter) != X - 1 || P < 1 || P >= X) return fprintf(stderr, "proof_only: Bombs one bit apart, and 1 <= P < X\n"), 1;
    classes = (uint32_t)1 << (P - 1); /* those whose strings start with 0 */
    size = (uint32_t)1 << (X - P);
    if (pipe(fds) != 0) return 1;
    fflush(NULL);

    for (int k = 0; k < jobs; k++) {
        pid_t pid = fork();
        if (pid < 0) return 1;
        if (pid == 0) {
            RevShared *s = rev_shared_create(X, P);
            uint8_t *need = malloc(size), *min = malloc(size), *wanted = malloc(size / 8 + 1);
            RevSharedHooks hooks = {0};
            Sums sums;
            memset(&sums, 0, sizeof sums);
            found_map = malloc(size / 8 + 1);
            hooks.found = on_found;
            close(fds[0]);
            if (!s || !need || !min || !wanted || !found_map) _exit(3);
            for (uint32_t c = (uint32_t)k; c < classes; c += (uint32_t)jobs) {
                uint32_t first = c << (X - P);
                int lowest = MAXB, highest = 0;
                for (uint32_t j = 0; j < size; j += 2) { /* the two extensions of one string of X - 1 bits */
                    char text[FRB_MAX_BITS + 1];
                    RevFixedBombInfo info;
                    RevGraph g;
                    uint32_t q = (first + j) >> 1;
                    int m, zero, other;
                    frb_target_format(q, X - 1, text);
                    if (rev_fixed_bomb_solve(shorter, text, (size_t)(X - 1), 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) _exit(4);
                    m = g.node_count;
                    zero = (int)(frb_graph_output(&g, X) & 1u);
                    other = zero ^ 1;
                    need[j + (uint32_t)zero] = 0; /* settled by the REV of its first bits */
                    need[j + (uint32_t)other] = (uint8_t)m;
                    min[j + (uint32_t)zero] = (uint8_t)m;
                    min[j + (uint32_t)other] = (uint8_t)rev_fixed_bomb_min_nodes(longer, X, first + j + (uint32_t)other);
                    sums.settled++;
                    if (min[j + (uint32_t)other] == m) sums.same++;
                    else sums.more++;
                    if (m < lowest) lowest = m;
                    if (min[j + (uint32_t)other] > highest) highest = min[j + (uint32_t)other];
                }
                /* As it is built. */
                memset(found_map, 0, size / 8 + 1);
                found_first = first;
                for (int n = lowest; n <= highest; n++) {
                    RevSharedStats st;
                    uint64_t asked = 0;
                    memset(wanted, 0, size / 8 + 1);
                    for (uint32_t j = 0; j < size; j++)
                        if (need[j] && need[j] <= n && !((found_map[j >> 3] >> (j & 7)) & 1)) wanted[j >> 3] |= (uint8_t)(1u << (j & 7)), asked++;
                    if (!asked) continue;
                    if (rev_shared_pass(s, c, n, wanted, &hooks, &st) != REV_SHARED_DONE) _exit(5);
                    sums.wanted[n] += st.wanted;
                    sums.found[n] += st.found;
                    sums.steps[n] += st.steps;
                }
                for (uint32_t j = 0; j < size; j++)
                    if (need[j] && !((found_map[j >> 3] >> (j & 7)) & 1)) _exit(6); /* every string has its REV */
                /* Proofs only. */
                for (int n = lowest; n < highest; n++) {
                    RevSharedStats st;
                    uint64_t asked = 0;
                    memset(wanted, 0, size / 8 + 1);
                    for (uint32_t j = 0; j < size; j++)
                        if (need[j] && need[j] <= n && min[j] > n) wanted[j >> 3] |= (uint8_t)(1u << (j & 7)), asked++;
                    if (!asked) continue;
                    if (rev_shared_pass(s, c, n, wanted, NULL, &st) != REV_SHARED_DONE || st.found != 0) _exit(7);
                    sums.proof_wanted[n] += st.wanted;
                    sums.proof_steps[n] += st.steps;
                }
            }
            if (write(fds[1], &sums, sizeof sums) != (ssize_t)sizeof sums) _exit(8);
            _exit(0);
        }
    }
    close(fds[1]);
    memset(&total, 0, sizeof total);
    for (int k = 0; k < jobs; k++) {
        Sums one;
        int status = 0;
        if (read(fds[0], &one, sizeof one) != (ssize_t)sizeof one) return fprintf(stderr, "proof_only: a worker failed\n"), 1;
        for (size_t i = 0; i < sizeof total / sizeof(uint64_t); i++) ((uint64_t *)&total)[i] += ((uint64_t *)&one)[i];
        wait(&status);
    }
    {
        uint64_t steps = 0, proof = 0;
        printf("Level %d on the Bomb of %d bits, in classes of %d bits (%s strings a class), %d processes\n", X, X - 1, P, commas(size), jobs);
        printf("  strings that start with 0: %s settled by the REV of their first bits; of the others, %s need what their\n"
               "  first bits need and %s need more\n\n", commas(total.settled), commas(total.same), commas(total.more));
        printf("  %-6s | %14s %14s %18s | %14s %18s\n", "budget", "searched", "found", "steps", "proofs only", "steps");
        for (int n = 1; n < MAXB; n++) {
            if (!total.wanted[n] && !total.proof_wanted[n]) continue;
            printf("  %-6d | %14s %14s %18s | %14s %18s\n", n, commas(total.wanted[n]), commas(total.found[n]), commas(total.steps[n]),
                   commas(total.proof_wanted[n]), commas(total.proof_steps[n]));
            steps += total.steps[n];
            proof += total.proof_steps[n];
        }
        printf("  %-6s | %14s %14s %18s | %14s %18s\n", "all", "", "", commas(steps), "", commas(proof));
        printf("\n  with every REV known beforehand, %.1f%% of the steps are left: 1/%.1f (%.0f s)\n",
               steps ? 100.0 * (double)proof / (double)steps : 0.0, proof ? (double)steps / (double)proof : 0.0,
               (double)time(NULL) - t0);
    }
    return 0;
}
