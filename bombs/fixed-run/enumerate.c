/*
 * Exhaustive enumeration of labeled REVs (Stage A of the construction).
 *
 * Counting loops and the interpreter of core/rev, nothing else: no search,
 * no pruning, no canonical forms.  That is the point of it.  Whatever this
 * finds, and above all whatever it does not find, is evidence that owes
 * nothing to the LevelSolver.
 */
#include <stdlib.h>
#include <string.h>

#include "fixed_run.h"

int frb_enum_part_count(int nodes) { return nodes * nodes; }

uint64_t frb_enum_part_graphs(int nodes, int root_white_only) {
    uint64_t graphs;
    if (nodes < 1 || nodes > FRB_NODE_CAPACITY) return 0;
    graphs = (uint64_t)1 << (root_white_only ? nodes - 1 : nodes); /* colorings */
    for (int i = 0; i < 2 * nodes - 2; i++) {                      /* links of nodes 1.. */
        if (graphs > UINT64_MAX / (uint64_t)nodes) return 0;
        graphs *= (uint64_t)nodes;
    }
    return graphs;
}

FrbError frb_enumerate_part(const FrbEnumPart *part, FrbEnumFound found, void *ctx,
                            const volatile sig_atomic_t *stop, uint64_t *graphs) {
    int digit[2 * FRB_NODE_CAPACITY] = {0};
    uint64_t run = 0;
    uint32_t mask, top;
    unsigned colorings, color_step;
    uint8_t *seen;
    RevGraph g;
    FrbError result = FRB_OK;
    int n;

    if (graphs) *graphs = 0;
    if (!part || !found) return FRB_ERR_ARG;
    n = part->nodes;
    if (n < 1 || n > FRB_NODE_CAPACITY || part->bits < 1 || part->bits > FRB_MAX_BITS ||
        part->part < 0 || part->part >= n * n)
        return FRB_ERR_ARG;

    seen = calloc((size_t)1 << part->bits, 1);
    if (!seen) return FRB_ERR_MEMORY;
    mask = ((uint32_t)1 << part->bits) - 1u;
    top = (uint32_t)1 << (part->bits - 1);
    colorings = 1u << n;
    color_step = part->root_white_only ? 2u : 1u; /* node 0 is bit 0: even = white root */

    memset(&g, 0, sizeof g);
    g.node_count = n;
    digit[0] = part->part % n; /* the two links of node 0 are fixed by the part */
    digit[1] = part->part / n;

    for (;;) {
        int k;
        if (stop && *stop) {
            result = FRB_ERR_STOPPED;
            break;
        }
        for (int i = 0; i < n; i++) {
            g.nodes[i].links[0] = digit[2 * i];
            g.nodes[i].links[1] = digit[2 * i + 1];
        }
        for (unsigned colors = 0; colors < colorings; colors += color_step) {
            RevMachine m;
            uint32_t v = 0, key;
            for (int i = 0; i < n; i++) g.nodes[i].color = (int)((colors >> i) & 1u);
            rev_machine_reset(&m);
            for (int b = 0; b < part->bits; b++)
                v = (v << 1) | (uint32_t)rev_machine_next_bit(&g, &m);
            /* With a white root only, a string and its complement count as one. */
            key = (part->root_white_only && (v & top)) ? (v ^ mask) : v;
            run++;
            if (!seen[key]) {
                seen[key] = 1;
                if (found(ctx, v, &g)) {
                    result = FRB_ERR_STOPPED;
                    goto done;
                }
            }
        }
        /* Next link assignment: a base-n counter over the links of nodes 1.. */
        k = 2;
        while (k < 2 * n && ++digit[k] == n) digit[k++] = 0;
        if (k >= 2 * n) break;
    }
done:
    free(seen);
    if (graphs) *graphs = run;
    return result;
}
