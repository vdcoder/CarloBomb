/*
 * v4_matrix: the candidate layouts for the compact Bomb, as files, measured
 * side by side.
 *
 *   v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants short|all|LIST]
 *                 [--queries N] [--threads 1,2,4] [--cold N] [--checkpoint B] [--keep] [--no-verify]
 *   v4_matrix convert --bomb FILE --output FILE --variant NAME [--checkpoint B]
 *
 * FILE of --bomb is a complete fixed-width Bomb.  It is opened for reading
 * and never written.  For each variant `run` makes a prototype file (CBX,
 * see cbx.h) in DIR, in a process of its own so that its time and memory can
 * be read off; checks it against the Bomb for every string of every length;
 * measures lookups; and removes the file again unless --keep is given.
 * Nothing else is written anywhere.
 *
 * A variant is three choices, written as a name like "rank,colorless,half":
 *   fields | base-n | rank     how the links of a REV are written
 *   colors | colorless         colors kept, or found by search at lookup
 *   whole | half               every string, or one of each complement pair
 * "short" is the shortlist under review, "all" the twelve combinations.
 *
 * What is measured for each (and for the fixed-width file itself, and for
 * the compact file of PR 8 if --compact names it):
 *
 *   size; the time and peak memory of the conversion;
 *   that every string of L bits gets its node count and a REV that emits it,
 *   how many get a REV other than the stored one and how many of those
 *   predict another next bit, and that every shorter string is answered by
 *   the same extension as before;
 *   node count of a string, and "which of the two extensions needs fewer
 *   nodes" (both from the index alone);
 *   the verified REV of a string of L bits and of a shorter string: mean,
 *   p50, p95 and p99 of single lookups;
 *   the same with several readers at once on one mapping: lookups per
 *   second, and the latencies;
 *   lookups with the file out of the page cache.
 *
 * Times are of the machine it runs on.  Run it on a quiet machine: on one
 * that is building a ladder the numbers mean little, and it takes cores the
 * ladder would use.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include "timer.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cbx.h"
#include "compact.h"

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

static double ns_per_tick = 1.0;
static uint64_t tick_overhead;

static void calibrate(void) {
    uint64_t least = ~(uint64_t)0, a, b;
    double t0 = cb_now(), t1;
    a = tick_begin();
    do t1 = cb_now();
    while (t1 - t0 < 0.2);
    b = tick_end();
    ns_per_tick = (t1 - t0) * 1e9 / (double)(b - a);
    for (int i = 0; i < 100000; i++) {
        a = tick_begin();
        b = tick_end();
        if (b - a < least) least = b - a;
    }
    tick_overhead = least;
}

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

/* ---- variants ------------------------------------------------------------------ */

typedef struct {
    char name[40];
    CbxOptions opt;
} Variant;

static int parse_variant(const char *text, int checkpoint_shift, Variant *v) {
    char copy[64], *tok, *save = NULL;
    int have = 0;
    memset(v, 0, sizeof *v);
    v->opt.checkpoint_shift = checkpoint_shift;
    v->opt.colors = 1;
    snprintf(copy, sizeof copy, "%s", text);
    for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (!strcmp(tok, "fields")) v->opt.encoding = CBX_FIELDS, have |= 1;
        else if (!strcmp(tok, "base-n")) v->opt.encoding = CBX_BASE_N, have |= 1;
        else if (!strcmp(tok, "rank")) v->opt.encoding = CBX_RANK, have |= 1;
        else if (!strcmp(tok, "colors")) v->opt.colors = 1;
        else if (!strcmp(tok, "colorless")) v->opt.colors = 0;
        else if (!strcmp(tok, "whole")) v->opt.complement = 0;
        else if (!strcmp(tok, "half")) v->opt.complement = 1;
        else return 0;
    }
    snprintf(v->name, sizeof v->name, "%s,%s,%s", cbx_encoding_str(v->opt.encoding),
             v->opt.colors ? "colors" : "colorless", v->opt.complement ? "half" : "whole");
    return have;
}

static const char *const shortlist[] = {"fields,colors,whole",    "fields,colors,half",    "base-n,colors,half",
                                        "rank,colors,whole",      "rank,colors,half",      "fields,colorless,half",
                                        "base-n,colorless,half",  "rank,colorless,whole",  "rank,colorless,half"};
static const char *const everything[] = {"fields,colors,whole",    "fields,colors,half",    "base-n,colors,whole",
                                         "base-n,colors,half",     "rank,colors,whole",     "rank,colors,half",
                                         "fields,colorless,whole", "fields,colorless,half", "base-n,colorless,whole",
                                         "base-n,colorless,half",  "rank,colorless,whole",  "rank,colorless,half"};

/* ---- conversion, in a process of its own ------------------------------------------ */

static long status_kb(const char *key) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long v = -1;
    size_t n = strlen(key);
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, n)) v = atol(line + n);
    fclose(f);
    return v;
}

static int cmd_convert(const char *bomb, const char *output, const char *variant_text, int checkpoint_shift) {
    Variant v;
    FrbFileInfo finfo;
    FrbTable table;
    CbxInfo info;
    uint32_t bad = 0;
    double t0 = cb_now(), t1, t2;
    FrbError fe;
    CbxError e;
    if (!bomb || !output || !variant_text || !parse_variant(variant_text, checkpoint_shift, &v)) {
        fprintf(stderr, "usage: v4_matrix convert --bomb FILE --output FILE --variant fields|base-n|rank[,colors|colorless][,whole|half]\n");
        return 2;
    }
    fe = frb_file_read(bomb, &table, &finfo, &bad); /* read only; validated in full, every REV run */
    if (fe != FRB_OK || !finfo.complete) {
        fprintf(stderr, "v4_matrix: %s is not a complete fixed-width Bomb (%s)\n", bomb,
                fe == FRB_OK ? "it is partial" : frb_error_str(fe));
        return 1;
    }
    t1 = cb_now();
    e = cbx_write(output, &table, finfo.table_crc, finfo.version, &v.opt, &info);
    t2 = cb_now();
    frb_table_destroy(&table);
    if (e != CBX_OK) {
        fprintf(stderr, "v4_matrix: cannot make %s: %s\n", output, cbx_error_str(e));
        printf("converted 0 error %d\n", (int)e);
        return e == CBX_ERR_NOT_SYMMETRIC || e == CBX_ERR_NUMBERING ? 3 : 1;
    }
    /* one line for `run` to read, then the same for a person */
    printf("converted 1 bytes %llu validate_s %.3f build_s %.3f own_kb %llu peak_kb %ld anon_peak_kb %ld\n",
           (unsigned long long)info.file_bytes, t1 - t0, t2 - t1,
           (unsigned long long)((info.file_bytes + info.strings) / 1024), status_kb("VmHWM:"), status_kb("RssAnon:"));
    printf("%s: %s bytes from %s (%s); source validated in %.1f s, file built and written in %.1f s\n", v.name,
           commas(info.file_bytes), bomb, output, t1 - t0, t2 - t1);
    printf("  one bit per shorter string %s, node counts %s, checkpoints %s, REVs %s bytes\n",
           commas(info.direction_bytes), commas(info.classes_bytes), commas(info.checkpoint_bytes),
           commas(info.pool_bytes));
    return 0;
}

typedef struct {
    int ok, error;
    uint64_t bytes, own_kb;
    double validate_s, build_s, wall_s;
    long peak_kb;
} Conversion;

/* Run `convert` as a child and read what it reports and what the system
 * says it used. */
static Conversion convert_in_child(const char *self, const char *bomb, const char *output, const Variant *v) {
    Conversion r;
    char shift[16], line[512];
    int fds[2];
    pid_t pid;
    double t0 = cb_now();
    memset(&r, 0, sizeof r);
    snprintf(shift, sizeof shift, "%d", 1 << v->opt.checkpoint_shift);
    if (pipe(fds) != 0) return r;
    pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        execl(self, self, "convert", "--bomb", bomb, "--output", output, "--variant", v->name, "--checkpoint", shift,
              (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    if (pid > 0) {
        FILE *in = fdopen(fds[0], "r");
        struct rusage ru;
        int status = 0;
        while (in && fgets(line, sizeof line, in)) {
            unsigned long long bytes = 0, own = 0;
            long peak = 0, anon = 0;
            int ok = 0, err = 0;
            if (sscanf(line, "converted 1 bytes %llu validate_s %lf build_s %lf own_kb %llu peak_kb %ld anon_peak_kb %ld",
                       &bytes, &r.validate_s, &r.build_s, &own, &peak, &anon) == 6) {
                r.ok = 1;
                r.bytes = bytes;
                r.own_kb = own;
            } else if (sscanf(line, "converted %d error %d", &ok, &err) == 2) {
                r.error = err;
            }
        }
        if (in) fclose(in);
        memset(&ru, 0, sizeof ru);
        wait4(pid, &status, 0, &ru);
        r.peak_kb = ru.ru_maxrss;
        if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 3)) r.ok = 0;
    } else {
        close(fds[0]);
    }
    r.wall_s = cb_now() - t0;
    return r;
}

/* ---- what is looked up -------------------------------------------------------------- */

typedef struct {
    uint32_t value;
    uint8_t bits;
} Query;

typedef enum { KIND_CBX, KIND_FIXED, KIND_COMPACT } Kind;

typedef struct {
    Kind kind;
    const Cbx *cbx;
    const FrbTable *table;
    const FrbCompact *compact;
    int L;
} Source;

/* A fixed-width entry: n nodes of 1 + 2w bits, the node count in the last byte. */
static inline int entry_unpack(const FrbTable *table, uint32_t index, ClTopo *t, uint32_t *colors) {
    const uint8_t *e = frb_table_entry(table, index);
    int w = table->format->link_bits, width = table->format->node_bits;
    int n = e[table->format->graph_bytes] & 31;
    uint32_t c = 0, mask = (1u << w) - 1u;
    t->n = (uint8_t)n;
    for (int i = 0; i < n && i < CL_MAX_NODES; i++) {
        unsigned bit = (unsigned)(i * width);
        uint32_t v = (uint32_t)e[bit >> 3] | ((uint32_t)e[(bit >> 3) + 1] << 8) | ((uint32_t)e[(bit >> 3) + 2] << 16);
        v >>= bit & 7u;
        c |= (v & 1u) << i;
        t->link[0][i] = (uint8_t)((v >> 1) & mask);
        t->link[1][i] = (uint8_t)((v >> (1 + w)) & mask);
    }
    *colors = c;
    return n;
}

/* The verified REV of a string: 1 if one was found and it emits the string. */
static inline int rev_of(const Source *s, int bits, uint32_t value, uint32_t *fold) {
    ClTopo t;
    uint32_t colors = 0;
    int ok;
    switch (s->kind) {
    case KIND_CBX: ok = cbx_lookup(s->cbx, bits, value, &t, &colors, NULL); break;
    case KIND_FIXED:
        entry_unpack(s->table, value, &t, &colors);
        ok = 1;
        break;
    default: {
        RevGraph g;
        ok = frb_compact_graph(s->compact, bits, value, &g) == FRB_OK && cl_from_graph(&g, &t, &colors);
        break;
    }
    }
    ok = ok && cl_verify(&t, colors, value, bits);
    *fold += colors + ((uint32_t)t.n << 16) + t.link[0][0] + 5u * t.link[1][t.n - 1];
    return ok;
}

static inline int nodes_of(const Source *s, int bits, uint32_t value) {
    switch (s->kind) {
    case KIND_CBX: return cbx_nodes(s->cbx, bits, value);
    case KIND_FIXED: return frb_table_nodes(s->table, value);
    default: return frb_compact_nodes(s->compact, bits, value);
    }
}

typedef struct {
    double mean, p50, p95, p99;
    unsigned long long failed;
} Latency;

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static Latency summarize(uint32_t *ticks, size_t n, unsigned long long failed) {
    Latency r;
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += ticks[i];
    qsort(ticks, n, sizeof *ticks, cmp_u32);
    r.mean = sum / (double)n * ns_per_tick;
    r.p50 = ticks[n / 2] * ns_per_tick;
    r.p95 = ticks[(size_t)((double)(n - 1) * 0.95)] * ns_per_tick;
    r.p99 = ticks[(size_t)((double)(n - 1) * 0.99)] * ns_per_tick;
    r.failed = failed;
    return r;
}

/* Every lookup timed by itself. */
static unsigned long long time_each(const Source *s, const Query *q, size_t n, uint32_t *ticks, uint32_t *fold_out) {
    unsigned long long failed = 0;
    uint32_t fold = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t a = tick_begin(), b;
        failed += !rev_of(s, q[i].bits, q[i].value, &fold);
        b = tick_end();
        ticks[i] = (uint32_t)(b - a > tick_overhead ? b - a - tick_overhead : 0);
    }
    *fold_out = fold;
    return failed;
}

typedef struct {
    const Source *source;
    const Query *q;
    size_t n;
    uint32_t *ticks;
    uint32_t fold;
    unsigned long long failed;
    pthread_barrier_t *start;
} Reader;

static void *reader_main(void *arg) {
    Reader *r = arg;
    pthread_barrier_wait(r->start);
    r->failed = time_each(r->source, r->q, r->n, r->ticks, &r->fold);
    return NULL;
}

/* T readers at once on the same source; returns lookups per second. */
static double concurrent(const Source *s, const Query *q, size_t n, int threads, Latency *lat) {
    pthread_t ids[64];
    Reader readers[64];
    pthread_barrier_t start;
    uint32_t *ticks = malloc(n * sizeof *ticks);
    size_t each = n / (size_t)threads;
    unsigned long long failed = 0;
    double t0, wall;
    if (!ticks || threads < 1 || threads > 64 || each < 1) {
        free(ticks);
        memset(lat, 0, sizeof *lat);
        return 0.0;
    }
    pthread_barrier_init(&start, NULL, (unsigned)threads + 1);
    for (int t = 0; t < threads; t++) {
        readers[t].source = s;
        readers[t].q = q + (size_t)t * each;
        readers[t].n = each;
        readers[t].ticks = ticks + (size_t)t * each;
        readers[t].fold = 0;
        readers[t].failed = 0;
        readers[t].start = &start;
        pthread_create(&ids[t], NULL, reader_main, &readers[t]);
    }
    t0 = cb_now();
    pthread_barrier_wait(&start);
    for (int t = 0; t < threads; t++) {
        pthread_join(ids[t], NULL);
        failed += readers[t].failed;
    }
    wall = cb_now() - t0;
    pthread_barrier_destroy(&start);
    *lat = summarize(ticks, each * (size_t)threads, failed);
    free(ticks);
    return (double)(each * (size_t)threads) / wall;
}

static int evict(const char *path) {
    int fd = open(path, O_RDONLY), ok;
    if (fd < 0) return 0;
    ok = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* ---- one row of the matrix -------------------------------------------------------- */

typedef struct {
    char name[40];
    uint64_t bytes;
    Conversion conversion;
    CbxCheck check;
    int checked;
    double nodes_full, nodes_short, choice; /* ns, mean */
    Latency full, shorter;
    int thread_counts[8], thread_rows;
    double throughput[8];
    Latency thread_latency[8];
    double cold_all_p50, cold_all_p99, cold_index_s, cold_rev_p50, cold_rev_p99; /* microseconds, seconds */
    int cold_done, lookups_failed;
} Row;

static void measure(Row *row, const Source *s, const Query *exact, const Query *prefix, size_t queries,
                    const int *thread_list, int thread_count) {
    uint32_t *ticks = malloc(queries * sizeof *ticks), fold = 0;
    unsigned long long sink = 0, failed;
    double best;
    int L = s->L;
    if (!ticks) return;

    /* node counts and the choice between two extensions: index only */
    best = 1e30;
    for (int run = 0; run < 3; run++) {
        double t0 = cb_now();
        for (size_t i = 0; i < queries; i++) sink += (unsigned)nodes_of(s, L, exact[i].value);
        t0 = cb_now() - t0;
        if (t0 < best) best = t0;
    }
    row->nodes_full = best / (double)queries * 1e9;
    row->nodes_short = row->choice = -1.0;
    if (s->kind != KIND_FIXED) {
        best = 1e30;
        for (int run = 0; run < 3; run++) {
            double t0 = cb_now();
            for (size_t i = 0; i < queries; i++) sink += (unsigned)nodes_of(s, prefix[i].bits, prefix[i].value);
            t0 = cb_now() - t0;
            if (t0 < best) best = t0;
        }
        row->nodes_short = best / (double)queries * 1e9;
        best = 1e30;
        for (int run = 0; run < 3; run++) {
            double t0 = cb_now();
            for (size_t i = 0; i < queries; i++) { /* the two extensions of a string of 0 to L-1 bits */
                int bits = prefix[i].bits - 1;
                uint32_t value = prefix[i].value >> 1;
                int n0 = nodes_of(s, bits + 1, 2 * value), n1 = nodes_of(s, bits + 1, 2 * value + 1);
                sink += (unsigned)(n0 < n1 ? 0 : n1 < n0 ? 1 : 2);
            }
            t0 = cb_now() - t0;
            if (t0 < best) best = t0;
        }
        row->choice = best / (double)queries * 1e9;
    }

    /* verified REVs, one lookup at a time; the second pass is the one kept */
    failed = time_each(s, exact, queries, ticks, &fold);
    failed = time_each(s, exact, queries, ticks, &fold);
    row->full = summarize(ticks, queries, failed);
    row->lookups_failed += failed != 0;
    memset(&row->shorter, 0, sizeof row->shorter);
    if (s->kind != KIND_FIXED) {
        failed = time_each(s, prefix, queries, ticks, &fold);
        failed = time_each(s, prefix, queries, ticks, &fold);
        row->shorter = summarize(ticks, queries, failed);
        row->lookups_failed += failed != 0;
    }
    for (int k = 0; k < thread_count && k < 8; k++) {
        row->thread_counts[k] = thread_list[k];
        row->throughput[k] = concurrent(s, exact, queries, thread_list[k], &row->thread_latency[k]);
        row->lookups_failed += row->thread_latency[k].failed != 0;
        row->thread_rows = k + 1;
    }
    free(ticks);
    if (sink == 1 && fold == 1) row->lookups_failed += 0; /* the results were used */
}

/* Lookups with the file out of the page cache: first with nothing in memory,
 * then with the index read in and only the REVs cold. */
static void measure_cold(Row *row, const char *path, const Query *exact, size_t lookups) {
    Cbx c;
    Source s;
    double *us = malloc(lookups * sizeof *us), t0;
    uint32_t fold = 0;
    volatile uint64_t touch = 0;
    if (!us) return;
    evict(path);
    if (cbx_open(path, &c) != CBX_OK) {
        free(us);
        return;
    }
    madvise((void *)(uintptr_t)c.base, c.mapped, MADV_RANDOM);
    s.kind = KIND_CBX;
    s.cbx = &c;
    s.L = c.info.bits;
    for (size_t i = 0; i < lookups; i++) {
        t0 = cb_now();
        rev_of(&s, s.L, exact[i].value, &fold);
        us[i] = (cb_now() - t0) * 1e6;
    }
    qsort(us, lookups, sizeof *us, cmp_double);
    row->cold_all_p50 = us[lookups / 2];
    row->cold_all_p99 = us[(size_t)((double)(lookups - 1) * 0.99)];
    cbx_close(&c);

    evict(path);
    if (cbx_open(path, &c) != CBX_OK) {
        free(us);
        return;
    }
    s.cbx = &c;
    t0 = cb_now(); /* the index: everything before the REVs */
    for (uint64_t at = 0; at < c.info.pool_offset[1]; at += 4096) touch += c.base[at];
    row->cold_index_s = cb_now() - t0;
    madvise((void *)(uintptr_t)(c.base + c.info.pool_offset[1]), (size_t)(c.info.file_bytes - c.info.pool_offset[1]),
            MADV_RANDOM);
    for (size_t i = 0; i < lookups; i++) {
        t0 = cb_now();
        rev_of(&s, s.L, exact[lookups + i].value, &fold);
        us[i] = (cb_now() - t0) * 1e6;
    }
    qsort(us, lookups, sizeof *us, cmp_double);
    row->cold_rev_p50 = us[lookups / 2];
    row->cold_rev_p99 = us[(size_t)((double)(lookups - 1) * 0.99)];
    row->cold_done = 1;
    cbx_close(&c);
    free(us);
    (void)touch;
}

static void print_row_detail(const Row *r, uint64_t fixed_bytes, int L) {
    printf("\n== %s ==\n", r->name);
    printf("  size                 %s bytes, %.1f%% of the fixed-width file\n", commas(r->bytes),
           100.0 * (double)r->bytes / (double)fixed_bytes);
    if (r->conversion.ok)
        printf("  conversion           %.1f s in all (source validated %.1f s, file built and written %.1f s);\n"
               "                       peak memory %.0f MiB with the mapped source, %.0f MiB of its own\n",
               r->conversion.wall_s, r->conversion.validate_s, r->conversion.build_s,
               (double)r->conversion.peak_kb / 1024.0, (double)r->conversion.own_kb / 1024.0);
    if (r->checked)
        printf("  every string         %s of %d bits: %s wrong node counts, %s without a REV that emits them,\n"
               "                       %s with a REV other than the stored one (%s of them predict another next bit);\n"
               "                       %s shorter strings: %s answered by another extension\n",
               commas(r->check.strings), L, commas(r->check.wrong_nodes), commas(r->check.not_emitting),
               commas(r->check.not_stored), commas(r->check.next_bit_differs), commas(r->check.shorter),
               commas(r->check.wrong_extension));
    if (r->checked && r->check.not_stored) {
        printf("                       REVs other than the stored one, by node count:");
        for (int n = 1; n < CBX_CLASSES; n++)
            if (r->check.not_stored_by_nodes[n]) printf("  %d: %s", n, commas(r->check.not_stored_by_nodes[n]));
        printf("\n");
    }
    printf("  node count           %.1f ns for a string of %d bits", r->nodes_full, L);
    if (r->nodes_short >= 0)
        printf(", %.1f ns for a shorter one;\n  smaller extension    %.1f ns (which of the two extensions of a string needs fewer nodes)\n",
               r->nodes_short, r->choice);
    else printf("\n");
    printf("  verified REV         %d bits: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", L, r->full.mean, r->full.p50,
           r->full.p95, r->full.p99);
    if (r->shorter.mean > 0)
        printf("\n                       shorter: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", r->shorter.mean,
               r->shorter.p50, r->shorter.p95, r->shorter.p99);
    printf("\n");
    for (int k = 0; k < r->thread_rows; k++)
        printf("  %2d reader%s           %.2f million lookups a second; p50 %.0f, p95 %.0f, p99 %.0f ns\n",
               r->thread_counts[k], r->thread_counts[k] == 1 ? " " : "s", r->throughput[k] / 1e6,
               r->thread_latency[k].p50, r->thread_latency[k].p95, r->thread_latency[k].p99);
    if (r->cold_done)
        printf("  out of the cache     nothing in memory: p50 %.0f, p99 %.0f us a lookup; index read in %.3f s, then\n"
               "                       p50 %.0f, p99 %.0f us a lookup\n",
               r->cold_all_p50, r->cold_all_p99, r->cold_index_s, r->cold_rev_p50, r->cold_rev_p99);
    if (r->lookups_failed) printf("  LOOKUPS FAILED in the timed runs\n");
    fflush(stdout);
}

static int cmd_run(const char *self, const char *bomb, const char *scratch, const char *compact_path,
                   const char *variants, size_t queries, const int *thread_list, int thread_count, size_t cold,
                   int checkpoint_shift, int keep, int verify) {
    Row *rows;
    Variant list[16];
    FrbFileInfo finfo;
    FrbTable table;
    FrbCompact *compact = NULL;
    Query *exact, *prefix;
    Source s;
    uint64_t rng = 0x9E3779B97F4A7C15ull, fixed_bytes;
    uint32_t bad = 0;
    int count = 0, L, failed = 0, nrows = 0;
    double t0;
    FrbError fe;

    if (!bomb || !scratch) {
        fprintf(stderr, "usage: v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants short|all|LIST]\n"
                        "                     [--queries N] [--threads 1,2,4] [--cold N] [--checkpoint B] [--keep] [--no-verify]\n");
        return 2;
    }
    if (!variants || !strcmp(variants, "short")) {
        for (size_t i = 0; i < sizeof shortlist / sizeof shortlist[0]; i++)
            parse_variant(shortlist[i], checkpoint_shift, &list[count++]);
    } else if (!strcmp(variants, "all")) {
        for (size_t i = 0; i < sizeof everything / sizeof everything[0]; i++)
            parse_variant(everything[i], checkpoint_shift, &list[count++]);
    } else {
        char copy[512], *tok, *save = NULL;
        snprintf(copy, sizeof copy, "%s", variants);
        for (tok = strtok_r(copy, ";", &save); tok && count < 16; tok = strtok_r(NULL, ";", &save))
            if (!parse_variant(tok, checkpoint_shift, &list[count++])) {
                fprintf(stderr, "v4_matrix: no such variant: %s\n", tok);
                return 2;
            }
    }
    rows = calloc((size_t)count + 2, sizeof *rows);
    exact = malloc((queries + 2 * cold + 16) * sizeof *exact);
    prefix = malloc(queries * sizeof *prefix);
    if (!rows || !exact || !prefix) return 1;

    t0 = cb_now();
    fe = frb_file_read(bomb, &table, &finfo, &bad);
    if (fe != FRB_OK || !finfo.complete) {
        fprintf(stderr, "v4_matrix: %s is not a complete fixed-width Bomb (%s)\n", bomb,
                fe == FRB_OK ? "it is partial" : frb_error_str(fe));
        return 1;
    }
    L = table.bits;
    fixed_bytes = frb_file_bytes(table.format, L);
    calibrate();
    printf("Bomb: FixedRunBomb(%d), %s strings, %s bytes, table CRC-32 %08x; validated in full in %.1f s\n", L,
           commas(table.count), commas(fixed_bytes), (unsigned)finfo.table_crc, cb_now() - t0);
    printf("%zu random strings per measurement; a checkpoint every %d strings; %ld processors here\n", queries,
           1 << checkpoint_shift, sysconf(_SC_NPROCESSORS_ONLN));
    for (size_t i = 0; i < queries + 2 * cold; i++) {
        rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
        exact[i].bits = (uint8_t)L;
        exact[i].value = (uint32_t)((rng * 0x2545F4914F6CDD1Dull) >> 33) & (table.count - 1u);
    }
    for (size_t i = 0; i < queries; i++) { /* a shorter string: 1 to L-1 bits, every length as likely */
        rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
        prefix[i].bits = (uint8_t)(1u + (unsigned)(((rng * 0x2545F4914F6CDD1Dull) >> 33) % (uint64_t)(unsigned)(L - 1)));
        rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
        prefix[i].value = (uint32_t)((rng * 0x2545F4914F6CDD1Dull) >> 33) & (((uint32_t)1 << prefix[i].bits) - 1u);
    }

    /* the fixed-width file itself */
    snprintf(rows[nrows].name, sizeof rows[nrows].name, "fixed-width file");
    rows[nrows].bytes = fixed_bytes;
    s.kind = KIND_FIXED;
    s.table = &table;
    s.cbx = NULL;
    s.compact = NULL;
    s.L = L;
    measure(&rows[nrows], &s, exact, prefix, queries, thread_list, thread_count);
    print_row_detail(&rows[nrows], fixed_bytes, L);
    nrows++;

    /* the compact file of PR 8, if there is one */
    if (compact_path) {
        if (frb_compact_open(compact_path, &compact, &bad) != FRB_OK || frb_compact_info(compact)->bits != L) {
            fprintf(stderr, "v4_matrix: %s is not the compact file of this Bomb\n", compact_path);
            return 1;
        }
        snprintf(rows[nrows].name, sizeof rows[nrows].name, "compact file of PR 8");
        rows[nrows].bytes = frb_compact_info(compact)->file_bytes;
        s.kind = KIND_COMPACT;
        s.compact = compact;
        measure(&rows[nrows], &s, exact, prefix, queries, thread_list, thread_count);
        print_row_detail(&rows[nrows], fixed_bytes, L);
        nrows++;
        frb_compact_close(compact);
    }

    for (int k = 0; k < count; k++) {
        char path[1200];
        Row *row = &rows[nrows];
        Cbx c;
        CbxError e;
        snprintf(path, sizeof path, "%s/v4-%d-%s.cbx", scratch, L, list[k].name);
        memcpy(row->name, list[k].name, sizeof row->name);
        remove(path); /* a file of an earlier run of this program */
        row->conversion = convert_in_child(self, bomb, path, &list[k]);
        if (!row->conversion.ok) {
            printf("\n== %s ==\n  not made: %s\n", row->name,
                   row->conversion.error ? cbx_error_str((CbxError)row->conversion.error) : "the conversion failed");
            failed |= row->conversion.error != CBX_ERR_NOT_SYMMETRIC && row->conversion.error != CBX_ERR_NUMBERING;
            continue;
        }
        e = cbx_open(path, &c);
        if (e != CBX_OK) {
            printf("\n== %s ==\n  cannot open %s: %s\n", row->name, path, cbx_error_str(e));
            failed = 1;
            continue;
        }
        row->bytes = c.info.file_bytes;
        if (verify) {
            e = cbx_check_body(&c);
            if (e == CBX_OK) e = cbx_check_against(&c, &table, &row->check);
            row->checked = e == CBX_OK;
            if (e != CBX_OK || row->check.wrong_nodes || row->check.not_emitting || row->check.wrong_extension ||
                (list[k].opt.colors && row->check.not_stored))
                failed = 1;
        }
        s.kind = KIND_CBX;
        s.cbx = &c;
        measure(row, &s, exact, prefix, queries, thread_list, thread_count);
        cbx_close(&c);
        if (cold) measure_cold(row, path, exact + queries, cold);
        failed |= row->lookups_failed;
        print_row_detail(row, fixed_bytes, L);
        if (!keep) remove(path);
        nrows++;
    }

    /* ---- the matrix ---- */
    printf("\n\nThe matrix (times in nanoseconds unless said otherwise)\n\n");
    printf("  %-24s %12s %6s  %7s %7s  %9s %8s  %6s %6s  %5s %5s %5s %5s  %5s %5s %5s %5s", "", "bytes", "of fix",
           "conv s", "MiB", "not stored", "next bit", "nodes", "choice", "REV", "p50", "p95", "p99", "short", "p50",
           "p95", "p99");
    for (int k = 0; k < thread_count; k++) printf("  %2dx M/s", thread_list[k]);
    if (cold) printf("  cold us");
    printf("\n");
    for (int i = 0; i < nrows; i++) {
        const Row *r = &rows[i];
        if (!r->bytes) continue;
        printf("  %-24s %12s %5.1f%%", r->name, commas(r->bytes), 100.0 * (double)r->bytes / (double)fixed_bytes);
        if (r->conversion.ok) printf("  %7.1f %7.0f", r->conversion.wall_s, (double)r->conversion.peak_kb / 1024.0);
        else printf("  %7s %7s", "-", "-");
        if (r->checked) printf("  %9s %8s", commas(r->check.not_stored), commas(r->check.next_bit_differs));
        else printf("  %9s %8s", "-", "-");
        printf("  %6.1f", r->nodes_full);
        if (r->choice >= 0) printf(" %6.1f", r->choice);
        else printf(" %6s", "-");
        printf("  %5.0f %5.0f %5.0f %5.0f", r->full.mean, r->full.p50, r->full.p95, r->full.p99);
        if (r->shorter.mean > 0) printf("  %5.0f %5.0f %5.0f %5.0f", r->shorter.mean, r->shorter.p50, r->shorter.p95, r->shorter.p99);
        else printf("  %5s %5s %5s %5s", "-", "-", "-", "-");
        for (int k = 0; k < r->thread_rows; k++) printf("  %7.2f", r->throughput[k] / 1e6);
        if (cold) {
            if (r->cold_done) printf("  %7.0f", r->cold_rev_p50);
            else printf("  %7s", "-");
        }
        printf("\n");
    }
    printf("\n  conv s, MiB: the conversion from the fixed-width file, with its validation; peak memory with the mapped source.\n"
           "  not stored, next bit: strings of %d bits that get a REV other than the stored one, and of those the ones whose\n"
           "  REV predicts another bit %d.  nodes: node count of a %d-bit string.  choice: which of the two extensions of a\n"
           "  shorter string needs fewer nodes.  REV: verified REV of a %d-bit string; short: of a string of 1 to %d bits.\n"
           "  Nx M/s: million verified REVs a second with N readers.  cold us: one lookup with the REVs out of the cache\n"
           "  and the index in memory, median microseconds.\n",
           L, L + 1, L, L, L - 1);
    printf("\n%s\n", failed ? "FAIL: a variant did not give what the Bomb gives" : "PASS");
    frb_table_destroy(&table);
    free(rows);
    free(exact);
    free(prefix);
    return failed;
}

int main(int argc, char **argv) {
    const char *bomb = NULL, *scratch = NULL, *compact = NULL, *variants = NULL, *output = NULL, *variant = NULL;
    char self[4096];
    size_t queries = 1000000, cold = 2000;
    int thread_list[8] = {1, 2, 4}, thread_count = 3, checkpoint_shift = 8, keep = 0, verify = 1;
    ssize_t len;
    if (argc < 2) goto usage;
    for (int i = 2; i < argc; i++) {
        const char *k = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(k, "--keep")) keep = 1;
        else if (!strcmp(k, "--no-verify")) verify = 0;
        else if (!v) goto usage;
        else {
            i++;
            if (!strcmp(k, "--bomb")) bomb = v;
            else if (!strcmp(k, "--scratch")) scratch = v;
            else if (!strcmp(k, "--compact")) compact = v;
            else if (!strcmp(k, "--variants")) variants = v;
            else if (!strcmp(k, "--output")) output = v;
            else if (!strcmp(k, "--variant")) variant = v;
            else if (!strcmp(k, "--queries")) queries = (size_t)strtoull(v, NULL, 10);
            else if (!strcmp(k, "--cold")) cold = (size_t)strtoull(v, NULL, 10);
            else if (!strcmp(k, "--checkpoint")) {
                int b = atoi(v);
                checkpoint_shift = 0;
                while ((1 << checkpoint_shift) < b) checkpoint_shift++;
                if (b < 16 || b > 4096 || (1 << checkpoint_shift) != b) goto usage;
            } else if (!strcmp(k, "--threads")) {
                char copy[128], *tok, *save = NULL;
                snprintf(copy, sizeof copy, "%s", v);
                thread_count = 0;
                for (tok = strtok_r(copy, ",", &save); tok && thread_count < 8; tok = strtok_r(NULL, ",", &save)) {
                    int t = atoi(tok);
                    if (t < 1 || t > 64) goto usage;
                    thread_list[thread_count++] = t;
                }
            } else goto usage;
        }
    }
    if (queries < 1000) queries = 1000;
    if (!strcmp(argv[1], "convert")) return cmd_convert(bomb, output, variant, checkpoint_shift);
    if (!strcmp(argv[1], "run")) {
        len = readlink("/proc/self/exe", self, sizeof self - 1);
        if (len > 0) self[len] = '\0';
        else snprintf(self, sizeof self, "%s", argv[0]);
        return cmd_run(self, bomb, scratch, compact, variants, queries, thread_list, thread_count, cold,
                       checkpoint_shift, keep, verify);
    }
usage:
    fprintf(stderr, "usage: v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants short|all|LIST]\n"
                    "                     [--queries N] [--threads 1,2,4] [--cold N] [--checkpoint B] [--keep] [--no-verify]\n"
                    "       v4_matrix convert --bomb FILE --output FILE --variant fields|base-n|rank[,colors|colorless][,whole|half]\n");
    return 2;
}
