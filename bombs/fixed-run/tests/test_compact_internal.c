/*
 * White-box test of the compact format: compact.c is compiled into this
 * test, so that the bytes of an opened Bomb can be changed after the reader
 * has accepted them.
 *
 * A compact file is checked twice before it gets its name.  The reader's own
 * validation requires every navigation bit to be what the node counts under
 * it call for.  frb_compact_against_table then works every shorter string
 * out again from the fixed-width table alone.  With a correct reader the
 * second check can never fail where the first has passed, which is why it
 * cannot be tested from outside: here each is shown to refuse, on its own, a
 * navigation bit that the other is not asked about.
 *
 * The same goes for what a conversion does after it has written: the new
 * file is opened and compared with the table before it gets its name, and an
 * expanded file is read back.  With a correct writer and a working disk
 * those checks pass, always.  Here two functions of the fixed-width library
 * that compact.c calls are replaced by ones that can be told to lie, the way
 * a broken writer or a bad disk would: a REV other than the table's while
 * the file is written, another file than the one written when it is read
 * back.  Each lie must be found, and nothing may be left behind.
 *
 * Usage: test_compact_internal [prefix-for-scratch-files]
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "fixed_run.h"

static FrbError lying_entry_decode_as(const FrbFormat *format, const uint8_t *in, FrbStatus *status, RevGraph *g);
static FrbError lying_file_read(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index);

#define frb_entry_decode_as lying_entry_decode_as
#define frb_file_read lying_file_read
#include "../compact.c"
#undef frb_entry_decode_as
#undef frb_file_read

#include "minimal_solver.h"
#include "test_util.h"

enum { BITS = 11 };

/* ---- the two functions that can be told to lie ---------------------------- */

typedef enum {
    HONEST = 0,
    WRITE_ANOTHER_REV,   /* while writing: for one string, another REV than the table's */
    READ_ANOTHER_SUM,    /* reading back: the file has another table checksum than was written */
    READ_ANOTHER_TABLE,  /* reading back: one entry is another REV */
    READ_NOT_COMPLETE    /* reading back: the file does not say it is complete */
} Lie;

static struct {
    Lie lie;
    uint32_t calls;       /* of lying_entry_decode_as since the lie was set */
    uint32_t count;       /* entries of the table: the first `count` calls are the writer's */
    const uint8_t *entry; /* the entry lied about */
    uint32_t at;          /* its string */
    RevGraph instead;     /* the REV given for it */
} liar;

static void tell(Lie lie, const FrbTable *t, uint32_t at, const RevGraph *instead) {
    memset(&liar, 0, sizeof liar);
    liar.lie = lie;
    liar.count = t->count;
    liar.entry = frb_table_entry(t, at);
    liar.at = at;
    if (instead) liar.instead = *instead;
}

static FrbError lying_entry_decode_as(const FrbFormat *format, const uint8_t *in, FrbStatus *status, RevGraph *g) {
    FrbError e = frb_entry_decode_as(format, in, status, g);
    uint32_t call = liar.calls++;
    /* frb_compact_write decodes every entry once to write its half of the table, a string and its
     * complement in turn, and frb_compact_against_table decodes every entry again to compare. */
    if (e == FRB_OK && liar.lie == WRITE_ANOTHER_REV && in == liar.entry && call < liar.count) *g = liar.instead;
    return e;
}

static FrbError lying_file_read(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index) {
    FrbError e = frb_file_read(path, t, info, bad_index);
    if (e != FRB_OK) return e;
    if (liar.lie == READ_ANOTHER_SUM) info->table_crc ^= 0x00000100u;
    if (liar.lie == READ_NOT_COMPLETE) info->complete = 0;
    if (liar.lie == READ_ANOTHER_TABLE && frb_table_put(t, liar.at, FRB_MINIMAL, &liar.instead) != FRB_OK)
        return FRB_ERR_ARG;
    return e;
}

static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
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

/* A string that starts with 0 whose REV has between `least` and `most` nodes and another numbering,
 * and a second string with a REV of as many nodes. */
static int find_pair(const FrbTable *t, int least, int most, uint32_t *at, RevGraph *other, RevGraph *wrong) {
    FrbStatus status;
    RevGraph g;
    for (uint32_t i = 0; i < t->count / 2; i++) {
        if (frb_table_get(t, i, &status, &g) != FRB_OK || g.node_count < least || g.node_count > most) continue;
        relabel(&g, 1, 2, other);
        if (same_graph(&g, other) || frb_graph_output(other, t->bits) != i) continue;
        for (uint32_t j = 0; j < t->count / 2; j++) {
            if (j == i || frb_table_nodes(t, j) != g.node_count) continue;
            if (frb_table_get(t, j, &status, wrong) != FRB_OK) return 0;
            *at = i;
            return 1;
        }
    }
    return 0;
}

/* What a conversion does when what it wrote, or what it reads back, is not the table. */
static void test_lies(const FrbTable *t, const char *prefix) {
    char path[1200], tmp[1300], out[1200], out_tmp[1300];
    FrbCompact *c = NULL;
    RevGraph other, wrong;
    uint32_t at = 0, crc = 0;

    snprintf(path, sizeof path, "%s_lie.frb", prefix);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(out, sizeof out, "%s_lie_fixed.frb", prefix);
    snprintf(out_tmp, sizeof out_tmp, "%s.tmp", out);
    remove(path);
    remove(out);

    /* Honest: the file is written, and nothing temporary stays. */
    tell(HONEST, t, 0, NULL);
    CHECK(frb_compact_write(path, t, 1, 0) == FRB_OK && exists(path) && !exists(tmp));
    CHECK(liar.calls == 2 * t->count); /* once to write, once to compare: the lies below are told to the first */
    remove(path);

    for (int large = 0; large < 2; large++) {
        int found = large ? find_pair(t, 6, 12, &at, &other, &wrong) : find_pair(t, 3, 5, &at, &other, &wrong);
        FrbError e;
        CHECK(found);
        if (!found) continue;
        /* The writer is given another REV of the string, as good as the table's.  What it writes is a
         * valid file in every byte, and it does not say of itself what the table says of it: found by
         * the comparison, and no file gets the name. */
        tell(WRITE_ANOTHER_REV, t, at, &other);
        CHECK(frb_compact_write(path, t, 1, 0) == FRB_ERR_EVIDENCE);
        CHECK(!exists(path) && !exists(tmp));
        /* The writer is given the REV of another string.  A small one is written as it is, and the
         * file does not open; a larger one has no colors with which it emits this string, or not on
         * all its nodes, and nothing is written at all. */
        tell(WRITE_ANOTHER_REV, t, at, &wrong);
        e = frb_compact_write(path, t, 1, 0);
        CHECK_MSG(e == (large ? FRB_ERR_EVIDENCE : FRB_ERR_OUTPUT), "%s REV of another string: %s", large ? "large" : "small",
                  frb_error_str(e));
        CHECK(!exists(path) && !exists(tmp));
    }

    /* Expanding: the file read back is not the one written. */
    CHECK(find_pair(t, 3, 12, &at, &other, &wrong));
    tell(HONEST, t, 0, NULL);
    CHECK(frb_compact_write(path, t, 1, 0) == FRB_OK);
    CHECK(frb_compact_open(path, &c, NULL) == FRB_OK);
    if (c) {
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_OK && exists(out) && !exists(out_tmp));
        remove(out);
        tell(READ_ANOTHER_SUM, t, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(READ_NOT_COMPLETE, t, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(READ_ANOTHER_TABLE, t, at, &other);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(HONEST, t, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_OK && exists(out));
        remove(out);
    }
    frb_compact_close(c);
    remove(path);
    printf("lies: a writer that is given another REV, or a wrong one, and a file that reads back as another:\n"
           "      each found before a file gets its name, and nothing left behind\n");
}

/* The checksum of the index made right after its bytes were changed. */
static void seal_index(FrbCompact *b) {
    b->info.index_crc = crc_of(b->nav, b->info.class_offset[1] - b->info.nav_offset);
}

int main(int argc, char **argv) {
    char path[1200], target[FRB_MAX_BITS + 1];
    FrbCompact b;
    FrbTable t;
    FILE *f = NULL;
    uint8_t *image, *nav;
    uint32_t bad = 0;
    int refused_by_both = 0, ties = 0;

    snprintf(path, sizeof path, "%s_i.frb", argc > 1 ? argv[1] : "compact_internal");
    CHECK(frb_table_create_as(&t, BITS, frb_format(1)) == FRB_OK);
    for (uint32_t i = 0; i < t.count / 2; i++) {
        RevGraph g, c;
        frb_target_format(i, BITS, target);
        CHECK(rev_minimal_solve(target, (size_t)BITS, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(&t, i ^ (t.count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
    tell(HONEST, &t, 0, NULL);
    CHECK(frb_compact_write(path, &t, 1, 0) == FRB_OK);

    /* The file in memory, opened by hand. */
    memset(&b, 0, sizeof b);
    CHECK(open_checked(path, &b.info, &f) == FRB_OK);
    image = malloc((size_t)b.info.file_bytes);
    CHECK(f && image && seek_start(f) == 0 && fread(image, 1, (size_t)b.info.file_bytes, f) == b.info.file_bytes);
    if (f) fclose(f);
    b.base = image;
    b.nav = nav = image + b.info.nav_offset;
    b.nodes = image + b.info.nodes_offset;
    b.blocks = image + b.info.blocks_offset;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) b.classes[n] = image + b.info.class_offset[n];
    b.mask = b.info.count - 1u;
    CHECK(validate(&b, &bad) == FRB_OK);
    CHECK(frb_compact_equals_table(&b, &t, &bad) == FRB_OK);

    /* Every stored string of 1 to L-1 bits gets the other navigation bit: the half that needs more
     * nodes, or, where both need the same, the half the rule does not take. */
    for (int bits = BITS - 1; bits >= 1; bits--) {
        for (uint32_t v = 0; v < ((uint32_t)1 << (bits - 1)); v++) {
            uint64_t place = ((uint64_t)1 << (bits - 1)) + v;
            int nodes[2];
            FrbError e1, e2;
            ties += frb_compact_compare(&b, bits, v, nodes) == FRB_COMPACT_TIE;
            nav[place >> 3] ^= (uint8_t)(1u << (place & 7u));
            /* The checksum of the index is the reader's first objection; with
             * that made right, the bit itself is the next. */
            CHECK(validate(&b, &bad) == FRB_ERR_CHECKSUM);
            seal_index(&b);
            bad = ~0u;
            e1 = validate(&b, &bad);
            CHECK(e1 == FRB_ERR_ENTRY && bad == v << (BITS - bits));
            bad = ~0u;
            e2 = frb_compact_against_table(&b, &t, NULL, &bad);
            CHECK(e2 == FRB_ERR_EVIDENCE && bad == v << (BITS - bits));
            refused_by_both += e1 == FRB_ERR_ENTRY && e2 == FRB_ERR_EVIDENCE;
            nav[place >> 3] ^= (uint8_t)(1u << (place & 7u));
            seal_index(&b);
        }
    }
    CHECK(refused_by_both == (1 << (BITS - 1)) - 1 && ties > 100 && ties < refused_by_both);
    CHECK(validate(&b, &bad) == FRB_OK && frb_compact_equals_table(&b, &t, &bad) == FRB_OK);

    /* Navigation bits that are all the other way: wrong in the same way
     * everywhere, as a reader and a writer sharing one mistake would have it. */
    for (uint64_t place = 1; place < b.info.stored; place++) nav[place >> 3] ^= (uint8_t)(1u << (place & 7u));
    seal_index(&b);
    CHECK(validate(&b, &bad) == FRB_ERR_ENTRY);
    CHECK(frb_compact_against_table(&b, &t, NULL, &bad) == FRB_ERR_EVIDENCE);
    for (uint64_t place = 1; place < b.info.stored; place++) nav[place >> 3] ^= (uint8_t)(1u << (place & 7u));
    seal_index(&b);
    CHECK(validate(&b, &bad) == FRB_OK);

    /* A node count that is another string's: the comparison meets it at the string, whatever the
     * reader would have said first. */
    {
        uint8_t *nodes = image + b.info.nodes_offset;
        uint32_t s = 700;
        unsigned n = nodes_at(nodes, s), other = n == 5 ? 4 : 5;
        nodes[s >> 1] ^= (uint8_t)((n ^ other) << (4u * (s & 1u)));
        bad = ~0u;
        CHECK(frb_compact_against_table(&b, &t, NULL, &bad) == FRB_ERR_EVIDENCE && bad == s);
        nodes[s >> 1] ^= (uint8_t)((n ^ other) << (4u * (s & 1u)));
        CHECK(frb_compact_against_table(&b, &t, NULL, &bad) == FRB_OK);
    }

    /* The fixed-width version a Bomb expands to: the one asked for if it can hold the length,
     * else none; unasked, the one it was made from, or the one its length calls for. */
    {
        FrbCompact c;
        memset(&c, 0, sizeof c);
        for (int bits = 1; bits <= 31; bits++) {
            int least = bits <= 16 ? 1 : bits <= 24 ? 2 : 3;
            c.info.bits = bits;
            for (int source = 0; source <= 3; source++) {
                c.info.source_version = source;
                for (int version = 1; version <= 3; version++) {
                    const FrbFormat *chosen = format_of(&c, frb_format(version));
                    CHECK(version >= least ? chosen == frb_format(version) : chosen == NULL);
                }
                /* a source version that could not have held the length is no version to expand to */
                CHECK(format_of(&c, NULL) ==
                      (source == 0 ? frb_format(least) : source >= least ? frb_format(source) : NULL));
            }
        }
    }

    /* A header that names a source which could not have held strings of its length: for every
     * length and version, written by the writer's own header code with a made-up layout. */
    {
        static uint8_t h[FRB_COMPACT_INDEX_OFFSET];
        for (int bits = 1; bits <= 31; bits++) {
            int least = bits <= 16 ? 1 : bits <= 24 ? 2 : 3;
            for (int source = 0; source <= 3; source++) {
                FrbCompactInfo made, read;
                memset(&made, 0, sizeof made);
                made.class_count[12] = (uint32_t)1 << (bits - 1); /* every REV of 12 nodes, say */
                CHECK(frb_compact_layout(&made, bits) == FRB_OK);
                made.source_version = source;
                write_header(&made, h);
                CHECK(parse_header(h, sizeof h, &read) ==
                      (source == 0 || source >= least ? FRB_OK : FRB_ERR_HEADER));
                if (source == 0) CHECK(read.bits == bits && read.class_count[12] == made.class_count[12] && read.file_bytes == made.file_bytes);
            }
        }
    }

    /* Records at every place of a byte, of every width, next to their neighbours. */
    {
        static uint8_t bytes[64 * 8 + 16];
        for (unsigned width = 1; width <= 58; width++) {
            uint64_t mask = ((uint64_t)1 << width) - 1u, x = 0x9E3779B97F4A7C15ull;
            memset(bytes, 0, sizeof bytes);
            for (uint64_t i = 0; i < 64; i++) {
                x ^= x << 13, x ^= x >> 7, x ^= x << 17;
                record_put(bytes, i, width, x & mask);
            }
            x = 0x9E3779B97F4A7C15ull;
            for (uint64_t i = 0; i < 64; i++) {
                x ^= x << 13, x ^= x >> 7, x ^= x << 17;
                CHECK(record_at(bytes, i, width) == (x & mask));
            }
            CHECK(all_zero(bytes + (64 * width + 7) / 8, sizeof bytes - (64 * width + 7) / 8));
        }
    }
    /* The node counts of a word that are a given one, against counting them one by one. */
    {
        uint64_t x = 0x243F6A8885A308D3ull;
        for (int i = 0; i < 20000; i++) {
            uint64_t word;
            unsigned n = 1 + (unsigned)(i % 12), limit = (unsigned)(i % 17), want = 0;
            x ^= x << 13, x ^= x >> 7, x ^= x << 17;
            word = i % 5 == 0 ? 0x1111111111111111ull * n : i % 7 == 0 ? x & (0x1111111111111111ull * n) : x;
            for (unsigned k = 0; k < limit; k++) want += ((word >> (4 * k)) & 15u) == n;
            CHECK(count_equal(word, 0x1111111111111111ull * n, limit) == want);
        }
    }

    free(image);
    remove(path);
    test_lies(&t, argc > 1 ? argv[1] : "compact_internal");
    frb_table_destroy(&t);
    printf("internal: %d wrong navigation bits (%d of them between halves that need the same), each refused by\n"
           "          the reader and, independently, by the comparison with the fixed-width table\n",
           refused_by_both, ties);
    return t_report("test_compact_internal");
}
