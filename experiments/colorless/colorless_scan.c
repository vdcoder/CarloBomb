/*
 * colorless_scan: every REV of a complete fixed-width Bomb, looked at as a
 * topology and a coloring.
 *
 *   colorless_scan --bomb FILE [--brute] [--data FILE]
 *
 * The Bomb is opened for reading and validated in full by the ordinary
 * reader; nothing is written to it or next to it.  For each of its 2^L
 * strings the stored minimum-node REV is taken apart, and counted:
 *
 *   1. Is every node reachable, and reached within the L bits?  Is the REV
 *      numbered the way the LevelSolver numbers (nodes in the order the
 *      machine reaches them), are the links it never follows 0, and does it
 *      come back from its canonical form by that rule?
 *   2. Its canonical topology: how many different ones are there for each
 *      node count, how many strings share one, what does it take to name one.
 *   3. Recoloring: every coloring of the topology that emits the string.
 *      How many are there, is the first one the search finds the original,
 *      and do the others go on differently after bit L?  How many machine
 *      steps does the search take?
 *
 * --brute also runs all 2^N colorings of every topology as a reference for
 * the number of solutions (minutes at 24 bits).  --data writes the per-class
 * numbers as "key value" lines for colorless_store.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colorless.h"
#include "fixed_run.h"

enum { CLASSES = CL_MAX_NODES + 1, STATE_CAP = 1 << 15, K_CAP = 64, TOP = 8 };

/* ---- the set of topologies --------------------------------------------------- */

typedef struct {
    ClKey key;        /* hi == 0: empty (a key has its node count in the top byte) */
    uint32_t count;   /* strings whose REV has this topology */
    uint32_t count0;  /* of them, strings that begin with 0 */
    uint32_t first;   /* the first such string */
    uint32_t ambiguous; /* of them, strings with more than one coloring */
} Slot;

static Slot *slots;
static uint64_t slot_mask, slot_used;

static uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static uint64_t hash_key(ClKey k) { return mix(k.lo ^ mix(k.hi)); }

static Slot *slot_find(ClKey k, int insert);

static void slots_grow(void) {
    Slot *old = slots;
    uint64_t old_mask = slot_mask;
    slot_mask = old ? 2 * (old_mask + 1) - 1 : (1u << 16) - 1;
    slots = calloc((size_t)slot_mask + 1, sizeof *slots);
    if (!slots) {
        fprintf(stderr, "colorless_scan: out of memory for the set of topologies\n");
        exit(1);
    }
    slot_used = 0;
    if (old) {
        for (uint64_t i = 0; i <= old_mask; i++)
            if (old[i].key.hi) *slot_find(old[i].key, 1) = old[i];
        free(old);
    }
}

static Slot *slot_find(ClKey k, int insert) {
    uint64_t at;
    if (!slots || (insert && 10 * (slot_used + 1) > 6 * (slot_mask + 1))) slots_grow();
    at = hash_key(k) & slot_mask;
    while (slots[at].key.hi) {
        if (slots[at].key.hi == k.hi && slots[at].key.lo == k.lo) return &slots[at];
        at = (at + 1) & slot_mask;
    }
    if (!insert) return NULL;
    slots[at].key = k;
    slot_used++;
    return &slots[at];
}

/* ---- what is counted ------------------------------------------------------------ */

typedef struct {
    uint64_t strings;
    /* the stored REV */
    uint64_t unreachable, unvisited, bfs_numbered, solver_numbered, restored;
    uint64_t ports, ports_unused, unused_not_zero, output_changed;
    /* recoloring */
    uint64_t one_coloring, two_colorings, more_colorings, colorings_total, max_colorings;
    uint64_t free_nodes, original_missing, brute_differs;
    uint64_t next_bit_open, within_8_open, within_64_open, stream_open;
    uint64_t first0_is_original, first1_is_original, rank_bits_x1000;
    uint64_t first0_next_differs, first0_stream_differs;
    uint64_t states0_total, states1_total, states_all_total, branches0_total, branches_all_total;
    uint32_t states0_max, states_all_max, branches0_max;
    uint32_t hard_string[TOP], hard_states[TOP]; /* the strings with the most steps to the first coloring */
    uint64_t diff_at[9];                         /* first difference from the original at bit L+0..L+7, later */
} ClassCount;

static ClassCount counts[CLASSES];
static uint32_t (*states0_hist)[STATE_CAP]; /* [class][steps to the first coloring] */
static uint32_t (*states_all_hist)[STATE_CAP];
static uint64_t colorings_hist[CLASSES][K_CAP + 1];

static void note_hard(ClassCount *c, uint32_t string, uint32_t states) {
    int at = TOP;
    while (at > 0 && states > c->hard_states[at - 1]) at--;
    if (at == TOP) return;
    for (int i = TOP - 1; i > at; i--) {
        c->hard_states[i] = c->hard_states[i - 1];
        c->hard_string[i] = c->hard_string[i - 1];
    }
    c->hard_states[at] = states;
    c->hard_string[at] = string;
}

static uint32_t percentile(const uint32_t *hist, uint64_t total, double p) {
    uint64_t want = (uint64_t)ceil(p * (double)total), seen = 0;
    if (want < 1) want = 1;
    for (uint32_t v = 0; v < STATE_CAP; v++) {
        seen += hist[v];
        if (seen >= want) return v;
    }
    return STATE_CAP - 1;
}

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

static double pct(uint64_t part, uint64_t whole) { return whole ? 100.0 * (double)part / (double)whole : 0.0; }

static int cmp_u32_desc(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x < y) - (x > y);
}

int main(int argc, char **argv) {
    const char *path = NULL, *data_path = NULL;
    FrbFileInfo info;
    FrbTable table;
    FILE *data = NULL;
    uint32_t bad = 0, mask, top_bit;
    int brute = 0, L, max_class = 0;
    double t0;
    FrbError e;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--brute")) brute = 1;
        else if (!strcmp(argv[i], "--bomb") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data_path = argv[++i];
        else path = NULL, i = argc;
    }
    if (!path) {
        fprintf(stderr, "usage: colorless_scan --bomb FILE [--brute] [--data FILE]\n");
        return 2;
    }
    t0 = cb_now();
    e = frb_file_read(path, &table, &info, &bad);
    if (e != FRB_OK || !info.complete) {
        fprintf(stderr, "colorless_scan: %s is not a complete fixed-width Bomb (%s)\n", path,
                e == FRB_OK ? "it is partial" : frb_error_str(e));
        return 1;
    }
    L = table.bits;
    mask = table.count - 1u;
    top_bit = (uint32_t)1 << (L - 1);
    states0_hist = calloc(CLASSES, sizeof *states0_hist);
    states_all_hist = calloc(CLASSES, sizeof *states_all_hist);
    if (!states0_hist || !states_all_hist) return 1;
    printf("Bomb: FixedRunBomb(%d), format version %d, %s strings, table CRC-32 %08x; validated in full (%.1f s)\n",
           L, info.version, commas(table.count), (unsigned)info.table_crc, cb_now() - t0);

    t0 = cb_now();
    for (uint32_t t = 0;; t++) {
        static uint32_t solutions[K_CAP], all[1 << CL_MAX_NODES];
        RevGraph g;
        FrbStatus status;
        ClTopo topo, canon, back;
        ClSearch s0, s1, sa;
        ClassCount *c;
        Slot *slot;
        uint8_t perm[CL_MAX_NODES];
        uint32_t colors = 0, kc = 0, back_colors = 0, visited, used[2], c0 = 0, c1 = 0, full;
        int n, k, identity = 1, rank = -1, unused, open_next = 0, open_8 = 0, open_64 = 0, open_stream = 0;

        if (frb_entry_decode_as(table.format, frb_table_entry(&table, t), &status, &g) != FRB_OK ||
            status != FRB_MINIMAL || !cl_from_graph(&g, &topo, &colors)) {
            fprintf(stderr, "colorless_scan: string %u has no REV this experiment can take\n", (unsigned)t);
            return 1;
        }
        n = topo.n;
        full = (1u << n) - 1u;
        if (n > max_class) max_class = n;
        c = &counts[n];
        c->strings++;

        /* 1. the stored REV */
        if (cl_reachable(&topo) != full) c->unreachable++;
        cl_trace(&topo, colors, L, &visited, used);
        if (visited != full) c->unvisited++;
        unused = 2 * n - __builtin_popcount(used[0]) - __builtin_popcount(used[1]);
        c->ports += (uint64_t)(2 * n);
        c->ports_unused += (uint64_t)unused;
        for (int i = 0; i < n; i++)
            for (int p = 0; p < 2; p++)
                if (!((used[p] >> i) & 1u) && topo.link[p][i] != 0) c->unused_not_zero++;
        cl_execution_order(&topo, colors, L, perm);
        for (int i = 0; i < n; i++) identity = identity && perm[i] == i;
        c->solver_numbered += (uint64_t)identity;
        c->bfs_numbered += (uint64_t)cl_is_canonical(&topo);

        /* 2. canonical form; nothing it emits may change, and the stored REV
         * must come back from it by the solver's rule */
        cl_canonical(&topo, colors, &canon, &kc, NULL);
        {
            ClState a, b;
            int same = cl_output(&topo, colors, 32, &a) == cl_output(&canon, kc, 32, &b);
            for (int i = 0; same && i < 224; i++) same = cl_next(&topo, colors, &a) == cl_next(&canon, kc, &b);
            if (!same) c->output_changed++;
        }
        cl_execution_order(&canon, kc, L, perm);
        cl_relabel(&canon, kc, perm, &back, &back_colors);
        c->restored += (uint64_t)(cl_same_topo(&back, &topo) && back_colors == colors);

        /* 3. every coloring of the canonical topology that emits the string */
        k = cl_recolor_all(&canon, t, L, solutions, K_CAP, &sa);
        cl_recolor(&canon, t, L, 0, &c0, &s0);
        cl_recolor(&canon, t, L, 1, &c1, &s1);
        c->free_nodes += sa.free;
        c->colorings_total += (uint64_t)k;
        if ((uint64_t)k > c->max_colorings) c->max_colorings = (uint64_t)k;
        colorings_hist[n][k > K_CAP ? K_CAP : k]++;
        if (k == 1) c->one_coloring++;
        else if (k == 2) c->two_colorings++;
        else c->more_colorings++;
        for (int i = 0; i < k && i < K_CAP; i++)
            if (solutions[i] == kc) rank = i;
        if (rank < 0) c->original_missing++;
        c->rank_bits_x1000 += (uint64_t)(1000.0 * log2((double)(k > 0 ? k : 1)));
        for (int i = 0; i < k && i < K_CAP; i++) {
            int d;
            if (solutions[i] == kc) continue;
            d = cl_first_difference(&canon, kc, solutions[i], L);
            if (d < 0) continue;
            open_stream = 1;
            if (d < 64) open_64 = 1;
            if (d < 8) open_8 = 1;
            if (d == 0) open_next = 1;
            c->diff_at[d < 8 ? d : 8]++;
        }
        c->next_bit_open += (uint64_t)open_next;
        c->within_8_open += (uint64_t)open_8;
        c->within_64_open += (uint64_t)open_64;
        c->stream_open += (uint64_t)open_stream;
        if (c0 == kc) c->first0_is_original++;
        else {
            int d = cl_first_difference(&canon, kc, c0, L);
            c->first0_next_differs += d == 0;
            c->first0_stream_differs += d >= 0;
        }
        c->first1_is_original += c1 == kc;
        c->states0_total += s0.states;
        c->states1_total += s1.states;
        c->states_all_total += sa.states;
        c->branches0_total += s0.branches;
        c->branches_all_total += sa.branches;
        if (s0.states > c->states0_max) c->states0_max = s0.states;
        if (sa.states > c->states_all_max) c->states_all_max = sa.states;
        if (s0.branches > c->branches0_max) c->branches0_max = s0.branches;
        states0_hist[n][s0.states < STATE_CAP ? s0.states : STATE_CAP - 1]++;
        states_all_hist[n][sa.states < STATE_CAP ? sa.states : STATE_CAP - 1]++;
        note_hard(c, t, s0.states);
        if (brute) {
            int b = cl_brute(&canon, t, L, all, 1 << CL_MAX_NODES);
            if (b != k) c->brute_differs++;
            for (int i = 0; i < b && i < k && i < K_CAP; i++) { /* the same colorings, not only as many */
                int found = 0;
                for (int j = 0; j < k && j < K_CAP; j++) found = found || solutions[j] == all[i];
                if (!found) {
                    c->brute_differs++;
                    break;
                }
            }
        }

        slot = slot_find(cl_key(&canon), 1);
        if (!slot->count) slot->first = t;
        slot->count++;
        slot->count0 += !(t & top_bit);
        slot->ambiguous += k > 1;
        if (t == mask) break;
    }
    printf("Scanned in %.1f s%s.\n\n", cb_now() - t0, brute ? ", with all 2^N colorings of every topology as reference" : "");

    if (data_path) {
        data = fopen(data_path, "w");
        if (!data) {
            fprintf(stderr, "colorless_scan: cannot write %s\n", data_path);
            return 1;
        }
        fprintf(data, "bits %d\ncrc %08x\nmax_class %d\n", L, (unsigned)info.table_crc, max_class);
    }

    /* ---- 1 ---- */
    printf("1. The stored REVs\n\n");
    printf("  Nodes       Strings   Unreachable   Not reached   Solver's      Canonical   Restored from     Links never   of them\n");
    printf("                           node        in %2d bits   numbering     already    canonical form    followed      not 0\n", L);
    {
        ClassCount sum;
        memset(&sum, 0, sizeof sum);
        for (int n = 1; n <= max_class; n++) {
            const ClassCount *c = &counts[n];
            if (!c->strings) continue;
            printf("  %5d  %12s  %12s  %12s  %9.4f%%  %9.4f%%  %12.4f%%  %11.2f%%  %8s\n", n, commas(c->strings),
                   commas(c->unreachable), commas(c->unvisited), pct(c->solver_numbered, c->strings),
                   pct(c->bfs_numbered, c->strings), pct(c->restored, c->strings), pct(c->ports_unused, c->ports),
                   commas(c->unused_not_zero));
            sum.strings += c->strings;
            sum.unreachable += c->unreachable;
            sum.unvisited += c->unvisited;
            sum.solver_numbered += c->solver_numbered;
            sum.bfs_numbered += c->bfs_numbered;
            sum.restored += c->restored;
            sum.ports += c->ports;
            sum.ports_unused += c->ports_unused;
            sum.unused_not_zero += c->unused_not_zero;
            sum.output_changed += c->output_changed;
            if (data)
                fprintf(data, "class %d strings %llu restored %llu solver_numbered %llu\n", n,
                        (unsigned long long)c->strings, (unsigned long long)c->restored,
                        (unsigned long long)c->solver_numbered);
        }
        printf("    all  %12s  %12s  %12s  %9.4f%%  %9.4f%%  %12.4f%%  %11.2f%%  %8s\n", commas(sum.strings),
               commas(sum.unreachable), commas(sum.unvisited), pct(sum.solver_numbered, sum.strings),
               pct(sum.bfs_numbered, sum.strings), pct(sum.restored, sum.strings), pct(sum.ports_unused, sum.ports),
               commas(sum.unused_not_zero));
        printf("\n  Canonical renumbering changed the first 256 output bits of %s REVs.\n",
               commas(sum.output_changed));
        printf("  \"Restored\": the canonical REV, renumbered in the order the machine reaches its nodes while\n"
               "  emitting the string, is the stored REV again, link for link and color for color.\n\n");
    }

    /* ---- 2 ---- */
    printf("2. Canonical topologies\n\n");
    printf("  Nodes       Strings      Topologies   Strings per   Used    Most     Half the strings   Bits to name one of them\n");
    printf("                                         topology     once    used     are covered by     flat      entropy   any of N nodes\n");
    {
        uint64_t total_strings = 0, total_topologies = 0, not_closed = 0;
        uint64_t per_class[CLASSES] = {0}, self_swap[CLASSES] = {0}, in_half[CLASSES] = {0};
        uint64_t buckets[CLASSES][12];
        uint32_t **mult = calloc(CLASSES, sizeof *mult);
        uint64_t fill[CLASSES] = {0};
        memset(buckets, 0, sizeof buckets);
        for (uint64_t i = 0; i <= slot_mask; i++)
            if (slots[i].key.hi) per_class[slots[i].key.hi >> 56]++;
        for (int n = 1; n <= max_class; n++) mult[n] = malloc((size_t)(per_class[n] + 1) * sizeof **mult);
        for (uint64_t i = 0; i <= slot_mask; i++) {
            ClTopo t, swapped, canon;
            uint32_t unused_colors = 0;
            const Slot *other;
            int n, b = 0;
            if (!slots[i].key.hi) continue;
            n = (int)(slots[i].key.hi >> 56);
            mult[n][fill[n]++] = slots[i].count;
            while (b < 11 && (1u << b) < slots[i].count) b++; /* 1, 2, 3-4, 5-8, ... */
            buckets[n][b]++;
            in_half[n] += slots[i].count0 > 0;
            /* the topology of the complements: ports exchanged, numbered canonically again */
            cl_key_topo(slots[i].key, &t);
            cl_swap_ports(&t, &swapped);
            cl_canonical(&swapped, 0, &canon, &unused_colors, NULL);
            other = slot_find(cl_key(&canon), 0);
            if (!other || other->count != slots[i].count) not_closed++;
            if (cl_same_topo(&canon, &t)) self_swap[n]++;
        }
        for (int n = 1; n <= max_class; n++) {
            const ClassCount *c = &counts[n];
            uint64_t u = per_class[n], seen = 0, half = 0, once = 0;
            double entropy = 0.0, any = log2((double)cl_canon_count(n));
            if (!c->strings) continue;
            qsort(mult[n], (size_t)u, sizeof **mult, cmp_u32_desc);
            for (uint64_t i = 0; i < u; i++) {
                double p = (double)mult[n][i] / (double)c->strings;
                entropy -= p * log2(p);
                if (2 * seen < c->strings) half = i + 1;
                seen += mult[n][i];
                once += mult[n][i] == 1;
            }
            printf("  %5d  %12s  %14s  %11.2f  %5.1f%%  %6u  %10s (%5.2f%%)  %5d  %10.2f  %10.2f\n", n,
                   commas(c->strings), commas(u), (double)c->strings / (double)u, pct(once, u), (unsigned)mult[n][0],
                   commas(half), pct(half, u), u > 1 ? (int)ceil(log2((double)u)) : 0, entropy, any);
            total_strings += c->strings;
            total_topologies += u;
            if (data)
                fprintf(data, "class %d topologies %llu topologies_half %llu self_swap %llu entropy %.6f any_bits %.6f\n",
                        n, (unsigned long long)u, (unsigned long long)in_half[n], (unsigned long long)self_swap[n],
                        entropy, any);
        }
        printf("    all  %12s  %14s  %11.2f\n\n", commas(total_strings), commas(total_topologies),
               (double)total_strings / (double)total_topologies);
        printf("  How many strings share a topology (topologies by the number of strings that use them):\n");
        printf("  Nodes          1          2        3-4        5-8       9-16      17-32      33-64     65-128    129-256       more\n");
        for (int n = 1; n <= max_class; n++) {
            if (!counts[n].strings) continue;
            printf("  %5d", n);
            for (int b = 0; b < 10; b++) printf(" %10s", commas(buckets[n][b] + (b == 9 ? buckets[n][10] + buckets[n][11] : 0)));
            printf("\n");
        }
        printf("\n  Complements.  The REV of a complement string is the REV with colors inverted and ports\n"
               "  exchanged, so its topology is the port-exchanged one:\n");
        printf("  Nodes      Topologies   Used by strings   Pairs under      Their own\n");
        printf("                          that start with 0  port exchange    exchange\n");
        for (int n = 1; n <= max_class; n++) {
            if (!counts[n].strings) continue;
            printf("  %5d  %14s  %16s  %13s  %12s\n", n, commas(per_class[n]), commas(in_half[n]),
                   commas((per_class[n] + self_swap[n]) / 2), commas(self_swap[n]));
        }
        printf("  Topologies whose port-exchanged twin is missing or used by another number of strings: %s\n\n",
               commas(not_closed));
        for (int n = 1; n <= max_class; n++) free(mult[n]);
        free(mult);
    }

    /* ---- 3 ---- */
    printf("3. Recoloring: the colorings of the stored topology that emit the string\n\n");
    printf("  Nodes       Strings    One coloring         Two        More    Most   Nodes left   Original   Brute force\n");
    printf("                                                                         uncolored    missing    disagrees\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        if (!c->strings) continue;
        printf("  %5d  %12s  %12s (%7.4f%%)  %10s  %10s  %6s  %10s  %9s  %11s\n", n, commas(c->strings),
               commas(c->one_coloring), pct(c->one_coloring, c->strings), commas(c->two_colorings),
               commas(c->more_colorings), commas(c->max_colorings), commas(c->free_nodes),
               commas(c->original_missing), brute ? commas(c->brute_differs) : "not run");
        if (data)
            fprintf(data, "class %d one %llu two %llu more %llu rank_bits %.3f first0_original %llu\n", n,
                    (unsigned long long)c->one_coloring, (unsigned long long)c->two_colorings,
                    (unsigned long long)c->more_colorings, (double)c->rank_bits_x1000 / 1000.0,
                    (unsigned long long)c->first0_is_original);
    }
    printf("\n  Strings with more than one coloring: do the colorings go on alike after bit %d?\n", L);
    printf("  Nodes   More than one    Next bit       Within 8      Within 64     Somewhere     Never: the same\n");
    printf("            coloring        differs        bits          bits          later         stream for ever\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        uint64_t many = c->two_colorings + c->more_colorings;
        if (!c->strings) continue;
        printf("  %5d  %12s  %12s  %12s  %12s  %12s  %12s\n", n, commas(many), commas(c->next_bit_open),
               commas(c->within_8_open), commas(c->within_64_open), commas(c->stream_open),
               commas(many - c->stream_open));
        if (data)
            fprintf(data, "class %d many %llu next_open %llu within8_open %llu within64_open %llu stream_open %llu\n", n,
                    (unsigned long long)many, (unsigned long long)c->next_bit_open,
                    (unsigned long long)c->within_8_open, (unsigned long long)c->within_64_open,
                    (unsigned long long)c->stream_open);
    }
    printf("\n  Where another coloring first differs from the original (colorings, not strings):\n");
    printf("  Nodes   bit L+1       L+2       L+3       L+4       L+5       L+6       L+7       L+8     later\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        if (!c->strings) continue;
        printf("  %5d", n);
        for (int d = 0; d < 9; d++) printf(" %9s", commas(c->diff_at[d]));
        printf("\n");
    }
    printf("\n  If the first coloring found is taken (color 0 tried first, or color 1):\n");
    printf("  Nodes       Strings   It is the original          Tried 1 first       Not the original (0 first):\n");
    printf("                         (0 first)                                       next bit differs   stream differs\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        if (!c->strings) continue;
        printf("  %5d  %12s  %12s (%8.4f%%)  %12s (%8.4f%%)  %12s  %14s\n", n, commas(c->strings),
               commas(c->first0_is_original), pct(c->first0_is_original, c->strings), commas(c->first1_is_original),
               pct(c->first1_is_original, c->strings), commas(c->first0_next_differs),
               commas(c->first0_stream_differs));
        if (data)
            fprintf(data, "class %d first0_next_differs %llu first0_stream_differs %llu first1_original %llu\n", n,
                    (unsigned long long)c->first0_next_differs, (unsigned long long)c->first0_stream_differs,
                    (unsigned long long)c->first1_is_original);
    }
    printf("\n  Machine steps of the search (one per bit tried; %d is the least that finds a coloring):\n", L);
    printf("  Nodes   To the first coloring, color 0 tried first                      1 first   All colorings          Bound\n");
    printf("            mean     p50     p95     p99   p99.9     max   guesses mean     mean      mean     max      L * 2^N\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        if (!c->strings) continue;
        printf("  %5d  %7.2f  %6u  %6u  %6u  %6u  %6u  %13.2f  %7.2f  %8.2f  %6u  %10u\n", n,
               (double)c->states0_total / (double)c->strings, (unsigned)percentile(states0_hist[n], c->strings, 0.50),
               (unsigned)percentile(states0_hist[n], c->strings, 0.95),
               (unsigned)percentile(states0_hist[n], c->strings, 0.99),
               (unsigned)percentile(states0_hist[n], c->strings, 0.999), (unsigned)c->states0_max,
               (double)c->branches0_total / (double)c->strings, (double)c->states1_total / (double)c->strings,
               (double)c->states_all_total / (double)c->strings, (unsigned)c->states_all_max, (unsigned)L << n);
        if (data)
            fprintf(data, "class %d states_mean %.4f states_p50 %u states_p95 %u states_p99 %u states_p999 %u states_max %u\n",
                    n, (double)c->states0_total / (double)c->strings,
                    (unsigned)percentile(states0_hist[n], c->strings, 0.50),
                    (unsigned)percentile(states0_hist[n], c->strings, 0.95),
                    (unsigned)percentile(states0_hist[n], c->strings, 0.99),
                    (unsigned)percentile(states0_hist[n], c->strings, 0.999), (unsigned)c->states0_max);
    }
    printf("\n  The strings that take the most steps to their first coloring:\n");
    for (int n = 1; n <= max_class; n++) {
        const ClassCount *c = &counts[n];
        char text[FRB_MAX_BITS + 1];
        if (!c->strings || n < 5) continue;
        printf("  %5d nodes:", n);
        for (int i = 0; i < 3 && c->hard_states[i]; i++) {
            frb_target_format(c->hard_string[i], L, text);
            printf("  %s (%u)", text, (unsigned)c->hard_states[i]);
        }
        printf("\n");
    }
    if (data) fclose(data);
    free(slots);
    free(states0_hist);
    free(states_all_hist);
    frb_table_destroy(&table);
    return 0;
}
