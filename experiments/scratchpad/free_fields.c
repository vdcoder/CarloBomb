/*
 * free_fields: what the REV a Bomb stores for a string leaves open.
 *
 *   free_fields SHORTER LONGER
 *
 * SHORTER is a complete Bomb for X - 1 bits, LONGER one for X bits (either
 * format).  For every string q of X - 1 bits that starts with 0, the REV the
 * shorter Bomb stores for q is run for X - 1 bits, and the fields of it that
 * the machine reads on the way are marked: the color of a node, a link of a
 * node.  A field that was not read is free: it can be given any value and
 * the REV still emits q.  (The search that found the REV never decided it;
 * the Bomb stores it as 0.)
 *
 * The REV with its free fields is therefore a REV of M(q) nodes for every
 * string that some way of filling them in emits.  This program asks how far
 * that goes for the next bit: which of q0 and q1 a filling emits.  And where
 * the link the control pointer takes in that step is free, a new node at its
 * end gives a REV of M(q) + 1 nodes for either.
 *
 * It reads the Bombs and writes nothing.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"

enum { MAXN = 32 };

static const char *commas(uint64_t v) {
    static char bufs[16][32];
    static int next;
    char digits[24], *out = bufs[next];
    int n, k = 0;
    next = (next + 1) % 16;
    n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    for (int i = 0; i < n; i++) {
        out[k++] = digits[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[k++] = ',';
    }
    out[k] = '\0';
    return out;
}

typedef struct {
    uint64_t strings;
    uint64_t free_fields[8];     /* by how many of its fields are free: 0, 1, .., 6, 7 or more */
    uint64_t step_has_free;      /* a field the next step reads is free */
    uint64_t both;               /* fillings emit q0 and q1 */
    uint64_t other_same;         /* the other extension has M(q) nodes in the longer Bomb */
    uint64_t other_same_by_free; /* and a filling of the stored REV emits it */
    uint64_t other_more;         /* it needs more */
    uint64_t other_more_by_node; /* and the stored REV with one new node emits it: M(q) + 1 nodes */
    uint64_t wrong;              /* a filling emits a string the longer Bomb gives more nodes: never */
} Tally;

int main(int argc, char **argv) {
    RevFixedBomb *shorter = NULL, *longer = NULL;
    Tally all, by[MAXN];
    int X;
    uint32_t half;
    if (argc != 3) return fprintf(stderr, "usage: free_fields SHORTER LONGER\n"), 2;
    if (rev_fixed_bomb_open(argv[1], &shorter, NULL) != FRB_OK || rev_fixed_bomb_open(argv[2], &longer, NULL) != FRB_OK)
        return fprintf(stderr, "free_fields: cannot open the Bombs\n"), 1;
    X = rev_fixed_bomb_bits(longer);
    if (rev_fixed_bomb_bits(shorter) != X - 1) return fprintf(stderr, "free_fields: the Bombs are not one bit apart\n"), 1;
    memset(&all, 0, sizeof all);
    memset(by, 0, sizeof by);
    half = (uint32_t)1 << (X - 2); /* strings of X - 1 bits that start with 0 */

    for (uint32_t q = 0; q < half; q++) {
        char text[FRB_MAX_BITS + 1];
        RevFixedBombInfo info;
        RevGraph g;
        uint8_t color_read[MAXN] = {0}, link_read[MAXN][2] = {{0}};
        int fp = 0, cp = 0, n, free_count = 0, zero_bit, other, m_other, bits_seen = 0, l2_free;
        Tally *t;
        frb_target_format(q, X - 1, text);
        if (rev_fixed_bomb_solve(shorter, text, (size_t)(X - 1), 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) return 1;
        n = g.node_count;
        if (n >= MAXN) return 1;
        t = &by[n];
        /* The machine, with what it reads marked. */
        for (int i = 0; i < X - 1; i++) {
            int c;
            if (i > 0) {
                color_read[cp] = 1;
                c = g.nodes[cp].color;
                link_read[fp][c] = 1;
                fp = g.nodes[fp].links[c];
            }
            color_read[fp] = 1;
            c = g.nodes[fp].color;
            link_read[cp][c] = 1;
            cp = g.nodes[cp].links[c];
            color_read[cp] = 1;
            if (g.nodes[cp].color != (int)((q >> (X - 2 - i)) & 1u)) return fprintf(stderr, "a REV does not emit its string\n"), 1;
        }
        for (int i = 0; i < n; i++) free_count += !color_read[i] + !link_read[i][0] + !link_read[i][1];

        /* The next step, over every way to fill in the free fields it reads. */
        {
            int c0 = g.nodes[cp].color; /* read: the control pointer is on it */
            int l1_free = !link_read[fp][c0];
            int any_free = 0;
            l2_free = 0;
            for (int a = 0; a < (l1_free ? n : 1); a++) {
                int fp2 = l1_free ? a : g.nodes[fp].links[c0];
                int c1_free = !color_read[fp2];
                for (int c1v = 0; c1v < (c1_free ? 2 : 1); c1v++) {
                    int c1 = c1_free ? c1v : g.nodes[fp2].color;
                    int same_link = (cp == fp && c1 == c0); /* the link just taken, or given a value */
                    int l2f = !same_link && !link_read[cp][c1];
                    if (l2f) l2_free = 1;
                    for (int b = 0; b < (l2f ? n : 1); b++) {
                        int cp2 = same_link ? fp2 : l2f ? b : g.nodes[cp].links[c1];
                        if (cp2 == fp2) bits_seen |= 1 << c1; /* its color was just read, or given */
                        else if (!color_read[cp2]) bits_seen |= 3, any_free = 1;
                        else bits_seen |= 1 << g.nodes[cp2].color;
                    }
                    any_free |= c1_free | l2f;
                }
            }
            any_free |= l1_free;
            t->step_has_free += (uint64_t)any_free;
        }
        zero_bit = (int)(frb_graph_output(&g, X) & 1u);
        other = zero_bit ^ 1;
        m_other = rev_fixed_bomb_min_nodes(longer, X, (q << 1) | (uint32_t)other);
        if (rev_fixed_bomb_min_nodes(longer, X, (q << 1) | (uint32_t)zero_bit) != n) return fprintf(stderr, "the Bombs disagree\n"), 1;
        if (!(bits_seen & (1 << zero_bit))) return fprintf(stderr, "the stored REV is not among its fillings\n"), 1;

        t->strings++;
        t->free_fields[free_count < 7 ? free_count : 7]++;
        if (bits_seen == 3) {
            t->both++;
            if (m_other != n) t->wrong++;
        }
        if (m_other == n) {
            t->other_same++;
            t->other_same_by_free += (uint64_t)(bits_seen == 3);
        } else {
            t->other_more++;
            t->other_more_by_node += (uint64_t)(l2_free && m_other == n + 1);
        }
    }

    printf("Strings of %d bits that start with 0, by the nodes of their REV in %s;\ntheir extensions by one bit in %s\n\n", X - 1, argv[1], argv[2]);
    printf("  %5s %12s | %-40s | %12s %12s | %12s %12s | %12s %12s\n", "nodes", "strings", "free fields: none, 1, 2, 3 or more (%)",
           "step is free", "both bits", "other: same", "by filling", "other: more", "by new node");
    for (int n = 1; n <= MAXN; n++) {
        Tally *t = n < MAXN ? &by[n] : &all;
        uint64_t three = 0;
        if (!t->strings) continue;
        for (int k = 3; k < 8; k++) three += t->free_fields[k];
        if (n < MAXN) {
            all.strings += t->strings;
            for (int k = 0; k < 8; k++) all.free_fields[k] += t->free_fields[k];
            all.step_has_free += t->step_has_free;
            all.both += t->both;
            all.other_same += t->other_same;
            all.other_same_by_free += t->other_same_by_free;
            all.other_more += t->other_more;
            all.other_more_by_node += t->other_more_by_node;
            all.wrong += t->wrong;
            printf("  %5d", n);
        } else {
            printf("  %5s", "all");
        }
        printf(" %12s | %8.3f %8.3f %8.3f %8.3f %4s | %12s %12s | %12s %12s | %12s %12s\n", commas(t->strings),
               100.0 * (double)t->free_fields[0] / (double)t->strings, 100.0 * (double)t->free_fields[1] / (double)t->strings,
               100.0 * (double)t->free_fields[2] / (double)t->strings, 100.0 * (double)three / (double)t->strings, "",
               commas(t->step_has_free), commas(t->both), commas(t->other_same), commas(t->other_same_by_free),
               commas(t->other_more), commas(t->other_more_by_node));
    }
    printf("\n  fillings that emit a string the longer Bomb gives more nodes (must be 0): %s\n", commas(all.wrong));
    rev_fixed_bomb_close(shorter);
    rev_fixed_bomb_close(longer);
    return all.wrong != 0;
}
