/*
 * White-box test of the Bomb-backed FixedSolver on a compact Bomb: the
 * solver and the compact reader are compiled into this test, so that the
 * bytes of a Bomb can be changed after it has been opened and validated.
 *
 * Every answer of the solver is run against the target before it is
 * returned.  For a Bomb that passed validation that check cannot fail unless
 * the memory it lives in has changed since, so it cannot be tested from
 * outside.  test_fixed_bomb_solver shows it for a fixed-width table, whose
 * storage a caller can hand over and keep a pointer to; this shows it for a
 * compact one.
 *
 * Usage: test_fixed_bomb_internal [prefix-for-scratch-files]
 */
#include "compact.c"

#include "fixed_bomb_solver.c"

#include "minimal_solver.h"
#include "test_util.h"

enum { BITS = 8 };

static uint8_t *rev_bytes(RevFixedBomb *b, uint32_t string, int *stride) {
    const FrbCompactInfo *info = &b->compact->info;
    uint32_t ordinal = frb_compact_ordinal(b->compact, BITS, string);
    int n = frb_compact_nodes_of_ordinal(b->compact, ordinal);
    *stride = (int)info->class_stride[n];
    return b->compact->heap + info->class_offset[n] + (uint64_t)(ordinal - info->class_start[n]) * info->class_stride[n];
}

int main(int argc, char **argv) {
    char path[1200], target[FRB_MAX_BITS + 1], other[FRB_MAX_BITS + 1];
    RevFixedBomb *b = NULL;
    RevFixedBombInfo info;
    FrbTable t;
    RevGraph g;
    const uint32_t at = 77; /* 01001101 */
    uint32_t swap = 0, spare = 0;
    int found = 0, with_spare = 0;

    snprintf(path, sizeof path, "%s_s.frb", argc > 1 ? argv[1] : "fixed_bomb_internal");
    CHECK(frb_table_create_as(&t, BITS, frb_format(1)) == FRB_OK);
    for (uint32_t i = 0; i < t.count; i++) {
        frb_target_format(i, BITS, target);
        CHECK(rev_minimal_solve(target, (size_t)BITS, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
    }
    CHECK(frb_compact_write(path, &t, 1, 0, 0) == FRB_OK);

#if !defined(_WIN32)
    setenv("CARLOBOMB_NO_MMAP", "1", 1); /* the Bomb in memory of its own, where it can be written to */
#endif
    CHECK(rev_fixed_bomb_open(path, &b, NULL) == FRB_OK && b && b->compact && b->compact->heap);
    if (b && b->compact && b->compact->heap) {
        uint8_t keep[64], *mine, *theirs;
        int stride = 0, stride2 = 0;
        frb_target_format(at, BITS, target);
        CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == at);

        /* Another string whose REV has as many nodes and is another REV. */
        for (uint32_t j = 0; j < t.count && !found; j++) {
            if (j == at || frb_table_nodes(&t, j) != frb_table_nodes(&t, at)) continue;
            mine = rev_bytes(b, at, &stride);
            theirs = rev_bytes(b, j, &stride2);
            if (memcmp(mine, theirs, (size_t)stride) == 0) continue;
            swap = j;
            found = 1;
        }
        CHECK(found);
        /* The REV of the one where the REV of the other was: valid, of the right size, and it
         * emits another string.  No REV is returned for either; every other string is unharmed. */
        mine = rev_bytes(b, at, &stride);
        theirs = rev_bytes(b, swap, &stride2);
        CHECK(stride == stride2 && stride <= (int)sizeof keep);
        memcpy(keep, mine, (size_t)stride);
        memcpy(mine, theirs, (size_t)stride);
        g.node_count = 77;
        CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
        CHECK(g.node_count == 0);
        CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, NULL, NULL) == REV_FIXED_BOMB_INTERNAL_ERROR);
        CHECK(rev_fixed_bomb_solve(b, target, BITS, 1, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
        frb_target_format(swap, BITS, other);
        CHECK(rev_fixed_bomb_solve(b, other, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == swap);
        memcpy(mine, keep, (size_t)stride);
        CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == at);

        /* A link to a node that is not there: a class of 3, 5, 6 or 7 nodes has link values to spare. */
        for (uint32_t j = 0; j < t.count && !with_spare; j++) {
            int n = frb_table_nodes(&t, j);
            if (n == 3 || (n >= 5 && n <= 7)) {
                spare = j;
                with_spare = 1;
            }
        }
        CHECK(with_spare);
        if (with_spare) {
            int n = frb_table_nodes(&t, spare), w = frb_compact_link_bits(n);
            mine = rev_bytes(b, spare, &stride);
            memcpy(keep, mine, (size_t)stride);
            mine[0] |= (uint8_t)(((1u << w) - 1u) << 1); /* link 0 of node 0: all ones */
            frb_target_format(spare, BITS, other);
            g.node_count = 5;
            CHECK(rev_fixed_bomb_solve(b, other, BITS, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR && g.node_count == 0);
            memcpy(mine, keep, (size_t)stride);
            CHECK(rev_fixed_bomb_solve(b, other, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL);
        }
    }
    rev_fixed_bomb_close(b);
    frb_table_destroy(&t);
    remove(path);
    printf("internal: a compact Bomb changed in memory after it was opened gives no REV for the strings\n"
           "          changed, and the others as before\n");
    return t_report("test_fixed_bomb_internal");
}
