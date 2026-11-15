/*
 * Tests of topology.c: the count of canonical graphs against an enumeration,
 * rank and graph of a rank as inverses in the order of the link lists, the
 * canonical numbering, and the search for the colors against a second
 * implementation, against all colorings, and on REVs the solver found.
 */
#include <stdlib.h>

#include "minimal_solver.h"
#include "test_util.h"
#include "topology.h"

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

static int same(const FrbTopology *a, const FrbTopology *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->link[0][i] != b->link[0][i] || a->link[1][i] != b->link[1][i]) return 0;
    return 1;
}

/* The link list of a graph before that of another: node 0 port 0 first. */
static int list_less(const FrbTopology *a, const FrbTopology *b) {
    for (int p = 0; p < 2 * a->n; p++) {
        int x = a->link[p & 1][p >> 1], y = b->link[p & 1][p >> 1];
        if (x != y) return x < y;
    }
    return 0;
}

static void test_counts(void) {
    static const uint64_t known[10] = {0,          1,           12,            216,            5248,
                                       160675,     5931540,     256182290,     12665445248ull, 705068085303ull};
    static const int bits[10] = {0, 0, 4, 8, 13, 18, 23, 28, 34, 40};
    for (int n = 1; n <= 9; n++) {
        CHECK_MSG(frb_topology_count(n) == known[n], "%d nodes: %llu", n, (unsigned long long)frb_topology_count(n));
        CHECK(frb_topology_rank_bits(n) == bits[n]);
    }
    for (int n = 2; n <= FRB_TOPOLOGY_MAX_NODES; n++) {
        CHECK(frb_topology_count(n) > frb_topology_count(n - 1));
        CHECK(frb_topology_count(n) < ((uint64_t)1 << 62)); /* what the division by multiplication relies on */
        CHECK(frb_topology_rank_bits(n) <= 58);
    }
    CHECK(frb_topology_count(0) == 0 && frb_topology_count(13) == 0 && frb_topology_count(-1) == 0);
    CHECK(frb_topology_rank_bits(0) == -1 && frb_topology_rank_bits(13) == -1);
    printf("counts: 1 to 9 nodes as known; 10, 11, 12 nodes: %llu, %llu, %llu graphs, ranks of %d, %d, %d bits\n",
           (unsigned long long)frb_topology_count(10), (unsigned long long)frb_topology_count(11),
           (unsigned long long)frb_topology_count(12), frb_topology_rank_bits(10), frb_topology_rank_bits(11),
           frb_topology_rank_bits(12));
}

/* Every list of 2n links for n up to 5, in order: those that are canonical
 * graphs must be exactly the graphs of the ranks 0, 1, 2, ... */
static void test_enumeration(void) {
    for (int n = 1; n <= 5; n++) {
        FrbTopology t, back;
        uint64_t lists = 1, canonical = 0;
        for (int p = 0; p < 2 * n; p++) lists *= (uint64_t)n;
        for (uint64_t code = 0; code < lists; code++) {
            uint64_t v = code;
            memset(&t, 0, sizeof t);
            t.n = (uint8_t)n;
            for (int p = 2 * n - 1; p >= 0; p--) { /* the first link is the highest digit */
                t.link[p & 1][p >> 1] = (uint8_t)(v % (uint64_t)n);
                v /= (uint64_t)n;
            }
            if (!frb_topology_is_canonical(&t)) continue;
            if (canonical < 300 || canonical % 97 == 0) {
                uint8_t perm[FRB_TOPOLOGY_MAX_NODES];
                CHECK(frb_topology_rank(&t) == canonical);
                CHECK(frb_topology_unrank(n, canonical, &back) && same(&back, &t));
                CHECK(frb_topology_canonical(&t, &back, perm) == n && same(&back, &t));
                for (int i = 0; i < n; i++) CHECK(perm[i] == i);
            } else if (frb_topology_rank(&t) != canonical) {
                CHECK(frb_topology_rank(&t) == canonical);
            }
            canonical++;
        }
        CHECK_MSG(canonical == frb_topology_count(n), "%d nodes: %llu canonical lists", n, (unsigned long long)canonical);
        CHECK(!frb_topology_unrank(n, frb_topology_count(n), &back));
    }
    printf("enumeration: every list of links of up to 5 nodes; the canonical ones are the graphs of the ranks in order\n");
}

static void test_ranks(void) {
    unsigned long long done = 0;
    for (int n = 1; n <= FRB_TOPOLOGY_MAX_NODES; n++) {
        uint64_t count = frb_topology_count(n);
        FrbTopology t, next;
        for (int i = 0; i < 40000; i++) {
            uint64_t r = i < 64 ? ((uint64_t)1 << i) - 1u : i < 128 ? count - 1u - (((uint64_t)1 << (i - 64)) - 1u) : rng();
            r %= count;
            CHECK(frb_topology_unrank(n, r, &t));
            if (!frb_topology_is_canonical(&t) || frb_topology_rank(&t) != r) {
                CHECK(frb_topology_is_canonical(&t));
                CHECK(frb_topology_rank(&t) == r);
            }
            if (r + 1 < count && i % 16 == 0) { /* the next rank is the next list */
                CHECK(frb_topology_unrank(n, r + 1, &next) && list_less(&t, &next));
            }
            done++;
        }
        CHECK(!frb_topology_unrank(n, count, &t) && !frb_topology_unrank(n, ~(uint64_t)0, &t));
    }
    CHECK(!frb_topology_unrank(0, 0, &(FrbTopology){0}) && !frb_topology_unrank(13, 0, &(FrbTopology){0}));
    printf("ranks: %llu ranks of 1 to 12 nodes to a graph and back, the ends of the range among them\n", done);
}

/* A graph of n nodes with every node reachable: a random spanning tree first, then the other links. */
static void random_graph(int n, FrbTopology *t) {
    uint8_t set[2][FRB_TOPOLOGY_MAX_NODES] = {{0}};
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)n;
    for (int i = 1; i < n;) { /* node i hangs on a free port of an earlier node */
        int from = (int)(rng() % (uint64_t)i), c = (int)(rng() & 1u);
        if (set[c][from]) c ^= 1;
        if (set[c][from]) continue;
        set[c][from] = 1;
        t->link[c][from] = (uint8_t)i;
        i++;
    }
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++)
            if (!set[c][i]) t->link[c][i] = (uint8_t)(rng() % (uint64_t)n);
}

static void test_canonical(void) {
    unsigned long long done = 0;
    for (int n = 1; n <= FRB_TOPOLOGY_MAX_NODES; n++) {
        for (int i = 0; i < 3000; i++) {
            FrbTopology g, canon, shuffled, again;
            uint8_t perm[FRB_TOPOLOGY_MAX_NODES], shuffle[FRB_TOPOLOGY_MAX_NODES];
            uint32_t colors = (uint32_t)rng() & ((1u << n) - 1u), ccanon = 0, cshuffled = 0;
            random_graph(n, &g);
            CHECK(frb_topology_canonical(&g, &canon, perm) == n);
            CHECK(frb_topology_is_canonical(&canon));
            CHECK(frb_topology_rank(&canon) < frb_topology_count(n));
            /* the same REV: the same bits */
            frb_topology_renumber(&g, colors, perm, &again, &ccanon);
            CHECK(same(&again, &canon));
            CHECK(frb_topology_output(&g, colors, 32) == frb_topology_output(&canon, ccanon, 32));
            /* any other numbering that keeps the root has the same canonical form */
            for (int k = 0; k < n; k++) shuffle[k] = (uint8_t)k;
            for (int k = n - 1; k >= 2; k--) {
                int j = 1 + (int)(rng() % (uint64_t)k);
                uint8_t tmp = shuffle[k];
                shuffle[k] = shuffle[j];
                shuffle[j] = tmp;
            }
            frb_topology_renumber(&g, colors, shuffle, &shuffled, &cshuffled);
            CHECK(frb_topology_output(&shuffled, cshuffled, 32) == frb_topology_output(&g, colors, 32));
            CHECK(frb_topology_canonical(&shuffled, &again, NULL) == n && same(&again, &canon));
            done++;
        }
    }
    {   /* a node no link leads to: no canonical numbering */
        FrbTopology g = {3, {{0, 0, 0}, {1, 1, 1}}}, out = {0};
        CHECK(frb_topology_canonical(&g, &out, NULL) == 2 && out.n == 0);
        CHECK(!frb_topology_is_canonical(&g));
    }
    {
        RevGraph g, back;
        FrbTopology t;
        uint32_t colors = 0;
        g.node_count = 3;
        g.nodes[0].color = 1, g.nodes[0].links[0] = 1, g.nodes[0].links[1] = 2;
        g.nodes[1].color = 0, g.nodes[1].links[0] = 2, g.nodes[1].links[1] = 0;
        g.nodes[2].color = 1, g.nodes[2].links[0] = 0, g.nodes[2].links[1] = 1;
        CHECK(frb_topology_from_graph(&g, &t, &colors) && t.n == 3 && colors == 5u);
        frb_topology_to_graph(&t, colors, &back);
        CHECK(back.node_count == 3 && back.nodes[1].links[0] == 2 && back.nodes[2].color == 1 && back.nodes[1].color == 0);
        g.node_count = 13;
        CHECK(!frb_topology_from_graph(&g, &t, &colors));
        g.node_count = 0;
        CHECK(!frb_topology_from_graph(&g, &t, &colors));
    }
    printf("canonical: %llu random graphs of 1 to 12 nodes; the same form from any numbering, the same bits emitted\n",
           done);
}

/* The nodes a run of `bits` bits stands on, as a mask, and the order in which it first reaches them:
 * the root, then at each bit first the node fp moves to, then the node cp moves to. */
static uint32_t reach_order(const FrbTopology *t, uint32_t colors, int bits, uint8_t first[FRB_TOPOLOGY_MAX_NODES]) {
    unsigned fp = 0, cp = 0, count = 1;
    uint32_t seen = 1u;
    first[0] = 0;
    for (int k = 0; k < bits; k++) {
        if (k) {
            fp = t->link[(colors >> cp) & 1u][fp];
            if (!((seen >> fp) & 1u)) first[fp] = (uint8_t)count++, seen |= 1u << fp;
        }
        cp = t->link[(colors >> fp) & 1u][cp];
        if (!((seen >> cp) & 1u)) first[cp] = (uint8_t)count++, seen |= 1u << cp;
    }
    return seen;
}

/* The search written a second time, as a recursion that copies its state. */
static int reference_search(const FrbTopology *t, uint32_t target, int bits, int k, unsigned fp, unsigned cp,
                            uint32_t known, uint32_t col, int count, uint8_t *order, uint32_t *colors) {
    for (;;) {
        unsigned to, bit;
        if (!((known >> fp) & 1u)) {
            uint8_t copy[FRB_TOPOLOGY_MAX_NODES];
            int found;
            order[fp] = (uint8_t)count;
            memcpy(copy, order, sizeof copy);
            found = reference_search(t, target, bits, k, fp, cp, known | (1u << fp), col, count + 1, copy, colors);
            if (found) {
                memcpy(order, copy, sizeof copy);
                return found;
            }
            return reference_search(t, target, bits, k, fp, cp, known | (1u << fp), col | (1u << fp), count + 1, order,
                                    colors);
        }
        to = t->link[(col >> fp) & 1u][cp];
        bit = (target >> (bits - 1 - k)) & 1u;
        if ((known >> to) & 1u) {
            if (((col >> to) & 1u) != bit) return 0;
        } else {
            order[to] = (uint8_t)count++;
            known |= 1u << to;
            col |= bit << to;
        }
        cp = to;
        if (++k == bits) {
            *colors = col;
            return count;
        }
        fp = t->link[(col >> cp) & 1u][fp];
    }
}

static void test_search_small(void) {
    unsigned long long done = 0, found_some = 0, found_none = 0;
    for (int n = 1; n <= 7; n++) {
        for (int i = 0; i < 6000; i++) {
            FrbTopology g;
            uint8_t order[FRB_TOPOLOGY_MAX_NODES] = {0}, want_order[FRB_TOPOLOGY_MAX_NODES] = {0};
            int bits = 1 + (int)(rng() % 14u), reached, want, any = 0;
            uint32_t target, colors = 0, want_colors = 0;
            random_graph(n, &g);
            /* half the time a string the graph can emit, else any string */
            target = (rng() & 1u) ? frb_topology_output(&g, (uint32_t)rng() & ((1u << n) - 1u), bits)
                                  : (uint32_t)rng() & ((1u << bits) - 1u);
            reached = frb_topology_find_colors(&g, target, bits, &colors, order);
            want = reference_search(&g, target, bits, 0, 0, 0, 0, 0, 0, want_order, &want_colors);
            for (uint32_t c = 0; c < (1u << n) && !any; c++) any = frb_topology_output(&g, c, bits) == target;
            CHECK(reached == want && (reached != 0) == any);
            if (reached) {
                uint8_t first[FRB_TOPOLOGY_MAX_NODES];
                uint32_t seen = reach_order(&g, colors, bits, first), places = 0;
                int n_seen = 0;
                CHECK(colors == want_colors && frb_topology_output(&g, colors, bits) == target);
                CHECK(reached >= 1 && reached <= n && order[0] == 0);
                /* the nodes the machine reaches have the places 0 .. reached-1 in the order it reaches
                 * them; the others have color 0 */
                for (int k = 0; k < n; k++) {
                    if ((seen >> k) & 1u) {
                        CHECK(order[k] == first[k] && order[k] == want_order[k]);
                        places |= 1u << order[k];
                        n_seen++;
                    } else {
                        CHECK(!((colors >> k) & 1u));
                    }
                }
                CHECK(n_seen == reached && places == (1u << reached) - 1u);
                found_some++;
            } else {
                found_none++;
            }
            done++;
        }
    }
    CHECK(found_some > 1000 && found_none > 1000);
    printf("search: %llu random graphs and strings against a second implementation and against all colorings\n"
           "        (%llu with colors, %llu without)\n",
           done, found_some, found_none);
}

/* A REV the solver found for a string that starts with 0 comes back from its
 * links alone, and from their canonical form, color for color and number for
 * number. */
static void test_search_on_witnesses(void) {
    unsigned long long done = 0, ranked = 0;
    for (int bits = 1; bits <= 13; bits++) {
        uint32_t count = (uint32_t)1 << (bits - 1), step = bits <= 11 ? 1 : 3;
        for (uint32_t s = 0; s < count; s += step) {
            char text[40];
            RevGraph g, back;
            FrbTopology t = {0}, canon = {0}, again = {0};
            uint8_t perm[FRB_TOPOLOGY_MAX_NODES] = {0}, order[FRB_TOPOLOGY_MAX_NODES] = {0};
            uint32_t colors = 0, found = 0, renumbered = 0;
            for (int i = 0; i < bits; i++) text[i] = (char)('0' + ((s >> (bits - 1 - i)) & 1u));
            text[bits] = '\0';
            CHECK(rev_minimal_solve(text, (size_t)bits, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
            CHECK(frb_topology_from_graph(&g, &t, &colors));
            CHECK(frb_topology_output(&t, colors, bits) == s);
            /* from its own links: its colors, and the numbering it has */
            CHECK(frb_topology_find_colors(&t, s, bits, &found, order) == t.n && found == colors);
            for (int i = 0; i < t.n; i++) CHECK(order[i] == i);
            /* from the canonical form, as the compact format keeps it */
            CHECK(frb_topology_canonical(&t, &canon, perm) == t.n);
            CHECK(frb_topology_unrank(t.n, frb_topology_rank(&canon), &again) && same(&again, &canon));
            CHECK(frb_topology_find_colors(&canon, s, bits, &found, order) == t.n);
            frb_topology_renumber(&canon, found, order, &again, &renumbered);
            CHECK(same(&again, &t) && renumbered == colors);
            frb_topology_to_graph(&again, renumbered, &back);
            CHECK(back.node_count == g.node_count);
            for (int i = 0; i < g.node_count; i++)
                CHECK(back.nodes[i].color == g.nodes[i].color && back.nodes[i].links[0] == g.nodes[i].links[0] &&
                      back.nodes[i].links[1] == g.nodes[i].links[1]);
            ranked += t.n >= 6;
            done++;
        }
    }
    printf("witnesses: %llu REVs of the solver for strings of 1 to 13 bits that start with 0, each back from its\n"
           "           canonical graph as it was (%llu of them of 6 nodes or more)\n",
           done, ranked);
}

int main(void) {
    frb_topology_init();
    test_counts();
    test_enumeration();
    test_ranks();
    test_canonical();
    test_search_small();
    test_search_on_witnesses();
    return t_report("test_topology");
}
