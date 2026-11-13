/*
 * colorless_bench: what recoloring costs, REV by REV, against ordinary
 * verification of the colored REV.
 *
 *   colorless_bench --bomb FILE [--sample N] [--hard H] [--passes P]
 *
 * From every node count of a complete fixed-width Bomb a random sample of N
 * strings is taken (all of them where there are fewer; default 100,000),
 * and beside it the H strings whose recoloring takes the most machine steps
 * of all strings of that node count (default 1,000), found by recoloring
 * every string of the Bomb once.
 *
 * Each operation is timed on each REV by itself, with the processor's time
 * stamp counter around one call.  The whole sample is gone through P times
 * (default 7) in a different random order each time, so that the processor
 * cannot learn a REV, and a REV's time is the median of its P times.  The
 * mean is taken a second way too, from one clock reading around a whole
 * pass, which has no timer in it.
 *
 * What is timed (the REV is in registers and first-level cache; getting it
 * from a file or from memory is not part of this, see colorless_lookup):
 *
 *   verify     the colored REV run for L bits and compared with the string
 *   reference  the same through rev_verify_prefix of core/rev, as the
 *              solver does it today
 *   recolor    the colors found from topology and string, first solution
 *   recolor+v  recolor, then verify what was found
 *   all        every coloring that emits the string (what proving that
 *              there is one, or finding the others, costs)
 *   brute      all 2^N colorings run, the reference for "all"
 *
 * and three ways of unpacking a REV from its stored form:
 *
 *   fields     the compact format's packing (color and two links per node)
 *   base N     colors and links as one number in base N ("hyper-bit")
 *   rank       the rank of the canonical topology among all canonical
 *              topologies of N nodes, the smallest fixed-size code
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorless.h"
#include "compact.h"
#include "fixed_run.h"

#if defined(__x86_64__)
#include <x86intrin.h>
static inline uint64_t tick_begin(void) {
    _mm_lfence();
    return __rdtsc();
}
static inline uint64_t tick_end(void) {
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}
#else
static inline uint64_t tick_begin(void) { return (uint64_t)(cb_now() * 1e9); }
static inline uint64_t tick_end(void) { return (uint64_t)(cb_now() * 1e9); }
#endif

enum { CLASSES = CL_MAX_NODES + 1, MAX_PASSES = 15 };

typedef struct {
    ClTopo canon;     /* canonical numbering */
    uint32_t colors;  /* its colors, in that numbering */
    uint32_t target;
    uint32_t states;  /* machine steps to the first coloring */
    uint64_t rank;    /* of the canonical topology */
    uint64_t base_n;  /* links of the stored REV in base N, with its colors below (n <= 8) */
    ClU128 base_n_wide; /* the same in 128 bits (any n here) */
    uint32_t stored_colors;
    uint8_t fields[16]; /* the stored REV as the compact format packs it */
} Item;

typedef enum { OP_VERIFY, OP_REFERENCE, OP_RECOLOR, OP_RECOLOR_VERIFY, OP_ALL, OP_BRUTE, OP_FIELDS, OP_BASE_N, OP_RANK, OPS } Op;

static const char *op_name[OPS] = {"verify", "reference", "recolor", "recolor+v", "all", "brute", "fields", "base N", "rank"};

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rnd64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

static int L;
static double ns_per_tick;
static uint64_t tick_overhead;

/* One operation on one REV; the result is something that depends on all of it. */
static inline uint32_t run_op(Op op, const Item *it, const RevGraph *g, const char *text) {
    static uint32_t many[1 << CL_MAX_NODES];
    ClTopo t;
    uint32_t colors = 0;
    switch (op) {
    case OP_VERIFY: return (uint32_t)cl_verify(&it->canon, it->colors, it->target, L);
    case OP_REFERENCE: return (uint32_t)(rev_verify_prefix(g, text, (size_t)L, NULL) == REV_OK);
    case OP_RECOLOR:
        cl_recolor(&it->canon, it->target, L, 0, &colors, NULL);
        return colors;
    case OP_RECOLOR_VERIFY:
        cl_recolor(&it->canon, it->target, L, 0, &colors, NULL);
        return colors + (uint32_t)cl_verify(&it->canon, colors, it->target, L);
    case OP_ALL: return (uint32_t)cl_recolor_all(&it->canon, it->target, L, many, 64, NULL);
    case OP_BRUTE: return (uint32_t)cl_brute(&it->canon, it->target, L, many, 64);
    case OP_FIELDS: {
        RevGraph out;
        frb_compact_unpack(it->fields, it->canon.n, &out);
        return (uint32_t)(out.nodes[0].links[0] + out.nodes[it->canon.n - 1].links[1] + out.nodes[it->canon.n - 1].color);
    }
    case OP_BASE_N:
        if (it->canon.n <= 8) {
            cl_radix_unpack64(it->base_n >> it->canon.n, it->canon.n, &t);
            colors = (uint32_t)(it->base_n & ((1u << it->canon.n) - 1u));
        } else {
            cl_radix_unpack(it->base_n_wide >> it->canon.n, it->canon.n, &t);
            colors = (uint32_t)(it->base_n_wide & ((1u << it->canon.n) - 1u));
        }
        return (uint32_t)t.link[0][0] + t.link[1][it->canon.n - 1] + colors;
    case OP_RANK:
        cl_canon_unrank(it->canon.n, it->rank, &t);
        return (uint32_t)t.link[0][0] + t.link[1][it->canon.n - 1];
    default: return 0;
    }
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef struct {
    double mean_loop, mean, p50, p95, p99, p999, max;
} Times;

/* Time one operation on every item, `passes` times in shuffled order. */
static Times time_op(Op op, const Item *items, size_t count, int passes, uint32_t *check) {
    uint32_t *order = malloc(count * sizeof *order);
    uint32_t *ticks = malloc(count * (size_t)passes * sizeof *ticks);
    double *ns = malloc(count * sizeof *ns), best_loop = 1e30, sum = 0.0;
    uint32_t sink = 0;
    Times r;
    for (size_t i = 0; i < count; i++) order[i] = (uint32_t)i;
    for (int p = 0; p < passes; p++) {
        double t0;
        for (size_t i = count - 1; i > 0; i--) {
            size_t j = (size_t)(rnd64() % (uint64_t)(i + 1));
            uint32_t tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        /* each REV by itself */
        for (size_t i = 0; i < count; i++) {
            Item it = items[order[i]]; /* out of the array, into registers and stack */
            RevGraph g;
            char text[CL_MAX_BITS + 1];
            uint64_t a, b;
            if (op == OP_REFERENCE) {
                cl_to_graph(&it.canon, it.colors, &g);
                frb_target_format(it.target, L, text);
            }
            a = tick_begin();
            sink += run_op(op, &it, &g, text);
            b = tick_end();
            ticks[(size_t)order[i] * (size_t)passes + (size_t)p] = (uint32_t)(b - a > tick_overhead ? b - a - tick_overhead : 0);
        }
        /* the whole pass under one clock, no timer inside */
        if (op != OP_REFERENCE) {
            t0 = cb_now();
            for (size_t i = 0; i < count; i++) sink += run_op(op, &items[order[i]], NULL, NULL);
            t0 = cb_now() - t0;
            if (t0 < best_loop) best_loop = t0;
        }
    }
    for (size_t i = 0; i < count; i++) {
        qsort(ticks + i * (size_t)passes, (size_t)passes, sizeof *ticks, cmp_u32);
        ns[i] = (double)ticks[i * (size_t)passes + (size_t)passes / 2] * ns_per_tick;
        sum += ns[i];
    }
    qsort(ns, count, sizeof *ns, cmp_double);
    r.mean_loop = op == OP_REFERENCE ? 0.0 : best_loop / (double)count * 1e9;
    r.mean = sum / (double)count;
    r.p50 = ns[count / 2];
    r.p95 = ns[(size_t)((double)(count - 1) * 0.95)];
    r.p99 = ns[(size_t)((double)(count - 1) * 0.99)];
    r.p999 = ns[(size_t)((double)(count - 1) * 0.999)];
    r.max = ns[count - 1];
    *check += sink;
    free(order);
    free(ticks);
    free(ns);
    return r;
}

static void calibrate(void) {
    uint64_t least = ~(uint64_t)0, a, b;
    double t0 = cb_now(), t1;
    a = tick_begin();
    do t1 = cb_now();
    while (t1 - t0 < 0.25);
    b = tick_end();
    ns_per_tick = (t1 - t0) * 1e9 / (double)(b - a);
    for (int i = 0; i < 100000; i++) {
        a = tick_begin();
        b = tick_end();
        if (b - a < least) least = b - a;
    }
    tick_overhead = least;
}

static void make_item(Item *it, const RevGraph *g, uint32_t target) {
    ClTopo stored;
    ClSearch s;
    uint32_t colors = 0, found = 0;
    memset(it, 0, sizeof *it);
    cl_from_graph(g, &stored, &colors);
    cl_canonical(&stored, colors, &it->canon, &it->colors, NULL);
    it->target = target;
    cl_recolor(&it->canon, target, L, 0, &found, &s);
    it->states = s.states;
    it->rank = cl_canon_rank(&it->canon);
    it->stored_colors = colors;
    it->base_n_wide = (cl_radix_pack(&stored) << stored.n) | colors;
    it->base_n = (uint64_t)it->base_n_wide;
    frb_compact_pack(g, it->fields);
}

int main(int argc, char **argv) {
    const char *path = NULL;
    FrbFileInfo info;
    FrbTable table;
    Item *sample[CLASSES] = {NULL}, *hard[CLASSES] = {NULL};
    size_t seen[CLASSES] = {0}, hard_count[CLASSES] = {0}, want = 100000, want_hard = 1000;
    uint32_t hard_least[CLASSES] = {0}, check = 0, bad = 0;
    int passes = 7, max_class = 0;
    double t0;
    FrbError e;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bomb") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--sample") && i + 1 < argc) want = (size_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--hard") && i + 1 < argc) want_hard = (size_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--passes") && i + 1 < argc) passes = atoi(argv[++i]);
        else path = NULL, i = argc;
    }
    if (!path || want < 1 || want_hard < 1 || passes < 1 || passes > MAX_PASSES) {
        fprintf(stderr, "usage: colorless_bench --bomb FILE [--sample N] [--hard H] [--passes 1..%d]\n", MAX_PASSES);
        return 2;
    }
    e = frb_file_read(path, &table, &info, &bad);
    if (e != FRB_OK || !info.complete) {
        fprintf(stderr, "colorless_bench: %s is not a complete fixed-width Bomb\n", path);
        return 1;
    }
    L = table.bits;
    calibrate();
    printf("Bomb: FixedRunBomb(%d), %u strings, table CRC-32 %08x\n", L, (unsigned)table.count,
           (unsigned)info.table_crc);
    printf("Timer: %.4f ns per tick of the time stamp counter; %llu ticks for an empty measurement, taken off\n",
           ns_per_tick, (unsigned long long)tick_overhead);

    /* Every string once: a random sample of each node count (reservoir), and
     * the strings with the most steps. */
    t0 = cb_now();
    for (uint32_t t = 0;; t++) {
        RevGraph g;
        FrbStatus status;
        Item it;
        int n;
        if (frb_entry_decode_as(table.format, frb_table_entry(&table, t), &status, &g) != FRB_OK ||
            g.node_count > CL_MAX_NODES || g.node_count > 12)
            return 1;
        n = g.node_count;
        if (n > max_class) max_class = n;
        if (!sample[n]) {
            sample[n] = malloc(want * sizeof **sample);
            hard[n] = malloc(want_hard * sizeof **hard);
            if (!sample[n] || !hard[n]) return 1;
        }
        make_item(&it, &g, t);
        if (seen[n] < want) sample[n][seen[n]] = it;
        else {
            uint64_t j = rnd64() % (uint64_t)(seen[n] + 1);
            if (j < want) sample[n][j] = it;
        }
        seen[n]++;
        if (hard_count[n] < want_hard) {
            hard[n][hard_count[n]++] = it;
            if (hard_count[n] == want_hard) {
                hard_least[n] = ~0u;
                for (size_t i = 0; i < want_hard; i++)
                    if (hard[n][i].states < hard_least[n]) hard_least[n] = hard[n][i].states;
            }
        } else if (it.states > hard_least[n]) {
            size_t at = 0;
            uint32_t least = ~0u;
            for (size_t i = 0; i < want_hard; i++)
                if (hard[n][i].states == hard_least[n]) {
                    at = i;
                    break;
                }
            hard[n][at] = it;
            for (size_t i = 0; i < want_hard; i++)
                if (hard[n][i].states < least) least = hard[n][i].states;
            hard_least[n] = least;
        }
        if (t == table.count - 1) break;
    }
    printf("Every string recolored once to choose the samples: %.1f s\n\n", cb_now() - t0);

    for (int set = 0; set < 2; set++) {
        printf(set == 0 ? "Random sample of each node count: nanoseconds for one REV\n"
                        : "\nThe strings with the most recoloring steps of each node count: nanoseconds for one REV\n");
        printf("  Nodes     REVs  Steps mean/max  Operation     mean*     mean      p50      p95      p99    p99.9      max   x verify\n");
        for (int n = 1; n <= max_class; n++) {
            const Item *items = set == 0 ? sample[n] : hard[n];
            size_t count = set == 0 ? (seen[n] < want ? seen[n] : want) : hard_count[n];
            double steps = 0.0, verify_mean = 1.0;
            uint32_t most = 0;
            if (!count || n < 5) continue; /* below 5 nodes a handful of strings each */
            for (size_t i = 0; i < count; i++) {
                steps += items[i].states;
                if (items[i].states > most) most = items[i].states;
            }
            for (int op = 0; op < OPS; op++) {
                Times r;
                if (set == 1 && (op == OP_FIELDS || op == OP_BASE_N || op == OP_RANK || op == OP_REFERENCE)) continue;
                r = time_op((Op)op, items, count, passes, &check);
                if (op == OP_VERIFY) verify_mean = r.mean;
                if (op == OP_VERIFY) printf("  %5d  %7zu  %7.1f / %-4u", n, count, steps / (double)count, (unsigned)most);
                else printf("  %5s  %7s  %14s", "", "", "");
                printf("  %-10s", op_name[op]);
                if (op == OP_REFERENCE) printf("  %8s", "-");
                else printf("  %8.1f", r.mean_loop);
                printf(" %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f   %6.2f\n", r.mean, r.p50, r.p95, r.p99, r.p999, r.max,
                       r.mean / verify_mean);
            }
        }
    }
    printf("\n  mean*: one clock around a whole pass, no timer inside.  The other columns: the time stamp\n"
           "  counter around one call, median of %d passes for each REV.  (check %08x)\n",
           passes, (unsigned)check);
    for (int n = 0; n < CLASSES; n++) {
        free(sample[n]);
        free(hard[n]);
    }
    frb_table_destroy(&table);
    return 0;
}
