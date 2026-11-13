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
 * Usage: test_compact_internal [prefix-for-scratch-files]
 */
#include "../compact.c"

#include "minimal_solver.h"
#include "test_util.h"

enum { BITS = 8 };

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

    free(image);
    frb_table_destroy(&t);
    remove(path);
    printf("internal: %d wrong answers for shorter strings, each refused by the reader and, independently,\n"
           "          by the comparison with the fixed-width table\n",
           refused_by_both);
    return t_report("test_compact_internal");
}
