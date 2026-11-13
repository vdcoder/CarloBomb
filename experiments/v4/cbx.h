/*
 * CBX: a candidate layout for the compact Bomb (v4), as a file.  PROTOTYPE.
 *
 * This is not format version 4 and not a FixedRunBomb file: it has its own
 * magic, it is written with the byte order and structure layout of the
 * machine it is made on, and nothing about it is promised to stay.  It
 * exists so that the candidate features can be measured end to end, on
 * files, before a format is decided.  PR 8 and every existing format are
 * untouched by it.
 *
 * A CBX file is made offline from a complete fixed-width Bomb, which is
 * only read.  It holds:
 *
 *   A  one bit for every stored string of 1 to L-1 bits, the navigation
 *      bit: 0 if an extension of the string with the fewest nodes lies
 *      under its extension by 0, 1 if only under its extension by 1.
 *      Followed from a string the bits lead to an L-bit extension of it
 *      with the fewest nodes: the lowest of them.  Nothing else is stored
 *      about shorter strings.  Their node count is the node count of the
 *      string the bits lead to; and which of the two extensions of a string
 *      needs fewer nodes, or neither, is found on demand by following both
 *      and comparing the two node counts (cbx_compare).  (CTO decision of
 *      13 November 2026: store what is essential, derive the rest.)
 *   B  the node count of every stored string of L bits in 4 bits, and the
 *      number of strings of each node count before every 2^k-th string.
 *      The REVs are kept by node count in the order of their strings, so
 *      the place of a string's REV is the number of strings of its node
 *      count before it: a checkpoint plus a count within one block.
 *   the REVs, one record per stored string, no two strings sharing one.
 *
 * and three switches, the features under review:
 *
 *   complement  Only strings that start with 0 are stored: in A, in B and
 *               in the REVs.  A string that starts with 1 is answered by
 *               the complement of the answer of its complement: its node
 *               count is the same, the extension the bits lead to is the
 *               complement of the other's, and its REV is the complement
 *               (colors inverted, ports exchanged) of the other's.  Requires
 *               a Bomb in which that holds REV for REV; the writer checks
 *               every pair and refuses otherwise.  The extension is again
 *               one with the fewest nodes; among equals it is the highest
 *               where for a string that starts with 0 it is the lowest.
 *   colors      Kept: N bits with each REV.  Dropped: the colors are found
 *               from the topology and the string when the REV is asked for
 *               (cl_recolor).  Where a color has to be guessed, the first
 *               bit of the string is tried first: that is the order of the
 *               search that found the REV (white first, and the REV of a
 *               string that starts with 1 is a complement), so the first
 *               coloring found is the stored one except for a few REVs that
 *               did not come from the search.  Where several colorings emit
 *               the string the first one found is returned; it has the same
 *               nodes and links and emits the same L bits as the stored
 *               REV, and may go on differently afterwards.  No list of such
 *               strings is kept: by the owner's decision of 13 November
 *               2026 either coloring is a good answer.  REVs of up to 5
 *               nodes keep their colors in any case: they are a thousandth
 *               of a Bomb, and most were found by the enumerator, whose
 *               order the reader's search does not follow.
 *   encoding    How the links of a REV of 6 or more nodes are written:
 *               FIELDS   2N numbers of ceil(log2 N) bits, stored numbering
 *               BASE_N   the 2N links as one number in base N ("hyper-bit")
 *               RANK     the rank of the canonical topology among all
 *                        canonical topologies of N nodes; the reader gives
 *                        the nodes back the numbering of the stored REV
 *                        (the order in which the machine reaches them)
 *               REVs of up to 5 nodes are always FIELDS: many of them were
 *               numbered by the enumerator, not by the search.
 *
 * With the colors kept every lookup returns the stored REV, link for link
 * and color for color, whatever the other switches.
 */
#ifndef CARLOBOMB_CBX_H
#define CARLOBOMB_CBX_H

#include <stdint.h>
#include <string.h>

#include "colorless.h"
#include "fixed_run.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CBX_HEADER_BYTES 512
#define CBX_MAX_NODES 12 /* the rank tables reach 12 nodes; minima to 31 bits have fewer */
#define CBX_CLASSES (CBX_MAX_NODES + 1)
#define CBX_SMALL 5
#define CBX_LAYOUT 3
#define CBX_TIE 2 /* cbx_compare: the two extensions need the same number of nodes */

typedef enum { CBX_FIELDS = 0, CBX_BASE_N = 1, CBX_RANK = 2 } CbxEncoding;

typedef struct {
    int complement;       /* 1: one string of each complement pair */
    int colors;           /* 1: kept; 0: found by search at lookup */
    CbxEncoding encoding;
    int checkpoint_shift; /* a checkpoint every 2^shift strings; 4..12 */
} CbxOptions;

typedef enum {
    CBX_OK = 0,
    CBX_ERR_ARG,
    CBX_ERR_IO,
    CBX_ERR_MEMORY,
    CBX_ERR_EXISTS,        /* the output file is there already */
    CBX_ERR_NOT_COMPLETE,  /* an entry of the source is not a certified minimum */
    CBX_ERR_NODES,         /* a REV of more nodes than this prototype takes */
    CBX_ERR_NOT_SYMMETRIC, /* complement asked for, and a pair is not a REV and its complement */
    CBX_ERR_NUMBERING,     /* RANK with colors: a REV does not come back by the numbering rule */
    CBX_ERR_FORMAT,        /* not a CBX file of this layout, or damaged */
    CBX_ERR_CHECKSUM
} CbxError;

const char *cbx_error_str(CbxError e);
const char *cbx_encoding_str(CbxEncoding e);

typedef struct {
    int bits, max_nodes;
    CbxOptions opt;
    uint32_t strings;                  /* 2^L */
    uint32_t stored;                   /* strings with a REV in the file */
    uint32_t class_count[CBX_CLASSES]; /* stored REVs by node count */
    uint8_t topo_bits[CBX_CLASSES];    /* bits of the links of one REV */
    uint8_t record_bits[CBX_CLASSES];  /* with the colors, if kept */
    uint64_t nav_offset, classes_offset, checkpoint_offset, pool_offset[CBX_CLASSES];
    uint64_t nav_bytes, classes_bytes, checkpoint_bytes, pool_bytes, file_bytes;
    uint32_t source_crc, body_crc;
    int source_version;
} CbxInfo;

/* An opened file: the whole of it mapped read-only. */
typedef struct {
    CbxInfo info;
    const uint8_t *base;
    size_t mapped;
    const uint64_t *nav;
    const uint64_t *classes;
    const uint32_t *checkpoint;
    const uint8_t *pool[CBX_CLASSES];
    uint32_t mask; /* 2^L - 1 */
} Cbx;

/*
 * Write the CBX file of a complete table.  The table is only read.  `path`
 * must not exist.  The file is built in memory, written under a temporary
 * name and renamed.
 */
CbxError cbx_write(const char *path, const FrbTable *table, uint32_t source_crc, int source_version,
                   const CbxOptions *opt, CbxInfo *info);

/* Map a file and check its header and length.  The body checksum is not
 * verified here (cbx_check_body does that). */
CbxError cbx_open(const char *path, Cbx *c);
void cbx_close(Cbx *c);
CbxError cbx_check_body(const Cbx *c);

/* What a file gives for every string, against the Bomb it was made from. */
typedef struct {
    uint64_t strings;          /* strings of L bits */
    uint64_t wrong_nodes;      /* node count other than the Bomb's */
    uint64_t not_emitting;     /* no REV, or one that does not emit the string */
    uint64_t not_stored;       /* a REV other than the stored one (another coloring) */
    uint64_t not_stored_by_nodes[CBX_CLASSES]; /* the same by node count */
    uint64_t next_bit_differs; /* of those: bit L+1 other than the stored REV's */
    uint64_t shorter;          /* strings of 1 to L-1 bits */
    uint64_t not_minimal;      /* answered by a string that is no extension of theirs with the fewest nodes */
    uint64_t wrong_extension;  /* by another such extension than the rule gives (lowest; highest in the mirror) */
    uint64_t wrong_comparison; /* strings of 0 to L-1 bits: cbx_compare other than the table says */
} CbxCheck;

CbxError cbx_check_against(const Cbx *c, const FrbTable *table, CbxCheck *check);

/* ---- lookups ------------------------------------------------------------------ */

static inline unsigned cbx_link_bits(unsigned n) {
    unsigned w = 0;
    while ((1u << w) < n) w++;
    return w;
}

/* A navigation bit: 0, the answer of the string lies under its extension by 0; 1, under its extension by 1. */
static inline unsigned cbx_nav_at(const Cbx *c, uint64_t place) {
    return (unsigned)(c->nav[place >> 6] >> (place & 63u)) & 1u;
}

/*
 * The L-bit string that answers the string of `bits` bits (0 to L): itself,
 * or an extension of it with the fewest nodes, found by following one bit a
 * level.  Among several such extensions it is the lowest; in a halved file,
 * for a string that starts with 1, the highest: the complement of the answer
 * of its complement.
 */
static inline uint32_t cbx_leaf(const Cbx *c, int bits, uint32_t value) {
    int L = c->info.bits;
    uint32_t mirror;
    uint64_t h;
    if (!c->info.opt.complement) {
        h = ((uint64_t)1 << bits) + value;
        for (int k = bits; k < L; k++) h = 2 * h + cbx_nav_at(c, h);
        return (uint32_t)(h - ((uint64_t)1 << L));
    }
    if (bits == 0) bits = 1, value = 0;             /* 0 and 1 are complements and need the same: the answer under 0 */
    mirror = 0u - ((value >> (bits - 1)) & 1u);     /* all ones for a string that starts with 1 */
    value ^= mirror & (((uint32_t)1 << bits) - 1u); /* then its complement, which starts with 0 */
    h = ((uint64_t)1 << (bits - 1)) + value;        /* the bits of the strings that start with 0, without that 0 */
    for (int k = bits; k < L; k++) h = 2 * h + cbx_nav_at(c, h);
    return (uint32_t)(h - ((uint64_t)1 << (L - 1))) ^ (mirror & c->mask); /* and the complement of its answer */
}

/* Where the REV of an L-bit string is kept: the string, or its complement. */
static inline uint32_t cbx_stored_string(const Cbx *c, uint32_t leaf, int *flip) {
    uint32_t f = (uint32_t)(c->info.opt.complement != 0) & (leaf >> (c->info.bits - 1));
    *flip = (int)f;
    return leaf ^ ((0u - f) & c->mask);
}

static inline uint32_t cbx_class_at(const Cbx *c, uint32_t s) {
    return (uint32_t)(c->classes[s >> 4] >> (4 * (s & 15u))) & 15u;
}

/* The certified minimum node count of a string of 1 to L bits.  Index only:
 * no REV is read and nothing is recolored. */
static inline int cbx_nodes(const Cbx *c, int bits, uint32_t value) {
    int flip;
    uint32_t leaf = bits == c->info.bits ? value : cbx_leaf(c, bits, value);
    return (int)cbx_class_at(c, cbx_stored_string(c, leaf, &flip));
}

/*
 * On demand: of the two extensions of a string by one bit, which needs fewer
 * nodes?  0 or 1, or CBX_TIE if they need the same.  bits is 0 to L-1.
 *
 * Nothing is stored for this question.  Each extension is followed to the
 * L-bit string that answers it and the node count read there: the fewest
 * nodes of a string are the fewest among its L-bit extensions.  No REV is
 * read and no color found.  `nodes`, if not NULL, receives the two counts.
 */
static inline int cbx_compare(const Cbx *c, int bits, uint32_t value, int nodes[2]) {
    int n0 = cbx_nodes(c, bits + 1, 2 * value), n1 = cbx_nodes(c, bits + 1, 2 * value + 1);
    if (nodes) nodes[0] = n0, nodes[1] = n1;
    return n0 < n1 ? 0 : n1 < n0 ? 1 : CBX_TIE;
}

static inline uint32_t cbx_count_equal(uint64_t w, uint64_t pattern, unsigned limit) {
    uint64_t x = w ^ pattern;
    uint64_t differs = (x | (x >> 1) | (x >> 2) | (x >> 3)) & 0x1111111111111111ull;
    if (limit < 16) differs |= 0x1111111111111111ull << (4 * limit);
    return 16u - (uint32_t)__builtin_popcountll(differs);
}

/* How many stored strings before s have the node count n. */
static inline uint32_t cbx_rank(const Cbx *c, uint32_t s, uint32_t n) {
    int shift = c->info.opt.checkpoint_shift;
    uint32_t block = s >> shift;
    uint32_t r = c->checkpoint[(size_t)block * (size_t)c->info.max_nodes + (n - 1)];
    uint64_t pattern = 0x1111111111111111ull * n;
    uint32_t w = (block << shift) >> 4, last = s >> 4;
    for (; w < last; w++) r += cbx_count_equal(c->classes[w], pattern, 16);
    return r + cbx_count_equal(c->classes[last], pattern, s & 15u);
}

static inline ClU128 cbx_record(const uint8_t *pool, uint64_t index, unsigned width) {
    uint64_t bit = index * (uint64_t)width, lo, hi;
    const uint8_t *p = pool + (bit >> 3);
    memcpy(&lo, p, sizeof lo);
    if (width <= 57) return (lo >> (bit & 7u)) & (((uint64_t)1 << width) - 1u);
    memcpy(&hi, p + 8, sizeof hi);
    return ((((ClU128)hi << 64) | lo) >> (bit & 7u)) & ((((ClU128)1) << width) - 1u);
}

/* Links as 2n numbers of ceil(log2 n) bits: node 0 port 0, node 0 port 1, ... */
static inline void cbx_unpack_fields(ClU128 code, unsigned n, ClTopo *t) {
    unsigned w = cbx_link_bits(n);
    uint32_t mask = (1u << w) - 1u;
    t->n = (uint8_t)n;
    if (2 * n * w <= 64) {
        uint64_t v = (uint64_t)code;
        for (unsigned i = 0; i < n; i++) {
            t->link[0][i] = (uint8_t)(v & mask);
            v >>= w;
            t->link[1][i] = (uint8_t)(v & mask);
            v >>= w;
        }
    } else {
        for (unsigned i = 0; i < n; i++) {
            t->link[0][i] = (uint8_t)((uint32_t)code & mask);
            code >>= w;
            t->link[1][i] = (uint8_t)((uint32_t)code & mask);
            code >>= w;
        }
    }
}

/* Digits in base N, least significant first; the divisor is a constant in
 * each of these, which the compiler turns into a multiplication. */
#define CBX_DIGITS(N) \
    static inline void cbx_digits_##N(uint64_t code, unsigned count, uint8_t *out) { \
        for (unsigned i = 0; i < count; i++) { \
            out[i] = (uint8_t)(code % N); \
            code /= N; \
        } \
    }
CBX_DIGITS(2)
CBX_DIGITS(3)
CBX_DIGITS(4)
CBX_DIGITS(5)
CBX_DIGITS(6)
CBX_DIGITS(7)
CBX_DIGITS(8)
CBX_DIGITS(9)
CBX_DIGITS(10)
CBX_DIGITS(11)
CBX_DIGITS(12)
#undef CBX_DIGITS

static inline void cbx_digits(uint64_t code, unsigned n, unsigned count, uint8_t *out) {
    switch (n) {
    case 2: cbx_digits_2(code, count, out); break;
    case 3: cbx_digits_3(code, count, out); break;
    case 4: cbx_digits_4(code, count, out); break;
    case 5: cbx_digits_5(code, count, out); break;
    case 6: cbx_digits_6(code, count, out); break;
    case 7: cbx_digits_7(code, count, out); break;
    case 8: cbx_digits_8(code, count, out); break;
    case 9: cbx_digits_9(code, count, out); break;
    case 10: cbx_digits_10(code, count, out); break;
    case 11: cbx_digits_11(code, count, out); break;
    case 12: cbx_digits_12(code, count, out); break;
    default:
        for (unsigned i = 0; i < count; i++) out[i] = 0;
        break;
    }
}

/* Bits of a number below n^digits. */
static inline unsigned cbx_bits_of_power(unsigned n, unsigned digits) {
    ClU128 limit = 1;
    unsigned bits = 0;
    for (unsigned i = 0; i < digits; i++) limit *= n;
    while (bits < 127 && (((ClU128)1) << bits) < limit) bits++;
    return bits;
}

/* The 2n links as one number in base n while that fits 64 bits (n <= 9);
 * above, as two numbers of n links each, side by side. */
static inline unsigned cbx_base_n_half_bits(unsigned n) { return n <= 9 ? 0 : cbx_bits_of_power(n, n); }

static inline void cbx_unpack_base_n(ClU128 code, unsigned n, ClTopo *t) {
    uint8_t d[2 * CBX_MAX_NODES];
    if (n > CBX_MAX_NODES) n = CBX_MAX_NODES; /* never: the caller has checked the node count */
    t->n = (uint8_t)n;
    if (n <= 9) {
        cbx_digits((uint64_t)code, n, 2 * n, d);
    } else {
        unsigned half = cbx_base_n_half_bits(n);
        cbx_digits((uint64_t)(code & ((((ClU128)1) << half) - 1u)), n, n, d);
        cbx_digits((uint64_t)(code >> half), n, n, d + n);
    }
    for (unsigned i = 0; i < n; i++) {
        t->link[0][i] = d[2 * i];
        t->link[1][i] = d[2 * i + 1];
    }
}

/* One run of L bits on a canonical REV: checks that it emits the string and
 * gives the nodes the numbering of the stored REV (the order in which the
 * machine reaches them).  0 if a bit is wrong or a node is never reached. */
static inline int cbx_verify_and_renumber(const ClTopo *canon, uint32_t colors, uint32_t target, int L, ClTopo *out,
                                          uint32_t *colors_out) {
    uint8_t perm[CL_MAX_NODES];
    unsigned fp = 0, cp = 0, count = 1;
    uint32_t seen = 1u, col = 0;
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
    if (count != canon->n) return 0;
    out->n = canon->n;
    for (int i = 0; i < canon->n; i++) {
        out->link[0][perm[i]] = perm[canon->link[0][i]];
        out->link[1][perm[i]] = perm[canon->link[1][i]];
        col |= ((colors >> i) & 1u) << perm[i];
    }
    *colors_out = col;
    return 1;
}

/*
 * The REV for a string of 1 to L bits: the REV of the L-bit string that
 * answers it (*leaf, if not NULL).  Returns 1 with the REV, or 0 if the file
 * yields none that emits the string (damage).
 *
 * With colors kept the REV is the stored one.  With colors dropped it is the
 * stored topology with the first coloring the search finds; the search runs
 * the machine against the string, so the REV emits it by construction.
 */
static inline int cbx_lookup(const Cbx *c, int bits, uint32_t value, ClTopo *t, uint32_t *colors, uint32_t *leaf_out) {
    const CbxInfo *info = &c->info;
    int L = info->bits, flip, ok = 1;
    uint32_t leaf = bits == L ? value : cbx_leaf(c, bits, value);
    uint32_t s = cbx_stored_string(c, leaf, &flip);
    uint32_t n = cbx_class_at(c, s), col = 0;
    unsigned first = (s >> (L - 1)) & 1u; /* the color the search that found the REV tried first */
    ClU128 rec;
    if (leaf_out) *leaf_out = leaf;
    if (n < 1 || n > (uint32_t)info->max_nodes) return 0;
    rec = cbx_record(c->pool[n], cbx_rank(c, s, n), info->record_bits[n]);
    if (info->opt.colors || n <= CBX_SMALL) {
        col = (uint32_t)rec & ((1u << n) - 1u);
        rec >>= n;
    }
    if (n <= CBX_SMALL) {
        cbx_unpack_fields(rec, n, t); /* as stored, with its colors */
    } else if (info->opt.encoding != CBX_RANK) {
        if (info->opt.encoding == CBX_FIELDS) cbx_unpack_fields(rec, n, t);
        else cbx_unpack_base_n(rec, n, t);
        if (!info->opt.colors) ok = cl_recolor(t, s, L, first, &col, NULL);
    } else {
        ClTopo canon;
        if (!cl_canon_unrank((int)n, (uint64_t)rec, &canon)) return 0;
        if (info->opt.colors) {
            ok = cbx_verify_and_renumber(&canon, col, s, L, t, &col);
        } else {
            uint8_t perm[CL_MAX_NODES];
            uint32_t found = 0;
            ok = cl_recolor_order(&canon, s, L, first, &found, perm);
            if (ok) cl_relabel(&canon, found, perm, t, &col);
        }
    }
    if (!ok) return 0;
    if (flip) { /* the complement: the other colors, the other ports */
        for (unsigned i = 0; i < n; i++) {
            uint8_t l0 = t->link[0][i];
            t->link[0][i] = t->link[1][i];
            t->link[1][i] = l0;
        }
        col ^= (1u << n) - 1u;
    }
    *colors = col;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
