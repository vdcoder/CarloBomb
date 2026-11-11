/*
 * Independent REV interpreter and verifier.  See SPEC.md.
 *
 * This file is deliberately written from the specification, not derived from
 * the solver's CTRL/FLOW search state machine.
 */
#include "rev.h"

#include <stdlib.h>
#include <string.h>

const char *rev_status_str(RevStatus s) {
    switch (s) {
    case REV_OK: return "ok";
    case REV_ERR_NULL: return "null argument";
    case REV_ERR_NODE_COUNT: return "node count out of range";
    case REV_ERR_COLOR: return "node color is not 0 or 1";
    case REV_ERR_LINK: return "link does not point to an existing node";
    case REV_ERR_TARGET: return "target must be a nonempty string of 0 and 1";
    case REV_ERR_MISMATCH: return "output does not match target";
    case REV_ERR_FORMAT: return "malformed graph text";
    case REV_ERR_IO: return "i/o error";
    }
    return "unknown";
}

RevStatus rev_graph_validate(const RevGraph *g) {
    if (!g) return REV_ERR_NULL;
    if (g->node_count < 1 || g->node_count > REV_MAX_NODES) return REV_ERR_NODE_COUNT;
    for (int i = 0; i < g->node_count; i++) {
        const RevNode *n = &g->nodes[i];
        if (n->color != REV_WHITE && n->color != REV_BLACK) return REV_ERR_COLOR;
        for (int k = 0; k < 2; k++)
            if (n->links[k] < 0 || n->links[k] >= g->node_count) return REV_ERR_LINK;
    }
    return REV_OK;
}

RevStatus rev_target_validate(const char *target, size_t len) {
    if (!target) return REV_ERR_NULL;
    if (len == 0) return REV_ERR_TARGET;
    for (size_t i = 0; i < len; i++)
        if (target[i] != '0' && target[i] != '1') return REV_ERR_TARGET;
    return REV_OK;
}

void rev_machine_reset(RevMachine *m) {
    m->fp = REV_ROOT;
    m->cp = REV_ROOT;
    m->emitted = 0;
}

/*
 * Output bit k (k >= 1) is produced by one FLOW operation followed by one
 * CTRL operation; bit 0 by the initial CTRL operation alone.  A trailing
 * FLOW has no observable effect, so performing it lazily here is the same
 * machine as "CTRL, FLOW, CTRL, FLOW, ..." stopped after any number of bits.
 */
int rev_machine_next_bit(const RevGraph *g, RevMachine *m) {
    if (m->emitted > 0) {
        /* FLOW: the color under cp steers fp. Emits nothing. */
        m->fp = g->nodes[m->fp].links[g->nodes[m->cp].color];
    }
    /* CTRL: the color under fp steers cp. Emits the color cp lands on. */
    m->cp = g->nodes[m->cp].links[g->nodes[m->fp].color];
    m->emitted++;
    return g->nodes[m->cp].color;
}

RevStatus rev_run(const RevGraph *g, size_t nbits, char *out) {
    RevStatus st = rev_graph_validate(g);
    if (st != REV_OK) return st;
    if (!out) return REV_ERR_NULL;
    RevMachine m;
    rev_machine_reset(&m);
    for (size_t i = 0; i < nbits; i++) out[i] = (char)('0' + rev_machine_next_bit(g, &m));
    out[nbits] = '\0';
    return REV_OK;
}

RevStatus rev_verify_prefix(const RevGraph *g, const char *target, size_t len,
                            size_t *mismatch_at) {
    RevStatus st = rev_graph_validate(g);
    if (st != REV_OK) return st;
    st = rev_target_validate(target, len);
    if (st != REV_OK) return st;
    RevMachine m;
    rev_machine_reset(&m);
    for (size_t i = 0; i < len; i++) {
        if (rev_machine_next_bit(g, &m) != target[i] - '0') {
            if (mismatch_at) *mismatch_at = i;
            return REV_ERR_MISMATCH;
        }
    }
    return REV_OK;
}

/* ---- text format ------------------------------------------------------- */

RevStatus rev_graph_write(FILE *f, const RevGraph *g) {
    if (!f) return REV_ERR_NULL;
    RevStatus st = rev_graph_validate(g);
    if (st != REV_OK) return st;
    if (fprintf(f, "REV 1\nnodes %d\n", g->node_count) < 0) return REV_ERR_IO;
    for (int i = 0; i < g->node_count; i++) {
        const RevNode *n = &g->nodes[i];
        if (fprintf(f, "%d %c %d %d\n", i, n->color == REV_WHITE ? 'W' : 'B', n->links[0],
                    n->links[1]) < 0)
            return REV_ERR_IO;
    }
    return REV_OK;
}

/* Next line that is not blank or a comment, with any trailing comment cut. */
static int next_line(FILE *f, char *buf, size_t cap) {
    while (fgets(buf, (int)cap, f)) {
        size_t n = strlen(buf);
        if (n + 1 == cap && buf[n - 1] != '\n') return -1; /* line too long */
        char *hash = strchr(buf, '#');
        if (hash) *hash = '\0';
        for (char *p = buf; *p; p++)
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return 1;
    }
    return 0;
}

RevStatus rev_graph_read(FILE *f, RevGraph *g) {
    char line[256], color[8], extra[2];
    int version, count;
    if (!f || !g) return REV_ERR_NULL;
    memset(g, 0, sizeof *g);
    if (next_line(f, line, sizeof line) != 1) return REV_ERR_FORMAT;
    if (sscanf(line, " REV %d %1s", &version, extra) != 1 || version != 1) return REV_ERR_FORMAT;
    if (next_line(f, line, sizeof line) != 1) return REV_ERR_FORMAT;
    if (sscanf(line, " nodes %d %1s", &count, extra) != 1) return REV_ERR_FORMAT;
    if (count < 1 || count > REV_MAX_NODES) return REV_ERR_NODE_COUNT;
    g->node_count = count;
    for (int i = 0; i < count; i++) {
        int idx, l0, l1;
        if (next_line(f, line, sizeof line) != 1) return REV_ERR_FORMAT;
        if (sscanf(line, " %d %7s %d %d %1s", &idx, color, &l0, &l1, extra) != 4)
            return REV_ERR_FORMAT;
        if (idx != i || color[1] != '\0') return REV_ERR_FORMAT;
        if (color[0] == 'W' || color[0] == '0') g->nodes[i].color = REV_WHITE;
        else if (color[0] == 'B' || color[0] == '1') g->nodes[i].color = REV_BLACK;
        else return REV_ERR_FORMAT;
        g->nodes[i].links[0] = l0;
        g->nodes[i].links[1] = l1;
    }
    if (next_line(f, line, sizeof line) != 0) return REV_ERR_FORMAT; /* trailing data */
    return rev_graph_validate(g);
}
