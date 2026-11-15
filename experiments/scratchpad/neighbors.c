/*
 * neighbors: what the shorter strings inside a string say about whether it
 * needs more nodes than its first bits.
 *
 *   neighbors LONGER
 *
 * LONGER is a complete Bomb for X bits; it answers every shorter length as
 * well.  For every string q of X - 1 bits that starts with 0, T is the
 * extension of q that the REV stored for q does not emit: the string a
 * level has to search.  T needs M(q) nodes or more.  The program counts how
 * often it needs more, by M(q) and by what T's last X - 1 bits need, which
 * is the other string of X - 1 bits inside T.
 *
 * The one rule known is in the table as its first line: if the last bits
 * need two nodes more than the first, T needs more than the first (a string
 * needs at least one node less than its last bits).  The other lines say
 * how much the last bits tell beyond that: frequencies, not proofs.
 *
 * It reads the Bomb and writes nothing.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"

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

int main(int argc, char **argv) {
    enum { MAXN = 32, D = 5 }; /* last bits need: 2 fewer or less, 1 fewer, the same, 1 more, 2 more */
    static uint64_t count[MAXN][D][2];
    uint64_t all[D][2] = {{0}};
    RevFixedBomb *bomb = NULL;
    uint32_t half, mask;
    int X;
    if (argc != 2) return fprintf(stderr, "usage: neighbors LONGER\n"), 2;
    if (rev_fixed_bomb_open(argv[1], &bomb, NULL) != FRB_OK) return fprintf(stderr, "neighbors: cannot open %s\n", argv[1]), 1;
    X = rev_fixed_bomb_bits(bomb);
    half = (uint32_t)1 << (X - 2);
    mask = ((uint32_t)1 << (X - 1)) - 1u;
    for (uint32_t q = 0; q < half; q++) {
        int m = rev_fixed_bomb_min_nodes(bomb, X - 1, q);
        int m0 = rev_fixed_bomb_min_nodes(bomb, X, q << 1), m1 = rev_fixed_bomb_min_nodes(bomb, X, (q << 1) | 1u);
        /* The extension the stored REV emits has m nodes; the other is the one to search.  Where both
         * have m, either may be it, and the answer to "does it need more" is no for both. */
        uint32_t t = m0 == m && m1 == m ? (q << 1) : m0 == m ? (q << 1) | 1u : (q << 1);
        int mt = rev_fixed_bomb_min_nodes(bomb, X, t);
        int s = rev_fixed_bomb_min_nodes(bomb, X - 1, t & mask) - m; /* its last X - 1 bits, against its first */
        int d = s <= -2 ? 0 : s >= 2 ? 4 : s + 2;
        if (m < 1 || m >= MAXN || (m0 != m && m1 != m)) return fprintf(stderr, "the Bomb contradicts itself\n"), 1;
        count[m][d][mt > m]++;
    }
    printf("%s: strings of %d bits; T is the extension of q that q's REV does not emit\n\n", argv[1], X);
    printf("  %-8s %-26s %14s %14s %9s\n", "M(q)", "T's last bits need", "T needs M(q)", "T needs more", "more");
    for (int m = 1; m < MAXN; m++)
        for (int d = D - 1; d >= 0; d--) {
            static const char *const name[D] = {"2 fewer, or less", "1 fewer", "the same as q", "1 more than q", "2 more than q, or over"};
            uint64_t same = count[m][d][0], more = count[m][d][1];
            if (!same && !more) continue;
            printf("  %-8d %-26s %14s %14s %8.2f%%\n", m, name[d], commas(same), commas(more), 100.0 * (double)more / (double)(same + more));
            all[d][0] += same;
            all[d][1] += more;
        }
    printf("\n");
    for (int d = D - 1; d >= 0; d--) {
        static const char *const name[D] = {"2 fewer, or less", "1 fewer", "the same as q", "1 more than q", "2 more than q, or over"};
        if (all[d][0] + all[d][1])
            printf("  %-8s %-26s %14s %14s %8.2f%%\n", "all", name[d], commas(all[d][0]), commas(all[d][1]),
                   100.0 * (double)all[d][1] / (double)(all[d][0] + all[d][1]));
    }
    rev_fixed_bomb_close(bomb);
    return 0;
}
