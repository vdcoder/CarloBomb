/*
 * Colorless REVs: an experiment, not a format.  See colorless.h.
 */
#include "colorless.h"

void cl_trace(const ClTopo *t, uint32_t colors, int L, uint32_t *visited, uint32_t used[2]) {
    unsigned fp = 0, cp = 0;
    uint32_t seen = 1u, u[2] = {0, 0};
    for (int k = 0; k < L; k++) {
        unsigned c;
        if (k) {
            c = (colors >> cp) & 1u;
            u[c] |= 1u << fp;
            fp = t->link[c][fp];
            seen |= 1u << fp;
        }
        c = (colors >> fp) & 1u;
        u[c] |= 1u << cp;
        cp = t->link[c][cp];
        seen |= 1u << cp;
    }
    if (visited) *visited = seen;
    if (used) {
        used[0] = u[0];
        used[1] = u[1];
    }
}

uint32_t cl_reachable(const ClTopo *t) {
    uint32_t seen = 1u, todo = 1u;
    while (todo) {
        int i = __builtin_ctz(todo);
        todo &= todo - 1;
        for (int c = 0; c < 2; c++) {
            uint32_t bit = 1u << t->link[c][i];
            if (!(seen & bit)) {
                seen |= bit;
                todo |= bit;
            }
        }
    }
    return seen;
}

int cl_first_difference(const ClTopo *t, uint32_t a, uint32_t b, int L) {
    ClState sa, sb;
    long limit = (long)t->n * t->n * t->n * t->n; /* joint states of the two machines */
    if (a == b) return -1;
    if (L < 1) L = 1;
    cl_output(t, a, L, &sa);
    cl_output(t, b, L, &sb);
    for (long i = 0; i <= limit; i++)
        if (cl_next(t, a, &sa) != cl_next(t, b, &sb)) return (int)i;
    return -1;
}

void cl_relabel(const ClTopo *in, uint32_t colors_in, const uint8_t perm[CL_MAX_NODES], ClTopo *out,
                uint32_t *colors_out) {
    ClTopo r;
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

int cl_canonical(const ClTopo *in, uint32_t colors_in, ClTopo *out, uint32_t *colors_out,
                 uint8_t perm_out[CL_MAX_NODES]) {
    uint8_t perm[CL_MAX_NODES], order[CL_MAX_NODES];
    uint32_t seen = 1u;
    int count = 1, reached;
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
    reached = count;
    for (int i = 0; i < in->n; i++) /* not reachable: after the others, in their old order */
        if (!((seen >> i) & 1u)) perm[i] = (uint8_t)count++;
    cl_relabel(in, colors_in, perm, out, colors_out);
    if (perm_out) memcpy(perm_out, perm, sizeof perm);
    return reached;
}

int cl_execution_order(const ClTopo *t, uint32_t colors, int L, uint8_t perm[CL_MAX_NODES]) {
    unsigned fp = 0, cp = 0;
    uint32_t seen = 1u;
    int count = 1, reached;
    perm[0] = 0;
    for (int k = 0; k < L; k++) {
        if (k) {
            fp = t->link[(colors >> cp) & 1u][fp];
            if (!((seen >> fp) & 1u)) {
                seen |= 1u << fp;
                perm[fp] = (uint8_t)count++;
            }
        }
        cp = t->link[(colors >> fp) & 1u][cp];
        if (!((seen >> cp) & 1u)) {
            seen |= 1u << cp;
            perm[cp] = (uint8_t)count++;
        }
    }
    reached = count;
    for (int i = 0; i < t->n; i++)
        if (!((seen >> i) & 1u)) perm[i] = (uint8_t)count++;
    return reached;
}

void cl_swap_ports(const ClTopo *in, ClTopo *out) {
    ClTopo r = *in;
    for (int i = 0; i < in->n; i++) {
        r.link[0][i] = in->link[1][i];
        r.link[1][i] = in->link[0][i];
    }
    *out = r;
}

void cl_key_topo(ClKey k, ClTopo *t) {
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)(k.hi >> 56);
    for (int i = 0; i < t->n; i++) {
        for (int c = 0; c < 2; c++) {
            int d = 2 * i + c;
            t->link[c][i] = (uint8_t)((d < 16 ? k.lo >> (4 * d) : k.hi >> (4 * (d - 16))) & 15u);
        }
    }
}

int cl_recolor_all(const ClTopo *t, uint32_t target, int L, uint32_t *solutions, int max, ClSearch *search) {
    struct {
        uint32_t known, col;
        uint8_t k, fp, cp;
    } stack[CL_MAX_NODES + 1];
    uint32_t known = 0, col = 0, states = 0, branches = 0, most_free = 0;
    unsigned fp = 0, cp = 0, sp = 0;
    int k = 0, found = 0;
    for (;;) {
        unsigned c, to, bit;
        int back = 0;
        if (!((known >> fp) & 1u)) {
            stack[sp].known = known | (1u << fp);
            stack[sp].col = col | (1u << fp);
            stack[sp].k = (uint8_t)k;
            stack[sp].fp = (uint8_t)fp;
            stack[sp].cp = (uint8_t)cp;
            sp++;
            branches++;
            known |= 1u << fp; /* color 0 first */
        }
        c = (col >> fp) & 1u;
        to = t->link[c][cp];
        bit = (target >> (L - 1 - k)) & 1u;
        states++;
        if ((known >> to) & 1u) {
            if (((col >> to) & 1u) != bit) back = 1;
        } else {
            known |= 1u << to;
            col |= bit << to;
        }
        if (!back) {
            cp = to;
            if (++k == L) {
                uint32_t free_nodes = (uint32_t)(t->n - __builtin_popcount(known));
                if (free_nodes > most_free) most_free = free_nodes;
                if (found < max) solutions[found] = col;
                found++;
                back = 1;
            } else {
                fp = t->link[(col >> cp) & 1u][fp];
            }
        }
        if (back) {
            if (!sp) break;
            sp--;
            known = stack[sp].known;
            col = stack[sp].col;
            k = stack[sp].k;
            fp = stack[sp].fp;
            cp = stack[sp].cp;
        }
    }
    if (search) {
        search->states = states;
        search->branches = branches;
        search->free = most_free;
    }
    return found;
}

int cl_brute(const ClTopo *t, uint32_t target, int L, uint32_t *solutions, int max) {
    int found = 0;
    for (uint32_t colors = 0; colors < (1u << t->n); colors++) {
        if (cl_verify(t, colors, target, L)) {
            if (found < max) solutions[found] = colors;
            found++;
        }
    }
    return found;
}

ClU128 cl_radix_pack(const ClTopo *t) {
    ClU128 code = 0;
    for (int i = t->n - 1; i >= 0; i--) {
        code = code * t->n + t->link[1][i];
        code = code * t->n + t->link[0][i];
    }
    return code;
}

void cl_radix_unpack(ClU128 code, int n, ClTopo *t) {
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        t->link[0][i] = (uint8_t)(code % (unsigned)n);
        code /= (unsigned)n;
        t->link[1][i] = (uint8_t)(code % (unsigned)n);
        code /= (unsigned)n;
    }
}

/*
 * Canonical topologies.  ways[n][p][m]: the number of ways to finish the
 * list from link p (node p / 2, port p % 2) when m nodes have a number.
 * Link p belongs to node p / 2, which must have been reached: p / 2 < m.  It
 * leads to one of the m numbered nodes or, if there is one left, to node m.
 */
enum { CL_COUNT_MAX = 12 };
static uint64_t ways[CL_COUNT_MAX + 1][2 * CL_COUNT_MAX + 1][CL_COUNT_MAX + 2];
/* For dividing a rank by ways[n][p + 1][m] with one multiplication:
 * rank / w == (rank * magic) >> shift for every rank below 2^62. */
static uint64_t magic[CL_COUNT_MAX + 1][2 * CL_COUNT_MAX + 1][CL_COUNT_MAX + 2];
static uint8_t magic_shift[CL_COUNT_MAX + 1][2 * CL_COUNT_MAX + 1][CL_COUNT_MAX + 2];
static int ways_ready;

static void ways_fill(void) {
    for (int n = 1; n <= CL_COUNT_MAX; n++) {
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
        /* magic = ceil(2^K / w) with K = 62 + the bits of w: below 2^63, and exact
         * for rank < 2^62 because rank * w < 2^K */
        for (int p = 0; p < 2 * n; p++) {
            for (int m = 0; m <= n + 1; m++) {
                uint64_t w = ways[n][p + 1][m];
                int bits = 0, k;
                if (w < 2) continue; /* 0: never divided by; 1: the rank itself */
                while (bits < 64 && (w >> bits)) bits++;
                k = 62 + bits;
                magic[n][p][m] = (uint64_t)((((ClU128)1 << k) + w - 1) / w);
                magic_shift[n][p][m] = (uint8_t)k;
            }
        }
    }
    ways_ready = 1;
}

uint64_t cl_canon_count(int n) {
    if (!ways_ready) ways_fill();
    return n >= 1 && n <= CL_COUNT_MAX ? ways[n][0][1] : 0;
}

int cl_is_canonical(const ClTopo *t) {
    int m = 1;
    for (int p = 0; p < 2 * t->n; p++) {
        int to = t->link[p & 1][p >> 1];
        if ((p >> 1) >= m) return 0; /* a node that was never reached */
        if (to > m) return 0;
        if (to == m) m++;
    }
    return m == t->n;
}

uint64_t cl_canon_rank(const ClTopo *t) {
    uint64_t rank = 0;
    int n = t->n, m = 1;
    if (!ways_ready) ways_fill();
    for (int p = 0; p < 2 * n; p++) {
        int to = t->link[p & 1][p >> 1];
        if (to < m) {
            rank += (uint64_t)to * ways[n][p + 1][m];
        } else {
            rank += (uint64_t)m * ways[n][p + 1][m];
            m++;
        }
    }
    return rank;
}

int cl_canon_unrank_reference(int n, uint64_t rank, ClTopo *t) {
    int m = 1;
    if (!ways_ready) ways_fill();
    if (n < 1 || n > CL_COUNT_MAX || rank >= ways[n][0][1]) return 0;
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)n;
    for (int p = 0; p < 2 * n; p++) {
        uint64_t w = ways[n][p + 1][m], old = (uint64_t)m * w;
        if (rank < old) {
            uint64_t to = w ? rank / w : 0;
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

/* The same without a division: each quotient is one multiplication and a shift. */
int cl_canon_unrank(int n, uint64_t rank, ClTopo *t) {
    int m = 1;
    if (!ways_ready) ways_fill();
    if (n < 1 || n > CL_COUNT_MAX || rank >= ways[n][0][1]) return 0;
    t->n = (uint8_t)n;
    for (int p = 0; p < 2 * n; p++) {
        uint64_t w = ways[n][p + 1][m], old = (uint64_t)m * w;
        if (rank < old) {
            uint64_t to = w > 1 ? (uint64_t)(((ClU128)rank * magic[n][p][m]) >> magic_shift[n][p][m]) : rank;
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
