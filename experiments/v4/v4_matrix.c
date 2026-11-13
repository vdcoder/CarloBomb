/*
 * v4_matrix: the candidate layouts for the compact Bomb, as files, measured
 * side by side.
 *
 *   v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants final|short|all|LIST]
 *                 [--queries N] [--rounds R] [--threads 1,2,4] [--cold N] [--checkpoint B]
 *                 [--keep] [--no-verify]
 *   v4_matrix convert --bomb FILE --output FILE --variant NAME [--checkpoint B]
 *   v4_matrix ask --file FILE --target STRING
 *   v4_matrix choices --bomb FILE
 *
 * FILE of --bomb is a complete fixed-width Bomb.  It is opened for reading
 * and never written.  For each variant `run` makes a prototype file (CBX,
 * see cbx.h) in DIR, in a process of its own so that its time and memory can
 * be read off; checks it against the Bomb for every string of every length;
 * and measures lookups, R times over, every file in turn in each round.  At
 * the end the files are removed again unless --keep is given.  Nothing else
 * is written anywhere.
 *
 * A variant is three choices, written as a name like "rank,colorless,half":
 *   fields | base-n | rank     how the links of a REV are written
 *   colors | colorless         colors kept, or found by search at lookup
 *   whole | half               every string, or one of each complement pair
 * "final" (the default) is the approved design, rank,colorless,half; "short"
 * is the shortlist that was under review, "all" the twelve combinations.  A
 * fourth part, c64 or c1024, gives a variant its own spacing of the counts
 * in B (--checkpoint sets it for the others).
 *
 * `ask` puts one string of 0s and 1s to a file made by `convert`: its node
 * count, the extension the navigation bits lead to, the comparison of its
 * two extensions, and its REV.
 *
 * `choices` prints, for every length below L, how many strings have one
 * extension by a bit that needs fewer nodes than the other.
 *
 * What is measured for each (and for the fixed-width file itself, and for
 * the compact file of PR 8 if --compact names it):
 *
 *   size; the time and peak memory of the conversion;
 *   that every string of L bits gets its node count and a REV that emits it,
 *   how many get a REV other than the stored one and how many of those
 *   predict another next bit, and that every shorter string is answered by
 *   the same extension as before;
 *   node count of a string, and the comparison on demand, "which of the two
 *   extensions needs fewer nodes, or neither" (both from the index alone);
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

/* What is timed has to be done, and done between the two readings of the
 * clock.  The answers of a loop are added up and the sums compared between
 * the files at the end, so no loop can be left out; and for a single timed
 * question HIDE makes the question unknown until the clock has been read and
 * KEEP makes the answer exist before it is read again. */
#define HIDE(x) __asm__ volatile("" : "+r"(x))
#define KEEP(x) __asm__ volatile("" : : "r"(x))

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
    char name[48];
    CbxOptions opt;
} Variant;

static int parse_variant(const char *text, int checkpoint_shift, Variant *v) {
    char copy[64], *tok, *save = NULL;
    int have = 0, own = 0;
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
        else if (tok[0] == 'c' && tok[1] >= '1' && tok[1] <= '9') { /* c64: a checkpoint every 64 strings */
            int b = atoi(tok + 1), shift = 0;
            while ((1 << shift) < b) shift++;
            if (b < 16 || b > 4096 || (1 << shift) != b) return 0;
            v->opt.checkpoint_shift = shift;
            own = b;
        } else return 0;
    }
    snprintf(v->name, sizeof v->name, "%s,%s,%s", cbx_encoding_str(v->opt.encoding),
             v->opt.colors ? "colors" : "colorless", v->opt.complement ? "half" : "whole");
    if (own) snprintf(v->name + strlen(v->name), sizeof v->name - strlen(v->name), ",c%d", own);
    return have;
}

static const char *const final_design[] = {"rank,colorless,half"};
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
    printf("  navigation bits %s, node counts %s, checkpoints %s, REVs %s bytes\n", commas(info.nav_bytes),
           commas(info.classes_bytes), commas(info.checkpoint_bytes), commas(info.pool_bytes));
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

/*
 * The REV of a string as the reader of the file returns it; with `verify`
 * also run against the string by cl_verify, which is no part of any reader.
 * 1 if a REV was found (and, verified, emits the string).
 *
 * What a reader does before it returns differs: the fixed-width and compact
 * files and the candidates with colors and plain links unpack a record; a
 * candidate without colors runs the machine to find them, and one with rank
 * and colors runs it to number the nodes, so for those the REV returned has
 * been seen to emit the string already and the verification is a second run.
 */
static inline int rev_of(const Source *s, int bits, uint32_t value, uint32_t *fold, int verify) {
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
    if (verify) ok = ok && cl_verify(&t, colors, value, bits);
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

/* Which of the two extensions of a string of 0 to L-1 bits needs fewer nodes: 0, 1, or 2 for neither.
 * On demand in every file: the node counts of the two extensions, compared. */
static inline int choice_of(const Source *s, int bits, uint32_t value) {
    int n0, n1;
    if (s->kind == KIND_CBX) return cbx_compare(s->cbx, bits, value, NULL);
    n0 = nodes_of(s, bits + 1, 2 * value);
    n1 = nodes_of(s, bits + 1, 2 * value + 1);
    return n0 < n1 ? 0 : n1 < n0 ? 1 : 2;
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
static unsigned long long time_each(const Source *s, const Query *q, size_t n, uint32_t *ticks, int verify,
                                    uint32_t *fold_out) {
    unsigned long long failed = 0;
    uint32_t fold = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t value = q[i].value;
        uint64_t a = tick_begin(), b;
        int ok;
        HIDE(value);
        ok = rev_of(s, q[i].bits, value, &fold, verify);
        KEEP(ok);
        KEEP(fold);
        b = tick_end();
        failed += !ok;
        ticks[i] = (uint32_t)(b - a > tick_overhead ? b - a - tick_overhead : 0);
    }
    *fold_out = fold;
    return failed;
}

/*
 * Four passes over the same strings.  The first brings the file into the
 * caches and is not counted; of the others the one with the lowest mean is
 * kept, the one the rest of the machine disturbed least.  *fold_out is what
 * one pass returned, added up.
 */
static Latency time_best(const Source *s, const Query *q, size_t n, uint32_t *ticks, int verify, uint32_t *fold_out) {
    Latency best;
    unsigned long long failed_any = 0;
    memset(&best, 0, sizeof best);
    for (int pass = 0; pass < 4; pass++) {
        uint32_t fold = 0;
        unsigned long long failed = time_each(s, q, n, ticks, verify, &fold);
        Latency now = summarize(ticks, n, failed);
        failed_any += failed;
        if (pass == 1 || (pass > 1 && now.mean < best.mean)) best = now;
        *fold_out = fold;
    }
    best.failed = failed_any;
    return best;
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
    r->failed = time_each(r->source, r->q, r->n, r->ticks, 1, &r->fold);
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
    char name[48];
    char path[1200];
    Kind kind;
    uint64_t bytes;
    double check_s;                  /* the check of every string, seconds */
    double full_low, full_high;      /* the mean of a verified L-bit lookup: the lowest and highest of the rounds */
    Conversion conversion;
    CbxCheck check;
    int checked;
    double nodes_full, nodes_short, choice; /* ns each, many in a row */
    Latency choice_alone;                   /* one at a time */
    Latency full, shorter;           /* verified REVs */
    Latency fetch_full, fetch_short; /* REVs as the reader returns them */
    /* what the timed questions answered, added up: the same for every file */
    unsigned long long sum_nodes_full, sum_nodes_short, sum_choice;
    uint32_t fold_full, fold_short;
    int thread_counts[8], thread_rows;
    double throughput[8];
    Latency thread_latency[8];
    double cold_all_p50, cold_all_p99, cold_index_s, cold_rev_p50, cold_rev_p99; /* microseconds, seconds */
    double cold_all_short_p50, cold_all_short_p99, cold_rev_short_p50, cold_rev_short_p99;
    size_t cold_lookups;
    int cold_done, lookups_failed, other_answers;
} Row;

static void better(Latency *best, const Latency *now) {
    unsigned long long failed = best->failed + now->failed;
    if (now->mean > 0 && (best->mean <= 0 || now->mean < best->mean)) *best = *now;
    best->failed = failed;
}

/*
 * A later round of the same measurements on the same file.  Of each timing
 * the quieter one is kept: the rounds go through all files in turn, so a
 * minute in which the machine was busy with something else costs every file
 * one round and no file its place in the table.
 */
static void keep_best(Row *best, const Row *now) {
    if (now->nodes_full < best->nodes_full) best->nodes_full = now->nodes_full;
    if (now->nodes_short >= 0 && now->nodes_short < best->nodes_short) best->nodes_short = now->nodes_short;
    if (now->choice >= 0 && now->choice < best->choice) best->choice = now->choice;
    better(&best->choice_alone, &now->choice_alone);
    better(&best->full, &now->full);
    better(&best->shorter, &now->shorter);
    better(&best->fetch_full, &now->fetch_full);
    better(&best->fetch_short, &now->fetch_short);
    if (now->full.mean < best->full_low) best->full_low = now->full.mean;
    if (now->full.mean > best->full_high) best->full_high = now->full.mean;
    for (int k = 0; k < best->thread_rows && k < now->thread_rows; k++)
        if (now->throughput[k] > best->throughput[k]) {
            best->throughput[k] = now->throughput[k];
            best->thread_latency[k] = now->thread_latency[k];
        }
    best->lookups_failed += now->lookups_failed;
    if (now->sum_nodes_full != best->sum_nodes_full || now->sum_nodes_short != best->sum_nodes_short ||
        now->sum_choice != best->sum_choice || now->fold_full != best->fold_full || now->fold_short != best->fold_short)
        best->lookups_failed++; /* the same questions to the same file: the same answers */
}

/* The timed questions have to get the answers the other files give: node
 * counts and comparisons always, the REVs themselves where the file returns
 * the stored ones.  `fixed` is the row of the fixed-width file, `sums` an
 * earlier row with answers for shorter strings, `revs` one that takes the
 * same extension among equals (whole or halved) and whose REVs are the stored
 * ones; either may be missing. */
static int same_answers(const Row *row, const Row *fixed, const Row *sums, const Row *revs, int as_stored) {
    if (row->sum_nodes_full != fixed->sum_nodes_full) return 0;
    if (as_stored && row->fold_full != fixed->fold_full) return 0;
    if (sums && (row->sum_nodes_short != sums->sum_nodes_short || row->sum_choice != sums->sum_choice)) return 0;
    if (revs && as_stored && row->fold_short != revs->fold_short) return 0;
    return 1;
}

static void measure(Row *row, const Source *s, const Query *exact, const Query *prefix, size_t queries,
                    const int *thread_list, int thread_count) {
    uint32_t *ticks = malloc(queries * sizeof *ticks), fold = 0, fold2 = 0;
    double best;
    int L = s->L;
    if (!ticks) return;

    /* node counts and the choice between two extensions: index only; the best of five runs */
    best = 1e30;
    for (int run = 0; run < 5; run++) {
        unsigned long long sum = 0;
        double t0 = cb_now();
        for (size_t i = 0; i < queries; i++) sum += (unsigned)nodes_of(s, L, exact[i].value);
        t0 = cb_now() - t0;
        if (t0 < best) best = t0;
        row->sum_nodes_full = sum;
    }
    row->nodes_full = best / (double)queries * 1e9;
    row->nodes_short = row->choice = -1.0;
    if (s->kind != KIND_FIXED) {
        unsigned long long sum = 0;
        best = 1e30;
        for (int run = 0; run < 5; run++) {
            double t0 = cb_now();
            sum = 0;
            for (size_t i = 0; i < queries; i++) sum += (unsigned)nodes_of(s, prefix[i].bits, prefix[i].value);
            t0 = cb_now() - t0;
            if (t0 < best) best = t0;
            row->sum_nodes_short = sum;
        }
        row->nodes_short = best / (double)queries * 1e9;
        best = 1e30;
        for (int run = 0; run < 5; run++) {
            double t0 = cb_now();
            sum = 0;
            for (size_t i = 0; i < queries; i++) /* the two extensions of a string of 1 to L-1 bits */
                sum += (unsigned)choice_of(s, prefix[i].bits, prefix[i].value);
            t0 = cb_now() - t0;
            if (t0 < best) best = t0;
            row->sum_choice = sum;
        }
        row->choice = best / (double)queries * 1e9;
        /* and one at a time, each by itself: what one such question costs when it comes alone */
        for (int pass = 0; pass < 3; pass++) {
            Latency now;
            sum = 0;
            for (size_t i = 0; i < queries; i++) {
                uint32_t value = prefix[i].value;
                unsigned answer;
                uint64_t a = tick_begin(), b;
                HIDE(value);
                answer = (unsigned)choice_of(s, prefix[i].bits, value);
                KEEP(answer);
                b = tick_end();
                sum += answer;
                ticks[i] = (uint32_t)(b - a > tick_overhead ? b - a - tick_overhead : 0);
            }
            if (sum != row->sum_choice) row->lookups_failed++; /* the same questions, the same answers */
            now = summarize(ticks, queries, 0);
            if (pass == 0 || now.mean < row->choice_alone.mean) row->choice_alone = now;
        }
    }

    /* REVs, one lookup at a time: as the reader returns them, and verified */
    row->fetch_full = time_best(s, exact, queries, ticks, 0, &fold);
    row->full = time_best(s, exact, queries, ticks, 1, &fold2);
    row->fold_full = fold;
    row->lookups_failed += row->fetch_full.failed != 0 || row->full.failed != 0 || fold != fold2;
    memset(&row->shorter, 0, sizeof row->shorter);
    memset(&row->fetch_short, 0, sizeof row->fetch_short);
    if (s->kind != KIND_FIXED) {
        row->fetch_short = time_best(s, prefix, queries, ticks, 0, &fold);
        row->shorter = time_best(s, prefix, queries, ticks, 1, &fold2);
        row->fold_short = fold;
        row->lookups_failed += row->fetch_short.failed != 0 || row->shorter.failed != 0 || fold != fold2;
    }
    for (int k = 0; k < thread_count && k < 8; k++) { /* twice; the faster run is kept */
        Latency again;
        double second;
        row->thread_counts[k] = thread_list[k];
        row->throughput[k] = concurrent(s, exact, queries, thread_list[k], &row->thread_latency[k]);
        second = concurrent(s, exact, queries, thread_list[k], &again);
        row->lookups_failed += row->thread_latency[k].failed != 0 || again.failed != 0;
        if (second > row->throughput[k]) {
            row->throughput[k] = second;
            row->thread_latency[k] = again;
        }
        row->thread_rows = k + 1;
    }
    free(ticks);
}

/* n lookups, each timed with the clock of the system: median and 99th percentile in microseconds. */
static void cold_pass(const Source *s, const Query *q, size_t n, double *us, double *p50, double *p99) {
    uint32_t fold = 0;
    for (size_t i = 0; i < n; i++) {
        double t0 = cb_now();
        int ok = rev_of(s, q[i].bits, q[i].value, &fold, 1);
        KEEP(ok);
        us[i] = (cb_now() - t0) * 1e6;
    }
    qsort(us, n, sizeof *us, cmp_double);
    *p50 = us[n / 2];
    *p99 = us[(size_t)((double)(n - 1) * 0.99)];
}

/* Lookups with the file out of the page cache: with nothing in memory, and
 * with the index read in and only the REVs to fetch; strings of L bits and
 * shorter ones.  exact and prefix hold 2 * lookups strings each. */
static void measure_cold(Row *row, const char *path, const Query *exact, const Query *prefix, size_t lookups) {
    Cbx c;
    Source s;
    double *us = malloc(lookups * sizeof *us), t0;
    volatile uint64_t touch = 0;
    size_t second = lookups; /* where the second set of strings starts */
    if (!us) return;
    s.kind = KIND_CBX;
    s.cbx = &c;
    s.table = NULL;
    s.compact = NULL;
    /* Few enough lookups that a page is rarely asked for twice: an eighth of the pages of the REVs at most. */
    if (cbx_open(path, &c) == CBX_OK) {
        size_t pages = (size_t)((c.info.file_bytes - c.info.pool_offset[1]) / 4096u);
        if (lookups > pages / 8) lookups = pages / 8 < 50 ? 50 : pages / 8;
        if (lookups > second) lookups = second;
        cbx_close(&c);
    }
    row->cold_lookups = lookups;
    for (int pass = 0; pass < 2; pass++) { /* nothing in memory: strings of L bits, then shorter ones */
        evict(path);
        if (cbx_open(path, &c) != CBX_OK) {
            free(us);
            return;
        }
        s.L = c.info.bits;
        madvise((void *)(uintptr_t)c.base, c.mapped, MADV_RANDOM); /* one page for one fault */
        if (pass == 0) cold_pass(&s, exact, lookups, us, &row->cold_all_p50, &row->cold_all_p99);
        else cold_pass(&s, prefix, lookups, us, &row->cold_all_short_p50, &row->cold_all_short_p99);
        cbx_close(&c);
    }
    evict(path);
    if (cbx_open(path, &c) != CBX_OK) {
        free(us);
        return;
    }
    t0 = cb_now(); /* the index: everything before the REVs */
    for (uint64_t at = 0; at < c.info.pool_offset[1]; at += 4096) touch += c.base[at];
    row->cold_index_s = cb_now() - t0;
    {   /* from here on one page for one fault: the address has to be on a page boundary */
        uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE), from = (c.info.pool_offset[1] + page - 1) / page * page;
        if (from < c.info.file_bytes)
            madvise((void *)(uintptr_t)(c.base + from), (size_t)(c.info.file_bytes - from), MADV_RANDOM);
    }
    cold_pass(&s, exact + second, lookups, us, &row->cold_rev_p50, &row->cold_rev_p99);
    cold_pass(&s, prefix + second, lookups, us, &row->cold_rev_short_p50, &row->cold_rev_short_p99);
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
               "                       %s shorter strings: %s not answered by an extension with the fewest nodes,\n"
               "                       %s by another such extension than the rule gives, %s with another answer to\n"
               "                       \"which extension needs fewer nodes, or neither\";\n"
               "                       %s answered by another extension than the lowest of the equals (no error:\n"
               "                       the rule for strings that start with 1 in a halved file)\n",
               commas(r->check.strings), L, commas(r->check.wrong_nodes), commas(r->check.not_emitting),
               commas(r->check.not_stored), commas(r->check.next_bit_differs), commas(r->check.shorter),
               commas(r->check.not_minimal), commas(r->check.wrong_extension), commas(r->check.wrong_comparison),
               commas(r->check.not_lowest));
    if (r->checked && r->check.not_stored) {
        printf("                       REVs other than the stored one, by node count:");
        for (int n = 1; n < CBX_CLASSES; n++)
            if (r->check.not_stored_by_nodes[n]) printf("  %d: %s", n, commas(r->check.not_stored_by_nodes[n]));
        printf("\n");
    }
    printf("  node count           %.1f ns for a string of %d bits", r->nodes_full, L);
    if (r->nodes_short >= 0)
        printf(", %.1f ns for a shorter one (many in a row);\n"
               "  comparison           which of the two extensions of a string needs fewer nodes, or neither, on\n"
               "                       demand: %.1f ns in a row; one at a time: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns\n",
               r->nodes_short, r->choice, r->choice_alone.mean, r->choice_alone.p50, r->choice_alone.p95,
               r->choice_alone.p99);
    else printf(" (many in a row)\n");
    printf("  REV                  as the reader returns it, %d bits: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", L,
           r->fetch_full.mean, r->fetch_full.p50, r->fetch_full.p95, r->fetch_full.p99);
    if (r->fetch_short.mean > 0)
        printf("\n                       shorter: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", r->fetch_short.mean,
               r->fetch_short.p50, r->fetch_short.p95, r->fetch_short.p99);
    printf("\n  verified REV         and run against the string, %d bits: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", L,
           r->full.mean, r->full.p50, r->full.p95, r->full.p99);
    if (r->shorter.mean > 0)
        printf("\n                       shorter: mean %.0f, p50 %.0f, p95 %.0f, p99 %.0f ns", r->shorter.mean,
               r->shorter.p50, r->shorter.p95, r->shorter.p99);
    printf("\n");
    for (int k = 0; k < r->thread_rows; k++)
        printf("  %2d reader%s           %.2f million verified REVs a second; p50 %.0f, p95 %.0f, p99 %.0f ns\n",
               r->thread_counts[k], r->thread_counts[k] == 1 ? " " : "s", r->throughput[k] / 1e6,
               r->thread_latency[k].p50, r->thread_latency[k].p95, r->thread_latency[k].p99);
    if (r->cold_done)
        printf("  out of the cache     %zu lookups each.  Nothing in memory: p50 %.0f, p99 %.0f us for a string of %d bits,\n"
               "                       p50 %.0f, p99 %.0f us for a shorter one.  With the index read in (%.3f s):\n"
               "                       p50 %.0f, p99 %.0f us and p50 %.0f, p99 %.0f us.  One read of the disk for each\n"
               "                       page not yet in memory; the REVs of shorter strings share few pages.\n",
               r->cold_lookups, r->cold_all_p50, r->cold_all_p99, L, r->cold_all_short_p50, r->cold_all_short_p99,
               r->cold_index_s, r->cold_rev_p50, r->cold_rev_p99, r->cold_rev_short_p50, r->cold_rev_short_p99);
    if (r->lookups_failed) printf("  LOOKUPS FAILED in the timed runs\n");
    if (r->other_answers) printf("  THE TIMED QUESTIONS GOT OTHER ANSWERS than from the files before\n");
    fflush(stdout);
}

static int cmd_run(const char *self, const char *bomb, const char *scratch, const char *compact_path,
                   const char *variants, size_t queries, const int *thread_list, int thread_count, size_t cold,
                   int checkpoint_shift, int keep, int verify, int rounds) {
    Row *rows;
    /* earlier rows to compare the answers for shorter strings with; the REVs by the rule among equals:
     * [0] the lowest everywhere (PR 8, whole files), [1] the complement rule (halved files) */
    const Row *sums = NULL, *revs[2] = {NULL, NULL};
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
        fprintf(stderr, "usage: v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants final|short|all|LIST]\n"
                        "                     [--queries N] [--rounds R] [--threads 1,2,4] [--cold N] [--checkpoint B]\n"
                        "                     [--keep] [--no-verify]\n");
        return 2;
    }
    if (!variants || !strcmp(variants, "final")) {
        for (size_t i = 0; i < sizeof final_design / sizeof final_design[0]; i++)
            parse_variant(final_design[i], checkpoint_shift, &list[count++]);
    } else if (!strcmp(variants, "short")) {
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
    printf("%zu random strings per measurement, %d round%s; a checkpoint every %d strings; %ld processors here\n\n",
           queries, rounds, rounds == 1 ? "" : "s", 1 << checkpoint_shift, sysconf(_SC_NPROCESSORS_ONLN));
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

    /* ---- the first round: every file measured, the candidates made and checked on the way ---- */
    snprintf(rows[nrows].name, sizeof rows[nrows].name, "fixed-width file");
    rows[nrows].bytes = fixed_bytes;
    rows[nrows].kind = KIND_FIXED;
    s.kind = KIND_FIXED;
    s.table = &table;
    s.cbx = NULL;
    s.compact = NULL;
    s.L = L;
    measure(&rows[nrows], &s, exact, prefix, queries, thread_list, thread_count);
    failed |= rows[nrows].lookups_failed;
    printf("measured  %s\n", rows[nrows].name);
    fflush(stdout);
    nrows++;

    if (compact_path) { /* the compact file of PR 8, if there is one */
        if (frb_compact_open(compact_path, &compact, &bad) != FRB_OK || frb_compact_info(compact)->bits != L) {
            fprintf(stderr, "v4_matrix: %s is not the compact file of this Bomb\n", compact_path);
            return 1;
        }
        snprintf(rows[nrows].name, sizeof rows[nrows].name, "compact file of PR 8");
        rows[nrows].bytes = frb_compact_info(compact)->file_bytes;
        rows[nrows].kind = KIND_COMPACT;
        s.kind = KIND_COMPACT;
        s.compact = compact;
        measure(&rows[nrows], &s, exact, prefix, queries, thread_list, thread_count);
        rows[nrows].other_answers = !same_answers(&rows[nrows], &rows[0], NULL, NULL, 1);
        failed |= rows[nrows].other_answers | rows[nrows].lookups_failed;
        sums = revs[0] = &rows[nrows];
        printf("measured  %s\n", rows[nrows].name);
        fflush(stdout);
        nrows++;
    }

    for (int k = 0; k < count; k++) {
        Row *row = &rows[nrows];
        Cbx c;
        CbxError e;
        snprintf(row->path, sizeof row->path, "%s/v4-%d-%s.cbx", scratch, L, list[k].name);
        memcpy(row->name, list[k].name, sizeof row->name);
        row->kind = KIND_CBX;
        remove(row->path); /* a file of an earlier run of this program */
        row->conversion = convert_in_child(self, bomb, row->path, &list[k]);
        if (!row->conversion.ok) {
            printf("not made  %s: %s\n", row->name,
                   row->conversion.error ? cbx_error_str((CbxError)row->conversion.error) : "the conversion failed");
            failed |= row->conversion.error != CBX_ERR_NOT_SYMMETRIC && row->conversion.error != CBX_ERR_NUMBERING;
            continue;
        }
        e = cbx_open(row->path, &c);
        if (e != CBX_OK) {
            printf("cannot open %s: %s\n", row->path, cbx_error_str(e));
            failed = 1;
            continue;
        }
        row->bytes = c.info.file_bytes;
        if (verify) {
            t0 = cb_now();
            e = cbx_check_body(&c);
            if (e == CBX_OK) e = cbx_check_against(&c, &table, &row->check);
            row->check_s = cb_now() - t0;
            row->checked = e == CBX_OK;
            if (e != CBX_OK || row->check.wrong_nodes || row->check.not_emitting || row->check.not_minimal ||
                row->check.wrong_extension || row->check.wrong_comparison ||
                (list[k].opt.colors && row->check.not_stored))
                failed = 1;
        }
        s.kind = KIND_CBX;
        s.cbx = &c;
        measure(row, &s, exact, prefix, queries, thread_list, thread_count);
        cbx_close(&c);
        {   /* a colorless file may return another REV of the same graph: then only the counts are compared */
            int as_stored = list[k].opt.colors || (row->checked && !row->check.not_stored);
            int rule = list[k].opt.complement != 0;
            row->other_answers = !same_answers(row, &rows[0], sums, revs[rule], as_stored);
            if (!sums) sums = row;
            if (!revs[rule] && as_stored) revs[rule] = row;
        }
        if (cold) measure_cold(row, row->path, exact + queries, prefix, cold);
        failed |= row->lookups_failed | row->other_answers;
        printf("made      %-26s %14s bytes in %5.1f s", row->name, commas(row->bytes), row->conversion.wall_s);
        if (verify) printf("; every string checked in %5.1f s", row->check_s);
        printf("; measured\n");
        fflush(stdout);
        nrows++;
    }
    for (int i = 0; i < nrows; i++) rows[i].full_low = rows[i].full_high = rows[i].full.mean;

    /* ---- the other rounds: every file again, in turn ---- */
    for (int round = 1; round < rounds; round++) {
        for (int i = 0; i < nrows; i++) {
            Row now;
            Cbx c;
            if (!rows[i].bytes) continue;
            memset(&now, 0, sizeof now);
            s.kind = rows[i].kind;
            s.compact = compact;
            if (rows[i].kind == KIND_CBX) {
                if (cbx_open(rows[i].path, &c) != CBX_OK) {
                    failed = 1;
                    continue;
                }
                s.cbx = &c;
            }
            measure(&now, &s, exact, prefix, queries, thread_list, thread_count);
            if (rows[i].kind == KIND_CBX) cbx_close(&c);
            keep_best(&rows[i], &now);
            failed |= rows[i].lookups_failed;
        }
        printf("round %d of %d measured\n", round + 1, rounds);
        fflush(stdout);
    }
    if (compact) frb_compact_close(compact);
    for (int i = 0; i < nrows; i++) {
        if (!rows[i].bytes) continue;
        print_row_detail(&rows[i], fixed_bytes, L);
        if (rows[i].kind == KIND_CBX && !keep) remove(rows[i].path);
    }

    /* ---- the matrix ---- */
    printf("\n\nTHE MATRIX   FixedRunBomb(%d), table CRC-32 %08x; times in nanoseconds unless said otherwise\n", L,
           (unsigned)finfo.table_crc);

    printf("\n1. Size, conversion, and what comes back\n\n");
    printf("  %-26s %14s %8s  %8s %9s  %14s %11s\n", "", "bytes", "of fixed", "conv s", "peak MiB", "not as stored",
           "other next");
    for (int i = 0; i < nrows; i++) {
        const Row *r = &rows[i];
        if (!r->bytes) continue;
        printf("  %-26s %14s %7.1f%%", r->name, commas(r->bytes), 100.0 * (double)r->bytes / (double)fixed_bytes);
        if (r->conversion.ok) printf("  %8.1f %9.0f", r->conversion.wall_s, (double)r->conversion.peak_kb / 1024.0);
        else printf("  %8s %9s", "-", "-");
        if (r->checked) printf("  %14s %11s", commas(r->check.not_stored), commas(r->check.next_bit_differs));
        else printf("  %14s %11s", "-", "-");
        printf("\n");
    }
    printf("\n  conv s, peak MiB: the conversion from the fixed-width file, its full validation included; the memory\n"
           "  of the converting process at its peak, with the source it holds.  not as stored: strings of %d bits\n"
           "  that get a REV other than the stored one; other next: those of them whose REV emits another bit %d.\n",
           L, L + 1);

    printf("\n2. Questions the index answers without reading a REV\n\n");
    printf("  %-26s %12s %12s %12s   %s\n", "", "nodes", "nodes", "comparison", "comparison, one at a time");
    printf("  %-26s %12s %12s %12s  %6s %6s %6s %6s\n", "", "L bits", "shorter", "", "mean", "p50", "p95", "p99");
    for (int i = 0; i < nrows; i++) {
        const Row *r = &rows[i];
        if (!r->bytes) continue;
        printf("  %-26s %12.1f", r->name, r->nodes_full);
        if (r->choice >= 0)
            printf(" %12.1f %12.1f  %6.0f %6.0f %6.0f %6.0f\n", r->nodes_short, r->choice, r->choice_alone.mean,
                   r->choice_alone.p50, r->choice_alone.p95, r->choice_alone.p99);
        else printf(" %12s %12s  %6s %6s %6s %6s\n", "-", "-", "-", "-", "-", "-");
    }
    printf("\n  nodes: the minimum node count of a string, a million in a row.  comparison: which of the two\n"
           "  extensions of a string of 1 to %d bits needs fewer nodes, or neither, worked out on demand from the\n"
           "  node counts of the two; in a row, and each question by itself.\n",
           L - 1);

    for (int part = 0; part < 2; part++) {
        printf("\n%d. The REV of a string of %s, one lookup at a time\n\n", 3 + part,
               part ? "1 to L-1 bits (every length as likely)" : "L bits");
        printf("  %-26s  %-27s  %-27s%s\n", "", "as the reader returns it", "and run against the string",
               part || rounds < 2 ? "" : "  that mean in each");
        printf("  %-26s  %6s %6s %6s %6s  %6s %6s %6s %6s%s\n", "", "mean", "p50", "p95", "p99", "mean", "p50", "p95",
               "p99", part || rounds < 2 ? "" : "  round: from, to");
        for (int i = 0; i < nrows; i++) {
            const Row *r = &rows[i];
            const Latency *f = part ? &r->fetch_short : &r->fetch_full, *v = part ? &r->shorter : &r->full;
            if (!r->bytes || v->mean <= 0) continue;
            printf("  %-26s  %6.0f %6.0f %6.0f %6.0f  %6.0f %6.0f %6.0f %6.0f", r->name, f->mean, f->p50, f->p95,
                   f->p99, v->mean, v->p50, v->p95, v->p99);
            if (!part && rounds > 1) printf("  %7.0f %5.0f", r->full_low, r->full_high);
            printf("\n");
        }
    }
    if (rounds > 1)
        printf("\n  Every timing in these tables is the quietest of %d rounds, each round going through all files in\n"
               "  turn; the last two columns of table 3 show how far the rounds lie apart on this machine.\n",
               rounds);
    printf("\n  as the reader returns it: what the reader of the file does to hand over a REV.  A candidate without\n"
           "  colors has run the machine on it to find them, one with rank and colors to number its nodes: those\n"
           "  REVs have been seen to emit the string.  The other readers unpack a record and return it unrun.\n"
           "  and run against the string: the REV returned is then run by code outside the reader, the same for all.\n");

    printf("\n5. Readers at once on one mapping: million verified REVs of L-bit strings a second, and one lookup\n\n");
    printf("  %-26s", "");
    for (int k = 0; k < thread_count; k++) printf("  %2d: %6s %5s %5s", thread_list[k], "M/s", "p50", "p99");
    printf("\n");
    for (int i = 0; i < nrows; i++) {
        const Row *r = &rows[i];
        if (!r->bytes) continue;
        printf("  %-26s", r->name);
        for (int k = 0; k < r->thread_rows; k++)
            printf("      %6.2f %5.0f %5.0f", r->throughput[k] / 1e6, r->thread_latency[k].p50, r->thread_latency[k].p99);
        printf("\n");
    }

    if (cold) {
        printf("\n6. Out of the page cache: microseconds for one lookup\n\n");
        printf("  %-26s  %-25s  %-25s\n", "", "nothing in memory", "the index in memory");
        printf("  %-26s  %5s %5s %6s %6s  %5s %5s %6s %6s\n", "", "L p50", "p99", "sh p50", "p99", "L p50", "p99",
               "sh p50", "p99");
        for (int i = 0; i < nrows; i++) {
            const Row *r = &rows[i];
            if (!r->bytes || !r->cold_done) continue;
            printf("  %-26s  %5.0f %5.0f %6.0f %6.0f  %5.0f %5.0f %6.0f %6.0f\n", r->name, r->cold_all_p50,
                   r->cold_all_p99, r->cold_all_short_p50, r->cold_all_short_p99, r->cold_rev_p50, r->cold_rev_p99,
                   r->cold_rev_short_p50, r->cold_rev_short_p99);
        }
        printf("\n  L: strings of %d bits; sh: shorter ones, whose REVs lie on few pages that are soon in memory.\n"
               "  A lookup costs one read of the disk for each page it touches that is not in memory yet.\n",
               L);
    }
    printf("\n%s\n", failed ? "FAIL: a variant did not give what the Bomb gives" : "PASS");
    frb_table_destroy(&table);
    free(rows);
    free(exact);
    free(prefix);
    return failed;
}

/* ---- one string, asked of a file ------------------------------------------------------ */

static int cmd_ask(const char *file, const char *target) {
    Cbx c;
    ClTopo t;
    uint32_t value = 0, colors = 0, leaf = 0;
    char text[40];
    int bits = 0, L, ok = 1;
    CbxError e;
    if (!file || !target) {
        fprintf(stderr, "usage: v4_matrix ask --file FILE --target STRING\n");
        return 2;
    }
    for (const char *p = target; *p; p++) {
        if ((*p != '0' && *p != '1') || bits >= 31) {
            fprintf(stderr, "v4_matrix: the target is a string of 0s and 1s, 31 at most\n");
            return 2;
        }
        value = 2 * value + (uint32_t)(*p - '0');
        bits++;
    }
    e = cbx_open(file, &c);
    if (e != CBX_OK) {
        fprintf(stderr, "v4_matrix: %s: %s\n", file, cbx_error_str(e));
        return 1;
    }
    L = c.info.bits;
    if (bits > L) {
        fprintf(stderr, "v4_matrix: %s answers strings of up to %d bits\n", file, L);
        cbx_close(&c);
        return 1;
    }
    printf("Target: %s (%d bits; prototype file of the %d-bit Bomb: %s, %s, %s)\n", bits ? target : "the empty string",
           bits, L, cbx_encoding_str(c.info.opt.encoding), c.info.opt.colors ? "colors" : "colorless",
           c.info.opt.complement ? "half" : "whole");
    if (bits >= 1) {
        ok = cbx_lookup(&c, bits, value, &t, &colors, &leaf) && cl_verify(&t, colors, value, bits);
        printf("Nodes: %d, the certified minimum\n", cbx_nodes(&c, bits, value));
        if (bits < L) {
            for (int i = 0; i < L; i++) text[i] = (char)('0' + ((leaf >> (L - 1 - i)) & 1u));
            text[L] = '\0';
            printf("Extension: %s: where the navigation bits lead, a %d-bit extension with the fewest nodes\n", text, L);
        }
    }
    if (bits < L) {
        int nodes[2], which = cbx_compare(&c, bits, value, nodes);
        printf("Followed by 0: %d nodes.  Followed by 1: %d nodes.  ", nodes[0], nodes[1]);
        if (which == CBX_TIE) printf("Neither needs fewer.\n");
        else printf("%d needs fewer, by %d.\n", which, which ? nodes[0] - nodes[1] : nodes[1] - nodes[0]);
    } else {
        printf("Followed by 0 or by 1: beyond this Bomb; the Bomb of %d-bit strings can say.\n", L + 1);
    }
    if (bits >= 1 && ok) {
        printf("\nNode   Color   Link0   Link1\n");
        for (int i = 0; i < t.n; i++)
            printf("%-6d %-7s %-7d %d\n", i, (colors >> i) & 1u ? "B (1)" : "W (0)", t.link[0][i], t.link[1][i]);
    }
    if (bits >= 1) printf("\nVerification: %s\n", ok ? "PASS (the REV emits the target)" : "FAIL");
    cbx_close(&c);
    return !ok;
}

/* ---- what the Bomb says about the next bit ------------------------------------------ */

/*
 * For every length k below L: of the strings of k bits, how many have one
 * extension by a bit that needs fewer nodes than the other, and by how many
 * nodes.  From the fixed-width table alone.  The minimum over the L-bit
 * extensions of a string is the minimum of the string itself, so the rows
 * are the same whatever complete Bomb they are taken from.
 */
static int cmd_choices(const char *bomb) {
    FrbFileInfo finfo;
    FrbTable table;
    uint64_t (*rows)[5], total[5] = {0};
    uint32_t bad = 0;
    uint8_t *below;
    FrbError fe;
    int L;
    if (!bomb) {
        fprintf(stderr, "usage: v4_matrix choices --bomb FILE\n");
        return 2;
    }
    fe = frb_file_read(bomb, &table, &finfo, &bad);
    if (fe != FRB_OK || !finfo.complete) {
        fprintf(stderr, "v4_matrix: %s is not a complete fixed-width Bomb (%s)\n", bomb,
                fe == FRB_OK ? "it is partial" : frb_error_str(fe));
        return 1;
    }
    L = table.bits;
    below = malloc(table.count);
    rows = calloc((size_t)L, sizeof *rows);
    if (!below || !rows) return 1;
    for (uint32_t t = 0;; t++) {
        below[t] = (uint8_t)frb_table_nodes(&table, t);
        if (t == table.count - 1u) break;
    }
    for (int k = L - 1; k >= 0; k--) { /* [0] the same, [1] to [3] fewer by 1, 2, 3 or more; [4] the larger count summed */
        for (uint32_t v = 0; v < ((uint32_t)1 << k); v++) {
            int zero = below[2 * v], one = below[2 * v + 1], d = zero < one ? one - zero : zero - one;
            rows[k][d > 3 ? 3 : d]++;
            below[v] = (uint8_t)(one < zero ? one : zero);
        }
    }
    printf("Bomb: FixedRunBomb(%d), %s strings, table CRC-32 %08x\n\n", L, commas(table.count), (unsigned)finfo.table_crc);
    printf("Of the strings of k bits, how many have one extension by a bit that needs fewer nodes than the other\n"
           "(the next bit the Bomb picks), and by how many nodes; and how many have two that need the same.\n\n");
    printf("   k %14s %14s %8s %14s %12s %12s %14s\n", "strings", "one is smaller", "share", "by 1 node", "by 2",
           "by 3 or more", "the same");
    for (int k = 0; k < L; k++) {
        uint64_t clear = rows[k][1] + rows[k][2] + rows[k][3], all = clear + rows[k][0];
        printf("  %2d %14s %14s %7.2f%% %14s %12s %12s %14s\n", k, commas(all), commas(clear),
               100.0 * (double)clear / (double)all, commas(rows[k][1]), commas(rows[k][2]), commas(rows[k][3]),
               commas(rows[k][0]));
        for (int i = 0; i < 4; i++) total[i] += rows[k][i];
    }
    {
        uint64_t clear = total[1] + total[2] + total[3], all = clear + total[0];
        printf("  %2s %14s %14s %7.2f%% %14s %12s %12s %14s\n", "", commas(all), commas(clear),
               100.0 * (double)clear / (double)all, commas(total[1]), commas(total[2]), commas(total[3]),
               commas(total[0]));
    }
    free(below);
    free(rows);
    frb_table_destroy(&table);
    return 0;
}

int main(int argc, char **argv) {
    const char *bomb = NULL, *scratch = NULL, *compact = NULL, *variants = NULL, *output = NULL, *variant = NULL;
    const char *file = NULL, *target = NULL;
    char self[4096];
    size_t queries = 1000000, cold = 1000;
    int thread_list[8] = {1, 2, 4}, thread_count = 3, checkpoint_shift = 8, keep = 0, verify = 1, rounds = 3;
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
            else if (!strcmp(k, "--file")) file = v;
            else if (!strcmp(k, "--target")) target = v;
            else if (!strcmp(k, "--queries")) queries = (size_t)strtoull(v, NULL, 10);
            else if (!strcmp(k, "--cold")) cold = (size_t)strtoull(v, NULL, 10);
            else if (!strcmp(k, "--rounds")) rounds = atoi(v) < 1 ? 1 : atoi(v) > 20 ? 20 : atoi(v);
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
    if (cold > queries / 2) cold = queries / 2; /* the shorter strings of the cold lookups are taken from the others */
    if (!strcmp(argv[1], "convert")) return cmd_convert(bomb, output, variant, checkpoint_shift);
    if (!strcmp(argv[1], "choices")) return cmd_choices(bomb);
    if (!strcmp(argv[1], "ask")) return cmd_ask(file, target);
    if (!strcmp(argv[1], "run")) {
        len = readlink("/proc/self/exe", self, sizeof self - 1);
        if (len > 0) self[len] = '\0';
        else snprintf(self, sizeof self, "%s", argv[0]);
        return cmd_run(self, bomb, scratch, compact, variants, queries, thread_list, thread_count, cold,
                       checkpoint_shift, keep, verify, rounds);
    }
usage:
    fprintf(stderr, "usage: v4_matrix run --bomb FILE --scratch DIR [--compact FILE] [--variants final|short|all|LIST]\n"
                    "                     [--queries N] [--rounds R] [--threads 1,2,4] [--cold N] [--checkpoint B]\n"
                    "                     [--keep] [--no-verify]\n"
                    "       v4_matrix convert --bomb FILE --output FILE --variant fields|base-n|rank[,colors|colorless][,whole|half]\n"
                    "       v4_matrix ask --file FILE --target STRING\n"
                    "       v4_matrix choices --bomb FILE\n");
    return 2;
}
