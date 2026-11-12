/*
 * Pilot for building FixedRunBombs beyond 16 bits: what does it cost?
 *
 *   ladder_pilot [--bits L[,L...]] [--samples N] [--workers W] [--seed S]
 *                [--engine c|amd64-auto] [--max-steps K] [--no-structured]
 *
 * The construction it prices is the ladder: a complete Bomb for L-1 bits is
 * extended to L bits.  For a string p of L-1 bits with minimum m(p), the
 * stored REV goes on to emit one more bit, so one of the two extensions of p
 * is settled at once with m(p) nodes.  The other one, the sibling, needs at
 * least m(p) nodes because it starts with p, so it is searched from node
 * budget m(p) upward.  The cost of a level is the cost of its siblings.
 *
 * For each length L the pilot draws random strings p of L-1 bits (first bit
 * 0: one of each complement pair), solves p from one node up as the direct
 * method would (which also yields m(p) and a REV for p), then searches the
 * sibling as the ladder would.  It reports the cost of both, by node budget
 * and as a distribution, split into the time to find the REV and the time to
 * prove the smaller budgets empty, and projects the cost of the level and of
 * the whole ladder up to it.  With --workers W the sampling runs in W
 * processes at once and the aggregate throughput is what the projection
 * uses, so that it can be measured for 6, 10 or 16 processes on the machine
 * that will do the work.
 *
 * Random strings do not show the hardest cases, so a set of structured
 * strings of each length is solved as well (single runs and near-runs,
 * periodic strings with a late change, growing gaps, Thue-Morse, Fibonacci),
 * and the projection's upper end makes an allowance for a tail the sample
 * could have missed.
 *
 * Samples are drawn in growing stages (N/8, N/4, N/2, N) with the estimate
 * printed after each, so a run can be stopped as soon as the answer is
 * clear.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "minimal_solver.h"
#ifdef CARLOBOMB_HAVE_AMD64_ASM
#include "asm_solver.h"
#endif

enum { MAXN = 33 }; /* node budgets tracked */

typedef struct {
    int bits;            /* L */
    int m_prefix;        /* minimum of the (L-1)-bit string */
    int m_sibling;       /* minimum of the sibling; 0 if a limit stopped the search */
    uint64_t direct;     /* steps: the prefix solved from one node up */
    uint64_t find;       /* steps: the search that found the sibling's REV */
    uint64_t certify;    /* steps: the sibling's searches that found nothing */
    double direct_s, find_s, certify_s;
    uint64_t steps_at[MAXN]; /* sibling searches, by node budget */
    double secs_at[MAXN];
    uint32_t unsat_at[MAXN], sat_at[MAXN];
} Sample;

static RevLevelSolveFn engine = rev_level_solve_ex;
static const char *engine_name = "c";
static uint64_t max_steps = 20000000000ull;

static uint64_t rng(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* One random string of bits-1 bits and its sibling extension. */
static void run_sample(int bits, uint64_t *seed, Sample *s) {
    RevLevelOptions opt = {max_steps};
    char target[80];
    RevLevelStats st;
    RevGraph g, w;
    RevMachine m;
    int next = 0;
    memset(s, 0, sizeof *s);
    s->bits = bits;
    target[0] = '0';
    for (int i = 1; i < bits - 1; i++) target[i] = (char)('0' + (rng(seed) >> 33 & 1));
    target[bits - 1] = '\0';

    for (int n = 1; n < MAXN; n++) { /* the prefix, from one node up */
        double t0 = cb_now();
        RevSolveResult r = engine(target, (size_t)(bits - 1), n, &opt, &w, &st);
        s->direct += st.steps;
        s->direct_s += cb_now() - t0;
        if (r == REV_SOLVE_SOLVED) {
            s->m_prefix = n;
            break;
        }
        if (r != REV_SOLVE_PROVEN_UNSAT) return; /* limit: give up on this sample */
    }
    if (!s->m_prefix) return;
    rev_machine_reset(&m);
    for (int i = 0; i < bits; i++) next = rev_machine_next_bit(&w, &m);
    target[bits - 1] = (char)('0' + (1 - next)); /* the extension the stored REV does not emit */
    target[bits] = '\0';

    for (int n = s->m_prefix; n < MAXN; n++) { /* the sibling, from m(p) up */
        double t0 = cb_now(), dt;
        RevSolveResult r = engine(target, (size_t)bits, n, &opt, &g, &st);
        dt = cb_now() - t0;
        s->steps_at[n] += st.steps;
        s->secs_at[n] += dt;
        if (r == REV_SOLVE_SOLVED) {
            s->sat_at[n]++;
            s->find = st.steps;
            s->find_s = dt;
            s->m_sibling = n;
            return;
        }
        if (r != REV_SOLVE_PROVEN_UNSAT) return;
        s->unsat_at[n]++;
        s->certify += st.steps;
        s->certify_s += dt;
    }
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t pick(const uint64_t *sorted, size_t n, double q) {
    return sorted[(size_t)((double)(n - 1) * q)];
}

/* Draw `count` samples in `workers` processes; returns wall-clock seconds. */
static double draw(int bits, size_t count, int workers, uint64_t seed, Sample *out) {
    double t0 = cb_now();
    if (workers <= 1) {
        for (size_t i = 0; i < count; i++) run_sample(bits, &seed, &out[i]);
        return cb_now() - t0;
    }
    int (*fds)[2] = malloc((size_t)workers * sizeof *fds);
    pid_t *pids = malloc((size_t)workers * sizeof *pids);
    size_t done = 0;
    fflush(NULL);
    for (int k = 0; k < workers; k++) {
        size_t mine = count / (size_t)workers + ((size_t)k < count % (size_t)workers);
        if (pipe(fds[k]) != 0 || (pids[k] = fork()) < 0) {
            perror("ladder_pilot");
            exit(1);
        }
        if (pids[k] == 0) {
            uint64_t s = seed + (uint64_t)0x1000003 * ((uint64_t)k + 1u);
            close(fds[k][0]);
            for (size_t i = 0; i < mine; i++) {
                Sample one;
                run_sample(bits, &s, &one);
                if (write(fds[k][1], &one, sizeof one) != (ssize_t)sizeof one) _exit(1);
            }
            _exit(0);
        }
        close(fds[k][1]);
    }
    { /* take samples from whichever worker has one, so that none waits on a full pipe */
        struct pollfd *pf = malloc((size_t)workers * sizeof *pf);
        size_t *have = calloc((size_t)workers, sizeof *have);
        Sample *part = malloc((size_t)workers * sizeof *part);
        int open_pipes = workers;
        for (int k = 0; k < workers; k++) {
            pf[k].fd = fds[k][0];
            pf[k].events = POLLIN;
        }
        while (open_pipes > 0) {
            if (poll(pf, (nfds_t)workers, -1) < 0) continue;
            for (int k = 0; k < workers; k++) {
                ssize_t got;
                if (pf[k].fd < 0 || !(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                got = read(pf[k].fd, (char *)&part[k] + have[k], sizeof(Sample) - have[k]);
                if (got <= 0) {
                    close(pf[k].fd);
                    pf[k].fd = -1;
                    open_pipes--;
                    continue;
                }
                have[k] += (size_t)got;
                if (have[k] == sizeof(Sample)) {
                    if (done < count) out[done++] = part[k];
                    have[k] = 0;
                }
            }
        }
        for (int k = 0; k < workers; k++) waitpid(pids[k], NULL, 0);
        free(pf);
        free(have);
        free(part);
    }
    free(fds);
    free(pids);
    if (done != count) {
        fprintf(stderr, "ladder_pilot: a worker failed\n");
        exit(1);
    }
    return cb_now() - t0;
}

typedef struct {
    double mean_sibling, se_sibling; /* steps per sibling job, and its standard error */
    double mean_direct;              /* steps to solve an (L-1)-bit string from one node */
    double steps_per_second;         /* aggregate, all workers */
    double tail_steps;               /* allowance for hard targets the sample missed, per job */
    int max_nodes;
} LevelEstimate;

static void report(int bits, const Sample *s, size_t n, double wall, int workers, int final,
                   LevelEstimate *est) {
    uint64_t *sib = malloc(n * sizeof *sib), *dir = malloc(n * sizeof *dir);
    uint64_t steps_at[MAXN] = {0}, total = 0, find = 0, certify = 0, direct = 0, all_steps = 0;
    double secs_at[MAXN] = {0}, find_s = 0, certify_s = 0, direct_s = 0, sum = 0, sumsq = 0, mean, sd;
    uint32_t unsat_at[MAXN] = {0}, sat_at[MAXN] = {0}, jump[4] = {0}, nodes[MAXN] = {0}, limited = 0;
    int max_nodes = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t c = s[i].find + s[i].certify;
        sib[i] = c;
        dir[i] = s[i].direct;
        total += c;
        find += s[i].find;
        certify += s[i].certify;
        direct += s[i].direct;
        find_s += s[i].find_s;
        certify_s += s[i].certify_s;
        direct_s += s[i].direct_s;
        sum += (double)c;
        sumsq += (double)c * (double)c;
        if (!s[i].m_sibling) {
            limited++;
            continue;
        }
        jump[s[i].m_sibling - s[i].m_prefix > 3 ? 3 : s[i].m_sibling - s[i].m_prefix]++;
        nodes[s[i].m_sibling]++;
        if (s[i].m_sibling > max_nodes) max_nodes = s[i].m_sibling;
        for (int b = 1; b < MAXN; b++) {
            steps_at[b] += s[i].steps_at[b];
            secs_at[b] += s[i].secs_at[b];
            unsat_at[b] += s[i].unsat_at[b];
            sat_at[b] += s[i].sat_at[b];
        }
    }
    all_steps = total + direct;
    mean = sum / (double)n;
    sd = n > 1 ? sqrt((sumsq - sum * sum / (double)n) / (double)(n - 1)) : 0;
    qsort(sib, n, sizeof *sib, cmp_u64);
    qsort(dir, n, sizeof *dir, cmp_u64);
    est->mean_sibling = mean;
    est->se_sibling = sd / sqrt((double)n);
    est->mean_direct = (double)direct / (double)n;
    est->steps_per_second = (double)all_steps / wall;
    est->max_nodes = max_nodes;

    printf("\n%d bits: %zu random strings of %d bits and their siblings, %d process%s, %.1f s, "
           "%.3g steps/s in all\n",
           bits, n, bits - 1, workers, workers == 1 ? "" : "es", wall, est->steps_per_second);
    printf("  sibling search (the ladder's cost per job), steps:\n");
    printf("    mean %.4g +- %.2g   median %.4g   p90 %.4g   p99 %.4g   max %.4g\n", mean,
           est->se_sibling, (double)pick(sib, n, 0.5), (double)pick(sib, n, 0.9),
           (double)pick(sib, n, 0.99), (double)sib[n - 1]);
    printf("    finding the REV %.0f%% of the steps, proving smaller budgets empty %.0f%%\n",
           100.0 * (double)find / (double)(total ? total : 1),
           100.0 * (double)certify / (double)(total ? total : 1));
    printf("    time per job: %.3g ms to find, %.3g ms to certify (this machine, %s)\n",
           1e3 * find_s / (double)n, 1e3 * certify_s / (double)n, engine_name);
    printf("  sibling needs as many nodes as its prefix: %u, one more: %u, two more: %u, more: %u",
           jump[0], jump[1], jump[2], jump[3]);
    if (limited) printf(", stopped by the step limit: %u", limited);
    printf("\n  minimum of the sibling:");
    for (int b = 1; b < MAXN; b++)
        if (nodes[b]) printf("  %d nodes: %u", b, nodes[b]);
    printf("\n");
    if (final) {
        printf("  by node budget:  budget   searches   unsat     sat          steps      seconds   steps/s\n");
        for (int b = 1; b < MAXN; b++)
            if (unsat_at[b] + sat_at[b])
                printf("                   %6d   %8u  %6u  %6u   %12.4g   %10.3f   %.3g\n", b,
                       unsat_at[b] + sat_at[b], unsat_at[b], sat_at[b], (double)steps_at[b], secs_at[b],
                       secs_at[b] > 0 ? (double)steps_at[b] / secs_at[b] : 0.0);
        printf("  direct method on the %d-bit strings (from one node up), steps: mean %.4g   median %.4g   "
               "p99 %.4g   max %.4g   (%.3g ms)\n",
               bits - 1, est->mean_direct, (double)pick(dir, n, 0.5), (double)pick(dir, n, 0.99),
               (double)dir[n - 1], 1e3 * direct_s / (double)n);
    }
    free(sib);
    free(dir);
}

/* ---- structured strings -------------------------------------------------- */

static void fill_periodic(char *t, int bits, const char *unit) {
    size_t u = strlen(unit);
    for (int i = 0; i < bits; i++) t[i] = unit[(size_t)i % u];
    t[bits] = '\0';
}

static void flip(char *t, int at) { t[at] = t[at] == '0' ? '1' : '0'; }

static int structured(int bits, char names[][40], char targets[][80]) {
    int n = 0;
    char *t;
#define NEXT(label) (snprintf(names[n], 40, "%s", label), t = targets[n++])
    NEXT("00...01"); memset(t, '0', (size_t)bits); t[bits] = '\0'; t[bits - 1] = '1';
    NEXT("00...011"); memset(t, '0', (size_t)bits); t[bits] = '\0'; t[bits - 1] = t[bits - 2] = '1';
    NEXT("00...010"); memset(t, '0', (size_t)bits); t[bits] = '\0'; t[bits - 2] = '1';
    NEXT("one 1 in the middle"); memset(t, '0', (size_t)bits); t[bits] = '\0'; t[bits / 2] = '1';
    NEXT("0..01..1 halves"); memset(t, '0', (size_t)bits); t[bits] = '\0'; memset(t + bits / 2, '1', (size_t)(bits - bits / 2));
    NEXT("halves, last bit flipped"); memset(t, '0', (size_t)bits); t[bits] = '\0'; memset(t + bits / 2, '1', (size_t)(bits - bits / 2)); flip(t, bits - 1);
    NEXT("thirds 0..01..10..0"); memset(t, '0', (size_t)bits); t[bits] = '\0'; memset(t + bits / 3, '1', (size_t)(bits / 3));
    NEXT("0101.. last flipped"); fill_periodic(t, bits, "01"); flip(t, bits - 1);
    NEXT("0011.. last flipped"); fill_periodic(t, bits, "0011"); flip(t, bits - 1);
    NEXT("001.. last flipped"); fill_periodic(t, bits, "001"); flip(t, bits - 1);
    NEXT("00010111.. last flipped"); fill_periodic(t, bits, "00010111"); flip(t, bits - 1);
    NEXT("0101.. middle flipped"); fill_periodic(t, bits, "01"); flip(t, bits / 2);
    NEXT("growing gaps 0100100010.."); { int i = 0, gap = 1; while (i < bits) { for (int k = 0; k < gap && i < bits; k++) t[i++] = '0'; if (i < bits) t[i++] = '1'; gap++; } t[bits] = '\0'; }
    NEXT("growing runs 0110001111.."); { int i = 0, run = 1, c = 0; while (i < bits) { for (int k = 0; k < run && i < bits; k++) t[i++] = (char)('0' + c); c ^= 1; run++; } t[bits] = '\0'; }
    NEXT("Thue-Morse"); for (int i = 0; i < bits; i++) t[i] = (char)('0' + (__builtin_popcount((unsigned)i) & 1)); t[bits] = '\0';
    NEXT("Fibonacci word"); { char a[128] = "0", b[128] = "01", c[256]; while ((int)strlen(b) < bits) { snprintf(c, sizeof c, "%s%s", b, a); snprintf(a, sizeof a, "%s", b); snprintf(b, sizeof b, "%.127s", c); } memcpy(t, b, (size_t)bits); t[bits] = '\0'; }
    NEXT("squares 1 at 0,1,4,9,16"); memset(t, '0', (size_t)bits); t[bits] = '\0'; for (int k = 0; k * k < bits; k++) t[k * k] = '1';
    NEXT("0^(L-3) 101"); memset(t, '0', (size_t)bits); t[bits] = '\0'; t[bits - 1] = t[bits - 3] = '1';
#undef NEXT
    return n;
}

static void run_structured(int bits) {
    static char names[32][40], targets[32][80];
    RevLevelOptions opt = {max_steps};
    int count = structured(bits, names, targets), worst = 0;
    uint64_t worst_steps = 0;
    printf("\n%d bits: structured strings, each solved from one node up (direct) and as a sibling (ladder)\n",
           bits);
    printf("  %-26s nodes   prefix   direct steps   seconds   ladder steps   top budget steps/s\n", "string");
    for (int k = 0; k < count; k++) {
        RevLevelStats st;
        RevGraph g;
        uint64_t direct = 0, ladder = 0, top_steps = 0;
        double t0 = cb_now(), secs, top_s = 0;
        int nodes = 0, prefix = 0;
        for (int n = 1; n < MAXN && !prefix; n++) /* minimum of the first bits-1 bits */
            if (engine(targets[k], (size_t)(bits - 1), n, &opt, &g, &st) == REV_SOLVE_SOLVED) prefix = n;
            else if (st.limit != REV_LIMIT_NONE) break;
        t0 = cb_now();
        for (int n = 1; n < MAXN && !nodes; n++) {
            double c0 = cb_now();
            RevSolveResult r = engine(targets[k], (size_t)bits, n, &opt, &g, &st);
            direct += st.steps;
            if (n >= prefix) ladder += st.steps;
            top_steps = st.steps;
            top_s = cb_now() - c0;
            if (r == REV_SOLVE_SOLVED) nodes = n;
            else if (r != REV_SOLVE_PROVEN_UNSAT) break;
        }
        secs = cb_now() - t0;
        printf("  %-26s %5d   %6d   %12.4g   %7.3f   %12.4g   %.3g%s\n", names[k], nodes, prefix,
               (double)direct, secs, (double)ladder, top_s > 0 ? (double)top_steps / top_s : 0.0,
               nodes ? "" : "   (step limit)");
        if (direct > worst_steps) {
            worst_steps = direct;
            worst = k;
        }
    }
    printf("  costliest: %s (%s), %.4g steps\n", names[worst], targets[worst], (double)worst_steps);
}

int main(int argc, char **argv) {
    int bits_list[16], nbits = 0, workers = 1, with_structured = 1;
    size_t samples = 800;
    uint64_t seed = 20261112;
    LevelEstimate est[32];
    double hardest[32] = {0};
    memset(est, 0, sizeof est);

#ifdef CARLOBOMB_HAVE_AMD64_ASM
    if (cb_amd64_kernel_select(cb_amd64_kernel_find("auto")) == 0) {
        engine = rev_level_solve_amd64_ex;
        engine_name = "amd64-auto";
    }
#endif
    for (int i = 1; i < argc; i++) {
        const char *k = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(k, "--no-structured")) { with_structured = 0; continue; }
        if (!v) goto usage;
        i++;
        if (!strcmp(k, "--bits")) {
            char *end;
            for (const char *p = v; *p && nbits < 16;) {
                long b = strtol(p, &end, 10);
                if (end == p || b < 4 || b > 31) goto usage;
                bits_list[nbits++] = (int)b;
                p = *end == ',' ? end + 1 : end;
                if (*end && *end != ',') goto usage;
            }
        } else if (!strcmp(k, "--samples")) samples = (size_t)strtoull(v, NULL, 10);
        else if (!strcmp(k, "--workers")) workers = atoi(v);
        else if (!strcmp(k, "--seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(k, "--max-steps")) max_steps = strtoull(v, NULL, 10);
        else if (!strcmp(k, "--engine")) {
            if (!strcmp(v, "c")) { engine = rev_level_solve_ex; engine_name = "c"; }
            else if (strcmp(v, engine_name) != 0) { fprintf(stderr, "engine %s is not available here\n", v); return 2; }
        } else goto usage;
    }
    if (nbits == 0) { bits_list[0] = 20; bits_list[1] = 22; bits_list[2] = 24; nbits = 3; }
    if (samples < 8 || workers < 1 || workers > 256) goto usage;

    printf("Ladder pilot: engine %s, %d process%s, up to %zu samples per length, seed %llu\n", engine_name,
           workers, workers == 1 ? "" : "es", samples, (unsigned long long)seed);
    for (int k = 0; k < nbits; k++) {
        int bits = bits_list[k];
        Sample *s = malloc(samples * sizeof *s);
        size_t have = 0;
        double wall = 0;
        for (int stage = 3; stage >= 0; stage--) { /* N/8, N/4, N/2, N samples in all */
            size_t upto = samples >> stage;
            if (upto <= have) continue;
            wall += draw(bits, upto - have, workers, seed + (uint64_t)bits * 7919u + have, s + have);
            have = upto;
            report(bits, s, have, wall, workers, stage == 0, &est[bits]);
            fflush(stdout);
        }
        if (with_structured) {
            /* the costliest structured string, as a ladder job, sizes the unseen tail */
            static char names[32][40], targets[32][80];
            RevLevelOptions opt = {max_steps};
            int count = structured(bits, names, targets);
            run_structured(bits);
            for (int j = 0; j < count; j++) {
                RevLevelStats st;
                RevGraph g;
                uint64_t ladder = 0;
                int prefix = 0;
                for (int n = 1; n < MAXN && !prefix; n++)
                    if (engine(targets[j], (size_t)(bits - 1), n, &opt, &g, &st) == REV_SOLVE_SOLVED) prefix = n;
                    else if (st.limit != REV_LIMIT_NONE) break;
                for (int n = prefix ? prefix : 1; n < MAXN; n++) {
                    RevSolveResult r = engine(targets[j], (size_t)bits, n, &opt, &g, &st);
                    ladder += st.steps;
                    if (r != REV_SOLVE_PROVEN_UNSAT) break;
                }
                if ((double)ladder > hardest[bits]) hardest[bits] = (double)ladder;
            }
        }
        /* If the sample saw no job as costly as the hardest structured one,
         * up to 3/N of all jobs could still be like it (rule of three). */
        est[bits].tail_steps = hardest[bits] * 3.0 / (double)have;
        free(s);
        fflush(stdout);
    }

    printf("\nProjection (complement pairs only: 2^(L-2) sibling jobs at level L)\n");
    printf("  level   jobs         steps, expected      range (low .. high)        at the measured %.3g steps/s\n",
           est[bits_list[nbits - 1]].steps_per_second);
    for (int k = 0; k < nbits; k++) {
        int bits = bits_list[k];
        double jobs = ldexp(1.0, bits - 2), rate = est[bits].steps_per_second;
        double mid = jobs * est[bits].mean_sibling;
        double lo = jobs * (est[bits].mean_sibling - 2 * est[bits].se_sibling);
        double hi = jobs * (est[bits].mean_sibling + 2 * est[bits].se_sibling + est[bits].tail_steps);
        if (lo < 0.5 * mid) lo = 0.5 * mid;
        printf("  %5d   %9.0f   %14.4g   %10.4g .. %-10.4g   %.2f h  (%.2f .. %.2f h)\n", bits, jobs, mid, lo,
               hi, mid / rate / 3600, lo / rate / 3600, hi / rate / 3600);
    }
    if (nbits >= 2) {
        /* Levels between and below the measured ones, by the geometric growth between them. */
        int top = bits_list[nbits - 1], below = bits_list[nbits - 2];
        double per_bit = pow((ldexp(1.0, top - 2) * est[top].mean_sibling) /
                                 (ldexp(1.0, below - 2) * est[below].mean_sibling),
                             1.0 / (double)(top - below));
        double level = ldexp(1.0, top - 2) * est[top].mean_sibling, ladder = 0;
        double hi_scale = (est[top].mean_sibling + 2 * est[top].se_sibling + est[top].tail_steps) / est[top].mean_sibling;
        for (int b = top; b > 16; b--) {
            ladder += level;
            level /= per_bit;
        }
        printf("  growth per level between %d and %d bits: x%.2f\n", below, top, per_bit);
        printf("  whole ladder 17..%d: %.4g steps expected, %.2f h at the measured rate (up to %.2f h);\n"
               "  the last level is %.0f%% of it\n",
               top, ladder, ladder / est[top].steps_per_second / 3600,
               ladder * hi_scale / est[top].steps_per_second / 3600,
               100.0 * ldexp(1.0, top - 2) * est[top].mean_sibling / ladder);
        printf("  direct method for comparison (every pair from one node up): about %.4g steps at %d bits\n",
               ldexp(1.0, top - 1) * est[top].mean_direct * per_bit, top);
    }
    return 0;
usage:
    fprintf(stderr, "usage: ladder_pilot [--bits L[,L...]] [--samples N] [--workers W] [--seed S]\n"
                    "                    [--engine c|amd64-auto] [--max-steps K] [--no-structured]\n");
    return 2;
}
