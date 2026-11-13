/*
 * Tests of the Bomb-backed FixedSolver.
 *
 * Usage: test_fixed_bomb_solver [scratch-directory] [--bomb FILE] [--cross-bits N]
 *
 *   --bomb FILE      use this complete FixedRunBomb(16) for the 16-bit part
 *                    instead of building one
 *   --cross-bits N   compare with the MinimalSolver for every string of 1..N
 *                    bits (default 16: all 131,070 strings)
 *
 * Part 1 builds small Bombs straight from the C MinimalSolver and checks the
 * solver on every string of every length they cover, and every way a Bomb
 * can be unfit for use.  It needs nothing but C.
 *
 * Part 2 is the milestone's validation list on the 16-bit Bomb: all 131,070
 * strings of 1 to 16 bits, each against a direct scan of the table and
 * against the MinimalSolver, the tie-break, and the facts the lower bound
 * for longer strings rests on.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <stdint.h>
#include <stdlib.h>

#include "compact.h"
#include "fixed_bomb_solver.h"
#include "minimal_solver.h"
#include "test_util.h"

#ifdef CARLOBOMB_HAVE_AMD64_ASM
#include "asm_solver.h"
#endif
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fixed_run_build.h"
#endif

static char scratch[1024] = "fixed_bomb_test_work";
static RevLevelSolveFn engine = rev_level_solve_ex;
static const char *engine_name = "c";

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)(((rng_state * 0x2545F4914F6CDD1Dull) >> 33) % n);
}

static const char *in_scratch(const char *name) {
    static char bufs[8][1200];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 8;
    snprintf(buf, sizeof bufs[0], "%s/%s", scratch, name);
    return buf;
}

static int same_graph(const RevGraph *a, const RevGraph *b) {
    if (a->node_count != b->node_count) return 0;
    for (int i = 0; i < a->node_count; i++)
        if (a->nodes[i].color != b->nodes[i].color || a->nodes[i].links[0] != b->nodes[i].links[0] ||
            a->nodes[i].links[1] != b->nodes[i].links[1])
            return 0;
    return 1;
}

static int entry_nodes(const FrbTable *t, uint32_t index) {
    return t->entries[(size_t)index * FRB_ENTRY_BYTES + FRB_GRAPH_BYTES] & 31;
}

/* Fewest nodes among the extensions of a string and the lowest index that
 * has them, from the table entries alone. */
static void scan(const FrbTable *t, int bits, uint32_t value, int *nodes, uint32_t *index) {
    int shift = t->bits - bits;
    uint32_t first = value << shift, span = (uint32_t)1 << shift;
    *nodes = 99;
    *index = 0;
    for (uint32_t i = first; i < first + span; i++)
        if (entry_nodes(t, i) < *nodes) {
            *nodes = entry_nodes(t, i);
            *index = i;
        }
}

/* A complete table of `bits`-bit strings from the C MinimalSolver. */
static void solver_table(FrbTable *t, int bits) {
    char target[FRB_MAX_BITS + 1];
    RevGraph g;
    CHECK(frb_table_create(t, bits) == FRB_OK);
    for (uint32_t i = 0; i < t->count; i++) {
        frb_target_format(i, bits, target);
        CHECK(rev_minimal_solve(target, (size_t)bits, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
}

static void copy_table(FrbTable *to, const FrbTable *from) {
    CHECK(frb_table_create(to, from->bits) == FRB_OK);
    memcpy(to->entries, from->entries, (size_t)from->count * FRB_ENTRY_BYTES);
}

static RevFixedBomb *bomb_of(const FrbTable *t) {
    FrbTable copy;
    RevFixedBomb *b = NULL;
    copy_table(&copy, t);
    CHECK(rev_fixed_bomb_from_table(&copy, &b, NULL) == FRB_OK);
    CHECK(b != NULL && copy.entries == NULL && copy.count == 0); /* the storage changed hands */
    return b;
}

/* ---- part 1: small Bombs ------------------------------------------------- */

/* The same table in each of the three format versions: the same answers. */
static void test_three_formats(int bits) {
    char target[64];
    FrbTable t, wide;
    RevFixedBomb *b[4] = {NULL, NULL, NULL, NULL};
    unsigned long long strings = 0, wrong = 0;
    solver_table(&t, bits);
    b[1] = bomb_of(&t);
    for (int version = 2; version <= 3; version++) {
        CHECK(frb_table_create_as(&wide, bits, frb_format(version)) == FRB_OK);
        for (uint32_t i = 0; i < t.count; i++) {
            RevGraph g;
            FrbStatus st;
            CHECK(frb_table_get(&t, i, &st, &g) == FRB_OK);
            CHECK(frb_table_put(&wide, i, st, &g) == FRB_OK);
        }
        CHECK(rev_fixed_bomb_from_table(&wide, &b[version], NULL) == FRB_OK && wide.entries == NULL);
    }
    if (!b[1] || !b[2] || !b[3]) return;
    for (int len = 1; len <= bits; len++) {
        uint32_t h1[FRB_MAX_NODES + 1], h[FRB_MAX_NODES + 1];
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevFixedBombInfo i1, i;
            RevGraph g1, g;
            frb_target_format(v, len, target);
            CHECK(rev_fixed_bomb_solve(b[1], target, (size_t)len, 0, &g1, &i1) == REV_FIXED_BOMB_MINIMAL);
            for (int version = 2; version <= 3; version++) {
                int ok = rev_fixed_bomb_solve(b[version], target, (size_t)len, 0, &g, &i) == REV_FIXED_BOMB_MINIMAL;
                ok = ok && i.min_nodes == i1.min_nodes && i.extension == i1.extension && same_graph(&g, &g1);
                ok = ok && rev_fixed_bomb_min_nodes(b[version], len, v) == i1.min_nodes;
                ok = ok && rev_fixed_bomb_solve(b[version], target, (size_t)len, i1.min_nodes - 1, &g, &i) ==
                               (i1.min_nodes > 1 ? REV_FIXED_BOMB_PROVEN_UNSAT : REV_FIXED_BOMB_MINIMAL);
                wrong += !ok;
            }
            strings++;
        }
        rev_fixed_bomb_histogram(b[1], len, h1);
        for (int version = 2; version <= 3; version++) {
            rev_fixed_bomb_histogram(b[version], len, h);
            CHECK(memcmp(h, h1, sizeof h) == 0);
        }
    }
    CHECK(wrong == 0);
    for (int version = 1; version <= 3; version++) rev_fixed_bomb_close(b[version]);
    frb_table_destroy(&t);
    printf("three formats: a %d-bit Bomb in versions 1, 2 and 3 gives the same answer for all %llu strings\n",
           bits, strings);
}

/* Every string of every length a small Bomb covers, against the MinimalSolver. */
static void test_small(int bits) {
    char target[64];
    FrbTable t;
    RevFixedBomb *b;
    unsigned long long strings = 0;

    solver_table(&t, bits);
    b = bomb_of(&t);
    if (!b) return;
    CHECK(rev_fixed_bomb_bits(b) == bits);

    for (int len = 1; len <= bits; len++) {
        uint32_t hist[FRB_MAX_NODES + 1], direct[FRB_MAX_NODES + 1] = {0}, total = 0;
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevFixedBombInfo info;
            RevMinimalInfo mi;
            RevGraph g, mg;
            uint32_t want_index;
            int want_nodes;
            frb_target_format(v, len, target);
            scan(&t, len, v, &want_nodes, &want_index);
            CHECK(rev_minimal_solve(target, (size_t)len, 0, NULL, &mg, &mi) == REV_MIN_MINIMAL);
            CHECK_MSG(mi.node_count == want_nodes, "%s: solver %d, table %d", target, mi.node_count, want_nodes);

            CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL);
            CHECK(info.min_nodes == mi.node_count && info.lower_bound == mi.node_count);
            CHECK(info.extension == want_index && info.bomb_bits == bits);
            t_check_witness(&g, target, (size_t)len, mi.node_count);
            CHECK(rev_fixed_bomb_min_nodes(b, len, v) == mi.node_count);
            CHECK(rev_fixed_bomb_extension(b, len, v) == want_index);
            CHECK(rev_fixed_bomb_lower_bound(b, target, (size_t)len) == mi.node_count);

            /* A node budget: enough, exactly enough, one too few. */
            CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, mi.node_count, &g, &info) == REV_FIXED_BOMB_MINIMAL);
            CHECK(g.node_count == mi.node_count);
            CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, 64, &g, NULL) == REV_FIXED_BOMB_MINIMAL);
            if (mi.node_count > 1) {
                g.node_count = 77;
                CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, mi.node_count - 1, &g, &info) ==
                      REV_FIXED_BOMB_PROVEN_UNSAT);
                CHECK(g.node_count == 0 && info.min_nodes == mi.node_count && info.extension == want_index);
                /* and it is true: the search finds nothing within that budget either */
                CHECK(rev_level_solve(target, (size_t)len, mi.node_count - 1, &mg) == REV_SOLVE_PROVEN_UNSAT);
            }
            direct[mi.node_count]++;
            strings++;
        }
        rev_fixed_bomb_histogram(b, len, hist);
        for (int n = 0; n <= FRB_MAX_NODES; n++) {
            CHECK(hist[n] == direct[n]);
            total += hist[n];
        }
        CHECK(total == (1u << len) && hist[0] == 0);
    }

    /* One bit too long: nothing is claimed, and the bound that is given holds. */
    for (int extra = 1; extra <= 3; extra++) {
        int len = bits + extra;
        for (int k = 0; k < 40; k++) {
            RevFixedBombInfo info;
            RevMinimalInfo mi;
            RevGraph g, mg;
            for (int i = 0; i < len; i++) target[i] = (char)('0' + rnd(2));
            target[len] = '\0';
            g.node_count = 77;
            CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, 0, &g, &info) == REV_FIXED_BOMB_UNSUPPORTED);
            CHECK(g.node_count == 0 && info.min_nodes == 0 && info.bomb_bits == bits);
            CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, 1, &g, &info) == REV_FIXED_BOMB_UNSUPPORTED);
            CHECK(rev_minimal_solve(target, (size_t)len, 0, NULL, &mg, &mi) == REV_MIN_MINIMAL);
            CHECK_MSG(info.lower_bound >= 1 && info.lower_bound <= mi.node_count,
                      "%s: bound %d, minimum %d", target, info.lower_bound, mi.node_count);
            CHECK(rev_fixed_bomb_lower_bound(b, target, (size_t)len) == info.lower_bound);
        }
    }

    /* What is not a question. */
    {
        RevFixedBombInfo info;
        RevGraph g;
        frb_target_format(0, bits, target);
        g.node_count = 77;
        CHECK(rev_fixed_bomb_solve(b, NULL, 3, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT && g.node_count == 0);
        CHECK(rev_fixed_bomb_solve(b, target, 0, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(rev_fixed_bomb_solve(b, "2", 1, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(rev_fixed_bomb_solve(b, "0 ", 2, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(rev_fixed_bomb_solve(b, target, (size_t)bits, -1, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(rev_fixed_bomb_solve(NULL, target, (size_t)bits, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(info.min_nodes == 0 && info.lower_bound == 0);
        /* a longer target with a bad character is invalid, not unsupported */
        memset(target, '0', (size_t)bits + 2);
        target[bits + 1] = 'x';
        CHECK(rev_fixed_bomb_solve(b, target, (size_t)bits + 2, 0, &g, &info) == REV_FIXED_BOMB_INVALID_INPUT);
        CHECK(rev_fixed_bomb_lower_bound(b, target, (size_t)bits + 2) == 0);
        CHECK(rev_fixed_bomb_lower_bound(b, NULL, 1) == 0 && rev_fixed_bomb_lower_bound(NULL, "0", 1) == 0);
        CHECK(rev_fixed_bomb_min_nodes(b, 0, 0) == 0 && rev_fixed_bomb_min_nodes(b, bits + 1, 0) == 0);
        CHECK(rev_fixed_bomb_min_nodes(b, bits, 1u << bits) == 0 && rev_fixed_bomb_min_nodes(NULL, 1, 0) == 0);
        CHECK(rev_fixed_bomb_extension(b, 1, 2) == 0 && rev_fixed_bomb_extension(b, -1, 0) == 0);
        /* result and info are optional */
        frb_target_format(1, bits, target);
        CHECK(rev_fixed_bomb_solve(b, target, (size_t)bits, 0, NULL, NULL) == REV_FIXED_BOMB_MINIMAL);
    }
    rev_fixed_bomb_close(b);
    frb_table_destroy(&t);
    printf("FixedRunBomb(%d): all %llu strings of 1 to %d bits agree with the MinimalSolver\n", bits,
           strings, bits);
}

/* The same minimum graph with an unreachable node added: a valid REV for
 * the same string that is not a minimum. */
static void pad_entry(FrbTable *t, uint32_t index) {
    RevGraph g;
    FrbStatus st;
    CHECK(frb_table_get(t, index, &st, &g) == FRB_OK && g.node_count < FRB_NODE_CAPACITY);
    g.nodes[g.node_count].color = REV_WHITE;
    g.nodes[g.node_count].links[0] = 0;
    g.nodes[g.node_count].links[1] = 0;
    g.node_count++;
    CHECK(frb_table_put(t, index, FRB_MINIMAL, &g) == FRB_OK);
}

static void fix_crcs(uint8_t *image, size_t size) {
    uint32_t crc = frb_crc32(0, image + FRB_HEADER_BYTES, size - FRB_HEADER_BYTES);
    for (int i = 0; i < 4; i++) image[36 + i] = (uint8_t)(crc >> (8 * i));
    crc = frb_crc32(0, image, 60);
    for (int i = 0; i < 4; i++) image[60 + i] = (uint8_t)(crc >> (8 * i));
}

static FrbError open_image(const uint8_t *image, size_t size, uint32_t *bad) {
    const char *file = in_scratch("mutated.frb");
    RevFixedBomb *b = (RevFixedBomb *)&b; /* must come back NULL on failure */
    FrbError e;
    CHECK(frb_write_atomic(file, image, size) == FRB_OK);
    e = rev_fixed_bomb_open(file, &b, bad);
    CHECK((e == FRB_OK) == (b != NULL));
    rev_fixed_bomb_close(b);
    return e;
}

/* Bombs that must not be used, each refused for its own reason. */
static void test_rejections(void) {
    const char *file = in_scratch("good.frb");
    RevFixedBomb *b = NULL;
    FrbTable good, t;
    RevGraph g;
    uint8_t *image = NULL, *copy;
    size_t size = 0;
    uint32_t bad;

    solver_table(&good, 8);
    CHECK(frb_file_write(file, &good) == FRB_OK);
    CHECK(rev_fixed_bomb_open(file, &b, NULL) == FRB_OK && rev_fixed_bomb_bits(b) == 8);
    rev_fixed_bomb_close(b);
    rev_fixed_bomb_close(NULL);

    /* Missing. */
    b = (RevFixedBomb *)&b;
    CHECK(rev_fixed_bomb_open(in_scratch("no-such.frb"), &b, NULL) == FRB_ERR_IO && b == NULL);
    CHECK(rev_fixed_bomb_open(NULL, &b, NULL) != FRB_OK && b == NULL);
    CHECK(rev_fixed_bomb_open(file, NULL, NULL) == FRB_ERR_ARG);

    /* Incompletely certified: one entry that is a valid witness and no more ... */
    for (int kind = 0; kind < 4; kind++) {
        static const FrbStatus status[4] = {FRB_WITNESS, FRB_EMPTY, FRB_UNRESOLVED, FRB_UNSUPPORTED};
        copy_table(&t, &good);
        frb_ring_witness(200, 8, &g);
        CHECK(frb_table_put(&t, 200, status[kind], kind == 0 ? &g : NULL) == FRB_OK);
        bad = 0;
        b = (RevFixedBomb *)&b;
        CHECK(rev_fixed_bomb_from_table(&t, &b, &bad) == FRB_ERR_INCOMPLETE && bad == 200 && b == NULL);
        CHECK(t.entries != NULL && t.count == 256); /* a refused table stays with the caller */
        CHECK(frb_file_write(in_scratch("partial.frb"), &t) == FRB_OK);
        bad = 0;
        CHECK(rev_fixed_bomb_open(in_scratch("partial.frb"), &b, &bad) == FRB_ERR_INCOMPLETE && bad == 200);
        CHECK(b == NULL);
        /* ... also when its header claims otherwise. */
        if (kind == 0) {
            CHECK(frb_image_build(&t, &image, &size) == FRB_OK);
            image[20] |= 1; /* the complete flag */
            fix_crcs(image, size);
            CHECK(open_image(image, size, NULL) == FRB_ERR_SUMMARY);
            free(image);
        }
        frb_table_destroy(&t);
    }

    /* A "minimum" that its own complement contradicts. */
    copy_table(&t, &good);
    pad_entry(&t, 77);
    bad = 0;
    CHECK(rev_fixed_bomb_from_table(&t, &b, &bad) == FRB_ERR_EVIDENCE && bad == 77 && b == NULL);
    CHECK(frb_file_write(in_scratch("lopsided.frb"), &t) == FRB_OK);
    CHECK(rev_fixed_bomb_open(in_scratch("lopsided.frb"), &b, &bad) == FRB_ERR_EVIDENCE && b == NULL);
    /* Padded on both sides the table is consistent with itself, and opens.
     * Nothing in a table can show that a larger graph is not the smallest;
     * that takes the evidence of the construction, or a recomputation, and
     * a recomputation does show it. */
    pad_entry(&t, 77 ^ 255);
    CHECK(rev_fixed_bomb_from_table(&t, &b, NULL) == FRB_OK && b != NULL);
    if (b) {
        RevMinimalInfo mi;
        CHECK(rev_minimal_solve("01001101", 8, 0, NULL, &g, &mi) == REV_MIN_MINIMAL); /* 77 */
        CHECK(rev_fixed_bomb_min_nodes(b, 8, 77) == mi.node_count + 1);
        rev_fixed_bomb_close(b);
        b = NULL;
    }
    frb_table_destroy(&t);

    /* Damage after opening: the memory of an open Bomb overwritten.  The
     * graph is run again on every answer, so the solver hands back nothing
     * rather than a graph that does not emit the target. */
    {
        RevFixedBombInfo info;
        uint8_t *raw;
        copy_table(&t, &good);
        raw = t.entries; /* about to become the solver's own storage */
        CHECK(rev_fixed_bomb_from_table(&t, &b, NULL) == FRB_OK && b != NULL);
        if (b) {
            CHECK(rev_fixed_bomb_solve(b, "01001101", 8, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL);
            /* entry 77 now holds the graph of its complement: valid, same size, wrong string */
            memcpy(raw + 77 * FRB_ENTRY_BYTES, raw + (77 ^ 255) * FRB_ENTRY_BYTES, FRB_GRAPH_BYTES);
            g.node_count = 77;
            CHECK(rev_fixed_bomb_solve(b, "01001101", 8, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(g.node_count == 0);
            CHECK(rev_fixed_bomb_solve(b, "01001101", 8, 0, NULL, NULL) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(rev_fixed_bomb_solve(b, "01001100", 8, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL); /* others unharmed */
            raw[78 * FRB_ENTRY_BYTES] |= 0x1E; /* entry 78: a link to a node that is not there */
            CHECK(rev_fixed_bomb_solve(b, "01001110", 8, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(g.node_count == 0);
            rev_fixed_bomb_close(b);
            b = NULL;
        }
    }

    /* Not a table at all, as memory. */
    CHECK(rev_fixed_bomb_from_table(NULL, &b, NULL) == FRB_ERR_ARG);
    memset(&t, 0, sizeof t);
    CHECK(rev_fixed_bomb_from_table(&t, &b, NULL) == FRB_ERR_ARG && b == NULL);
    copy_table(&t, &good);
    CHECK(rev_fixed_bomb_from_table(&t, NULL, NULL) == FRB_ERR_ARG && t.entries != NULL);
    t.entries[5 * FRB_ENTRY_BYTES] ^= 0x1E; /* entry 5: link 0 of the root now points nowhere */
    bad = 0;
    {
        FrbError e = rev_fixed_bomb_from_table(&t, &b, &bad);
        CHECK((e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT) && bad == 5 && b == NULL);
    }
    frb_table_destroy(&t);
    copy_table(&t, &good); /* two entries exchanged: fine graphs in the wrong places */
    {
        uint8_t tmp[FRB_ENTRY_BYTES];
        memcpy(tmp, t.entries + 9 * FRB_ENTRY_BYTES, FRB_ENTRY_BYTES);
        memcpy(t.entries + 9 * FRB_ENTRY_BYTES, t.entries + 100 * FRB_ENTRY_BYTES, FRB_ENTRY_BYTES);
        memcpy(t.entries + 100 * FRB_ENTRY_BYTES, tmp, FRB_ENTRY_BYTES);
        bad = 0;
        CHECK(rev_fixed_bomb_from_table(&t, &b, &bad) == FRB_ERR_OUTPUT && bad == 9 && b == NULL);
    }
    frb_table_destroy(&t);

    /* Damaged files. */
    CHECK(frb_image_build(&good, &image, &size) == FRB_OK);
    copy = malloc(size + 8);
    CHECK(open_image(image, size, NULL) == FRB_OK);
    CHECK(open_image(image, size - 1, NULL) == FRB_ERR_SIZE);     /* cut short */
    CHECK(open_image(image, FRB_HEADER_BYTES, NULL) == FRB_ERR_SIZE);
    CHECK(open_image(image, 3, NULL) == FRB_ERR_MAGIC);
    CHECK(open_image(image, 0, NULL) == FRB_ERR_MAGIC);            /* empty */
    memcpy(copy, image, size);
    copy[size] = 0;
    CHECK(open_image(copy, size + 1, NULL) == FRB_ERR_SIZE);      /* too long */
    memcpy(copy, image, size);
    copy[FRB_HEADER_BYTES + 1000] ^= 0x04;
    CHECK(open_image(copy, size, NULL) == FRB_ERR_CHECKSUM);      /* a flipped bit */
    memcpy(copy, image, size);
    copy[30] ^= 0x01;
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);        /* in the header */
    memcpy(copy, image, size);
    copy[8] = 5;
    fix_crcs(copy, size);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_VERSION);       /* a format this code does not read */
    memcpy(copy, image, size);
    copy[8] = 4;
    fix_crcs(copy, size);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);        /* nor is it a compact file */
    memcpy(copy, image, size);
    copy[8] = 3;
    fix_crcs(copy, size);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);        /* nor is it version 3 */
    memcpy(copy, image, size);
    copy[8] = 2;
    fix_crcs(copy, size);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);        /* version 2 has other entries */
    memcpy(copy, image, size);
    copy[0] = 'X';
    CHECK(open_image(copy, size, NULL) == FRB_ERR_MAGIC);
    CHECK(open_image((const uint8_t *)"REV 1\nnodes 1\n0 W 0 0\n", 22, NULL) == FRB_ERR_MAGIC);
    /* Damage behind repaired checksums. */
    memcpy(copy, image, size);
    memcpy(copy + FRB_HEADER_BYTES + 31 * FRB_ENTRY_BYTES, image + FRB_HEADER_BYTES + 32 * FRB_ENTRY_BYTES,
           FRB_ENTRY_BYTES); /* entry 31 holds the graph of entry 32 */
    fix_crcs(copy, size);
    bad = 0;
    CHECK(open_image(copy, size, &bad) == FRB_ERR_OUTPUT && bad == 31);
    memcpy(copy, image, size);
    copy[FRB_HEADER_BYTES + 40 * FRB_ENTRY_BYTES + FRB_GRAPH_BYTES] = (uint8_t)(7u << 5); /* unknown status */
    fix_crcs(copy, size);
    bad = 0;
    CHECK(open_image(copy, size, &bad) == FRB_ERR_ENTRY && bad == 40);
    memcpy(copy, image, size);
    copy[FRB_HEADER_BYTES + 12 * FRB_ENTRY_BYTES + 17] |= 0x80; /* bits in an unused node slot */
    fix_crcs(copy, size);
    bad = 0;
    CHECK(open_image(copy, size, &bad) == FRB_ERR_ENTRY && bad == 12);
    free(copy);
    free(image);
    frb_table_destroy(&good);
    printf("rejections: missing, cut, damaged, foreign, partial and self-contradicting Bombs are refused\n");
}

/* ---- part 2: the 16-bit Bomb --------------------------------------------- */

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *de;
        while (d && (de = readdir(d)) != NULL) {
            char child[2400];
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            snprintf(child, sizeof child, "%s/%s", path, de->d_name);
            remove_tree(child);
        }
        if (d) closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}
#endif

/* Make a complete FixedRunBomb(16) at `file`: with the constructor of
 * Milestone 2 where it exists, else string by string with the MinimalSolver. */
static void make_bomb_16(const char *file) {
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    const char *dir = in_scratch("work16");
    char err[FRB_ERR_TEXT] = "";
    FrbRunOptions opt;
    FrbRunResult res;
    FrbConfig cfg;
    FrbError e;
    remove_tree(dir);
    frb_config_default(&cfg, 16);
    cfg.enumerate_max = 4;
    cfg.workers = 2;
    snprintf(cfg.engine, sizeof cfg.engine, "%s", engine_name);
    frb_run_options_default(&opt);
    opt.solve = engine;
    opt.progress_seconds = 0;
    e = frb_work_create(dir, &cfg, err, sizeof err);
    if (e == FRB_OK) e = frb_work_run(dir, &cfg, &opt, &res, err, sizeof err);
    if (e == FRB_OK) e = frb_work_finalize(dir, file, 0, NULL, err, sizeof err);
    CHECK_MSG(e == FRB_OK, "building the 16-bit Bomb: %s (%s)", frb_error_str(e), err);
    remove_tree(dir);
#else
    char target[FRB_MAX_BITS + 1];
    FrbTable t;
    RevGraph g;
    CHECK(frb_table_create(&t, 16) == FRB_OK);
    for (uint32_t i = 0; i < t.count; i++) {
        frb_target_format(i, 16, target);
        CHECK(rev_minimal_solve_with(engine, target, 16, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
    CHECK(frb_file_write(file, &t) == FRB_OK);
    frb_table_destroy(&t);
#endif
}

/* Minimum-node counts of all strings of each length, 1 to 16 bits: the table
 * of benchmarks/FIXED_BOMB_SOLVER.md.  Rows 10, 12, 14 and 16 were first
 * obtained by running the MinimalSolver over all strings of that length,
 * before this solver existed; test_cross_check below re-derives every row. */
static const uint32_t atlas[17][8] = {
    {0},
    {0, 2},
    {0, 2, 2},
    {0, 2, 4, 2},
    {0, 2, 6, 8},
    {0, 2, 6, 20, 4},
    {0, 2, 6, 38, 18},
    {0, 2, 6, 50, 68, 2},
    {0, 2, 6, 60, 176, 12},
    {0, 2, 6, 62, 348, 94},
    {0, 2, 6, 64, 562, 388, 2},
    {0, 2, 6, 64, 726, 1226, 24},
    {0, 2, 6, 64, 830, 2994, 200},
    {0, 2, 6, 64, 910, 5836, 1372, 2},
    {0, 2, 6, 64, 946, 9292, 6066, 8},
    {0, 2, 6, 64, 956, 12210, 19430, 100},
    {0, 2, 6, 64, 962, 14348, 48956, 1198},
};

/* Validation 1, 2, 3, 5: every string of 1..16 bits; the graph emits it; the
 * index agrees with a direct scan; ties go to the lowest extension. */
static void test_every_string(const RevFixedBomb *b, const FrbTable *t) {
    char target[FRB_MAX_BITS + 1];
    unsigned long long strings = 0, wrong = 0, ties = 0;
    for (int len = 1; len <= 16; len++) {
        uint32_t hist[FRB_MAX_NODES + 1];
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevFixedBombInfo info;
            RevGraph g, stored;
            FrbStatus st;
            uint32_t want_index, mask = (1u << len) - 1u;
            int want_nodes, ok;
            scan(t, len, v, &want_nodes, &want_index);
            frb_target_format(v, len, target);
            ok = rev_fixed_bomb_solve(b, target, (size_t)len, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL;
            ok = ok && info.min_nodes == want_nodes && info.extension == want_index;
            ok = ok && (want_index >> (16 - len)) == v;
            ok = ok && g.node_count == want_nodes;
            ok = ok && rev_verify_prefix(&g, target, (size_t)len, NULL) == REV_OK; /* independent run */
            /* the graph is the Bomb's own, unchanged */
            ok = ok && frb_table_get(t, want_index, &st, &stored) == FRB_OK && same_graph(&g, &stored);
            ok = ok && rev_fixed_bomb_min_nodes(b, len, v) == want_nodes;
            ok = ok && rev_fixed_bomb_extension(b, len, v) == want_index;
            /* a string and its complement */
            ok = ok && rev_fixed_bomb_min_nodes(b, len, v ^ mask) == want_nodes;
            /* a string never needs more nodes than a string it starts */
            if (len < 16)
                ok = ok && rev_fixed_bomb_min_nodes(b, len + 1, 2 * v) >= want_nodes &&
                     rev_fixed_bomb_min_nodes(b, len + 1, 2 * v + 1) >= want_nodes;
            if (len < 16) { /* is the lowest extension really preferred over another just as small? */
                int n2;
                uint32_t i2;
                for (uint32_t i = want_index + 1; i < ((v + 1) << (16 - len)); i++)
                    if (entry_nodes(t, i) == want_nodes) {
                        ties++;
                        break;
                    }
                scan(t, len, v, &n2, &i2);
                ok = ok && i2 == want_index;
            }
            strings++;
            if (!ok && wrong++ < 5) CHECK_MSG(ok, "wrong answer for %s", target);
        }
        rev_fixed_bomb_histogram(b, len, hist);
        for (int n = 0; n <= FRB_MAX_NODES; n++)
            CHECK_MSG(hist[n] == (n < 8 ? atlas[len][n] : 0), "%d-bit strings needing %d nodes: %u", len, n,
                      (unsigned)hist[n]);
    }
    CHECK(strings == 131070 && wrong == 0);
    CHECK(ties > 30000); /* the tie-break is exercised, not vacuous */
    CHECK(rev_fixed_bomb_bits(b) == 16 && rev_fixed_bomb_max_nodes(b) == 7);
    printf("every string: %llu strings of 1 to 16 bits agree with a direct scan of the table;\n"
           "              %llu of them had more than one smallest extension and got the lowest\n",
           strings, ties);
}

/* Validation 4: the MinimalSolver, started from one node, for every string. */
static void test_cross_check(const RevFixedBomb *b, int cross_bits) {
    char target[FRB_MAX_BITS + 1];
    unsigned long long strings = 0, wrong = 0, steps = 0;
    for (int len = 1; len <= cross_bits; len++) {
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevMinimalInfo mi;
            RevGraph g;
            frb_target_format(v, len, target);
            RevMinimalStatus s = rev_minimal_solve_with(engine, target, (size_t)len, 0, NULL, &g, &mi);
            if (s != REV_MIN_MINIMAL || mi.node_count != rev_fixed_bomb_min_nodes(b, len, v))
                if (wrong++ < 5)
                    CHECK_MSG(0, "%s: MinimalSolver %s/%d, Bomb %d", target, rev_minimal_status_str(s),
                              mi.node_count, rev_fixed_bomb_min_nodes(b, len, v));
            steps += mi.steps;
            strings++;
        }
    }
    CHECK(wrong == 0);
    printf("cross-check: MinimalSolver (engine %s) agrees on all %llu strings of 1 to %d bits "
           "(%.3g search steps)\n",
           engine_name, strings, cross_bits, (double)steps);
}

/* The REV of n + 1 nodes that emits what g emits after its first j bits: a
 * new root that sends the control pointer and the flow pointer where g has
 * them one step later, and is never visited again.  README.md, "Longer
 * strings". */
static void shifted(const RevGraph *g, int j, RevGraph *out) {
    RevMachine m;
    int fp, cp, cp1, fp1, bit;
    rev_machine_reset(&m);
    for (int i = 0; i < j; i++) rev_machine_next_bit(g, &m);
    cp = m.cp;
    fp = j > 0 ? g->nodes[m.fp].links[g->nodes[m.cp].color] : m.fp; /* the FLOW still owed */
    cp1 = g->nodes[cp].links[g->nodes[fp].color];                   /* CTRL: emits bit j */
    bit = g->nodes[cp1].color;
    fp1 = g->nodes[fp].links[bit];                                  /* FLOW */
    memset(out, 0, sizeof *out);
    out->node_count = g->node_count + 1;
    out->nodes[0].color = 1 - bit;
    out->nodes[0].links[1 - bit] = cp1 + 1;
    out->nodes[0].links[bit] = fp1 + 1;
    for (int i = 0; i < g->node_count; i++) {
        out->nodes[i + 1].color = g->nodes[i].color;
        out->nodes[i + 1].links[0] = g->nodes[i].links[0] + 1;
        out->nodes[i + 1].links[1] = g->nodes[i].links[1] + 1;
    }
}

/* What the lower bound for longer strings rests on, on every 16-bit string
 * and every cut: the constructed graph does emit the remainder, so the
 * remainder needs at most one node more. */
static void test_suffix_lemma(const RevFixedBomb *b, const FrbTable *t) {
    unsigned long long cases = 0, bad_graph = 0, bad_count = 0, needs_more = 0;
    unsigned long long longer = 0, bad_bound = 0;
    for (uint32_t v = 0; v < 65536; v++) {
        RevGraph g, s;
        FrbStatus st;
        if (frb_table_get(t, v, &st, &g) != FRB_OK) {
            bad_graph++;
            continue;
        }
        /* The stored graph goes on emitting after 16 bits.  Each longer
         * string it emits is emitted with these nodes, and its first 16 bits
         * cannot be had with fewer: so that is its exact minimum, known
         * without a search.  A sound bound may not exceed it, and the first
         * 16 bits alone already give it: the bound must equal it. */
        {
            char text[32];
            RevMachine m;
            rev_machine_reset(&m);
            for (int i = 0; i < 31; i++) text[i] = (char)('0' + rev_machine_next_bit(&g, &m));
            for (size_t len = 17; len <= 31; len++) {
                if (rev_fixed_bomb_lower_bound(b, text, len) != g.node_count) bad_bound++;
                longer++;
            }
        }
        for (int j = 1; j <= 15; j++) {
            int rest = 16 - j;
            uint32_t tail = v & ((1u << rest) - 1u);
            int m = rev_fixed_bomb_min_nodes(b, rest, tail);
            shifted(&g, j, &s);
            if (s.node_count != g.node_count + 1 || rev_graph_validate(&s) != REV_OK ||
                frb_graph_output(&s, rest) != tail)
                bad_graph++;
            if (m > g.node_count + 1) bad_count++;
            if (m > g.node_count) needs_more++;
            cases++;
        }
    }
    CHECK(cases == 65536ull * 15 && bad_graph == 0 && bad_count == 0);
    CHECK(needs_more > 0); /* the "one more" is not slack: it happens */
    CHECK_MSG(longer == 65536ull * 15 && bad_bound == 0, "%llu unsound or slack bounds", bad_bound);
    printf("suffix lemma: %llu cuts of 16-bit strings; the remainder never needs more than one node more,\n"
           "              and needs exactly one more in %llu of them\n"
           "lower bound:  exact for all %llu strings of 17 to 31 bits that the stored graphs go on to emit\n",
           cases, needs_more, longer);
}

/*
 * The same Bomb in the compact format (version 4), through the same solver:
 * every answer the fixed-width Bomb gives, for every string of 1 to B bits
 * and for longer targets, and the bytes of the fixed-width file back.
 */
static void test_compact_bomb(const RevFixedBomb *fixed, const char *fixed_file) {
    const char *path = in_scratch("compact.frb");
    char target[40];
    FrbTable t, back;
    FrbFileInfo info;
    RevFixedBomb *b = NULL;
    FrbCompact *c = NULL;
    uint8_t *image = NULL, *image2 = NULL;
    size_t size = 0, size2 = 0;
    unsigned long long strings = 0, wrong = 0;
    int bits;

    CHECK(frb_file_read(fixed_file, &t, &info, NULL) == FRB_OK);
    if (!t.entries) return;
    bits = t.bits;
    CHECK(frb_compact_write(path, &t, info.version, info.table_crc, 0) == FRB_OK);
    CHECK(rev_fixed_bomb_open(path, &b, NULL) == FRB_OK);
    if (!b) {
        frb_table_destroy(&t);
        return;
    }
    CHECK(rev_fixed_bomb_bits(b) == bits && rev_fixed_bomb_max_nodes(b) == rev_fixed_bomb_max_nodes(fixed));
    for (int len = 1; len <= bits; len++) {
        uint32_t h1[FRB_MAX_NODES + 1], h2[FRB_MAX_NODES + 1];
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevFixedBombInfo i1, i2;
            RevGraph g1, g2;
            int ok;
            frb_target_format(v, len, target);
            ok = rev_fixed_bomb_solve(fixed, target, (size_t)len, 0, &g1, &i1) == REV_FIXED_BOMB_MINIMAL;
            ok = ok && rev_fixed_bomb_solve(b, target, (size_t)len, 0, &g2, &i2) == REV_FIXED_BOMB_MINIMAL;
            ok = ok && i1.min_nodes == i2.min_nodes && i1.extension == i2.extension && i1.lower_bound == i2.lower_bound;
            ok = ok && same_graph(&g1, &g2) && i2.bomb_bits == bits;
            ok = ok && rev_fixed_bomb_min_nodes(b, len, v) == i1.min_nodes;
            ok = ok && rev_fixed_bomb_extension(b, len, v) == i1.extension;
            if ((v & 63u) == 5u) {
                /* A budget one below the minimum is refused the same way, and no REV comes with
                 * the refusal; a budget of exactly the minimum is enough. */
                RevFixedBombResult r = rev_fixed_bomb_solve(b, target, (size_t)len, i1.min_nodes - 1, &g2, &i2);
                if (i1.min_nodes > 1)
                    ok = ok && r == REV_FIXED_BOMB_PROVEN_UNSAT && g2.node_count == 0 && i2.min_nodes == i1.min_nodes;
                else ok = ok && r == REV_FIXED_BOMB_MINIMAL;
                ok = ok && i2.extension == i1.extension;
                ok = ok && rev_fixed_bomb_solve(b, target, (size_t)len, i1.min_nodes, &g2, &i2) ==
                               REV_FIXED_BOMB_MINIMAL &&
                     same_graph(&g1, &g2) && i2.extension == i1.extension;
            }
            strings++;
            wrong += !ok;
        }
        rev_fixed_bomb_histogram(fixed, len, h1);
        rev_fixed_bomb_histogram(b, len, h2);
        CHECK(memcmp(h1, h2, sizeof h1) == 0);
    }
    CHECK_MSG(wrong == 0, "%llu strings answered differently by the compact Bomb", wrong);
    /* Longer targets: the same refusal and the same bound. */
    for (int r = 0; r < 2000; r++) {
        RevFixedBombInfo i1, i2;
        int len = bits + 1 + (int)rnd(20);
        for (int i = 0; i < len; i++) target[i] = (char)('0' + (int)rnd(2));
        target[len] = '\0';
        if (len > 38) continue;
        CHECK(rev_fixed_bomb_solve(fixed, target, (size_t)len, 0, NULL, &i1) == REV_FIXED_BOMB_UNSUPPORTED);
        CHECK(rev_fixed_bomb_solve(b, target, (size_t)len, 0, NULL, &i2) == REV_FIXED_BOMB_UNSUPPORTED);
        CHECK(i1.lower_bound == i2.lower_bound &&
              rev_fixed_bomb_lower_bound(b, target, (size_t)len) == i1.lower_bound);
    }
    rev_fixed_bomb_reindex(b); /* nothing to rebuild; nothing changes */
    CHECK(rev_fixed_bomb_min_nodes(b, 7, 93) == rev_fixed_bomb_min_nodes(fixed, 7, 93));
    rev_fixed_bomb_close(b);

    /* And the compact file gives the fixed-width file back, byte for byte. */
    CHECK(frb_compact_open(path, &c, NULL) == FRB_OK);
    if (c) {
        CHECK(frb_compact_info(c)->source_version == info.version &&
              frb_compact_info(c)->source_table_crc == info.table_crc);
        CHECK(frb_compact_to_table(c, NULL, &back) == FRB_OK);
        CHECK(frb_image_build(&t, &image, &size) == FRB_OK && frb_image_build(&back, &image2, &size2) == FRB_OK);
        CHECK(size == size2 && image && image2 && memcmp(image, image2, size) == 0);
        printf("compact: the %d-bit Bomb in the compact format is %llu bytes against %llu; the solver gives\n"
               "         the same answer for all %llu strings, and the fixed-width file comes back byte for byte\n",
               bits, (unsigned long long)frb_compact_info(c)->file_bytes, (unsigned long long)size, strings);
        free(image);
        free(image2);
        frb_table_destroy(&back);
        frb_compact_close(c);
    }

    /* A compact file is valid with any REVs that emit their strings.  A Bomb is more: a string
     * and its complement need the same number of nodes, and the solver refuses a file in which
     * they do not, compact or not. */
    {
        const char *odd = in_scratch("compact-odd.frb");
        FrbStatus st;
        RevGraph g;
        uint32_t bad = 0, at = 5;
        CHECK(frb_table_get(&t, at, &st, &g) == FRB_OK && g.node_count < t.format->node_capacity);
        g.nodes[g.node_count].color = 0; /* a node no link leads to */
        g.nodes[g.node_count].links[0] = g.nodes[g.node_count].links[1] = 0;
        g.node_count++;
        CHECK(frb_table_put(&t, at, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_compact_write(odd, &t, 0, 0, 0) == FRB_OK);
        b = NULL;
        CHECK(rev_fixed_bomb_open(odd, &b, &bad) == FRB_ERR_EVIDENCE && b == NULL && bad == at);
        rev_fixed_bomb_close(b);
        remove(odd);
    }
    frb_table_destroy(&t);
}

/* Longer targets: never solved, and the bound that is reported is a bound. */
static void test_longer(const RevFixedBomb *b) {
    static const char *const regular[] = {
        "00000000000000000000000000000000", "01010101010101010101010101010101",
        "00110011001100110011001100110011", "0101010101010101011",
        "000000000000000000000001",         "1111111111111111111111110",
        "01001000100001000001000000100000001",
    };
    char target[40];
    unsigned long long checked = 0, tight = 0;
    int largest = 0;
    for (int k = 0; k < 160 + (int)(sizeof regular / sizeof regular[0]); k++) {
        RevFixedBombInfo info;
        RevMinimalInfo mi;
        RevGraph g, mg;
        size_t len;
        if (k < 160) {
            len = 17 + (size_t)(k % 6); /* 17..22 random bits */
            for (size_t i = 0; i < len; i++) target[i] = (char)('0' + rnd(2));
            target[len] = '\0';
        } else {
            len = strlen(regular[k - 160]);
            memcpy(target, regular[k - 160], len + 1);
        }
        g.node_count = 77;
        CHECK(rev_fixed_bomb_solve(b, target, len, 0, &g, &info) == REV_FIXED_BOMB_UNSUPPORTED);
        CHECK(g.node_count == 0 && info.min_nodes == 0 && info.bomb_bits == 16);
        CHECK(rev_fixed_bomb_solve(b, target, len, 3, &g, &info) == REV_FIXED_BOMB_UNSUPPORTED);
        CHECK(rev_fixed_bomb_lower_bound(b, target, len) == info.lower_bound);
        CHECK(rev_minimal_solve_with(engine, target, len, 0, NULL, &mg, &mi) == REV_MIN_MINIMAL);
        CHECK_MSG(info.lower_bound >= 1 && info.lower_bound <= mi.node_count, "%s: bound %d, minimum %d",
                  target, info.lower_bound, mi.node_count);
        /* at least what the first 16 bits need */
        CHECK(info.lower_bound >= rev_fixed_bomb_lower_bound(b, target, 16));
        tight += info.lower_bound == mi.node_count;
        if (mi.node_count > largest) largest = mi.node_count;
        checked++;
    }
    /* A very long target costs one lookup per character.  The bound is
     * worked out again here, stretch by stretch, without the rolling window. */
    {
        static char big[100001];
        size_t n = sizeof big - 1;
        int want;
        for (size_t i = 0; i < n; i++) big[i] = (char)('0' + rnd(2));
        want = rev_fixed_bomb_lower_bound(b, big, 16);
        for (size_t j = 1; j < n; j++) {
            size_t take = n - j < 16 ? n - j : 16;
            int m = rev_fixed_bomb_lower_bound(b, big + j, take) - 1;
            if (m > want) want = m;
        }
        CHECK(want >= 6); /* among 100,000 stretches some need 7 nodes */
        CHECK(rev_fixed_bomb_lower_bound(b, big, n) == want);
        CHECK(rev_fixed_bomb_solve(b, big, n, 0, NULL, NULL) == REV_FIXED_BOMB_UNSUPPORTED);
    }
    printf("longer targets: %llu of 17 to 35 bits are all UNSUPPORTED; the reported bound never exceeds\n"
           "                the MinimalSolver's minimum (largest %d) and equals it for %llu\n",
           checked, largest, tight);
}

/* The same REV with nodes 1 and 2 exchanging names: another graph, the same
 * machine. */
static void renamed(const RevGraph *g, RevGraph *out) {
    static const int to[3] = {0, 2, 1};
    *out = *g;
    if (g->node_count < 3) return;
    for (int i = 0; i < g->node_count; i++) {
        int at = i < 3 ? to[i] : i;
        out->nodes[at].color = g->nodes[i].color;
        for (int k = 0; k < 2; k++)
            out->nodes[at].links[k] = g->nodes[i].links[k] < 3 ? to[g->nodes[i].links[k]] : g->nodes[i].links[k];
    }
}

/* Validation 5 again: another Bomb with the same node counts and other
 * graphs selects the same extensions, and returns its own graphs. */
static void test_other_witnesses(const RevFixedBomb *b, const FrbTable *t) {
    char target[FRB_MAX_BITS + 1];
    RevFixedBomb *other = NULL;
    FrbTable twin;
    unsigned long long differ = 0, wrong = 0, other_graph = 0;
    CHECK(frb_table_create(&twin, 16) == FRB_OK);
    for (uint32_t i = 0; i < 65536; i++) {
        RevGraph g, r;
        FrbStatus st;
        CHECK(frb_table_get(t, i, &st, &g) == FRB_OK);
        renamed(&g, &r);
        if (frb_table_put(&twin, i, FRB_MINIMAL, &r) != FRB_OK) wrong++; /* put runs the graph */
        differ += !same_graph(&g, &r);
    }
    CHECK(wrong == 0 && differ > 60000);
    CHECK(rev_fixed_bomb_from_table(&twin, &other, NULL) == FRB_OK);
    if (!other) return;
    for (int len = 1; len <= 16; len++)
        for (uint32_t v = 0; v < (1u << len); v++) {
            RevGraph mine, theirs;
            if (rev_fixed_bomb_min_nodes(other, len, v) != rev_fixed_bomb_min_nodes(b, len, v) ||
                rev_fixed_bomb_extension(other, len, v) != rev_fixed_bomb_extension(b, len, v))
                wrong++;
            frb_target_format(v, len, target);
            if (rev_fixed_bomb_solve(b, target, (size_t)len, 0, &mine, NULL) != REV_FIXED_BOMB_MINIMAL ||
                rev_fixed_bomb_solve(other, target, (size_t)len, 0, &theirs, NULL) != REV_FIXED_BOMB_MINIMAL ||
                mine.node_count != theirs.node_count)
                wrong++;
            else other_graph += !same_graph(&mine, &theirs);
        }
    CHECK(wrong == 0 && other_graph > 100000);
    rev_fixed_bomb_close(other);
    printf("tie-break: a Bomb holding other graphs (%llu of 65536 entries differ) selects the same extension\n"
           "           for every string\n",
           differ);
}

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
/*
 * A Bomb of more than 16 bits: format version 2 and an index of 32-bit
 * numbers.  The 17-bit table is built here from the 16-bit one (one rung of
 * the ladder), and everything it says about strings of up to 16 bits must
 * be what the 16-bit Bomb says.
 */
static void test_longer_bomb(const RevFixedBomb *b16, const char *file16) {
    static const uint32_t atlas17[9] = {0, 2, 6, 64, 966, 15740, 102914, 11378, 2};
    const char *dir = in_scratch("ladder");
    char file17[1400], target[FRB_MAX_BITS + 2], err[FRB_ERR_TEXT] = "";
    uint32_t hist[FRB_MAX_NODES + 1];
    unsigned long long wrong = 0, strings = 0;
    FrbRunOptions opt;
    FrbLadderResult res;
    FrbConfig settings;
    RevFixedBomb *b = NULL;
    RevFixedBombInfo info;
    FrbTable t;
    RevGraph g;
    FrbError e;

    remove_tree(dir);
    frb_config_default(&settings, 1);
    settings.workers = 4;
    settings.format = 0;
    settings.max_nodes = 0;
    snprintf(settings.engine, sizeof settings.engine, "%s", engine_name);
    frb_run_options_default(&opt);
    opt.solve = engine;
    opt.progress_seconds = 0;
    e = frb_ladder_run(dir, file16, 17, &settings, &opt, &res, err, sizeof err);
    CHECK_MSG(e == FRB_OK && res.complete, "one rung up from the 16-bit Bomb: %s (%s)", frb_error_str(e), err);
    snprintf(file17, sizeof file17, "%s/fixed-run-17.frb", dir);
    CHECK(frb_file_read(file17, &t, NULL, NULL) == FRB_OK && t.format->version == 2);
    CHECK(rev_fixed_bomb_open(file17, &b, NULL) == FRB_OK);
    if (!b || !t.entries) return;
    CHECK(rev_fixed_bomb_bits(b) == 17 && rev_fixed_bomb_max_nodes(b) == 8);

    for (int len = 1; len <= 17; len++) {
        for (uint32_t v = 0; v < (1u << len); v++) {
            int want = 99, ok;
            uint32_t want_index = 0, first = v << (17 - len), span = 1u << (17 - len);
            for (uint32_t i = first; i < first + span; i++) /* direct scan of the table */
                if (frb_table_nodes(&t, i) < want) {
                    want = frb_table_nodes(&t, i);
                    want_index = i;
                }
            frb_target_format(v, len, target);
            ok = rev_fixed_bomb_solve(b, target, (size_t)len, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL;
            ok = ok && info.min_nodes == want && info.extension == want_index && info.bomb_bits == 17;
            ok = ok && g.node_count == want && rev_verify_prefix(&g, target, (size_t)len, NULL) == REV_OK;
            ok = ok && rev_fixed_bomb_min_nodes(b, len, v) == want;
            ok = ok && rev_fixed_bomb_extension(b, len, v) == want_index;
            /* what the 16-bit Bomb says about the same string */
            if (len <= 16) ok = ok && rev_fixed_bomb_min_nodes(b16, len, v) == want;
            else /* not fewer nodes than its first 16 bits; at most one fewer than its last 16 */
                ok = ok && want >= rev_fixed_bomb_min_nodes(b16, 16, v >> 1) &&
                     want + 1 >= rev_fixed_bomb_min_nodes(b16, 16, v & 0xFFFFu) && info.extension == v;
            if (len == 9) ok = ok && rev_fixed_bomb_solve(b, target, 9, want - 1, &g, &info) ==
                                         (want > 1 ? REV_FIXED_BOMB_PROVEN_UNSAT : REV_FIXED_BOMB_MINIMAL);
            strings++;
            if (!ok && wrong++ < 5) CHECK_MSG(ok, "17-bit Bomb: wrong answer for %s", target);
        }
        rev_fixed_bomb_histogram(b, len, hist);
        for (int n = 0; n <= FRB_MAX_NODES; n++) {
            uint32_t expect = len == 17 ? (n <= 8 ? atlas17[n] : 0) : (n < 8 ? atlas[len][n] : 0);
            CHECK_MSG(hist[n] == expect, "17-bit Bomb, %d-bit strings needing %d nodes: %u", len, n,
                      (unsigned)hist[n]);
        }
    }
    CHECK(strings == 262142 && wrong == 0);
    /* One bit more than it holds.  Its last 17 bits need 8 nodes, so it needs 7 at least. */
    CHECK(rev_fixed_bomb_solve(b, "000000000000000001", 18, 0, &g, &info) == REV_FIXED_BOMB_UNSUPPORTED);
    CHECK(info.bomb_bits == 17 && info.lower_bound == 7 && g.node_count == 0);
    CHECK(rev_fixed_bomb_lower_bound(b, "000000000000000001", 18) == 7);
    CHECK(rev_fixed_bomb_lower_bound(b, "000000000000000010", 18) == 8); /* its first 17 need 8 */
    rev_fixed_bomb_close(b);
    frb_table_destroy(&t);
    printf("longer Bomb: a 17-bit Bomb (format 2, 32-bit index) answers all %llu strings of 1 to 17 bits;\n"
           "             for 1 to 16 bits exactly as the 16-bit Bomb does\n",
           strings);
}
#endif

int main(int argc, char **argv) {
    const char *bomb_file = NULL;
    char built[1300];
    RevFixedBomb *b = NULL;
    FrbTable t;
    int cross_bits = 16;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bomb") && i + 1 < argc) bomb_file = argv[++i];
        else if (!strcmp(argv[i], "--cross-bits") && i + 1 < argc) cross_bits = atoi(argv[++i]);
        else if (argv[i][0] != '-') snprintf(scratch, sizeof scratch, "%s", argv[i]);
        else {
            fprintf(stderr, "usage: test_fixed_bomb_solver [scratch-directory] [--bomb FILE] [--cross-bits N]\n");
            return 2;
        }
    }
    if (cross_bits < 0 || cross_bits > 16) cross_bits = 16;
#ifdef CARLOBOMB_HAVE_AMD64_ASM
    if (cb_amd64_kernel_select(cb_amd64_kernel_find("auto")) == 0) {
        engine = rev_level_solve_amd64_ex;
        engine_name = "amd64-auto";
    }
#endif
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    remove_tree(scratch);
    CHECK(mkdir(scratch, 0777) == 0);
#else
    /* Without POSIX the scratch directory has to exist already. */
#endif

    test_small(1);
    test_small(2);
    test_small(5);
    test_small(8);
    test_small(10);
    test_three_formats(9);
    test_rejections();

    if (!bomb_file) {
        snprintf(built, sizeof built, "%s/fixed-run-16.frb", scratch);
        make_bomb_16(built);
        bomb_file = built;
    }
    CHECK(frb_file_read(bomb_file, &t, NULL, NULL) == FRB_OK);
    CHECK(rev_fixed_bomb_open(bomb_file, &b, NULL) == FRB_OK);
    if (b && t.entries) {
        /* Building the index twice changes nothing. */
        uint32_t before = rev_fixed_bomb_extension(b, 7, 93);
        rev_fixed_bomb_reindex(b);
        CHECK(rev_fixed_bomb_extension(b, 7, 93) == before);

        test_every_string(b, &t);
        test_other_witnesses(b, &t);
        test_suffix_lemma(b, &t);
        test_longer(b);
        test_compact_bomb(b, bomb_file);
        if (cross_bits > 0) test_cross_check(b, cross_bits);
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
        if (cross_bits == 16) test_longer_bomb(b, bomb_file); /* 32,768 searches: not for a slow build */
#endif
    }
    rev_fixed_bomb_close(b);
    frb_table_destroy(&t);
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    if (!t_failures) remove_tree(scratch);
#endif
    return t_report("test_fixed_bomb_solver");
}
