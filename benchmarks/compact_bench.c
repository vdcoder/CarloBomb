/*
 * compact_bench: the fixed-width file and the compact file of one Bomb, side
 * by side, through the one interface that reads both.
 *
 *   compact_bench --fixed FILE --compact FILE [--queries N] [--no-check]
 *
 * FILE of --fixed is a complete fixed-width Bomb (format version 1, 2 or 3),
 * FILE of --compact the compact file made from it (`carlobomb bomb compact`).
 *
 * For each file: its size; the time to open it as a solver does, which is a
 * full validation, and the memory that takes; and the time of a lookup in
 * the opened Bomb, for random strings of the full length and of every
 * length, as a node count alone and as the REV from rev_fixed_bomb_solve,
 * which runs it against the target before returning it.  For the compact
 * file also the REV from its reader alone, without that run, and the
 * comparison of the two extensions of a string, which it works out on
 * demand.  Then lookups without opening anything: one answer read from the
 * file.
 *
 * Unless --no-check is given, every string of every length is first asked of
 * both files.  The node counts must agree, and every comparison must be what
 * the node counts of the fixed-width Bomb say.  The extension chosen for a
 * shorter string must be the same where the string starts with 0, and where
 * it starts with 1 the complement of the one chosen for its complement: the
 * compact file's rule.  The timed lookups add up what they return, and the
 * sums that have to agree are compared.
 *
 * Timings are the best of five passes over the same queries, in an order
 * the processor cannot learn; the percentiles are of single lookups, each
 * timed by itself, with the cost of reading the clock taken off.  They are
 * of this machine with the files in the page cache; what a cold disk adds is
 * its own business.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact.h"
#include "fixed_bomb_solver.h"

enum { RUNS = 5 };

typedef struct {
    uint32_t value;
    uint8_t bits;
} Query;

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rnd64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

/* Resident memory of this process in KiB: anonymous (its own) and file
 * pages (of mapped files, shared with the page cache).  -1 where the system
 * does not say. */
static void memory_now(long *anon_kb, long *file_kb) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    *anon_kb = *file_kb = -1;
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "RssAnon:", 8)) *anon_kb = atol(line + 8);
        if (!strncmp(line, "RssFile:", 8)) *file_kb = atol(line + 8);
    }
    fclose(f);
}

static double time_nodes(const RevFixedBomb *b, const Query *q, size_t n, unsigned long long *sum) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) s += (unsigned)rev_fixed_bomb_min_nodes(b, q[i].bits, q[i].value);
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

/* What a solve returned, folded into a number. */
static unsigned long long fold(const RevGraph *g, const RevFixedBombInfo *info) {
    unsigned long long s = (unsigned)g->node_count * 1000003u + info->extension;
    for (int k = 0; k < g->node_count; k++)
        s += (unsigned)(g->nodes[k].color + 3 * g->nodes[k].links[0] + 7 * g->nodes[k].links[1]) * (unsigned)(k + 1);
    return s;
}

static double time_solve(const RevFixedBomb *b, const Query *q, char (*text)[FRB_MAX_BITS + 1], size_t n,
                         unsigned long long *sum, int *failed) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) {
            RevFixedBombInfo info;
            RevGraph g;
            if (rev_fixed_bomb_solve(b, text[i], q[i].bits, 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) *failed = 1;
            s += fold(&g, &info);
        }
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

/* The comparison of the two extensions of a string, from the node counts of
 * the two, as either Bomb gives them. */
static double time_compare_nodes(const RevFixedBomb *b, const Query *q, size_t n, unsigned long long *sum) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) {
            int n0 = rev_fixed_bomb_min_nodes(b, q[i].bits + 1, 2 * q[i].value);
            int n1 = rev_fixed_bomb_min_nodes(b, q[i].bits + 1, 2 * q[i].value + 1);
            s += (unsigned)(n0 < n1 ? 0 : n1 < n0 ? 1 : 2);
        }
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

static double time_compare(const FrbCompact *c, const Query *q, size_t n, unsigned long long *sum) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) s += (unsigned)frb_compact_compare(c, q[i].bits, q[i].value, NULL);
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

/* The REV as the reader of the compact format hands it over: found by its navigation bits,
 * decoded, its colors found, its nodes numbered; not run again against the target. */
static double time_graph(const FrbCompact *c, const Query *q, size_t n, unsigned long long *sum) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) {
            RevGraph g;
            if (frb_compact_graph(c, q[i].bits, q[i].value, &g) != FRB_OK) g.node_count = 0;
            s += (unsigned)g.node_count;
        }
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Single lookups, each timed by itself: the 50th, 95th and 99th percentile in nanoseconds. */
typedef struct {
    double p50, p95, p99;
} Percentiles;

static double clock_cost; /* seconds for one pair of readings of the clock */

static Percentiles percentiles(double *seconds, size_t n) {
    Percentiles p;
    qsort(seconds, n, sizeof *seconds, cmp_double);
    p.p50 = (seconds[n / 2] - clock_cost) * 1e9;
    p.p95 = (seconds[(size_t)((double)(n - 1) * 0.95)] - clock_cost) * 1e9;
    p.p99 = (seconds[(size_t)((double)(n - 1) * 0.99)] - clock_cost) * 1e9;
    return p;
}

static Percentiles single_solves(const RevFixedBomb *b, const Query *q, char (*text)[FRB_MAX_BITS + 1], size_t n,
                                 double *seconds) {
    for (int pass = 0; pass < 2; pass++) /* the second pass is the one kept */
        for (size_t i = 0; i < n; i++) {
            RevFixedBombInfo info;
            RevGraph g;
            double t0 = cb_now();
            rev_fixed_bomb_solve(b, text[i], q[i].bits, 0, &g, &info);
            seconds[i] = cb_now() - t0;
        }
    return percentiles(seconds, n);
}

static Percentiles single_compares(const FrbCompact *c, const Query *q, size_t n, double *seconds) {
    volatile unsigned keep = 0;
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < n; i++) {
            double t0 = cb_now();
            keep += (unsigned)frb_compact_compare(c, q[i].bits, q[i].value, NULL);
            seconds[i] = cb_now() - t0;
        }
    return percentiles(seconds, n);
}

int main(int argc, char **argv) {
    const char *paths[2] = {NULL, NULL};
    const char *names[2] = {"fixed-width", "compact"};
    RevFixedBomb *bombs[2] = {NULL, NULL};
    FrbCompact *compact = NULL;
    FrbCompactInfo cinfo;
    FrbFileInfo finfo;
    double open_s[2] = {0}, ns[2][4], compare_ns[2] = {0}, reader_ns[2] = {0}, *seconds;
    unsigned long long sums[2][4], compare_sum[2] = {0}, reader_sum[2] = {0};
    long anon_kb[2] = {0}, mapped_kb[2] = {0};
    char (*text)[FRB_MAX_BITS + 1];
    Query *exact, *any, *shorter;
    Percentiles p_exact[2], p_any[2], p_compare;
    uint64_t fixed_bytes;
    size_t queries = 2000000, singles;
    int bits = 0, check = 1, failed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-check")) check = 0;
        else if (!strcmp(argv[i], "--fixed") && i + 1 < argc) paths[0] = argv[++i];
        else if (!strcmp(argv[i], "--compact") && i + 1 < argc) paths[1] = argv[++i];
        else if (!strcmp(argv[i], "--queries") && i + 1 < argc) queries = (size_t)strtoull(argv[++i], NULL, 10);
        else paths[0] = NULL, i = argc;
    }
    if (!paths[0] || !paths[1] || queries < 1) {
        fprintf(stderr, "usage: compact_bench --fixed FILE --compact FILE [--queries N] [--no-check]\n");
        return 2;
    }
    if (frb_file_is_compact(paths[0]) != 0 || frb_file_is_compact(paths[1]) != 1 ||
        frb_file_info(paths[0], &finfo) != FRB_OK || frb_compact_file_info(paths[1], &cinfo) != FRB_OK) {
        fprintf(stderr, "compact_bench: --fixed takes a fixed-width Bomb, --compact its compact file\n");
        return 2;
    }

    /* ---- sizes ---- */
    fixed_bytes = frb_file_bytes(frb_format(finfo.version), finfo.bits);
    printf("Files\n");
    printf("  %-12s %15llu bytes  %6.1f%%   %s\n", names[0], (unsigned long long)fixed_bytes, 100.0, paths[0]);
    printf("  %-12s %15llu bytes  %6.1f%%   %s\n", names[1], (unsigned long long)cinfo.file_bytes,
           100.0 * (double)cinfo.file_bytes / (double)fixed_bytes, paths[1]);
    {
        uint64_t revs = 0;
        for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) revs += cinfo.class_bytes[n];
        printf("  %12s   REVs %llu, node counts %llu, counts before %llu, navigation bits %llu, the rest %llu\n", "",
               (unsigned long long)revs, (unsigned long long)cinfo.nodes_bytes, (unsigned long long)cinfo.blocks_bytes,
               (unsigned long long)cinfo.nav_bytes,
               (unsigned long long)(cinfo.file_bytes - revs - cinfo.nodes_bytes - cinfo.blocks_bytes - cinfo.nav_bytes));
        printf("  %12s   %.2f bits a string; %u of its %u strings with another REV than the fixed-width file's\n", "",
               8.0 * (double)cinfo.file_bytes / (double)cinfo.count, (unsigned)cinfo.other_revs, (unsigned)cinfo.count);
    }

    /* ---- opening ---- */
    printf("\nOpening, as rev_fixed_bomb_open does it: the whole file validated, every REV run\n");
    for (int f = 0; f < 2; f++) {
        long a0, f0, a1, f1;
        double t0;
        FrbError e;
        memory_now(&a0, &f0);
        t0 = cb_now();
        e = rev_fixed_bomb_open(paths[f], &bombs[f], NULL);
        open_s[f] = cb_now() - t0;
        if (e != FRB_OK) {
            fprintf(stderr, "compact_bench: cannot open %s: %s\n", paths[f], frb_error_str(e));
            return 1;
        }
        memory_now(&a1, &f1);
        anon_kb[f] = a0 < 0 ? -1 : a1 - a0;
        mapped_kb[f] = a0 < 0 ? -1 : f1 - f0;
        if (f == 0) bits = rev_fixed_bomb_bits(bombs[f]);
        else if (rev_fixed_bomb_bits(bombs[f]) != bits) {
            fprintf(stderr, "compact_bench: the files are Bombs for different lengths\n");
            return 1;
        }
        printf("  %-12s %7.2f s   memory of its own %7.1f MiB, mapped file pages resident %7.1f MiB\n", names[f],
               open_s[f], (double)anon_kb[f] / 1024.0, (double)mapped_kb[f] / 1024.0);
    }
    if (frb_compact_open(paths[1], &compact, NULL) != FRB_OK) {
        fprintf(stderr, "compact_bench: cannot open %s\n", paths[1]);
        return 1;
    }

    /* ---- the same answers ---- */
    if (check) {
        const uint32_t mask = (((uint32_t)1 << (bits - 1)) << 1) - 1u;
        unsigned long long strings = 0, wrong = 0, mirrored = 0;
        double t0 = cb_now();
        for (int len = 1; len <= bits; len++) {
            for (uint32_t v = 0;; v++) {
                int nodes = rev_fixed_bomb_min_nodes(bombs[0], len, v);
                uint32_t extension = rev_fixed_bomb_extension(bombs[0], len, v);
                uint32_t mine = rev_fixed_bomb_extension(bombs[1], len, v);
                wrong += rev_fixed_bomb_min_nodes(bombs[1], len, v) != nodes;
                if (len < bits && (v >> (len - 1))) { /* starts with 1: the complement of its complement's */
                    uint32_t twin = rev_fixed_bomb_extension(bombs[0], len, v ^ (((uint32_t)1 << len) - 1u));
                    wrong += mine != (twin ^ mask) || rev_fixed_bomb_min_nodes(bombs[0], bits, mine) != nodes;
                    mirrored += mine != extension;
                } else {
                    wrong += mine != extension;
                }
                if (len < bits) { /* the comparison of its two extensions */
                    int n0 = rev_fixed_bomb_min_nodes(bombs[0], len + 1, 2 * v);
                    int n1 = rev_fixed_bomb_min_nodes(bombs[0], len + 1, 2 * v + 1), got[2];
                    wrong += frb_compact_compare(compact, len, v, got) != (n0 < n1 ? 0 : n1 < n0 ? 1 : FRB_COMPACT_TIE) ||
                             got[0] != n0 || got[1] != n1;
                }
                strings++;
                if (v == ((uint32_t)1 << len) - 1u) break;
            }
        }
        printf("\nEvery string of 1 to %d bits (%llu) asked of both files: node counts, extensions and comparisons\n"
               "%s; %llu shorter strings that start with 1 answered by another of their equally small\n"
               "extensions than the fixed-width Bomb takes  (%.1f s)\n",
               bits, strings, wrong ? "DIFFER" : "agree", mirrored, cb_now() - t0);
        failed |= wrong != 0;
    }

    /* ---- lookups in the opened Bomb ---- */
    exact = malloc(queries * sizeof *exact);
    any = malloc(queries * sizeof *any);
    shorter = malloc(queries * sizeof *shorter);
    text = malloc(queries * sizeof *text);
    singles = queries < 200000 ? queries : 200000;
    seconds = malloc(singles * sizeof *seconds);
    if (!exact || !any || !shorter || !text || !seconds) {
        fprintf(stderr, "compact_bench: out of memory for the queries\n");
        return 1;
    }
    for (size_t i = 0; i < queries; i++) {
        exact[i].bits = (uint8_t)bits;
        exact[i].value = (uint32_t)(rnd64() >> 33) & ((((uint32_t)1 << (bits - 1)) << 1) - 1u);
        any[i].bits = (uint8_t)(1 + (rnd64() >> 33) % (uint64_t)bits);
        any[i].value = (uint32_t)(rnd64() >> 33) & ((((uint32_t)1 << (any[i].bits - 1)) << 1) - 1u);
        shorter[i].bits = (uint8_t)(bits > 1 ? 1 + (rnd64() >> 33) % (uint64_t)(bits - 1) : 0);
        shorter[i].value = shorter[i].bits ? (uint32_t)(rnd64() >> 33) & (((uint32_t)1 << shorter[i].bits) - 1u) : 0;
    }
    {   /* what reading the clock twice costs */
        double least = 1.0;
        for (int i = 0; i < 100000; i++) {
            double t0 = cb_now(), t = cb_now() - t0;
            if (t < least) least = t;
        }
        clock_cost = least;
    }
    for (int f = 0; f < 2; f++) {
        ns[f][0] = time_nodes(bombs[f], exact, queries, &sums[f][0]);
        ns[f][1] = time_nodes(bombs[f], any, queries, &sums[f][1]);
    }
    for (int w = 2; w < 4; w++) {
        const Query *q = w == 2 ? exact : any;
        for (size_t i = 0; i < queries; i++) frb_target_format(q[i].value, q[i].bits, text[i]);
        for (int f = 0; f < 2; f++) {
            ns[f][w] = time_solve(bombs[f], q, text, queries, &sums[f][w], &failed);
            if (w == 2) p_exact[f] = single_solves(bombs[f], q, text, singles, seconds);
            else p_any[f] = single_solves(bombs[f], q, text, singles, seconds);
        }
    }
    reader_ns[0] = time_graph(compact, exact, queries, &reader_sum[0]);
    reader_ns[1] = time_graph(compact, any, queries, &reader_sum[1]);
    compare_ns[0] = time_compare_nodes(bombs[0], shorter, queries, &compare_sum[0]);
    compare_ns[1] = time_compare(compact, shorter, queries, &compare_sum[1]);
    p_compare = single_compares(compact, shorter, singles, seconds);

    printf("\nLookups in the opened Bomb, nanoseconds each (%zu random strings, best of %d passes)\n", queries, RUNS);
    printf("  %-12s %14s %14s %14s %14s %14s\n", "", "nodes, L bits", "nodes, 1..L", "REV, L bits", "REV, 1..L",
           "comparison");
    for (int f = 0; f < 2; f++)
        printf("  %-12s %14.1f %14.1f %14.1f %14.1f %14.1f\n", names[f], ns[f][0], ns[f][1], ns[f][2], ns[f][3],
               compare_ns[f]);
    printf("  %-12s %14s %14s %14.1f %14.1f %14s   (its reader alone)\n", "", "", "", reader_ns[0], reader_ns[1], "");
    printf("  REV: from rev_fixed_bomb_solve, which runs every REV against the target before it returns it,\n"
           "  whichever file it came from.  Its reader alone: frb_compact_graph, the REV found, decoded,\n"
           "  colored and numbered, without that run.\n");
    printf("  comparison: which of the two extensions of a string of 1 to %d bits needs fewer nodes.  The compact\n"
           "  file works it out on demand; for the fixed-width Bomb it is two node counts from the index the\n"
           "  solver builds in memory when it opens the file.\n",
           bits - 1);
    printf("\nSingle lookups, each timed by itself, nanoseconds (%zu lookups)\n", singles);
    printf("  %-12s %30s %30s %30s\n", "", "REV, L bits: p50  p95  p99", "REV, 1..L: p50  p95  p99",
           "comparison: p50  p95  p99");
    printf("  %-12s %18.0f %5.0f %5.0f %18.0f %5.0f %5.0f %30s\n", names[0], p_exact[0].p50, p_exact[0].p95,
           p_exact[0].p99, p_any[0].p50, p_any[0].p95, p_any[0].p99, "-");
    printf("  %-12s %18.0f %5.0f %5.0f %18.0f %5.0f %5.0f %18.0f %5.0f %5.0f\n", names[1], p_exact[1].p50,
           p_exact[1].p95, p_exact[1].p99, p_any[1].p50, p_any[1].p95, p_any[1].p99, p_compare.p50, p_compare.p95,
           p_compare.p99);
    /* Node counts and comparisons are the same from both; so are the REVs of L-bit strings, if the
     * compact file has no other REVs.  The REVs of shorter strings differ by the rule, and are not compared. */
    failed |= sums[1][0] != sums[0][0] || sums[1][1] != sums[0][1] || compare_sum[1] != compare_sum[0];
    failed |= reader_sum[0] != sums[0][0] || reader_sum[1] != sums[0][1]; /* the node counts of its REVs */
    if (cinfo.other_revs == 0) failed |= sums[1][2] != sums[0][2];
    printf("  what the lookups returned, added up: %s\n",
           failed ? "DIFFERS between the files" : "the same from both files where it has to be");

    /* ---- one answer from the file, nothing opened ---- */
    {
        size_t n = queries < 20000 ? queries : 20000;
        unsigned long long s_fixed = 0, s_compact = 0;
        double t0, t_fixed, t_compact, t_any, t_compare;
        RevGraph g;
        t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            FrbStatus status;
            if (frb_file_lookup(paths[0], exact[i].value, NULL, &status, &g) != FRB_OK) failed = 1;
            s_fixed += (unsigned)g.node_count;
        }
        t_fixed = (cb_now() - t0) / (double)n * 1e6;
        t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            if (frb_compact_file_lookup(paths[1], bits, exact[i].value, NULL, &g, NULL) != FRB_OK) failed = 1;
            s_compact += (unsigned)g.node_count;
        }
        t_compact = (cb_now() - t0) / (double)n * 1e6;
        failed |= s_compact != s_fixed;
        t0 = cb_now();
        for (size_t i = 0; i < n; i++)
            if (frb_compact_file_lookup(paths[1], any[i].bits, any[i].value, NULL, &g, NULL) != FRB_OK) failed = 1;
        t_any = (cb_now() - t0) / (double)n * 1e6;
        t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            int which = 0;
            if (frb_compact_file_compare(paths[1], shorter[i].bits, shorter[i].value, NULL, &which, NULL) != FRB_OK)
                failed = 1;
        }
        t_compare = (cb_now() - t0) / (double)n * 1e6;
        printf("\nOne answer read from the file, nothing opened before, microseconds each (%zu lookups)\n", n);
        printf("  %-12s %14s %14s %14s\n", "", "REV, L bits", "REV, 1..L", "comparison");
        printf("  %-12s %14.1f %14s %14s   (a shorter string needs the Bomb opened: %.2f s)\n", names[0], t_fixed, "-",
               "-", open_s[0]);
        printf("  %-12s %14.1f %14.1f %14.1f\n", names[1], t_compact, t_any, t_compare);
    }

    for (int f = 0; f < 2; f++) rev_fixed_bomb_close(bombs[f]);
    frb_compact_close(compact);
    free(exact);
    free(any);
    free(shorter);
    free(text);
    free(seconds);
    printf("\n%s\n", failed ? "FAIL: the files do not give the same answers" : "PASS: the same answers from both files");
    return failed ? 1 : 0;
}
