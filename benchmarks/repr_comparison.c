/*
 * Benchmark: AMD64 search kernels against each other, by node budget and
 * target length.
 *
 * A cell is one (node budget N, target length L).  Its workload is a fixed
 * set of pseudo-random targets of length L, each searched at budget N with a
 * step cap, so that every kernel performs exactly the same steps (checked:
 * result code and step count must agree for every target).  The set is made
 * large enough that one pass is at least MIN_PASS_STEPS steps: a short
 * workload repeated many times is memorized by the branch predictor and
 * runs far faster than the same searches would in real use.  Only the
 * assembly kernel is timed, through cb_amd64_kernel_only; there is no
 * validation, export to RevGraph or verification inside the timed region.
 *
 * Timing is round-robin: in each round every kernel runs the cell's
 * workload once (repeated until it lasts long enough to time), in rotating
 * order.  A kernel's speedup is the median over rounds of
 * (reference time / kernel time) taken within the same round, which cancels
 * the slow drift of a shared machine.  The reference is kernel 0, the
 * unmodified kernel of search_sysv.S.
 *
 * Reported per cell: steps in one pass, the reference kernel's time-stamp
 * ticks per step (median), and each kernel's speedup.  Then geometric means
 * per budget and overall.
 *
 * Usage: repr_comparison [-k name,name,...] [-n N,N,...] [-l L,L,...]
 *                        [-r rounds] [-c step_cap] [-s min_pass_steps]
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

#ifndef CARLOBOMB_REVISION
#define CARLOBOMB_REVISION "unknown"
#endif

#define MAX_KERNELS 32
#define MAX_LIST 16
#define MAX_TARGETS 8192
#define MAX_ROUNDS 64
#define MAX_LEN 128
#define MIN_SAMPLE_SECONDS 0.006

static int kernel_ids[MAX_KERNELS], n_kernels;
static int budgets[MAX_LIST] = {3, 4, 5, 6, 7, 8}, n_budgets = 6;
static int lengths[MAX_LIST] = {12, 16, 24, 32, 48}, n_lengths = 5;
static int rounds = 15, n_targets;
static uint64_t step_cap = 200000, min_pass_steps = 1500000;

static char targets[MAX_TARGETS][MAX_LEN + 1];

/* Fill the target set for a cell: add pseudo-random targets until one pass
 * of the reference kernel is at least min_pass_steps steps. */
static void make_targets(int len, int budget) {
    unsigned long long x = 0x9E3779B97F4A7C15ull ^ ((unsigned long long)len * 0xBF58476D1CE4E5B9ull);
    uint64_t total = 0;
    cb_amd64_kernel_select(0);
    for (n_targets = 0; n_targets < MAX_TARGETS && total < min_pass_steps; n_targets++) {
        uint64_t steps;
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            targets[n_targets][i] = (char)('0' + (int)((x >> 32) & 1));
        }
        targets[n_targets][len] = '\0';
        cb_amd64_kernel_only(targets[n_targets], (size_t)len, budget, step_cap, &steps);
        total += steps;
    }
}

/* One pass of the cell's workload on the selected kernel.  Returns the total
 * step count and folds every result code into *sig. */
static uint64_t pass(int len, int budget, uint64_t *sig) {
    uint64_t total = 0, s = 0;
    for (int t = 0; t < n_targets; t++) {
        uint64_t steps;
        int r = cb_amd64_kernel_only(targets[t], (size_t)len, budget, step_cap, &steps);
        total += steps;
        s = s * 1000003u + (uint64_t)r + steps;
    }
    *sig = s;
    return total;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
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

int main(int argc, char **argv) {
    const char *klist = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-k")) klist = argv[i + 1];
        else if (!strcmp(argv[i], "-n")) n_budgets = parse_ints(argv[i + 1], budgets, MAX_LIST);
        else if (!strcmp(argv[i], "-l")) n_lengths = parse_ints(argv[i + 1], lengths, MAX_LIST);
        else if (!strcmp(argv[i], "-r")) rounds = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-c")) step_cap = strtoull(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "-s")) min_pass_steps = strtoull(argv[i + 1], NULL, 10);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (rounds < 1 || rounds > MAX_ROUNDS) return 2;

    /* kernel 0 is always the reference; then the requested or all others */
    kernel_ids[n_kernels++] = 0;
    for (int k = 1; k < cb_amd64_kernel_count() && n_kernels < MAX_KERNELS; k++) {
        const char *name = cb_amd64_kernel_info(k)->name;
        if (klist) {
            const char *p = strstr(klist, name);
            size_t len = strlen(name);
            int hit = 0;
            for (; p; p = strstr(p + 1, name))
                if ((p == klist || p[-1] == ',') && (p[len] == ',' || p[len] == '\0')) hit = 1;
            if (!hit) continue;
        } else {
            /* the "-count" kernels are benchmark-only twins; compare them only on request */
            size_t len = strlen(name);
            if (len > 6 && strcmp(name + len - 6, "-count") == 0) continue;
        }
        if (!cb_amd64_kernel_available(k)) {
            fprintf(stderr, "kernel %s not available on this CPU, skipped\n", name);
            continue;
        }
        kernel_ids[n_kernels++] = k;
    }

    /* stay on one CPU if we are allowed to */
    cpu_set_t set;
    int cpu = sched_getcpu();
    if (cpu >= 0) {
        CPU_ZERO(&set);
        CPU_SET((size_t)cpu, &set);
        (void)sched_setaffinity(0, sizeof set, &set);
    }

    printf("CarloBomb AMD64 kernel comparison by node budget and target length\n");
    printf("revision:  %s\n", CARLOBOMB_REVISION);
    printf("workload:  pseudo-random targets, step cap %llu per search, at least %llu steps per "
           "pass (up to %d targets)\n",
           (unsigned long long)step_cap, (unsigned long long)min_pass_steps, MAX_TARGETS);
    printf("timing:    kernel only; %d rounds, round-robin; speedup = median of per-round "
           "(reference time / kernel time)\n",
           rounds);
    printf("reference: kernel \"%s\"; ref = its time-stamp ticks per search step (median)\n\n",
           cb_amd64_kernel_info(0)->name);

    printf("| N | L | targets | steps/pass | ref |");
    for (int k = 1; k < n_kernels; k++) printf(" %s |", cb_amd64_kernel_info(kernel_ids[k])->name);
    printf("\n|---|---|---|---|---|");
    for (int k = 1; k < n_kernels; k++) printf("---|");
    printf("\n");

    static double logsum[MAX_LIST][MAX_KERNELS], logall[MAX_KERNELS];
    int bad = 0;
    volatile uint64_t sink = 0;

    for (int bi = 0; bi < n_budgets; bi++) {
        for (int li = 0; li < n_lengths; li++) {
            int budget = budgets[bi], len = lengths[li];
            if (len > MAX_LEN || budget < 1 || budget > REV_LEVEL_MAX_NODES) return 2;
            make_targets(len, budget);

            /* agreement and steps per pass */
            uint64_t sig0 = 0, steps0 = 0;
            for (int k = 0; k < n_kernels; k++) {
                uint64_t sig, steps;
                cb_amd64_kernel_select(kernel_ids[k]);
                steps = pass(len, budget, &sig);
                if (k == 0) { sig0 = sig; steps0 = steps; }
                else if (sig != sig0 || steps != steps0) {
                    fprintf(stderr, "DISAGREEMENT: kernel %s, N=%d L=%d\n",
                            cb_amd64_kernel_info(kernel_ids[k])->name, budget, len);
                    bad = 1;
                }
            }

            /* repetitions per sample, calibrated on the reference */
            long reps = 1;
            cb_amd64_kernel_select(0);
            for (;;) {
                uint64_t sig;
                double t0 = cb_now();
                for (long i = 0; i < reps; i++) sink += pass(len, budget, &sig);
                if (cb_now() - t0 >= MIN_SAMPLE_SECONDS || reps >= (1L << 24)) break;
                reps *= 2;
            }

            static double ticks[MAX_KERNELS][MAX_ROUNDS], ratio[MAX_KERNELS][MAX_ROUNDS];
            for (int r = 0; r < rounds; r++) {
                for (int j = 0; j < n_kernels; j++) {
                    int k = (j + r) % n_kernels; /* rotate the order */
                    uint64_t sig;
                    cb_amd64_kernel_select(kernel_ids[k]);
                    sink += pass(len, budget, &sig); /* warm this kernel's code */
                    unsigned long long c0 = __rdtsc();
                    for (long i = 0; i < reps; i++) sink += pass(len, budget, &sig);
                    unsigned long long c1 = __rdtsc();
                    ticks[k][r] = (double)(c1 - c0) / ((double)reps * (double)steps0);
                }
                for (int k = 0; k < n_kernels; k++) ratio[k][r] = ticks[0][r] / ticks[k][r];
            }

            printf("| %d | %d | %d | %llu | %.2f |", budget, len, n_targets,
                   (unsigned long long)steps0, median(ticks[0], rounds));
            for (int k = 1; k < n_kernels; k++) {
                double sp = median(ratio[k], rounds);
                printf(" %.2f |", sp);
                logsum[bi][k] += log(sp);
                logall[k] += log(sp);
            }
            printf("\n");
            fflush(stdout);
        }
    }

    printf("\ngeometric mean speedup over target lengths, by node budget\n\n| N |");
    for (int k = 1; k < n_kernels; k++) printf(" %s |", cb_amd64_kernel_info(kernel_ids[k])->name);
    printf("\n|---|");
    for (int k = 1; k < n_kernels; k++) printf("---|");
    printf("\n");
    for (int bi = 0; bi < n_budgets; bi++) {
        printf("| %d |", budgets[bi]);
        for (int k = 1; k < n_kernels; k++) printf(" %.2f |", exp(logsum[bi][k] / n_lengths));
        printf("\n");
    }
    printf("| all |");
    for (int k = 1; k < n_kernels; k++) printf(" %.2f |", exp(logall[k] / (n_budgets * n_lengths)));
    printf("\n");

    cb_amd64_kernel_select(0);
    (void)sink;
    return bad;
}
