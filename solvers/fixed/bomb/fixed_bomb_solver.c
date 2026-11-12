/*
 * Bomb-backed FixedSolver.  See fixed_bomb_solver.h and README.md.
 *
 * The prefix index is a binary tree laid out as a heap.  The string of L
 * bits with value v (first bit most significant) is node 2^L + v, so the
 * empty string is node 1, the children of node k are 2k (append 0) and
 * 2k + 1 (append 1), and the B-bit strings would be nodes 2^B .. 2^(B+1)-1.
 * Those leaves are the Bomb's own entries and are not stored again: best[]
 * covers nodes 1 .. 2^B - 1 and holds, for each, the table index of the
 * chosen B-bit extension.  2^B numbers of 16 bits: 128 KiB for B = 16.  A
 * Bomb of more than 16 bits has indices that need 32: 64 MiB for B = 24.
 */
#include "fixed_bomb_solver.h"

#include <stdlib.h>
#include <string.h>

struct RevFixedBomb {
    FrbTable table;   /* the Bomb's entries, byte for byte as in the file */
    uint16_t *best16; /* table.count numbers, [0] unused: the index of a Bomb of up to 16 bits */
    uint32_t *best32; /* the same for a longer Bomb; exactly one of the two is used */
    size_t entry_bytes, graph_bytes; /* of the table's format */
    int max_nodes;    /* the largest minimum in the table */
};

static const uint8_t *entry_at(const RevFixedBomb *b, uint32_t index) {
    return b->table.entries + (size_t)index * b->entry_bytes;
}

/* Node count of a table entry: the low five bits of its metadata byte. */
static int entry_nodes(const RevFixedBomb *b, uint32_t index) {
    return entry_at(b, index)[b->graph_bytes] & 31;
}

static uint32_t best_of(const RevFixedBomb *b, uint32_t node) {
    return b->best16 ? b->best16[node] : b->best32[node];
}

static void build_index(RevFixedBomb *b) {
    uint32_t count = b->table.count;
    for (uint32_t node = count - 1; node >= 1; node--) {
        uint32_t zero = 2 * node, one = zero + 1;
        uint32_t a = zero >= count ? zero - count : best_of(b, zero);
        uint32_t c = one >= count ? one - count : best_of(b, one);
        /* Every extension through the 0-child has a lower index than every
         * extension through the 1-child, so keeping `a` on a tie keeps the
         * lowest index. */
        uint32_t chosen = entry_nodes(b, c) < entry_nodes(b, a) ? c : a;
        if (b->best16) b->best16[node] = (uint16_t)chosen;
        else b->best32[node] = chosen;
    }
}

/* 1 <= bits <= B and value < 2^bits. */
static int in_range(const RevFixedBomb *b, int bits, uint32_t value) {
    return b && bits >= 1 && bits <= b->table.bits && value < ((uint32_t)1 << bits);
}

static uint32_t select_extension(const RevFixedBomb *b, int bits, uint32_t value) {
    return bits == b->table.bits ? value : best_of(b, ((uint32_t)1 << bits) + value);
}

static FrbError adopt(FrbTable *table, int validated, RevFixedBomb **out, uint32_t *bad_index) {
    RevFixedBomb *b;
    uint32_t count, mask;
    size_t entry_bytes, graph_bytes;
    FrbError e;
    if (!out) return FRB_ERR_ARG;
    *out = NULL;
    if (!table || !table->entries || !table->format) return FRB_ERR_ARG;
    entry_bytes = (size_t)table->format->entry_bytes;
    graph_bytes = (size_t)table->format->graph_bytes;
    if (!validated) {
        e = frb_table_validate(table, NULL, bad_index);
        if (e != FRB_OK) return e;
    }
    count = table->count;
    mask = count - 1u;
    /* Certified minima only: a witness that is merely valid answers nothing here. */
    for (uint32_t i = 0; i < count; i++) {
        unsigned meta = table->entries[(size_t)i * entry_bytes + graph_bytes];
        if ((meta >> 5) != (unsigned)FRB_MINIMAL) {
            if (bad_index) *bad_index = i;
            return FRB_ERR_INCOMPLETE;
        }
    }
    /* A string and its complement need the same number of nodes. */
    for (uint32_t i = 0; i < count; i++) {
        unsigned n = table->entries[(size_t)i * entry_bytes + graph_bytes] & 31u;
        unsigned c = table->entries[(size_t)(i ^ mask) * entry_bytes + graph_bytes] & 31u;
        if (n != c) {
            if (bad_index) *bad_index = i;
            return FRB_ERR_EVIDENCE;
        }
    }

    b = calloc(1, sizeof *b);
    if (!b) return FRB_ERR_MEMORY;
    /* Node 0 does not exist and stays 0. */
    if (table->bits <= 16) b->best16 = calloc(count, sizeof *b->best16);
    else b->best32 = calloc(count, sizeof *b->best32);
    if (!b->best16 && !b->best32) {
        free(b);
        return FRB_ERR_MEMORY;
    }
    b->entry_bytes = entry_bytes;
    b->graph_bytes = graph_bytes;
    b->table = *table; /* the storage changes hands */
    table->entries = NULL;
    table->count = 0;
    table->bits = 0;
    build_index(b);
    for (uint32_t i = 0; i < count; i++)
        if (entry_nodes(b, i) > b->max_nodes) b->max_nodes = entry_nodes(b, i);
    *out = b;
    return FRB_OK;
}

FrbError rev_fixed_bomb_from_table(FrbTable *table, RevFixedBomb **bomb, uint32_t *bad_index) {
    return adopt(table, 0, bomb, bad_index);
}

FrbError rev_fixed_bomb_open(const char *path, RevFixedBomb **bomb, uint32_t *bad_index) {
    FrbTable table;
    FrbFileInfo info;
    FrbError e;
    if (!bomb) return FRB_ERR_ARG;
    *bomb = NULL;
    e = frb_file_read(path, &table, &info, bad_index); /* every check of FORMAT.md section 6 */
    if (e != FRB_OK) return e;
    e = adopt(&table, 1, bomb, bad_index);
    if (e != FRB_OK) frb_table_destroy(&table);
    return e;
}

void rev_fixed_bomb_reindex(RevFixedBomb *bomb) {
    if (bomb) build_index(bomb);
}

void rev_fixed_bomb_close(RevFixedBomb *bomb) {
    if (!bomb) return;
    frb_table_destroy(&bomb->table);
    free(bomb->best16);
    free(bomb->best32);
    free(bomb);
}

int rev_fixed_bomb_bits(const RevFixedBomb *bomb) { return bomb ? bomb->table.bits : 0; }

int rev_fixed_bomb_max_nodes(const RevFixedBomb *bomb) { return bomb ? bomb->max_nodes : 0; }

int rev_fixed_bomb_min_nodes(const RevFixedBomb *bomb, int bits, uint32_t value) {
    if (!in_range(bomb, bits, value)) return 0;
    return entry_nodes(bomb, select_extension(bomb, bits, value));
}

uint32_t rev_fixed_bomb_extension(const RevFixedBomb *bomb, int bits, uint32_t value) {
    if (!in_range(bomb, bits, value)) return 0;
    return select_extension(bomb, bits, value);
}

void rev_fixed_bomb_histogram(const RevFixedBomb *bomb, int bits,
                              uint32_t counts[FRB_MAX_NODES + 1]) {
    if (!counts) return;
    memset(counts, 0, (FRB_MAX_NODES + 1) * sizeof counts[0]);
    if (!bomb || bits < 1 || bits > bomb->table.bits) return;
    for (uint32_t v = 0; v < ((uint32_t)1 << bits); v++)
        counts[entry_nodes(bomb, select_extension(bomb, bits, v))]++;
}

const char *rev_fixed_bomb_result_str(RevFixedBombResult r) {
    switch (r) {
    case REV_FIXED_BOMB_MINIMAL: return "MINIMAL";
    case REV_FIXED_BOMB_PROVEN_UNSAT: return "PROVEN_UNSAT";
    case REV_FIXED_BOMB_UNSUPPORTED: return "UNSUPPORTED";
    case REV_FIXED_BOMB_INVALID_INPUT: return "INVALID_INPUT";
    case REV_FIXED_BOMB_INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "?";
}

/* The first `bits` characters of a validated target as a number. */
static uint32_t leading_value(const char *target, int bits) {
    uint32_t v = 0;
    for (int i = 0; i < bits; i++) v = (v << 1) | (uint32_t)(target[i] - '0');
    return v;
}

/*
 * Lower bound for a validated target of more than B characters.
 *
 * Its first B bits must be emitted by any REV for it: their minimum is a
 * bound.  And a REV of n nodes for the target yields one of n + 1 nodes for
 * the target without its first j bits (README.md), which in turn emits the
 * first B bits of that remainder, or all of it if it is shorter: each such
 * minimum, less one, is a bound too.
 */
static int lower_bound_long(const RevFixedBomb *b, const char *target, size_t len) {
    int bits = b->table.bits;
    uint32_t mask = b->table.count - 1u;
    uint32_t window = leading_value(target, bits);
    int bound = entry_nodes(b, window);
    for (size_t j = 1; j + (size_t)bits <= len; j++) {
        window = ((window << 1) | (uint32_t)(target[j + (size_t)bits - 1] - '0')) & mask;
        if (entry_nodes(b, window) - 1 > bound) bound = entry_nodes(b, window) - 1;
    }
    /* `window` is now the last B characters; what follows are the ends of
     * the target that are shorter than B. */
    for (int tail = bits - 1; tail >= 1; tail--) {
        uint32_t value = window & (((uint32_t)1 << tail) - 1u);
        int n = entry_nodes(b, select_extension(b, tail, value)) - 1;
        if (n > bound) bound = n;
    }
    return bound;
}

int rev_fixed_bomb_lower_bound(const RevFixedBomb *bomb, const char *target, size_t target_len) {
    if (!bomb || rev_target_validate(target, target_len) != REV_OK) return 0;
    if (target_len > (size_t)bomb->table.bits) return lower_bound_long(bomb, target, target_len);
    return entry_nodes(bomb, select_extension(bomb, (int)target_len,
                                               leading_value(target, (int)target_len)));
}

RevFixedBombResult rev_fixed_bomb_solve(const RevFixedBomb *bomb, const char *target,
                                        size_t target_len, int max_nodes, RevGraph *result,
                                        RevFixedBombInfo *info) {
    RevFixedBombInfo local_info;
    RevGraph local_graph;
    uint32_t extension;
    int bits, nodes;

    if (!info) info = &local_info;
    memset(info, 0, sizeof *info);
    if (result) result->node_count = 0;
    if (!bomb || max_nodes < 0 || rev_target_validate(target, target_len) != REV_OK)
        return REV_FIXED_BOMB_INVALID_INPUT;
    info->bomb_bits = bomb->table.bits;

    if (target_len > (size_t)bomb->table.bits) {
        info->lower_bound = lower_bound_long(bomb, target, target_len);
        return REV_FIXED_BOMB_UNSUPPORTED;
    }
    bits = (int)target_len;
    extension = select_extension(bomb, bits, leading_value(target, bits));
    nodes = entry_nodes(bomb, extension);
    info->min_nodes = nodes;
    info->lower_bound = nodes;
    info->extension = extension;
    if (max_nodes != 0 && nodes > max_nodes) return REV_FIXED_BOMB_PROVEN_UNSAT;

    /* The table was run in full when it was opened.  Run this one graph
     * again, against the target as the caller wrote it. */
    if (!result) result = &local_graph;
    memset(result, 0, sizeof *result);
    if (frb_graph_unpack_as(bomb->table.format, entry_at(bomb, extension), nodes, result) != FRB_OK ||
        rev_verify_prefix(result, target, target_len, NULL) != REV_OK) {
        result->node_count = 0;
        return REV_FIXED_BOMB_INTERNAL_ERROR;
    }
    return REV_FIXED_BOMB_MINIMAL;
}
