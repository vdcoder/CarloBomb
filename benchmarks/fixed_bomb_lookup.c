/*
 * Bomb-backed FixedSolver: exhaustive self-check and timings.
 *
 *   fixed_bomb_lookup BOMB.frb            check, then time
 *   fixed_bomb_lookup BOMB.frb --check    check only
 *   fixed_bomb_lookup BOMB.frb --bench    time only
 *
 * This program links the lookup solver, the FixedRunBomb format code and the
 * REV interpreter, and nothing else.  There is no search solver in it, so
 * whatever it answers, it answers from the table.
 *
 * The check does not use the prefix index to check the prefix index: for
 * every string of every length it scans all B-bit extensions in the table
 * itself for the fewest nodes and the lowest index, and compares.  The graph
 * returned for the string is then run on the interpreter here, bit by bit.
 *
 * The timings take loading (reading the file and validating it in full),
 * index construction and lookups apart.  Lookups are timed over every string
 * of every length, in a shuffled order so that the processor cannot learn
 * the sequence.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"

typedef struct {
    uint8_t bits;
    uint16_t value;
} Query;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Every string of 1..bits bits, shuffled (the same shuffle every run). */
static Query *all_queries(int bits, size_t *count) {
    size_t n = ((size_t)2 << bits) - 2, k = 0;
    uint64_t state = 0x9E3779B97F4A7C15ull;
    Query *q = malloc(n * sizeof *q);
    if (!q) return NULL;
    for (int len = 1; len <= bits; len++)
        for (uint32_t v = 0; v < ((uint32_t)1 << len); v++) {
            q[k].bits = (uint8_t)len;
            q[k].value = (uint16_t)v;
            k++;
        }
    for (size_t i = n - 1; i > 0; i--) {
        size_t j;
        Query tmp;
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        j = (size_t)(state % (uint64_t)(i + 1));
        tmp = q[i];
        q[i] = q[j];
        q[j] = tmp;
    }
    *count = n;
    return q;
}

/* Fewest nodes among the B-bit extensions of a string, and the lowest index
 * that has them: straight from the table entries, without the index. */
static void scan(const FrbTable *t, int bits, uint32_t value, int *nodes, uint32_t *index) {
    int shift = t->bits - bits;
    uint32_t first = value << shift, span = (uint32_t)1 << shift;
    *nodes = 99;
    *index = 0;
    for (uint32_t i = first; i < first + span; i++) {
        int n = frb_table_nodes(t, i);
        if (n < *nodes) {
            *nodes = n;
            *index = i;
        }
    }
}

static int check(const char *path) {
    RevFixedBomb *bomb = NULL;
    FrbTable table;
    char target[FRB_MAX_BITS + 1];
    unsigned long long strings = 0, failures = 0;
    int bits;
    FrbError e = frb_file_read(path, &table, NULL, NULL);
    if (e == FRB_OK) {
        e = rev_fixed_bomb_open(path, &bomb, NULL);
        if (e != FRB_OK) frb_table_destroy(&table); /* a valid file that is not a complete Bomb */
    }
    if (e != FRB_OK) {
        printf("check: cannot use %s: %s\n", path, frb_error_str(e));
        return 1;
    }
    bits = rev_fixed_bomb_bits(bomb);
    for (int len = 1; len <= bits; len++) {
        for (uint32_t v = 0; v < ((uint32_t)1 << len); v++) {
            RevFixedBombInfo info;
            RevGraph g;
            RevMachine m;
            uint32_t want_index;
            int want_nodes, ok;
            scan(&table, len, v, &want_nodes, &want_index);
            frb_target_format(v, len, target);
            ok = rev_fixed_bomb_solve(bomb, target, (size_t)len, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL;
            ok = ok && info.min_nodes == want_nodes && info.extension == want_index;
            ok = ok && rev_fixed_bomb_min_nodes(bomb, len, v) == want_nodes;
            ok = ok && rev_fixed_bomb_extension(bomb, len, v) == want_index;
            ok = ok && (want_index >> (bits - len)) == v; /* the extension extends the string */
            ok = ok && g.node_count == want_nodes && rev_graph_validate(&g) == REV_OK;
            if (ok) { /* run it here, one bit at a time */
                rev_machine_reset(&m);
                for (int i = 0; i < len && ok; i++)
                    ok = rev_machine_next_bit(&g, &m) == target[i] - '0';
            }
            strings++;
            if (!ok && failures++ < 5) printf("check: wrong answer for %s\n", target);
        }
    }
    printf("check: %llu strings of 1 to %d bits against a direct scan of the table: %s\n", strings,
           bits, failures ? "FAILED" : "all agree");
    rev_fixed_bomb_close(bomb);
    frb_table_destroy(&table);
    return failures ? 1 : 0;
}

static int bench(const char *path) {
    enum { RUNS = 9 };
    double load[RUNS], index[RUNS], t0;
    RevFixedBomb *bomb = NULL;
    char (*text)[FRB_MAX_BITS + 1];
    size_t count = 0;
    Query *q;
    unsigned long long sink = 0;
    double best_select = 1e9, best_nodes = 1e9, best_solve = 1e9;
    int bits;

    for (int r = 0; r < RUNS; r++) { /* loading: read, checksums, every graph run */
        FrbTable table;
        t0 = cb_now();
        if (frb_file_read(path, &table, NULL, NULL) != FRB_OK) {
            printf("bench: cannot read %s\n", path);
            return 1;
        }
        load[r] = cb_now() - t0;
        frb_table_destroy(&table);
    }
    if (rev_fixed_bomb_open(path, &bomb, NULL) != FRB_OK) {
        printf("bench: %s is not a complete FixedRunBomb\n", path);
        return 1;
    }
    bits = rev_fixed_bomb_bits(bomb);
    for (int r = 0; r < RUNS; r++) { /* index construction alone */
        t0 = cb_now();
        rev_fixed_bomb_reindex(bomb);
        index[r] = cb_now() - t0;
    }
    qsort(load, RUNS, sizeof load[0], cmp_double);
    qsort(index, RUNS, sizeof index[0], cmp_double);

    q = all_queries(bits, &count);
    text = q ? malloc(count * sizeof *text) : NULL;
    if (!q || !text) {
        free(q);
        rev_fixed_bomb_close(bomb);
        return 1;
    }
    for (size_t i = 0; i < count; i++) frb_target_format(q[i].value, q[i].bits, text[i]);

    for (int r = 0; r < RUNS; r++) {
        int passes = 20;
        t0 = cb_now(); /* which entry answers the string */
        for (int p = 0; p < passes; p++)
            for (size_t i = 0; i < count; i++) sink += rev_fixed_bomb_extension(bomb, q[i].bits, q[i].value);
        t0 = (cb_now() - t0) / ((double)passes * (double)count);
        if (t0 < best_select) best_select = t0;

        t0 = cb_now(); /* its minimum node count */
        for (int p = 0; p < passes; p++)
            for (size_t i = 0; i < count; i++)
                sink += (unsigned)rev_fixed_bomb_min_nodes(bomb, q[i].bits, q[i].value);
        t0 = (cb_now() - t0) / ((double)passes * (double)count);
        if (t0 < best_nodes) best_nodes = t0;

        passes = 3;
        t0 = cb_now(); /* the whole answer: parse, select, unpack the graph, run it */
        for (int p = 0; p < passes; p++)
            for (size_t i = 0; i < count; i++) {
                RevGraph g;
                sink += rev_fixed_bomb_solve(bomb, text[i], q[i].bits, 0, &g, NULL) ==
                        REV_FIXED_BOMB_MINIMAL;
                sink += (unsigned)g.node_count;
            }
        t0 = (cb_now() - t0) / ((double)passes * (double)count);
        if (t0 < best_solve) best_solve = t0;
    }

    printf("Bomb: %s, FixedRunBomb(%d), largest minimum %d nodes\n", path, bits,
           rev_fixed_bomb_max_nodes(bomb));
    printf("Loading (read, checksums, every graph run): %.2f ms min, %.2f ms median of %d\n",
           load[0] * 1e3, load[RUNS / 2] * 1e3, (int)RUNS);
    printf("Index construction (%u prefixes, %u KiB):     %.1f us min, %.1f us median of %d\n",
           (1u << bits) - 2u, (unsigned)(((size_t)1 << bits) * sizeof(uint16_t) / 1024u), index[0] * 1e6,
           index[RUNS / 2] * 1e6, (int)RUNS);
    printf("Lookup, over all %zu strings of 1 to %d bits in shuffled order, best of %d:\n", count,
           bits, (int)RUNS);
    printf("  select the extension          %7.1f ns\n", best_select * 1e9);
    printf("  minimum node count            %7.1f ns\n", best_nodes * 1e9);
    printf("  solve: text in, verified graph out  %7.1f ns   (%.2f million per second)\n",
           best_solve * 1e9, 1e-6 / best_solve);
    if (sink == 0) printf("(nothing was computed)\n");
    free(q);
    free(text);
    rev_fixed_bomb_close(bomb);
    return 0;
}

int main(int argc, char **argv) {
    int do_check = 1, do_bench = 1, status = 0;
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: fixed_bomb_lookup BOMB.frb [--check | --bench]\n");
        return 2;
    }
    if (argc == 3) {
        if (!strcmp(argv[2], "--check")) do_bench = 0;
        else if (!strcmp(argv[2], "--bench")) do_check = 0;
        else {
            fprintf(stderr, "usage: fixed_bomb_lookup BOMB.frb [--check | --bench]\n");
            return 2;
        }
    }
    if (do_check) status = check(argv[1]);
    if (do_bench && status == 0) status = bench(argv[1]);
    return status;
}
