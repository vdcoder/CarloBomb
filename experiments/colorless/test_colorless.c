/*
 * Tests of the colorless-REV experiment code (colorless.h): the machine
 * against the reference interpreter, the canonical numbering, recoloring
 * against the brute force over all colorings, the numbering of solver
 * witnesses, and the topology codes.
 */
#include <stdio.h>
#include <stdlib.h>

#include "colorless.h"
#include "fixed_run.h"
#include "level_solver.h"
#include "test_util.h"

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static uint32_t rnd(uint32_t n) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(((rng_state * 0x2545F4914F6CDD1Dull) >> 33) % n);
}

static void random_topo(ClTopo *t, int n) {
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        t->link[0][i] = (uint8_t)rnd((uint32_t)n);
        t->link[1][i] = (uint8_t)rnd((uint32_t)n);
    }
}

static void random_perm(uint8_t perm[CL_MAX_NODES], int n) { /* root stays */
    for (int i = 0; i < n; i++) perm[i] = (uint8_t)i;
    for (int i = n - 1; i > 1; i--) {
        int j = 1 + (int)rnd((uint32_t)i);
        uint8_t tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }
}

static int popcount32(uint32_t v) { return __builtin_popcount(v); }

/* The machine here is the machine of core/rev. */
static void test_machine(void) {
    for (int r = 0; r < 20000; r++) {
        int n = 1 + (int)rnd(12), bits = 1 + (int)rnd(31);
        ClTopo t, back;
        RevGraph g;
        uint32_t colors = rnd(1u << n), colors_back = 0, out, visited, used[2];
        ClState s;
        random_topo(&t, n);
        cl_to_graph(&t, colors, &g);
        CHECK(rev_graph_validate(&g) == REV_OK);
        CHECK(cl_from_graph(&g, &back, &colors_back) && cl_same_topo(&t, &back) && colors == colors_back);
        out = cl_output(&t, colors, bits, &s);
        CHECK(out == frb_graph_output(&g, bits));
        CHECK(cl_verify(&t, colors, out, bits));
        CHECK(!cl_verify(&t, colors, out ^ (1u << rnd((uint32_t)bits)), bits));
        /* one more bit from the state is bit `bits` of a longer run */
        if (bits < 31) CHECK(cl_next(&t, colors, &s) == (cl_output(&t, colors, bits + 1, NULL) & 1u));
        cl_trace(&t, colors, bits, &visited, used);
        CHECK((visited & 1u) && (visited & ~cl_reachable(&t)) == 0);
        CHECK(((used[0] | used[1]) & ~visited) == 0);
    }
    printf("machine: outputs, verification and traces agree with the reference interpreter\n");
}

/* Canonical numbering: the same for every numbering of the same REV, and the
 * canonical topologies of n nodes are as many as the count says. */
static void test_canonical(void) {
    int invariant = 0;
    for (int r = 0; r < 20000; r++) {
        int n = 1 + (int)rnd(10), reached, reached2;
        ClTopo t, c1, c2, moved;
        uint8_t perm[CL_MAX_NODES], p1[CL_MAX_NODES];
        uint32_t colors = rnd(1u << n), k1 = 0, k2 = 0, moved_colors = 0;
        random_topo(&t, n);
        random_perm(perm, n);
        cl_relabel(&t, colors, perm, &moved, &moved_colors);
        reached = cl_canonical(&t, colors, &c1, &k1, p1);
        reached2 = cl_canonical(&moved, moved_colors, &c2, &k2, NULL);
        CHECK(reached == popcount32(cl_reachable(&t)) && reached == reached2 && p1[0] == 0);
        /* a numbering changes nothing the REV emits */
        CHECK(cl_output(&t, colors, 32, NULL) == cl_output(&moved, moved_colors, 32, NULL));
        CHECK(cl_output(&t, colors, 32, NULL) == cl_output(&c1, k1, 32, NULL));
        if (reached == n) {
            ClKey a = cl_key(&c1), b = cl_key(&c2);
            ClTopo from_key, again;
            uint32_t k3 = 0;
            CHECK(cl_same_topo(&c1, &c2) && k1 == k2 && a.hi == b.hi && a.lo == b.lo);
            CHECK(cl_is_canonical(&c1));
            cl_key_topo(a, &from_key);
            CHECK(cl_same_topo(&from_key, &c1));
            CHECK(cl_canonical(&c1, k1, &again, &k3, NULL) == n && cl_same_topo(&again, &c1) && k3 == k1);
            invariant++;
        }
    }
    CHECK(invariant > 3000);
    /* Every labeled topology of 1 to 4 nodes: those with all nodes reachable
     * fall into exactly cl_canon_count(n) canonical ones. */
    {
        static const uint64_t known[5] = {0, 1, 12, 216, 5248};
        for (int n = 1; n <= 4; n++) {
            uint64_t total = 1, found = 0;
            uint8_t *seen = calloc((size_t)cl_canon_count(n), 1);
            for (int i = 0; i < 2 * n; i++) total *= (uint64_t)n;
            for (uint64_t code = 0; code < total; code++) {
                ClTopo t, c;
                uint32_t k = 0;
                cl_radix_unpack(code, n, &t);
                if (cl_reachable(&t) != (1u << n) - 1u) {
                    CHECK(cl_canonical(&t, 0, &c, &k, NULL) < n);
                    continue;
                }
                CHECK(cl_canonical(&t, 0, &c, &k, NULL) == n && cl_is_canonical(&c));
                CHECK(cl_is_canonical(&t) == cl_same_topo(&t, &c));
                if (cl_canon_rank(&c) < cl_canon_count(n) && !seen[cl_canon_rank(&c)]) {
                    seen[cl_canon_rank(&c)] = 1;
                    found++;
                }
            }
            CHECK(cl_canon_count(n) == known[n] && found == known[n]);
            free(seen);
        }
    }
    printf("canonical: %d random REVs renumbered at random give the same canonical REV; every topology of\n"
           "           1 to 4 nodes falls into one of 1, 12, 216, 5248 canonical ones\n",
           invariant);
}

/* Rank and unrank are inverse, and the rank is the order of the lists. */
static void test_rank(void) {
    uint64_t total = 0;
    for (int n = 1; n <= 5; n++) {
        ClKey last = {0, 0};
        for (uint64_t r = 0; r < cl_canon_count(n); r++) {
            ClTopo t;
            int ok = cl_canon_unrank(n, r, &t) && cl_is_canonical(&t) && cl_canon_rank(&t) == r;
            ClKey k = cl_key(&t);
            total++;
            if (!ok) {
                CHECK(ok);
                break;
            }
            (void)last;
            (void)k;
        }
        {
            ClTopo t;
            CHECK(!cl_canon_unrank(n, cl_canon_count(n), &t));
        }
    }
    CHECK(cl_canon_count(5) == 160675 && cl_canon_count(6) == 5931540 && cl_canon_count(7) == 256182290);
    CHECK(cl_canon_count(8) == 12665445248ull && cl_canon_count(9) == 705068085303ull);
    CHECK(cl_canon_count(12) > 0 && cl_canon_count(13) == 0);
    for (int r = 0; r < 200000; r++) {
        int n = 6 + (int)rnd(7);
        uint64_t count = cl_canon_count(n);
        uint64_t rank = (((uint64_t)rnd(1u << 31) << 31) ^ rnd(1u << 31)) % count, other;
        ClTopo t, u;
        CHECK(cl_canon_unrank(n, rank, &t) && cl_is_canonical(&t) && cl_canon_rank(&t) == rank);
        other = (rank + 1 + rnd(1000)) % count;
        CHECK(cl_canon_unrank(n, other, &u) && !cl_same_topo(&t, &u));
        total++;
    }
    /* a canonical topology made from a random one has a rank that gives it back */
    for (int r = 0; r < 50000; r++) {
        int n = 1 + (int)rnd(12);
        ClTopo t, c, back;
        uint32_t k = 0;
        random_topo(&t, n);
        if (cl_canonical(&t, 0, &c, &k, NULL) != n) continue;
        CHECK(cl_canon_rank(&c) < cl_canon_count(n));
        CHECK(cl_canon_unrank(n, cl_canon_rank(&c), &back) && cl_same_topo(&back, &c));
    }
    printf("rank: %llu canonical topologies unranked and ranked back; counts 160,675 (5 nodes), 5,931,540 (6),\n"
           "      256,182,290 (7), 12,665,445,248 (8), 705,068,085,303 (9)\n",
           (unsigned long long)total);
}

static int in_list(const uint32_t *list, int n, uint32_t v) {
    for (int i = 0; i < n; i++)
        if (list[i] == v) return 1;
    return 0;
}

/* Recoloring finds what the brute force over all colorings finds. */
static void test_recolor(void) {
    static uint32_t dfs[1 << 12], brute[1 << 12];
    unsigned long long cases = 0, with_many = 0, none = 0;
    for (int r = 0; r < 60000; r++) {
        int n = 1 + (int)rnd(10), L = 1 + (int)rnd(26), found, all, distinct = 0;
        ClTopo t;
        ClSearch s0, s1, sa;
        uint32_t colors = rnd(1u << n), target, c0 = 0, c1 = 0;
        static uint32_t reduced[1 << 12];
        random_topo(&t, n);
        target = cl_output(&t, colors, L, NULL);
        if (r % 7 == 3) target ^= 1u << rnd((uint32_t)L); /* often a string no coloring emits */
        found = cl_recolor_all(&t, target, L, dfs, 1 << 12, &sa);
        all = cl_brute(&t, target, L, brute, 1 << 12);
        CHECK((found == 0) == (all == 0));
        CHECK(cl_recolor(&t, target, L, 0, &c0, &s0) == (found > 0));
        CHECK(cl_recolor(&t, target, L, 1, &c1, &s1) == (found > 0));
        if (!found) {
            none++;
            CHECK(s0.states >= 1 && s0.states == sa.states && s1.states == sa.states);
            continue;
        }
        CHECK(c0 == dfs[0] && in_list(dfs, found, c1) && s0.states >= (uint32_t)L && s0.states <= sa.states);
        /* at most 2^n ends of at most L steps each, and a guess less than there are ends */
        CHECK(sa.branches < (1u << n) && s0.branches <= sa.branches && sa.states <= (uint32_t)L << n);
        for (int i = 0; i < found; i++) {
            CHECK(cl_verify(&t, dfs[i], target, L));
            for (int j = 0; j < i; j++) CHECK(dfs[i] != dfs[j]);
        }
        /* every coloring the brute force finds is a solution of the search
         * with the nodes it never touches set to 0, and the other way round */
        for (int i = 0; i < all; i++) {
            uint32_t visited, cut;
            cl_trace(&t, brute[i], L, &visited, NULL);
            cut = brute[i] & visited;
            CHECK(in_list(dfs, found, cut));
            if (!in_list(reduced, distinct, cut)) reduced[distinct++] = cut;
        }
        CHECK(distinct == found);
        if (sa.free == 0) CHECK(all == found);
        else CHECK(all > found && all <= found << sa.free);
        cases++;
        with_many += found > 1;
    }
    CHECK(cases > 20000 && with_many > 1000 && none > 1000);
    printf("recolor: on %llu random REVs the search finds exactly the colorings the brute force finds\n"
           "         (%llu with more than one), and none where there is none (%llu)\n",
           cases, with_many, none);
}

/* The first difference of two colorings, against two long runs. */
static void test_difference(void) {
    int same_forever = 0, differ = 0;
    for (int r = 0; r < 20000; r++) {
        int n = 1 + (int)rnd(6), L = 1 + (int)rnd(20), d, want = -1;
        ClTopo t;
        ClState sa, sb;
        uint32_t a = rnd(1u << n), b = rnd(1u << n);
        random_topo(&t, n);
        d = cl_first_difference(&t, a, b, L);
        cl_output(&t, a, L, &sa);
        cl_output(&t, b, L, &sb);
        for (int i = 0; i < 4 * n * n * n * n + 64 && want < 0; i++)
            if (cl_next(&t, a, &sa) != cl_next(&t, b, &sb)) want = i;
        CHECK(d == want);
        same_forever += d < 0;
        differ += d >= 0;
    }
    CHECK(same_forever > 500 && differ > 500);
    printf("difference: first differing bit of two colorings right in %d cases, 'never' right in %d\n", differ,
           same_forever);
}

/* A LevelSolver witness has its nodes in the order the machine reaches them,
 * and comes back from its canonical form by that rule. */
static void test_witness_numbering(void) {
    int witnesses = 0, by_rule = 0;
    for (int r = 0; r < 400; r++) {
        char target[32];
        int L = 6 + (int)rnd(9), reached;
        uint32_t value = rnd(1u << L), colors = 0, kc = 0, back_colors = 0;
        uint8_t perm[CL_MAX_NODES];
        RevGraph g;
        ClTopo t, canon, back;
        frb_target_format(value, L, target);
        for (int budget = 1; budget <= L; budget++)
            if (rev_level_solve(target, (size_t)L, budget, &g) == REV_SOLVE_SOLVED) break;
        CHECK(g.node_count >= 1 && cl_from_graph(&g, &t, &colors));
        witnesses++;
        /* in the order the machine reaches the nodes already */
        reached = cl_execution_order(&t, colors, L, perm);
        CHECK(reached == t.n);
        {
            int identity = 1;
            for (int i = 0; i < t.n; i++) identity = identity && perm[i] == i;
            CHECK(identity);
        }
        /* canonical form, then back by the rule: the witness, link for link */
        CHECK(cl_canonical(&t, colors, &canon, &kc, NULL) == t.n);
        CHECK(cl_execution_order(&canon, kc, L, perm) == t.n);
        cl_relabel(&canon, kc, perm, &back, &back_colors);
        by_rule += cl_same_topo(&back, &t) && back_colors == colors;
    }
    CHECK(by_rule == witnesses);
    printf("witnesses: %d minimum-node REVs from the LevelSolver are numbered in the order the machine reaches\n"
           "           their nodes, and come back from the canonical form by that rule, link for link\n",
           witnesses);
}

/* Complement, and the two link codes. */
static void test_codes(void) {
    for (int r = 0; r < 20000; r++) {
        int n = 1 + (int)rnd(10), L = 1 + (int)rnd(31);
        ClTopo t, swapped, twice, back;
        uint32_t colors = rnd(1u << n), mask = (1u << n) - 1u, all = L == 32 ? ~0u : (1u << L) - 1u;
        random_topo(&t, n);
        cl_swap_ports(&t, &swapped);
        cl_swap_ports(&swapped, &twice);
        CHECK(cl_same_topo(&t, &twice));
        CHECK(cl_output(&swapped, colors ^ mask, L, NULL) == (cl_output(&t, colors, L, NULL) ^ all));
        cl_radix_unpack(cl_radix_pack(&t), n, &back);
        CHECK(cl_same_topo(&t, &back));
        if (n <= 9) {
            CHECK((ClU128)cl_radix_pack64(&t) == cl_radix_pack(&t));
            cl_radix_unpack64(cl_radix_pack64(&t), n, &back);
            CHECK(cl_same_topo(&t, &back));
        }
    }
    printf("codes: the complement is the other color and the other port; links in base n come back\n");
}

int main(void) {
    test_machine();
    test_canonical();
    test_rank();
    test_recolor();
    test_difference();
    test_witness_numbering();
    test_codes();
    return t_report("test_colorless");
}
