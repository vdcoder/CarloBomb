/*
 * The two walks of the SharedSolver on the same passes: the reference walk
 * and the run kernel.
 *
 *   shared_comparison BITS CLASS_BITS CLASSES LOWEST HIGHEST [REPEAT]
 *
 * CLASSES classes of strings of BITS bits, taken across the first half of
 * the classes (strings beginning with 0).  For each, every string is wanted
 * at node budget LOWEST, and what a pass finds is not wanted at the next
 * budget, up to HIGHEST: a construction of the level from nothing, for those
 * classes.  Each pass is made by both walks, REPEAT times (the least time
 * counts), and everything a pass reports has to be the same from both: the
 * strings found, in the order found, their REVs and steps, the steps of
 * the strings left, the statistics.  A difference ends the program with
 * status 1.
 *
 * Times are processor time of this process, so that other work on the
 * machine disturbs them less than it would the wall clock.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shared_solver.h"

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xff51afd7ed558ccdull;
    return h ^ (h >> 33);
}

typedef struct {
    uint64_t digest;
    uint8_t *found;
    uint32_t mask;
} Told;

static int on_found(void *ctx, uint32_t string, const RevGraph *rev, uint64_t steps) {
    Told *t = ctx;
    uint32_t j = string & t->mask;
    t->digest = mix(mix(mix(t->digest, string), steps), (uint64_t)rev->node_count);
    for (int i = 0; i < rev->node_count; i++)
        t->digest = mix(t->digest, (uint64_t)rev->nodes[i].color | (uint64_t)rev->nodes[i].links[0] << 8 |
                                       (uint64_t)rev->nodes[i].links[1] << 16);
    t->found[j >> 3] |= (uint8_t)(1u << (j & 7));
    return 0;
}

static double cpu_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int main(int argc, char **argv) {
    static const char *const name[2] = {"reference walk", "run kernel"};
    int bits, class_bits, classes, lowest, highest, repeat;
    uint32_t size;
    uint64_t steps_by[REV_SHARED_MAX_NODES + 1] = {0};
    double time_by[2][REV_SHARED_MAX_NODES + 1] = {{0}};
    uint64_t steps_all = 0, strings = 0, found_all = 0;
    double all[2] = {0, 0};
    RevShared *s;
    uint8_t *wanted, *found[2];

    if (argc < 6) {
        fprintf(stderr, "usage: shared_comparison BITS CLASS_BITS CLASSES LOWEST HIGHEST [REPEAT]\n");
        return 2;
    }
    bits = atoi(argv[1]);
    class_bits = atoi(argv[2]);
    classes = atoi(argv[3]);
    lowest = atoi(argv[4]);
    highest = atoi(argv[5]);
    repeat = argc > 6 ? atoi(argv[6]) : 1;
    s = rev_shared_create(bits, class_bits);
    if (!s || classes < 1 || lowest < 1 || highest < lowest || highest > REV_SHARED_MAX_NODES || repeat < 1) {
        fprintf(stderr, "shared_comparison: impossible arguments\n");
        return 2;
    }
    size = rev_shared_class_size(s);
    wanted = malloc(size / 8 + 1);
    found[0] = malloc(size / 8 + 1);
    found[1] = malloc(size / 8 + 1);

    for (int c = 0; c < classes; c++) {
        /* spread over the classes whose strings begin with 0 */
        uint32_t half = class_bits > 0 ? (uint32_t)1 << (class_bits - 1) : 1;
        uint32_t klass = (uint32_t)(((uint64_t)(2 * c + 1) * half) / (uint64_t)(2 * classes));
        memset(wanted, 0xFF, size / 8 + 1);
        if (size < 8) wanted[0] = (uint8_t)((1u << size) - 1u);
        strings += size;
        for (int b = lowest; b <= highest; b++) {
            RevSharedStats st[2];
            uint64_t digest[2] = {0, 0};
            for (int e = 0; e < 2; e++) {
                double least = 0;
                for (int r = 0; r < repeat; r++) {
                    Told told = {0, found[e], size - 1};
                    RevSharedHooks hooks = {on_found, &told, NULL, NULL, 0};
                    RevSharedResult res;
                    double t0;
                    memset(found[e], 0, size / 8 + 1);
                    t0 = cpu_now();
                    res = e ? rev_shared_pass_run(s, klass, b, wanted, &hooks, &st[e])
                            : rev_shared_pass(s, klass, b, wanted, &hooks, &st[e]);
                    t0 = cpu_now() - t0;
                    if (r == 0 || t0 < least) least = t0;
                    if (res != REV_SHARED_DONE) {
                        fprintf(stderr, "%s, class %u, %d nodes: %s\n", name[e], (unsigned)klass, b, rev_shared_result_str(res));
                        return 1;
                    }
                    digest[e] = mix(mix(mix(mix(told.digest, st[e].steps), st[e].choices), st[e].string_steps), st[e].found);
                    for (uint32_t j = 0; j < size; j++)
                        if (rev_shared_unfound(s, j)) digest[e] = mix(digest[e], rev_shared_unfound_steps(s, j));
                }
                time_by[e][b] += least;
                all[e] += least;
            }
            if (digest[0] != digest[1] || memcmp(found[0], found[1], size / 8 + 1) != 0) {
                fprintf(stderr, "class %u at %d nodes: the two walks do not report the same\n", (unsigned)klass, b);
                return 1;
            }
            steps_by[b] += st[0].steps;
            steps_all += st[0].steps;
            found_all += st[0].found;
            for (uint32_t i = 0; i < size / 8 + 1; i++) wanted[i] &= (uint8_t)~found[0][i];
        }
    }

    printf("%d classes of %u strings of %d bits, node budgets %d to %d: %llu strings, %llu found\n", classes, (unsigned)size,
           bits, lowest, highest, (unsigned long long)strings, (unsigned long long)found_all);
    printf("every pass reported the same from both walks\n\n");
    printf("  budget            steps   %s   %s      steps a second            ratio\n", name[0], name[1]);
    for (int b = lowest; b <= highest; b++) {
        if (!steps_by[b]) continue;
        printf("  %6d %16llu   %12.3f s   %8.3f s   %7.1f M   %7.1f M   %6.2f\n", b, (unsigned long long)steps_by[b],
               time_by[0][b], time_by[1][b], time_by[0][b] > 0 ? (double)steps_by[b] / time_by[0][b] * 1e-6 : 0.0,
               time_by[1][b] > 0 ? (double)steps_by[b] / time_by[1][b] * 1e-6 : 0.0,
               time_by[1][b] > 0 ? time_by[0][b] / time_by[1][b] : 0.0);
    }
    printf("     all %16llu   %12.3f s   %8.3f s   %7.1f M   %7.1f M   %6.2f\n", (unsigned long long)steps_all, all[0], all[1],
           (double)steps_all / all[0] * 1e-6, (double)steps_all / all[1] * 1e-6, all[0] / all[1]);
    free(wanted);
    free(found[0]);
    free(found[1]);
    rev_shared_destroy(s);
    return 0;
}
