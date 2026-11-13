/*
 * Tests of the compact FixedRunBomb format (version 4): the packing of a
 * class, the layout, conversion from the fixed-width formats and back, the
 * prefix index against a search of the table, and every kind of damage the
 * reader claims to refuse.
 *
 * Usage: test_compact [prefix-for-scratch-files]
 */
#include <stdint.h>
#include <stdlib.h>

#include "compact.h"
#include "minimal_solver.h"
#include "test_util.h"

enum { REF_MAX = 10 };

static uint64_t rng_state = 0xA4093822299F31D0ull;

static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)(((rng_state * 0x2545F4914F6CDD1Dull) >> 33) % n);
}

static void random_graph(RevGraph *g, int nodes) {
    memset(g, 0, sizeof *g);
    g->node_count = nodes;
    for (int i = 0; i < nodes; i++) {
        g->nodes[i].color = (int)rnd(2);
        g->nodes[i].links[0] = (int)rnd((uint32_t)nodes);
        g->nodes[i].links[1] = (int)rnd((uint32_t)nodes);
    }
}

static int same_graph(const RevGraph *a, const RevGraph *b) {
    if (a->node_count != b->node_count) return 0;
    for (int i = 0; i < a->node_count; i++)
        if (a->nodes[i].color != b->nodes[i].color || a->nodes[i].links[0] != b->nodes[i].links[0] ||
            a->nodes[i].links[1] != b->nodes[i].links[1])
            return 0;
    return 1;
}

static char scratch[1024] = "compact_test";

static const char *file(const char *name) {
    static char bufs[8][1200];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 8;
    snprintf(buf, sizeof bufs[0], "%s_%s", scratch, name);
    return buf;
}

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)n + 1);
        *size = fread(buf, 1, (size_t)n, f);
    }
    fclose(f);
    return buf;
}

static void spit(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL && fwrite(data, 1, size, f) == size && fclose(f) == 0);
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* Minimum-node REVs of every string of 1..REF_MAX bits, by the MinimalSolver. */
static RevGraph *ref_graph[REF_MAX + 1];

static void compute_reference(void) {
    char t[FRB_MAX_BITS + 1];
    for (int bits = 1; bits <= REF_MAX; bits++) {
        ref_graph[bits] = calloc((size_t)1 << bits, sizeof(RevGraph));
        for (uint32_t i = 0; i < (1u << bits); i++) {
            frb_target_format(i, bits, t);
            CHECK(rev_minimal_solve(t, (size_t)bits, 0, NULL, &ref_graph[bits][i], NULL) == REV_MIN_MINIMAL);
        }
    }
}

static void reference_table(FrbTable *t, int bits, const FrbFormat *format) {
    CHECK(frb_table_create_as(t, bits, format) == FRB_OK);
    for (uint32_t i = 0; i < t->count; i++)
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &ref_graph[bits][i]) == FRB_OK);
}

/* ---- one REV of a class -------------------------------------------------- */

static void test_strides(void) {
    static const struct { int n, w, stride; } known[] = {
        {1, 0, 1}, {2, 1, 1}, {3, 2, 2}, {4, 2, 3}, {5, 3, 5}, {6, 3, 6}, {7, 3, 7}, {8, 3, 7},
        {9, 4, 11}, {10, 4, 12}, {16, 4, 18}, {17, 5, 24}, {24, 5, 33}, {31, 5, 43}};
    for (size_t k = 0; k < sizeof known / sizeof known[0]; k++)
        CHECK(frb_compact_link_bits(known[k].n) == known[k].w && frb_compact_stride(known[k].n) == known[k].stride);
    for (int n = 1; n <= 31; n++) {
        int w = 0, bits;
        while ((1 << w) < n) w++; /* the fewest bits that tell n nodes apart */
        bits = n * (1 + 2 * w);
        CHECK(frb_compact_link_bits(n) == w && (n == 1 || (1 << (w - 1)) < n));
        CHECK(frb_compact_stride(n) * 8 >= bits && frb_compact_stride(n) * 8 < bits + 8);
        CHECK(frb_compact_stride(n) <= FRB_MAX_GRAPH_BYTES);
    }
    CHECK(frb_compact_link_bits(0) == -1 && frb_compact_link_bits(32) == -1);
    CHECK(frb_compact_stride(0) == 0 && frb_compact_stride(32) == 0 && frb_compact_stride(-4) == 0);
}

static unsigned bit_at(const uint8_t *p, int bit) { return ((unsigned)p[bit >> 3] >> (bit & 7)) & 1u; }

static void test_pack(void) {
    uint8_t packed[64], wide[FRB_MAX_GRAPH_BYTES];
    RevGraph g, back;

    /* The worked examples of FORMAT.md. */
    memset(&g, 0, sizeof g);
    g.node_count = 3; /* emits 010010001... */
    g.nodes[0].color = REV_BLACK; g.nodes[0].links[0] = 2; g.nodes[0].links[1] = 1;
    g.nodes[1].color = REV_WHITE; g.nodes[1].links[0] = 0; g.nodes[1].links[1] = 0;
    g.nodes[2].color = REV_WHITE; g.nodes[2].links[0] = 1; g.nodes[2].links[1] = 2;
    CHECK(frb_compact_pack(&g, packed) == FRB_OK && packed[0] == 0x0D && packed[1] == 0x48);
    CHECK(frb_compact_unpack(packed, 3, &back) == FRB_OK && same_graph(&g, &back));
    memset(&g, 0, sizeof g);
    g.node_count = 2;
    g.nodes[0].color = REV_WHITE; g.nodes[0].links[0] = 1; g.nodes[0].links[1] = 1;
    g.nodes[1].color = REV_BLACK; g.nodes[1].links[0] = 0; g.nodes[1].links[1] = 1;
    CHECK(frb_compact_pack(&g, packed) == FRB_OK && packed[0] == 0x2E);
    memset(&g, 0, sizeof g);
    g.node_count = 1; /* one node: a color and nothing else */
    g.nodes[0].color = REV_BLACK;
    CHECK(frb_compact_pack(&g, packed) == FRB_OK && packed[0] == 0x01);
    CHECK(frb_compact_unpack(packed, 1, &back) == FRB_OK && same_graph(&g, &back));
    g.nodes[0].color = REV_WHITE;
    CHECK(frb_compact_pack(&g, packed) == FRB_OK && packed[0] == 0x00);

    for (int n = 1; n <= 31; n++) {
        int w = frb_compact_link_bits(n), width = 1 + 2 * w, stride = frb_compact_stride(n);
        for (int r = 0; r < 400; r++) {
            int wrong = 0;
            random_graph(&g, n);
            memset(packed, 0xAA, sizeof packed);
            CHECK(frb_compact_pack(&g, packed) == FRB_OK);
            CHECK(packed[stride] == 0xAA); /* not a byte more than the stride */
            /* Node i at bits i*width: color, link 0, link 1; zero after the last. */
            for (int i = 0; i < n; i++) {
                unsigned v = (unsigned)g.nodes[i].color | ((unsigned)g.nodes[i].links[0] << 1) |
                             ((unsigned)g.nodes[i].links[1] << (1 + w));
                for (int k = 0; k < width; k++) wrong += bit_at(packed, i * width + k) != ((v >> k) & 1u);
            }
            for (int bit = n * width; bit < stride * 8; bit++) wrong += bit_at(packed, bit) != 0;
            CHECK(wrong == 0);
            CHECK(frb_compact_unpack(packed, n, &back) == FRB_OK && same_graph(&g, &back));
            /* Where the fixed-width formats use the same link width, the bytes are theirs. */
            if (n == 16) {
                CHECK(frb_graph_pack(&g, wide) == FRB_OK && memcmp(wide, packed, 18) == 0);
            }
            if (n > 16) {
                CHECK(frb_graph_pack_as(frb_format(3), &g, wide) == FRB_OK && memcmp(wide, packed, (size_t)stride) == 0);
            }
            /* A bit after the last node, and a link to a node that is not there. */
            if (n * width < stride * 8) {
                int bit = n * width + (int)rnd((uint32_t)(stride * 8 - n * width));
                packed[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                CHECK(frb_compact_unpack(packed, n, &back) == FRB_ERR_ENTRY && back.node_count == 0);
                packed[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
            }
            if (n < (1 << w)) {
                int node = (int)rnd((uint32_t)n), which = (int)rnd(2), at = node * width + 1 + which * w;
                int bad = n + (int)rnd((uint32_t)((1 << w) - n));
                for (int k = 0; k < w; k++) {
                    packed[(at + k) >> 3] &= (uint8_t)~(1u << ((at + k) & 7));
                    packed[(at + k) >> 3] |= (uint8_t)(((unsigned)(bad >> k) & 1u) << ((at + k) & 7));
                }
                CHECK(frb_compact_unpack(packed, n, &back) == FRB_ERR_ENTRY);
            }
        }
    }
    random_graph(&g, 32);
    CHECK(frb_compact_pack(&g, packed) == FRB_ERR_CAPACITY);
    random_graph(&g, 5);
    g.nodes[2].links[1] = 5; /* not a graph */
    CHECK(frb_compact_pack(&g, packed) == FRB_ERR_ARG);
    CHECK(frb_compact_unpack(packed, 0, &back) == FRB_ERR_ENTRY && frb_compact_unpack(packed, 32, &back) == FRB_ERR_ENTRY);
    CHECK(frb_compact_pack(NULL, packed) == FRB_ERR_ARG && frb_compact_unpack(NULL, 3, &back) == FRB_ERR_ARG);
}

static void test_layout(void) {
    /* The certified histogram of the 16-bit Bomb. */
    static const uint32_t h16[8] = {0, 2, 6, 64, 962, 14348, 48956, 1198};
    FrbCompactInfo info;
    uint64_t pool = 0;
    memset(&info, 0, sizeof info);
    for (int n = 1; n <= 7; n++) {
        info.class_count[n] = h16[n];
        pool += (uint64_t)h16[n] * (uint64_t)frb_compact_stride(n);
    }
    CHECK(pool == 376884);
    CHECK(frb_compact_layout(&info, 16, 0) == FRB_OK);
    CHECK(info.index_bytes == 2 && info.count == 65536 && info.index_entries == 131070 && info.max_nodes == 7);
    CHECK(info.index_offset == 1056 && info.pool_offset == 263200 && info.file_bytes == 263200 + 376884);
    CHECK(info.class_start[1] == 0 && info.class_start[2] == 2 && info.class_start[7] == 64338 &&
          info.class_start[8] == 65536 && info.class_offset[1] == 263200 && info.class_offset[2] == 263202);
    CHECK(info.class_offset[31] == info.file_bytes && info.class_stride[7] == 7);
    CHECK(frb_compact_layout(&info, 16, 4) == FRB_OK && info.pool_offset == 1056 + 131070 * 4 &&
          info.file_bytes == 1056 + 131070 * 4 + 376884);
    CHECK(frb_compact_layout(&info, 16, 3) == FRB_OK && info.pool_offset == 394272); /* 394266, to a multiple of 8 */
    CHECK(frb_compact_layout(&info, 16, 1) == FRB_ERR_ARG && frb_compact_layout(&info, 16, 5) == FRB_ERR_ARG);
    CHECK(frb_compact_layout(&info, 15, 0) == FRB_ERR_ARG); /* the counts are those of 16 bits */
    info.class_count[7]--;
    CHECK(frb_compact_layout(&info, 16, 0) == FRB_ERR_ARG);
    CHECK(frb_compact_layout(&info, 0, 0) == FRB_ERR_ARG && frb_compact_layout(&info, 32, 0) == FRB_ERR_ARG &&
          frb_compact_layout(NULL, 8, 0) == FRB_ERR_ARG);

    /* The largest a file can be: 31 bits, every REV of 31 nodes.  Nothing
     * overflows; an index place needs more than 32 bits there. */
    memset(&info, 0, sizeof info);
    info.class_count[31] = 0x80000000u;
    CHECK(frb_compact_layout(&info, 31, 0) == FRB_OK && info.index_bytes == 4);
    CHECK(info.index_entries == 0xFFFFFFFEull && info.pool_offset == 1056 + 0xFFFFFFFEull * 4);
    CHECK(info.class_offset[31] == info.pool_offset && info.file_bytes == info.pool_offset + 0x80000000ull * 43);
    CHECK(info.file_bytes == 109521667096ull);
    /* Widths: the fewest bytes that hold an ordinal of L bits. */
    for (int bits = 1; bits <= 31; bits++) {
        memset(&info, 0, sizeof info);
        info.class_count[1] = (uint32_t)1 << bits;
        CHECK(frb_compact_layout(&info, bits, 0) == FRB_OK && info.index_bytes == (bits + 7) / 8);
        CHECK(info.pool_offset % 8 == 0 && info.file_bytes == info.pool_offset + ((uint64_t)1 << bits));
    }
}

/* ---- conversion ---------------------------------------------------------- */

/* Fewest nodes among the extensions of a string in a table, lowest index
 * among equals: straight from the entries. */
static void scan(const FrbTable *t, int bits, uint32_t value, int *nodes, uint32_t *index) {
    int shift = t->bits - bits;
    uint32_t first = value << shift, span = (uint32_t)1 << shift;
    *nodes = 99;
    *index = 0;
    for (uint32_t i = first; i < first + span; i++)
        if (frb_table_nodes(t, i) < *nodes) {
            *nodes = frb_table_nodes(t, i);
            *index = i;
        }
}

/* A table made compact, opened, questioned for every string of every length,
 * and turned back into the bytes it came from. */
static void check_conversion(const FrbTable *t, int index_bytes, const char *what) {
    const char *path = file("c.frb");
    FrbCompact *b = NULL;
    const FrbCompactInfo *info;
    FrbTable back;
    uint8_t *image = NULL, *image2 = NULL, *bytes;
    size_t size = 0, size2 = 0, file_size;
    uint32_t bad = 0, *target_of;
    int L = t->bits, wrong = 0;
    FrbError e;

    remove(path);
    e = frb_compact_write(path, t, t->format->version, 0x12345678u, index_bytes);
    CHECK_MSG(e == FRB_OK, "%s: %s", what, frb_error_str(e));
    if (e != FRB_OK) return;
    CHECK(frb_file_is_compact(path) == 1 && !file_exists(file("c.frb.tmp")));
    e = frb_compact_open(path, &b, &bad);
    CHECK_MSG(e == FRB_OK, "%s: %s at %u", what, frb_error_str(e), (unsigned)bad);
    if (!b) return;
    info = frb_compact_info(b);
    bytes = slurp(path, &file_size);
    CHECK(bytes != NULL && file_size == info->file_bytes);
    free(bytes);
    CHECK(info->bits == L && info->count == t->count && info->source_version == t->format->version &&
          info->source_table_crc == 0x12345678u);
    CHECK(info->index_bytes == (index_bytes ? index_bytes : (L + 7) / 8));
    CHECK(frb_compact_equals_table(b, t, &bad) == FRB_OK);

    /* The ordinals are the places in the order (nodes, string). */
    target_of = malloc((size_t)t->count * sizeof *target_of);
    for (uint32_t i = 0; i < t->count; i++) {
        uint32_t o = frb_compact_ordinal(b, L, i);
        wrong += o >= t->count;
        if (o < t->count) target_of[o] = i;
    }
    for (uint32_t o = 1; o < t->count && !wrong; o++) {
        int n0 = frb_table_nodes(t, target_of[o - 1]), n1 = frb_table_nodes(t, target_of[o]);
        wrong += !(n0 < n1 || (n0 == n1 && target_of[o - 1] < target_of[o]));
        wrong += frb_compact_nodes_of_ordinal(b, o) != n1;
    }
    CHECK_MSG(wrong == 0, "%s: ordinals out of order", what);
    free(target_of);

    /* Every string of every length: fewest nodes, lowest extension, its REV. */
    for (int bits = 1; bits <= L; bits++) {
        for (uint32_t v = 0; v < (1u << bits); v++) {
            RevGraph g, stored, from_file;
            FrbCompactInfo finfo;
            FrbStatus st;
            uint32_t want_index, extension = 0;
            int want_nodes, ok;
            scan(t, bits, v, &want_nodes, &want_index);
            ok = frb_compact_nodes(b, bits, v) == want_nodes;
            ok = ok && frb_compact_ordinal(b, bits, v) == frb_compact_ordinal(b, L, want_index);
            ok = ok && frb_compact_graph(b, bits, v, &g) == FRB_OK && g.node_count == want_nodes;
            ok = ok && frb_graph_output(&g, L) == want_index;
            ok = ok && frb_table_get(t, want_index, &st, &stored) == FRB_OK && same_graph(&g, &stored);
            if ((v & 7u) == 3u || bits == L) { /* and without the rest of the file */
                ok = ok && frb_compact_file_lookup(path, bits, v, &finfo, &from_file, &extension) == FRB_OK;
                ok = ok && same_graph(&g, &from_file) && extension == want_index && finfo.file_bytes == info->file_bytes;
            }
            wrong += !ok;
        }
    }
    CHECK_MSG(wrong == 0, "%s: %d strings answered wrongly", what, wrong);
    CHECK(frb_compact_graph(b, 0, 0, &ref_graph[1][0]) == FRB_ERR_ARG);
    CHECK(frb_compact_graph(b, L + 1, 0, &ref_graph[1][0]) == FRB_ERR_ARG);
    CHECK(frb_compact_graph(b, L, t->count, &ref_graph[1][0]) == FRB_ERR_ARG);
    CHECK(frb_compact_file_lookup(path, L + 1, 0, NULL, NULL, NULL) == FRB_ERR_ARG);

    /* And back: the fixed-width file it was made from, byte for byte. */
    CHECK(frb_compact_to_table(b, NULL, &back) == FRB_OK);
    CHECK(frb_image_build(t, &image, &size) == FRB_OK && frb_image_build(&back, &image2, &size2) == FRB_OK);
    CHECK(size == size2 && image && image2 && memcmp(image, image2, size) == 0);
    free(image);
    free(image2);
    frb_table_destroy(&back);
    frb_compact_close(b);
    remove(path);
}

static void test_conversion(void) {
    char what[64];
    unsigned long long strings = 0;
    for (int bits = 1; bits <= REF_MAX; bits++) {
        for (int version = 1; version <= 3; version++) {
            FrbTable t;
            if ((bits > 6 && version != 1 + bits % 3) || (bits == REF_MAX && version != 3)) continue;
            reference_table(&t, bits, frb_format(version));
            snprintf(what, sizeof what, "%d bits, source version %d", bits, version);
            check_conversion(&t, 0, what);
            if (bits == 3 || bits == 8) /* every width that can hold the ordinals */
                for (int w = (bits + 7) / 8 + 1; w <= 4; w++) check_conversion(&t, w, what);
            {   /* complement symmetry holds in a real table */
                FrbCompact *b = NULL;
                CHECK(frb_compact_write(file("s.frb"), &t, version, 0, 0) == FRB_OK);
                CHECK(frb_compact_open(file("s.frb"), &b, NULL) == FRB_OK);
                if (b) CHECK(frb_compact_check_symmetry(b, NULL) == FRB_OK);
                frb_compact_close(b);
                remove(file("s.frb"));
            }
            strings += ((unsigned long long)2 << bits) - 2;
            frb_table_destroy(&t);
        }
    }
    printf("conversion: tables of 1 to %d bits in the three fixed-width versions made compact and back,\n"
           "            byte for byte; %llu strings of every length answered as a scan of the table answers\n",
           REF_MAX, strings);
}

/* Every class there is, 1 to 31 nodes: REVs with nodes added that nothing
 * leads to.  Not minima, and the test does not say so to anyone. */
static void test_all_classes(void) {
    FrbTable t;
    FrbCompact *b = NULL;
    uint32_t bad = 0;
    int classes = 0;
    CHECK(frb_table_create_as(&t, 8, frb_format(3)) == FRB_OK);
    for (uint32_t i = 0; i < 256; i++) {
        RevGraph g = ref_graph[8][i];
        int want = i < 31 ? (int)i + 1 : 0; /* strings 5 to 30 get 6 to 31 nodes; the rest keep theirs */
        while (g.node_count < want) { /* a node no link reaches */
            int k = g.node_count++;
            g.nodes[k].color = (int)rnd(2);
            g.nodes[k].links[0] = (int)rnd((uint32_t)g.node_count);
            g.nodes[k].links[1] = (int)rnd((uint32_t)g.node_count);
        }
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
    check_conversion(&t, 0, "all classes");
    check_conversion(&t, 4, "all classes, 4-byte index");
    CHECK(frb_compact_write(file("a.frb"), &t, 3, 0, 0) == FRB_OK);
    CHECK(frb_compact_open(file("a.frb"), &b, &bad) == FRB_OK);
    if (b) {
        const FrbCompactInfo *info = frb_compact_info(b);
        for (int n = 1; n <= 31; n++) classes += info->class_count[n] > 0;
        CHECK(classes == 31 && info->max_nodes == 31);
        /* This table is not symmetric in its node counts, and that is seen. */
        CHECK(frb_compact_check_symmetry(b, &bad) == FRB_ERR_EVIDENCE);
    }
    frb_compact_close(b);
    remove(file("a.frb"));
    frb_table_destroy(&t);
    printf("classes: a table with REVs of every size from 1 to 31 nodes converts and reads back\n");
}

/* ---- what is refused ----------------------------------------------------- */

/* Recompute every checksum of a compact image, so that what is wrong in it
 * has to be found by looking. */
static void repair(uint8_t *image, const FrbCompactInfo *info) {
    uint32_t crc;
    for (int n = 1; n <= 31; n++) {
        uint8_t *row = image + 64 + (size_t)(n - 1) * 32;
        crc = frb_crc32(0, image + info->class_offset[n], (size_t)info->class_count[n] * info->class_stride[n]);
        for (int i = 0; i < 4; i++) row[24 + i] = (uint8_t)(crc >> (8 * i));
    }
    crc = frb_crc32(0, image + 64, 31 * 32);
    for (int i = 0; i < 4; i++) image[36 + i] = (uint8_t)(crc >> (8 * i));
    crc = frb_crc32(0, image + info->index_offset, (size_t)(info->index_entries * (uint64_t)info->index_bytes));
    for (int i = 0; i < 4; i++) image[40 + i] = (uint8_t)(crc >> (8 * i));
    crc = frb_crc32(0, image, 60);
    for (int i = 0; i < 4; i++) image[60 + i] = (uint8_t)(crc >> (8 * i));
}

static FrbError open_image(const uint8_t *image, size_t size, uint32_t *bad) {
    const char *path = file("damaged.frb");
    FrbCompact *b = NULL;
    FrbError e;
    spit(path, image, size);
    e = frb_compact_open(path, &b, bad);
    CHECK((e == FRB_OK) == (b != NULL));
    if (e != FRB_OK) { /* what cannot be opened cannot be asked either, where the damage shows in 1056 bytes */
        FrbError l = frb_compact_file_lookup(path, 1, 0, NULL, NULL, NULL);
        CHECK(l == e || l == FRB_OK || l == FRB_ERR_ENTRY || l == FRB_ERR_OUTPUT);
    }
    frb_compact_close(b);
    remove(path);
    return e;
}

static void test_refusals(void) {
    const char *path = file("r.frb");
    FrbCompactInfo info;
    FrbCompact *b = NULL;
    FrbTable t, partial;
    RevGraph ring;
    uint8_t *image, *copy;
    size_t size, w;
    uint32_t bad = 0, leaf0, a = 0, c = 0;
    int refused = 0, flips = 0;

    /* Only a complete table can be compact, and nothing is written for another. */
    reference_table(&partial, 8, frb_format(1));
    frb_ring_witness(77, 8, &ring);
    CHECK(frb_table_put(&partial, 77, FRB_WITNESS, &ring) == FRB_OK);
    remove(path);
    CHECK(frb_compact_write(path, &partial, 1, 0, 0) == FRB_ERR_INCOMPLETE && !file_exists(path));
    CHECK(frb_table_put(&partial, 77, FRB_EMPTY, NULL) == FRB_OK);
    CHECK(frb_compact_write(path, &partial, 1, 0, 0) == FRB_ERR_INCOMPLETE && !file_exists(path));
    frb_table_destroy(&partial);
    reference_table(&t, 8, frb_format(1));
    CHECK(frb_compact_write(path, &t, 1, 0, 5) == FRB_ERR_ARG && !file_exists(path));
    CHECK(frb_compact_write(file("no/such/dir/x.frb"), &t, 1, 0, 0) == FRB_ERR_IO);

    /* The two kinds of file do not read as each other. */
    CHECK(frb_file_write(file("fixed.frb"), &t) == FRB_OK);
    CHECK(frb_file_is_compact(file("fixed.frb")) == 0 && frb_file_is_compact(file("nothing.frb")) == -1);
    CHECK(frb_compact_open(file("fixed.frb"), &b, NULL) == FRB_ERR_VERSION && b == NULL);
    CHECK(frb_compact_file_info(file("fixed.frb"), &info) == FRB_ERR_VERSION);
    CHECK(frb_compact_write(path, &t, 1, 0xCAFEF00Du, 0) == FRB_OK);
    CHECK(frb_file_read(path, &partial, NULL, NULL) == FRB_ERR_VERSION && partial.entries == NULL);
    CHECK(frb_file_info(path, NULL) == FRB_ERR_VERSION);
    CHECK(frb_file_lookup(path, 3, NULL, NULL, NULL) == FRB_ERR_VERSION);
    remove(file("fixed.frb"));

    CHECK(frb_compact_file_info(path, &info) == FRB_OK && info.source_table_crc == 0xCAFEF00Du);
    image = slurp(path, &size);
    CHECK(image != NULL && size == info.file_bytes);
    copy = malloc(size + 8);
    w = (size_t)info.index_bytes;

    /* One bit anywhere: every byte of the header and directory, and a
     * sample of the index and of the REVs. */
    for (size_t at = 0; at < size; at += at < 1056 ? 1 : 1 + rnd(37)) {
        FrbError e;
        memcpy(copy, image, size);
        copy[at] ^= (uint8_t)(1u << rnd(8));
        e = open_image(copy, size, NULL);
        flips++;
        refused += e != FRB_OK;
        if (at < 8) CHECK(e == FRB_ERR_MAGIC);
        else if (at < 10) CHECK(e == FRB_ERR_VERSION);
        else if (at < 1056) CHECK(e == FRB_ERR_HEADER);
        else CHECK(e == FRB_ERR_CHECKSUM || e == FRB_ERR_HEADER); /* the padding before the REVs is not summed */
    }
    CHECK(refused == flips);
    CHECK(open_image(image, size - 1, NULL) == FRB_ERR_SIZE);
    memcpy(copy, image, size);
    copy[size] = 0;
    CHECK(open_image(copy, size + 1, NULL) == FRB_ERR_SIZE);
    CHECK(open_image(image, 500, NULL) == FRB_ERR_SIZE && open_image(image, 5, NULL) == FRB_ERR_MAGIC);

    /* The same file with every checksum made right again after the change. */
    leaf0 = (uint32_t)(info.index_offset + (((uint64_t)1 << 8) - 2) * w); /* place of the leaf of string 0 */
    /* two strings of one class exchange their ordinals */
    for (a = 0; a < 256; a++) {
        for (c = a + 1; c < 256; c++)
            if (frb_table_nodes(&t, a) == frb_table_nodes(&t, c)) break;
        if (c < 256) break;
    }
    CHECK(a < 256 && c < 256);
    memcpy(copy, image, size);
    {
        uint8_t tmp[4];
        memcpy(tmp, copy + leaf0 + a * w, w);
        memcpy(copy + leaf0 + a * w, copy + leaf0 + c * w, w);
        memcpy(copy + leaf0 + c * w, tmp, w);
    }
    repair(copy, &info);
    CHECK(open_image(copy, size, &bad) == FRB_ERR_ENTRY && bad == a);
    /* two REVs of one class exchange their places */
    memcpy(copy, image, size);
    {
        int n = frb_table_nodes(&t, a);
        uint32_t oa = image[leaf0 + a * w], oc = image[leaf0 + c * w]; /* 1-byte ordinals at 8 bits */
        uint8_t *pa = copy + info.class_offset[n] + (size_t)(oa - info.class_start[n]) * info.class_stride[n];
        uint8_t *pc = copy + info.class_offset[n] + (size_t)(oc - info.class_start[n]) * info.class_stride[n];
        uint8_t tmp[64];
        CHECK(w == 1);
        memcpy(tmp, pa, info.class_stride[n]);
        memcpy(pa, pc, info.class_stride[n]);
        memcpy(pc, tmp, info.class_stride[n]);
    }
    repair(copy, &info);
    CHECK(open_image(copy, size, &bad) == FRB_ERR_OUTPUT && bad == a);
    /* a shorter string answered by the worse of its two extensions */
    {
        int done = 0;
        for (uint32_t v = 0; v < 128 && !done; v++) {
            size_t parent = (size_t)info.index_offset + (((size_t)1 << 7) - 2 + v) * w;
            uint8_t zero = image[leaf0 + 2 * v * w], one = image[leaf0 + (2 * v + 1) * w];
            if (zero == one) continue;
            memcpy(copy, image, size);
            copy[parent] = zero > one ? zero : one;
            repair(copy, &info);
            CHECK(open_image(copy, size, &bad) == FRB_ERR_ENTRY && bad == v << 1);
            done = 1;
        }
        CHECK(done);
    }
    /* an ordinal beyond the table */
    memcpy(copy, image, size);
    copy[info.index_offset] = 0xFF; /* the string "0": no such ordinal in a table of 256 unless it is 255 */
    repair(copy, &info);
    CHECK(open_image(copy, size, &bad) != FRB_OK);
    /* a REV with a bit after its last node */
    memcpy(copy, image, size);
    {
        int n = 3; /* 15 bits in 2 bytes: the last bit is spare */
        CHECK(info.class_count[3] > 0);
        copy[info.class_offset[n] + 1] |= 0x80;
    }
    repair(copy, &info);
    CHECK(open_image(copy, size, &bad) == FRB_ERR_ENTRY);
    /* one REV counted in another class: every place after it moves */
    memcpy(copy, image, size);
    copy[64 + 32 * 3]--; /* count of class 4, low byte */
    copy[64 + 32 * 4]++; /* count of class 5 */
    repair(copy, &info);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);
    /* another index width than the file has */
    memcpy(copy, image, size);
    copy[14] = 2;
    repair(copy, &info);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);
    /* a fixed-width entry size, a version that is none, a version that is fixed-width */
    memcpy(copy, image, size);
    copy[15] = 19;
    repair(copy, &info);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);
    for (int version = 0; version <= 5; version++) {
        FrbTable none;
        if (version == 4) continue;
        memcpy(copy, image, size);
        copy[8] = (uint8_t)version;
        repair(copy, &info);
        CHECK(open_image(copy, size, NULL) == FRB_ERR_VERSION);
        spit(file("v.frb"), copy, size); /* and the fixed-width reader does not take it for one of its own */
        CHECK(frb_file_read(file("v.frb"), &none, NULL, NULL) != FRB_OK && none.entries == NULL);
        remove(file("v.frb"));
    }
    /* not complete, by the flag */
    memcpy(copy, image, size);
    copy[20] = 0;
    repair(copy, &info);
    CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);
    /* bytes between the index and the REVs that are not zero */
    if (info.pool_offset > info.index_offset + info.index_entries * w) {
        memcpy(copy, image, size);
        copy[info.pool_offset - 1] = 1;
        repair(copy, &info);
        CHECK(open_image(copy, size, NULL) == FRB_ERR_HEADER);
    }
    CHECK(open_image(image, size, NULL) == FRB_OK); /* and the file itself is still fine */
    free(copy);
    free(image);
    frb_table_destroy(&t);
    remove(path);
    printf("refusals: %d single-bit changes all refused; exchanged ordinals and REVs, a wrong index entry,\n"
           "          moved class counts and foreign versions refused with their checksums made right\n",
           flips);
}

int main(int argc, char **argv) {
    if (argc > 1) snprintf(scratch, sizeof scratch, "%s", argv[1]);
    compute_reference();
    test_strides();
    test_pack();
    test_layout();
    test_conversion();
    test_all_classes();
    test_refusals();
    for (int bits = 1; bits <= REF_MAX; bits++) free(ref_graph[bits]);
    return t_report("test_compact");
}
