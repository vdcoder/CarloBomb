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

enum { BITS = 11 };

/* The record of a stored string (one that starts with 0), read and written in the Bomb's own memory. */
static uint64_t record_get(RevFixedBomb *b, uint32_t string, unsigned *n) {
    const FrbCompact *c = b->compact;
    *n = nodes_at(c->nodes, string);
    return record_at(c->classes[*n], rank_of(c, string, *n), c->info.class_bits[*n]);
}

static void record_set(RevFixedBomb *b, uint32_t string, uint64_t value) {
    const FrbCompact *c = b->compact;
    unsigned n = nodes_at(c->nodes, string), width = c->info.class_bits[n];
    uint8_t *bytes = c->heap + c->info.class_offset[n];
    uint64_t bit = (uint64_t)rank_of(c, string, n) * width;
    for (unsigned k = 0; k < width; k++) bytes[(bit + k) >> 3] &= (uint8_t)~(1u << ((bit + k) & 7u));
    record_put(bytes, rank_of(c, string, n), width, value);
}

int main(int argc, char **argv) {
    char path[1200], target[FRB_MAX_BITS + 1], other[FRB_MAX_BITS + 1];
    RevFixedBomb *b = NULL;
    RevFixedBombInfo info;
    FrbTable t;
    RevGraph g;
    int refused = 0, classes = 0;

    snprintf(path, sizeof path, "%s_s.frb", argc > 1 ? argv[1] : "fixed_bomb_internal");
    CHECK(frb_table_create_as(&t, BITS, frb_format(1)) == FRB_OK);
    for (uint32_t i = 0; i < t.count / 2; i++) {
        RevGraph c;
        frb_target_format(i, BITS, target);
        CHECK(rev_minimal_solve(target, (size_t)BITS, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(&t, i ^ (t.count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
    CHECK(frb_compact_write(path, &t, 1, 0) == FRB_OK);

#if !defined(_WIN32)
    setenv("CARLOBOMB_NO_MMAP", "1", 1); /* the Bomb in memory of its own, where it can be written to */
#endif
    CHECK(rev_fixed_bomb_open(path, &b, NULL) == FRB_OK && b && b->compact && b->compact->heap);
    if (b && b->compact && b->compact->heap) {
        /* One string of each node count from 3 on, with its REV kept as colors and links (up to 5
         * nodes) or as the rank of its graph (6 and more). */
        for (int n = 3; n <= b->compact->info.max_nodes; n++) {
            uint32_t at = 0, swap = 0, mask = t.count - 1u;
            uint64_t mine = 0, theirs = 0;
            unsigned na = 0, nb = 0;
            int found = 0;
            /* a string of that many nodes, and another whose REV does not serve it as well */
            for (uint32_t i = 0; i < t.count / 2 && !found; i++) {
                if (frb_table_nodes(&t, i) != n) continue;
                for (uint32_t j = 0; j < t.count / 2 && !found; j++) {
                    FrbTopology graph;
                    uint8_t order[FRB_TOPOLOGY_MAX_NODES];
                    uint32_t colors = 0;
                    if (j == i || frb_table_nodes(&t, j) != n) continue;
                    theirs = record_get(b, j, &nb);
                    if (theirs == record_get(b, i, &na)) continue;
                    /* a graph of 6 or more nodes that happens to emit this string as well, on all its
                     * nodes, would be a good REV for it: not what is wanted here */
                    if (n > FRB_COMPACT_PLAIN_NODES && frb_topology_unrank(n, theirs, &graph) &&
                        frb_topology_find_colors(&graph, i, BITS, &colors, order) == n)
                        continue;
                    at = i, swap = j, found = 1;
                }
            }
            if (!found) continue;
            classes++;
            mine = record_get(b, at, &na);
            frb_target_format(at, BITS, target);
            frb_target_format(at ^ mask, BITS, other);
            CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == at);

            /* The REV of the other string where the REV of this one was: valid, of the right size,
             * and no REV of this string.  No REV is returned for it, nor for its complement, which
             * is answered from the same record; every other string is unharmed. */
            record_set(b, at, theirs);
            g.node_count = 77;
            CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(g.node_count == 0);
            CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, NULL, NULL) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(rev_fixed_bomb_solve(b, target, BITS, 1, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
            CHECK(rev_fixed_bomb_solve(b, other, BITS, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR);
            refused += 2;
            frb_target_format(swap, BITS, other);
            CHECK(rev_fixed_bomb_solve(b, other, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == swap);
            record_set(b, at, mine);
            CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == at);

            /* A record that is no REV: a link to a node that is not there (3 and 5 nodes have link
             * values to spare), a rank beyond the last graph. */
            if (n == 3 || n == 5 || n > FRB_COMPACT_PLAIN_NODES) {
                unsigned w = (unsigned)link_bits(n);
                uint64_t broken = n > FRB_COMPACT_PLAIN_NODES ? (((uint64_t)1 << b->compact->info.class_bits[n]) - 1u)
                                                              : mine | ((((uint64_t)1 << w) - 1u) << n);
                record_set(b, at, broken);
                g.node_count = 5;
                CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_INTERNAL_ERROR && g.node_count == 0);
                record_set(b, at, mine);
                CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL);
                refused++;
            }

            /* Another node count for the string: the record of another class is taken for its REV. */
            {
                uint8_t *nodes = b->compact->heap + b->compact->info.nodes_offset;
                unsigned wrong = n == 3 ? 4u : (unsigned)n - 1u;
                RevFixedBombResult r;
                nodes[at >> 1] ^= (uint8_t)(((unsigned)n ^ wrong) << (4u * (at & 1u)));
                r = rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info);
                /* no REV, unless the record found there happens to be one of this string */
                CHECK(r == REV_FIXED_BOMB_INTERNAL_ERROR || (r == REV_FIXED_BOMB_MINIMAL && frb_graph_output(&g, BITS) == at));
                nodes[at >> 1] ^= (uint8_t)(((unsigned)n ^ wrong) << (4u * (at & 1u)));
                CHECK(rev_fixed_bomb_solve(b, target, BITS, 0, &g, &info) == REV_FIXED_BOMB_MINIMAL && info.extension == at);
            }
        }
        CHECK(classes >= 4 && refused >= 10); /* 3, 4 and 5 nodes, and a class of ranks */
    }
    rev_fixed_bomb_close(b);
    frb_table_destroy(&t);
    remove(path);
    printf("internal: a compact Bomb changed in memory after it was opened gives no REV for the strings\n"
           "          changed (%d cases in %d classes of node counts), and the others as before\n",
           refused, classes);
    return t_report("test_fixed_bomb_internal");
}
