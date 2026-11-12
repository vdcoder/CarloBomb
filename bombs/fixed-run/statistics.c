/*
 * FixedRunBomb reports: what `carlobomb bomb status` and `bomb verify` print.
 *
 * Everything here is read off an FrbState or an FrbSummary; nothing is
 * computed that the journal replay has not already decided.  The one piece
 * of judgement is the wording of the conclusion, which says "exact maximum"
 * only when every target has a certified minimum.
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fixed_run_build.h"
#include "level_solver.h"

/* 1234567 -> "1,234,567".  Returns one of a few static buffers in turn, so
 * several calls can appear in one printf. */
static const char *commas(uint64_t v) {
    static char bufs[8][32];
    static int next;
    char digits[24], *out = bufs[next];
    int n, k = 0;
    next = (next + 1) % 8;
    n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    for (int i = 0; i < n; i++) {
        out[k++] = digits[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[k++] = ',';
    }
    out[k] = '\0';
    return out;
}

static double secs(uint64_t ns) { return (double)ns * 1e-9; }

/* ---- a construction in progress ----------------------------------------- */

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void print_settings(FILE *out, const FrbConfig *cfg) {
    fprintf(out, "Settings\n");
    fprintf(out, "  target length           %d bits (%s targets)\n", cfg->bits,
            commas((uint64_t)1 << cfg->bits));
    fprintf(out, "  complement symmetry     %s\n",
            cfg->symmetry ? "on (one string of each complement pair is worked on, the other derived)"
                          : "off (every string is worked on)");
    if (cfg->enumerate_max)
        fprintf(out, "  exhaustive enumeration  node counts 1..%d\n", cfg->enumerate_max);
    else fprintf(out, "  exhaustive enumeration  none\n");
    fprintf(out, "  largest node budget     %d\n", cfg->max_nodes);
    fprintf(out, "  fallback witness        %s\n",
            cfg->fallback_ring ? "ring (as many nodes as bits)" : "none");
    fprintf(out, "  engine                  %s\n", cfg->engine);
    fprintf(out, "  worker processes        %d\n", cfg->workers);
    fprintf(out, "  step limit per search   ");
    for (int i = 0; i < cfg->passes; i++) {
        if (cfg->max_steps[i]) fprintf(out, "%s%s", i ? ", then " : "", commas(cfg->max_steps[i]));
        else fprintf(out, "%snone", i ? ", then " : "");
    }
    if (cfg->base_bits)
        fprintf(out, "\n  built on                the complete %d-bit Bomb %s\n"
                     "                          (table checksum %08x)",
                cfg->base_bits, cfg->base_file, (unsigned)cfg->base_crc);
    fprintf(out, "\n  search revision %d, journal format %d, table format %d\n", FRB_SEARCH_REVISION,
            cfg->format, cfg->format);
}

static void print_stage_a(FILE *out, const FrbConfig *cfg, const FrbState *s) {
    uint32_t reached[FRB_NODE_CAPACITY + 1] = {0};
    uint32_t cumulative = 0;
    fprintf(out, "\nStage A: exhaustive enumeration\n");
    if (cfg->enumerate_max == 0) {
        fprintf(out, "  not used\n");
        return;
    }
    for (uint32_t i = 0; i < s->count; i++) reached[s->targets[i].enum_nodes]++;
    fprintf(out, "  Nodes     Parts        Graphs run   Time (s)   Targets first emitted   Cumulative\n");
    for (int n = 1; n <= cfg->enumerate_max; n++) {
        char parts[32];
        cumulative += reached[n];
        snprintf(parts, sizeof parts, "%u/%d", (unsigned)s->parts_done[n], n * n);
        fprintf(out, "  %5d   %7s   %15s   %8.2f   %21s   %10s%s\n", n, parts,
                commas(s->enum_graphs[n]), secs(s->enum_ns[n]), commas(reached[n]),
                commas(cumulative), n <= s->enum_complete ? "" : "   (not complete)");
    }
    if (s->enum_complete == cfg->enumerate_max) {
        fprintf(out,
                "  Complete: %s of %s targets are emitted by a graph of at most %d nodes;\n"
                "  the other %s need at least %d.\n",
                commas(cumulative), commas(s->count), s->enum_complete,
                commas(s->count - cumulative), s->enum_complete + 1);
    } else {
        fprintf(out, "  Complete for node counts up to %d only.  The counts above for larger node\n"
                     "  counts are of what has been found so far and exclude nothing.\n",
                s->enum_complete);
    }
    if (s->symmetry)
        fprintf(out, "  (Graphs with a white root only; each stands for itself and its complement.)\n");
}

static void print_stage_b(FILE *out, const FrbConfig *cfg, const FrbState *s) {
    uint32_t by_enum = 0, by_search = 0, by_ring = 0, by_base = 0, limited = 0, beyond = 0, pending = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        if (ts->status == FRB_MINIMAL) {
            if (ts->source == FRB_SRC_ENUM) by_enum++;
            else if (ts->source == FRB_SRC_SEARCH) by_search++;
            else if (ts->source == FRB_SRC_BASE) by_base++;
            else by_ring++;
        } else if (ts->conflict) {
            /* counted separately below */
        } else if (ts->limit_budget == ts->lower_bound) {
            limited++;
        } else if (ts->lower_bound > cfg->max_nodes) {
            beyond++;
        } else {
            pending++;
        }
    }
    if (cfg->base_bits) {
        fprintf(out, "\nStage B: search for the targets the %d-bit Bomb left open\n", cfg->base_bits);
        fprintf(out, "  minimum certified, REV of the %2d-bit Bomb    %10s\n", cfg->base_bits,
                commas(by_base));
    } else {
        fprintf(out, "\nStage B: search for the targets enumeration left open\n");
    }
    fprintf(out, "  minimum certified by enumeration             %10s\n", commas(by_enum));
    fprintf(out, "  minimum certified by search                  %10s\n", commas(by_search));
    if (by_ring)
        fprintf(out, "  minimum certified, witness is the ring       %10s\n", commas(by_ring));
    fprintf(out, "  open: not searched yet                       %10s\n", commas(pending));
    fprintf(out, "  open: search stopped by a resource limit     %10s\n", commas(limited));
    if (beyond)
        fprintf(out, "  open: every budget up to max-nodes excluded  %10s\n", commas(beyond));
    if (s->conflicts)
        fprintf(out, "  CONTRADICTORY RESULTS                        %10s\n", commas(s->conflicts));
    if (s->symmetry)
        fprintf(out, "  (Counts are of targets; searches run on one string of each complement pair.)\n");
}

static void print_histogram(FILE *out, const FrbState *s) {
    uint32_t minimal[FRB_MAX_NODES + 2] = {0}, lower[FRB_MAX_NODES + 3] = {0};
    uint32_t cumulative = 0, more = s->count, uncertified = 0, none = 0;
    int max_minimal = 0, max_witness = 0, max_lower = 0, all_have_witness = 1, last;
    int capacity = s->format->node_capacity;
    int complete = s->by_status[FRB_MINIMAL] == s->count && !s->conflicts;

    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        if (ts->status == FRB_MINIMAL) {
            minimal[ts->nodes]++;
            if (ts->nodes > max_minimal) max_minimal = ts->nodes;
        } else if (ts->nodes) {
            uncertified++;
        }
        if (ts->nodes > max_witness) max_witness = ts->nodes;
        if (!ts->nodes) {
            none++;
            all_have_witness = 0;
        }
        lower[ts->lower_bound]++;
        if (ts->lower_bound > max_lower) max_lower = ts->lower_bound;
    }

    /* Rows after `last` have nothing certified, nothing proven and nothing open. */
    last = max_minimal > max_lower ? max_minimal : max_lower;
    if (!complete) last = capacity;
    if (last > capacity) last = capacity;

    fprintf(out, "\nMinimum node count\n");
    fprintf(out, "  Nodes   Certified minimum   Cumulative     Share   Proven to need more   Unresolved\n");
    for (int n = 1; n <= last; n++) {
        cumulative += minimal[n];
        more -= lower[n]; /* targets whose lower bound exceeds n */
        fprintf(out, "  %5d   %17s   %10s   %6.2f%%   %19s   %10s\n", n, commas(minimal[n]),
                commas(cumulative), 100.0 * (double)cumulative / (double)s->count, commas(more),
                commas(s->count - cumulative - more));
    }
    if (last < capacity)
        fprintf(out, "  %2d-%-2d   %17s   %10s   %6.2f%%   %19s   %10s\n", last + 1,
                capacity, "0", commas(cumulative),
                100.0 * (double)cumulative / (double)s->count, "0", "0");
    fprintf(out, "  (Unresolved at n: neither certified at n or fewer nodes nor proven to need more.)\n");

    fprintf(out, "\n  largest certified minimum              %d node%s\n", max_minimal,
            max_minimal == 1 ? "" : "s");
    fprintf(out, "  largest node count of any witness      %d\n", max_witness);
    fprintf(out, "  witnesses not yet certified minimal    %s\n", commas(uncertified));
    fprintf(out, "  targets with no witness at all         %s\n", commas(none));

    fprintf(out, "\n");
    if (s->conflicts) {
        fprintf(out, "INVALID: the journal holds contradictory results for %s target(s).\n"
                     "Nothing can be certified from it until that is explained.\n",
                commas(s->conflicts));
    } else if (complete) {
        fprintf(out,
                "COMPLETE: every target has a certified minimum.  The largest is %d nodes,\n"
                "which is therefore the exact maximum over all %d-bit strings.\n",
                max_minimal, s->bits);
    } else {
        fprintf(out, "NOT COMPLETE: %s of %s targets have no certified minimum.\n",
                commas(s->count - s->by_status[FRB_MINIMAL]), commas(s->count));
        fprintf(out, "Proven so far about the maximum over all %d-bit strings: at least %d", s->bits,
                max_lower > capacity ? capacity : max_lower);
        if (all_have_witness) fprintf(out, ", at most %d.\n", max_witness);
        else fprintf(out, "; no upper bound, since some targets have no witness.\n");
        if (max_minimal)
            fprintf(out, "The largest certified minimum (%d) is not that maximum until the table is complete.\n",
                    max_minimal);
    }
    if (s->by_status[FRB_UNSUPPORTED])
        fprintf(out,
                "\nWARNING: %s target(s) have every node budget up to %d recorded as excluded.  A ring\n"
                "of L nodes emits any L-bit string, so that cannot be true: suspect the engine or the\n"
                "journal.  The table reports them as UNSUPPORTED rather than store anything for them.\n",
                commas(s->by_status[FRB_UNSUPPORTED]), capacity);
}

static void print_effort(FILE *out, const FrbState *s) {
    FrbBudgetEffort total;
    enum { TOP = 10 };
    uint64_t *steps, searched = 0, max_steps = 0, enum_graphs = 0, enum_ns = 0;
    uint32_t max_index = 0, top[TOP];
    int top_count = 0;
    char target[FRB_MAX_BITS + 1];
    memset(&total, 0, sizeof total);

    fprintf(out, "\nSearch effort by node budget\n");
    fprintf(out, "  Budget     Searches       Solved        Unsat    Limit      Logical steps   Time (s)\n");
    for (int b = 1; b <= FRB_MAX_NODES; b++) {
        const FrbBudgetEffort *e = &s->budget[b];
        if (!e->searches) continue;
        fprintf(out, "  %6d   %10s   %10s   %10s   %6s   %16s   %8.2f\n", b, commas(e->searches),
                commas(e->solved), commas(e->unsat), commas(e->limited), commas(e->steps),
                secs(e->ns));
        total.searches += e->searches;
        total.solved += e->solved;
        total.unsat += e->unsat;
        total.limited += e->limited;
        total.steps += e->steps;
        total.ns += e->ns;
    }
    if (!total.searches) fprintf(out, "  no searches yet\n");
    else
        fprintf(out, "   total   %10s   %10s   %10s   %6s   %16s   %8.2f\n", commas(total.searches),
                commas(total.solved), commas(total.unsat), commas(total.limited),
                commas(total.steps), secs(total.ns));

    steps = malloc((size_t)s->count * sizeof *steps);
    if (steps) {
        for (uint32_t i = 0; i < s->count; i++) {
            const FrbTargetState *ts = &s->targets[i];
            if (!ts->searches) continue;
            if (ts->steps >= max_steps) {
                if (ts->steps > max_steps || searched == 0) max_index = i;
                max_steps = ts->steps;
            }
            steps[searched++] = ts->steps;
            /* The costliest few, most steps first; among equals, table order. */
            if (top_count < TOP || ts->steps > s->targets[top[TOP - 1]].steps) {
                int at = top_count < TOP ? top_count++ : TOP - 1;
                while (at > 0 && s->targets[top[at - 1]].steps < ts->steps) {
                    top[at] = top[at - 1];
                    at--;
                }
                top[at] = i;
            }
        }
        if (searched) {
            qsort(steps, (size_t)searched, sizeof *steps, cmp_u64);
            frb_target_format(max_index, s->bits, target);
            fprintf(out, "\nSearch effort per target (logical steps; %s targets searched)\n",
                    commas(searched));
            fprintf(out, "  median %s   p90 %s   p99 %s   max %s (target %s)\n",
                    commas(steps[(searched - 1) / 2]), commas(steps[(searched - 1) * 9 / 10]),
                    commas(steps[(searched - 1) * 99 / 100]), commas(steps[searched - 1]), target);
            if (total.ns)
                fprintf(out, "  %.3g steps per second of solver time, %.1f microseconds per target\n",
                        (double)total.steps / secs(total.ns),
                        1e6 * secs(total.ns) / (double)searched);
            fprintf(out, "  the costliest:\n");
            for (int k = 0; k < top_count; k++) {
                const FrbTargetState *ts = &s->targets[top[k]];
                frb_target_format(top[k], s->bits, target);
                fprintf(out, "    %s  %15s steps  %9.3f ms  %u search%s", target, commas(ts->steps),
                        (double)ts->search_ns * 1e-6, (unsigned)ts->searches,
                        ts->searches == 1 ? "" : "es");
                if (ts->status == FRB_MINIMAL) fprintf(out, "  minimum %d nodes\n", ts->nodes);
                else fprintf(out, "  open\n");
            }
        }
        free(steps);
    }

    for (int n = 1; n <= FRB_NODE_CAPACITY; n++) {
        enum_graphs += s->enum_graphs[n];
        enum_ns += s->enum_ns[n];
    }
    fprintf(out, "\nTotals\n");
    fprintf(out, "  enumeration   %s graphs, %.2f s of processor time\n", commas(enum_graphs),
            secs(enum_ns));
    fprintf(out, "  search        %s logical steps, %.2f s of processor time\n", commas(total.steps),
            secs(total.ns));
    fprintf(out, "  sessions      %u, %.2f s of wall-clock time", (unsigned)s->sessions,
            secs(s->session_ns));
    if (s->sessions_unfinished)
        fprintf(out, " (not counting %u that has not ended: running now, or killed)",
                (unsigned)s->sessions_unfinished);
    fprintf(out, "\n");
    fprintf(out, "  journal       %s bytes in %u segment(s), %s records\n", commas(s->journal.bytes),
            (unsigned)s->journal.segments, commas(s->journal.records));
    if (s->journal.torn_bytes)
        fprintf(out,
                "                %s bytes at the end of %u segment(s) are an interrupted writer's\n"
                "                unfinished tail and were ignored\n",
                commas(s->journal.torn_bytes), (unsigned)s->journal.torn_segments);
    fprintf(out, "  final table   %s bytes (64-byte header + %s entries of %d bytes)\n",
            commas(FRB_HEADER_BYTES + (uint64_t)s->count * (uint64_t)s->format->entry_bytes),
            commas(s->count), s->format->entry_bytes);
    fprintf(out, "  peak disk     journal + one table: %s bytes (one table more while an existing\n"
                 "                output file is being replaced)\n",
            commas(s->journal.bytes + FRB_HEADER_BYTES +
                   (uint64_t)s->count * (uint64_t)s->format->entry_bytes));
}

void frb_report_state(FILE *out, const char *dir, const FrbConfig *cfg, const FrbState *s) {
    if (!out || !cfg || !s) return;
    fprintf(out, "FixedRunBomb(%d) construction in %s\n\n", cfg->bits, dir ? dir : "?");
    print_settings(out, cfg);
    print_stage_a(out, cfg, s);
    print_stage_b(out, cfg, s);
    print_histogram(out, s);
    print_effort(out, s);
}

/* ---- one target --------------------------------------------------------- */

typedef struct {
    FILE *out;
    uint32_t index, mirror;
    int symmetry, bits;
    uint64_t shown;
} TargetDump;

static const char *stamp(uint64_t unix_time) {
    static char buf[32];
    time_t t = (time_t)unix_time;
    struct tm tm;
    if (!gmtime_r(&t, &tm) || !strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%SZ", &tm))
        snprintf(buf, sizeof buf, "?");
    return buf;
}

static int dump_record(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    TargetDump *d = ctx;
    const char *whose;
    if (!rec) return 0;
    if (rec->type != FRB_REC_SEARCH && rec->type != FRB_REC_ENUM_WITNESS) return 0;
    if (rec->target == d->index) whose = "this string";
    else if (d->symmetry && rec->target == d->mirror) whose = "its complement";
    else return 0;
    d->shown++;
    fprintf(d->out, "  %s  segment %06u (session %u, worker %u, %s)  %s: ", stamp(rec->time),
            (unsigned)seg->number, (unsigned)seg->session, (unsigned)seg->worker, seg->engine, whose);
    if (rec->type == FRB_REC_ENUM_WITNESS) {
        fprintf(d->out, "enumeration of %d-node graphs, part %u, emitted it\n", rec->budget,
                (unsigned)rec->aux);
    } else {
        fprintf(d->out, "search at node budget %d: ", rec->budget);
        if (rec->result == REV_SOLVE_SOLVED) fprintf(d->out, "SOLVED with %d nodes", rec->nodes);
        else if (rec->result == REV_SOLVE_PROVEN_UNSAT) fprintf(d->out, "PROVEN_UNSAT");
        else if (rec->limit == REV_LIMIT_STEPS)
            fprintf(d->out, "RESOURCE_LIMIT (step limit %s)", commas(rec->max_steps));
        else fprintf(d->out, "RESOURCE_LIMIT (search stack full)");
        fprintf(d->out, ", %s steps, %.3f ms\n", commas(rec->steps), (double)rec->elapsed_ns * 1e-6);
    }
    return 0;
}

FrbError frb_report_target(FILE *out, const char *dir, const FrbConfig *cfg, const FrbState *s,
                           uint32_t index, char *err, size_t errlen) {
    static const char *const source[] = {"none", "exhaustive enumeration", "search",
                                         "the ring construction",
                                         "the REV the base Bomb stores for its first bits"};
    char target[FRB_MAX_BITS + 1];
    const FrbTargetState *ts;
    TargetDump d;
    FrbError e;
    if (!out || !dir || !cfg || !s || index >= s->count) return FRB_ERR_ARG;
    ts = &s->targets[index];
    frb_target_format(index, s->bits, target);

    fprintf(out, "Target %s (index %u of the %d-bit table)\n", target, (unsigned)index, s->bits);
    fprintf(out, "  status         %s%s\n", frb_status_str((FrbStatus)ts->status),
            ts->conflict ? "  ** contradictory results **" : "");
    if (ts->nodes) {
        RevGraph g;
        int have;
        fprintf(out, "  witness        %d nodes, from %s%s\n", ts->nodes,
                source[ts->source <= FRB_SRC_BASE ? ts->source : 0],
                ts->mirrored ? " (complement of the witness of the complement string)" : "");
        if (ts->source == FRB_SRC_RING) { /* not stored: it is what it always is */
            frb_ring_witness(index, s->bits, &g);
            have = 1;
        } else {
            have = frb_graph_unpack_as(s->format, frb_state_graph(s, index), ts->nodes, &g) == FRB_OK;
        }
        if (have) {
            fprintf(out, "                 Node   Color   Link0   Link1\n");
            for (int i = 0; i < g.node_count; i++)
                fprintf(out, "                 %-6d %-7s %-7d %d\n", i,
                        g.nodes[i].color == REV_WHITE ? "W (0)" : "B (1)", g.nodes[i].links[0],
                        g.nodes[i].links[1]);
        }
    } else {
        fprintf(out, "  witness        none\n");
    }
    fprintf(out, "  lower bound    %d: ", ts->lower_bound);
    if (ts->lower_bound == 1) {
        fprintf(out, "no node budget is excluded\n");
    } else {
        /* The same two sources derive() uses, shown separately. */
        int by_search = ts->unsat_max, by_enum = s->enum_complete;
        if (ts->nodes && ts->nodes - 1 < by_enum) by_enum = ts->nodes - 1;
        fprintf(out, "node budgets 1..%d are excluded\n", ts->lower_bound - 1);
        if (s->base_bits && ts->lower_bound - 1 > by_search && ts->lower_bound - 1 > by_enum)
            fprintf(out, "                 1..%d by the %d-bit Bomb: the first %d bits alone need %d nodes\n",
                    ts->lower_bound - 1, s->base_bits, s->base_bits, ts->lower_bound);
        if (by_enum >= 1)
            fprintf(out, "                 1..%d by exhaustive enumeration: no graph of that size emits it\n",
                    by_enum);
        if (by_search >= 1)
            fprintf(out, "                 up to %d by a search that returned PROVEN_UNSAT at budget %d\n",
                    by_search, by_search);
    }
    if (ts->limit_budget && ts->limit_budget >= ts->lower_bound && ts->status != FRB_MINIMAL) {
        if (ts->limit_kind == REV_LIMIT_STEPS)
            fprintf(out, "  open           the search at node budget %d was stopped by the step limit (%s);\n"
                         "                 that excludes nothing\n",
                    ts->limit_budget, commas(ts->limit_max_steps));
        else
            fprintf(out, "  open           the search at node budget %d filled the search stack; that\n"
                         "                 excludes nothing\n",
                    ts->limit_budget);
    }
    fprintf(out, "  own searches   %u, %s logical steps, %.3f ms\n", (unsigned)ts->searches,
            commas(ts->steps), (double)ts->search_ns * 1e-6);

    fprintf(out, "\nJournal records\n");
    memset(&d, 0, sizeof d);
    d.out = out;
    d.index = index;
    d.mirror = index ^ (s->count - 1u);
    d.symmetry = s->symmetry;
    d.bits = s->bits;
    e = frb_journal_scan(dir, cfg, dump_record, &d, NULL, err, errlen);
    if (e == FRB_OK && d.shown == 0) fprintf(out, "  none\n");
    return e;
}
