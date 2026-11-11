/*
 * LevelSolver, C baseline.  Parameterized adaptation of legacy_original.c.
 *
 * search() below is the original solve() with these changes and no others:
 *   - the target and its length are runtime parameters instead of constants;
 *   - push() refuses to write past the end of the stack, and the search
 *     returns REV_SOLVE_RESOURCE_LIMIT instead of corrupting memory;
 *   - loop iterations and peak stack depth are counted, and an optional
 *     iteration limit is enforced;
 *   - on success it returns instead of printing; the caller copies the graph
 *     out, completes it and has it independently verified.
 * States, transitions, undo actions and the order in which moves are tried
 * are the same, so both solvers visit the same search tree in the same order
 * and return the same first solution.
 */
#include "level_solver.h"

#define UNDECIDED REV_UNDECIDED

typedef RevNode Node;
typedef enum { CTRL, FLOW } Func;
typedef enum { INIT, LOOP_C, LOOP_C_R, LOOP_L, LOOP_L_R, TRY_N, TRY_N_R, MATCH, SIMPLE_R } State;
typedef struct { Func fn; int fp, cp; State st; int it, dir, nxt, set; } Frame;

static const char *target_str;
#define target(i) (target_str[i] - '0')
static size_t target_len;

static Node G[REV_LEVEL_MAX_NODES];
static Frame S[REV_LEVEL_MAX_STACK];
static int Gn, Sp;
static size_t gen;

static uint64_t steps, step_limit;
static int peak;
static RevLevelLimit limit_hit;

#define push(fn_, fp_, cp_) \
    do { \
        if (Sp == REV_LEVEL_MAX_STACK) { limit_hit = REV_LIMIT_STACK; return REV_SOLVE_RESOURCE_LIMIT; } \
        S[Sp++] = (Frame){fn_, fp_, cp_, INIT, 0, 0, 0, 0}; \
        if (Sp > peak) peak = Sp; \
    } while (0)

static RevSolveResult search(int max) {
    G[0] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
    Gn = 1; gen = 0; Sp = 0;
    push(CTRL, 0, 0);
    while (Sp > 0) {
        if (steps == step_limit) { limit_hit = REV_LIMIT_STEPS; return REV_SOLVE_RESOURCE_LIMIT; }
        steps++;
        Frame *f = &S[Sp-1];
        int fp = f->fp, cp = f->cp;
        if (f->fn == CTRL) {
            switch (f->st) {
            case INIT: {
                int d = G[fp].color;
                if (d == UNDECIDED) { f->st = LOOP_C; f->it = 0; }
                else {
                    int nxt = G[cp].links[d];
                    if (nxt == UNDECIDED) { f->dir = d; f->st = LOOP_L; f->it = 0; }
                    else {
                        int cc = G[nxt].color;
                        if (cc == UNDECIDED) { G[nxt].color = target(gen); f->set = 1; }
                        else if (cc != target(gen)) { Sp--; break; }
                        else f->set = 0;
                        if (++gen == target_len) return REV_SOLVE_SOLVED;
                        f->nxt = nxt; f->st = MATCH; push(FLOW, fp, nxt);
                    }
                }
                break;
            }
            case LOOP_C:
                if (f->it < 2) { G[fp].color = f->it++; f->st = LOOP_C_R; push(CTRL, fp, cp); }
                else Sp--;
                break;
            case LOOP_C_R: G[fp].color = UNDECIDED; f->st = LOOP_C; break;
            case LOOP_L:
                if (f->it < Gn) { G[cp].links[f->dir] = f->it++; f->st = LOOP_L_R; push(CTRL, fp, cp); }
                else { G[cp].links[f->dir] = UNDECIDED; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[cp].links[f->dir] = UNDECIDED; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[cp].links[f->dir] = Gn;
                    G[Gn++] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
                    f->st = TRY_N_R; push(CTRL, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[cp].links[f->dir] = UNDECIDED; Sp--; break;
            case MATCH:
                if (f->set) G[f->nxt].color = UNDECIDED;
                gen--; Sp--;
                break;
            default: break;
            }
        } else { /* FLOW */
            switch (f->st) {
            case INIT: {
                int d = G[cp].color;
                if (d == UNDECIDED) { f->st = LOOP_C; f->it = 0; }
                else {
                    int nxt = G[fp].links[d];
                    if (nxt == UNDECIDED) { f->dir = d; f->st = LOOP_L; f->it = 0; }
                    else { f->st = SIMPLE_R; push(CTRL, nxt, cp); }
                }
                break;
            }
            case LOOP_C:
                if (f->it < 2) { G[cp].color = f->it++; f->st = LOOP_C_R; push(FLOW, fp, cp); }
                else Sp--;
                break;
            case LOOP_C_R: G[cp].color = UNDECIDED; f->st = LOOP_C; break;
            case LOOP_L:
                if (f->it < Gn) { G[fp].links[f->dir] = f->it++; f->st = LOOP_L_R; push(FLOW, fp, cp); }
                else { G[fp].links[f->dir] = UNDECIDED; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[fp].links[f->dir] = UNDECIDED; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[fp].links[f->dir] = Gn;
                    G[Gn++] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
                    f->st = TRY_N_R; push(FLOW, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[fp].links[f->dir] = UNDECIDED; Sp--; break;
            case SIMPLE_R: Sp--; break;
            default: break;
            }
        }
    }
    return REV_SOLVE_PROVEN_UNSAT;
}

/*
 * Copy the solver's graph out and give every field the search never had to
 * decide a valid value.  Such fields are never read while emitting the
 * target, so any valid value keeps the prefix intact; we use node 0 for
 * links and WHITE for colors.  They do affect output beyond the prefix.
 */
static void export_graph(RevGraph *out) {
    out->node_count = Gn;
    for (int i = 0; i < Gn; i++) {
        Node n = G[i];
        if (n.color == UNDECIDED) n.color = REV_WHITE;
        if (n.links[0] == UNDECIDED) n.links[0] = REV_ROOT;
        if (n.links[1] == UNDECIDED) n.links[1] = REV_ROOT;
        out->nodes[i] = n;
    }
}

RevSolveResult rev_level_solve_ex(const char *tgt, size_t tgt_len, int max_nodes,
                                  const RevLevelOptions *opt, RevGraph *result,
                                  RevLevelStats *stats) {
    if (stats) { stats->steps = 0; stats->peak_stack = 0; stats->limit = REV_LIMIT_NONE; }
    if (result) result->node_count = 0;
    if (!result || rev_target_validate(tgt, tgt_len) != REV_OK) return REV_SOLVE_INVALID_INPUT;
    if (max_nodes < 1 || max_nodes > REV_LEVEL_MAX_NODES) return REV_SOLVE_INVALID_INPUT;

    target_str = tgt;
    target_len = tgt_len;
    steps = 0;
    step_limit = (opt && opt->max_steps) ? opt->max_steps : UINT64_MAX;
    peak = 0;
    limit_hit = REV_LIMIT_NONE;

    RevSolveResult r = search(max_nodes);

    if (stats) { stats->steps = steps; stats->peak_stack = peak; stats->limit = limit_hit; }
    if (r != REV_SOLVE_SOLVED) return r;

    export_graph(result);
    if (rev_verify_prefix(result, tgt, tgt_len, NULL) != REV_OK) {
        result->node_count = 0;
        return REV_SOLVE_INTERNAL_ERROR;
    }
    return REV_SOLVE_SOLVED;
}

RevSolveResult rev_level_solve(const char *tgt, size_t tgt_len, int max_nodes, RevGraph *result) {
    return rev_level_solve_ex(tgt, tgt_len, max_nodes, NULL, result, NULL);
}

const char *rev_solve_result_str(RevSolveResult r) {
    switch (r) {
    case REV_SOLVE_SOLVED: return "SOLVED";
    case REV_SOLVE_PROVEN_UNSAT: return "PROVEN_UNSAT";
    case REV_SOLVE_RESOURCE_LIMIT: return "RESOURCE_LIMIT";
    case REV_SOLVE_INVALID_INPUT: return "INVALID_INPUT";
    case REV_SOLVE_INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "?";
}

const char *rev_level_limit_str(RevLevelLimit l) {
    switch (l) {
    case REV_LIMIT_NONE: return "none";
    case REV_LIMIT_STACK: return "search stack full";
    case REV_LIMIT_STEPS: return "step limit reached";
    }
    return "?";
}
