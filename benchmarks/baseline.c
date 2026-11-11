/*
 * Baseline benchmark: unmodified legacy_original.c search kernel versus the
 * parameterized C LevelSolver, on identical workloads.
 *
 * A workload is one target string.  Both solvers run the protocol of the
 * legacy main(): try node budgets 2, 3, ..., 10 and stop at the first that
 * succeeds.  Only that loop is timed; printing is compiled out of the legacy
 * code (see tests/legacy_embed.h) and LevelSolver's timing includes its
 * input validation, graph completion and independent verification, which
 * are part of its API.
 *
 * Usage: baseline [target ...]      (default: a built-in workload list)
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level_solver.h"

#include "legacy_embed.h" /* last: defines short global names */

#ifndef CARLOBOMB_REVISION
#define CARLOBOMB_REVISION "unknown"
#endif
#ifndef CARLOBOMB_C_FLAGS
#define CARLOBOMB_C_FLAGS "unknown"
#endif
#ifndef CARLOBOMB_SYSTEM
#define CARLOBOMB_SYSTEM "unknown"
#endif

#define SAMPLES 9
#define MIN_SAMPLE_SECONDS 0.05

typedef struct {
    int solved, nodes;
    uint64_t steps; /* LevelSolver only */
    int peak_stack; /* LevelSolver only */
} Outcome;

static Outcome run_legacy(const char *t, int len) {
    Outcome o = {0, 0, 0, 0};
    for (int n = 2; n <= MAX_NODES; n++)
        if (legacy_solve_target(t, len, n)) { o.solved = 1; o.nodes = Gn; break; }
    return o;
}

static Outcome run_level(const char *t, int len) {
    Outcome o = {0, 0, 0, 0};
    RevGraph g;
    RevLevelStats st;
    for (int n = 2; n <= MAX_NODES; n++) {
        RevSolveResult r = rev_level_solve_ex(t, (size_t)len, n, NULL, &g, &st);
        o.steps += st.steps;
        if (st.peak_stack > o.peak_stack) o.peak_stack = st.peak_stack;
        if (r == REV_SOLVE_SOLVED) { o.solved = 1; o.nodes = g.node_count; break; }
        if (r != REV_SOLVE_PROVEN_UNSAT) { o.solved = -1; break; }
    }
    return o;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Seconds per run: minimum and median over SAMPLES samples, each sample
 * being enough repetitions to last at least MIN_SAMPLE_SECONDS. */
static void measure(Outcome (*fn)(const char *, int), const char *t, int len, double *min_s,
                    double *med_s, long *reps_out, Outcome *out) {
    long reps = 1;
    volatile int sink = 0;
    for (;;) { /* calibrate */
        double t0 = cb_now();
        for (long i = 0; i < reps; i++) sink += fn(t, len).nodes;
        if (cb_now() - t0 >= MIN_SAMPLE_SECONDS || reps >= (1L << 28)) break;
        reps *= 2;
    }
    double s[SAMPLES];
    for (int k = 0; k < SAMPLES; k++) {
        double t0 = cb_now();
        for (long i = 0; i < reps; i++) sink += fn(t, len).nodes;
        s[k] = (cb_now() - t0) / (double)reps;
    }
    qsort(s, SAMPLES, sizeof s[0], cmp_double);
    *min_s = s[0];
    *med_s = s[SAMPLES / 2];
    *reps_out = reps;
    *out = fn(t, len);
    (void)sink;
}

static void cpu_name(char *buf, size_t cap) {
    snprintf(buf, cap, "unknown");
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *p = strchr(line, ':');
            if (p) {
                p += 2;
                p[strcspn(p, "\n")] = '\0';
                snprintf(buf, cap, "%s", p);
            }
            break;
        }
    }
    fclose(f);
}

static const char *default_workloads[] = {
    "010010001", /* the historical target */
    "00000000",
    "01010101",
    "0110100110010110",
    "001011011101111011111",
    "110100100010000100000",
    "0100011011000001010011",
    "01101001100101101001011001101001",
    "0001101100101110100111",
    "000001000110010100111010",
};

int main(int argc, char **argv) {
    const char **w = default_workloads;
    int count = (int)(sizeof default_workloads / sizeof *default_workloads);
    if (argc > 1) { w = (const char **)(argv + 1); count = argc - 1; }

    char cpu[200];
    cpu_name(cpu, sizeof cpu);
    printf("CarloBomb baseline benchmark\n");
    printf("revision:  %s\n", CARLOBOMB_REVISION);
#if defined(__VERSION__)
    printf("compiler:  %s\n", __VERSION__);
#elif defined(_MSC_VER)
    printf("compiler:  MSVC %d\n", _MSC_VER);
#endif
    printf("flags:     %s\n", CARLOBOMB_C_FLAGS);
    printf("platform:  %s\n", CARLOBOMB_SYSTEM);
    printf("processor: %s\n", cpu);
    printf("protocol:  budgets 2..%d, stop at first success; %d samples of >= %.0f ms each\n",
           MAX_NODES, SAMPLES, MIN_SAMPLE_SECONDS * 1e3);
    printf("legacy:    solvers/level/fastest/c/legacy_original.c (unmodified, printing disabled)\n");
    printf("level:     solvers/level/fastest/c/level_solver.c\n\n");

    printf("| target | len | status | nodes | steps | peak stack | legacy min us | legacy med us "
           "| level min us | level med us | level/legacy (min) |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|\n");

    int bad = 0;
    for (int i = 0; i < count; i++) {
        const char *t = w[i];
        size_t len = strlen(t);
        if (rev_target_validate(t, len) != REV_OK || len > LEGACY_SAFE_TARGET_LEN) {
            fprintf(stderr, "skipping unsupported target: %s\n", t);
            bad = 1;
            continue;
        }
        double lmin, lmed, vmin, vmed;
        long lreps, vreps;
        Outcome lo, vo;
        measure(run_legacy, t, (int)len, &lmin, &lmed, &lreps, &lo);
        measure(run_level, t, (int)len, &vmin, &vmed, &vreps, &vo);

        const char *status = vo.solved == 1 ? "SOLVED" : vo.solved == 0 ? "UNSAT<=10" : "LIMIT";
        if (lo.solved != (vo.solved == 1) || lo.nodes != vo.nodes) {
            status = "DISAGREE";
            bad = 1;
        }
        printf("| %s | %zu | %s | %d | %llu | %d | %.3f | %.3f | %.3f | %.3f | %.2f |\n", t, len,
               status, vo.nodes, (unsigned long long)vo.steps, vo.peak_stack, lmin * 1e6,
               lmed * 1e6, vmin * 1e6, vmed * 1e6, vmin / lmin);
        fflush(stdout);
    }
    return bad;
}
