/*
 * Benchmark: C LevelSolver (as built by this compiler) versus the AMD64
 * assembly LevelSolver, on identical workloads.
 *
 * A workload is one target.  Each engine runs the baseline protocol: node
 * budgets 2, 3, ..., 10 through its public API, stopping at the first
 * success.  Before timing, the two engines are compared budget by budget:
 * status, graph, step count and peak stack must all agree.
 *
 * Timing: the engines are sampled alternately (C, ASM, kernel-only, C, ...)
 * so that slow drift of the machine affects all of them alike.  Each sample
 * repeats the run until it lasts at least MIN_SAMPLE_SECONDS.  Reported per
 * run: minimum and median wall time, and minimum time-stamp-counter ticks
 * per search step.
 *
 * "ASM kernel" is the assembly routine alone, without the wrapper's
 * validation, export and verification, to show the API overhead.
 *
 * Usage: asm_comparison [target ...]
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#include "asm_solver.h"

#ifndef CARLOBOMB_REVISION
#define CARLOBOMB_REVISION "unknown"
#endif
#ifndef CARLOBOMB_C_FLAGS
#define CARLOBOMB_C_FLAGS "unknown"
#endif
#ifndef CARLOBOMB_SYSTEM
#define CARLOBOMB_SYSTEM "unknown"
#endif

#define SAMPLES 15
#define MIN_SAMPLE_SECONDS 0.04
#define BUDGET_FIRST 2
#define BUDGET_LAST 10

static int run_c(const char *t, size_t len) {
    RevGraph g;
    for (int n = BUDGET_FIRST; n <= BUDGET_LAST; n++)
        if (rev_level_solve_ex(t, len, n, NULL, &g, NULL) == REV_SOLVE_SOLVED) return g.node_count;
    return 0;
}

static int run_asm(const char *t, size_t len) {
    RevGraph g;
    for (int n = BUDGET_FIRST; n <= BUDGET_LAST; n++)
        if (rev_level_solve_amd64_ex(t, len, n, NULL, &g, NULL) == REV_SOLVE_SOLVED)
            return g.node_count;
    return 0;
}

static int run_kernel(const char *t, size_t len) {
    for (int n = BUDGET_FIRST; n <= BUDGET_LAST; n++)
        if (cb_amd64_kernel_only(t, len, n, NULL) == CB_RES_SOLVED) return n;
    return 0;
}

typedef int (*RunFn)(const char *, size_t);
enum { E_C, E_ASM, E_KERNEL, ENGINES };
static const RunFn engines[ENGINES] = {run_c, run_asm, run_kernel};

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct {
    double min_s, med_s, min_ticks;
} Timing;

static void measure(const char *t, size_t len, Timing out[ENGINES]) {
    long reps = 1;
    volatile int sink = 0;
    for (;;) { /* calibrate on the C engine; also warms everything up */
        double t0 = cb_now();
        for (long i = 0; i < reps; i++) sink += run_c(t, len);
        if (cb_now() - t0 >= MIN_SAMPLE_SECONDS || reps >= (1L << 28)) break;
        reps *= 2;
    }
    for (int e = 0; e < ENGINES; e++)
        for (long i = 0; i < (reps < 16 ? reps : 16); i++) sink += engines[e](t, len);

    static double s[ENGINES][SAMPLES];
    double ticks[ENGINES];
    for (int e = 0; e < ENGINES; e++) ticks[e] = 1e300;
    for (int k = 0; k < SAMPLES; k++)
        for (int e = 0; e < ENGINES; e++) {
            double t0 = cb_now();
            unsigned long long c0 = __rdtsc();
            for (long i = 0; i < reps; i++) sink += engines[e](t, len);
            unsigned long long c1 = __rdtsc();
            s[e][k] = (cb_now() - t0) / (double)reps;
            double tk = (double)(c1 - c0) / (double)reps;
            if (tk < ticks[e]) ticks[e] = tk;
        }
    for (int e = 0; e < ENGINES; e++) {
        qsort(s[e], SAMPLES, sizeof s[e][0], cmp_double);
        out[e].min_s = s[e][0];
        out[e].med_s = s[e][SAMPLES / 2];
        out[e].min_ticks = ticks[e];
    }
    (void)sink;
}

/* Budget-by-budget agreement of the two engines; fills the run's totals. */
static int agree(const char *t, size_t len, int *nodes, uint64_t *steps, int *peak) {
    int ok = 1;
    *nodes = 0; *steps = 0; *peak = 0;
    for (int n = BUDGET_FIRST; n <= BUDGET_LAST; n++) {
        RevGraph gc, ga;
        RevLevelStats sc, sa;
        RevSolveResult rc = rev_level_solve_ex(t, len, n, NULL, &gc, &sc);
        RevSolveResult ra = rev_level_solve_amd64_ex(t, len, n, NULL, &ga, &sa);
        if (rc != ra || sc.steps != sa.steps || sc.peak_stack != sa.peak_stack ||
            gc.node_count != ga.node_count ||
            memcmp(gc.nodes, ga.nodes, (size_t)gc.node_count * sizeof(RevNode)) != 0)
            ok = 0;
        *steps += sc.steps;
        if (sc.peak_stack > *peak) *peak = sc.peak_stack;
        if (rc == REV_SOLVE_SOLVED) { *nodes = gc.node_count; break; }
        if (rc != REV_SOLVE_PROVEN_UNSAT) { ok = 0; break; }
    }
    return ok;
}

static void cpu_info(char *name, size_t ncap, char *isa, size_t icap) {
    static const char *want[] = {"sse2", "sse4_2", "popcnt", "avx", "avx2", "bmi1", "bmi2",
                                 "avx512f"};
    static char line[8192];
    snprintf(name, ncap, "unknown");
    isa[0] = '\0';
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;
    int got_name = 0, got_flags = 0;
    while (fgets(line, sizeof line, f) && !(got_name && got_flags)) {
        char *p = strchr(line, ':');
        if (!p) continue;
        line[strcspn(line, "\n")] = '\0';
        if (!got_name && strncmp(line, "model name", 10) == 0) {
            snprintf(name, ncap, "%s", p + 2);
            got_name = 1;
        } else if (!got_flags && strncmp(line, "flags", 5) == 0) {
            size_t used = 0;
            strcat(p, " "); /* so the last flag is also followed by a space */
            for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
                char key[32];
                snprintf(key, sizeof key, " %s ", want[i]);
                if (strstr(p, key) && used + strlen(want[i]) + 2 < icap)
                    used += (size_t)snprintf(isa + used, icap - used, "%s ", want[i]);
            }
            got_flags = 1;
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

    char cpu[200], isa[200];
    cpu_info(cpu, sizeof cpu, isa, sizeof isa);
    printf("CarloBomb C vs AMD64 assembly LevelSolver\n");
    printf("revision:   %s\n", CARLOBOMB_REVISION);
    printf("C compiler: %s\n", __VERSION__);
    printf("C flags:    %s\n", CARLOBOMB_C_FLAGS);
    printf("platform:   %s\n", CARLOBOMB_SYSTEM);
    printf("processor:  %s\n", cpu);
    printf("ISA (of those checked): %s\n", isa);
    printf("protocol:   budgets %d..%d, stop at first success; %d interleaved samples of >= %.0f ms\n",
           BUDGET_FIRST, BUDGET_LAST, SAMPLES, MIN_SAMPLE_SECONDS * 1e3);
    printf("times in microseconds per run; ticks = TSC ticks per search step (minimum)\n\n");

    printf("| target | nodes | steps | peak | agree | C min | C med | ASM min | ASM med "
           "| speedup min | speedup med | ASM kernel min | C ticks | ASM ticks |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");

    int bad = 0;
    double log_sum = 0;
    int heavy = 0;
    for (int i = 0; i < count; i++) {
        const char *t = w[i];
        size_t len = strlen(t);
        if (rev_target_validate(t, len) != REV_OK) {
            fprintf(stderr, "skipping invalid target: %s\n", t);
            bad = 1;
            continue;
        }
        int nodes, peak;
        uint64_t steps;
        int ok = agree(t, len, &nodes, &steps, &peak);
        if (!ok) bad = 1;
        Timing tm[ENGINES];
        measure(t, len, tm);
        double sp_min = tm[E_C].min_s / tm[E_ASM].min_s;
        double sp_med = tm[E_C].med_s / tm[E_ASM].med_s;
        printf("| %s | %d | %llu | %d | %s | %.3f | %.3f | %.3f | %.3f | %.2f | %.2f | %.3f "
               "| %.1f | %.1f |\n",
               t, nodes, (unsigned long long)steps, peak, ok ? "yes" : "NO", tm[E_C].min_s * 1e6,
               tm[E_C].med_s * 1e6, tm[E_ASM].min_s * 1e6, tm[E_ASM].med_s * 1e6, sp_min, sp_med,
               tm[E_KERNEL].min_s * 1e6, tm[E_C].min_ticks / (double)steps,
               tm[E_ASM].min_ticks / (double)steps);
        fflush(stdout);
        if (steps >= 100000) {
            log_sum += log(sp_med);
            heavy++;
        }
    }
    if (heavy)
        printf("\ngeometric mean speedup (median times, %d workloads with >= 100000 steps): %.2f\n",
               heavy, exp(log_sum / heavy));
    return bad;
}
