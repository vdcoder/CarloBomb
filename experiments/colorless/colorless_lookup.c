/*
 * colorless_lookup: candidate layouts for a compact Bomb, built in memory
 * from a complete fixed-width Bomb and measured end to end.
 *
 *   colorless_lookup --bomb FILE [--queries N] [--block B] [--no-check]
 *
 * Nothing is written anywhere: the layouts live in this process and are gone
 * when it ends.  They are prototypes for sizes and lookup times, not a file
 * format.  Each of them must give back, for every string of L bits, the REV
 * the fixed-width Bomb stores, link for link and color for color; unless
 * --no-check is given that is checked for all 2^L strings, and for every
 * shorter string that the walk along the one-bit decisions ends at the
 * extension the ordinal rule of the compact format chooses.
 *
 * The layouts:
 *
 *   F  fixed-width   the Bomb file as it is (entries found by arithmetic),
 *                    with the one-bit decisions for shorter strings
 *   V  v4 as built   an ordinal for every string of 1 to L bits, and the
 *                    REVs by node count with color and links as bit fields
 *   W  A+B           one bit for every shorter string (A); for every string
 *                    of L bits its node count in 4 bits, the ordinal by
 *                    counting from a checkpoint every B strings (B); the
 *                    REVs as in V
 *   Y  A+B, rank     the REVs of 6 and more nodes as the rank of their
 *                    canonical topology plus their N colors; the numbering
 *                    of the stored REV is found again by running it
 *   X  A+B, rank,    the same without the colors: they are found from the
 *      colorless     topology and the string; the few strings whose first
 *                    coloring is not the stored one are listed
 *
 * Y and X keep the REVs of up to 5 nodes as V has them: 19,122 of
 * 16,777,216 at 24 bits, most of them numbered by the enumerator and not by
 * the search, for which the rule of the search does not hold.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorless.h"
#include "compact.h"
#include "fixed_run.h"

enum { CLASSES = 16, SMALL = 5, RUNS = 5 };

typedef struct {
    int L;
    uint32_t count;
    const FrbTable *table;
    uint32_t class_count[CLASSES], class_start[CLASSES];
    /* A: one bit for every shorter string, at 2^k + v */
    uint64_t *direction;
    /* B: node counts, 16 to a word; counts before each block */
    uint64_t *classes;
    uint32_t *checkpoint; /* [block][class] */
    int block_shift;
    /* V: ordinals */
    uint8_t *ordinal; /* 3 bytes each, at 2^k - 2 + v */
    /* REVs by node count */
    uint8_t *fields[CLASSES];
    uint32_t stride[CLASSES];
    uint8_t *ranked[CLASSES]; /* rank, colorless, bit-packed */
    uint8_t *ranked_colors[CLASSES]; /* rank and colors, bit-packed */
    int rank_bits[CLASSES];
    /* X: strings whose stored coloring is not the first found */
    uint32_t *other_string;
    uint8_t *other_rank;
    uint32_t other_mask, others;
} Layouts;

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rnd64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

/* ---- pieces ------------------------------------------------------------------ */

static inline uint64_t load64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof v);
    return v; /* little-endian machines only: this is a prototype */
}

static inline uint64_t get_packed(const uint8_t *pool, uint64_t index, int width) {
    uint64_t bit = index * (uint64_t)width;
    return (load64(pool + (bit >> 3)) >> (bit & 7u)) & (((uint64_t)1 << width) - 1u);
}

static void put_packed(uint8_t *pool, uint64_t index, int width, uint64_t value) {
    uint64_t bit = index * (uint64_t)width;
    uint64_t v = load64(pool + (bit >> 3));
    v |= value << (bit & 7u);
    memcpy(pool + (bit >> 3), &v, sizeof v);
}

/* The compact format's record: node i at bit i * (1 + 2w), color then two links of w bits. */
static inline void fields_unpack(const uint8_t *rec, int n, ClTopo *t, uint32_t *colors) {
    int w = 0, width;
    uint32_t c = 0, mask;
    while ((1 << w) < n) w++;
    width = 1 + 2 * w;
    mask = (1u << w) - 1u;
    t->n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        unsigned bit = (unsigned)(i * width);
        uint32_t v = (uint32_t)(load64(rec + (bit >> 3)) >> (bit & 7u));
        c |= (v & 1u) << i;
        t->link[0][i] = (uint8_t)((v >> 1) & mask);
        t->link[1][i] = (uint8_t)((v >> (1 + w)) & mask);
    }
    *colors = c;
}

/* A fixed-width entry of format version 1, 2 or 3: n nodes of 1 + 2w bits. */
static inline int entry_unpack(const FrbTable *table, uint32_t index, ClTopo *t, uint32_t *colors) {
    const uint8_t *e = frb_table_entry(table, index);
    int w = table->format->link_bits, width = table->format->node_bits;
    int n = e[table->format->graph_bytes] & 31;
    uint32_t c = 0, mask = (1u << w) - 1u;
    t->n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        unsigned bit = (unsigned)(i * width);
        uint32_t v = (uint32_t)e[bit >> 3] | ((uint32_t)e[(bit >> 3) + 1] << 8) | ((uint32_t)e[(bit >> 3) + 2] << 16);
        v >>= bit & 7u;
        c |= (v & 1u) << i;
        t->link[0][i] = (uint8_t)((v >> 1) & mask);
        t->link[1][i] = (uint8_t)((v >> (1 + w)) & mask);
    }
    *colors = c;
    return n;
}

static inline uint32_t class_at(const Layouts *y, uint32_t t) {
    return (uint32_t)(y->classes[t >> 4] >> (4 * (t & 15u))) & 15u;
}

/* How many of the first `limit` (0..16) 4-bit fields of w equal the pattern's. */
static inline uint32_t count_equal(uint64_t w, uint64_t pattern, unsigned limit) {
    uint64_t x = w ^ pattern;
    uint64_t differs = (x | (x >> 1) | (x >> 2) | (x >> 3)) & 0x1111111111111111ull;
    if (limit < 16) differs |= 0x1111111111111111ull << (4 * limit);
    return 16u - (uint32_t)__builtin_popcountll(differs);
}

/* B: how many strings before t have the node count n. */
static inline uint32_t rank_in_class(const Layouts *y, uint32_t t, uint32_t n) {
    uint32_t block = t >> y->block_shift;
    uint32_t r = y->checkpoint[(size_t)block * CLASSES + n];
    uint64_t pattern = 0x1111111111111111ull * n;
    uint32_t w = (block << y->block_shift) >> 4, last = t >> 4;
    for (; w < last; w++) r += count_equal(y->classes[w], pattern, 16);
    return r + count_equal(y->classes[last], pattern, t & 15u);
}

/* A: from a string of `bits` bits to its extension of L bits with the fewest nodes. */
static inline uint32_t walk(const Layouts *y, int bits, uint32_t value) {
    uint64_t h = ((uint64_t)1 << bits) + value;
    for (int k = bits; k < y->L; k++) h = 2 * h + ((y->direction[h >> 6] >> (h & 63u)) & 1u);
    return (uint32_t)(h - ((uint64_t)1 << y->L));
}

static inline uint32_t ordinal_at(const Layouts *y, int bits, uint32_t value) {
    const uint8_t *p = y->ordinal + 3 * ((((size_t)1 << bits) - 2u) + value);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline int class_of_ordinal(const Layouts *y, uint32_t ordinal) {
    int n = 1;
    for (int k = 2; k < CLASSES; k++) n += ordinal >= y->class_start[k];
    return n;
}

static inline uint32_t other_coloring(const Layouts *y, uint32_t t) {
    uint32_t at = (uint32_t)((t * 0x9E3779B1u) >> 8) & y->other_mask;
    while (y->other_string[at] != ~0u) {
        if (y->other_string[at] == t) return y->other_rank[at];
        at = (at + 1) & y->other_mask;
    }
    return 0;
}

/* The numbering of the stored REV: nodes in the order the machine reaches them. */
static inline void renumber(const ClTopo *canon, uint32_t colors, int L, ClTopo *out, uint32_t *colors_out) {
    uint8_t perm[CL_MAX_NODES];
    cl_execution_order(canon, colors, L, perm);
    cl_relabel(canon, colors, perm, out, colors_out);
}

/*
 * One run of L bits that does two things: checks that the canonical REV emits
 * the string, and notes the order in which the machine reaches the nodes,
 * which is the numbering of the stored REV.  Returns 0 if a bit is wrong.
 */
static inline int verify_and_renumber(const ClTopo *canon, uint32_t colors, uint32_t target, int L, ClTopo *out,
                                      uint32_t *colors_out) {
    uint8_t perm[CL_MAX_NODES];
    unsigned fp = 0, cp = 0, count = 1;
    uint32_t seen = 1u, c = 0;
    perm[0] = 0;
    for (int k = 0; k < L; k++) {
        if (k) {
            fp = canon->link[(colors >> cp) & 1u][fp];
            if (!((seen >> fp) & 1u)) {
                seen |= 1u << fp;
                perm[fp] = (uint8_t)count++;
            }
        }
        cp = canon->link[(colors >> fp) & 1u][cp];
        if (!((seen >> cp) & 1u)) {
            seen |= 1u << cp;
            perm[cp] = (uint8_t)count++;
        }
        if (((colors >> cp) & 1u) != ((target >> (L - 1 - k)) & 1u)) return 0;
    }
    if (count != canon->n) return 0; /* a minimum-node REV reaches all its nodes */
    out->n = canon->n;
    for (int i = 0; i < canon->n; i++) {
        out->link[0][perm[i]] = perm[canon->link[0][i]];
        out->link[1][perm[i]] = perm[canon->link[1][i]];
        c |= ((colors >> i) & 1u) << perm[i];
    }
    *colors_out = c;
    return 1;
}

/* ---- the five lookups: the REV for the string of `bits` bits, as stored ------- */

static inline uint32_t lookup_F(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    entry_unpack(y->table, leaf, t, colors);
    return leaf;
}

static inline uint32_t lookup_V(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t ordinal = ordinal_at(y, bits, value);
    int n = class_of_ordinal(y, ordinal);
    fields_unpack(y->fields[n] + (size_t)(ordinal - y->class_start[n]) * y->stride[n], n, t, colors);
    return ordinal;
}

static inline uint32_t lookup_W(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf);
    fields_unpack(y->fields[n] + (size_t)rank_in_class(y, leaf, n) * y->stride[n], (int)n, t, colors);
    return leaf;
}

static inline uint32_t lookup_Y(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) {
        fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
    } else {
        ClTopo canon;
        uint64_t rec = get_packed(y->ranked_colors[n], r, y->rank_bits[n] + (int)n);
        cl_canon_unrank((int)n, rec >> n, &canon);
        renumber(&canon, (uint32_t)(rec & ((1u << n) - 1u)), y->L, t, colors);
    }
    return leaf;
}

static inline uint32_t lookup_X(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) {
        fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
    } else {
        ClTopo canon;
        uint32_t found = 0, other;
        cl_canon_unrank((int)n, get_packed(y->ranked[n], r, y->rank_bits[n]), &canon);
        cl_recolor(&canon, leaf, y->L, 0, &found, NULL);
        other = other_coloring(y, leaf);
        if (other) { /* one of the few strings with another coloring before the stored one */
            uint32_t all[64];
            cl_recolor_all(&canon, leaf, y->L, all, 64, NULL);
            found = all[other];
        }
        renumber(&canon, found, y->L, t, colors);
    }
    return leaf;
}

/* Y and X as a reader would do them: the stored REV, verified, with one run
 * for both.  Return 0 in *ok if the REV does not emit its string. */
static inline uint32_t lookup_Y_verified(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) {
        fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
        if (!cl_verify(t, *colors, leaf, y->L)) t->n = 0;
    } else {
        ClTopo canon;
        uint64_t rec = get_packed(y->ranked_colors[n], r, y->rank_bits[n] + (int)n);
        cl_canon_unrank((int)n, rec >> n, &canon);
        if (!verify_and_renumber(&canon, (uint32_t)(rec & ((1u << n) - 1u)), leaf, y->L, t, colors)) t->n = 0;
    }
    return leaf;
}

static inline uint32_t lookup_X_verified(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) {
        fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
        if (!cl_verify(t, *colors, leaf, y->L)) t->n = 0;
    } else {
        ClTopo canon;
        uint32_t found = 0, other;
        cl_canon_unrank((int)n, get_packed(y->ranked[n], r, y->rank_bits[n]), &canon);
        cl_recolor(&canon, leaf, y->L, 0, &found, NULL);
        other = other_coloring(y, leaf);
        if (other) {
            uint32_t all[64];
            cl_recolor_all(&canon, leaf, y->L, all, 64, NULL);
            found = all[other];
        }
        if (!verify_and_renumber(&canon, found, leaf, y->L, t, colors)) t->n = 0;
    }
    return leaf;
}

/* X with the search as the verification: the colors are found by running the
 * machine against the string, so what is found emits it; the order of that
 * run is the numbering.  No second run. */
static inline uint32_t lookup_X_search_only(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) {
        fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
    } else {
        ClTopo canon;
        uint8_t perm[CL_MAX_NODES];
        uint32_t found = 0, other;
        cl_canon_unrank((int)n, get_packed(y->ranked[n], r, y->rank_bits[n]), &canon);
        if (!cl_recolor_order(&canon, leaf, y->L, 0, &found, perm)) {
            t->n = 0;
            return leaf;
        }
        other = other_coloring(y, leaf);
        if (other) {
            uint32_t all[64];
            cl_recolor_all(&canon, leaf, y->L, all, 64, NULL);
            found = all[other];
            cl_execution_order(&canon, found, y->L, perm);
        }
        cl_relabel(&canon, found, perm, t, colors);
    }
    return leaf;
}

/* Y without the renumbering: the REV in canonical numbering. */
static inline uint32_t lookup_Y_canonical(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
    else {
        uint64_t rec = get_packed(y->ranked_colors[n], r, y->rank_bits[n] + (int)n);
        cl_canon_unrank((int)n, rec >> n, t);
        *colors = (uint32_t)(rec & ((1u << n) - 1u));
    }
    return leaf;
}

/* X without the renumbering: a valid minimum-node REV for the string, in canonical numbering. */
static inline uint32_t lookup_X_canonical(const Layouts *y, int bits, uint32_t value, ClTopo *t, uint32_t *colors) {
    uint32_t leaf = bits == y->L ? value : walk(y, bits, value);
    uint32_t n = class_at(y, leaf), r = rank_in_class(y, leaf, n);
    if (n <= SMALL) fields_unpack(y->fields[n] + (size_t)r * y->stride[n], (int)n, t, colors);
    else {
        cl_canon_unrank((int)n, get_packed(y->ranked[n], r, y->rank_bits[n]), t);
        cl_recolor(t, leaf, y->L, 0, colors, NULL);
    }
    return leaf;
}

/* ---- building ------------------------------------------------------------------- */

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

static void *zalloc(size_t bytes) {
    void *p = calloc(bytes + 16, 1);
    if (!p) {
        fprintf(stderr, "colorless_lookup: out of memory\n");
        exit(1);
    }
    return p;
}

static void build_checkpoints(Layouts *y, int block_shift) {
    uint32_t blocks = (y->count >> block_shift) + 1, running[CLASSES] = {0};
    free(y->checkpoint);
    y->block_shift = block_shift;
    y->checkpoint = zalloc((size_t)blocks * CLASSES * sizeof *y->checkpoint);
    for (uint32_t t = 0;; t++) {
        if ((t & (((uint32_t)1 << block_shift) - 1u)) == 0)
            memcpy(y->checkpoint + (size_t)(t >> block_shift) * CLASSES, running, sizeof running);
        running[class_at(y, t)]++;
        if (t == y->count - 1) break;
    }
}

static void build(Layouts *y, const FrbTable *table, int block_shift) {
    uint32_t next[CLASSES] = {0}, others = 0;
    uint8_t *best, *below;
    int L = table->bits;
    memset(y, 0, sizeof *y);
    y->L = L;
    y->count = table->count;
    y->table = table;
    y->classes = zalloc(((size_t)y->count / 16 + 1) * sizeof *y->classes);
    for (uint32_t t = 0;; t++) {
        uint32_t n = (uint32_t)frb_table_nodes(table, t);
        if (n >= CLASSES) {
            fprintf(stderr, "colorless_lookup: a REV of %u nodes; this prototype takes up to %d\n", (unsigned)n, CLASSES - 1);
            exit(1);
        }
        y->classes[t >> 4] |= (uint64_t)n << (4 * (t & 15u));
        y->class_count[n]++;
        if (t == y->count - 1) break;
    }
    for (int n = 1; n < CLASSES; n++) y->class_start[n] = y->class_start[n - 1] + y->class_count[n - 1];
    build_checkpoints(y, block_shift);

    /* A, and the ordinals of V: from the strings of L bits upwards */
    y->direction = zalloc(((size_t)y->count / 64 + 1) * sizeof *y->direction);
    y->ordinal = zalloc(3 * (((size_t)2 << L) - 2));
    below = zalloc(y->count);
    for (uint32_t t = 0;; t++) {
        uint32_t n = class_at(y, t), o = y->class_start[n] + next[n]++;
        uint8_t *p = y->ordinal + 3 * ((((size_t)1 << L) - 2u) + t);
        p[0] = (uint8_t)o;
        p[1] = (uint8_t)(o >> 8);
        p[2] = (uint8_t)(o >> 16);
        below[t] = (uint8_t)n;
        if (t == y->count - 1) break;
    }
    for (int k = L - 1; k >= 1; k--) {
        best = zalloc((size_t)1 << k);
        for (uint32_t v = 0; v < ((uint32_t)1 << k); v++) {
            uint32_t zero = ordinal_at(y, k + 1, 2 * v), one = ordinal_at(y, k + 1, 2 * v + 1), o = one < zero ? one : zero;
            uint8_t *p = y->ordinal + 3 * ((((size_t)1 << k) - 2u) + v);
            uint64_t h = ((uint64_t)1 << k) + v;
            p[0] = (uint8_t)o;
            p[1] = (uint8_t)(o >> 8);
            p[2] = (uint8_t)(o >> 16);
            /* A is made from the node counts alone: fewer nodes, and 0 before 1 among equals */
            if (below[2 * v + 1] < below[2 * v]) y->direction[h >> 6] |= (uint64_t)1 << (h & 63u);
            best[v] = below[2 * v + 1] < below[2 * v] ? below[2 * v + 1] : below[2 * v];
        }
        free(below);
        below = best;
    }
    free(below);

    /* the REVs, by node count, in the order of their strings */
    for (int n = 1; n < CLASSES; n++) {
        uint64_t any = cl_canon_count(n);
        if (!y->class_count[n]) continue;
        y->stride[n] = (uint32_t)frb_compact_stride(n);
        y->fields[n] = zalloc((size_t)y->class_count[n] * y->stride[n]);
        while (((uint64_t)1 << y->rank_bits[n]) < any) y->rank_bits[n]++;
        if (n > SMALL) {
            y->ranked[n] = zalloc(((size_t)y->class_count[n] * (size_t)y->rank_bits[n] + 7) / 8);
            y->ranked_colors[n] = zalloc(((size_t)y->class_count[n] * (size_t)(y->rank_bits[n] + n) + 7) / 8);
        }
    }
    memset(next, 0, sizeof next);
    /* first pass counts the strings with another coloring first, second fills */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            y->other_mask = 255;
            while (y->other_mask + 1 < 4 * others + 16) y->other_mask = 2 * y->other_mask + 1;
            y->other_string = zalloc(((size_t)y->other_mask + 1) * sizeof *y->other_string);
            y->other_rank = zalloc((size_t)y->other_mask + 1);
            memset(y->other_string, 0xFF, ((size_t)y->other_mask + 1) * sizeof *y->other_string);
            y->others = others;
        }
        for (uint32_t t = 0;; t++) {
            RevGraph g;
            FrbStatus status;
            ClTopo stored, canon;
            uint32_t colors = 0, kc = 0, first = 0, n;
            frb_entry_decode_as(table->format, frb_table_entry(table, t), &status, &g);
            cl_from_graph(&g, &stored, &colors);
            n = stored.n;
            if (pass == 0) {
                uint32_t r = next[n]++;
                frb_compact_pack(&g, y->fields[n] + (size_t)r * y->stride[n]);
                if (n > SMALL) {
                    uint64_t rank;
                    cl_canonical(&stored, colors, &canon, &kc, NULL);
                    rank = cl_canon_rank(&canon);
                    put_packed(y->ranked[n], r, y->rank_bits[n], rank);
                    put_packed(y->ranked_colors[n], r, y->rank_bits[n] + (int)n, (rank << n) | kc);
                    cl_recolor(&canon, t, L, 0, &first, NULL);
                    others += first != kc;
                }
            } else if (n > SMALL) {
                cl_canonical(&stored, colors, &canon, &kc, NULL);
                cl_recolor(&canon, t, L, 0, &first, NULL);
                if (first != kc) {
                    uint32_t all[64], at = (uint32_t)((t * 0x9E3779B1u) >> 8) & y->other_mask;
                    int k = cl_recolor_all(&canon, t, L, all, 64, NULL), which = 0;
                    for (int i = 0; i < k && i < 64; i++)
                        if (all[i] == kc) which = i;
                    while (y->other_string[at] != ~0u) at = (at + 1) & y->other_mask;
                    y->other_string[at] = t;
                    y->other_rank[at] = (uint8_t)which;
                }
            }
            if (t == y->count - 1) break;
        }
    }
}

static void destroy(Layouts *y) {
    free(y->direction);
    free(y->classes);
    free(y->checkpoint);
    free(y->ordinal);
    for (int n = 0; n < CLASSES; n++) {
        free(y->fields[n]);
        free(y->ranked[n]);
        free(y->ranked_colors[n]);
    }
    free(y->other_string);
    free(y->other_rank);
    memset(y, 0, sizeof *y);
}

/* ---- measuring -------------------------------------------------------------------- */

typedef struct {
    uint32_t value;
    uint8_t bits;
} Query;

#define TIME_LOOKUP(NAME, CALL) \
    static double NAME(const Layouts *y, const Query *q, size_t n, int verify, unsigned long long *sum) { \
        double best = 1e30; \
        for (int run = 0; run < RUNS; run++) { \
            unsigned long long s = 0; \
            double t0 = cb_now(), t; \
            for (size_t i = 0; i < n; i++) { \
                ClTopo topo; \
                uint32_t colors = 0, leaf; \
                leaf = CALL(y, q[i].bits, q[i].value, &topo, &colors); \
                (void)leaf; \
                if (verify) s += (unsigned)cl_verify(&topo, colors, q[i].value, q[i].bits); \
                s += colors + ((unsigned)topo.n << 20) + topo.link[0][0] + 3u * topo.link[1][topo.n - 1]; \
            } \
            t = cb_now() - t0; \
            if (t < best) best = t; \
            *sum = s; \
        } \
        return best / (double)n * 1e9; \
    }
TIME_LOOKUP(time_F, lookup_F)
TIME_LOOKUP(time_V, lookup_V)
TIME_LOOKUP(time_W, lookup_W)
TIME_LOOKUP(time_Y, lookup_Y)
TIME_LOOKUP(time_X, lookup_X)
TIME_LOOKUP(time_Xc, lookup_X_canonical)
TIME_LOOKUP(time_Yc, lookup_Y_canonical)
TIME_LOOKUP(time_Yv, lookup_Y_verified)
TIME_LOOKUP(time_Xv, lookup_X_verified)
TIME_LOOKUP(time_Xs, lookup_X_search_only)

int main(int argc, char **argv) {
    const char *path = NULL;
    FrbFileInfo info;
    FrbTable table;
    Layouts y;
    Query *exact, *any;
    size_t queries = 2000000;
    uint64_t bytes_A, bytes_B, bytes_ordinals, bytes_fields = 0, bytes_ranked = 0, bytes_ranked_colors = 0, bytes_small = 0;
    uint64_t bytes_others, bytes_fixed;
    uint32_t bad = 0;
    int block_shift = 8, check = 1, L, failed = 0;
    double t0;
    FrbError e;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-check")) check = 0;
        else if (!strcmp(argv[i], "--bomb") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--queries") && i + 1 < argc) queries = (size_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--block") && i + 1 < argc) {
            int b = atoi(argv[++i]);
            block_shift = 0;
            while ((1 << block_shift) < b) block_shift++;
            if (b < 16 || (1 << block_shift) != b) path = NULL, i = argc;
        } else path = NULL, i = argc;
    }
    if (!path || queries < 1) {
        fprintf(stderr, "usage: colorless_lookup --bomb FILE [--queries N] [--block 16|32|...|4096] [--no-check]\n");
        return 2;
    }
    e = frb_file_read(path, &table, &info, &bad);
    if (e != FRB_OK || !info.complete || table.bits < 2) {
        fprintf(stderr, "colorless_lookup: %s is not a complete fixed-width Bomb\n", path);
        return 1;
    }
    L = table.bits;
    t0 = cb_now();
    build(&y, &table, block_shift);
    printf("Bomb: FixedRunBomb(%d), %s strings, table CRC-32 %08x; layouts built in memory in %.1f s\n", L,
           commas(table.count), (unsigned)info.table_crc, cb_now() - t0);

    /* ---- sizes ---- */
    bytes_fixed = frb_file_bytes(table.format, L);
    bytes_A = ((uint64_t)y.count + 7) / 8;
    bytes_ordinals = 3 * (((uint64_t)2 << L) - 2);
    bytes_others = (uint64_t)y.others * 5; /* a string and which coloring */
    {
        int present = 0;
        for (int n = 1; n < CLASSES; n++) present += y.class_count[n] > 0;
        bytes_B = (uint64_t)y.count / 2 + (uint64_t)((y.count >> y.block_shift) + 1) * (uint64_t)present * 4u;
        for (int n = 1; n < CLASSES; n++) {
            uint64_t f = (uint64_t)y.class_count[n] * y.stride[n];
            bytes_fields += f;
            if (n <= SMALL) bytes_small += f;
            else {
                bytes_ranked += ((uint64_t)y.class_count[n] * (uint64_t)y.rank_bits[n] + 7) / 8;
                bytes_ranked_colors += ((uint64_t)y.class_count[n] * (uint64_t)(y.rank_bits[n] + n) + 7) / 8;
            }
        }
    }
    printf("\nSizes in bytes (a checkpoint every %d strings)\n", 1 << y.block_shift);
    printf("  one bit for every shorter string (A)                     %15s\n", commas(bytes_A));
    printf("  node count of every string and checkpoints (B)           %15s\n", commas(bytes_B));
    printf("  an ordinal for every string of 1 to %d bits              %15s\n", L, commas(bytes_ordinals));
    printf("  REVs as bit fields (the compact format's records)        %15s\n", commas(bytes_fields));
    printf("  REVs of 6 and more nodes as rank and colors              %15s  (+ %s for 1 to 5 nodes as fields)\n",
           commas(bytes_ranked_colors), commas(bytes_small));
    printf("  REVs of 6 and more nodes as rank                         %15s  (+ %s, and %s strings listed: %s)\n",
           commas(bytes_ranked), commas(bytes_small), commas(y.others), commas(bytes_others));
    printf("\n  Layout                                   bytes     of fixed-width\n");
    printf("  F  fixed-width (+A)             %15s   %6.1f%%\n", commas(bytes_fixed + bytes_A), 100.0);
    printf("  V  v4 as built                  %15s   %6.1f%%\n", commas(bytes_ordinals + bytes_fields),
           100.0 * (double)(bytes_ordinals + bytes_fields) / (double)bytes_fixed);
    printf("  W  A+B, bit fields              %15s   %6.1f%%\n", commas(bytes_A + bytes_B + bytes_fields),
           100.0 * (double)(bytes_A + bytes_B + bytes_fields) / (double)bytes_fixed);
    printf("  Y  A+B, rank and colors         %15s   %6.1f%%\n",
           commas(bytes_A + bytes_B + bytes_ranked_colors + bytes_small),
           100.0 * (double)(bytes_A + bytes_B + bytes_ranked_colors + bytes_small) / (double)bytes_fixed);
    printf("  X  A+B, rank, colorless         %15s   %6.1f%%\n",
           commas(bytes_A + bytes_B + bytes_ranked + bytes_small + bytes_others),
           100.0 * (double)(bytes_A + bytes_B + bytes_ranked + bytes_small + bytes_others) / (double)bytes_fixed);

    /* ---- every layout gives the stored REV, for every string ---- */
    if (check) {
        unsigned long long wrong[8] = {0}, walk_differs = 0, strings = 0;
        t0 = cb_now();
        for (uint32_t t = 0;; t++) {
            ClTopo want, got;
            uint32_t want_colors = 0, colors = 0;
            entry_unpack(&table, t, &want, &want_colors);
            {
                RevGraph g; /* the decoder here against the library's */
                FrbStatus status;
                ClTopo lib;
                uint32_t lib_colors = 0;
                frb_entry_decode_as(table.format, frb_table_entry(&table, t), &status, &g);
                cl_from_graph(&g, &lib, &lib_colors);
                wrong[0] += !cl_same_topo(&lib, &want) || lib_colors != want_colors;
            }
            lookup_V(&y, L, t, &got, &colors);
            wrong[1] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_W(&y, L, t, &got, &colors);
            wrong[2] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_Y(&y, L, t, &got, &colors);
            wrong[3] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_X(&y, L, t, &got, &colors);
            wrong[4] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_X_canonical(&y, L, t, &got, &colors);
            wrong[5] += got.n != want.n || !cl_verify(&got, colors, t, L);
            lookup_Y_verified(&y, L, t, &got, &colors); /* the two a reader would use */
            wrong[6] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_X_verified(&y, L, t, &got, &colors);
            wrong[7] += !cl_same_topo(&got, &want) || colors != want_colors;
            lookup_X_search_only(&y, L, t, &got, &colors);
            wrong[7] += !cl_same_topo(&got, &want) || colors != want_colors;
            if (t == y.count - 1) break;
        }
        for (int k = 1; k < L; k++)
            for (uint32_t v = 0; v < ((uint32_t)1 << k); v++) {
                walk_differs += ordinal_at(&y, L, walk(&y, k, v)) != ordinal_at(&y, k, v);
                strings++;
            }
        printf("\nEvery string of %d bits (%s) looked up in each layout and compared with the stored REV,\n"
               "link for link and color for color (%.1f s):\n",
               L, commas(y.count), cb_now() - t0);
        printf("  V %s wrong, W %s, Y %s, X %s; X without renumbering: %s that do not emit their string\n",
               commas(wrong[1]), commas(wrong[2]), commas(wrong[3] + wrong[6]), commas(wrong[4] + wrong[7]),
               commas(wrong[5]));
        printf("  (the entry decoder of this program against the library's: %s differences)\n", commas(wrong[0]));
        printf("Every shorter string (%s): the walk along the bits ends where the ordinal rule points: %s differ\n",
               commas(strings), commas(walk_differs));
        for (int i = 0; i < 8; i++) failed |= wrong[i] != 0;
        failed |= walk_differs != 0;
    }

    /* ---- lookups ---- */
    exact = malloc(queries * sizeof *exact);
    any = malloc(queries * sizeof *any);
    if (!exact || !any) return 1;
    for (size_t i = 0; i < queries; i++) {
        exact[i].bits = (uint8_t)L;
        exact[i].value = (uint32_t)(rnd64() >> 33) & (y.count - 1u);
        any[i].bits = (uint8_t)(1 + (rnd64() >> 33) % (uint64_t)L);
        any[i].value = (uint32_t)(rnd64() >> 33) & (((uint32_t)1 << any[i].bits) - 1u);
    }
    printf("\nLookups, nanoseconds each (%zu random strings, best of %d passes)\n", queries, RUNS);
    printf("  \"REV\": unpacked into memory.  \"verified\": and run against the string asked for.  For Y and X the\n"
           "  first two columns are the REV in canonical numbering; \"as stored\" is the stored REV, verified,\n"
           "  with its numbering found by the run that verifies it.\n");
    printf("                                   strings of %d bits               strings of 1 to %d bits\n", L, L);
    printf("  Layout                           REV   verified   as stored        REV   verified   as stored\n");
    {
        typedef double (*TimeFn)(const Layouts *, const Query *, size_t, int, unsigned long long *);
        struct {
            const char *name;
            TimeFn plain, stored;
        } rows[6] = {{"F  fixed-width (+A)", time_F, NULL},
                     {"V  v4 as built", time_V, NULL},
                     {"W  A+B, bit fields", time_W, NULL},
                     {"Y  A+B, rank and colors", time_Yc, time_Yv},
                     {"X  A+B, rank, colorless", time_Xc, time_Xv},
                     {"X  search as verification", time_Xc, time_Xs}};
        unsigned long long sums[6][2], unused = 0, same[2] = {0, 0};
        for (int r = 0; r < 6; r++) {
            double ns[6];
            const Query *sets[2] = {exact, any};
            for (int q = 0; q < 2; q++) {
                ns[3 * q] = rows[r].plain(&y, sets[q], queries, 0, &unused);
                ns[3 * q + 1] = rows[r].plain(&y, sets[q], queries, 1, &unused);
                /* the stored REV, verified: one sum for all layouts */
                if (rows[r].stored) ns[3 * q + 2] = rows[r].stored(&y, sets[q], queries, 0, &sums[r][q]);
                else {
                    ns[3 * q + 2] = ns[3 * q + 1];
                    rows[r].plain(&y, sets[q], queries, 0, &sums[r][q]);
                }
            }
            printf("  %-27s %8.1f  %9.1f  %10.1f   %8.1f  %9.1f  %10.1f\n", rows[r].name, ns[0], ns[1], ns[2], ns[3],
                   ns[4], ns[5]);
            if (r == 1) same[0] = sums[1][0], same[1] = sums[1][1];
            if (r >= 2) failed |= sums[r][0] != same[0] || sums[r][1] != same[1];
        }
        /* F returns the same REVs too; only what it returns beside them (a string, not an ordinal) differs */
        (void)time_X;
        (void)time_Y;
    }

    /* ---- B by itself: the ordinal of a string of L bits ---- */
    printf("\nThe ordinal of a string of %d bits, nanoseconds (the same random strings)\n", L);
    printf("  an ordinal stored for every string (3 bytes)             ");
    {
        double best = 1e30;
        unsigned long long s = 0;
        for (int run = 0; run < RUNS; run++) {
            double t1 = cb_now();
            s = 0;
            for (size_t i = 0; i < queries; i++) s += ordinal_at(&y, L, exact[i].value);
            t1 = cb_now() - t1;
            if (t1 < best) best = t1;
        }
        printf("%7.1f   %15s bytes\n", best / (double)queries * 1e9, commas(3 * (uint64_t)y.count));
        for (int shift = 4; shift <= 12; shift += 2) {
            unsigned long long s2 = 0;
            int present = 0;
            build_checkpoints(&y, shift);
            for (int n = 1; n < CLASSES; n++) present += y.class_count[n] > 0;
            best = 1e30;
            for (int run = 0; run < RUNS; run++) {
                double t1 = cb_now();
                s2 = 0;
                for (size_t i = 0; i < queries; i++) {
                    uint32_t t = exact[i].value, n = class_at(&y, t);
                    s2 += y.class_start[n] + rank_in_class(&y, t, n);
                }
                t1 = cb_now() - t1;
                if (t1 < best) best = t1;
            }
            printf("  node counts in 4 bits, a checkpoint every %4d strings   %7.1f   %15s bytes%s\n", 1 << shift,
                   best / (double)queries * 1e9,
                   commas((uint64_t)y.count / 2 + (uint64_t)((y.count >> shift) + 1) * (uint64_t)present * 4u),
                   s2 == s ? "" : "   WRONG ORDINALS");
            failed |= s2 != s;
        }
    }
    printf("\n%s\n", failed ? "FAIL" : "PASS: every layout gives the stored REVs");
    free(exact);
    free(any);
    destroy(&y);
    frb_table_destroy(&table);
    return failed;
}
