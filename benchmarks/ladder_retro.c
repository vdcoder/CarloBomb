/*
 * ladder_retro: what a finished level of the ladder cost, and what it would
 * have cost with more known beforehand.  Reads; changes nothing.
 *
 *   ladder_retro --work DIR --base BOMB [--witnesses]
 *
 * DIR is the work directory of a level (its journal is the record of every
 * search that was made), BOMB the complete Bomb the level was built on, in
 * either format (the compact file is the smaller thing to open).
 *
 * The journal says, for every search: the string, the node budget, the
 * result, the logical steps and the time.  From that alone:
 *
 *   - where the effort went, by node budget and by result;
 *   - how many searches a string took.
 *
 * And for a rule that would have excluded node budgets without a search, the
 * searches it would have made unnecessary are in the journal, with their
 * steps and their time: the saving is counted, not estimated.  Two such
 * rules are evaluated, both consequences of the suffix theorem
 * (solvers/fixed/bomb/README.md: if n nodes emit S, n + 1 nodes emit S
 * without its first j bits):
 *
 *   one cut      LB(S) = max(M(P), M(Q) - 1), P = S without its last bit,
 *                Q = S without its first;
 *   every cut    LB(S) = max(M(P), M(S without its first j bits) - 1 for
 *                every j >= 1).
 *
 * Every string of the level is also checked against both: a bound above the
 * certified minimum would refute the theorem or the table.
 *
 * With --witnesses, three ways to a REV without a search are counted for
 * every string that was searched, each against the REV the search found:
 *
 *   last link    the REV the base Bomb stores for P emits the other bit, but
 *                the last link it follows for that was never followed
 *                before: pointed elsewhere, the same nodes emit S;
 *   prepend      a REV for S from the REV of Q: a new root, and a second
 *                new node where the old root has the wrong color;
 *
 * With --seed-out FILE the steps of the search that found each string's REV
 * are written out (for a string settled by the REV of the base: the steps
 * that found that one, if --seed-in gives them), and with --seed-in FILE,
 * the file written for the level below, a third rule is evaluated:
 *
 *   seed         the search for S at its first budget, M(P), goes through
 *                everything the search for P went through before it found
 *                the REV the base stores, and none of that can hold a REV
 *                for S.  Started at that REV instead, it saves those steps.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"
#include "fixed_run_build.h"
#include "level_solver.h"

enum { MAXB = 32 };

typedef struct {
    uint64_t n, steps, ns;
} Sum;

static void add(Sum *s, uint64_t steps, uint64_t ns) {
    s->n++;
    s->steps += steps;
    s->ns += ns;
}

typedef struct {
    FrbConfig cfg;
    RevFixedBomb *base;
    int bits, base_bits, shift, witnesses;
    uint32_t base_mask;
    uint8_t *final; /* per string of the level: nodes of the REV a search found, 0 = none */
    uint8_t *tries; /* per string: searches made */

    Sum by[MAXB][3];          /* [budget][result] */
    Sum first_sat, first_unsat, later_sat, later_unsat, limited;
    Sum skip1, skipall;       /* searches a bound makes unnecessary */
    Sum skip1_by[MAXB], skipall_by[MAXB];
    uint64_t violations;      /* a search that FOUND a REV below a bound: must stay 0 */
    uint64_t raised1, raisedall, tight1, tightall; /* strings, counted at their solved record */
    uint64_t budgets1, budgetsall;
    uint64_t found_strings;
    uint64_t gap[8];          /* strings by M(S) - M(P) */

    /* --witnesses */
    Sum last_link;            /* solved searches a redirected last link makes unnecessary */
    uint64_t last_link_same;  /* ... where that REV is the one the search found */
    Sum prepend_min;          /* solved searches whose REV the prepend construction matches in size */
    uint64_t prepend_cost[3]; /* strings by nodes added: 1 or 2 */
    uint64_t prepend_tight;   /* prepend REV has exactly the lower bound (every cut): no search at all */
    uint64_t prepend_over[4]; /* strings by nodes of the prepend REV above M(S) */

    /* --seed-in, --seed-out: steps of the search that found a string's REV; 0 = not known */
    uint32_t *seed_in, *seed_out;
    Sum seed_first_sat, seed_first_unsat; /* first-budget searches with a seed: n, steps saved, - */
    uint64_t seed_unknown, seed_violations; /* no seed known; a search shorter than its seed */

    uint32_t memo_target;
    int memo_old, memo_one, memo_all;
} Retro;

static int base_nodes(const Retro *R, int bits, uint32_t value) {
    return rev_fixed_bomb_min_nodes(R->base, bits, value);
}

/* The three lower bounds of a string of the level. */
static void bounds(Retro *R, uint32_t target, int *old, int *one, int *all) {
    char text[FRB_MAX_BITS + 1];
    int lo, best;
    if (target == R->memo_target) {
        *old = R->memo_old, *one = R->memo_one, *all = R->memo_all;
        return;
    }
    lo = base_nodes(R, R->base_bits, target >> R->shift);
    best = lo;
    /* one cut: the stretch of base_bits bits that begins after the first bit */
    {
        int q = base_nodes(R, R->base_bits, (target >> (R->shift - 1)) & R->base_mask) - 1;
        if (q > best) best = q;
    }
    frb_target_format(target, R->bits, text);
    R->memo_target = target;
    R->memo_old = *old = lo;
    R->memo_one = *one = best;
    R->memo_all = *all = rev_fixed_bomb_lower_bound(R->base, text, (size_t)R->bits);
}

/* Links of g followed while it emits `bits` bits; the machine's last state in fp, cp. */
static void run_marking(const RevGraph *g, int bits, uint8_t used[][2], int *fp_out, int *cp_out) {
    int fp = 0, cp = 0;
    for (int k = 0; k < bits; k++) {
        int c;
        if (k) {
            c = g->nodes[cp].color;
            used[fp][c] = 1;
            fp = g->nodes[fp].links[c];
        }
        c = g->nodes[fp].color;
        used[cp][c] = 1;
        cp = g->nodes[cp].links[c];
    }
    *fp_out = fp;
    *cp_out = cp;
}

/*
 * The REV stored for P, which goes on to emit the bit S does not have.  Can
 * links it has not followed yet be set so that it emits the other bit?  If
 * so *out is that REV, with the choices a search would make first: fp's
 * link to the lowest node that works, then cp's.
 */
static int last_link_witness(const Retro *R, uint32_t target, RevGraph *out) {
    char text[FRB_MAX_BITS + 1];
    uint8_t used[REV_MAX_NODES][2];
    RevFixedBombInfo info;
    RevGraph g;
    int fp, cp, want = (int)(target & 1u), n;
    if (R->shift != 1) return 0;
    frb_target_format(target >> 1, R->base_bits, text);
    if (rev_fixed_bomb_solve(R->base, text, (size_t)R->base_bits, 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) return 0;
    n = g.node_count;
    memset(used, 0, sizeof used);
    run_marking(&g, R->base_bits, used, &fp, &cp);
    {
        int cf = g.nodes[cp].color; /* fp moves on the color of cp */
        int z_first = used[fp][cf] ? g.nodes[fp].links[cf] : 0;
        int z_last = used[fp][cf] ? z_first : n - 1;
        for (int z = z_first; z <= z_last; z++) {
            int cc = g.nodes[z].color; /* cp moves on the color of fp's new node */
            int same_link = !used[fp][cf] && fp == cp && cf == cc; /* the link being set is cp's too */
            if (same_link) {
                if (g.nodes[z].color != want) continue;
                *out = g;
                out->nodes[fp].links[cf] = z;
                return 1;
            }
            if (used[cp][cc]) {
                if (g.nodes[g.nodes[cp].links[cc]].color != want) continue;
                *out = g;
                out->nodes[fp].links[cf] = z;
                return 1;
            }
            for (int y = 0; y < n; y++) {
                if (g.nodes[y].color != want) continue;
                *out = g;
                out->nodes[fp].links[cf] = z;
                out->nodes[cp].links[cc] = y;
                return 1;
            }
        }
    }
    return 0;
}

/* Nodes the prepend construction adds to the REV stored for Q: 1 or 2; 0 if it cannot be read. */
static int prepend_cost(const Retro *R, uint32_t target, int *q_nodes) {
    char text[FRB_MAX_BITS + 1];
    uint8_t used[REV_MAX_NODES][2];
    RevFixedBombInfo info;
    RevGraph g;
    int fp, cp, b = (int)((target >> (R->bits - 1)) & 1u), rc, c1;
    if (R->shift != 1) return 0;
    frb_target_format(target & R->base_mask, R->base_bits, text);
    if (rev_fixed_bomb_solve(R->base, text, (size_t)R->base_bits, 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) return 0;
    *q_nodes = g.node_count;
    rc = g.nodes[0].color;
    if (rc == b) return 1; /* a new root of the other color, both links to the old root */
    /* else a node of color b whose link on the root's color leads, or can be made to lead, where
     * the root's does: an old one if there is one, a new one if not */
    c1 = g.nodes[0].links[rc];
    memset(used, 0, sizeof used);
    run_marking(&g, R->base_bits, used, &fp, &cp);
    for (int y = 0; y < g.node_count; y++)
        if (g.nodes[y].color == b && (g.nodes[y].links[rc] == c1 || !used[y][rc])) return 1;
    return 2;
}

static int same_graph(const RevGraph *a, const RevGraph *b) {
    if (a->node_count != b->node_count) return 0;
    for (int i = 0; i < a->node_count; i++)
        if (a->nodes[i].color != b->nodes[i].color || a->nodes[i].links[0] != b->nodes[i].links[0] ||
            a->nodes[i].links[1] != b->nodes[i].links[1])
            return 0;
    return 1;
}

static int visit(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Retro *R = ctx;
    int old, one, all, b, first;
    (void)seg;
    if (!rec || rec->type != FRB_REC_SEARCH) return 0;
    b = rec->budget;
    if (b < 1 || b >= MAXB || rec->result > 2) return 1;
    bounds(R, rec->target, &old, &one, &all);
    add(&R->by[b][rec->result], rec->steps, rec->elapsed_ns);
    if (R->tries[rec->target] < 255) R->tries[rec->target]++;
    first = b == old;
    if (first && R->seed_in && rec->result != REV_SOLVE_RESOURCE_LIMIT) {
        uint32_t seed = R->seed_in[rec->target >> 1];
        if (!seed) R->seed_unknown++;
        else {
            if (rec->steps < seed) R->seed_violations++;
            add(rec->result == REV_SOLVE_SOLVED ? &R->seed_first_sat : &R->seed_first_unsat, seed, 0);
        }
    }
    if (rec->result == REV_SOLVE_RESOURCE_LIMIT) {
        add(&R->limited, rec->steps, rec->elapsed_ns);
        return 0;
    }
    if (rec->result == REV_SOLVE_PROVEN_UNSAT) {
        add(first ? &R->first_unsat : &R->later_unsat, rec->steps, rec->elapsed_ns);
        if (b < one) add(&R->skip1, rec->steps, rec->elapsed_ns), add(&R->skip1_by[b], rec->steps, rec->elapsed_ns);
        if (b < all) add(&R->skipall, rec->steps, rec->elapsed_ns), add(&R->skipall_by[b], rec->steps, rec->elapsed_ns);
        return 0;
    }
    /* solved */
    add(first ? &R->first_sat : &R->later_sat, rec->steps, rec->elapsed_ns);
    if (rec->nodes < one || rec->nodes < all) R->violations++;
    if (R->final[rec->target] == 0 || rec->nodes < R->final[rec->target]) R->final[rec->target] = rec->nodes;
    R->found_strings++;
    if (R->seed_out) R->seed_out[rec->target] = rec->steps < 0xFFFFFFFFu ? (uint32_t)(rec->steps ? rec->steps : 1) : 0xFFFFFFFFu;
    R->gap[rec->nodes - old < 7 ? rec->nodes - old : 7]++;
    if (one > old) R->raised1++, R->budgets1 += (uint64_t)(one - old);
    if (all > old) R->raisedall++, R->budgetsall += (uint64_t)(all - old);
    if (rec->nodes > old && one == rec->nodes) R->tight1++;
    if (rec->nodes > old && all == rec->nodes) R->tightall++;

    if (R->witnesses) {
        RevGraph found, w;
        int q_nodes = 0, cost;
        if (frb_graph_unpack_as(frb_format(R->cfg.format), rec->graph, rec->nodes, &found) != FRB_OK) return 1;
        if (rec->nodes == old && last_link_witness(R, rec->target, &w)) {
            if (frb_graph_output(&w, R->bits) != rec->target) return 1; /* the construction is wrong */
            add(&R->last_link, rec->steps, rec->elapsed_ns);
            R->last_link_same += (uint64_t)same_graph(&w, &found);
        }
        cost = prepend_cost(R, rec->target, &q_nodes);
        if (cost) {
            int nodes = q_nodes + cost, over = nodes - rec->nodes;
            R->prepend_cost[cost]++;
            if (over < 0) return 1; /* fewer nodes than the certified minimum: impossible */
            R->prepend_over[over < 3 ? over : 3]++;
            if (over == 0) add(&R->prepend_min, rec->steps, rec->elapsed_ns);
            if (nodes == all) R->prepend_tight++;
        }
    }
    return 0;
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

static double pct(uint64_t a, uint64_t b) { return b ? 100.0 * (double)a / (double)b : 0.0; }

static void line(const char *name, const Sum *s, const Sum *total) {
    printf("  %-44s %14s %22s  %5.1f%%  %10.1f s  %5.1f%%\n", name, commas(s->n), commas(s->steps),
           pct(s->steps, total->steps), (double)s->ns / 1e9, pct(s->ns, total->ns));
}

int main(int argc, char **argv) {
    const char *work = NULL, *base_path = NULL, *seed_in_path = NULL, *seed_out_path = NULL;
    char err[FRB_ERR_TEXT] = "";
    FrbJournalInfo info;
    Retro R;
    Sum total = {0, 0, 0};
    uint64_t searched = 0, settled = 0, above1 = 0, aboveall = 0, tries_hist[8] = {0};
    uint32_t count, top;
    double t0 = cb_now();
    FrbError e;

    memset(&R, 0, sizeof R);
    R.memo_target = 0xFFFFFFFFu;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--work") && i + 1 < argc) work = argv[++i];
        else if (!strcmp(argv[i], "--base") && i + 1 < argc) base_path = argv[++i];
        else if (!strcmp(argv[i], "--witnesses")) R.witnesses = 1;
        else if (!strcmp(argv[i], "--seed-in") && i + 1 < argc) seed_in_path = argv[++i];
        else if (!strcmp(argv[i], "--seed-out") && i + 1 < argc) seed_out_path = argv[++i];
        else work = NULL, i = argc;
    }
    if (!work || !base_path) {
        fprintf(stderr, "usage: ladder_retro --work DIR --base BOMB [--witnesses] [--seed-in FILE] [--seed-out FILE]\n");
        return 2;
    }
    e = frb_config_read(work, &R.cfg, err, sizeof err);
    if (e != FRB_OK) return fprintf(stderr, "ladder_retro: %s: %s\n", work, err[0] ? err : frb_error_str(e)), 1;
    if (!R.cfg.base_bits) return fprintf(stderr, "ladder_retro: %s was not built on a base Bomb\n", work), 1;
    e = rev_fixed_bomb_open(base_path, &R.base, NULL);
    if (e != FRB_OK) return fprintf(stderr, "ladder_retro: cannot open %s: %s\n", base_path, frb_error_str(e)), 1;
    if (rev_fixed_bomb_bits(R.base) != R.cfg.base_bits)
        return fprintf(stderr, "ladder_retro: %s is a Bomb of %d bits; the level was built on one of %d\n", base_path,
                       rev_fixed_bomb_bits(R.base), R.cfg.base_bits),
               1;
    R.bits = R.cfg.bits;
    R.base_bits = R.cfg.base_bits;
    R.shift = R.bits - R.base_bits;
    R.base_mask = (((uint32_t)1 << (R.base_bits - 1)) << 1) - 1u;
    count = (uint32_t)1 << R.bits;
    top = count >> 1;
    R.final = calloc(count, 1);
    R.tries = calloc(count, 1);
    if (!R.final || !R.tries) return fprintf(stderr, "ladder_retro: out of memory\n"), 1;
    if ((seed_in_path || seed_out_path) && (!R.cfg.symmetry || R.shift != 1))
        return fprintf(stderr, "ladder_retro: --seed-in and --seed-out are for a level built with the symmetry on the Bomb one bit shorter\n"), 1;
    if (seed_in_path) { /* one number for each string of the base that starts with 0 */
        FILE *f = fopen(seed_in_path, "rb");
        size_t want = (size_t)(top >> 1);
        R.seed_in = malloc(want * sizeof *R.seed_in);
        if (!f || !R.seed_in || fread(R.seed_in, sizeof *R.seed_in, want, f) != want)
            return fprintf(stderr, "ladder_retro: cannot read %zu numbers from %s\n", want, seed_in_path), 1;
        fclose(f);
    }
    if (seed_out_path) {
        R.seed_out = calloc((size_t)top, sizeof *R.seed_out);
        if (!R.seed_out) return fprintf(stderr, "ladder_retro: out of memory\n"), 1;
    }

    e = frb_journal_scan(work, &R.cfg, visit, &R, &info, err, sizeof err);
    if (e != FRB_OK) return fprintf(stderr, "ladder_retro: %s: %s\n", work, err[0] ? err : frb_error_str(e)), 1;

    /* Every string of the level against the bounds: searched or settled by the REV of the base. */
    for (uint32_t i = 0; i < count; i++) {
        int old, one, all, m;
        if (R.cfg.symmetry && (i & top)) continue;
        bounds(&R, i, &old, &one, &all);
        m = R.final[i] ? R.final[i] : old;
        if (R.final[i]) searched++;
        else settled++;
        above1 += one > m;
        aboveall += all > m;
        tries_hist[R.tries[i] < 7 ? R.tries[i] : 7]++;
        /* settled by the REV of the base: found by the search that found that one */
        if (R.seed_out && !R.final[i] && i < top) R.seed_out[i] = R.seed_in ? R.seed_in[i >> 1] : 0;
    }
    if (seed_out_path) {
        FILE *f = fopen(seed_out_path, "wb");
        if (!f || fwrite(R.seed_out, sizeof *R.seed_out, (size_t)top, f) != (size_t)top || fclose(f) != 0)
            return fprintf(stderr, "ladder_retro: cannot write %s\n", seed_out_path), 1;
    }
    for (int b = 1; b < MAXB; b++)
        for (int r = 0; r < 3; r++) total.n += R.by[b][r].n, total.steps += R.by[b][r].steps, total.ns += R.by[b][r].ns;

    printf("Level %d on the Bomb of %d bits%s\n", R.bits, R.base_bits, R.cfg.symmetry ? " (one string of each complement pair)" : "");
    printf("  journal: %s records in %s bytes; read in %.1f s\n", commas(info.records), commas(info.bytes), cb_now() - t0);
    printf("  strings: %s settled by the REV of the base, %s searched\n", commas(settled), commas(searched));
    printf("  searches: %s, %s logical steps, %.1f s of solver time, %.2f ns a step\n\n", commas(total.n),
           commas(total.steps), (double)total.ns / 1e9, total.steps ? (double)total.ns / (double)total.steps : 0.0);

    printf("Effort by node budget and result\n");
    printf("  %-6s %-8s %14s %22s %7s %12s %7s %9s %14s\n", "budget", "result", "searches", "steps", "", "time", "",
           "ns/step", "steps/search");
    for (int b = 1; b < MAXB; b++)
        for (int r = 0; r < 3; r++) {
            const Sum *s = &R.by[b][r];
            if (!s->n) continue;
            printf("  %-6d %-8s %14s %22s %6.1f%% %10.1f s %6.1f%% %9.2f %14s\n", b,
                   r == 0 ? "found" : r == 1 ? "none" : "limit", commas(s->n), commas(s->steps), pct(s->steps, total.steps),
                   (double)s->ns / 1e9, pct(s->ns, total.ns), s->steps ? (double)s->ns / (double)s->steps : 0.0,
                   commas(s->steps / s->n));
        }

    printf("\nEffort by the place of a search in its string's sequence\n");
    printf("  %-44s %14s %22s %7s %12s %7s\n", "", "searches", "steps", "", "time", "");
    line("first budget M(P): a REV found", &R.first_sat, &total);
    line("first budget M(P): none (proof)", &R.first_unsat, &total);
    line("a later budget: a REV found", &R.later_sat, &total);
    line("a later budget: none (proof)", &R.later_unsat, &total);
    if (R.limited.n) line("stopped by a limit", &R.limited, &total);
    printf("  strings by nodes above their first bits, M(S) - M(P):");
    for (int k = 0; k < 8; k++)
        if (R.gap[k]) printf("  +%d: %s", k, commas(R.gap[k]));
    printf("\n  strings by searches made:");
    for (int k = 1; k < 8; k++)
        if (tries_hist[k]) printf("  %d: %s", k, commas(tries_hist[k]));
    printf("\n");

    printf("\nThe suffix bound, on the searches that were made\n");
    printf("  %-44s %14s %22s %7s %12s %7s\n", "", "searches", "steps", "", "time", "");
    line("one cut: searches it makes unnecessary", &R.skip1, &total);
    line("every cut: searches it makes unnecessary", &R.skipall, &total);
    for (int b = 1; b < MAXB; b++)
        if (R.skipall_by[b].n) {
            char name[64];
            snprintf(name, sizeof name, "  every cut, budget %d", b);
            line(name, &R.skipall_by[b], &total);
        }
    printf("  one cut:   %s of the %s searched strings start higher (%.3f%%), %s budgets skipped;\n"
           "             for %s the bound is the minimum itself: no proof left to search for\n",
           commas(R.raised1), commas(R.found_strings), pct(R.raised1, R.found_strings), commas(R.budgets1), commas(R.tight1));
    printf("  every cut: %s of the %s searched strings start higher (%.3f%%), %s budgets skipped;\n"
           "             for %s the bound is the minimum itself\n",
           commas(R.raisedall), commas(R.found_strings), pct(R.raisedall, R.found_strings), commas(R.budgetsall),
           commas(R.tightall));
    printf("  check: of the %s strings of the level, %s have a bound above their certified minimum (one cut),\n"
           "         %s (every cut); searches that found a REV below a bound: %s\n",
           commas(searched + settled), commas(above1), commas(aboveall), commas(R.violations));

    if (R.witnesses) {
        printf("\nREVs without a search, for the strings that were searched\n");
        printf("  %-44s %14s %22s %7s %12s %7s\n", "", "searches", "steps", "", "time", "");
        line("last link: the search it makes unnecessary", &R.last_link, &total);
        printf("             in %s of them the REV is the one the search found (%.2f%%)\n", commas(R.last_link_same),
               pct(R.last_link_same, R.last_link.n));
        line("prepend: a REV of the minimum size", &R.prepend_min, &total);
        printf("             nodes added: 1 for %s strings, 2 for %s; above the minimum by 0/1/2/3+: %s / %s / %s / %s;\n"
               "             equal to the lower bound of every cut (no search at all): %s\n",
               commas(R.prepend_cost[1]), commas(R.prepend_cost[2]), commas(R.prepend_over[0]), commas(R.prepend_over[1]),
               commas(R.prepend_over[2]), commas(R.prepend_over[3]), commas(R.prepend_tight));
    }
    if (R.seed_in) {
        Sum both = {R.seed_first_sat.n + R.seed_first_unsat.n, R.seed_first_sat.steps + R.seed_first_unsat.steps, 0};
        printf("\nThe search at the first budget, started at the REV of the base instead of at the beginning\n");
        printf("  %-44s %14s %22s %7s\n", "", "searches", "steps saved", "");
        printf("  %-44s %14s %22s  %5.1f%%   (%.1f%% of what those searches took)\n", "a REV found at the first budget",
               commas(R.seed_first_sat.n), commas(R.seed_first_sat.steps), pct(R.seed_first_sat.steps, total.steps),
               pct(R.seed_first_sat.steps, R.first_sat.steps));
        printf("  %-44s %14s %22s  %5.1f%%   (%.1f%% of what those searches took)\n", "none at the first budget (proof)",
               commas(R.seed_first_unsat.n), commas(R.seed_first_unsat.steps), pct(R.seed_first_unsat.steps, total.steps),
               pct(R.seed_first_unsat.steps, R.first_unsat.steps));
        printf("  %-44s %14s %22s  %5.1f%%\n", "together", commas(both.n), commas(both.steps), pct(both.steps, total.steps));
        printf("  no seed known (the REV is older than the journals read): %s searches;\n"
               "  check: searches that took fewer steps than their seed: %s\n",
               commas(R.seed_unknown), commas(R.seed_violations));
    }
    printf("\n%s\n", above1 || aboveall || R.violations || R.seed_violations ? "FAIL: see the checks above" : "PASS: no bound exceeds a certified minimum");
    rev_fixed_bomb_close(R.base);
    free(R.final);
    free(R.tries);
    free(R.seed_in);
    free(R.seed_out);
    return above1 || aboveall || R.violations || R.seed_violations ? 1 : 0;
}
