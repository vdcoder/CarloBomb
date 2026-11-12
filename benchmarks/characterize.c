/*
 * Workload characterization of the LevelSolver engines.
 *
 * For each node budget this sorts pseudo-random targets into classes by how
 * their search ends and how long it is:
 *
 *   completed (SOLVED or PROVEN_UNSAT) in     <= 2,000 steps
 *   completed in                              2,001 .. 100,000 steps
 *   completed in                              100,001 .. 2,000,000 steps
 *   cut off by a step limit of                2,000 steps
 *   cut off by a step limit of                100,000 steps
 *   cut off by a step limit of                2,000,000 steps
 *
 * and then, class by class, reports
 *
 *   - time per step of the C LevelSolver through its API, and the speedup of
 *     the reference assembly kernel ("asm") and of the automatic choice
 *     ("auto") over it, also through the API (validation, export and
 *     verification included);
 *   - "auto" against "asm", through the API and kernel only;
 *   - search accounting, from the benchmark-only counting kernel that
 *     matches "auto": how many of the logical steps were executed one at a
 *     time and how many were accounted for in bulk;
 *   - where the machine provides hardware counters: cycles, instructions,
 *     branches and branch misses per step for the two kernels.
 *
 * Every class is a set of distinct targets large enough for one pass to be
 * at least MIN_PASS_STEPS steps, so that the branch predictor cannot learn
 * the workload by heart.  Engines are timed round-robin and compared within
 * each round, as in repr_comparison.  All engines must agree on every
 * result and step count.
 *
 * Usage: characterize [-n N,N,...] [-r rounds] [-s min_pass_steps]
 *                     [-t seconds_per_budget_for_sorting] [--perf-selftest]
 */
#define _GNU_SOURCE /* sched_setaffinity */
#include "timer.h"

#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#include "asm_solver.h"
#include "perf_counters.h"

#ifndef CARLOBOMB_REVISION
#define CARLOBOMB_REVISION "unknown"
#endif

#define SHORT_STEPS 2000u
#define MEDIUM_STEPS 100000u
#define LONG_STEPS 2000000u

#define MAX_LEN 64
#define MAX_TARGETS 40000
#define MAX_ROUNDS 64
#define MAX_BUDGETS 16
#define MIN_SAMPLE_SECONDS 0.006

enum { CL_C_SHORT, CL_C_MEDIUM, CL_C_LONG, CL_L_SHORT, CL_L_MEDIUM, CL_L_LONG, N_CLASSES };
static const char *class_name[N_CLASSES] = {
    "completed, up to 2,000 steps", "completed, 2,001 to 100,000", "completed, over 100,000",
    "cut off at 2,000 steps",       "cut off at 100,000",          "cut off at 2,000,000",
};
static const uint64_t class_limit[N_CLASSES] = {0, 0, 0, SHORT_STEPS, MEDIUM_STEPS, LONG_STEPS};
static const int class_min_targets[N_CLASSES] = {1, 1, 3, 1, 1, 3};

enum { E_C_API, E_ASM_API, E_AUTO_API, E_ASM_K, E_AUTO_K, ENGINES };

typedef struct {
    char t[MAX_LEN + 1];
    unsigned char len;
} Target;

typedef struct {
    int *idx;
    int n;
    uint64_t steps; /* steps of one pass */
    int solved;     /* targets whose search ends SOLVED */
} Pool;

typedef struct {
    int valid, targets, solved;
    uint64_t steps;
    double c_ticks, asm_vs_c, auto_vs_c, auto_vs_asm_api, auto_vs_asm_k;
    uint64_t bulk_steps, bulk_loops, fallbacks;
    int have_counts, have_perf;
    double perf[2][CB_PERF_N]; /* asm kernel, auto kernel: events per step */
} ClassResult;

static Target *targets;
static int n_targets;
static Pool pools[N_CLASSES];
static uint64_t min_pass_steps = 1500000;
static int rounds = 9;
static double sort_seconds = 10.0;
static int k_asm, k_auto, k_count = -1;
static CbPerf perf;
static int have_perf;

static int pool_full(int c) {
    return pools[c].steps >= min_pass_steps && pools[c].n >= class_min_targets[c];
}

static void pool_add(int c, int target, uint64_t steps, int solved) {
    if (pool_full(c)) return;
    pools[c].idx[pools[c].n++] = target;
    pools[c].steps += steps;
    pools[c].solved += solved;
}

/* Sort pseudo-random targets into the classes for one budget.  Each
 * candidate is searched with the smallest cap that settles its class. */
static void build_pools(int budget) {
    static const int lens[] = {8, 10, 12, 14, 16, 20, 24, 32, 48};
    unsigned long long x = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)budget * 0xD6E8FEB86659FD93ull);
    double t0 = cb_now();
    long since_progress = 0;
    n_targets = 0;
    for (int c = 0; c < N_CLASSES; c++) pools[c].n = 0, pools[c].steps = 0, pools[c].solved = 0;
    cb_amd64_kernel_select(k_asm);

    for (long cand = 0; n_targets < MAX_TARGETS; cand++) {
        int all = 1;
        for (int c = 0; c < N_CLASSES; c++) all &= pool_full(c);
        if (all || since_progress > 4000 || cb_now() - t0 > sort_seconds) break;

        Target *t = &targets[n_targets];
        int len = lens[cand % (long)(sizeof lens / sizeof *lens)];
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            t->t[i] = (char)('0' + (int)((x >> 32) & 1));
        }
        t->t[len] = '\0';
        t->len = (unsigned char)len;

        uint64_t steps;
        int before = 0, after = 0;
        for (int c = 0; c < N_CLASSES; c++) before += pools[c].n;

        int r = cb_amd64_kernel_only(t->t, (size_t)len, budget, SHORT_STEPS, &steps);
        if (r != CB_RES_LIMIT) {
            pool_add(CL_C_SHORT, n_targets, steps, r == CB_RES_SOLVED);
        } else {
            pool_add(CL_L_SHORT, n_targets, SHORT_STEPS, 0);
            if (!pool_full(CL_C_MEDIUM) || !pool_full(CL_L_MEDIUM) || !pool_full(CL_C_LONG) ||
                !pool_full(CL_L_LONG)) {
                r = cb_amd64_kernel_only(t->t, (size_t)len, budget, MEDIUM_STEPS, &steps);
                if (r != CB_RES_LIMIT) {
                    pool_add(CL_C_MEDIUM, n_targets, steps, r == CB_RES_SOLVED);
                } else {
                    pool_add(CL_L_MEDIUM, n_targets, MEDIUM_STEPS, 0);
                    if (!pool_full(CL_C_LONG) || !pool_full(CL_L_LONG)) {
                        r = cb_amd64_kernel_only(t->t, (size_t)len, budget, LONG_STEPS, &steps);
                        if (r != CB_RES_LIMIT) pool_add(CL_C_LONG, n_targets, steps, r == CB_RES_SOLVED);
                        else pool_add(CL_L_LONG, n_targets, LONG_STEPS, 0);
                    }
                }
            }
        }
        for (int c = 0; c < N_CLASSES; c++) after += pools[c].n;
        if (after > before) { n_targets++; since_progress = 0; }
        else since_progress++;
    }
}

/* One pass of a class on one engine.  Returns total steps; *sig folds every
 * result code and step count. */
static uint64_t pass(int engine, int cls, int budget, uint64_t *sig) {
    const Pool *p = &pools[cls];
    uint64_t limit = class_limit[cls], total = 0, s = 0;
    RevLevelOptions opt = {limit};
    for (int i = 0; i < p->n; i++) {
        const Target *t = &targets[p->idx[i]];
        uint64_t steps;
        int r;
        if (engine == E_C_API) {
            RevGraph g;
            RevLevelStats st;
            r = (int)rev_level_solve_ex(t->t, t->len, budget, &opt, &g, &st);
            steps = st.steps;
        } else if (engine == E_ASM_API || engine == E_AUTO_API) {
            RevGraph g;
            RevLevelStats st;
            r = (int)rev_level_solve_amd64_ex(t->t, t->len, budget, &opt, &g, &st);
            steps = st.steps;
        } else {
            r = cb_amd64_kernel_only(t->t, t->len, budget, limit, &steps);
        }
        total += steps;
        s = s * 1000003u + (uint64_t)r + steps;
    }
    *sig = s;
    return total;
}

static void select_engine(int engine) {
    if (engine == E_ASM_API || engine == E_ASM_K) cb_amd64_kernel_select(k_asm);
    else if (engine == E_AUTO_API || engine == E_AUTO_K) cb_amd64_kernel_select(k_auto);
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static int measure_class(int cls, int budget, ClassResult *out) {
    const Pool *p = &pools[cls];
    volatile uint64_t sink = 0;
    memset(out, 0, sizeof *out);
    if (p->n < class_min_targets[cls] || p->steps < min_pass_steps / 4) return 1; /* class absent */

    uint64_t sig0 = 0, steps0 = 0;
    int ok = 1;
    for (int e = 0; e < ENGINES; e++) {
        uint64_t sig, steps;
        select_engine(e);
        steps = pass(e, cls, budget, &sig);
        if (e == 0) { sig0 = sig; steps0 = steps; }
        else if (sig != sig0 || steps != steps0) {
            fprintf(stderr, "DISAGREEMENT: budget %d, class \"%s\", engine %d\n", budget,
                    class_name[cls], e);
            ok = 0;
        }
    }

    long reps = 1;
    select_engine(E_ASM_K);
    for (;;) {
        uint64_t sig;
        double t0 = cb_now();
        for (long i = 0; i < reps; i++) sink += pass(E_ASM_K, cls, budget, &sig);
        if (cb_now() - t0 >= MIN_SAMPLE_SECONDS || reps >= (1L << 24)) break;
        reps *= 2;
    }

    static double ticks[ENGINES][MAX_ROUNDS], ratio[4][MAX_ROUNDS];
    for (int r = 0; r < rounds; r++) {
        for (int j = 0; j < ENGINES; j++) {
            int e = (j + r) % ENGINES;
            uint64_t sig;
            select_engine(e);
            sink += pass(e, cls, budget, &sig);
            unsigned long long c0 = __rdtsc();
            for (long i = 0; i < reps; i++) sink += pass(e, cls, budget, &sig);
            unsigned long long c1 = __rdtsc();
            ticks[e][r] = (double)(c1 - c0) / ((double)reps * (double)steps0);
        }
        ratio[0][r] = ticks[E_C_API][r] / ticks[E_ASM_API][r];
        ratio[1][r] = ticks[E_C_API][r] / ticks[E_AUTO_API][r];
        ratio[2][r] = ticks[E_ASM_API][r] / ticks[E_AUTO_API][r];
        ratio[3][r] = ticks[E_ASM_K][r] / ticks[E_AUTO_K][r];
    }
    out->valid = 1;
    out->targets = p->n;
    out->solved = p->solved;
    out->steps = steps0;
    out->c_ticks = median(ticks[E_C_API], rounds);
    out->asm_vs_c = median(ratio[0], rounds);
    out->auto_vs_c = median(ratio[1], rounds);
    out->auto_vs_asm_api = median(ratio[2], rounds);
    out->auto_vs_asm_k = median(ratio[3], rounds);

    /* search accounting: one pass of the counting twin of "auto" */
    if (k_count >= 0 && cb_amd64_kernel_select(k_count) == 0) {
        uint64_t sig;
        cb_amd64_counters_reset();
        uint64_t steps = pass(E_ASM_K, cls, budget, &sig); /* kernel only, selected = counting */
        if (steps != steps0 || sig != sig0) {
            fprintf(stderr, "DISAGREEMENT: counting kernel, budget %d, class \"%s\"\n", budget,
                    class_name[cls]);
            ok = 0;
        }
        const CbAsmCounters *cnt = cb_amd64_counters();
        out->bulk_steps = cnt->bulk_steps;
        out->bulk_loops = cnt->bulk_loops;
        out->fallbacks = cnt->fallback_steps + cnt->fallback_stack + cnt->fallback_uncolored;
        out->have_counts = 1;
    }

    /* hardware counters, where there are any */
    if (have_perf) {
        out->have_perf = 1;
        for (int k = 0; k < 2; k++) {
            int e = k == 0 ? E_ASM_K : E_AUTO_K;
            uint64_t sig, v[CB_PERF_N] = {0};
            select_engine(e);
            sink += pass(e, cls, budget, &sig);
            cb_perf_start(&perf);
            for (long i = 0; i < reps; i++) sink += pass(e, cls, budget, &sig);
            if (!cb_perf_stop(&perf, v)) out->have_perf = 0;
            for (int i = 0; i < CB_PERF_N; i++)
                out->perf[k][i] = (double)v[i] / ((double)reps * (double)steps0);
        }
    }
    (void)sink;
    return ok;
}

static int parse_ints(const char *s, int *out, int cap) {
    int n = 0;
    while (*s && n < cap) {
        out[n++] = atoi(s);
        s = strchr(s, ',');
        if (!s) break;
        s++;
    }
    return n;
}

static void cpu_name(char *buf, size_t cap) {
    snprintf(buf, cap, "unknown");
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "model name", 10) == 0) {
            char *p = strchr(line, ':');
            if (p) { p[strcspn(p, "\n")] = '\0'; snprintf(buf, cap, "%s", p + 2); }
            break;
        }
    fclose(f);
}

/* The perf code path, exercised with software events on machines that have
 * no hardware counters. */
static int perf_selftest(void) {
    CbPerf p;
    uint64_t v[CB_PERF_N] = {0};
    volatile double x = 0;
    if (!cb_perf_open_software(&p)) {
        /* Not a failure of this program: some systems forbid perf_event_open
         * altogether.  There is then nothing to check. */
        printf("perf self-test: skipped, this system does not allow performance events (%s)\n",
               cb_perf_error(&p));
        return 0;
    }
    cb_perf_start(&p);
    double t0 = cb_now();
    while (cb_now() - t0 < 0.05) x += 1.0;
    int exact = cb_perf_stop(&p, v);
    double ms = (double)v[0] / 1e6;
    printf("perf self-test: task clock %.1f ms for a 50 ms busy loop, group read %s\n", ms,
           exact ? "exact" : "NOT exact");
    return !(exact && ms > 30.0 && ms < 80.0);
}

int main(int argc, char **argv) {
    int budgets[MAX_BUDGETS] = {3, 4, 5, 6, 7, 8}, n_budgets = 6;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--perf-selftest")) return perf_selftest();
        if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", argv[i]); return 2; }
        if (!strcmp(argv[i], "-n")) n_budgets = parse_ints(argv[++i], budgets, MAX_BUDGETS);
        else if (!strcmp(argv[i], "-r")) rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s")) min_pass_steps = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-t")) sort_seconds = atof(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (rounds < 1 || rounds > MAX_ROUNDS) return 2;

    targets = malloc(sizeof(Target) * MAX_TARGETS);
    for (int c = 0; c < N_CLASSES; c++) pools[c].idx = malloc(sizeof(int) * MAX_TARGETS);
    if (!targets) return 2;

    k_asm = cb_amd64_kernel_find("asm");
    k_auto = cb_amd64_kernel_find("auto");
    have_perf = cb_perf_open(&perf);

    cpu_set_t set;
    int cpu = sched_getcpu();
    if (cpu >= 0) {
        CPU_ZERO(&set);
        CPU_SET((size_t)cpu, &set);
        (void)sched_setaffinity(0, sizeof set, &set);
    }

    char name[200];
    unsigned isa = cb_amd64_isa();
    cpu_name(name, sizeof name);
    printf("CarloBomb workload characterization\n");
    printf("revision:   %s\n", CARLOBOMB_REVISION);
    printf("processor:  %s\n", name);
    printf("ISA found:  BMI1/BMI2 %s, AVX-512 F/BW/VL %s\n", (isa & CB_ISA_BMI2) ? "yes" : "no",
           (isa & CB_ISA_AVX512) ? "yes" : "no");
    if (have_perf) printf("counters:   hardware cycles, instructions, branches, branch misses\n");
    else printf("counters:   no hardware counters on this machine (perf_event_open: %s)\n",
                cb_perf_error(&perf));
    printf("method:     %d rounds, round-robin; at least %llu steps per pass; ticks = time-stamp "
           "counter\n\n",
           rounds, (unsigned long long)min_pass_steps);

    static ClassResult res[MAX_BUDGETS][N_CLASSES];
    int bad = 0;
    for (int bi = 0; bi < n_budgets; bi++) {
        int budget = budgets[bi];
        if (budget < 1 || budget > REV_LEVEL_MAX_NODES) return 2;
        build_pools(budget);

        cb_amd64_kernel_select(k_auto);
        const char *auto_name = cb_amd64_kernel_info(cb_amd64_kernel_used(budget))->name;
        char count_name[64];
        snprintf(count_name, sizeof count_name, "%s-count", auto_name);
        k_count = cb_amd64_kernel_find(count_name);

        printf("### Node budget %d  (auto runs %s)\n\n", budget, auto_name);
        printf("| class | targets | steps per pass | solved | C ticks/step | asm vs C | auto vs C "
               "| auto vs asm, API | auto vs asm, kernel | executed | accounted in bulk |\n");
        printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
        for (int c = 0; c < N_CLASSES; c++) {
            ClassResult *r = &res[bi][c];
            if (!measure_class(c, budget, r)) bad = 1;
            if (!r->valid) continue;
            printf("| %s | %d | %llu |", class_name[c], r->targets, (unsigned long long)r->steps);
            if (class_limit[c]) printf(" - |");
            else printf(" %.0f%% |", 100.0 * r->solved / r->targets);
            printf(" %.2f | %.2f | %.2f | %.2f | %.2f |", r->c_ticks, r->asm_vs_c, r->auto_vs_c,
                   r->auto_vs_asm_api, r->auto_vs_asm_k);
            if (r->have_counts)
                printf(" %.1f%% | %.1f%% |\n",
                       100.0 * (double)(r->steps - r->bulk_steps) / (double)r->steps,
                       100.0 * (double)r->bulk_steps / (double)r->steps);
            else printf(" - | - |\n");
            fflush(stdout);
        }
        if (have_perf) {
            printf("\n| class | kernel | cycles/step | instructions/step | branches/step "
                   "| branch misses/step | miss rate | cycles per branch miss |\n");
            printf("|---|---|---|---|---|---|---|---|\n");
            for (int c = 0; c < N_CLASSES; c++) {
                const ClassResult *r = &res[bi][c];
                if (!r->valid || !r->have_perf) continue;
                for (int k = 0; k < 2; k++) {
                    const double *v = r->perf[k];
                    printf("| %s | %s | %.2f | %.2f | %.2f | %.3f | %.1f%% | %.0f |\n",
                           class_name[c], k == 0 ? "asm" : auto_name, v[CB_PERF_CYCLES],
                           v[CB_PERF_INSTRUCTIONS], v[CB_PERF_BRANCHES], v[CB_PERF_BRANCH_MISSES],
                           100.0 * v[CB_PERF_BRANCH_MISSES] / v[CB_PERF_BRANCHES],
                           v[CB_PERF_CYCLES] / v[CB_PERF_BRANCH_MISSES]);
                }
            }
        }
        printf("\n");
    }

    /* Summaries: one row per class, one column per budget. */
    static const struct { const char *title; int which; } tables[] = {
        {"auto vs asm, kernel only", 0}, {"auto vs asm, through the API", 1},
        {"auto vs the C solver, through the API", 2}, {"asm vs the C solver, through the API", 3},
        {"share of logical steps accounted for in bulk by auto", 4},
        {"CTRL link loops that fell back to per-node code, per 1,000 started in bulk mode", 5},
    };
    for (size_t t = 0; t < sizeof tables / sizeof *tables; t++) {
        printf("### Summary: %s\n\n| class |", tables[t].title);
        for (int bi = 0; bi < n_budgets; bi++) printf(" N = %d |", budgets[bi]);
        printf("\n|---|");
        for (int bi = 0; bi < n_budgets; bi++) printf("---|");
        printf("\n");
        for (int c = 0; c < N_CLASSES; c++) {
            printf("| %s |", class_name[c]);
            for (int bi = 0; bi < n_budgets; bi++) {
                const ClassResult *r = &res[bi][c];
                if (!r->valid) { printf(" |"); continue; }
                switch (tables[t].which) {
                case 0: printf(" %.2f |", r->auto_vs_asm_k); break;
                case 1: printf(" %.2f |", r->auto_vs_asm_api); break;
                case 2: printf(" %.2f |", r->auto_vs_c); break;
                case 3: printf(" %.2f |", r->asm_vs_c); break;
                case 4:
                    if (r->have_counts)
                        printf(" %.1f%% |", 100.0 * (double)r->bulk_steps / (double)r->steps);
                    else printf(" - |");
                    break;
                default:
                    if (r->have_counts && r->bulk_loops)
                        printf(" %.1f |", 1000.0 * (double)r->fallbacks / (double)r->bulk_loops);
                    else printf(" - |");
                }
            }
            printf("\n");
        }
        printf("\n");
    }
    cb_amd64_kernel_select(0);
    return bad;
}
