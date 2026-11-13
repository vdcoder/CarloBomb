/*
 * compact_bench: the fixed-width file and the compact file of one Bomb, side
 * by side, through the one interface that reads both.
 *
 *   compact_bench --fixed FILE --compact FILE [--wide FILE] [--queries N] [--no-check]
 *                 [--alternatives]
 *
 * FILE of --fixed is a complete fixed-width Bomb (format version 1, 2 or 3),
 * FILE of --compact the compact file made from it, and --wide optionally the
 * same with 4-byte index entries (`carlobomb bomb compact --index-bytes 4`).
 *
 * For each file: its size; the time to open it as a solver does, which is a
 * full validation, and the memory that takes; and the time of a lookup in
 * the opened Bomb, for random strings of the full length and of every
 * length, as a node count alone and as the REV from rev_fixed_bomb_solve,
 * which decodes it and runs it against the target before returning it.
 * Then lookups without opening anything: one answer read from the file.
 *
 * Unless --no-check is given, every string of every length is first asked of
 * all the files, and the node count and the extension chosen must agree.
 * The timed lookups add up what they return, and the sums must agree too.
 *
 * --alternatives measures two things the format does not do, for the design
 * review in benchmarks/COMPACT_FORMAT.md: an index that keeps one bit for
 * every shorter string instead of an ordinal (which of its two extensions
 * holds the answer), built here in memory from the compact file and walked
 * down to the full-length string; and the size the REVs would have with
 * their links coded in base N.  Nothing of it is written anywhere.
 *
 * Timings are the best of five passes over the same queries, in an order
 * the processor cannot learn.  They are of this machine with the files in
 * the page cache; what a cold disk adds is its own business.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact.h"
#include "fixed_bomb_solver.h"

enum { RUNS = 5, MAX_FILES = 3 };

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

static uint64_t file_bytes(const char *path) {
    FrbCompactInfo cinfo;
    FrbFileInfo info;
    if (frb_file_is_compact(path) == 1) return frb_compact_file_info(path, &cinfo) == FRB_OK ? cinfo.file_bytes : 0;
    if (frb_file_info(path, &info) != FRB_OK) return 0;
    return frb_file_bytes(frb_format(info.version), info.bits);
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

static double time_solve(const RevFixedBomb *b, const Query *q, char (*text)[FRB_MAX_BITS + 1], size_t n,
                         unsigned long long *sum) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        for (size_t i = 0; i < n; i++) {
            RevFixedBombInfo info;
            RevGraph g;
            if (rev_fixed_bomb_solve(b, text[i], q[i].bits, 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) s += 1u << 30;
            s += (unsigned)g.node_count * 1000003u + info.extension;
            for (int k = 0; k < g.node_count; k++)
                s += (unsigned)(g.nodes[k].color + 3 * g.nodes[k].links[0] + 7 * g.nodes[k].links[1]) * (unsigned)(k + 1);
        }
        t = cb_now() - t0;
        if (t < best) best = t;
        *sum = s;
    }
    return best / (double)n * 1e9;
}

/*
 * The index that is not in the format.  One bit for the string of k bits
 * with index v (k = 0 .. L-1), at position 2^k + v: 1 if the answer for the
 * string lies among the extensions of v1, 0 if among those of v0.  Following
 * the bits from a string leads to its L-bit extension with the fewest nodes
 * and the lowest index, in L - k steps.
 */
static uint32_t walk(const uint64_t *direction, int L, int bits, uint32_t value) {
    uint64_t h = ((uint64_t)1 << bits) + value;
    for (int k = bits; k < L; k++) h = 2 * h + ((direction[h >> 6] >> (h & 63)) & 1u);
    return (uint32_t)(h - ((uint64_t)1 << L));
}

static int alternatives(const char *path, const Query *any, size_t n) {
    FrbCompact *c = NULL;
    const FrbCompactInfo *info;
    uint64_t *direction, pool = 0, pool_radix = 0, index_now, index_bits;
    unsigned long long wrong = 0, sums[4] = {0, 0, 0, 0};
    double ns[4] = {1e30, 1e30, 1e30, 1e30};
    int L;
    if (frb_compact_open(path, &c, NULL) != FRB_OK) return 1;
    info = frb_compact_info(c);
    L = info->bits;
    direction = calloc(((size_t)1 << L) / 64 + 1, sizeof *direction);
    if (!direction) return 1;
    for (int k = 1; k < L; k++)
        for (uint32_t v = 0; v < ((uint32_t)1 << k); v++)
            if (frb_compact_ordinal(c, k + 1, 2 * v + 1) < frb_compact_ordinal(c, k + 1, 2 * v)) {
                uint64_t h = ((uint64_t)1 << k) + v;
                direction[h >> 6] |= (uint64_t)1 << (h & 63);
            }
    /* The walk ends at the string whose REV the ordinal index names: for every shorter string. */
    for (int k = 1; k < L; k++)
        for (uint32_t v = 0; v < ((uint32_t)1 << k); v++)
            wrong += frb_compact_ordinal(c, L, walk(direction, L, k, v)) != frb_compact_ordinal(c, k, v);

    for (int run = 0; run < RUNS; run++) {
        unsigned long long s = 0;
        double t0 = cb_now(), t;
        RevGraph g;
        for (size_t i = 0; i < n; i++) s += (unsigned)frb_compact_nodes(c, any[i].bits, any[i].value);
        t = (cb_now() - t0) / (double)n * 1e9;
        if (t < ns[0]) ns[0] = t;
        sums[0] = s;

        s = 0, t0 = cb_now();
        for (size_t i = 0; i < n; i++)
            s += (unsigned)frb_compact_nodes(c, L, walk(direction, L, any[i].bits, any[i].value));
        t = (cb_now() - t0) / (double)n * 1e9;
        if (t < ns[1]) ns[1] = t;
        sums[1] = s;

        /* The REV, the L-bit string it stands for, and the check that it emits the string asked for. */
        s = 0, t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            uint32_t extension;
            if (frb_compact_graph(c, any[i].bits, any[i].value, &g) != FRB_OK) s += 1u << 30;
            extension = frb_graph_output(&g, L);
            if ((extension >> (L - any[i].bits)) != any[i].value) s += 1u << 30;
            s += (unsigned)g.node_count * 1000003u + extension;
        }
        t = (cb_now() - t0) / (double)n * 1e9;
        if (t < ns[2]) ns[2] = t;
        sums[2] = s;

        s = 0, t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            uint32_t extension = walk(direction, L, any[i].bits, any[i].value);
            if (frb_compact_graph(c, L, extension, &g) != FRB_OK) s += 1u << 30;
            if (frb_graph_output(&g, any[i].bits) != any[i].value) s += 1u << 30;
            s += (unsigned)g.node_count * 1000003u + extension;
        }
        t = (cb_now() - t0) / (double)n * 1e9;
        if (t < ns[3]) ns[3] = t;
        sums[3] = s;
    }
    wrong += sums[0] != sums[1] || sums[2] != sums[3];

    for (int k = 1; k <= info->max_nodes; k++) {
        /* 2^k colorings times k^(2k) link tables: the bytes that many values need. */
        double bits_needed = (double)k + 2.0 * (double)k * log2((double)k);
        pool += (uint64_t)info->class_count[k] * info->class_stride[k];
        pool_radix += (uint64_t)info->class_count[k] * (uint64_t)ceil(bits_needed / 8.0 - 1e-9);
    }
    index_now = info->index_entries * (uint64_t)info->index_bytes;
    index_bits = (uint64_t)info->count * (uint64_t)info->index_bytes + (uint64_t)info->count / 8u;
    printf("\nAlternatives to the index and the packing of the format (measured in memory, not written)\n");
    printf("  Prefix index                                   bytes   nodes, 1..L   REV, 1..L (ns)\n");
    printf("  an ordinal for every string (the format)  %12llu %11.1f %13.1f\n", (unsigned long long)index_now,
           ns[0], ns[2]);
    printf("  ordinals for L bits, 1 bit for shorter    %12llu %11.1f %13.1f\n", (unsigned long long)index_bits,
           ns[1], ns[3]);
    printf("  the walk along the bits ends at the REV the ordinal names, for every shorter string: %s\n",
           wrong ? "NO" : "yes");
    printf("  REVs                                           bytes\n");
    printf("  links of ceil(log2 N) bits (the format)   %12llu\n", (unsigned long long)pool);
    printf("  colors and links as one number in base N  %12llu   (%.1f%%)\n", (unsigned long long)pool_radix,
           100.0 * (double)pool_radix / (double)pool);
    printf("  file with both: %llu bytes, against %llu\n",
           (unsigned long long)(info->pool_offset - index_now + index_bits + pool_radix),
           (unsigned long long)info->file_bytes);
    free(direction);
    frb_compact_close(c);
    return wrong != 0;
}

int main(int argc, char **argv) {
    const char *paths[MAX_FILES] = {NULL, NULL, NULL};
    const char *names[MAX_FILES] = {"fixed-width", "compact", "compact, 4-byte index"};
    RevFixedBomb *bombs[MAX_FILES] = {NULL, NULL, NULL};
    double open_s[MAX_FILES] = {0}, ns[MAX_FILES][4];
    unsigned long long sums[MAX_FILES][4];
    long anon_kb[MAX_FILES] = {0}, mapped_kb[MAX_FILES] = {0};
    char (*text)[FRB_MAX_BITS + 1];
    Query *exact, *any;
    size_t queries = 2000000;
    int files = 0, bits = 0, check = 1, failed = 0, alternative = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-check")) check = 0;
        else if (!strcmp(argv[i], "--alternatives")) alternative = 1;
        else if (!strcmp(argv[i], "--fixed") && i + 1 < argc) paths[0] = argv[++i];
        else if (!strcmp(argv[i], "--compact") && i + 1 < argc) paths[1] = argv[++i];
        else if (!strcmp(argv[i], "--wide") && i + 1 < argc) paths[2] = argv[++i];
        else if (!strcmp(argv[i], "--queries") && i + 1 < argc) queries = (size_t)strtoull(argv[++i], NULL, 10);
        else paths[0] = NULL, i = argc;
    }
    if (!paths[0] || !paths[1] || queries < 1) {
        fprintf(stderr, "usage: compact_bench --fixed FILE --compact FILE [--wide FILE] [--queries N] [--no-check]"
                        " [--alternatives]\n");
        return 2;
    }
    files = paths[2] ? 3 : 2;
    if (frb_file_is_compact(paths[0]) != 0 || frb_file_is_compact(paths[1]) != 1 ||
        (paths[2] && frb_file_is_compact(paths[2]) != 1)) {
        fprintf(stderr, "compact_bench: --fixed takes a fixed-width Bomb, --compact and --wide compact ones\n");
        return 2;
    }

    /* ---- sizes ---- */
    printf("Files\n");
    for (int f = 0; f < files; f++) {
        uint64_t bytes = file_bytes(paths[f]);
        printf("  %-22s %15llu bytes  %6.1f%%   %s\n", names[f], (unsigned long long)bytes,
               100.0 * (double)bytes / (double)file_bytes(paths[0]), paths[f]);
        if (f > 0) {
            FrbCompactInfo c;
            if (frb_compact_file_info(paths[f], &c) != FRB_OK) return 1;
            printf("  %22s   REVs %llu, prefix index %llu (%d bytes each), header and directory %llu\n", "",
                   (unsigned long long)(c.file_bytes - c.pool_offset),
                   (unsigned long long)(c.index_entries * (uint64_t)c.index_bytes), c.index_bytes,
                   (unsigned long long)(c.pool_offset - c.index_entries * (uint64_t)c.index_bytes));
        }
    }

    /* ---- opening ---- */
    printf("\nOpening, as rev_fixed_bomb_open does it: the whole file validated, every REV run\n");
    for (int f = 0; f < files; f++) {
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
        printf("  %-22s %7.2f s   memory of its own %7.1f MiB, mapped file pages resident %7.1f MiB\n", names[f],
               open_s[f], (double)anon_kb[f] / 1024.0, (double)mapped_kb[f] / 1024.0);
    }

    /* ---- the same answers ---- */
    if (check) {
        unsigned long long strings = 0, wrong = 0;
        double t0 = cb_now();
        for (int len = 1; len <= bits; len++) {
            for (uint32_t v = 0;; v++) {
                int nodes = rev_fixed_bomb_min_nodes(bombs[0], len, v);
                uint32_t extension = rev_fixed_bomb_extension(bombs[0], len, v);
                for (int f = 1; f < files; f++)
                    wrong += rev_fixed_bomb_min_nodes(bombs[f], len, v) != nodes ||
                             rev_fixed_bomb_extension(bombs[f], len, v) != extension;
                strings++;
                if (v == ((uint32_t)1 << len) - 1u) break;
            }
        }
        printf("\nEvery string of 1 to %d bits (%llu) asked of each file: node count and extension chosen %s"
               "  (%.1f s)\n",
               bits, strings, wrong ? "DIFFER" : "agree", cb_now() - t0);
        failed |= wrong != 0;
    }

    /* ---- lookups in the opened Bomb ---- */
    exact = malloc(queries * sizeof *exact);
    any = malloc(queries * sizeof *any);
    text = malloc(queries * sizeof *text);
    if (!exact || !any || !text) {
        fprintf(stderr, "compact_bench: out of memory for the queries\n");
        return 1;
    }
    for (size_t i = 0; i < queries; i++) {
        exact[i].bits = (uint8_t)bits;
        exact[i].value = (uint32_t)(rnd64() >> 33) & (((uint32_t)1 << bits) - 1u);
        any[i].bits = (uint8_t)(1 + (rnd64() >> 33) % (uint64_t)bits);
        any[i].value = (uint32_t)(rnd64() >> 33) & (((uint32_t)1 << any[i].bits) - 1u);
    }
    for (int f = 0; f < files; f++) {
        ns[f][0] = time_nodes(bombs[f], exact, queries, &sums[f][0]);
        ns[f][1] = time_nodes(bombs[f], any, queries, &sums[f][1]);
    }
    for (int w = 2; w < 4; w++) {
        const Query *q = w == 2 ? exact : any;
        for (size_t i = 0; i < queries; i++) frb_target_format(q[i].value, q[i].bits, text[i]);
        for (int f = 0; f < files; f++) ns[f][w] = time_solve(bombs[f], q, text, queries, &sums[f][w]);
    }
    printf("\nLookups in the opened Bomb, nanoseconds each (%zu random strings, best of %d passes)\n", queries, RUNS);
    printf("  %-22s %14s %14s %14s %14s\n", "", "nodes, L bits", "nodes, 1..L", "REV, L bits", "REV, 1..L");
    for (int f = 0; f < files; f++) {
        printf("  %-22s %14.1f %14.1f %14.1f %14.1f\n", names[f], ns[f][0], ns[f][1], ns[f][2], ns[f][3]);
        for (int w = 0; w < 4; w++) failed |= sums[f][w] != sums[0][w];
    }
    printf("  what the lookups returned, added up: %s\n", failed ? "DIFFERS between the files" : "the same from every file");

    /* ---- one answer from the file, nothing opened ---- */
    {
        size_t n = queries < 20000 ? queries : 20000;
        unsigned long long s_fixed = 0, s_compact = 0;
        double t0, t_fixed, t_compact[MAX_FILES] = {0}, t_any[MAX_FILES] = {0};
        RevGraph g;
        t0 = cb_now();
        for (size_t i = 0; i < n; i++) {
            FrbStatus status;
            if (frb_file_lookup(paths[0], exact[i].value, NULL, &status, &g) != FRB_OK) failed = 1;
            s_fixed += (unsigned)g.node_count;
        }
        t_fixed = (cb_now() - t0) / (double)n * 1e6;
        for (int f = 1; f < files; f++) {
            unsigned long long s = 0;
            t0 = cb_now();
            for (size_t i = 0; i < n; i++) {
                if (frb_compact_file_lookup(paths[f], bits, exact[i].value, NULL, &g, NULL) != FRB_OK) failed = 1;
                s += (unsigned)g.node_count;
            }
            t_compact[f] = (cb_now() - t0) / (double)n * 1e6;
            if (f == 1) s_compact = s;
            failed |= s != s_fixed;
            t0 = cb_now();
            for (size_t i = 0; i < n; i++)
                if (frb_compact_file_lookup(paths[f], any[i].bits, any[i].value, NULL, &g, NULL) != FRB_OK) failed = 1;
            t_any[f] = (cb_now() - t0) / (double)n * 1e6;
        }
        printf("\nOne answer read from the file, nothing opened before, microseconds each (%zu lookups)\n", n);
        printf("  %-22s %14s %14s\n", "", "REV, L bits", "REV, 1..L");
        printf("  %-22s %14.1f %14s   (a shorter string needs the Bomb opened: %.2f s)\n", names[0], t_fixed, "-",
               open_s[0]);
        for (int f = 1; f < files; f++) printf("  %-22s %14.1f %14.1f\n", names[f], t_compact[f], t_any[f]);
        (void)s_compact;
    }

    for (int f = 0; f < files; f++) rev_fixed_bomb_close(bombs[f]);
    if (alternative) failed |= alternatives(paths[1], any, queries);
    free(exact);
    free(any);
    free(text);
    printf("\n%s\n", failed ? "FAIL: the files do not give the same answers" : "PASS: the same answers from every file");
    return failed ? 1 : 0;
}
