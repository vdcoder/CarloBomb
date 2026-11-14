/*
 * Tests of the FixedRunBomb format, enumerator and construction.
 *
 * Usage: test_fixed_run [scratch-directory]
 *
 * The first half needs only the format code and runs everywhere.  The second
 * half builds small tables in work directories under the scratch directory,
 * interrupts and damages them in every way the design claims to survive, and
 * needs POSIX.
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fixed_run_build.h"
#endif

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static uint32_t rnd(uint32_t n) { /* xorshift64*, uniform enough for tests */
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

/* Minimum node count of every `bits`-bit string, by the C MinimalSolver. */
static uint8_t ref_min[1u << 12];
static RevGraph ref_graph_tmp;

static void compute_reference(int bits) {
    char t[FRB_MAX_BITS + 1];
    for (uint32_t i = 0; i < (1u << bits); i++) {
        RevMinimalInfo info;
        frb_target_format(i, bits, t);
        RevMinimalStatus s = rev_minimal_solve(t, (size_t)bits, 0, NULL, &ref_graph_tmp, &info);
        CHECK(s == REV_MIN_MINIMAL);
        ref_min[i] = (uint8_t)info.node_count;
    }
}

static uint32_t ref_count_upto(int bits, int nodes) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < (1u << bits); i++) n += ref_min[i] <= nodes;
    return n;
}

/* ---- format ------------------------------------------------------------- */

static void test_targets(void) {
    char out[FRB_MAX_BITS + 1];
    uint32_t index = 0;
    int bits = 0;
    CHECK(frb_target_parse("0100100011010110", &bits, &index) && bits == 16 && index == 0x48D6u);
    CHECK(frb_target_parse("1", &bits, &index) && bits == 1 && index == 1);
    CHECK(frb_target_parse("010010001", &bits, &index) && bits == 9 && index == 145);
    CHECK(!frb_target_parse("", &bits, &index));
    CHECK(frb_target_parse("01000100011010110", &bits, &index) && bits == 17 && index == 0x88D6u);
    CHECK(frb_target_parse("100000000000000000000001", &bits, &index) && bits == 24 && index == 0x800001u);
    CHECK(frb_target_parse("0100010001101011001000100", &bits, &index) && bits == 25);
    CHECK(!frb_target_parse("01000100011010110010001000100010", &bits, &index)); /* 32 bits */
    CHECK(!frb_target_parse("0102", &bits, &index));
    CHECK(!frb_target_parse(NULL, &bits, &index));
    for (int b = 1; b <= FRB_MAX_BITS; b++) {
        for (int k = 0; k < 50; k++) {
            uint32_t v = rnd(1u << b), back = 0;
            int bb = 0;
            frb_target_format(v, b, out);
            CHECK(strlen(out) == (size_t)b && frb_target_parse(out, &bb, &back) && bb == b && back == v);
        }
    }
    frb_target_format(0x48D6u, 16, out);
    CHECK(strcmp(out, "0100100011010110") == 0);
    CHECK(frb_crc32(0, "123456789", 9) == 0xCBF43926u); /* the standard check value */
}

/* The worked example of FORMAT.md, byte for byte. */
static void test_pack_golden(void) {
    static const uint8_t expect[FRB_ENTRY_BYTES] = {0x25, 0x00, 0x08, 0x01, 0, 0, 0, 0, 0, 0,
                                                    0,    0,    0,    0,    0, 0, 0, 0, 0x43};
    uint8_t entry[FRB_ENTRY_BYTES];
    RevGraph g, back;
    FrbStatus st;
    memset(&g, 0, sizeof g);
    g.node_count = 3; /* the legacy solver's answer for 010010001 */
    g.nodes[0].color = REV_BLACK; g.nodes[0].links[0] = 2; g.nodes[0].links[1] = 1;
    g.nodes[1].color = REV_WHITE; g.nodes[1].links[0] = 0; g.nodes[1].links[1] = 0;
    g.nodes[2].color = REV_WHITE; g.nodes[2].links[0] = 1; g.nodes[2].links[1] = 2;
    CHECK(frb_graph_output(&g, 9) == 145);
    CHECK(frb_entry_encode(FRB_MINIMAL, &g, entry) == FRB_OK);
    CHECK(memcmp(entry, expect, sizeof expect) == 0);
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_OK && st == FRB_MINIMAL && same_graph(&g, &back));

    /* Node i occupies bits 9i..9i+8: color, then link 0, then link 1. */
    memset(&g, 0, sizeof g);
    g.node_count = 16;
    for (int i = 0; i < 16; i++) {
        g.nodes[i].color = REV_BLACK;
        g.nodes[i].links[0] = 15;
        g.nodes[i].links[1] = 15;
    }
    CHECK(frb_entry_encode(FRB_WITNESS, &g, entry) == FRB_OK);
    for (int i = 0; i < FRB_GRAPH_BYTES; i++) CHECK(entry[i] == 0xFF);
    CHECK(entry[FRB_GRAPH_BYTES] == ((1u << 5) | 16u));
    for (int i = 0; i < 16; i++) g.nodes[i].links[1] = 0; /* link 1 is the top 4 bits of a node */
    CHECK(frb_graph_pack(&g, entry) == FRB_OK);
    CHECK(entry[0] == 0x1F && entry[1] == 0x3E); /* 000011111 000011111 ... least significant first */
}

static void test_pack_roundtrip(void) {
    uint8_t packed[FRB_GRAPH_BYTES], entry[FRB_ENTRY_BYTES];
    RevGraph g, back;
    static const int required[] = {1, 8, 15, 16};
    for (int n = 1; n <= FRB_NODE_CAPACITY; n++) {
        int reps = 200;
        for (size_t k = 0; k < sizeof required / sizeof required[0]; k++)
            if (required[k] == n) reps = 2000;
        for (int r = 0; r < reps; r++) {
            FrbStatus st;
            random_graph(&g, n);
            CHECK(frb_graph_pack(&g, packed) == FRB_OK);
            CHECK(frb_graph_unpack(packed, n, &back) == FRB_OK && same_graph(&g, &back));
            /* Everything above the last node is zero. */
            for (int bit = 9 * n; bit < 144; bit++) CHECK(((packed[bit >> 3] >> (bit & 7)) & 1) == 0);
            /* One node count more or less is not the same graph. */
            if (n < FRB_NODE_CAPACITY) {
                FrbError e = frb_graph_unpack(packed, n + 1, &back);
                CHECK(e == FRB_OK ? !same_graph(&g, &back) : e == FRB_ERR_ENTRY);
            }
            CHECK(frb_entry_encode(r & 1 ? FRB_MINIMAL : FRB_WITNESS, &g, entry) == FRB_OK);
            CHECK(frb_entry_decode(entry, &st, &back) == FRB_OK && same_graph(&g, &back));
            CHECK(st == (r & 1 ? FRB_MINIMAL : FRB_WITNESS));
        }
    }
}

static void test_pack_rejects(void) {
    uint8_t packed[FRB_GRAPH_BYTES], entry[FRB_ENTRY_BYTES];
    RevGraph g, back;
    FrbStatus st;

    random_graph(&g, 17);
    CHECK(frb_graph_pack(&g, packed) == FRB_ERR_CAPACITY); /* never truncated */
    CHECK(frb_entry_encode(FRB_WITNESS, &g, entry) == FRB_ERR_CAPACITY);
    random_graph(&g, 5);
    g.nodes[3].links[1] = 5;
    CHECK(frb_graph_pack(&g, packed) == FRB_ERR_ARG);
    g.nodes[3].links[1] = 0;
    g.nodes[2].color = 2;
    CHECK(frb_graph_pack(&g, packed) == FRB_ERR_ARG);
    g.node_count = 0;
    CHECK(frb_graph_pack(&g, packed) == FRB_ERR_ARG);

    random_graph(&g, 5);
    g.nodes[4].links[0] = 4;
    CHECK(frb_graph_pack(&g, packed) == FRB_OK);
    CHECK(frb_graph_unpack(packed, 4, &back) == FRB_ERR_ENTRY);  /* link 4 with 4 nodes */
    CHECK(frb_graph_unpack(packed, 0, &back) == FRB_ERR_ENTRY);
    CHECK(frb_graph_unpack(packed, 17, &back) == FRB_ERR_ENTRY);
    packed[17] |= 0x80; /* the last bit of node slot 15 */
    CHECK(frb_graph_unpack(packed, 5, &back) == FRB_ERR_ENTRY);  /* padding must be zero */

    random_graph(&g, 3);
    CHECK(frb_entry_encode(FRB_MINIMAL, &g, entry) == FRB_OK);
    for (unsigned code = 5; code < 8; code++) {
        entry[FRB_GRAPH_BYTES] = (uint8_t)((code << 5) | 3u);
        CHECK(frb_entry_decode(entry, &st, &back) == FRB_ERR_ENTRY); /* unknown status */
    }
    entry[FRB_GRAPH_BYTES] = (uint8_t)((FRB_MINIMAL << 5) | 0u);
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_ERR_ENTRY);     /* witness without nodes */
    entry[FRB_GRAPH_BYTES] = (uint8_t)((FRB_MINIMAL << 5) | 17u);
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_ERR_ENTRY);

    CHECK(frb_entry_encode(FRB_UNRESOLVED, NULL, entry) == FRB_OK);
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_OK && st == FRB_UNRESOLVED && back.node_count == 0);
    entry[FRB_GRAPH_BYTES] |= 1u;
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_ERR_ENTRY);     /* nodes without a witness */
    CHECK(frb_entry_encode(FRB_EMPTY, NULL, entry) == FRB_OK);
    entry[4] = 1;
    CHECK(frb_entry_decode(entry, &st, &back) == FRB_ERR_ENTRY);     /* payload without a witness */
    CHECK(frb_entry_encode(FRB_EMPTY, &g, entry) == FRB_ERR_ARG);
    CHECK(frb_entry_encode(FRB_WITNESS, NULL, entry) == FRB_ERR_ARG);
}

static uint64_t run64(const RevGraph *g) {
    char out[65];
    uint64_t v = 0;
    CHECK(rev_run(g, 64, out) == REV_OK);
    for (int i = 0; i < 64; i++) v = (v << 1) | (uint64_t)(out[i] - '0');
    return v;
}

/* Complement symmetry: the transformed graph against its own output, on
 * every labeled graph of up to 4 nodes and on random larger ones. */
static void test_complement(void) {
    unsigned long long graphs = 0;
    RevGraph g, c, cc;
    for (int n = 1; n <= 4; n++) {
        int digit[8] = {0};
        memset(&g, 0, sizeof g);
        g.node_count = n;
        for (;;) {
            int k = 0;
            for (int i = 0; i < n; i++) {
                g.nodes[i].links[0] = digit[2 * i];
                g.nodes[i].links[1] = digit[2 * i + 1];
            }
            for (unsigned colors = 0; colors < (1u << n); colors++) {
                int ok;
                for (int i = 0; i < n; i++) g.nodes[i].color = (int)((colors >> i) & 1u);
                frb_graph_complement(&g, &c);
                ok = c.node_count == n && rev_graph_validate(&c) == REV_OK &&
                     frb_graph_output(&c, 16) == (frb_graph_output(&g, 16) ^ 0xFFFFu);
                if (!ok) CHECK_MSG(ok, "complement fails for a %d-node graph", n);
                graphs++;
            }
            while (k < 2 * n && ++digit[k] == n) digit[k++] = 0;
            if (k == 2 * n) break;
        }
    }
    CHECK(graphs == 2 + 64 + 5832 + 1048576ull);
    for (int r = 0; r < 20000; r++) {
        random_graph(&g, 1 + (int)rnd(16));
        frb_graph_complement(&g, &c);
        CHECK(c.node_count == g.node_count);
        CHECK(run64(&c) == ~run64(&g));
        frb_graph_complement(&c, &cc);
        CHECK(same_graph(&g, &cc)); /* an involution */
        frb_graph_complement(&c, &c);  /* in place */
        CHECK(same_graph(&g, &c));
    }
    printf("complement symmetry: %llu small graphs exhaustively, 20000 random ones for 64 bits\n", graphs);
}

/* The ring emits its target, for every string of every length 1..16: no
 * L-bit string needs more than L nodes, and so none overflows an entry. */
static void test_ring(void) {
    enum { ALL_UP_TO = 20 }; /* every string up to here; a sample of the 4 billion beyond */
    unsigned long long checked = 0, sampled = 0;
    RevGraph g;
    for (int bits = 1; bits <= FRB_MAX_BITS; bits++) {
        uint32_t top = (uint32_t)1 << (bits - 1);
        uint32_t count = bits <= ALL_UP_TO ? top * 2u : 200000u;
        int bad = 0;
        for (uint32_t k = 0; k < count; k++) {
            /* Beyond ALL_UP_TO: the two ends of the table, then random strings. */
            uint32_t i = bits <= ALL_UP_TO ? k : k < 1000 ? k : k < 2000 ? top * 2u - 1u - (k - 1000u)
                                                                        : rnd(top) * 2u + rnd(2);
            frb_ring_witness(i, bits, &g);
            if (g.node_count != bits || rev_graph_validate(&g) != REV_OK ||
                frb_graph_output(&g, bits) != i)
                bad++;
            if (bits <= ALL_UP_TO) checked++;
            else sampled++;
        }
        CHECK_MSG(bad == 0, "ring fails for %d strings of %d bits", bad, bits);
    }
    printf("ring witness: valid for all %llu strings of 1..%d bits and %llu sampled ones of %d..%d bits\n",
           checked, (int)ALL_UP_TO, sampled, ALL_UP_TO + 1, FRB_MAX_BITS);
}

/* A complete 8-bit table straight from the C MinimalSolver. */
static void make_table(FrbTable *t, int bits) {
    char target[FRB_MAX_BITS + 1];
    RevGraph g;
    CHECK(frb_table_create(t, bits) == FRB_OK);
    for (uint32_t i = 0; i < t->count; i++) {
        frb_target_format(i, bits, target);
        CHECK(rev_minimal_solve(target, (size_t)bits, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
}

static void fix_crcs(uint8_t *image, size_t size) {
    uint32_t crc = frb_crc32(0, image + FRB_HEADER_BYTES, size - FRB_HEADER_BYTES);
    for (int i = 0; i < 4; i++) image[36 + i] = (uint8_t)(crc >> (8 * i));
    crc = frb_crc32(0, image, 60);
    for (int i = 0; i < 4; i++) image[60 + i] = (uint8_t)(crc >> (8 * i));
}

static FrbError parse_mutated(const uint8_t *image, size_t size, size_t offset, unsigned xor_mask,
                              int fix, uint32_t *bad) {
    uint8_t *copy = malloc(size);
    FrbError e;
    memcpy(copy, image, size);
    copy[offset] ^= (uint8_t)xor_mask;
    if (fix) fix_crcs(copy, size);
    e = frb_image_parse(copy, size, NULL, NULL, bad);
    free(copy);
    return e;
}

static void test_table_and_image(void) {
    FrbTable t, back;
    FrbFileInfo info;
    FrbSummary sum;
    uint8_t *image = NULL;
    size_t size = 0;
    uint32_t bad = 99999;
    RevGraph g, ring;
    FrbStatus st;

    make_table(&t, 8);
    CHECK(frb_table_validate(&t, &sum, NULL) == FRB_OK && sum.complete && sum.count == 256);
    CHECK(sum.by_status[FRB_MINIMAL] == 256 && sum.max_minimal_nodes == sum.max_witness_nodes);
    CHECK(sum.minimal_by_nodes[1] == 2 && sum.minimal_by_nodes[2] == 6 && sum.minimal_by_nodes[0] == 0);
    CHECK(frb_table_check_symmetry(&t, NULL) == FRB_OK);

    CHECK(frb_image_build(&t, &image, &size) == FRB_OK);
    CHECK(size == FRB_HEADER_BYTES + 256 * FRB_ENTRY_BYTES);
    CHECK(frb_image_parse(image, size, &back, &info, NULL) == FRB_OK);
    CHECK(info.version == 1 && info.bits == 8 && info.count == 256 && info.complete);
    CHECK(info.witnesses == 256 && info.minimal == 256 && info.node_capacity == 16);
    CHECK(memcmp(back.entries, t.entries, 256 * FRB_ENTRY_BYTES) == 0);
    frb_table_destroy(&back);

    /* A witness filed under the wrong string is refused at the door. */
    CHECK(frb_table_get(&t, 5, &st, &g) == FRB_OK && st == FRB_MINIMAL);
    CHECK(frb_table_put(&t, 6, FRB_MINIMAL, &g) == FRB_ERR_OUTPUT);
    CHECK(frb_table_put(&t, 256, FRB_MINIMAL, &g) == FRB_ERR_ARG);

    /* Damage, one kind at a time.  The first group is caught by a checksum
     * or the header; the second is damage with every checksum repaired, as a
     * buggy or dishonest writer would produce, and must be caught anyway. */
    CHECK(frb_image_parse(image, size - 1, NULL, NULL, NULL) == FRB_ERR_SIZE);
    CHECK(frb_image_parse(image, FRB_HEADER_BYTES, NULL, NULL, NULL) == FRB_ERR_SIZE);
    CHECK(frb_image_parse(image, 20, NULL, NULL, NULL) == FRB_ERR_SIZE);
    CHECK(frb_image_parse(image, 5, NULL, NULL, NULL) == FRB_ERR_MAGIC);
    CHECK(frb_image_parse(image, 0, NULL, NULL, NULL) == FRB_ERR_MAGIC);
    {
        uint8_t *longer = malloc(size + 1);
        memcpy(longer, image, size);
        longer[size] = 0;
        CHECK(frb_image_parse(longer, size + 1, NULL, NULL, NULL) == FRB_ERR_SIZE);
        free(longer);
    }
    CHECK(parse_mutated(image, size, 0, 0x20, 0, NULL) == FRB_ERR_MAGIC);
    CHECK(parse_mutated(image, size, 8, 0x05, 0, NULL) == FRB_ERR_VERSION);  /* version 4 */
    CHECK(parse_mutated(image, size, 8, 0x05, 1, NULL) == FRB_ERR_VERSION);
    CHECK(parse_mutated(image, size, 8, 0x03, 0, NULL) == FRB_ERR_HEADER);   /* version 2 it is not */
    CHECK(parse_mutated(image, size, 8, 0x03, 1, NULL) == FRB_ERR_HEADER);
    CHECK(parse_mutated(image, size, 8, 0x02, 0, NULL) == FRB_ERR_HEADER);   /* nor version 3 */
    CHECK(parse_mutated(image, size, 8, 0x02, 1, NULL) == FRB_ERR_HEADER);
    CHECK(parse_mutated(image, size, 9, 0x01, 1, NULL) == FRB_ERR_VERSION);  /* version 257 */
    CHECK(parse_mutated(image, size, 24, 0x01, 0, NULL) == FRB_ERR_HEADER);  /* header checksum */
    CHECK(parse_mutated(image, size, 62, 0x40, 0, NULL) == FRB_ERR_HEADER);
    CHECK(parse_mutated(image, size, 10, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* header size 65 */
    CHECK(parse_mutated(image, size, 12, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* 9 bits, 256 entries */
    CHECK(parse_mutated(image, size, 13, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* capacity 17 */
    CHECK(parse_mutated(image, size, 14, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* 8 bits per node */
    CHECK(parse_mutated(image, size, 15, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* entry size 18 */
    CHECK(parse_mutated(image, size, 17, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* entry count */
    CHECK(parse_mutated(image, size, 20, 0x02, 1, NULL) == FRB_ERR_HEADER);  /* unknown flag */
    CHECK(parse_mutated(image, size, 34, 0x01, 1, NULL) == FRB_ERR_HEADER);  /* reserved */
    CHECK(parse_mutated(image, size, 45, 0x80, 1, NULL) == FRB_ERR_HEADER);  /* reserved */
    CHECK(parse_mutated(image, size, FRB_HEADER_BYTES + 700, 0x04, 0, NULL) == FRB_ERR_CHECKSUM);
    CHECK(parse_mutated(image, size, size - 1, 0x01, 0, NULL) == FRB_ERR_CHECKSUM);
    CHECK(parse_mutated(image, size, 36, 0x01, 0, NULL) == FRB_ERR_HEADER);  /* the stored table CRC */

    CHECK(parse_mutated(image, size, 20, 0x01, 1, NULL) == FRB_ERR_SUMMARY); /* complete flag cleared */
    CHECK(parse_mutated(image, size, 24, 0x01, 1, NULL) == FRB_ERR_SUMMARY); /* witness count */
    CHECK(parse_mutated(image, size, 28, 0x01, 1, NULL) == FRB_ERR_SUMMARY); /* minimal count */
    CHECK(parse_mutated(image, size, 32, 0x01, 1, NULL) == FRB_ERR_SUMMARY); /* largest minimum */
    CHECK(parse_mutated(image, size, 33, 0x01, 1, NULL) == FRB_ERR_SUMMARY);
    {
        /* Entry 37: status MINIMAL -> WITNESS, without telling the header. */
        size_t meta = FRB_HEADER_BYTES + 37 * FRB_ENTRY_BYTES + FRB_GRAPH_BYTES;
        uint8_t *copy = malloc(size);
        unsigned nodes = image[meta] & 31u;
        CHECK(parse_mutated(image, size, meta, (FRB_MINIMAL ^ FRB_WITNESS) << 5, 1, NULL) == FRB_ERR_SUMMARY);
        /* Entry 37: an unknown status. */
        CHECK(parse_mutated(image, size, meta, 0x80, 1, &bad) == FRB_ERR_ENTRY && bad == 37);
        /* Entry 37: one node fewer than the graph has: a link now points
         * nowhere, or the last node sits in what should be padding. */
        memcpy(copy, image, size);
        copy[meta] = (uint8_t)((FRB_MINIMAL << 5) | (nodes - 1u));
        fix_crcs(copy, size);
        bad = 99999;
        CHECK(nodes >= 2 && frb_image_parse(copy, size, NULL, NULL, &bad) == FRB_ERR_ENTRY && bad == 37);
        /* Entry 37: one node more, an unreachable one.  That is a valid REV
         * for the string, so the file is well-formed; what is false is the
         * word "minimum", and no file can show that by itself.  Recomputing
         * does. */
        memcpy(copy, image, size);
        copy[meta] = (uint8_t)((FRB_MINIMAL << 5) | (nodes + 1u));
        if (nodes + 1u > image[32]) copy[32] = copy[33] = (uint8_t)(nodes + 1u);
        fix_crcs(copy, size);
        CHECK(frb_image_parse(copy, size, &back, NULL, NULL) == FRB_OK);
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
        {
            FrbRecertify rc;
            CHECK(frb_recertify(&back, rev_level_solve_ex, 0, 0, 0, &rc) == FRB_ERR_EVIDENCE);
            CHECK(rc.disagree == 1 && rc.first_bad == 37 && rc.agree == 255);
        }
#endif
        frb_table_destroy(&back);
        free(copy);
        /* Entry 3: nonzero bits in the last, unused node slot. */
        bad = 99999;
        CHECK(parse_mutated(image, size, FRB_HEADER_BYTES + 3 * FRB_ENTRY_BYTES + 17, 0x80, 1, &bad) ==
                  FRB_ERR_ENTRY && bad == 3);
    }
    { /* Two entries exchanged: each is a fine graph in the wrong place. */
        uint8_t *copy = malloc(size), tmp[FRB_ENTRY_BYTES];
        uint8_t *a = copy + FRB_HEADER_BYTES + 10 * FRB_ENTRY_BYTES;
        uint8_t *b = copy + FRB_HEADER_BYTES + 77 * FRB_ENTRY_BYTES;
        memcpy(copy, image, size);
        memcpy(tmp, a, FRB_ENTRY_BYTES);
        memcpy(a, b, FRB_ENTRY_BYTES);
        memcpy(b, tmp, FRB_ENTRY_BYTES);
        fix_crcs(copy, size);
        bad = 99999;
        CHECK(frb_image_parse(copy, size, NULL, NULL, &bad) == FRB_ERR_OUTPUT && bad == 10);
        free(copy);
    }
    free(image);

    /* A partial table: the complete flag follows the entries, not the caller. */
    frb_ring_witness(9, 8, &ring);
    CHECK(frb_table_put(&t, 9, FRB_WITNESS, &ring) == FRB_OK);
    CHECK(frb_table_put(&t, 10, FRB_EMPTY, NULL) == FRB_OK);
    CHECK(frb_table_put(&t, 11, FRB_UNRESOLVED, NULL) == FRB_OK);
    CHECK(frb_table_put(&t, 12, FRB_UNSUPPORTED, NULL) == FRB_OK);
    CHECK(frb_table_validate(&t, &sum, NULL) == FRB_OK && !sum.complete);
    CHECK(sum.by_status[FRB_MINIMAL] == 252 && sum.by_status[FRB_WITNESS] == 1 &&
          sum.by_status[FRB_EMPTY] == 1 && sum.by_status[FRB_UNRESOLVED] == 1 &&
          sum.by_status[FRB_UNSUPPORTED] == 1 && sum.max_witness_nodes == 8 &&
          sum.witness_by_nodes[8] == 1);
    CHECK(frb_image_build(&t, &image, &size) == FRB_OK);
    CHECK(frb_image_parse(image, size, NULL, &info, NULL) == FRB_OK);
    CHECK(!info.complete && info.witnesses == 253 && info.minimal == 252 && info.max_witness_nodes == 8);
    /* Nobody can label it complete: the flag alone ... */
    CHECK(parse_mutated(image, size, 20, 0x01, 1, NULL) == FRB_ERR_SUMMARY);
    /* ... or the uncertified ring relabeled as a minimum with the counts adjusted. */
    {
        uint8_t *copy = malloc(size);
        memcpy(copy, image, size);
        copy[FRB_HEADER_BYTES + 9 * FRB_ENTRY_BYTES + FRB_GRAPH_BYTES] =
            (uint8_t)((FRB_MINIMAL << 5) | 8u);
        copy[28] = 253; /* minimal count */
        copy[32] = 8;   /* largest minimum */
        fix_crcs(copy, size);
        /* Structurally this is a valid partial file ... */
        CHECK(frb_image_parse(copy, size, &back, NULL, NULL) == FRB_OK);
        /* ... but its complement entry certifies fewer nodes, which cannot be. */
        bad = 99999;
        CHECK(frb_table_check_symmetry(&back, &bad) == FRB_ERR_EVIDENCE && (bad == 9 || bad == (9 ^ 255)));
        frb_table_destroy(&back);
        free(copy);
    }
    free(image);
    frb_table_destroy(&t);
}

struct EnumCtx {
    uint8_t *min; /* per string: the first node count at which it was emitted */
    int n, bits, mode;
    uint32_t mask;
    int bad;
};

static int enum_found(void *vctx, uint32_t index, const RevGraph *g) {
    struct EnumCtx *c = vctx;
    if (g->node_count != c->n || rev_graph_validate(g) != REV_OK || frb_graph_output(g, c->bits) != index)
        c->bad++;
    if (!c->min[index]) c->min[index] = (uint8_t)c->n;
    if (c->mode) { /* white root only: the complement graph covers the complement string */
        RevGraph comp;
        if (g->nodes[0].color != REV_WHITE) c->bad++;
        frb_graph_complement(g, &comp);
        if (frb_graph_output(&comp, c->bits) != (index ^ c->mask)) c->bad++;
        if (!c->min[index ^ c->mask]) c->min[index ^ c->mask] = (uint8_t)c->n;
    }
    return 0;
}

static int enum_abort(void *ctx, uint32_t index, const RevGraph *g) {
    (void)ctx;
    (void)index;
    (void)g;
    return 1;
}

static void test_enumerate(void) {
    /* Strings emitted by graphs of at most n nodes: the figures of
     * benchmarks/FIXED_RUN_16.md (16 bits) and of tests/test_oracle.c (10). */
    static const uint32_t expect16[5] = {0, 2, 8, 72, 1034};
    static const uint32_t expect10[5] = {0, 2, 8, 72, 634};
    for (int mode = 0; mode < 2; mode++) { /* 0: every graph.  1: white root + complement */
        for (int bits = 10; bits <= 16; bits += 6) {
            uint8_t *min_nodes = calloc((size_t)1 << bits, 1);
            uint32_t mask = (1u << bits) - 1u, cumulative = 0;
            for (int n = 1; n <= 4; n++) {
                uint64_t graphs = 0, expect_graphs = 1ull << n;
                for (int i = 0; i < 2 * n; i++) expect_graphs *= (uint64_t)n;
                for (int p = 0; p < frb_enum_part_count(n); p++) {
                    struct EnumCtx ctx = {min_nodes, n, bits, mode, mask, 0};
                    FrbEnumPart part = {n, bits, p, mode};
                    uint64_t run = 0;
                    CHECK(frb_enumerate_part(&part, enum_found, &ctx, NULL, &run) == FRB_OK);
                    CHECK(run == frb_enum_part_graphs(n, mode));
                    CHECK(ctx.bad == 0);
                    graphs += run;
                }
                CHECK(graphs == (mode ? expect_graphs / 2 : expect_graphs));
                cumulative = 0;
                for (uint32_t i = 0; i <= mask; i++) cumulative += min_nodes[i] != 0;
                CHECK_MSG(cumulative == (bits == 16 ? expect16[n] : expect10[n]),
                          "mode %d bits %d n %d: %u targets", mode, bits, n, (unsigned)cumulative);
            }
            if (bits == 10) { /* against the solver, target by target */
                char target[FRB_MAX_BITS + 1];
                int wrong = 0;
                for (uint32_t i = 0; i <= mask; i++) {
                    RevMinimalInfo info;
                    RevGraph g;
                    frb_target_format(i, bits, target);
                    RevMinimalStatus st = rev_minimal_solve(target, (size_t)bits, 4, NULL, &g, &info);
                    if (min_nodes[i]) wrong += !(st == REV_MIN_MINIMAL && info.node_count == min_nodes[i]);
                    else wrong += !(st == REV_MIN_BUDGET_EXHAUSTED && info.lower_bound == 5);
                }
                CHECK_MSG(wrong == 0, "enumeration and MinimalSolver disagree on %d targets", wrong);
            }
            free(min_nodes);
        }
    }
    {
        FrbEnumPart part = {3, 10, 9, 0};
        volatile sig_atomic_t stop = 1;
        uint64_t run = 0;
        CHECK(frb_enumerate_part(&part, enum_abort, NULL, NULL, &run) == FRB_ERR_ARG); /* parts are 0..8 */
        part.part = 8;
        CHECK(frb_enumerate_part(&part, enum_abort, NULL, &stop, &run) == FRB_ERR_STOPPED && run == 0);
        CHECK(frb_enumerate_part(&part, enum_abort, NULL, NULL, &run) == FRB_ERR_STOPPED && run == 1);
        part.nodes = 0;
        CHECK(frb_enumerate_part(&part, enum_abort, NULL, NULL, &run) == FRB_ERR_ARG);
        CHECK(frb_enum_part_graphs(5, 0) == 12500000ull && frb_enum_part_graphs(5, 1) == 6250000ull);
        CHECK(frb_enum_part_graphs(16, 0) == 0); /* overflows 64 bits: reported, not wrapped */
    }
    printf("enumeration: 1..4 nodes agree with the MinimalSolver on every 10-bit string\n");
}

/* ---- construction ------------------------------------------------------- */

#ifdef CARLOBOMB_HAVE_BOMB_BUILD

static char base_dir[1024];
static char last_err[FRB_ERR_TEXT];

static const char *in_base(const char *name) {
    static char bufs[16][1200];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 16;
    snprintf(buf, sizeof bufs[0], "%s/%s", base_dir, name);
    return buf;
}

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

static long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static uint8_t *slurp(const char *path, size_t *size) {
    long n = file_size(path);
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    *size = 0;
    if (!f || n < 0) return NULL;
    buf = malloc((size_t)n + 1);
    *size = fread(buf, 1, (size_t)n, f);
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

static void spoil(const char *path, long offset, unsigned xor_mask) {
    FILE *f = fopen(path, "r+b");
    int c;
    CHECK(f != NULL);
    if (!f) return;
    fseek(f, offset, SEEK_SET);
    c = fgetc(f);
    fseek(f, offset, SEEK_SET);
    fputc((c ^ (int)xor_mask) & 0xFF, f);
    fclose(f);
}

static void config(FrbConfig *cfg, int bits, int enumerate_max, int symmetry, int workers) {
    frb_config_default(cfg, bits);
    cfg->enumerate_max = enumerate_max;
    cfg->symmetry = symmetry;
    cfg->workers = workers;
}

static FrbRunOptions quiet_options(void) {
    FrbRunOptions opt;
    frb_run_options_default(&opt);
    opt.progress_seconds = 0;
    return opt;
}

static FrbError start(const char *dir, const FrbConfig *cfg) {
    remove_tree(dir);
    last_err[0] = '\0';
    return frb_work_create(dir, cfg, last_err, sizeof last_err);
}

static FrbError run(const char *dir, const FrbConfig *cfg, const FrbRunOptions *opt, FrbRunResult *res) {
    last_err[0] = '\0';
    return frb_work_run(dir, cfg, opt, res, last_err, sizeof last_err);
}

static FrbState *load(const char *dir, const FrbConfig *cfg) {
    FrbState *s = NULL;
    last_err[0] = '\0';
    FrbError e = frb_state_load(dir, cfg, &s, last_err, sizeof last_err);
    CHECK_MSG(e == FRB_OK, "%s: %s (%s)", dir, frb_error_str(e), last_err);
    return s;
}

/* Every target certified, with the node count the reference says. */
static void check_complete(const char *dir, const FrbConfig *cfg) {
    FrbState *s = load(dir, cfg);
    int wrong = 0;
    if (!s) return;
    CHECK(s->conflicts == 0 && s->by_status[FRB_MINIMAL] == s->count);
    for (uint32_t i = 0; i < s->count; i++)
        wrong += !(s->targets[i].status == FRB_MINIMAL && s->targets[i].nodes == ref_min[i]);
    CHECK_MSG(wrong == 0, "%s: %d targets with a wrong or missing minimum", dir, wrong);
    frb_state_free(s);
}

/* What a correct but unfinished construction may and may not say. */
static void check_sound(const char *dir, const FrbConfig *cfg) {
    FrbState *s = load(dir, cfg);
    int wrong = 0;
    if (!s) return;
    CHECK(s->conflicts == 0);
    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        if (ts->lower_bound > ref_min[i]) wrong++;                       /* excluded a real solution */
        if (ts->nodes && ts->nodes < ref_min[i]) wrong++;                /* impossible witness */
        if (ts->status == FRB_MINIMAL && ts->nodes != ref_min[i]) wrong++; /* false certificate */
        if (ts->status != FRB_MINIMAL && ts->nodes && ts->lower_bound == ts->nodes) wrong++;
    }
    CHECK_MSG(wrong == 0, "%s: %d unsound entries", dir, wrong);
    frb_state_free(s);
}

typedef struct {
    uint16_t *conclusive; /* per target * 17 + budget: SOLVED or PROVEN_UNSAT records */
    uint16_t part[FRB_NODE_CAPACITY + 1][256];
    uint64_t searches, limits, duplicates;
} Tally;

static int tally_record(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Tally *t = ctx;
    (void)seg;
    if (!rec) return 0;
    if (rec->type == FRB_REC_SEARCH) {
        t->searches++;
        if (rec->result == REV_SOLVE_RESOURCE_LIMIT) t->limits++;
        else if (++t->conclusive[rec->target * 17u + rec->budget] > 1) t->duplicates++;
    } else if (rec->type == FRB_REC_ENUM_PART) {
        if (++t->part[rec->budget][rec->aux] > 1) t->duplicates++;
    }
    return 0;
}

/* No search that reached a conclusion, and no enumeration part, was done twice. */
static Tally tally(const char *dir, const FrbConfig *cfg) {
    Tally t;
    memset(&t, 0, sizeof t);
    t.conclusive = calloc(((size_t)1 << cfg->bits) * 17u, sizeof *t.conclusive);
    CHECK(frb_journal_scan(dir, cfg, tally_record, &t, NULL, last_err, sizeof last_err) == FRB_OK);
    free(t.conclusive);
    t.conclusive = NULL;
    return t;
}

static void finalize_ok(const char *dir, const char *file) {
    FrbSummary sum;
    FrbFileInfo info;
    FrbTable t;
    remove(file);
    FrbError e = frb_work_finalize(dir, file, 0, &sum, last_err, sizeof last_err);
    CHECK_MSG(e == FRB_OK && sum.complete, "%s: %s (%s)", dir, frb_error_str(e), last_err);
    CHECK(frb_file_read(file, &t, &info, NULL) == FRB_OK && info.complete);
    if (t.entries) {
        CHECK(frb_table_check_symmetry(&t, NULL) == FRB_OK);
        frb_table_destroy(&t);
    }
}

/* Four ways to build the same table. */
static void test_build_variants(void) {
    static const struct { int enumerate_max, symmetry, workers; } v[] = {
        {3, 1, 1}, {0, 0, 1}, {4, 1, 3}, {2, 0, 2}, {3, 1, 4}, {0, 1, 1}};
    FrbRunOptions opt = quiet_options();
    char file[6][1200];
    for (size_t k = 0; k < sizeof v / sizeof v[0]; k++) {
        const char *dir = in_base("variant");
        FrbConfig cfg, back;
        FrbRunResult res;
        FrbState *s;
        FrbTable t;
        config(&cfg, 9, v[k].enumerate_max, v[k].symmetry, v[k].workers);
        CHECK(start(dir, &cfg) == FRB_OK);
        CHECK(frb_config_read(dir, &back, NULL, 0) == FRB_OK && memcmp(&back, &cfg, sizeof cfg) == 0);
        CHECK(run(dir, &cfg, &opt, &res) == FRB_OK);
        CHECK(res.complete && !res.interrupted && !res.stopped_early);
        check_complete(dir, &cfg);
        snprintf(file[k], sizeof file[k], "%s/variant-%d.frb", base_dir, (int)k);
        finalize_ok(dir, file[k]);
        s = load(dir, &cfg);
        if (s) {
            CHECK(s->enum_complete == v[k].enumerate_max && s->sessions == 1 && s->sessions_unfinished == 0);
            CHECK(frb_file_read(file[k], &t, NULL, NULL) == FRB_OK);
            CHECK(frb_verify_evidence(&t, s, NULL, last_err, sizeof last_err) == FRB_OK);
            frb_table_destroy(&t);
            frb_state_free(s);
        }
        /* Nothing left to do: a second session runs no job. */
        CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete && res.jobs == 0);
        Tally t2 = tally(dir, &cfg);
        CHECK(t2.duplicates == 0 && t2.limits == 0);
    }
    /* Same settings, 1 worker or 4: the same file, byte for byte. */
    CHECK(same_file(file[0], file[4]));
    /* Other settings: other witnesses are allowed, other minima are not
     * (check_complete above compared every one of them with the reference). */
    printf("construction: 6 variants of the 9-bit table agree; 1 and 4 workers give identical files\n");
}

/* Stop after a few jobs, again and again, and end with the same table. */
static void test_resume_job_caps(void) {
    const char *dir = in_base("caps"), *ref = in_base("caps-ref");
    const char *file = in_base("caps.frb"), *ref_file = in_base("caps-ref.frb"), *part = in_base("caps-part.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbSummary sum;
    FrbFileInfo info;
    FrbRecertify rc;
    FrbState *s;
    FrbTable t;
    int sessions = 1;

    config(&cfg, 9, 3, 1, 1);
    CHECK(start(ref, &cfg) == FRB_OK && run(ref, &cfg, &opt, &res) == FRB_OK && res.complete);
    finalize_ok(ref, ref_file);

    /* A node count is enumerated when all of its parts are, not before:
     * stopped after 3 of the 9 parts of three-node graphs, nothing may yet
     * be said to need four nodes. */
    {
        const char *half = in_base("caps-half");
        int max_lower = 0;
        CHECK(start(half, &cfg) == FRB_OK);
        opt.limit_jobs = 8;
        CHECK(run(half, &cfg, &opt, &res) == FRB_OK && !res.complete && res.jobs == 8);
        check_sound(half, &cfg);
        s = load(half, &cfg);
        if (s) {
            for (uint32_t i = 0; i < s->count; i++)
                if (s->targets[i].lower_bound > max_lower) max_lower = s->targets[i].lower_bound;
            CHECK(s->enum_complete == 2 && s->parts_done[3] == 3 && max_lower == 3);
            CHECK(s->by_status[FRB_MINIMAL] > 8 && s->by_status[FRB_MINIMAL] < ref_count_upto(9, 3));
            frb_state_free(s);
        }
        /* The remaining six parts, then the searches. */
        opt.limit_jobs = 0;
        CHECK(run(half, &cfg, &opt, &res) == FRB_OK && res.complete);
        check_complete(half, &cfg);
        Tally th = tally(half, &cfg);
        CHECK(th.duplicates == 0);
    }

    CHECK(start(dir, &cfg) == FRB_OK);
    opt.limit_jobs = 5; /* the 1 + 4 parts of one- and two-node graphs */
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK);
    CHECK(!res.complete && res.stopped_early && !res.interrupted && res.jobs == 5);
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->enum_complete == 2 && s->by_status[FRB_MINIMAL] == 8 && s->by_status[FRB_WITNESS] == 504);
        CHECK(ref_count_upto(9, 2) == 8);
        frb_state_free(s);
    }

    /* An incomplete table cannot be finalized as complete ... */
    remove(file);
    CHECK(frb_work_finalize(dir, file, 0, &sum, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);
    CHECK(file_size(file) == -1); /* and nothing is written */
    /* ... only as what it is. */
    CHECK(frb_work_finalize(dir, part, 1, &sum, last_err, sizeof last_err) == FRB_OK && !sum.complete);
    CHECK(frb_file_read(part, &t, &info, NULL) == FRB_OK);
    CHECK(!info.complete && info.minimal == 8 && info.witnesses == 512 && info.max_witness_nodes == 9);
    CHECK(frb_table_check_symmetry(&t, NULL) == FRB_OK);
    /* Recomputing confirms the 8 certificates and shows the ring is not minimal elsewhere. */
    CHECK(frb_recertify(&t, rev_level_solve_ex, 0, 0, 0, &rc) == FRB_OK);
    CHECK(rc.checked == 512 && rc.agree == 8 && rc.disagree == 0 && rc.tightened == 504);
    frb_table_destroy(&t);

    opt.limit_jobs = 40;
    for (; sessions < 100; sessions++) {
        CHECK(run(dir, &cfg, &opt, &res) == FRB_OK);
        check_sound(dir, &cfg);
        if (res.complete) break;
        CHECK(res.stopped_early && res.jobs == 40);
    }
    sessions++;
    CHECK(sessions > 5 && sessions < 100);
    check_complete(dir, &cfg);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file));
    Tally t2 = tally(dir, &cfg);
    CHECK(t2.duplicates == 0);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->sessions == (uint32_t)sessions && s->journal.segments == (uint32_t)sessions);
        frb_state_free(s);
    }
    printf("resume: 9-bit table finished in %d capped sessions, no job done twice, same file\n", sessions);
}

/* Step limits: UNKNOWN stays UNKNOWN, lower bounds survive, retries need a larger limit. */
static void test_step_limits(void) {
    const char *dir = in_base("limits"), *ref = in_base("limits-ref"), *sched = in_base("limits-sched");
    const char *file = in_base("limits.frb"), *ref_file = in_base("limits-ref.frb"), *sched_file = in_base("limits-sched.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbSummary sum;
    FrbState *s;
    Tally t1, t2;
    uint32_t limited = 0, with_bound = 0;

    compute_reference(10);
    config(&cfg, 10, 0, 1, 2);
    CHECK(start(ref, &cfg) == FRB_OK && run(ref, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(ref, &cfg);
    finalize_ok(ref, ref_file);

    cfg.max_steps[0] = 60; /* enough to exclude one node, not two */
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK);
    CHECK(!res.complete && !res.stopped_early && !res.interrupted);
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        for (uint32_t i = 0; i < s->count; i++) {
            const FrbTargetState *ts = &s->targets[i];
            if (ts->status != FRB_MINIMAL) {
                limited += ts->limit_budget == ts->lower_bound && ts->limit_kind == REV_LIMIT_STEPS &&
                           ts->limit_max_steps == 60;
                with_bound += ts->lower_bound > 1;
                CHECK(ts->status == FRB_WITNESS && ts->nodes == 10 && ts->source == FRB_SRC_RING);
            }
        }
        CHECK(s->by_status[FRB_MINIMAL] >= 2 && s->by_status[FRB_MINIMAL] < 100);
        CHECK(limited == s->count - s->by_status[FRB_MINIMAL]); /* every open target is waiting on the limit */
        CHECK(with_bound > 500);                                /* and keeps what was proven */
        frb_state_free(s);
    }
    CHECK(frb_work_finalize(dir, file, 0, &sum, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);

    /* The same limit again: nothing is searched twice. */
    t1 = tally(dir, &cfg);
    CHECK(t1.limits > 0 && t1.duplicates == 0);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete && res.jobs == 0 && res.searches == 0);
    t2 = tally(dir, &cfg);
    CHECK(t2.searches == t1.searches);

    /* A larger limit: the stopped searches are retried, the proven bounds are not. */
    cfg.max_steps[0] = 3000;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete && res.searches > 0);
    check_sound(dir, &cfg);
    t2 = tally(dir, &cfg);
    CHECK(t2.duplicates == 0 && t2.searches > t1.searches);

    cfg.max_steps[0] = 0;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    t2 = tally(dir, &cfg);
    CHECK(t2.duplicates == 0);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file));

    /* The same escalation as one schedule. */
    CHECK(frb_parse_step_limits("60,3000,0", &cfg) && cfg.passes == 3 && cfg.max_steps[1] == 3000);
    CHECK(!frb_parse_step_limits("60,,0", &cfg) && !frb_parse_step_limits("", &cfg) &&
          !frb_parse_step_limits("5,", &cfg) && !frb_parse_step_limits("-1", &cfg) &&
          !frb_parse_step_limits("1,2,3,4,5,6,7,8,9", &cfg));
    CHECK(frb_parse_step_limits("60,3000,0", &cfg));
    CHECK(start(sched, &cfg) == FRB_OK && run(sched, &cfg, &opt, &res) == FRB_OK && res.complete);
    CHECK(res.rounds == 3);
    t2 = tally(sched, &cfg);
    CHECK(t2.duplicates == 0 && t2.limits > 0);
    finalize_ok(sched, sched_file);
    CHECK(same_file(sched_file, ref_file));
    compute_reference(9);
    printf("step limits: %u of 1024 targets waited on a 60-step limit, none was called unsolvable\n",
           (unsigned)limited);
}

static const char *segment_file(const char *dir, unsigned number) {
    static char buf[1400];
    snprintf(buf, sizeof buf, "%s/journal/%06u.frj", dir, number);
    return buf;
}

static void append_record(const char *dir, const FrbConfig *cfg, uint32_t number, const FrbRecord *rec) {
    FrbSegment *seg = NULL;
    CHECK(frb_segment_create(dir, cfg, number, 999, 0, &seg) == FRB_OK);
    if (!seg) return;
    CHECK(frb_segment_append(seg, rec) == FRB_OK);
    CHECK(frb_segment_close(seg) == FRB_OK);
}

/* Interrupted and damaged journals. */
static void test_journal_damage(void) {
    const char *dir = in_base("damage"), *ref_file = in_base("caps-ref.frb"), *file = in_base("damage.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg, other;
    FrbRunResult res;
    FrbState *s = NULL;
    long size;

    /* (a) A writer killed in the middle of a record. */
    config(&cfg, 9, 3, 1, 1);
    opt.limit_jobs = 60;
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete);
    size = file_size(segment_file(dir, 1));
    CHECK(size > 64 * 10 && (size - 64) % 64 == 0);
    CHECK(truncate(segment_file(dir, 1), size - 64 - 30) == 0); /* SESSION_END gone, a search torn */
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->journal.torn_bytes == 34 && s->journal.torn_segments == 1);
        CHECK(s->sessions == 1 && s->sessions_unfinished == 1);
        frb_state_free(s);
    }
    check_sound(dir, &cfg);
    /* (b) A file extended but never written: zeros. */
    {
        FILE *f = fopen(segment_file(dir, 1), "ab");
        static const char zeros[100];
        CHECK(f && fwrite(zeros, 1, sizeof zeros, f) == sizeof zeros && fclose(f) == 0);
    }
    /* (e) A segment created and never written. */
    {
        FILE *f = fopen(segment_file(dir, 700), "wb");
        CHECK(f && fclose(f) == 0);
    }
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->journal.torn_bytes == 134 && s->journal.torn_segments == 2 && s->journal.next_segment == 701);
        frb_state_free(s);
    }
    opt.limit_jobs = 0;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file)); /* the lost record was redone, nothing else changed */
    Tally t = tally(dir, &cfg);
    CHECK(t.duplicates == 0);

    /* (c) Damage in the middle is not an interrupted writer and is not ignored. */
    spoil(segment_file(dir, 1), 64 + 5 * 64 + 9, 0x10);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL && s == NULL);
    CHECK(strstr(last_err, "damaged record") != NULL);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_ERR_JOURNAL);
    CHECK(frb_work_finalize(dir, file, 1, NULL, last_err, sizeof last_err) == FRB_ERR_JOURNAL);
    spoil(segment_file(dir, 1), 64 + 5 * 64 + 9, 0x10); /* undo */
    s = load(dir, &cfg);
    frb_state_free(s);

    /* (d) A segment written with other settings. */
    other = cfg;
    other.bits = 8;
    {
        FrbSegment *seg = NULL;
        CHECK(frb_segment_create(dir, &other, 800, 9, 0, &seg) == FRB_OK && frb_segment_close(seg) == FRB_OK);
    }
    s = NULL;
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE && s == NULL);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_ERR_INCOMPATIBLE);
    CHECK(unlink(segment_file(dir, 800)) == 0);
    other = cfg;
    other.symmetry = 0;
    {
        FrbSegment *seg = NULL;
        CHECK(frb_segment_create(dir, &other, 800, 9, 0, &seg) == FRB_OK && frb_segment_close(seg) == FRB_OK);
    }
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    CHECK(unlink(segment_file(dir, 800)) == 0);
    /* A segment header that is damaged while records follow it. */
    spoil(segment_file(dir, 1), 20, 0x01);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL);
    spoil(segment_file(dir, 1), 20, 0x01);
    /* A segment may not be created twice. */
    {
        FrbSegment *seg = NULL;
        CHECK(frb_segment_create(dir, &cfg, 1, 9, 0, &seg) == FRB_ERR_IO && seg == NULL);
    }
}

/* Records that are well-formed and wrong. */
static void test_forged_evidence(void) {
    const char *dir = in_base("forged"), *file = in_base("forged.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbState *s = NULL;
    FrbRecord rec;
    RevGraph g;
    uint32_t victim = 0;

    config(&cfg, 9, 3, 1, 1);
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    for (uint32_t i = 0; i < 256; i++)
        if (ref_min[i] == 5) victim = i;
    CHECK(victim != 0);

    /* (a) "No REV of at most 5 nodes emits it", about a string that has one. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_PROVEN_UNSAT;
    rec.budget = 5;
    rec.target = victim;
    rec.steps = 1;
    append_record(dir, &cfg, 500, &rec);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->conflicts == 2); /* the string and its complement */
        CHECK(s->targets[victim].conflict && s->targets[victim].status != FRB_MINIMAL);
        CHECK(s->targets[victim ^ 511u].conflict && s->by_status[FRB_MINIMAL] == 510);
        frb_state_free(s);
    }
    CHECK(run(dir, &cfg, &opt, &res) == FRB_ERR_EVIDENCE && !res.complete);
    CHECK(strstr(last_err, "contradictory") != NULL);
    remove(file);
    CHECK(frb_work_finalize(dir, file, 0, NULL, last_err, sizeof last_err) == FRB_ERR_EVIDENCE);
    CHECK(frb_work_finalize(dir, file, 1, NULL, last_err, sizeof last_err) == FRB_ERR_EVIDENCE);
    CHECK(file_size(file) == -1);
    CHECK(unlink(segment_file(dir, 500)) == 0);

    /* A search that claims a smaller graph than a complete enumeration found. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_PROVEN_UNSAT;
    rec.budget = 2;
    for (uint32_t i = 0; i < 256; i++)
        if (ref_min[i] == 2) rec.target = i;
    append_record(dir, &cfg, 500, &rec);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->conflicts == 2);
        frb_state_free(s);
    }
    CHECK(unlink(segment_file(dir, 500)) == 0);

    /* (b) A witness that does not emit the string it is recorded for. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_SOLVED;
    rec.budget = 9;
    rec.target = victim;
    frb_ring_witness(victim ^ 1u, 9, &g);
    rec.nodes = 9;
    CHECK(frb_graph_pack(&g, rec.graph) == FRB_OK);
    append_record(dir, &cfg, 500, &rec);
    s = NULL;
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL && s == NULL);
    CHECK(strstr(last_err, "does not emit") != NULL);
    CHECK(unlink(segment_file(dir, 500)) == 0);
    /* The same record with the right graph is merely a larger witness: harmless. */
    frb_ring_witness(victim, 9, &g);
    CHECK(frb_graph_pack(&g, rec.graph) == FRB_OK);
    append_record(dir, &cfg, 500, &rec);
    check_complete(dir, &cfg);
    CHECK(unlink(segment_file(dir, 500)) == 0);
    /* A witness larger than its search budget. */
    rec.budget = 8;
    append_record(dir, &cfg, 500, &rec);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL);
    CHECK(unlink(segment_file(dir, 500)) == 0);

    /* (c) "This part of the enumeration is complete", without its findings. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_ENUM_PART;
    rec.budget = 4;
    rec.aux = 3;
    rec.target = 7; /* claims 7 witness records that are not there */
    rec.steps = frb_enum_part_graphs(4, 1);
    append_record(dir, &cfg, 500, &rec);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL);
    CHECK(strstr(last_err, "witness records") != NULL);
    CHECK(unlink(segment_file(dir, 500)) == 0);
    rec.target = 0;
    rec.steps = 12345; /* or that ran some other number of graphs */
    append_record(dir, &cfg, 500, &rec);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL);
    CHECK(unlink(segment_file(dir, 500)) == 0);
    check_complete(dir, &cfg);

    /* (d) In an empty construction: a limit excludes nothing, an UNSAT
     * excludes its budget and everything below, and neither makes a witness. */
    config(&cfg, 6, 0, 0, 1);
    cfg.fallback_ring = 0;
    CHECK(start(dir, &cfg) == FRB_OK);
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_RESOURCE_LIMIT;
    rec.limit = REV_LIMIT_STEPS;
    rec.budget = 3;
    rec.target = 21;
    rec.max_steps = 1000;
    rec.steps = 1000;
    append_record(dir, &cfg, 1, &rec);
    rec.result = REV_SOLVE_PROVEN_UNSAT;
    rec.limit = 0;
    rec.budget = 2;
    rec.target = 22;
    append_record(dir, &cfg, 2, &rec);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->targets[21].lower_bound == 1 && s->targets[21].status == FRB_UNRESOLVED &&
              s->targets[21].limit_budget == 3 && s->targets[21].nodes == 0);
        CHECK(s->targets[22].lower_bound == 3 && s->targets[22].status == FRB_UNRESOLVED);
        CHECK(s->targets[22 ^ 63].lower_bound == 1 && s->targets[22 ^ 63].status == FRB_EMPTY); /* symmetry off */
        CHECK(s->by_status[FRB_EMPTY] == 62 && s->by_status[FRB_UNRESOLVED] == 2 && s->conflicts == 0);
        frb_state_free(s);
    }
    /* Without symmetry there is no complement to give a wrong graph away:
     * the graph itself must be run. */
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SEARCH;
    rec.result = REV_SOLVE_SOLVED;
    rec.budget = 6;
    rec.target = 23;
    rec.nodes = 6;
    frb_ring_witness(22, 6, &g);
    CHECK(frb_graph_pack(&g, rec.graph) == FRB_OK);
    append_record(dir, &cfg, 3, &rec);
    s = NULL;
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL && s == NULL);
    CHECK(strstr(last_err, "does not emit the target it is recorded for") != NULL);
    CHECK(unlink(segment_file(dir, 3)) == 0);
    rec.type = FRB_REC_ENUM_WITNESS; /* the same from "enumeration" */
    rec.result = 0;
    rec.aux = 0;
    append_record(dir, &cfg, 3, &rec);
    CHECK(frb_state_load(dir, &cfg, &s, last_err, sizeof last_err) == FRB_ERR_JOURNAL && s == NULL);
    CHECK(unlink(segment_file(dir, 3)) == 0);

    cfg.fallback_ring = 1; /* the same journal, with the ring standing in */
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->by_status[FRB_WITNESS] == 64 && s->targets[22].nodes == 6 && s->targets[22].lower_bound == 3);
        frb_state_free(s);
    }
    printf("evidence: forged UNSAT, wrong witness and unbacked enumeration records are all refused\n");
}

/* Engines that misbehave. */
static RevSolveResult engine_liar(const char *target, size_t len, int max_nodes, const RevLevelOptions *opt,
                                  RevGraph *result, RevLevelStats *stats) {
    (void)target; (void)len; (void)max_nodes; (void)opt;
    if (result) result->node_count = 0;
    if (stats) { stats->steps = 1; stats->peak_stack = 1; stats->limit = REV_LIMIT_NONE; }
    return REV_SOLVE_PROVEN_UNSAT;
}

static RevSolveResult engine_limit(const char *target, size_t len, int max_nodes, const RevLevelOptions *opt,
                                   RevGraph *result, RevLevelStats *stats) {
    (void)target; (void)len; (void)max_nodes;
    if (result) result->node_count = 0;
    if (stats) { stats->steps = opt ? opt->max_steps : 0; stats->peak_stack = 1; stats->limit = REV_LIMIT_STEPS; }
    return REV_SOLVE_RESOURCE_LIMIT;
}

static RevSolveResult engine_wrong(const char *target, size_t len, int max_nodes, const RevLevelOptions *opt,
                                   RevGraph *result, RevLevelStats *stats) {
    uint32_t index = 0;
    int bits = 0;
    (void)opt;
    frb_target_parse(target, &bits, &index);
    if (stats) { stats->steps = 1; stats->peak_stack = 1; stats->limit = REV_LIMIT_NONE; }
    if (max_nodes < (int)len) return REV_SOLVE_PROVEN_UNSAT;
    frb_ring_witness(index ^ 1u, bits, result); /* a fine REV for some other string */
    return REV_SOLVE_SOLVED;
}

static int stop_after;
static RevSolveResult engine_stopper(const char *target, size_t len, int max_nodes, const RevLevelOptions *opt,
                                     RevGraph *result, RevLevelStats *stats) {
    if (--stop_after == 0) frb_request_stop();
    return rev_level_solve_ex(target, len, max_nodes, opt, result, stats);
}

static long slow_ns = 500000;
static RevSolveResult engine_slow(const char *target, size_t len, int max_nodes, const RevLevelOptions *opt,
                                  RevGraph *result, RevLevelStats *stats) {
    struct timespec ts = {0, slow_ns};
    nanosleep(&ts, NULL);
    return rev_level_solve_ex(target, len, max_nodes, opt, result, stats);
}

static void test_bad_engines(void) {
    const char *dir = in_base("engines"), *file = in_base("engines.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbFileInfo info;
    FrbRecertify rc;
    FrbState *s;
    FrbTable t;

    /* "Unsolvable at every budget."  Without a fallback there is then no
     * witness, and the table says UNSUPPORTED instead of inventing one. */
    config(&cfg, 5, 0, 1, 1);
    cfg.fallback_ring = 0;
    opt.solve = engine_liar;
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->by_status[FRB_UNSUPPORTED] == 32 && s->targets[7].lower_bound == 17 && s->conflicts == 0);
        frb_state_free(s);
    }
    remove(file);
    CHECK(frb_work_finalize(dir, file, 0, NULL, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);
    CHECK(frb_work_finalize(dir, file, 1, NULL, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_file_read(file, &t, &info, NULL) == FRB_OK && !info.complete && info.witnesses == 0);
    frb_table_destroy(&t);
    /* With the ring, the same lies certify the ring.  Nothing in such a
     * journal contradicts itself; recomputing with an honest engine does. */
    cfg.fallback_ring = 1;
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    CHECK(frb_work_finalize(dir, file, 0, NULL, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_file_read(file, &t, &info, NULL) == FRB_OK && info.complete && info.max_minimal_nodes == 5);
    CHECK(frb_recertify(&t, rev_level_solve_ex, 0, 0, 0, &rc) == FRB_ERR_EVIDENCE);
    CHECK(rc.checked == 32 && rc.disagree == 32 && rc.agree == 0);
    CHECK(frb_recertify(&t, rev_level_solve_ex, 0, 10, 3, &rc) == FRB_ERR_EVIDENCE && rc.checked == 10);
    frb_table_destroy(&t);
    /* Whatever enumeration covers is out of the liar's reach. */
    config(&cfg, 5, 2, 1, 1);
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->targets[0].nodes == 1 && s->targets[0].source == FRB_SRC_ENUM); /* not the liar's doing */
        frb_state_free(s);
    }

    /* "Out of steps", always.  Nothing is excluded, nothing certified. */
    config(&cfg, 5, 0, 1, 2);
    cfg.max_steps[0] = 77;
    opt.solve = engine_limit;
    for (int ring = 0; ring < 2; ring++) {
        cfg.fallback_ring = ring;
        CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK);
        CHECK(!res.complete && res.searches == 16);
        s = load(dir, &cfg);
        if (s) {
            CHECK(s->by_status[ring ? FRB_WITNESS : FRB_UNRESOLVED] == 32 && s->by_status[FRB_MINIMAL] == 0);
            CHECK(s->targets[3].lower_bound == 1 && s->targets[3].limit_budget == 1 &&
                  s->targets[3].limit_max_steps == 77 && s->budget[1].limited == 16);
            frb_state_free(s);
        }
        CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.jobs == 0); /* same limit: no retry */
        CHECK(frb_work_finalize(dir, file, 0, NULL, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);
    }
    cfg.max_steps[0] = 0; /* "no limit" is larger than 77: one retry, then it rests */
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.searches == 16 && !res.complete);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.jobs == 0);

    /* A witness for some other string is caught where the record is made. */
    for (int workers = 1; workers <= 2; workers++) {
        config(&cfg, 5, 0, 1, workers);
        cfg.fallback_ring = 0; /* so that budget 5, where it "finds" something, is searched */
        opt.solve = engine_wrong;
        CHECK(start(dir, &cfg) == FRB_OK);
        if (workers == 2) fprintf(stderr, "(two worker error messages are expected here)\n");
        CHECK(run(dir, &cfg, &opt, &res) == (workers == 1 ? FRB_ERR_EVIDENCE : FRB_ERR_IO) && !res.complete);
        s = load(dir, &cfg);
        if (s) {
            CHECK(s->by_status[FRB_MINIMAL] == 0 && s->by_status[FRB_WITNESS] == 0);
            frb_state_free(s);
        }
    }

    /* A node budget cap: what lies beyond it stays open, and is resumed. */
    config(&cfg, 9, 0, 1, 1);
    cfg.max_nodes = 3;
    cfg.fallback_ring = 0;
    opt.solve = rev_level_solve_ex;
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete);
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->by_status[FRB_MINIMAL] == ref_count_upto(9, 3) &&
              s->by_status[FRB_UNRESOLVED] == 512 - ref_count_upto(9, 3));
        CHECK(s->by_status[FRB_UNSUPPORTED] == 0); /* 4 > max-nodes is not "more than 16" */
        frb_state_free(s);
    }
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.jobs == 0);
    cfg.max_nodes = 16;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    Tally tl = tally(dir, &cfg);
    CHECK(tl.duplicates == 0);
}

static void test_settings_and_lock(void) {
    const char *dir = in_base("settings");
    char path[1400];
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg, back;
    FrbRunResult res;
    FILE *f;
    int fd;

    config(&cfg, 7, 2, 1, 2);
    strcpy(cfg.engine, "amd64-auto");
    CHECK(frb_parse_step_limits("1000,0", &cfg));
    CHECK(start(dir, &cfg) == FRB_OK);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_ERR_EXISTS);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_OK);
    CHECK(memcmp(&back, &cfg, sizeof cfg) == 0);

    /* Another revision of the search, or of the journal, is another construction. */
    snprintf(path, sizeof path, "%s/config.txt", dir);
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 1\nsearch_revision 2\nbits 7\nsymmetry 1\n"
               "enumerate_max 2\nmax_nodes 16\nfallback ring\nengine c\nworkers 1\nmax_steps 0\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    CHECK(strstr(last_err, "search revision 2") != NULL);
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 3\nsearch_revision 1\nbits 7\nsymmetry 1\n"
               "enumerate_max 2\nmax_nodes 16\nfallback ring\nengine c\nworkers 1\nmax_steps 0\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 2\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 1\nsearch_revision 1\nbits 7\nsymmetry 1\n"
               "enumerate_max 2\nmax_nodes 16\nfallback ring\nengine c\nworkers 1\nmax_steps 0\nturbo 1\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 1\nsearch_revision 1\nbits 7\nsymmetry 1\n"
               "max_nodes 16\nfallback ring\nengine c\nworkers 1\nmax_steps 0\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE); /* a setting missing */
    f = fopen(path, "w");
    fprintf(f, "carlobomb-fixed-run-work 1\njournal_version 1\nsearch_revision 1\nbits 17\nsymmetry 1\n"
               "enumerate_max 2\nmax_nodes 16\nfallback ring\nengine c\nworkers 1\nmax_steps 0\n");
    fclose(f);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
    CHECK(frb_config_write(dir, &cfg) == FRB_OK);
    CHECK(frb_config_read(in_base("no-such-dir"), &back, last_err, sizeof last_err) == FRB_ERR_IO);

    /* Impossible settings never reach the disk. */
    back = cfg; back.bits = 17;
    CHECK(frb_config_check(&back, last_err, sizeof last_err) == FRB_ERR_ARG);
    back = cfg; back.max_nodes = 17;
    CHECK(frb_config_check(&back, last_err, sizeof last_err) == FRB_ERR_ARG);
    back = cfg; back.enumerate_max = FRB_ENUMERATE_LIMIT + 1;
    CHECK(frb_config_check(&back, last_err, sizeof last_err) == FRB_ERR_ARG);
    back = cfg; back.workers = 0;
    CHECK(frb_config_check(&back, last_err, sizeof last_err) == FRB_ERR_ARG);
    CHECK(frb_work_create(in_base("never"), &back, last_err, sizeof last_err) == FRB_ERR_ARG);
    CHECK(file_size(in_base("never")) == -1);

    /* One build at a time. */
    snprintf(path, sizeof path, "%s/lock", dir);
    fd = open(path, O_CREAT | O_RDWR, 0666);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_ERR_BUSY);
    close(fd);
    opt.solve = rev_level_solve_ex;
    compute_reference(7);
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    compute_reference(9);
}

/* A sample of the open targets (the pilot), a time limit, a stop request. */
static void test_partial_sessions(void) {
    const char *dir = in_base("partial"), *file = in_base("partial.frb"), *ref_file = in_base("caps-ref.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbState *s;
    uint32_t searched = 0;
    FILE *report;
    char text[20000];
    size_t n;

    config(&cfg, 9, 3, 1, 1);
    opt.sample = 25;
    opt.seed = 12345;
    CHECK(start(dir, &cfg) == FRB_OK && run(dir, &cfg, &opt, &res) == FRB_OK);
    CHECK(!res.complete && res.stopped_early && res.jobs == 14 + 25);
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        for (uint32_t i = 0; i < s->count; i++) searched += s->targets[i].searches > 0;
        uint32_t certified = ref_count_upto(9, 3) + 50; /* each sampled string and its complement */
        char expect[160];
        CHECK(searched == 25 && s->enum_complete == 3);
        CHECK(s->by_status[FRB_MINIMAL] == certified);
        snprintf(expect, sizeof expect, "NOT COMPLETE: %u of 512 targets have no certified minimum",
                 (unsigned)(512 - certified));
        /* The status report of an unfinished construction says so. */
        report = tmpfile();
        frb_report_state(report, dir, &cfg, s);
        CHECK(frb_report_target(report, dir, &cfg, s, 3, last_err, sizeof last_err) == FRB_OK);
        rewind(report);
        n = fread(text, 1, sizeof text - 1, report);
        text[n] = '\0';
        fclose(report);
        CHECK_MSG(strstr(text, expect) != NULL, "status report lacks: %s", expect);
        CHECK(strstr(text, "is not that maximum until the table is complete") != NULL);
        CHECK(strstr(text, "exact maximum") == NULL);
        CHECK(strstr(text, "Target 000000011") != NULL);
        frb_state_free(s);
    }

    opt.sample = 0;
    opt.time_limit = 1e-9; /* already over */
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && !res.complete && res.stopped_early && res.jobs == 0);
    opt.time_limit = 0;

    opt.solve = engine_stopper; /* asks to stop from inside the 30th search */
    stop_after = 30;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.interrupted && !res.complete && !res.stopped_early);
    CHECK(res.jobs >= 10 && res.jobs <= 30);
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->sessions == 3 && s->sessions_unfinished == 0); /* a stop request ends a session properly */
        frb_state_free(s);
    }
    opt.solve = rev_level_solve_ex;
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file));
    s = load(dir, &cfg);
    if (s) {
        report = tmpfile();
        frb_report_state(report, dir, &cfg, s);
        rewind(report);
        n = fread(text, 1, sizeof text - 1, report);
        text[n] = '\0';
        fclose(report);
        CHECK(strstr(text, "COMPLETE: every target has a certified minimum") != NULL);
        CHECK(strstr(text, "exact maximum over all 9-bit strings") != NULL);
        CHECK(strstr(text, "NOT COMPLETE") == NULL);
        frb_state_free(s);
    }
}

/* Run a session in a child process and return its pid. */
static pid_t spawn_session(const char *dir, const FrbConfig *cfg, int own_group) {
    pid_t pid;
    fflush(NULL);
    pid = fork();
    if (pid == 0) {
        FrbRunOptions opt = quiet_options();
        FrbRunResult res;
        char err[FRB_ERR_TEXT];
        FrbError e;
        if (own_group) setpgid(0, 0);
        opt.solve = engine_slow;
        opt.sync_seconds = 0.01;
        e = frb_work_run(dir, cfg, &opt, &res, err, sizeof err);
        _exit(e == FRB_ERR_BUSY ? 5 : e != FRB_OK ? 2 : res.complete ? 0 : res.interrupted ? 3 : 4);
    }
    return pid;
}

static void nap(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* kill -9 at arbitrary moments, of the whole process group or of the
 * coordinator alone, then finish and compare with an undisturbed build. */
static void test_kill_and_resume(void) {
    const char *dir = in_base("kill"), *ref = in_base("kill-ref");
    const char *file = in_base("kill.frb"), *ref_file = in_base("kill-ref.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbState *s;
    int kills = 0, busy = 0, finished_early = 0;

    compute_reference(10);
    config(&cfg, 10, 3, 1, 2);
    CHECK(start(ref, &cfg) == FRB_OK && run(ref, &cfg, &opt, &res) == FRB_OK && res.complete);
    finalize_ok(ref, ref_file);

    CHECK(start(dir, &cfg) == FRB_OK);
    slow_ns = 1500000; /* about half a second of work for two workers */
    for (int round = 0; round < 12 && !finished_early; round++) {
        int group = round % 2 == 0, status = 0;
        pid_t pid = spawn_session(dir, &cfg, group);
        CHECK(pid > 0);
        if (group) setpgid(pid, pid); /* in case the child has not got there yet */
        nap(15 + 12 * round);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(group ? -pid : pid, SIGKILL);
            waitpid(pid, &status, 0);
            kills++;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 5) {
            busy++; /* workers orphaned by the previous kill still hold the lock */
        } else {
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            finished_early = 1;
        }
        /* Whatever instant the kill hit, what is on disk is sound. */
        check_sound(dir, &cfg);
    }
    CHECK(kills >= 3);
    slow_ns = 500000;
    /* Finish.  Orphaned workers notice the missing coordinator at their next
     * report and stop; until then the directory is theirs. */
    for (int attempt = 0; attempt < 400; attempt++) {
        FrbError e = run(dir, &cfg, &opt, &res);
        if (e != FRB_ERR_BUSY) {
            CHECK_MSG(e == FRB_OK && res.complete, "%s (%s)", frb_error_str(e), last_err);
            break;
        }
        busy++;
        nap(25);
    }
    check_complete(dir, &cfg);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file));
    s = load(dir, &cfg);
    if (s) {
        printf("kill -9: %d kills (%d sessions never ended, %llu torn bytes in %u segments, lock busy %d times); "
               "finished with the same file\n",
               kills, (int)s->sessions_unfinished, (unsigned long long)s->journal.torn_bytes,
               (unsigned)s->journal.torn_segments, busy);
        CHECK(s->sessions_unfinished >= 1);
        frb_state_free(s);
    }
    compute_reference(9);
}

/* SIGINT to the coordinator alone: it tells its workers, everyone finishes
 * the job in hand, the session ends properly, and resume completes. */
static void test_sigint(void) {
    const char *dir = in_base("sigint"), *file = in_base("sigint.frb"), *ref_file = in_base("kill-ref.frb");
    FrbRunOptions opt = quiet_options();
    FrbConfig cfg;
    FrbRunResult res;
    FrbState *s;
    int status = 0;
    pid_t pid;

    compute_reference(10);
    config(&cfg, 10, 3, 1, 2);
    CHECK(start(dir, &cfg) == FRB_OK);
    slow_ns = 3000000; /* far too slow to finish before the signal */
    pid = spawn_session(dir, &cfg, 0);
    nap(150);
    CHECK(kill(pid, SIGINT) == 0);
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 3, "status %d", status);
    slow_ns = 500000;
    check_sound(dir, &cfg);
    s = load(dir, &cfg);
    if (s) {
        CHECK(s->sessions == 1 && s->sessions_unfinished == 0 && s->journal.torn_bytes == 0);
        CHECK(s->by_status[FRB_MINIMAL] < s->count);
        frb_state_free(s);
    }
    CHECK(run(dir, &cfg, &opt, &res) == FRB_OK && res.complete);
    check_complete(dir, &cfg);
    finalize_ok(dir, file);
    CHECK(same_file(file, ref_file));
    compute_reference(9);
}

/* Writes that were interrupted, and files that are not what they claim. */
static void test_files(void) {
    const char *file = in_base("files.frb"), *tmp = in_base("files.frb.tmp");
    FrbFileInfo info;
    FrbStatus st;
    FrbTable t, back;
    RevGraph g;
    uint8_t *image;
    size_t size;
    FILE *f;

    make_table(&t, 8);
    remove(file);
    /* A previous writer died before its rename: its temporary file is debris. */
    f = fopen(tmp, "wb");
    fputs("half a table", f);
    fclose(f);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_IO); /* no file yet */
    CHECK(frb_file_write(file, &t) == FRB_OK);
    CHECK(file_size(tmp) == -1 && file_size(file) == FRB_HEADER_BYTES + 256 * FRB_ENTRY_BYTES);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_OK && info.complete);
    frb_table_destroy(&back);
    CHECK(frb_file_info(file, &info) == FRB_OK && info.bits == 8 && info.minimal == 256);
    for (uint32_t i = 0; i < 256; i += 17) {
        CHECK(frb_file_lookup(file, i, &info, &st, &g) == FRB_OK && st == FRB_MINIMAL);
        CHECK(frb_graph_output(&g, 8) == i);
    }
    CHECK(frb_file_lookup(file, 256, &info, &st, &g) == FRB_ERR_ARG);
    CHECK(frb_write_atomic(in_base("no-such-dir/x.frb"), "x", 1) == FRB_ERR_IO);
    CHECK(frb_file_write(in_base("no-such-dir/x.frb"), &t) == FRB_ERR_IO);

    /* A file cut short, as a crash without the rename discipline would leave it. */
    image = slurp(file, &size);
    CHECK(frb_write_atomic(file, image, size - 100) == FRB_OK);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_SIZE);
    CHECK(frb_file_info(file, &info) == FRB_ERR_SIZE);
    CHECK(frb_file_lookup(file, 3, &info, &st, &g) == FRB_ERR_SIZE);
    /* Longer than any valid file. */
    {
        size_t big = FRB_HEADER_BYTES + (1u << 16) * FRB_ENTRY_BYTES + 5000;
        uint8_t *junk = calloc(1, big);
        memcpy(junk, image, size);
        CHECK(frb_write_atomic(file, junk, big) == FRB_OK);
        CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_SIZE);
        free(junk);
    }
    /* One entry damaged behind a repaired checksum: lookup of that entry
     * fails, lookup of the others does not, a full read fails. */
    image[FRB_HEADER_BYTES + 40 * FRB_ENTRY_BYTES] ^= 1;
    fix_crcs(image, size);
    CHECK(frb_write_atomic(file, image, size) == FRB_OK);
    CHECK(frb_file_lookup(file, 40, &info, &st, &g) == FRB_ERR_OUTPUT);
    CHECK(frb_file_lookup(file, 41, &info, &st, &g) == FRB_OK);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_OUTPUT);
    /* Not a table at all. */
    CHECK(frb_write_atomic(file, "REV 1\nnodes 1\n0 W 0 0\n", 22) == FRB_OK);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_MAGIC);
    CHECK(frb_file_info(file, &info) == FRB_ERR_MAGIC);
    CHECK(frb_write_atomic(file, "", 0) == FRB_OK);
    CHECK(frb_file_read(file, &back, &info, NULL) == FRB_ERR_MAGIC);
    free(image);
    frb_table_destroy(&t);
}

#endif /* CARLOBOMB_HAVE_BOMB_BUILD */

int main(int argc, char **argv) {
    test_targets();
    test_pack_golden();
    test_pack_roundtrip();
    test_pack_rejects();
    test_complement();
    test_ring();
    test_table_and_image();
    test_enumerate();

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    snprintf(base_dir, sizeof base_dir, "%s", argc > 1 ? argv[1] : "fixed_run_test_work");
    remove_tree(base_dir);
    CHECK(mkdir(base_dir, 0777) == 0);
    compute_reference(9);
    test_files();
    test_build_variants();
    test_resume_job_caps();
    test_step_limits();
    test_journal_damage();
    test_forged_evidence();
    test_bad_engines();
    test_settings_and_lock();
    test_partial_sessions();
    test_kill_and_resume();
    test_sigint();
    if (!t_failures) remove_tree(base_dir); /* keep the evidence of a failure */
#else
    (void)argc;
    (void)argv;
    printf("construction tests skipped: no POSIX build of the constructor\n");
#endif
    return t_report("test_fixed_run");
}
