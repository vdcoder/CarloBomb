/*
 * Tests of FixedRunBomb format version 2 (up to 24 bits, up to 24 nodes) and
 * of the ladder: a table for L bits built on the complete table for fewer.
 *
 * Usage: test_fixed_run_ladder [scratch-directory]
 *
 * The first half needs only the format code.  The second half builds small
 * ladders and compares every entry of every level with the MinimalSolver,
 * which knows nothing of tables; it needs POSIX.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include <stdint.h>
#include <stdlib.h>

#include "fixed_run.h"
#include "minimal_solver.h"
#include "test_util.h"

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fixed_run_build.h"
#endif

enum { REF_MAX = 11 }; /* longest strings solved one by one for reference */

static uint64_t rng_state = 0x13198A2E03707344ull;

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

/* Minimum node count and one minimal REV of every string of 1..REF_MAX
 * bits, by the C MinimalSolver from node budget 1. */
static uint8_t ref_min[REF_MAX + 1][1u << REF_MAX];
static RevGraph *ref_graph[REF_MAX + 1];

static void compute_reference(void) {
    char t[FRB_MAX_BITS + 1];
    for (int bits = 1; bits <= REF_MAX; bits++) {
        ref_graph[bits] = calloc((size_t)1 << bits, sizeof(RevGraph));
        for (uint32_t i = 0; i < (1u << bits); i++) {
            RevMinimalInfo info;
            frb_target_format(i, bits, t);
            CHECK(rev_minimal_solve(t, (size_t)bits, 0, NULL, &ref_graph[bits][i], &info) ==
                  REV_MIN_MINIMAL);
            ref_min[bits][i] = (uint8_t)info.node_count;
        }
    }
}

/* A complete table straight from the reference, in the given format. */
static void reference_table(FrbTable *t, int bits, const FrbFormat *format) {
    CHECK(frb_table_create_as(t, bits, format) == FRB_OK);
    for (uint32_t i = 0; i < t->count; i++)
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &ref_graph[bits][i]) == FRB_OK);
}

/* ---- format version 2 --------------------------------------------------- */

static void test_formats(void) {
    const FrbFormat *f1 = frb_format(1), *f2 = frb_format(2);
    CHECK(f1 && f1->version == 1 && f1->max_bits == 16 && f1->node_capacity == 16 &&
          f1->link_bits == 4 && f1->node_bits == 9 && f1->graph_bytes == 18 && f1->entry_bytes == 19);
    CHECK(f2 && f2->version == 2 && f2->max_bits == 24 && f2->node_capacity == 24 &&
          f2->link_bits == 5 && f2->node_bits == 11 && f2->graph_bytes == 33 && f2->entry_bytes == 34);
    CHECK(!frb_format(0) && !frb_format(3) && !frb_format(-1) && !frb_format(257));
    CHECK(frb_format_for_bits(1) == f1 && frb_format_for_bits(16) == f1);
    CHECK(frb_format_for_bits(17) == f2 && frb_format_for_bits(24) == f2);
    CHECK(!frb_format_for_bits(0) && !frb_format_for_bits(25));
    /* The constants of version 1 mean what they always meant. */
    CHECK(FRB_FORMAT_VERSION == 1 && FRB_NODE_CAPACITY == 16 && FRB_NODE_BITS == 9 &&
          FRB_GRAPH_BYTES == 18 && FRB_ENTRY_BYTES == 19 && FRB_HEADER_BYTES == 64);
    CHECK(FRB_MAX_BITS == 24 && FRB_MAX_NODES == 24 && FRB_MAX_GRAPH_BYTES == 33 &&
          FRB_MAX_ENTRY_BYTES == 34);
    /* 24 nodes of 11 bits are exactly 33 bytes; the largest table is 544 MiB. */
    CHECK(24 * 11 == 33 * 8);
    CHECK((uint64_t)34 << 24 == (uint64_t)544 << 20);
}

/* Bit `bit` of a packed graph, least significant bit of byte 0 first. */
static unsigned bit_at(const uint8_t *packed, int bit) {
    return ((unsigned)packed[bit >> 3] >> (bit & 7)) & 1u;
}

static void test_v2_golden(void) {
    /* The worked example of FORMAT.md in version 2, byte for byte. */
    static const uint8_t expect[4] = {0x45, 0x00, 0x80, 0x20};
    const FrbFormat *f2 = frb_format(2);
    uint8_t entry[FRB_MAX_ENTRY_BYTES];
    RevGraph g, back;
    FrbStatus st;
    memset(&g, 0, sizeof g);
    g.node_count = 3;
    g.nodes[0].color = REV_BLACK; g.nodes[0].links[0] = 2; g.nodes[0].links[1] = 1;
    g.nodes[1].color = REV_WHITE; g.nodes[1].links[0] = 0; g.nodes[1].links[1] = 0;
    g.nodes[2].color = REV_WHITE; g.nodes[2].links[0] = 1; g.nodes[2].links[1] = 2;
    CHECK(frb_graph_output(&g, 9) == 145);
    CHECK(frb_entry_encode_as(f2, FRB_MINIMAL, &g, entry) == FRB_OK);
    CHECK(memcmp(entry, expect, sizeof expect) == 0);
    for (int i = 4; i < 33; i++) CHECK(entry[i] == 0);
    CHECK(entry[33] == 0x43); /* status 2 in the top three bits, 3 nodes in the low five */
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_OK && st == FRB_MINIMAL && same_graph(&g, &back));

    /* Node i occupies bits 11i..11i+10: color, then link 0 (5 bits), then
     * link 1 (5 bits).  One node set in an otherwise all-zero 24-node graph. */
    for (int rep = 0; rep < 2000; rep++) {
        uint8_t packed[FRB_MAX_GRAPH_BYTES];
        int i = (int)rnd(24), color = (int)rnd(2), l0 = (int)rnd(24), l1 = (int)rnd(24);
        unsigned value = (unsigned)color | ((unsigned)l0 << 1) | ((unsigned)l1 << 6);
        int wrong = 0;
        memset(&g, 0, sizeof g);
        g.node_count = 24;
        g.nodes[i].color = color;
        g.nodes[i].links[0] = l0;
        g.nodes[i].links[1] = l1;
        CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_OK);
        for (int bit = 0; bit < 264; bit++) {
            unsigned want = bit >= 11 * i && bit < 11 * i + 11 ? (value >> (bit - 11 * i)) & 1u : 0u;
            wrong += bit_at(packed, bit) != want;
        }
        CHECK(wrong == 0);
    }
    /* All 264 bits can be in use: 24 black nodes with both links to node 23. */
    memset(&g, 0, sizeof g);
    g.node_count = 24;
    for (int i = 0; i < 24; i++) {
        g.nodes[i].color = REV_BLACK;
        g.nodes[i].links[0] = g.nodes[i].links[1] = 23;
    }
    CHECK(frb_entry_encode_as(f2, FRB_WITNESS, &g, entry) == FRB_OK);
    for (int i = 0; i < 24; i++)
        for (int k = 0; k < 11; k++) /* 1 | 23 << 1 | 23 << 6 = 10111101111 */
            CHECK(bit_at(entry, 11 * i + k) == ((0x5EFu >> k) & 1u));
    CHECK(entry[33] == ((1u << 5) | 24u));
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_OK && st == FRB_WITNESS && same_graph(&g, &back));
}

static void test_v2_roundtrip(void) {
    const FrbFormat *f1 = frb_format(1), *f2 = frb_format(2);
    uint8_t packed[FRB_MAX_GRAPH_BYTES], entry[FRB_MAX_ENTRY_BYTES];
    RevGraph g, back;
    FrbStatus st;
    for (int n = 1; n <= 24; n++) {
        int reps = n == 1 || n == 16 || n == 17 || n == 23 || n == 24 ? 2000 : 300;
        for (int r = 0; r < reps; r++) {
            random_graph(&g, n);
            CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_OK);
            CHECK(frb_graph_unpack_as(f2, packed, n, &back) == FRB_OK && same_graph(&g, &back));
            for (int bit = 11 * n; bit < 264; bit++) CHECK(bit_at(packed, bit) == 0);
            if (n < 24) { /* one node count more is not the same graph */
                FrbError e = frb_graph_unpack_as(f2, packed, n + 1, &back);
                CHECK(e == FRB_OK ? !same_graph(&g, &back) : e == FRB_ERR_ENTRY);
            }
            CHECK(frb_entry_encode_as(f2, r & 1 ? FRB_MINIMAL : FRB_WITNESS, &g, entry) == FRB_OK);
            CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_OK && same_graph(&g, &back));
            CHECK(st == (r & 1 ? FRB_MINIMAL : FRB_WITNESS));
            /* Up to 16 nodes the two versions hold the same graphs. */
            if (n <= 16) {
                uint8_t v1[FRB_GRAPH_BYTES];
                RevGraph from_v1;
                CHECK(frb_graph_pack(&g, v1) == FRB_OK);
                CHECK(frb_graph_unpack(v1, n, &from_v1) == FRB_OK && same_graph(&from_v1, &back));
            } else {
                CHECK(frb_graph_pack_as(f1, &g, packed) == FRB_ERR_CAPACITY);
            }
        }
    }
    /* Rejections. */
    random_graph(&g, 25);
    CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_ERR_CAPACITY);
    random_graph(&g, 5);
    CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_OK);
    CHECK(frb_graph_unpack_as(f2, packed, 0, &back) == FRB_ERR_ENTRY);
    CHECK(frb_graph_unpack_as(f2, packed, 25, &back) == FRB_ERR_ENTRY);
    for (int r = 0; r < 500; r++) {
        int n = 1 + (int)rnd(23), node = (int)rnd((uint32_t)n), which = (int)rnd(2);
        int bad = n + (int)rnd((uint32_t)(32 - n)); /* a link to a node that is not there */
        int at = 11 * node + 1 + 5 * which;
        random_graph(&g, n);
        CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_OK);
        for (int k = 0; k < 5; k++) {
            packed[(at + k) >> 3] &= (uint8_t)~(1u << ((at + k) & 7));
            packed[(at + k) >> 3] |= (uint8_t)(((unsigned)(bad >> k) & 1u) << ((at + k) & 7));
        }
        CHECK(frb_graph_unpack_as(f2, packed, n, &back) == FRB_ERR_ENTRY);
        /* A bit set above the last node. */
        CHECK(frb_graph_pack_as(f2, &g, packed) == FRB_OK);
        at = 11 * n + (int)rnd((uint32_t)(264 - 11 * n));
        packed[at >> 3] |= (uint8_t)(1u << (at & 7));
        CHECK(frb_graph_unpack_as(f2, packed, n, &back) == FRB_ERR_ENTRY);
    }
    /* Metadata. */
    random_graph(&g, 7);
    CHECK(frb_entry_encode_as(f2, FRB_MINIMAL, &g, entry) == FRB_OK);
    entry[33] = (uint8_t)((5u << 5) | 7u);
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_ERR_ENTRY); /* status 5 */
    entry[33] = (uint8_t)((2u << 5) | 25u);
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_ERR_ENTRY); /* 25 nodes */
    entry[33] = (uint8_t)(0u << 5);
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_ERR_ENTRY); /* EMPTY with a graph */
    CHECK(frb_entry_encode_as(f2, FRB_EMPTY, NULL, entry) == FRB_OK);
    for (int i = 0; i < 34; i++) CHECK(entry[i] == 0);
    CHECK(frb_entry_decode_as(f2, entry, &st, &back) == FRB_OK && st == FRB_EMPTY && back.node_count == 0);
    CHECK(frb_entry_encode_as(f2, FRB_UNRESOLVED, NULL, entry) == FRB_OK && entry[33] == (3u << 5));
    CHECK(frb_entry_encode_as(f2, FRB_MINIMAL, NULL, entry) == FRB_ERR_ARG);
    CHECK(frb_entry_encode_as(f2, FRB_EMPTY, &g, entry) == FRB_ERR_ARG);
    CHECK(frb_entry_encode_as(NULL, FRB_EMPTY, NULL, entry) == FRB_ERR_ARG);
}

static void fix_crcs(uint8_t *image, size_t size) {
    uint32_t crc = frb_crc32(0, image + FRB_HEADER_BYTES, size - FRB_HEADER_BYTES);
    for (int i = 0; i < 4; i++) image[36 + i] = (uint8_t)(crc >> (8 * i));
    crc = frb_crc32(0, image, 60);
    for (int i = 0; i < 4; i++) image[60 + i] = (uint8_t)(crc >> (8 * i));
}

static void test_v2_table_and_image(void) {
    const FrbFormat *f1 = frb_format(1), *f2 = frb_format(2);
    FrbTable t, back, t1;
    FrbSummary sum;
    FrbFileInfo info;
    uint8_t *image = NULL, *copy;
    size_t size = 0;
    uint32_t bad = 0;

    /* A table of 17 bits is version 2 and cannot be anything else. */
    CHECK(frb_table_create(&t, 17) == FRB_OK && t.format == f2 && t.count == 131072u);
    for (int r = 0; r < 200; r++) {
        uint32_t index = rnd(131072u);
        RevGraph ring, got;
        FrbStatus st;
        frb_ring_witness(index, 17, &ring);
        CHECK(ring.node_count == 17 && frb_graph_output(&ring, 17) == index);
        CHECK(frb_table_put(&t, index, FRB_WITNESS, &ring) == FRB_OK);
        CHECK(frb_table_put(&t, index ^ 1u, FRB_WITNESS, &ring) == FRB_ERR_OUTPUT);
        CHECK(frb_table_get(&t, index, &st, &got) == FRB_OK && st == FRB_WITNESS && same_graph(&ring, &got));
        CHECK(frb_table_nodes(&t, index) == 17);
    }
    frb_table_destroy(&t);
    CHECK(frb_table_create_as(&t, 17, f1) == FRB_ERR_ARG && t.entries == NULL);
    CHECK(frb_table_create(&t, 25) == FRB_ERR_ARG && t.entries == NULL);
    CHECK(frb_table_create_as(&t, 25, f2) == FRB_ERR_ARG);

    /* The ring is valid at every length the format holds. */
    for (int bits = 17; bits <= 24; bits++) {
        for (int r = 0; r < 300; r++) {
            uint32_t index = r == 0 ? 0u : r == 1 ? 1u : r == 2 ? (1u << bits) - 1u : rnd(1u << bits);
            uint8_t packed[FRB_MAX_GRAPH_BYTES];
            RevGraph ring, unpacked;
            frb_ring_witness(index, bits, &ring);
            CHECK(ring.node_count == bits && rev_graph_validate(&ring) == REV_OK);
            CHECK(frb_graph_output(&ring, bits) == index);
            CHECK(frb_graph_pack_as(f2, &ring, packed) == FRB_OK);
            CHECK(frb_graph_unpack_as(f2, packed, bits, &unpacked) == FRB_OK && same_graph(&ring, &unpacked));
        }
    }

    /* A small table in version 2: same content as in version 1, other bytes. */
    reference_table(&t, 8, f2);
    reference_table(&t1, 8, f1);
    CHECK(frb_table_validate(&t, &sum, NULL) == FRB_OK && sum.complete && sum.count == 256);
    CHECK(frb_table_check_symmetry(&t, NULL) == FRB_OK);
    for (uint32_t i = 0; i < 256; i++) {
        RevGraph a, b;
        FrbStatus sa = FRB_EMPTY, sb = FRB_UNRESOLVED;
        CHECK(frb_table_get(&t, i, &sa, &a) == FRB_OK);
        CHECK(frb_table_get(&t1, i, &sb, &b) == FRB_OK);
        CHECK(sa == sb && same_graph(&a, &b) && frb_table_nodes(&t, i) == ref_min[8][i]);
        CHECK(frb_table_entry(&t, i) == t.entries + 34u * i);
    }
    CHECK(frb_image_build(&t, &image, &size) == FRB_OK && size == 64 + 256 * 34);
    CHECK(image[8] == 2 && image[9] == 0 && image[12] == 8 && image[13] == 24 && image[14] == 11 &&
          image[15] == 34);
    CHECK(frb_image_parse(image, size, &back, &info, &bad) == FRB_OK);
    CHECK(info.version == 2 && info.bits == 8 && info.node_capacity == 24 && info.entry_bytes == 34 &&
          info.complete && info.count == 256);
    CHECK(back.format == f2 && memcmp(back.entries, t.entries, 256 * 34) == 0);
    frb_table_destroy(&back);

    copy = malloc(size + 1);
    memcpy(copy, image, size);
    copy[8] = 1; /* says version 1; the entries are not */
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_HEADER);
    memcpy(copy, image, size);
    copy[15] = 19;
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_HEADER);
    memcpy(copy, image, size);
    copy[13] = 16;
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_HEADER);
    memcpy(copy, image, size);
    copy[12] = 25; /* more bits than any version holds */
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_HEADER);
    CHECK(frb_image_parse(image, size - 1, NULL, NULL, NULL) == FRB_ERR_SIZE);
    CHECK(frb_image_parse(image, size - 34, NULL, NULL, NULL) == FRB_ERR_SIZE);
    memcpy(copy, image, size);
    copy[64 + 34 * 100 + 5] ^= 0x10;
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_CHECKSUM);
    memcpy(copy, image, size);
    memcpy(copy + 64 + 34 * 31, image + 64 + 34 * 32, 34); /* entry 31 holds the graph of entry 32 */
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, &bad) == FRB_ERR_OUTPUT && bad == 31);
    memcpy(copy, image, size);
    copy[64 + 34 * 12 + 32] |= 0x80; /* the last bit of the last node slot of a small graph */
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, &bad) == FRB_ERR_ENTRY && bad == 12);
    memcpy(copy, image, size);
    copy[64 + 34 * 40 + 33] = (uint8_t)(7u << 5);
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, &bad) == FRB_ERR_ENTRY && bad == 40);
    memcpy(copy, image, size);
    copy[64 + 34 * 77 + 33] = (uint8_t)((1u << 5) | (unsigned)ref_min[8][77]); /* MINIMAL -> WITNESS */
    fix_crcs(copy, size);
    CHECK(frb_image_parse(copy, size, NULL, NULL, NULL) == FRB_ERR_SUMMARY); /* header says complete */
    free(copy);
    free(image);
    frb_table_destroy(&t);
    frb_table_destroy(&t1);
}

/* Two complete tables of different lengths against each other. */
static void test_chain(void) {
    const FrbFormat *f1 = frb_format(1), *f2 = frb_format(2);
    FrbTable t[REF_MAX + 1], forged;
    uint32_t bad = 0, pick = 0;
    int found = 0;
    for (int bits = 5; bits <= REF_MAX; bits++) reference_table(&t[bits], bits, bits & 1 ? f1 : f2);
    for (int longer = 6; longer <= REF_MAX; longer++)
        for (int shorter = 5; shorter < longer; shorter++)
            CHECK(frb_table_check_chain(&t[longer], &t[shorter], &bad) == FRB_OK);
    CHECK(frb_table_check_chain(&t[7], &t[7], &bad) == FRB_ERR_ARG);
    CHECK(frb_table_check_chain(&t[7], &t[9], &bad) == FRB_ERR_ARG);
    CHECK(frb_table_check_chain(NULL, &t[9], &bad) == FRB_ERR_ARG);

    /* A 9-bit entry certified with too many nodes, where its 8-bit prefix
     * needs fewer than both of its extensions then show. */
    for (uint32_t i = 0; i < 512 && !found; i++) {
        if (ref_min[9][i] < 9 && ref_min[9][i] == ref_min[8][i >> 1] && ref_min[9][i ^ 1u] > ref_min[9][i]) {
            pick = i;
            found = 1;
        }
    }
    CHECK(found);
    reference_table(&forged, 9, f1);
    {
        RevGraph ring;
        frb_ring_witness(pick, 9, &ring);
        CHECK(frb_table_put(&forged, pick, FRB_MINIMAL, &ring) == FRB_OK); /* valid REV, false minimum */
        CHECK(frb_table_validate(&forged, NULL, NULL) == FRB_OK);          /* nothing a table alone shows */
        CHECK(frb_table_check_chain(&forged, &t[8], &bad) == FRB_ERR_EVIDENCE && (bad >> 1) == (pick >> 1));
        CHECK(frb_table_check_chain(&t[10], &forged, &bad) == FRB_ERR_EVIDENCE);
        CHECK(frb_table_put(&forged, pick, FRB_WITNESS, &ring) == FRB_OK); /* honest about it */
        CHECK(frb_table_check_chain(&forged, &t[8], &bad) == FRB_ERR_INCOMPLETE && bad == pick);
        CHECK(frb_table_check_chain(&t[10], &forged, &bad) == FRB_ERR_INCOMPLETE && (bad >> 1) == pick);
    }
    frb_table_destroy(&forged);
    /* The same in the shorter table: 8 bits that claim to need 8 nodes. */
    reference_table(&forged, 8, f2);
    {
        RevGraph ring;
        uint32_t q = 0;
        while (ref_min[8][q] >= 7) q++;
        frb_ring_witness(q, 8, &ring);
        CHECK(frb_table_put(&forged, q, FRB_MINIMAL, &ring) == FRB_OK);
        CHECK(frb_table_check_chain(&t[9], &forged, &bad) == FRB_ERR_EVIDENCE);
        CHECK(frb_table_check_chain(&t[11], &forged, &bad) == FRB_ERR_EVIDENCE);
    }
    frb_table_destroy(&forged);
    for (int bits = 5; bits <= REF_MAX; bits++) frb_table_destroy(&t[bits]);
    printf("chain: reference tables of 5 to %d bits agree pairwise; forged minima are found\n", REF_MAX);
}

/* ---- the ladder --------------------------------------------------------- */

#ifdef CARLOBOMB_HAVE_BOMB_BUILD

static char base_dir[1024];
static char last_err[FRB_ERR_TEXT];

static const char *in_base(const char *name) {
    static char bufs[32][1300];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 32;
    snprintf(buf, sizeof bufs[0], "%s/%s", base_dir, name);
    return buf;
}

static const char *level_file(const char *dir, int bits) {
    static char bufs[32][1400];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 32;
    snprintf(buf, sizeof bufs[0], "%s/fixed-run-%02d.frb", dir, bits);
    return buf;
}

static const char *level_work(const char *dir, int bits) {
    static char bufs[32][1400];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 32;
    snprintf(buf, sizeof bufs[0], "%s/work-%02d", dir, bits);
    return buf;
}

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *de;
        while (d && (de = readdir(d)) != NULL) {
            char child[2800];
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

static uint8_t *slurp(const char *path, size_t *size) {
    struct stat st;
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    *size = 0;
    if (!f || stat(path, &st) != 0) {
        if (f) fclose(f);
        return NULL;
    }
    buf = malloc((size_t)st.st_size + 1);
    *size = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    return buf;
}

static int same_file(const char *a, const char *b) {
    size_t na, nb;
    uint8_t *da = slurp(a, &na), *db = slurp(b, &nb);
    int same = da && db && na == nb && na > 0 && memcmp(da, db, na) == 0;
    free(da);
    free(db);
    return same;
}

static FrbRunOptions quiet_options(void) {
    FrbRunOptions opt;
    frb_run_options_default(&opt);
    opt.progress_seconds = 0;
    return opt;
}

static void ladder_settings(FrbConfig *settings, int workers, int format) {
    frb_config_default(settings, 1);
    settings->workers = workers;
    settings->format = format; /* 0 = what each length needs */
    settings->max_nodes = 0;
}

/* Build the table for `bits` bits directly (enumeration and search from one
 * node up) and write it to `file`. */
static void build_direct(int bits, int symmetry, int enumerate_max, const char *file) {
    const char *dir = in_base("direct");
    FrbRunOptions opt = quiet_options();
    FrbRunResult res;
    FrbConfig cfg;
    remove_tree(dir);
    frb_config_default(&cfg, bits);
    cfg.symmetry = symmetry;
    cfg.enumerate_max = enumerate_max;
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_work_run(dir, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete);
    CHECK(frb_work_finalize(dir, file, 0, NULL, last_err, sizeof last_err) == FRB_OK);
}

/* A complete Bomb file whose every minimum is the reference's. */
static void check_bomb(const char *file, int bits, int version) {
    FrbFileInfo info;
    FrbTable t;
    int wrong = 0;
    FrbError e = frb_file_read(file, &t, &info, NULL);
    CHECK_MSG(e == FRB_OK, "%s: %s", file, frb_error_str(e));
    if (e != FRB_OK) return;
    CHECK(info.complete && info.bits == bits && info.version == version && t.format->version == version);
    for (uint32_t i = 0; i < t.count; i++) wrong += frb_table_nodes(&t, i) != ref_min[bits][i];
    CHECK_MSG(wrong == 0, "%s: %d entries differ from the MinimalSolver", file, wrong);
    CHECK(frb_table_check_symmetry(&t, NULL) == FRB_OK);
    frb_table_destroy(&t);
}

typedef struct {
    uint16_t *conclusive; /* per target * 32 + budget */
    uint64_t searches, duplicates, records;
    uint32_t lowest_budget;
} Tally;

static int tally_record(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Tally *t = ctx;
    (void)seg;
    if (!rec) return 0;
    t->records++;
    if (rec->type != FRB_REC_SEARCH) return 0;
    t->searches++;
    if (rec->budget < t->lowest_budget) t->lowest_budget = rec->budget;
    if (rec->result != REV_SOLVE_RESOURCE_LIMIT && ++t->conclusive[rec->target * 32u + rec->budget] > 1)
        t->duplicates++;
    return 0;
}

/* The journal of one level: sizes, and that no search was made twice. */
static Tally tally_level(const char *dir, int bits, int record_bytes) {
    const char *work = level_work(dir, bits);
    FrbJournalInfo info;
    FrbConfig cfg;
    Tally t;
    memset(&t, 0, sizeof t);
    t.lowest_budget = 99;
    CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    t.conclusive = calloc(((size_t)1 << bits) * 32u, sizeof *t.conclusive);
    CHECK(frb_journal_scan(work, &cfg, tally_record, &t, &info, last_err, sizeof last_err) == FRB_OK);
    CHECK(info.records == t.records && info.torn_bytes == 0);
    CHECK(info.bytes == 64u * info.segments + (uint64_t)record_bytes * info.records);
    free(t.conclusive);
    t.conclusive = NULL;
    return t;
}

static const char *base6; /* the direct 6-bit Bomb every ladder here starts from */

static void test_ladder_basic(void) {
    const char *dir = in_base("ladder a"); /* a space in the name: it goes into config.txt */
    const char *other = in_base("ladder_b");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult res;
    FrbConfig settings;
    uint64_t searches = 0;

    remove_tree(dir);
    ladder_settings(&settings, 3, 0);
    CHECK(frb_ladder_run(dir, NULL, REF_MAX, &settings, &opt, &res, last_err, sizeof last_err) == FRB_ERR_ARG);
    CHECK(frb_ladder_run(dir, base6, FRB_MAX_BITS + 1, &settings, &opt, &res, last_err, sizeof last_err) ==
          FRB_ERR_ARG);
    CHECK_MSG(frb_ladder_run(dir, base6, REF_MAX, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK,
              "%s", last_err);
    CHECK(res.complete && res.top_bits == REF_MAX && res.levels_done == REF_MAX - 6);
    CHECK(same_file(level_file(dir, 6), base6));
    for (int bits = 7; bits <= REF_MAX; bits++) {
        FrbConfig cfg;
        FrbState *s = NULL;
        FrbTable longer, shorter;
        Tally t;
        char report[1500];
        struct stat st;
        check_bomb(level_file(dir, bits), bits, 1);
        /* The level's journal certifies its table, and holds no search below
         * the minimum of any string's first bits. */
        CHECK(frb_config_read(level_work(dir, bits), &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(cfg.bits == bits && cfg.base_bits == bits - 1 && cfg.format == 1 && cfg.enumerate_max == 0);
        CHECK(frb_state_load(level_work(dir, bits), &cfg, &s, last_err, sizeof last_err) == FRB_OK);
        if (s) {
            int wrong = 0;
            CHECK(s->conflicts == 0 && s->by_status[FRB_MINIMAL] == s->count);
            CHECK(s->base_settled >= s->count / 2);
            for (uint32_t i = 0; i < s->count; i++) {
                const FrbTargetState *ts = &s->targets[i];
                wrong += ts->nodes != ref_min[bits][i] || ts->lower_bound != ts->nodes;
                wrong += ts->source != FRB_SRC_BASE && ts->source != FRB_SRC_SEARCH;
                /* A string the base settled was never searched; another was
                 * searched from the minimum of its first bits, not below. */
                if (ts->source == FRB_SRC_BASE) wrong += ts->searches != 0;
            }
            CHECK_MSG(wrong == 0, "level %d: %d wrong target states", bits, wrong);
            CHECK(frb_file_read(level_file(dir, bits), &longer, NULL, NULL) == FRB_OK);
            CHECK(frb_verify_evidence(&longer, s, NULL, last_err, sizeof last_err) == FRB_OK);
            CHECK(frb_file_read(level_file(dir, bits - 1), &shorter, NULL, NULL) == FRB_OK);
            CHECK(frb_table_check_chain(&longer, &shorter, NULL) == FRB_OK);
            frb_table_destroy(&shorter);
            CHECK(frb_file_read(base6, &shorter, NULL, NULL) == FRB_OK);
            CHECK(frb_table_check_chain(&longer, &shorter, NULL) == FRB_OK);
            frb_table_destroy(&shorter);
            frb_table_destroy(&longer);
            frb_state_free(s);
        }
        t = tally_level(dir, bits, 64);
        CHECK(t.duplicates == 0 && t.searches > 0);
        searches += t.searches;
        snprintf(report, sizeof report, "%s/level-%02d.txt", dir, bits);
        CHECK(stat(report, &st) == 0 && st.st_size > 200);
    }
    /* Again: nothing to do, nothing changed. */
    CHECK(frb_ladder_run(dir, NULL, REF_MAX, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK);
    CHECK(res.complete && res.levels_done == 0 && res.top_bits == REF_MAX);
    CHECK(frb_ladder_run(dir, NULL, 9, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK);
    CHECK(res.complete && res.levels_done == 0 && res.top_bits == REF_MAX);

    /* One worker, started two levels at a time: the same files, byte for byte. */
    remove_tree(other);
    ladder_settings(&settings, 1, 0);
    for (int to = 8; to <= REF_MAX + 1; to += 2) {
        int stop = to > REF_MAX ? REF_MAX : to;
        CHECK(frb_ladder_run(other, base6, stop, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK);
        CHECK(res.complete && res.top_bits == stop);
    }
    for (int bits = 6; bits <= REF_MAX; bits++)
        CHECK(same_file(level_file(dir, bits), level_file(other, bits)));
    printf("ladder: levels 7 to %d on a direct 6-bit Bomb agree with the MinimalSolver on every string\n"
           "        (%llu searches); 3 workers and 1 worker give identical files\n",
           REF_MAX, (unsigned long long)searches);
}

/* The ladder stopped after a few jobs, again and again. */
static void test_ladder_resume(void) {
    const char *dir = in_base("ladder_c"), *whole = in_base("ladder a");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult res;
    FrbConfig settings;
    int calls = 0, top = 0, stalled = 0;
    remove_tree(dir);
    opt.limit_jobs = 23;
    do {
        ladder_settings(&settings, 1 + calls % 3, 0);
        CHECK_MSG(frb_ladder_run(dir, calls ? NULL : base6, REF_MAX - 1, &settings, &opt, &res, last_err,
                                 sizeof last_err) == FRB_OK,
                  "%s", last_err);
        if (!res.complete) CHECK(res.last.stopped_early && !res.last.interrupted && !res.last.disk_low);
        if (res.top_bits == top && res.last.jobs == 0) stalled++;
        CHECK(res.top_bits >= top);
        top = res.top_bits;
        calls++;
    } while (!res.complete && calls < 400 && stalled < 3);
    CHECK(res.complete && top == REF_MAX - 1 && stalled == 0);
    CHECK(calls > 10);
    for (int bits = 7; bits < REF_MAX; bits++) {
        Tally t = tally_level(dir, bits, 64);
        CHECK(t.duplicates == 0);
        CHECK(same_file(level_file(dir, bits), level_file(whole, bits)));
    }
    printf("resume: a ladder to %d bits finished in %d capped sessions, no search made twice, same files\n",
           REF_MAX - 1, calls);
}

/* Format version 2 forced on short strings: the content of version 1. */
static void test_ladder_v2(void) {
    const char *dir = in_base("ladder_v2"), *v1 = in_base("ladder a");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult res;
    FrbConfig settings;
    remove_tree(dir);
    ladder_settings(&settings, 2, 2);
    CHECK_MSG(frb_ladder_run(dir, base6, 10, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK, "%s",
              last_err);
    CHECK(res.complete && res.top_bits == 10);
    check_bomb(level_file(dir, 6), 6, 1); /* the starting Bomb is copied as it is */
    for (int bits = 7; bits <= 10; bits++) {
        FrbTable a, b;
        FrbConfig cfg;
        Tally t;
        int differ = 0;
        check_bomb(level_file(dir, bits), bits, 2);
        CHECK(frb_config_read(level_work(dir, bits), &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(cfg.format == 2 && cfg.max_nodes == 24);
        t = tally_level(dir, bits, 80); /* 33-byte graphs: records of 80 bytes */
        CHECK(t.duplicates == 0);
        /* Entry for entry the graphs of the version-1 ladder. */
        CHECK(frb_file_read(level_file(dir, bits), &a, NULL, NULL) == FRB_OK);
        CHECK(frb_file_read(level_file(v1, bits), &b, NULL, NULL) == FRB_OK);
        for (uint32_t i = 0; i < a.count; i++) {
            RevGraph ga, gb;
            FrbStatus sa = FRB_EMPTY, sb = FRB_UNRESOLVED;
            CHECK(frb_table_get(&a, i, &sa, &ga) == FRB_OK);
            CHECK(frb_table_get(&b, i, &sb, &gb) == FRB_OK);
            differ += sa != sb || !same_graph(&ga, &gb);
        }
        CHECK(differ == 0);
        frb_table_destroy(&a);
        frb_table_destroy(&b);
    }
    printf("version 2: a ladder forced into the 24-bit format holds the graphs of the version-1 ladder\n");
}

static void write_file(const char *path, const uint8_t *data, size_t size) {
    CHECK(frb_write_atomic(path, data, size) == FRB_OK);
}

static void fix_file_crcs(uint8_t *image, size_t size) { fix_crcs(image, size); }

/* What a construction refuses to build on. */
static void test_base_refusals(void) {
    const char *dir = in_base("refuse"), *work = in_base("refuse_work");
    const char *base7 = level_file(in_base("ladder a"), 7);
    const char *partial = in_base("partial6.frb"), *other7 = in_base("other7.frb");
    const char *moved = in_base("moved7.frb"), *forged = in_base("forged7.frb");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult lres;
    FrbRunResult res;
    FrbConfig cfg, settings, back;
    FrbState *s = NULL;
    uint8_t *image;
    size_t size;

    /* A partial Bomb. */
    {
        const char *pd = in_base("partial_work");
        FrbRunOptions few = quiet_options();
        remove_tree(pd);
        frb_config_default(&cfg, 6);
        cfg.enumerate_max = 2;
        few.limit_jobs = 8;
        CHECK(frb_work_create(pd, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_run(pd, &cfg, &few, &res, last_err, sizeof last_err) == FRB_OK && !res.complete);
        CHECK(frb_work_finalize(pd, partial, 1, NULL, last_err, sizeof last_err) == FRB_OK);
    }
    frb_config_default(&cfg, 8);
    CHECK(frb_config_set_base(&cfg, partial, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);
    CHECK(cfg.base_bits == 0 && cfg.base_file[0] == '\0');
    remove_tree(dir);
    ladder_settings(&settings, 1, 0);
    CHECK(frb_ladder_run(dir, partial, 8, &settings, &opt, &lres, last_err, sizeof last_err) ==
          FRB_ERR_INCOMPLETE);
    CHECK(lres.top_bits == 0 && !lres.complete);
    /* Not shorter than the construction; not there; not a Bomb. */
    frb_config_default(&cfg, 7);
    CHECK(frb_config_set_base(&cfg, base7, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 6);
    CHECK(frb_config_set_base(&cfg, base7, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 8);
    CHECK(frb_config_set_base(&cfg, in_base("nothing.frb"), last_err, sizeof last_err) == FRB_ERR_IO);
    CHECK(frb_config_set_base(&cfg, in_base("ladder a/work-07/config.txt"), last_err, sizeof last_err) ==
          FRB_ERR_MAGIC);
    /* A configuration cannot name a base without saying which, or the reverse. */
    frb_config_default(&cfg, 8);
    cfg.base_bits = 7;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 8);
    strcpy(cfg.base_file, "x.frb");
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 8);
    cfg.base_bits = 8;
    strcpy(cfg.base_file, "x.frb");
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 17);
    cfg.format = 1;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 8);
    cfg.max_nodes = 17; /* version 1 holds 16 */
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.format = 2;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_OK);
    cfg.max_nodes = 25;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);

    /* A construction on base7, half done. */
    image = slurp(base7, &size);
    CHECK(image != NULL && size == 64 + 128 * 19);
    write_file(moved, image, size);
    remove_tree(work);
    frb_config_default(&cfg, 8);
    CHECK(frb_config_set_base(&cfg, moved, last_err, sizeof last_err) == FRB_OK);
    CHECK(cfg.base_bits == 7 && cfg.enumerate_max == 0 && cfg.base_file[0] == '/');
    CHECK(frb_work_create(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_config_read(work, &back, last_err, sizeof last_err) == FRB_OK);
    CHECK(memcmp(&back, &cfg, sizeof cfg) == 0);
    opt.limit_jobs = 20;
    CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && !res.complete);
    opt.limit_jobs = 0;

    /* Its base gone. */
    CHECK(unlink(moved) == 0);
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_IO && s == NULL);
    CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_ERR_IO);
    CHECK(frb_work_finalize(work, in_base("never.frb"), 1, NULL, last_err, sizeof last_err) == FRB_ERR_IO);

    /* Another complete 7-bit Bomb in its place: other graphs, other checksum. */
    build_direct(7, 0, 0, other7);
    check_bomb(other7, 7, 1);
    CHECK(!same_file(other7, base7));
    {
        size_t other_size;
        uint8_t *other = slurp(other7, &other_size);
        write_file(moved, other, other_size);
        free(other);
    }
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE && s == NULL);
    CHECK(strstr(last_err, "is not the Bomb this construction was started on") != NULL);
    CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);

    /* A base whose checksums are right and whose entry 5 is the graph of
     * entry 9: nothing in it is trusted unrun. */
    memcpy(image + 64 + 19 * 5, image + 64 + 19 * 9, 19);
    fix_file_crcs(image, size);
    write_file(forged, image, size);
    frb_config_default(&cfg, 8);
    CHECK(frb_config_set_base(&cfg, forged, last_err, sizeof last_err) == FRB_OK); /* the header is fine */
    remove_tree(work);
    CHECK(frb_work_create(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_OUTPUT && s == NULL);
    CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_ERR_OUTPUT);
    /* And one that says "complete" over an entry that is only a witness. */
    free(image);
    image = slurp(base7, &size);
    image[64 + 19 * 5 + 18] = (uint8_t)((1u << 5) | (image[64 + 19 * 5 + 18] & 31u));
    fix_file_crcs(image, size);
    write_file(forged, image, size);
    frb_config_default(&cfg, 8);
    CHECK(frb_config_set_base(&cfg, forged, last_err, sizeof last_err) == FRB_OK);
    remove_tree(work);
    CHECK(frb_work_create(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE && s == NULL);
    free(image);

    /* A ladder whose lower Bomb was exchanged under a level in progress. */
    remove_tree(dir);
    ladder_settings(&settings, 2, 0);
    opt.limit_jobs = 20;
    CHECK(frb_ladder_run(dir, base7, 9, &settings, &opt, &lres, last_err, sizeof last_err) == FRB_OK);
    CHECK(!lres.complete && lres.top_bits == 7);
    opt.limit_jobs = 0;
    {
        size_t other_size;
        uint8_t *other = slurp(other7, &other_size);
        write_file(level_file(dir, 7), other, other_size);
        free(other);
    }
    CHECK(frb_ladder_run(dir, NULL, 9, &settings, &opt, &lres, last_err, sizeof last_err) ==
          FRB_ERR_INCOMPATIBLE);
    CHECK(strstr(last_err, "was not started on the Bomb") != NULL && lres.top_bits == 7);
    /* A file in the ladder that is not a complete Bomb stops everything. */
    {
        size_t partial_size;
        uint8_t *bytes = slurp(partial, &partial_size);
        write_file(level_file(dir, 6), bytes, partial_size);
        CHECK(unlink(level_file(dir, 7)) == 0);
        CHECK(frb_ladder_run(dir, NULL, 9, &settings, &opt, &lres, last_err, sizeof last_err) ==
              FRB_ERR_INCOMPATIBLE);
        free(bytes);
    }
    printf("refusals: partial, missing, exchanged and forged base Bombs are all refused\n");
}

static void append_record(const char *dir, const FrbConfig *cfg, uint32_t number, const FrbRecord *rec) {
    FrbSegment *seg = NULL;
    CHECK(frb_segment_create(dir, cfg, number, 999, 0, &seg) == FRB_OK);
    if (!seg) return;
    CHECK(frb_segment_append(seg, rec) == FRB_OK);
    CHECK(frb_segment_close(seg) == FRB_OK);
}

/* What the base Bomb is taken to prove, and what contradicts it. */
static void test_base_evidence(void) {
    const char *work = in_base("evidence");
    const char *base7 = level_file(in_base("ladder a"), 7);
    FrbRunOptions opt = quiet_options();
    FrbRunResult res;
    FrbConfig cfg;
    FrbState *s = NULL;
    FrbRecord rec;
    uint32_t settled = 0, open_target = 0, base_target = 0;
    int wrong = 0;

    for (int symmetry = 0; symmetry <= 1; symmetry++) {
        remove_tree(work);
        frb_config_default(&cfg, 8);
        cfg.symmetry = symmetry;
        cfg.fallback_ring = symmetry; /* with and without the ring */
        CHECK(frb_config_set_base(&cfg, base7, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_create(work, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_OK);
        if (!s) return;
        /* Before any search: what is certified is right, what is excluded
         * is really impossible, and half the table is settled. */
        CHECK(s->conflicts == 0 && s->journal.records == 0);
        settled = 0;
        wrong = 0;
        for (uint32_t i = 0; i < s->count; i++) {
            const FrbTargetState *ts = &s->targets[i];
            if (ts->status == FRB_MINIMAL) {
                settled++;
                base_target = i;
                wrong += ts->nodes != ref_min[8][i] || ts->source != FRB_SRC_BASE;
            } else {
                open_target = i;
                /* With the ring it has a witness; without, it is known to
                 * need more than one node, or nothing is known at all. */
                wrong += ts->status != (symmetry ? FRB_WITNESS
                                                 : ts->lower_bound > 1 ? FRB_UNRESOLVED : FRB_EMPTY);
                wrong += ts->nodes != (symmetry ? 8 : 0);
            }
            wrong += ts->lower_bound > ref_min[8][i];
            wrong += ts->lower_bound != ref_min[7][i >> 1]; /* exactly the minimum of the first 7 bits */
        }
        CHECK(wrong == 0);
        CHECK(settled == s->base_settled && settled >= 128 && settled < 256);
        frb_state_free(s);
        s = NULL;
        CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete);
        CHECK(res.jobs == (symmetry ? (256 - settled) / 2 : 256 - settled));
        CHECK(frb_work_finalize(work, in_base("evidence.frb"), 0, NULL, last_err, sizeof last_err) == FRB_OK);
        check_bomb(in_base("evidence.frb"), 8, 1);
    }

    /* A forged record: "no REV of up to m nodes" for a string the base Bomb
     * has a REV of m nodes for. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_PROVEN_UNSAT;
    rec.budget = ref_min[8][base_target];
    rec.target = base_target;
    append_record(work, &cfg, 900, &rec);
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_OK);
    if (s) {
        CHECK(s->conflicts >= 1 && s->targets[base_target].conflict);
        CHECK(s->targets[base_target].status != FRB_MINIMAL);
        frb_state_free(s);
        s = NULL;
    }
    CHECK(frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_ERR_EVIDENCE);
    CHECK(frb_work_finalize(work, in_base("never.frb"), 1, NULL, last_err, sizeof last_err) ==
          FRB_ERR_EVIDENCE);
    (void)open_target;
    printf("evidence: the base Bomb settles %u of 256 strings and bounds the rest exactly from below;\n"
           "          a forged record against one of its REVs is a contradiction, not a result\n",
           (unsigned)settled);
}

/* A full disk is a reason to stop, not to fail. */
static void test_disk_watch(void) {
    const char *dir = in_base("disk");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult res;
    FrbConfig settings;
    remove_tree(dir);
    for (int workers = 1; workers <= 2; workers++) {
        ladder_settings(&settings, workers, 0);
        opt.min_free_bytes = UINT64_MAX / 2; /* more than any disk has */
        CHECK(frb_ladder_run(dir, base6, 8, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK);
        CHECK(!res.complete && res.last.disk_low && !res.last.interrupted && res.last.jobs == 0);
        CHECK(res.top_bits == 6);
    }
    opt.min_free_bytes = 1; /* watched, and enough */
    CHECK(frb_ladder_run(dir, base6, 8, &settings, &opt, &res, last_err, sizeof last_err) == FRB_OK);
    CHECK(res.complete && !res.last.disk_low && res.top_bits == 8);
    check_bomb(level_file(dir, 8), 8, 1);
    printf("disk: a construction that finds too little free space stops cleanly and continues later\n");
}

/* config.txt: what a construction without a base writes has not changed. */
static void test_config_layouts(void) {
    const char *dir = in_base("layout");
    FrbConfig cfg, back;
    size_t size;
    char *text;
    remove_tree(dir);
    frb_config_default(&cfg, 12);
    CHECK(cfg.format == 1 && cfg.max_nodes == 16 && cfg.base_bits == 0);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    text = (char *)slurp(in_base("layout/config.txt"), &size);
    CHECK(text != NULL);
    if (text) {
        text[size] = '\0';
        CHECK(strncmp(text, "carlobomb-fixed-run-work 1\n", 27) == 0);
        CHECK(strstr(text, "\njournal_version 1\n") && !strstr(text, "format ") && !strstr(text, "base_"));
        free(text);
    }
    remove_tree(dir);
    frb_config_default(&cfg, 20);
    CHECK(cfg.format == 2 && cfg.max_nodes == 24);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    text = (char *)slurp(in_base("layout/config.txt"), &size);
    CHECK(text != NULL);
    if (text) {
        text[size] = '\0';
        CHECK(strncmp(text, "carlobomb-fixed-run-work 2\n", 27) == 0);
        CHECK(strstr(text, "\njournal_version 2\n") && strstr(text, "\nformat 2\n") &&
              strstr(text, "\nbase_bits 0\n") && !strstr(text, "\nbase_file") && !strstr(text, "\nbase_crc"));
        free(text);
    }
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_OK && memcmp(&back, &cfg, sizeof cfg) == 0);
    /* A base line in a layout that has none, and a missing one in the layout that needs it. */
    {
        FILE *f = fopen(in_base("layout/config.txt"), "w");
        fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 1\nsearch_revision 1\nbits 9\nsymmetry 1\n"
                   "enumerate_max 0\nbase_bits 8\nmax_nodes 16\nfallback ring\nengine c\nworkers 1\n"
                   "max_steps 0\n");
        fclose(f);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        f = fopen(in_base("layout/config.txt"), "w");
        fprintf(f, "carlobomb-fixed-run-work 2\njournal_version 1\nsearch_revision 1\nbits 9\nsymmetry 1\n"
                   "enumerate_max 0\nformat 1\nbase_bits 8\nbase_crc 0badf00d\nmax_nodes 16\n"
                   "fallback ring\nengine c\nworkers 1\nmax_steps 0\n");
        fclose(f);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        f = fopen(in_base("layout/config.txt"), "w");
        fprintf(f, "carlobomb-fixed-run-work 2\njournal_version 1\nsearch_revision 1\nbits 9\nsymmetry 1\n"
                   "enumerate_max 0\nformat 2\nbase_bits 0\nmax_nodes 16\n"
                   "fallback ring\nengine c\nworkers 1\nmax_steps 0\n");
        fclose(f);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE); /* 1 != 2 */
        f = fopen(in_base("layout/config.txt"), "w");
        fprintf(f, "carlobomb-fixed-run-work 3\n");
        fclose(f);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    }
}

#endif /* CARLOBOMB_HAVE_BOMB_BUILD */

int main(int argc, char **argv) {
    compute_reference();
    test_formats();
    test_v2_golden();
    test_v2_roundtrip();
    test_v2_table_and_image();
    test_chain();

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    snprintf(base_dir, sizeof base_dir, "%s", argc > 1 ? argv[1] : "fixed_run_ladder_test_work");
    remove_tree(base_dir);
    CHECK(mkdir(base_dir, 0777) == 0);
    base6 = in_base("base6.frb");
    {
        static char keep[1300];
        snprintf(keep, sizeof keep, "%s", base6);
        base6 = keep;
    }
    build_direct(6, 1, 3, base6);
    check_bomb(base6, 6, 1);
    test_config_layouts();
    test_ladder_basic();
    test_ladder_resume();
    test_ladder_v2();
    test_base_refusals();
    test_base_evidence();
    test_disk_watch();
    if (!t_failures) remove_tree(base_dir); /* keep the evidence of a failure */
#else
    (void)argc;
    (void)argv;
    printf("ladder tests skipped: no POSIX build of the constructor\n");
#endif
    for (int bits = 1; bits <= REF_MAX; bits++) free(ref_graph[bits]);
    return t_report("test_fixed_run_ladder");
}
