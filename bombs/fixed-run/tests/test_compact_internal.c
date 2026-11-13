/*
 * White-box test of the compact format: compact.c is compiled into this
 * test, so that the bytes of an opened Bomb can be changed after the reader
 * has accepted them.
 *
 * A compact file is checked twice before it gets its name.  The reader's own
 * validation requires every inner entry of the prefix index to be the
 * smaller of its two children.  frb_compact_equals_table then works every
 * shorter string out again from the fixed-width table alone.  With a correct
 * reader the second check can never fail where the first has passed, which
 * is why it cannot be tested from outside: here each is shown to refuse, on
 * its own, an index that the other is not asked about.
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

enum { BITS = 8 };

/* ---- the two functions that can be told to lie ---------------------------- */

typedef enum {
    HONEST = 0,
    WRITE_ANOTHER_REV,   /* while writing: for one string, another REV of the same size that emits it too */
    WRITE_A_WRONG_REV,   /* while writing: for one string, the REV of another string of the same size */
    READ_ANOTHER_SUM,    /* reading back: the file has another table checksum than was written */
    READ_ANOTHER_TABLE,  /* reading back: one entry is another REV */
    READ_NOT_COMPLETE    /* reading back: the file does not say it is complete */
} Lie;

static struct {
    Lie lie;
    uint32_t calls;       /* of lying_entry_decode_as since the lie was set */
    uint32_t count;       /* entries of the table: the first `count` calls are the writer's */
    uint32_t at;          /* the string lied about */
    RevGraph instead;     /* the REV given for it */
} liar;

static void tell(Lie lie, uint32_t count, uint32_t at, const RevGraph *instead) {
    memset(&liar, 0, sizeof liar);
    liar.lie = lie;
    liar.count = count;
    liar.at = at;
    if (instead) liar.instead = *instead;
}

static FrbError lying_entry_decode_as(const FrbFormat *format, const uint8_t *in, FrbStatus *status, RevGraph *g) {
    FrbError e = frb_entry_decode_as(format, in, status, g);
    uint32_t call = liar.calls++;
    /* frb_compact_write decodes every entry once to write it, in order, and
     * frb_compact_equals_table decodes every entry again to compare. */
    if (e == FRB_OK && (liar.lie == WRITE_ANOTHER_REV || liar.lie == WRITE_A_WRONG_REV) && call == liar.at &&
        call < liar.count)
        *g = liar.instead;
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

static int same_rev(const RevGraph *a, const RevGraph *b) {
    if (a->node_count != b->node_count) return 0;
    for (int i = 0; i < a->node_count; i++)
        if (a->nodes[i].color != b->nodes[i].color || a->nodes[i].links[0] != b->nodes[i].links[0] ||
            a->nodes[i].links[1] != b->nodes[i].links[1])
            return 0;
    return 1;
}

/* What a conversion does when what it wrote, or what it reads back, is not the table. */
static void test_lies(const FrbTable *t, const char *prefix) {
    char path[1200], tmp[1300], out[1200], out_tmp[1300];
    FrbCompact *c = NULL;
    FrbStatus status;
    RevGraph g, other, wrong;
    uint32_t at = 0, from = 0, crc = 0;
    int found = 0;

    snprintf(path, sizeof path, "%s_lie.frb", prefix);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(out, sizeof out, "%s_lie_fixed.frb", prefix);
    snprintf(out_tmp, sizeof out_tmp, "%s.tmp", out);
    remove(path);
    remove(out);

    /* A string with a REV that has another numbering, and a second string of the same size. */
    for (uint32_t i = 0; i < t->count && !found; i++) {
        CHECK(frb_table_get(t, i, &status, &g) == FRB_OK);
        if (g.node_count < 3) continue;
        relabel(&g, 1, 2, &other);
        if (same_rev(&g, &other) || frb_graph_output(&other, t->bits) != i) continue;
        for (uint32_t j = 0; j < t->count && !found; j++) {
            if (j == i || frb_table_nodes(t, j) != g.node_count) continue;
            CHECK(frb_table_get(t, j, &status, &wrong) == FRB_OK);
            at = i;
            from = j;
            found = 1;
        }
    }
    CHECK(found);
    if (!found) return;
    (void)from;

    /* Honest: the file is written, and nothing temporary stays. */
    tell(HONEST, t->count, 0, NULL);
    CHECK(frb_compact_write(path, t, 1, 0, 0) == FRB_OK && exists(path) && !exists(tmp));
    CHECK(liar.calls == 2 * t->count); /* once to write, once to compare: the lies below are told to the first */
    remove(path);

    /* The writer puts another REV of the string into the file.  The file is valid in every byte,
     * and it is not the table: found by the comparison, and no file gets the name. */
    tell(WRITE_ANOTHER_REV, t->count, at, &other);
    CHECK(frb_compact_write(path, t, 1, 0, 0) == FRB_ERR_EVIDENCE);
    CHECK(!exists(path) && !exists(tmp));

    /* The writer puts the REV of another string there.  The file does not open. */
    tell(WRITE_A_WRONG_REV, t->count, at, &wrong);
    CHECK(frb_compact_write(path, t, 1, 0, 0) == FRB_ERR_OUTPUT);
    CHECK(!exists(path) && !exists(tmp));

    /* Expanding: the file read back is not the one written. */
    tell(HONEST, t->count, 0, NULL);
    CHECK(frb_compact_write(path, t, 1, 0, 0) == FRB_OK);
    CHECK(frb_compact_open(path, &c, NULL) == FRB_OK);
    if (c) {
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_OK && exists(out) && !exists(out_tmp));
        remove(out);
        tell(READ_ANOTHER_SUM, t->count, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(READ_NOT_COMPLETE, t->count, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(READ_ANOTHER_TABLE, t->count, at, &other);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_ERR_EVIDENCE && !exists(out) && !exists(out_tmp));
        tell(HONEST, t->count, 0, NULL);
        CHECK(frb_compact_expand(c, NULL, out, &crc) == FRB_OK && exists(out));
        remove(out);
    }
    frb_compact_close(c);
    remove(path);
    printf("lies: a writer that writes another REV, or a wrong one, and a file that reads back as another:\n"
           "      each found before a file gets its name, and nothing left behind\n");
}

int main(int argc, char **argv) {
    char path[1200], target[FRB_MAX_BITS + 1];
    FrbCompact b;
    FrbTable t;
    FILE *f = NULL;
    uint8_t *image;
    uint32_t bad = 0;
    int refused_by_both = 0;

    snprintf(path, sizeof path, "%s_i.frb", argc > 1 ? argv[1] : "compact_internal");
    CHECK(frb_table_create_as(&t, BITS, frb_format(1)) == FRB_OK);
    for (uint32_t i = 0; i < t.count; i++) {
        RevGraph g;
        frb_target_format(i, BITS, target);
        CHECK(rev_minimal_solve(target, (size_t)BITS, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
    CHECK(frb_compact_write(path, &t, 1, 0, 0) == FRB_OK);

    /* The file in memory, opened by hand. */
    memset(&b, 0, sizeof b);
    CHECK(open_checked(path, &b.info, &f) == FRB_OK);
    image = malloc((size_t)b.info.file_bytes);
    CHECK(f && image && seek64(f, 0) == 0 && fread(image, 1, (size_t)b.info.file_bytes, f) == b.info.file_bytes);
    if (f) fclose(f);
    b.base = image;
    CHECK(validate(&b, &bad) == FRB_OK);
    CHECK(frb_compact_equals_table(&b, &t, &bad) == FRB_OK);

    /* Every shorter string whose two extensions differ in their REV gets the
     * other one: the larger ordinal, where the format asks for the smaller. */
    for (int bits = BITS - 1; bits >= 1; bits--) {
        for (uint32_t v = 0; v < (1u << bits); v++) {
            uint8_t *entry = image + b.info.index_offset + index_place(bits, v) * (uint64_t)b.info.index_bytes;
            uint32_t right = get_ordinal(entry, b.info.index_bytes);
            uint32_t zero = ordinal_at(&b, index_place(bits + 1, 2 * v));
            uint32_t one = ordinal_at(&b, index_place(bits + 1, 2 * v + 1));
            FrbError e1, e2;
            CHECK(right == (one < zero ? one : zero));
            put_ordinal(entry, b.info.index_bytes, one < zero ? zero : one);
            /* The checksum of the index is the reader's first objection; with
             * that made right, the entry itself is the next. */
            CHECK(validate(&b, &bad) == FRB_ERR_CHECKSUM);
            b.info.index_crc = crc_of(image + b.info.index_offset, b.info.index_entries * (uint64_t)b.info.index_bytes);
            bad = ~0u;
            e1 = validate(&b, &bad);
            CHECK(e1 == FRB_ERR_ENTRY && bad == v << (BITS - bits));
            bad = ~0u;
            e2 = frb_compact_equals_table(&b, &t, &bad);
            CHECK(e2 == FRB_ERR_EVIDENCE && bad == v << (BITS - bits));
            refused_by_both += e1 == FRB_ERR_ENTRY && e2 == FRB_ERR_EVIDENCE;
            put_ordinal(entry, b.info.index_bytes, right);
            b.info.index_crc = crc_of(image + b.info.index_offset, b.info.index_entries * (uint64_t)b.info.index_bytes);
        }
    }
    CHECK(refused_by_both == (1 << BITS) - 2);
    CHECK(validate(&b, &bad) == FRB_OK && frb_compact_equals_table(&b, &t, &bad) == FRB_OK);

    /* An index in which every shorter string has the larger of its two
     * children, all the way up: wrong in the same way everywhere, as a
     * reader and a writer sharing one mistake would have it. */
    for (int bits = BITS - 1; bits >= 1; bits--) {
        for (uint32_t v = 0; v < (1u << bits); v++) {
            uint32_t zero = ordinal_at(&b, index_place(bits + 1, 2 * v));
            uint32_t one = ordinal_at(&b, index_place(bits + 1, 2 * v + 1));
            put_ordinal(image + b.info.index_offset + index_place(bits, v) * (uint64_t)b.info.index_bytes,
                        b.info.index_bytes, one < zero ? zero : one);
        }
    }
    b.info.index_crc = crc_of(image + b.info.index_offset, b.info.index_entries * (uint64_t)b.info.index_bytes);
    CHECK(validate(&b, &bad) == FRB_ERR_ENTRY);
    CHECK(frb_compact_equals_table(&b, &t, &bad) == FRB_ERR_EVIDENCE);

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
                made.class_count[bits] = (uint32_t)1 << bits; /* every REV a ring, say */
                CHECK(frb_compact_layout(&made, bits, 0) == FRB_OK);
                made.source_version = source;
                write_header(&made, h);
                CHECK(parse_header(h, sizeof h, &read) ==
                      (source == 0 || source >= least ? FRB_OK : FRB_ERR_HEADER));
            }
        }
    }

    free(image);
    remove(path);
    test_lies(&t, argc > 1 ? argv[1] : "compact_internal");
    frb_table_destroy(&t);
    printf("internal: %d wrong answers for shorter strings, each refused by the reader and, independently,\n"
           "          by the comparison with the fixed-width table\n",
           refused_by_both);
    return t_report("test_compact_internal");
}
