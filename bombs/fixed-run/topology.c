/*
 * The graph of a REV without its colors: canonical numbering, rank, and the
 * search for the colors.  See topology.h and FORMAT.md, section 12.
 */
#include "topology.h"

#include <string.h>

enum { MAXN = FRB_TOPOLOGY_MAX_NODES };

/*
 * Counting the canonically numbered graphs of n nodes.
 *
 * Such a graph is its list of 2n links in the order node 0 port 0, node 0
 * port 1, node 1 port 0, ...: link p belongs to node p / 2 and port p % 2.
 * Reading the list from the front, with m the number of nodes that have a
 * number so far (1 at the start: the root), every link leads either to one of
 * those m nodes or, if m < n, to the next new node, which then is node m.
 * The node a link belongs to must have a number by the time the list comes to
 * it: p / 2 < m.  A list is one of a canonical graph of n nodes if and only
 * if it keeps to that and ends with m = n.
 *
 * ways[n][p][m] is the number of ways to finish such a list from link p on
 * when m nodes have a number.
 */
static uint64_t ways[MAXN + 1][2 * MAXN + 1][MAXN + 2];

/*
 * A rank is taken apart by dividing by entries of that table.  For w =
 * ways[n][p + 1][m] of b bits, magic = ceil(2^(62 + b) / w), and for every x
 * below 2^62 (every rank is: the count for 12 nodes has 58 bits)
 *
 *     x / w == (x * magic) >> (62 + b)
 *
 * exactly: with magic * w = 2^(62 + b) + e, 0 <= e < w, the right side is
 * x / w + x * e / (w * 2^(62 + b)) before rounding down, and the second term
 * is below 2^-b < 1 / w, too little to reach the next whole number.  62 + b
 * is at least 64, so the product's high word shifted by b - 2 is the
 * quotient.
 */
static uint64_t magic[MAXN + 1][2 * MAXN + 1][MAXN + 2];
static uint8_t magic_shift[MAXN + 1][2 * MAXN + 1][MAXN + 2];
static int ready;

/* The high 64 bits of a product of two 64-bit numbers. */
static inline uint64_t mulhi64(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__) && !defined(FRB_NO_INT128)
    __extension__ typedef unsigned __int128 u128;
    return (uint64_t)(((u128)a * b) >> 64);
#else
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p01 = a0 * b1, p10 = a1 * b0;
    uint64_t mid = ((a0 * b0) >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    return a1 * b1 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

/* ceil(2^k / w) for 2 <= w < 2^62 of b bits and k = 62 + b: below 2^63.  One bit at a time. */
static uint64_t ceil_power_over(int k, uint64_t w) {
    uint64_t q = 0, r = 0;
    for (int i = k; i >= 0; i--) {
        r = (r << 1) | (uint64_t)(i == k);
        q <<= 1;
        if (r >= w) {
            r -= w;
            q |= 1u;
        }
    }
    return q + (r != 0);
}

void frb_topology_init(void) {
    if (ready) return;
    for (int n = 1; n <= MAXN; n++) {
        for (int m = 0; m <= n + 1; m++) ways[n][2 * n][m] = m == n;
        for (int p = 2 * n - 1; p >= 0; p--) {
            for (int m = 0; m <= n + 1; m++) {
                uint64_t w = 0;
                if (m >= 1 && m <= n && p / 2 < m) {
                    w = (uint64_t)m * ways[n][p + 1][m];
                    if (m < n) w += ways[n][p + 1][m + 1];
                }
                ways[n][p][m] = w;
            }
        }
        for (int p = 0; p < 2 * n; p++) {
            for (int m = 0; m <= n + 1; m++) {
                uint64_t w = ways[n][p + 1][m];
                int b = 0;
                if (w < 2) continue; /* 0: never divided by; 1: the quotient is the number itself */
                while (b < 64 && (w >> b)) b++;
                magic[n][p][m] = ceil_power_over(62 + b, w);
                magic_shift[n][p][m] = (uint8_t)(b - 2);
            }
        }
    }
    ready = 1;
}

uint64_t frb_topology_count(int nodes) {
    frb_topology_init();
    return nodes >= 1 && nodes <= MAXN ? ways[nodes][0][1] : 0;
}

int frb_topology_rank_bits(int nodes) {
    uint64_t count = frb_topology_count(nodes);
    int bits = 0;
    if (!count) return -1;
    while (bits < 64 && ((uint64_t)1 << bits) < count) bits++;
    return bits;
}

int frb_topology_from_graph(const RevGraph *g, FrbTopology *t, uint32_t *colors) {
    uint32_t c = 0;
    if (!g || g->node_count < 1 || g->node_count > MAXN) return 0;
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)g->node_count;
    for (int i = 0; i < g->node_count; i++) {
        t->link[0][i] = (uint8_t)g->nodes[i].links[0];
        t->link[1][i] = (uint8_t)g->nodes[i].links[1];
        c |= (uint32_t)(g->nodes[i].color & 1) << i;
    }
    if (colors) *colors = c;
    return 1;
}

void frb_topology_to_graph(const FrbTopology *t, uint32_t colors, RevGraph *g) {
    g->node_count = t->n;
    for (int i = 0; i < t->n; i++) {
        g->nodes[i].color = (int)((colors >> i) & 1u);
        g->nodes[i].links[0] = t->link[0][i];
        g->nodes[i].links[1] = t->link[1][i];
    }
}

void frb_topology_renumber(const FrbTopology *in, uint32_t colors_in, const uint8_t perm[FRB_TOPOLOGY_MAX_NODES],
                           FrbTopology *out, uint32_t *colors_out) {
    FrbTopology r;
    uint32_t c = 0;
    memset(&r, 0, sizeof r);
    r.n = in->n;
    for (int i = 0; i < in->n; i++) {
        r.link[0][perm[i]] = perm[in->link[0][i]];
        r.link[1][perm[i]] = perm[in->link[1][i]];
        c |= ((colors_in >> i) & 1u) << perm[i];
    }
    *out = r;
    if (colors_out) *colors_out = c;
}

int frb_topology_canonical(const FrbTopology *in, FrbTopology *out, uint8_t perm_out[FRB_TOPOLOGY_MAX_NODES]) {
    uint8_t perm[MAXN], order[MAXN];
    uint32_t seen = 1u;
    int count = 1;
    order[0] = 0;
    perm[0] = 0;
    for (int head = 0; head < count; head++) {
        int i = order[head];
        for (int c = 0; c < 2; c++) {
            int to = in->link[c][i];
            if (!((seen >> to) & 1u)) {
                seen |= 1u << to;
                perm[to] = (uint8_t)count;
                order[count++] = (uint8_t)to;
            }
        }
    }
    if (count != in->n) return count;
    frb_topology_renumber(in, 0, perm, out, NULL);
    if (perm_out) memcpy(perm_out, perm, (size_t)in->n);
    return count;
}

int frb_topology_is_canonical(const FrbTopology *t) {
    int m = 1;
    if (t->n < 1 || t->n > MAXN) return 0;
    for (int p = 0; p < 2 * t->n; p++) {
        int to = t->link[p & 1][p >> 1];
        if ((p >> 1) >= m) return 0; /* a node the list comes to before any link has led to it */
        if (to > m) return 0;
        if (to == m) m++;
    }
    return m == t->n;
}

uint64_t frb_topology_rank(const FrbTopology *t) {
    uint64_t rank = 0;
    int n = t->n, m = 1;
    frb_topology_init();
    for (int p = 0; p < 2 * n; p++) {
        int to = t->link[p & 1][p >> 1];
        if (to < m) {
            rank += (uint64_t)to * ways[n][p + 1][m];
        } else { /* the new node: after the m graphs-ful that lead to an old one here */
            rank += (uint64_t)m * ways[n][p + 1][m];
            m++;
        }
    }
    return rank;
}

int frb_topology_unrank(int nodes, uint64_t rank, FrbTopology *t) {
    int n = nodes, m = 1;
    frb_topology_init();
    if (n < 1 || n > MAXN || rank >= ways[n][0][1]) return 0;
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)n;
    for (int p = 0; p < 2 * n; p++) {
        uint64_t w = ways[n][p + 1][m], old = (uint64_t)m * w;
        if (rank < old) {
            uint64_t to = w > 1 ? mulhi64(rank, magic[n][p][m]) >> magic_shift[n][p][m] : rank;
            t->link[p & 1][p >> 1] = (uint8_t)to;
            rank -= to * w;
        } else {
            rank -= old;
            t->link[p & 1][p >> 1] = (uint8_t)m;
            m++;
        }
    }
    return 1;
}

int frb_topology_find_colors(const FrbTopology *t, uint32_t target, int bits, uint32_t *colors,
                             uint8_t order[FRB_TOPOLOGY_MAX_NODES]) {
    struct {
        uint32_t known, col;
        uint8_t k, fp, cp, count;
    } stack[MAXN + 1];
    uint32_t known = 0, col = 0;
    unsigned fp = 0, cp = 0, sp = 0, count = 0;
    int k = 0;
    for (;;) {
        unsigned to, bit;
        if (!((known >> fp) & 1u)) { /* the color of fp is open: 0 now; 1 is kept for later */
            order[fp] = (uint8_t)count++;
            known |= 1u << fp;
            stack[sp].known = known;
            stack[sp].col = col | (1u << fp);
            stack[sp].k = (uint8_t)k;
            stack[sp].fp = (uint8_t)fp;
            stack[sp].cp = (uint8_t)cp;
            stack[sp].count = (uint8_t)count;
            sp++;
        }
        to = t->link[(col >> fp) & 1u][cp];
        bit = (target >> (bits - 1 - k)) & 1u;
        if ((known >> to) & 1u) {
            if (((col >> to) & 1u) != bit) { /* a contradiction: back to the last color tried as 0 */
                if (!sp) return 0;
                sp--;
                known = stack[sp].known;
                col = stack[sp].col;
                k = stack[sp].k;
                fp = stack[sp].fp;
                cp = stack[sp].cp;
                count = stack[sp].count;
                continue;
            }
        } else { /* reached for the first time: the string says what color it has */
            order[to] = (uint8_t)count++;
            known |= 1u << to;
            col |= bit << to;
        }
        cp = to;
        if (++k == bits) {
            *colors = col;
            return (int)count;
        }
        fp = t->link[(col >> cp) & 1u][fp];
    }
}

uint32_t frb_topology_output(const FrbTopology *t, uint32_t colors, int bits) {
    unsigned fp = 0, cp = 0;
    uint32_t out = 0;
    for (int k = 0; k < bits; k++) {
        if (k) fp = t->link[(colors >> cp) & 1u][fp];
        cp = t->link[(colors >> fp) & 1u][cp];
        out = (out << 1) | ((colors >> cp) & 1u);
    }
    return out;
}
