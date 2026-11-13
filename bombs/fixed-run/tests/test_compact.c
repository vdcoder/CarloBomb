/*
 * Tests of the compact FixedRunBomb format (version 4): the records and the
 * layout, conversion from the fixed-width formats and back, every answer
 * against a search of the table, tables whose REVs do not all come back as
 * they are, what the writer refuses, and every kind of damage the reader
 * claims to refuse.
 *
 * Usage: test_compact [prefix-for-scratch-files]
 */
#include <stdint.h>
#include <stdlib.h>

#include "compact.h"
#include "minimal_solver.h"
#include "test_util.h"
#include "topology.h"

static uint64_t rng_state = 0xA4093822299F31D0ull;

static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)(((rng_state * 0x2545F4914F6CDD1Dull) >> 33) % n);
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

/* The path of a scratch file: the same pointer for the same name, for as long as the test runs. */
static const char *file(const char *name) {
    static char names[24][64], paths[24][1200];
    static int used;
    for (int i = 0; i < used; i++)
        if (!strcmp(names[i], name)) return paths[i];
    if (used == 24) {
        fprintf(stderr, "test_compact: too many scratch files\n");
        exit(2);
    }
    snprintf(names[used], sizeof names[0], "%s", name);
    snprintf(paths[used], sizeof paths[0], "%s_%s", scratch, name);
    return paths[used++];
}

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)n + 1);
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) *size = (size_t)n;
    }
    fclose(f);
    return buf;
}

static void spit(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(data, 1, size, f) == size && fclose(f) == 0);
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* ---- tables ---------------------------------------------------------------- */

/* The REV the solver finds for a string. */
static void solve(uint32_t index, int bits, RevGraph *g) {
    char text[FRB_MAX_BITS + 1];
    frb_target_format(index, bits, text);
    CHECK(rev_minimal_solve(text, (size_t)bits, 0, NULL, g, NULL) == REV_MIN_MINIMAL);
}

/*
 * A complete table as the construction makes it: the strings that start
 * with 0 solved by the search (white tried first, the nodes numbered as the
 * machine reaches them), the others their complements.
 */
static void solved_table(FrbTable *t, int bits, const FrbFormat *format) {
    CHECK(frb_table_create_as(t, bits, format) == FRB_OK);
    for (uint32_t i = 0; i < t->count / 2; i++) {
        RevGraph g, c;
        solve(i, bits, &g);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(t, i ^ (t->count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
}

/* Every string its ring of L nodes, entered as if that were a minimum: REVs of many nodes. */
static void ring_table(FrbTable *t, int bits, const FrbFormat *format) {
    CHECK(frb_table_create_as(t, bits, format) == FRB_OK);
    for (uint32_t i = 0; i < t->count / 2; i++) {
        RevGraph g, c;
        frb_ring_witness(i, bits, &g);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(t, i ^ (t->count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
}

/* The same REV with two of its nodes exchanged: it emits the same string. */
static void relabel(const RevGraph *in, int a, int b, RevGraph *out) {
    *out = *in;
    for (int i = 0; i < in->node_count; i++) {
        int to = i == a ? b : i == b ? a : i;
        out->nodes[to].color = in->nodes[i].color;
        for (int k = 0; k < 2; k++) {
            int link = in->nodes[i].links[k];
            out->nodes[to].links[k] = link == a ? b : link == b ? a : link;
        }
    }
}

/* Fewest nodes among the L-bit extensions of a string, and the lowest or the
 * highest of those extensions that need them: by looking at all of them. */
static void scan(const FrbTable *t, int bits, uint32_t value, int highest, int *nodes, uint32_t *index) {
    int shift = t->bits - bits, best = 99;
    uint32_t at = 0;
    for (uint32_t i = value << shift;; i++) {
        int n = frb_table_nodes(t, i);
        if (n < best || (highest && n == best)) best = n, at = i;
        if (i == (((value + 1u) << shift) - 1u)) break;
    }
    *nodes = best;
    if (index) *index = at;
}

/* ---- records and layout ------------------------------------------------------ */

static void test_records(void) {
    static const int want[14] = {0, 1, 6, 15, 20, 35, 23, 28, 34, 40, 46, 52, 58, 0};
    for (int n = 0; n <= 13; n++) CHECK_MSG(frb_compact_record_bits(n) == want[n], "%d nodes: %d", n, frb_compact_record_bits(n));
    CHECK(frb_compact_record_bits(-1) == 0 && frb_compact_record_bits(31) == 0);
    printf("records: 1, 6, 15, 20 and 35 bits for colors and links of 1 to 5 nodes; 23 to 58 bits for the rank of\n"
           "         a graph of 6 to 12 nodes\n");
}

static void test_layout(void) {
    FrbCompactInfo info;
    /* 24 bits: the counts of the real Bomb's stored half */
    static const uint32_t real[10] = {0, 1, 3, 32, 484, 9041, 209251, 3905704, 4263149, 943};
    memset(&info, 0, sizeof info);
    for (int n = 1; n <= 9; n++) info.class_count[n] = real[n];
    CHECK(frb_compact_layout(&info, 24) == FRB_OK);
    CHECK(info.bits == 24 && info.count == (1u << 24) && info.stored == (1u << 23) && info.max_nodes == 9);
    CHECK(info.block_shift == 8 && info.block_rows == (1u << 15));
    CHECK(info.nav_offset == 448 && info.nav_bytes == (1u << 20));
    CHECK(info.nodes_offset == 448 + (1u << 20) && info.nodes_bytes == (1u << 22));
    CHECK(info.blocks_offset == info.nodes_offset + info.nodes_bytes && info.blocks_bytes == (1u << 15) * 9u * 4u);
    CHECK(info.class_offset[1] == info.blocks_offset + info.blocks_bytes);
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) {
        uint64_t bits = (uint64_t)info.class_count[n] * (uint64_t)frb_compact_record_bits(n);
        CHECK(info.class_bits[n] == (uint32_t)frb_compact_record_bits(n));
        CHECK(info.class_bytes[n] == ((bits + 63u) / 64u) * 8u);
        if (n > 1) CHECK(info.class_offset[n] == info.class_offset[n - 1] + info.class_bytes[n - 1]);
    }
    CHECK(info.file_bytes == info.class_offset[12] + info.class_bytes[12] + 8);
    CHECK(info.file_bytes == 38858512u); /* the real file */

    /* the counts must be those of half the strings */
    info.class_count[7]++;
    CHECK(frb_compact_layout(&info, 24) == FRB_ERR_ARG);
    info.class_count[7]--;
    CHECK(frb_compact_layout(&info, 25) == FRB_ERR_ARG && frb_compact_layout(&info, 23) == FRB_ERR_ARG);
    CHECK(frb_compact_layout(&info, 0) == FRB_ERR_ARG && frb_compact_layout(&info, 32) == FRB_ERR_ARG);
    CHECK(frb_compact_layout(NULL, 24) == FRB_ERR_ARG);

    /* the smallest: one stored string */
    memset(&info, 0, sizeof info);
    info.class_count[1] = 1;
    CHECK(frb_compact_layout(&info, 1) == FRB_OK);
    CHECK(info.stored == 1 && info.block_rows == 1 && info.nav_bytes == 8 && info.nodes_bytes == 8 &&
          info.blocks_bytes == 8 && info.class_bytes[1] == 8 && info.file_bytes == 448 + 8 + 8 + 8 + 8 + 8);
    /* the longest: 2^30 stored strings, say all of 11 nodes */
    memset(&info, 0, sizeof info);
    info.class_count[11] = (uint32_t)1 << 30;
    CHECK(frb_compact_layout(&info, 31) == FRB_OK);
    CHECK(info.nav_bytes == ((uint64_t)1 << 27) && info.nodes_bytes == ((uint64_t)1 << 29) &&
          info.blocks_bytes == ((uint64_t)1 << 22) * 44u && info.class_bytes[11] == ((uint64_t)1 << 30) * 52u / 8u);
    printf("layout: every offset and length from the class counts; 38,858,512 bytes for the 24-bit Bomb\n");
}

/* ---- the worked example of FORMAT.md, section 12.12 ------------------------ */

static void test_worked_example(void) {
    static const uint8_t header[64] = {
        0x43, 0x42, 0x46, 0x52, 0x42, 0x0D, 0x0A, 0x1A, 0x04, 0x00, 0x40, 0x00, 0x05, 0x0C, 0x08, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x04, 0x04, 0x01, 0x00, 0x49, 0x86, 0x8D, 0x17, 0x8A, 0x5C, 0x17, 0xBA, 0xA9, 0x6D, 0x37, 0xC9,
        0x18, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC9, 0x93, 0x07, 0x1A};
    static const uint8_t rows[5][32] = {
        {0x01, 0, 0, 0, 0x01, 0, 0, 0, 0xE0, 0x01, 0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0, 0, 0, 0, 0, 0x69, 0xDF, 0x22, 0x65, 0, 0, 0, 0},
        {0x03, 0, 0, 0, 0x06, 0, 0, 0, 0xE8, 0x01, 0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0, 0, 0, 0, 0, 0xA2, 0x06, 0xEC, 0xF0, 0, 0, 0, 0},
        {0x0A, 0, 0, 0, 0x0F, 0, 0, 0, 0xF0, 0x01, 0, 0, 0, 0, 0, 0, 0x18, 0, 0, 0, 0, 0, 0, 0, 0x92, 0x4C, 0x6B, 0x43, 0, 0, 0, 0},
        {0x02, 0, 0, 0, 0x14, 0, 0, 0, 0x08, 0x02, 0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0, 0, 0, 0, 0, 0xF6, 0x14, 0x1A, 0x58, 0, 0, 0, 0},
        {0x00, 0, 0, 0, 0x23, 0, 0, 0, 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0x00, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0}};
    static const uint8_t rest[88] = {
        0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x43, 0x33, 0x33, 0x23, 0x32, 0x23, 0x33,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6D, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xB1, 0x84, 0x94, 0x40, 0x4A, 0xB0, 0x46, 0x11, 0x42, 0x60, 0x08, 0x30, 0x0D, 0x99, 0x62, 0x25,
        0x14, 0x86, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x0D, 0x12, 0x84, 0x0C, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    /* the 16 REVs of the table in the specification: nodes, then color, link on 0, link on 1 of each */
    static const int8_t revs[16][13] = {
        {1, 0, 0, 0},                         {4, 1, 2, 1, 0, 1, 3, 0, 0, 0, 0, 2, 0}, {3, 1, 2, 1, 0, 1, 2, 0, 0, 0},
        {4, 1, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0, 0}, {3, 1, 1, 1, 0, 2, 0, 0, 0, 0},       {3, 1, 1, 1, 0, 2, 0, 0, 0, 2},
        {3, 1, 2, 1, 0, 0, 1, 1, 1, 0},       {3, 1, 0, 1, 0, 0, 2, 0, 0, 0},          {3, 0, 1, 0, 0, 2, 0, 1, 0, 0},
        {2, 1, 1, 1, 0, 0, 1},                {2, 1, 1, 1, 0, 0, 0},                   {3, 0, 1, 2, 0, 2, 1, 1, 0, 2},
        {3, 0, 1, 2, 0, 2, 0, 1, 2, 1},       {2, 1, 0, 1, 0, 0, 0},                   {3, 1, 0, 1, 0, 0, 2, 1, 2, 0},
        {3, 0, 1, 0, 0, 2, 0, 1, 2, 0}};
    const char *path = file("e.frb");
    FrbTable t;
    FrbFileInfo finfo;
    FrbCompact *c = NULL;
    RevGraph g;
    uint8_t *image;
    size_t size;
    int nodes[2];

    /* the table as the construction builds it is the table printed there */
    solved_table(&t, 5, frb_format(1));
    for (uint32_t i = 0; i < 16; i++) {
        FrbStatus status;
        CHECK(frb_table_get(&t, i, &status, &g) == FRB_OK && g.node_count == revs[i][0]);
        for (int k = 0; k < g.node_count; k++)
            CHECK(g.nodes[k].color == revs[i][1 + 3 * k] && g.nodes[k].links[0] == revs[i][2 + 3 * k] &&
                  g.nodes[k].links[1] == revs[i][3 + 3 * k]);
    }
    CHECK(frb_file_write(file("f.frb"), &t) == FRB_OK && frb_file_info(file("f.frb"), &finfo) == FRB_OK);
    CHECK(finfo.table_crc == 0xC9376DA9u && finfo.version == 1);
    remove(path);
    CHECK(frb_compact_write(path, &t, finfo.version, finfo.table_crc) == FRB_OK);
    image = slurp(path, &size);
    CHECK(image && size == 536);
    if (image && size == 536) {
        CHECK(memcmp(image, header, 64) == 0);
        for (int r = 0; r < 5; r++) CHECK(memcmp(image + 64 + 32 * r, rows[r], 32) == 0);
        CHECK(memcmp(image + 448, rest, 88) == 0);
    }
    free(image);
    /* and the lookups it walks through */
    CHECK(frb_compact_open(path, &c, NULL) == FRB_OK);
    if (c) {
        CHECK(frb_compact_nodes(c, 5, 13) == 2 && frb_compact_graph(c, 5, 13, &g) == FRB_OK);
        CHECK(g.node_count == 2 && g.nodes[0].color == 1 && g.nodes[0].links[0] == 0 && g.nodes[0].links[1] == 1 &&
              g.nodes[1].color == 0 && g.nodes[1].links[0] == 0 && g.nodes[1].links[1] == 0);
        CHECK(frb_compact_extension(c, 3, 3) == 13 && frb_compact_nodes(c, 3, 3) == 2);     /* 011 -> 01101 */
        CHECK(frb_compact_extension(c, 2, 1) == 9 && frb_compact_extension(c, 2, 2) == 22); /* 01 -> 01001, 10 -> 10110 */
        CHECK(frb_compact_graph(c, 2, 2, &g) == FRB_OK && g.node_count == 2 && g.nodes[0].color == 0 &&
              g.nodes[0].links[0] == 1 && g.nodes[0].links[1] == 1 && g.nodes[1].color == 1 &&
              g.nodes[1].links[0] == 1 && g.nodes[1].links[1] == 0);
        CHECK(frb_compact_compare(c, 2, 1, nodes) == FRB_COMPACT_TIE && nodes[0] == 2 && nodes[1] == 2); /* 010 and 011 */
        CHECK(frb_compact_compare(c, 4, 4, nodes) == 1 && nodes[0] == 3 && nodes[1] == 2);               /* 01000 and 01001 */
        frb_compact_close(c);
    }
    remove(path);
    remove(file("f.frb"));
    frb_table_destroy(&t);

    /* the rank: the REV of 00000000001, its canonical numbering, and what the search makes of it */
    {
        static const uint8_t list[12] = {1, 2, 3, 3, 2, 4, 0, 5, 1, 0, 4, 0};
        static const int8_t table[6][3] = {{1, 2, 1}, {0, 1, 4}, {0, 3, 3}, {0, 0, 5}, {0, 2, 0}, {0, 4, 0}};
        static const uint8_t reached[6] = {0, 2, 1, 3, 4, 5};
        FrbTopology graph, canonical, back, again;
        uint8_t order[FRB_TOPOLOGY_MAX_NODES];
        uint32_t colors = 0, found = 0, back_colors = 0;
        solve(1, 11, &g);
        CHECK(g.node_count == 6);
        for (int k = 0; k < 6; k++)
            CHECK(g.nodes[k].color == table[k][0] && g.nodes[k].links[0] == table[k][1] && g.nodes[k].links[1] == table[k][2]);
        CHECK(frb_topology_from_graph(&g, &graph, &colors) && frb_topology_canonical(&graph, &canonical, NULL) == 6);
        for (int p = 0; p < 12; p++) CHECK(canonical.link[p & 1][p >> 1] == list[p]);
        CHECK(frb_topology_rank(&canonical) == 4581456u && frb_topology_count(6) == 5931540u);
        CHECK(frb_topology_unrank(6, 4581456u, &again) && frb_topology_find_colors(&again, 1, 11, &found, order) == 6);
        CHECK(found == 1u);
        for (int k = 0; k < 6; k++) CHECK(order[k] == reached[k]);
        frb_topology_renumber(&again, found, order, &back, &back_colors);
        CHECK(back_colors == colors && memcmp(&back, &graph, sizeof back) == 0);
    }
    /* the search on the graph of 01001, which has to take back its first color */
    {
        FrbTopology small = {2, {{1, 0}, {1, 1}}};
        uint8_t order[FRB_TOPOLOGY_MAX_NODES];
        uint32_t found = 0;
        CHECK(frb_topology_find_colors(&small, 9, 5, &found, order) == 2 && found == 1u && order[0] == 0 && order[1] == 1);
    }
    printf("example: the 536 bytes of the worked example in the specification, byte for byte, its lookups,\n"
           "         the rank 4,581,456 and the search\n");
}

/* ---- conversion ---------------------------------------------------------- */

/*
 * A table into a compact file and everything the file can be asked, against
 * the table.  others: how many strings get another REV than the table's.
 */
static void check_conversion(const FrbTable *t, uint32_t others, const char *what) {
    const char *path = file("c.frb"), *expanded = file("x.frb"), *fixed = file("f.frb");
    const int L = t->bits;
    const uint32_t mask = t->count - 1u;
    FrbCompact *c = NULL;
    FrbCompactInfo info, again;
    FrbCompactDifference d;
    FrbFileInfo finfo;
    FrbTable back;
    uint8_t *a, *b;
    size_t size_a, size_b;
    uint32_t bad = 0, crc = 0, classes[FRB_COMPACT_MAX_NODES + 1] = {0}, differing = 0, other_extension = 0;
    int max = 0;

    remove(path);
    CHECK(frb_file_write(fixed, t) == FRB_OK && frb_file_info(fixed, &finfo) == FRB_OK);
    CHECK_MSG(frb_compact_write(path, t, finfo.version, finfo.table_crc) == FRB_OK, "%s", what);
    CHECK(file_exists(path) && !file_exists(file("c.frb.tmp")));
    CHECK(frb_file_is_compact(path) == 1 && frb_file_is_compact(fixed) == 0 && !frb_compact_file_is_draft(path));

    /* the header and the directory */
    for (uint32_t i = 0; i < t->count / 2; i++) {
        int n = frb_table_nodes(t, i);
        classes[n]++;
        if (n > max) max = n;
    }
    CHECK(frb_compact_file_info(path, &info) == FRB_OK);
    CHECK(info.bits == L && info.count == t->count && info.stored == t->count / 2 && info.max_nodes == max);
    CHECK(info.source_version == finfo.version && info.source_table_crc == finfo.table_crc);
    CHECK_MSG(info.other_revs == others, "%s: %u other REVs in the header", what, (unsigned)info.other_revs);
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) CHECK(info.class_count[n] == classes[n]);
    again = info;
    CHECK(frb_compact_layout(&again, L) == FRB_OK && again.file_bytes == info.file_bytes);
    a = slurp(path, &size_a);
    CHECK(a && size_a == info.file_bytes);
    free(a);

    CHECK(frb_compact_open(path, &c, &bad) == FRB_OK);
    if (!c) return;
    CHECK(frb_compact_info(c)->file_bytes == info.file_bytes && frb_compact_info(c)->other_revs == others);

    /* every string of L bits */
    for (uint32_t i = 0;; i++) {
        RevGraph mine, theirs, twin, mirrored;
        FrbStatus status;
        CHECK(frb_table_get(t, i, &status, &theirs) == FRB_OK);
        if (frb_compact_nodes(c, L, i) != theirs.node_count) CHECK(frb_compact_nodes(c, L, i) == theirs.node_count);
        CHECK(frb_compact_extension(c, L, i) == i);
        CHECK(frb_compact_graph(c, L, i, &mine) == FRB_OK && mine.node_count == theirs.node_count);
        CHECK(rev_graph_validate(&mine) == REV_OK && frb_graph_output(&mine, L) == i);
        differing += !same_graph(&mine, &theirs);
        /* complement in, complement out */
        CHECK(frb_compact_graph(c, L, i ^ mask, &twin) == FRB_OK);
        frb_graph_complement(&mine, &mirrored);
        CHECK(same_graph(&mirrored, &twin));
        if (i == mask) break;
    }
    CHECK_MSG(differing == others, "%s: %u strings with another REV", what, (unsigned)differing);

    /* every shorter string, against a search of the table */
    for (int bits = 1; bits < L; bits++) {
        for (uint32_t v = 0; v < ((uint32_t)1 << bits); v++) {
            uint32_t lowest = 0, want = 0, answer = frb_compact_extension(c, bits, v), other = v ^ ((1u << bits) - 1u);
            RevGraph mine, leaf;
            int least = 0, unused = 0;
            scan(t, bits, v, 0, &least, &lowest);
            scan(t, bits, v, (int)((v >> (bits - 1)) & 1u), &unused, &want); /* the highest for a string that starts with 1 */
            CHECK(frb_compact_nodes(c, bits, v) == least);
            CHECK(answer == want && (answer >> (L - bits)) == v && frb_table_nodes(t, answer) == least);
            CHECK(frb_compact_extension(c, bits, other) == (answer ^ mask));
            CHECK(frb_compact_graph(c, bits, v, &mine) == FRB_OK && frb_compact_graph(c, L, answer, &leaf) == FRB_OK);
            CHECK(same_graph(&mine, &leaf) && (frb_graph_output(&mine, L) >> (L - bits)) == v);
            other_extension += answer != lowest;
        }
    }
    /* which of the two extensions of a string needs fewer nodes */
    for (int bits = 0; bits < L; bits++) {
        for (uint32_t v = 0; v < ((uint32_t)1 << bits); v++) {
            int n0 = 0, n1 = 0, nodes[2] = {-1, -1}, which;
            scan(t, bits + 1, 2 * v, 0, &n0, NULL);
            scan(t, bits + 1, 2 * v + 1, 0, &n1, NULL);
            which = frb_compact_compare(c, bits, v, nodes);
            CHECK(which == (n0 < n1 ? 0 : n1 < n0 ? 1 : FRB_COMPACT_TIE) && nodes[0] == n0 && nodes[1] == n1);
            CHECK(frb_compact_compare(c, bits, v, NULL) == which);
        }
    }
    CHECK(frb_compact_compare(c, L, 0, NULL) == -1 && frb_compact_compare(c, -1, 0, NULL) == -1);
    CHECK(frb_compact_compare(c, 0, 1, NULL) == -1 && frb_compact_compare(NULL, 0, 0, NULL) == -1);
    if (L > 1) CHECK(frb_compact_compare(c, L - 1, t->count / 2, NULL) == -1);

    /* the comparison with the table as the library makes it */
    CHECK(frb_compact_against_table(c, t, &d, &bad) == FRB_OK);
    CHECK(d.other_revs == others && d.other_extension == other_extension && d.other_next_bit <= d.other_revs);
    CHECK(frb_compact_equals_table(c, t, &bad) == FRB_OK);

    /* back to a fixed-width table, in memory and as a file */
    CHECK(frb_compact_to_table(c, t->format, &back) == FRB_OK);
    if (back.entries) {
        uint32_t entries = 0;
        for (uint32_t i = 0;; i++) {
            entries += memcmp(frb_table_entry(&back, i), frb_table_entry(t, i), (size_t)t->format->entry_bytes) != 0;
            if (i == mask) break;
        }
        CHECK(entries == others);
        CHECK(frb_compact_equals_table(c, &back, &bad) == (others ? FRB_ERR_EVIDENCE : FRB_OK));
        CHECK(frb_compact_against_table(c, &back, &d, &bad) == FRB_OK && d.other_revs == 0);
        frb_table_destroy(&back);
    }
    CHECK(frb_compact_table_crc(c, t->format, &crc) == FRB_OK);
    CHECK((crc == finfo.table_crc) == (others == 0));
    remove(expanded);
    crc = 0;
    CHECK(frb_compact_expand(c, NULL, expanded, &crc) == FRB_OK && !file_exists(file("x.frb.tmp")));
    a = slurp(expanded, &size_a);
    b = slurp(fixed, &size_b);
    CHECK(a && b && size_a == size_b);
    if (a && b && size_a == size_b) CHECK((memcmp(a, b, size_a) == 0) == (others == 0)); /* the source, byte for byte */
    CHECK((crc == finfo.table_crc) == (others == 0));
    free(a);
    free(b);
    {   /* and it is a complete Bomb that reads as any other */
        FrbTable read;
        FrbFileInfo rinfo;
        CHECK(frb_file_read(expanded, &read, &rinfo, &bad) == FRB_OK && rinfo.complete && rinfo.table_crc == crc);
        if (read.entries) frb_table_destroy(&read);
    }

    /* single questions put to the file */
    for (int k = 0; k < 40; k++) {
        int bits = 1 + (int)rnd((uint32_t)L);
        uint32_t v = rnd((uint32_t)1 << bits), extension = 0;
        RevGraph one, same;
        FrbCompactInfo linfo;
        CHECK(frb_compact_file_lookup(path, bits, v, &linfo, &one, &extension) == FRB_OK);
        CHECK(linfo.file_bytes == info.file_bytes && extension == frb_compact_extension(c, bits, v));
        CHECK(frb_compact_graph(c, bits, v, &same) == FRB_OK && same_graph(&one, &same));
    }
    {
        RevGraph g;
        CHECK(frb_compact_file_lookup(path, 0, 0, NULL, &g, NULL) == FRB_ERR_ARG);
        CHECK(frb_compact_file_lookup(path, L + 1, 0, NULL, &g, NULL) == FRB_ERR_ARG);
        CHECK(frb_compact_file_lookup(path, L, t->count, NULL, &g, NULL) == FRB_ERR_ARG || L == 31);
        CHECK(frb_compact_file_lookup(path, 1, 0, NULL, NULL, NULL) == FRB_OK);
        CHECK(frb_compact_graph(c, 0, 0, &g) == FRB_ERR_ARG && frb_compact_graph(c, L + 1, 0, &g) == FRB_ERR_ARG);
        CHECK(frb_compact_graph(c, 1, 2, &g) == FRB_ERR_ARG && frb_compact_graph(NULL, 1, 0, &g) == FRB_ERR_ARG);
        CHECK(frb_compact_graph(c, 1, 0, NULL) == FRB_ERR_ARG);
    }
    frb_compact_close(c);
    remove(path);
    remove(expanded);
    remove(fixed);
}

static void test_conversion(void) {
    unsigned long long strings = 0;
    int tables = 0;
    for (int bits = 1; bits <= 11; bits++) {
        for (int version = 1; version <= 3; version++) {
            FrbTable t;
            char what[64];
            if (bits > 9 && version != 1 + bits % 3) continue; /* the longer ones in one format each */
            snprintf(what, sizeof what, "%d bits, version %d", bits, version);
            solved_table(&t, bits, frb_format(version));
            check_conversion(&t, 0, what);
            strings += t.count;
            tables++;
            frb_table_destroy(&t);
        }
    }
    printf("conversion: %d tables of 1 to 11 bits as the construction makes them, in the three fixed-width formats;\n"
           "            %llu strings and every shorter string: node counts, REVs, extensions, comparisons, and the\n"
           "            fixed-width file back byte for byte\n",
           tables, strings);
}

/* REVs of 6 to 12 nodes: the rank of every size of graph, and the widest records. */
static void test_rings(void) {
    for (int bits = 6; bits <= 12; bits++) {
        FrbTable t;
        FrbCompactInfo info;
        char what[64];
        snprintf(what, sizeof what, "rings of %d nodes", bits);
        ring_table(&t, bits, frb_format(1 + bits % 3));
        check_conversion(&t, 0, what);
        /* every stored string in the class of its node count, and nothing else in the file */
        CHECK(frb_compact_write(file("r.frb"), &t, 0, 0) == FRB_OK && frb_compact_file_info(file("r.frb"), &info) == FRB_OK);
        CHECK(info.max_nodes == bits && info.class_count[bits] == t.count / 2 && info.source_version == 0);
        CHECK(info.class_bits[bits] == (uint32_t)frb_topology_rank_bits(bits));
        remove(file("r.frb"));
        frb_table_destroy(&t);
    }
    printf("rings: tables of REVs of 6 to 12 nodes, every one back from its rank\n");
}

/* Many node counts side by side, the counts of the blocks changing in the middle of them. */
static void test_mixed(void) {
    const int bits = 12;
    FrbTable t;
    uint32_t rings = 0;
    CHECK(frb_table_create_as(&t, bits, frb_format(2)) == FRB_OK);
    for (uint32_t i = 0; i < t.count / 2; i++) {
        RevGraph g, c;
        if (rnd(3) == 0) frb_ring_witness(i, bits, &g), rings++;
        else solve(i, bits, &g);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(&t, i ^ (t.count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
    CHECK(rings > 400);
    check_conversion(&t, 0, "12 bits, minima and rings mixed");
    frb_table_destroy(&t);
    printf("mixed: a 12-bit table of REVs of 1 to 7 and of 12 nodes in any order\n");
}

/* ---- REVs that do not come back as they are --------------------------------- */

static void test_other_revs(void) {
    const int bits = 11;
    FrbTable t, changed;
    FrbStatus status;
    RevGraph g, other, c;
    uint32_t at = 0, mask;
    int found = 0;
    solved_table(&t, bits, frb_format(1));
    mask = t.count - 1u;

    /* A REV of 6 nodes or more in another numbering than the search gives it: the same graph, and
     * it comes back in the numbering of the search.  One string, then the pair. */
    for (uint32_t i = 0; i < t.count / 2 && !found; i++) {
        CHECK(frb_table_get(&t, i, &status, &g) == FRB_OK);
        if (g.node_count < 6) continue;
        relabel(&g, 1, 2, &other);
        if (same_graph(&g, &other) || frb_graph_output(&other, bits) != i) continue;
        at = i;
        found = 1;
    }
    CHECK(found);
    solved_table(&changed, bits, frb_format(1));
    CHECK(frb_table_put(&changed, at, FRB_MINIMAL, &other) == FRB_OK);
    check_conversion(&changed, 1, "one REV in another numbering");
    frb_graph_complement(&other, &c);
    CHECK(frb_table_put(&changed, at ^ mask, FRB_MINIMAL, &c) == FRB_OK);
    check_conversion(&changed, 2, "a pair in another numbering");
    /* only the one that starts with 1: the stored one comes back, its complement is not the table's */
    CHECK(frb_table_put(&changed, at, FRB_MINIMAL, &g) == FRB_OK);
    check_conversion(&changed, 1, "the complement in another numbering");
    frb_table_destroy(&changed);

    /* A REV of up to 5 nodes is stored as it is, whatever its numbering: nothing differs. */
    found = 0;
    for (uint32_t i = 0; i < t.count / 2 && !found; i++) {
        CHECK(frb_table_get(&t, i, &status, &g) == FRB_OK);
        if (g.node_count < 3 || g.node_count > 5) continue;
        relabel(&g, 1, 2, &other);
        if (same_graph(&g, &other) || frb_graph_output(&other, bits) != i) continue;
        at = i;
        found = 1;
    }
    CHECK(found);
    solved_table(&changed, bits, frb_format(1));
    frb_graph_complement(&other, &c);
    CHECK(frb_table_put(&changed, at, FRB_MINIMAL, &other) == FRB_OK);
    CHECK(frb_table_put(&changed, at ^ mask, FRB_MINIMAL, &c) == FRB_OK);
    check_conversion(&changed, 0, "a small REV in another numbering");
    frb_table_destroy(&changed);

    /* A table built without the symmetry: every string solved for itself.  The REV of a string
     * that starts with 1 is then seldom the complement of its complement's. */
    {
        uint32_t others = 0;
        CHECK(frb_table_create_as(&changed, bits, frb_format(1)) == FRB_OK);
        for (uint32_t i = 0; i < changed.count; i++) {
            RevGraph own, twin, mirrored;
            solve(i, bits, &own);
            CHECK(frb_table_put(&changed, i, FRB_MINIMAL, &own) == FRB_OK);
            if (i >= changed.count / 2) {
                CHECK(frb_table_get(&t, i ^ mask, &status, &twin) == FRB_OK);
                frb_graph_complement(&twin, &mirrored);
                others += !same_graph(&mirrored, &own);
            }
        }
        CHECK(others > 0);
        check_conversion(&changed, others, "every string solved for itself");
        printf("other REVs: a REV in another numbering, alone and with its complement; a table built without the\n"
               "            symmetry, in which %u of %u strings get the complement of the other's REV instead of\n"
               "            their own: counted in the header, every answer still a REV of the fewest nodes\n",
               (unsigned)others, (unsigned)changed.count);
        frb_table_destroy(&changed);
    }
    frb_table_destroy(&t);
}

/* ---- what the writer refuses ------------------------------------------------- */

static void test_write_refusals(void) {
    const char *path = file("w.frb"), *tmp = file("w.frb.tmp");
    FrbTable t, bad;
    FrbStatus status;
    RevGraph g, ring, c, padded;
    uint32_t mask;
    remove(path);
    solved_table(&t, 8, frb_format(1));
    mask = t.count - 1u;
    CHECK(frb_compact_write(NULL, &t, 1, 0) == FRB_ERR_ARG && frb_compact_write(path, NULL, 1, 0) == FRB_ERR_ARG);

    /* not complete: an entry that is a witness and no minimum, an empty entry */
    solved_table(&bad, 8, frb_format(1));
    frb_ring_witness(77, 8, &ring);
    CHECK(frb_table_put(&bad, 77, FRB_WITNESS, &ring) == FRB_OK);
    CHECK(frb_compact_write(path, &bad, 1, 0) == FRB_ERR_INCOMPLETE && !file_exists(path) && !file_exists(tmp));
    frb_table_destroy(&bad);
    CHECK(frb_table_create_as(&bad, 8, frb_format(1)) == FRB_OK);
    CHECK(frb_compact_write(path, &bad, 1, 0) == FRB_ERR_INCOMPLETE && !file_exists(path));
    frb_table_destroy(&bad);

    /* a string and its complement with different node counts: no table of minima */
    solved_table(&bad, 8, frb_format(1));
    CHECK(frb_table_get(&bad, 77, &status, &g) == FRB_OK && g.node_count < 8);
    CHECK(frb_table_put(&bad, 77, FRB_MINIMAL, &ring) == FRB_OK);
    CHECK(frb_compact_write(path, &bad, 1, 0) == FRB_ERR_EVIDENCE && !file_exists(path) && !file_exists(tmp));
    frb_graph_complement(&ring, &c);
    CHECK(frb_table_put(&bad, 77 ^ mask, FRB_MINIMAL, &c) == FRB_OK); /* the pair of them: 8 nodes each, and fine */
    CHECK(frb_compact_write(path, &bad, 1, 0) == FRB_OK);
    remove(path);
    frb_table_destroy(&bad);

    /* a REV with a node its string does not need: a ring of 7 with an eighth node nothing leads to */
    {
        FrbTable seven;
        ring_table(&seven, 7, frb_format(1));
        CHECK(frb_table_get(&seven, 5, &status, &g) == FRB_OK && g.node_count == 7);
        padded = g;
        padded.node_count = 8;
        padded.nodes[7].color = 0;
        padded.nodes[7].links[0] = padded.nodes[7].links[1] = 0;
        frb_graph_complement(&padded, &c);
        CHECK(frb_table_put(&seven, 5, FRB_MINIMAL, &padded) == FRB_OK);
        CHECK(frb_table_put(&seven, 5 ^ (seven.count - 1u), FRB_MINIMAL, &c) == FRB_OK);
        CHECK(frb_compact_write(path, &seven, 1, 0) == FRB_ERR_EVIDENCE && !file_exists(path) && !file_exists(tmp));
        frb_table_destroy(&seven);
    }

    /* more than 12 nodes */
    {
        FrbTable big;
        ring_table(&big, 13, frb_format(1));
        CHECK(frb_compact_write(path, &big, 1, 0) == FRB_ERR_CAPACITY && !file_exists(path) && !file_exists(tmp));
        frb_table_destroy(&big);
    }

    /* an existing file is replaced by a complete new one, and only by that */
    CHECK(frb_compact_write(path, &t, 1, 0x1234) == FRB_OK);
    {
        FrbCompactInfo info;
        solved_table(&bad, 9, frb_format(1));
        CHECK(frb_compact_write(path, &bad, 1, 0x5678) == FRB_OK);
        CHECK(frb_compact_file_info(path, &info) == FRB_OK && info.bits == 9 && info.source_table_crc == 0x5678);
        frb_table_destroy(&bad);
        CHECK(frb_table_create_as(&bad, 9, frb_format(1)) == FRB_OK);
        CHECK(frb_compact_write(path, &bad, 1, 0) == FRB_ERR_INCOMPLETE);
        CHECK(frb_compact_file_info(path, &info) == FRB_OK && info.bits == 9 && info.source_table_crc == 0x5678);
        frb_table_destroy(&bad);
    }
    remove(path);
    frb_table_destroy(&t);
    printf("writer: refuses a table that is not complete, a pair with different node counts, a REV with a node\n"
           "        its string does not need, a REV of 13 nodes; leaves nothing behind\n");
}

/* ---- what the reader refuses ------------------------------------------------- */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* The checksums made right again after a change, so that what was changed is what the reader meets:
 * of the index, of every class, of the directory, of the header. */
static void reseal(uint8_t *image, const FrbCompactInfo *info) {
    wr32(image + 40, frb_crc32(0, image + info->nav_offset, (size_t)(info->class_offset[1] - info->nav_offset)));
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++)
        wr32(image + FRB_HEADER_BYTES + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES + 24,
             frb_crc32(0, image + info->class_offset[n], (size_t)info->class_bytes[n]));
    wr32(image + 36, frb_crc32(0, image + FRB_HEADER_BYTES, FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES));
    wr32(image + 60, frb_crc32(0, image, 60));
}

static FrbError open_image(const uint8_t *image, size_t size, uint32_t *bad) {
    const char *path = file("d.frb");
    FrbCompact *c = NULL;
    FrbError e;
    spit(path, image, size);
    e = frb_compact_open(path, &c, bad);
    CHECK((e == FRB_OK) == (c != NULL));
    frb_compact_close(c);
    return e;
}

static FrbError lookup_image(const uint8_t *image, size_t size, int bits, uint32_t value) {
    const char *path = file("d.frb");
    RevGraph g;
    spit(path, image, size);
    return frb_compact_file_lookup(path, bits, value, NULL, &g, NULL);
}

static void test_refusals(void) {
    const char *path = file("g.frb");
    const int L = 11;
    FrbTable t;
    FrbCompactInfo info;
    FrbCompact *c = NULL;
    uint8_t *good, *image;
    size_t size;
    uint32_t bad = 0;
    int refused = 0, flips = 0;

    solved_table(&t, L, frb_format(1));
    remove(path);
    CHECK(frb_compact_write(path, &t, 1, 0xCAFE) == FRB_OK && frb_compact_file_info(path, &info) == FRB_OK);
    good = slurp(path, &size);
    image = malloc(size + 16);
    CHECK(good && image && size == info.file_bytes);
    if (!good || !image) return;
    CHECK(info.max_nodes >= 6 && info.class_count[6] > 0); /* a class of ranks, and several blocks */
    CHECK(open_image(good, size, &bad) == FRB_OK);

    /* 1. One bit changed anywhere: every byte of the file is under a checksum or has to be zero. */
    for (size_t at = 0; at < size; at += at < 600 ? 1 : 1 + rnd(13)) {
        FrbError e;
        memcpy(image, good, size);
        image[at] ^= (uint8_t)(1u << rnd(8));
        e = open_image(image, size, &bad);
        CHECK_MSG(e != FRB_OK, "a bit of byte %zu changed and the file opens", at);
        refused += e != FRB_OK;
        flips++;
    }
    CHECK(refused == flips);

    /* 2. The length: cut short, grown, nothing but a header. */
    CHECK(open_image(good, size - 1, &bad) == FRB_ERR_SIZE && open_image(good, size - 8, &bad) == FRB_ERR_SIZE);
    memcpy(image, good, size);
    memset(image + size, 0, 16);
    CHECK(open_image(image, size + 8, &bad) == FRB_ERR_SIZE);
    CHECK(open_image(good, FRB_COMPACT_INDEX_OFFSET, &bad) == FRB_ERR_SIZE);
    CHECK(open_image(good, FRB_COMPACT_INDEX_OFFSET - 1, &bad) == FRB_ERR_SIZE);
    CHECK(open_image(good, 9, &bad) == FRB_ERR_SIZE && open_image(good, 4, &bad) == FRB_ERR_MAGIC);
    CHECK(open_image(good, 0, &bad) == FRB_ERR_MAGIC);
    CHECK(frb_compact_open(file("no-such-file"), &c, &bad) == FRB_ERR_IO && c == NULL);
    CHECK(frb_compact_open(path, NULL, &bad) == FRB_ERR_ARG && frb_compact_file_info(NULL, &info) == FRB_ERR_ARG);

    /* 3. The header, with its checksum made right: each field on its own. */
    {
        static const struct {
            int at;
            uint8_t value;
            FrbError want;
        } fields[] = {
            {0, 'X', FRB_ERR_MAGIC},     {8, 3, FRB_ERR_VERSION},   {8, 5, FRB_ERR_VERSION}, {10, 63, FRB_ERR_HEADER},
            {12, 10, FRB_ERR_HEADER},    {12, 12, FRB_ERR_HEADER},  {12, 0, FRB_ERR_HEADER}, {13, 31, FRB_ERR_HEADER},
            {13, 11, FRB_ERR_HEADER},    {14, 7, FRB_ERR_HEADER},   {14, 2, FRB_ERR_HEADER}, {15, 1, FRB_ERR_HEADER},
            {16, 1, FRB_ERR_HEADER},     {20, 0, FRB_ERR_HEADER},   {20, 3, FRB_ERR_HEADER}, {24, 1, FRB_ERR_HEADER},
            {28, 1, FRB_ERR_HEADER},     {32, 5, FRB_ERR_HEADER},   {33, 7, FRB_ERR_HEADER}, {34, 4, FRB_ERR_HEADER},
            {49, 0xEE, FRB_ERR_HEADER},  {53, 1, FRB_ERR_HEADER},   {59, 1, FRB_ERR_HEADER},
        };
        for (size_t k = 0; k < sizeof fields / sizeof fields[0]; k++) {
            FrbError e;
            memcpy(image, good, size);
            image[fields[k].at] = fields[k].value;
            CHECK(open_image(image, size, &bad) != FRB_OK); /* the header checksum, first */
            wr32(image + 60, frb_crc32(0, image, 60));
            e = open_image(image, size, &bad);
            CHECK_MSG(e == fields[k].want, "header byte %d set to %d: %s", fields[k].at, fields[k].value, frb_error_str(e));
            CHECK(frb_compact_file_info(file("d.frb"), NULL) == e);
            CHECK(lookup_image(image, size, 3, 5) == e);
        }
        /* a source that could not have held strings of this length is none; one that could is taken */
        memcpy(image, good, size);
        image[34] = 2;
        wr32(image + 60, frb_crc32(0, image, 60));
        CHECK(open_image(image, size, &bad) == FRB_OK);
    }

    /* 4. The directory, with its checksum and the header's made right. */
    {
        static const int rows[] = {1, 6, 7, 12};
        static const int offsets[] = {0, 4, 5, 6, 7, 8, 16, 28};
        for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
            for (size_t k = 0; k < sizeof offsets / sizeof offsets[0]; k++) {
                FrbError e;
                memcpy(image, good, size);
                image[FRB_HEADER_BYTES + (rows[r] - 1) * FRB_COMPACT_ROW_BYTES + offsets[k]] ^= 1;
                wr32(image + 36, frb_crc32(0, image + FRB_HEADER_BYTES, FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES));
                wr32(image + 60, frb_crc32(0, image, 60));
                e = open_image(image, size, &bad);
                CHECK_MSG(e == FRB_ERR_HEADER, "row %d byte %d: %s", rows[r], offsets[k], frb_error_str(e));
            }
        }
        /* a checksum for a class that has no REVs */
        memcpy(image, good, size);
        image[FRB_HEADER_BYTES + 11 * FRB_COMPACT_ROW_BYTES + 24] = 1;
        wr32(image + 36, frb_crc32(0, image + FRB_HEADER_BYTES, FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES));
        wr32(image + 60, frb_crc32(0, image, 60));
        CHECK(open_image(image, size, &bad) == FRB_ERR_HEADER);
    }

    /* 5. The index and the REVs, with every checksum made right: what is wrong is the content. */
    {
        const uint32_t stored = info.stored;
        uint8_t *nav, *nodes, *blocks;
        uint32_t s, six = 0, small = 0;
        int have_six = 0, have_small = 0;
        nav = image + info.nav_offset;
        nodes = image + info.nodes_offset;
        blocks = image + info.blocks_offset;
        for (s = 0; s < stored; s++) {
            int n = (good[info.nodes_offset + (s >> 1)] >> (4 * (s & 1))) & 15;
            if (n == 6 && !have_six && s > 300) six = s, have_six = 1;
            if (n == 4 && !have_small && s > 300) small = s, have_small = 1;
        }
        CHECK(have_six && have_small);

#define DAMAGE(change, want, at_string) \
    do { \
        FrbError e_; \
        memcpy(image, good, size); \
        change; \
        reseal(image, &info); \
        bad = 0xFFFFFFFFu; \
        e_ = open_image(image, size, &bad); \
        CHECK_MSG(e_ == (want), "%s: %s", #change, frb_error_str(e_)); \
        if ((at_string) >= 0) CHECK_MSG(bad == (uint32_t)(at_string), "%s: at %u", #change, (unsigned)bad); \
    } while (0)

        /* a node count that is none, or more than the file has */
        DAMAGE(nodes[six >> 1] &= (uint8_t)~(15u << (4 * (six & 1))), FRB_ERR_ENTRY, (int)six);
        DAMAGE(nodes[six >> 1] |= (uint8_t)(15u << (4 * (six & 1))), FRB_ERR_ENTRY, (int)six);
        /* another node count: the record of another class is taken for its REV, which does not emit it,
         * or is no REV, or there is no such record */
        for (int k = 0; k < 2; k++) {
            uint32_t at = k ? small : six;
            FrbError e;
            memcpy(image, good, size);
            nodes[at >> 1] ^= (uint8_t)(((k ? 4u : 6u) ^ 5u) << (4 * (at & 1)));
            reseal(image, &info);
            e = open_image(image, size, &bad);
            CHECK_MSG(e == FRB_ERR_OUTPUT || e == FRB_ERR_ENTRY, "another node count: %s", frb_error_str(e));
            CHECK(bad <= at);
        }
        /* a navigation bit: the string whose bit it is, as its first extension */
        for (int k = 1; k < L; k++) {
            uint32_t v = rnd((uint32_t)1 << (k - 1)), place = ((uint32_t)1 << (k - 1)) + v;
            DAMAGE(nav[place >> 3] ^= (uint8_t)(1u << (place & 7)), FRB_ERR_ENTRY, (int)(v << (L - k)));
        }
        /* place 0 of the navigation bits, which belongs to no string */
        DAMAGE(nav[0] ^= 1, FRB_ERR_HEADER, -1);
        /* a count of a block */
        DAMAGE(blocks[4 * info.max_nodes * 1 + 4 * 3] ^= 1, FRB_ERR_ENTRY, 256);
        DAMAGE(blocks[4 * info.max_nodes * 2 + 4 * 5 + 1] ^= 1, FRB_ERR_ENTRY, 512);
        DAMAGE(blocks[0] ^= 1, FRB_ERR_ENTRY, 0);
        /* A rank: another graph.  Most cannot emit the string, or not with all their nodes, and the
         * file does not open.  One that can is another REV of as many nodes for the same string: such
         * a file is a compact Bomb like any other, and only the table shows that it is not this one's. */
        {
            int refused_ranks = 0, other_bombs = 0;
            for (int k = 0; k < 3 * 23; k++) { /* each bit of the first three records of the class of 6 */
                FrbError e;
                memcpy(image, good, size);
                image[info.class_offset[6] + (uint64_t)(k >> 3)] ^= (uint8_t)(1u << (k & 7));
                reseal(image, &info);
                spit(file("d.frb"), image, size);
                e = frb_compact_open(file("d.frb"), &c, &bad);
                if (e == FRB_OK) {
                    CHECK(frb_compact_equals_table(c, &t, &bad) == FRB_ERR_EVIDENCE);
                    frb_compact_close(c);
                    c = NULL;
                    other_bombs++;
                } else {
                    CHECK(e == FRB_ERR_OUTPUT || e == FRB_ERR_ENTRY);
                    refused_ranks++;
                }
            }
            CHECK(refused_ranks > 0 && refused_ranks + other_bombs == 3 * 23);
        }
        /* colors and links of a small REV */
        DAMAGE(image[info.class_offset[4]] ^= 1, FRB_ERR_OUTPUT, -1);
        DAMAGE(image[info.class_offset[3] + 1] ^= 0x80, FRB_ERR_OUTPUT, -1);
        /* a link to a node that is not there: 3 nodes, links of 2 bits, one of them 3 */
        DAMAGE(image[info.class_offset[3]] |= 0x18, FRB_ERR_ENTRY, -1);
        /* bits after the last record of a class, and the bytes after the last class */
        {
            uint64_t used = (uint64_t)info.class_count[6] * info.class_bits[6];
            if (used % 64) DAMAGE(image[info.class_offset[6] + info.class_bytes[6] - 1] |= 0x80, FRB_ERR_HEADER, -1);
            DAMAGE(image[size - 1] = 1, FRB_ERR_HEADER, -1);
            DAMAGE(image[size - 8] = 1, FRB_ERR_HEADER, -1);
        }
#undef DAMAGE

        /* The number of other REVs in the header is nothing the file itself can show to be wrong:
         * it opens, and the comparison with the table finds it. */
        memcpy(image, good, size);
        wr32(image + 56, 2);
        wr32(image + 60, frb_crc32(0, image, 60));
        spit(file("d.frb"), image, size);
        CHECK(frb_compact_open(file("d.frb"), &c, &bad) == FRB_OK);
        if (c) {
            FrbCompactDifference d;
            CHECK(frb_compact_info(c)->other_revs == 2);
            CHECK(frb_compact_against_table(c, &t, &d, &bad) == FRB_OK && d.other_revs == 0);
            CHECK(frb_compact_equals_table(c, &t, &bad) == FRB_ERR_EVIDENCE);
            frb_compact_close(c);
            c = NULL;
        }
        /* more other REVs than strings */
        memcpy(image, good, size);
        wr32(image + 56, t.count + 1u);
        wr32(image + 60, frb_crc32(0, image, 60));
        CHECK(open_image(image, size, &bad) == FRB_ERR_HEADER);

        /* A single question put to a damaged file is answered from what it finds, without the
         * checksums: it must not read outside the file, and must not return a REV that does not
         * emit the string. */
        {
            int answered = 0, refused_lookups = 0;
            for (int k = 0; k < 400; k++) {
                size_t at = FRB_COMPACT_INDEX_OFFSET + rnd((uint32_t)(size - FRB_COMPACT_INDEX_OFFSET));
                int bits = 1 + (int)rnd((uint32_t)L);
                uint32_t v = rnd((uint32_t)1 << bits);
                FrbError e;
                memcpy(image, good, size);
                image[at] ^= (uint8_t)(1u << rnd(8));
                if (k % 3 == 0) memset(image + info.blocks_offset, 0xFF, (size_t)info.blocks_bytes);
                if (k % 7 == 0) memset(image + info.nodes_offset, 0x66, (size_t)info.nodes_bytes);
                e = lookup_image(image, size, bits, v);
                CHECK(e == FRB_OK || e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT);
                answered += e == FRB_OK;
                refused_lookups += e != FRB_OK;
            }
            CHECK(answered > 0 && refused_lookups > 100);
        }
        (void)rd32;
    }

    /* 6. Other files. */
    CHECK(frb_file_write(file("d.frb"), &t) == FRB_OK); /* a fixed-width file is no compact file */
    CHECK(frb_compact_open(file("d.frb"), &c, &bad) == FRB_ERR_VERSION && frb_file_is_compact(file("d.frb")) == 0);
    CHECK(frb_file_info(path, NULL) != FRB_OK); /* and a compact file no fixed-width one */
    {
        /* the layout of the draft: the same magic and version, 31 classes and the bytes of an ordinal */
        memcpy(image, good, size);
        image[13] = 31;
        image[14] = 2;
        wr32(image + 60, frb_crc32(0, image, 60));
        spit(file("d.frb"), image, size);
        CHECK(frb_compact_file_is_draft(file("d.frb")) == 1 && frb_file_is_compact(file("d.frb")) == 1);
        CHECK(frb_compact_open(file("d.frb"), &c, &bad) == FRB_ERR_HEADER);
        CHECK(frb_compact_file_is_draft(path) == 0 && frb_compact_file_is_draft(file("no-such-file")) == 0);
    }
    spit(file("d.frb"), (const uint8_t *)"not a bomb at all, not even nearly", 34);
    CHECK(frb_compact_open(file("d.frb"), &c, &bad) == FRB_ERR_MAGIC && frb_file_is_compact(file("d.frb")) == -1);
    CHECK(frb_file_is_compact(file("no-such-file")) == -1 && frb_file_is_compact(NULL) == -1);

    free(good);
    free(image);
    remove(path);
    remove(file("d.frb"));
    frb_table_destroy(&t);
    printf("reader: refuses a changed bit anywhere (%d places tried), a wrong length, each header field and directory\n"
           "        entry on its own, and with the checksums made right a wrong node count, navigation bit, block\n"
           "        count, rank, color or link; a single question put to a damaged file never leaves the file\n",
           flips);
}

int main(int argc, char **argv) {
    if (argc > 1) snprintf(scratch, sizeof scratch, "%s", argv[1]);
    frb_topology_init();
    test_records();
    test_layout();
    test_worked_example();
    test_conversion();
    test_rings();
    test_mixed();
    test_other_revs();
    test_write_refusals();
    test_refusals();
    return t_report("test_compact");
}
