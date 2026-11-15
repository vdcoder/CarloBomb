/*
 * SharedSolver.  See shared_solver.h for what it is and what it promises.
 *
 * The loop is the LevelSolver's loop (solvers/level/fastest/c), written
 * again here so that the two share no line, with its two differences:
 *
 *   - where the LevelSolver compares the bit a branch emits with the next
 *     bit of its target, this asks whether a wanted string begins with what
 *     the branch has emitted, that bit included;
 *   - where the LevelSolver gives a node that has no color the next bit of
 *     its target, this tries 0 and then 1 (states LOOP_B, LOOP_B_R).
 *
 * The beginnings of the strings of a class form a binary tree.  Its nodes
 * are numbered as a heap: the class's own beginning is 1, the beginnings one
 * bit longer are 2 and 3, and string j of the class is size + j.  `tree`
 * has a bit for each: set while a wanted string begins with it.  `count`
 * has a number for each beginning that is not a whole string: the steps made
 * while exactly that had been emitted.  The beginnings shorter than the
 * class's own lie on one path and are counted in count_top.
 */
#include "shared_solver.h"

#include <stdlib.h>
#include <string.h>

#define UND 255

enum { CTRL = 0, FLOW = 1 };
enum { INIT, LOOP_C, LOOP_C_R, LOOP_L, LOOP_L_R, TRY_N, TRY_N_R, MATCH, SIMPLE_R, LOOP_B, LOOP_B_R };
enum { STACK_FRAMES = 6 * REV_SHARED_MAX_BITS + 16 };

typedef struct { uint8_t color, link[2]; } Node;
typedef struct { uint8_t fn, st, fp, cp, it, dir, nxt; } Frame;

struct RevShared {
    int bits, class_bits, free_bits;
    uint32_t size;    /* strings in a class */
    uint8_t *tree;    /* a bit for heap numbers 1 .. 2 * size - 1 */
    uint8_t *asked;   /* a bit for each string of the class: wanted when the pass began */
    uint64_t *count;  /* [1 .. size - 1] */
    uint64_t count_top[REV_SHARED_MAX_BITS + 1];
    int done;         /* the last pass ran to its end */
    Node G[REV_SHARED_MAX_NODES];
    Frame S[STACK_FRAMES];
};

const char *rev_shared_result_str(RevSharedResult r) {
    switch (r) {
    case REV_SHARED_DONE: return "done";
    case REV_SHARED_STOPPED: return "stopped";
    case REV_SHARED_INVALID: return "invalid argument";
    case REV_SHARED_INTERNAL: return "internal error";
    }
    return "?";
}

RevShared *rev_shared_create(int bits, int class_bits) {
    RevShared *s;
    int free_bits = bits - class_bits;
    if (bits < 1 || bits > REV_SHARED_MAX_BITS || class_bits < 0 || class_bits > bits ||
        free_bits > REV_SHARED_MAX_FREE)
        return NULL;
    s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->bits = bits;
    s->class_bits = class_bits;
    s->free_bits = free_bits;
    s->size = (uint32_t)1 << free_bits;
    s->tree = calloc((size_t)s->size / 4 + 1, 1);
    s->asked = calloc((size_t)s->size / 8 + 1, 1);
    s->count = calloc((size_t)s->size, sizeof *s->count);
    if (!s->tree || !s->asked || !s->count) {
        rev_shared_destroy(s);
        return NULL;
    }
    return s;
}

void rev_shared_destroy(RevShared *s) {
    if (!s) return;
    free(s->tree);
    free(s->asked);
    free(s->count);
    free(s);
}

uint32_t rev_shared_class_size(const RevShared *s) { return s ? s->size : 0; }

static inline int bit_of(const uint8_t *map, uint32_t i) { return (map[i >> 3] >> (i & 7)) & 1; }
static inline void bit_clear(uint8_t *map, uint32_t i) { map[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static inline void bit_set(uint8_t *map, uint32_t i) { map[i >> 3] |= (uint8_t)(1u << (i & 7)); }

/* The steps made at the beginnings of string j: those of the search for it alone. */
static uint64_t steps_of(const RevShared *s, uint32_t j) {
    uint64_t sum = 0;
    for (int d = 0; d < s->class_bits; d++) sum += s->count_top[d];
    for (uint32_t h = (s->size + j) >> 1; h >= 1; h >>= 1) sum += s->count[h];
    return sum;
}

int rev_shared_unfound(const RevShared *s, uint32_t j) {
    return s && s->done && j < s->size && bit_of(s->tree, s->size + j);
}

uint64_t rev_shared_unfound_steps(const RevShared *s, uint32_t j) {
    return rev_shared_unfound(s, j) ? steps_of(s, j) : 0;
}

/* The graph as it stands, for a string all of whose bits it has emitted.
 * 1 if the interpreter of the REV core gets that string out of it. */
static int export_checked(const Node *G, int nodes, int bits, uint32_t string, RevGraph *g) {
    RevMachine m;
    uint32_t emitted = 0;
    g->node_count = nodes;
    for (int i = 0; i < nodes; i++) {
        g->nodes[i].color = G[i].color == UND ? 0 : G[i].color;
        g->nodes[i].links[0] = G[i].link[0] == UND ? 0 : G[i].link[0];
        g->nodes[i].links[1] = G[i].link[1] == UND ? 0 : G[i].link[1];
    }
    if (rev_graph_validate(g) != REV_OK) return 0;
    rev_machine_reset(&m);
    for (int i = 0; i < bits; i++) emitted = (emitted << 1) | (uint32_t)rev_machine_next_bit(g, &m);
    return emitted == string;
}

RevSharedResult rev_shared_pass(RevShared *s, uint32_t klass, int max_nodes, const uint8_t *wanted,
                                const RevSharedHooks *hooks, RevSharedStats *stats) {
    static const RevSharedHooks none;
    RevSharedStats local;
    Node *G;
    Frame *S;
    uint8_t *tree;
    uint64_t steps = 0, choices = 0, every, next_poll, counted = 0;
    uint64_t *at;
    uint32_t prefix = 0, node = 1, size;
    int X, P, Gn = 1, Sp = 0, gen = 0, peak = 0;

    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    if (!s) return REV_SHARED_INVALID;
    s->done = 0;
    if (!wanted || max_nodes < 1 || max_nodes > REV_SHARED_MAX_NODES ||
        (s->class_bits < 32 && s->class_bits > 0 && (klass >> s->class_bits) != 0) ||
        (s->class_bits == 0 && klass != 0))
        return REV_SHARED_INVALID;
    if (!hooks) hooks = &none;
    every = hooks->poll_every ? hooks->poll_every : (uint64_t)1 << 20;
    next_poll = hooks->poll ? every : UINT64_MAX;
    X = s->bits;
    P = s->class_bits;
    size = s->size;
    G = s->G;
    S = s->S;
    tree = s->tree;

    /* What is wanted, and every beginning of it. */
    memset(tree, 0, (size_t)size / 4 + 1);
    memset(s->asked, 0, (size_t)size / 8 + 1);
    memset(s->count, 0, (size_t)size * sizeof *s->count);
    memset(s->count_top, 0, sizeof s->count_top);
    for (uint32_t j = 0; j < size; j++)
        if (bit_of(wanted, j)) {
            bit_set(s->asked, j);
            bit_set(tree, size + j);
            stats->wanted++;
        }
    for (uint32_t h = size - 1; h >= 1; h--)
        if (bit_of(tree, 2 * h) || bit_of(tree, 2 * h + 1)) bit_set(tree, h);

#define PUSH(fn_, fp_, cp_) \
    do { \
        if (Sp == STACK_FRAMES) return REV_SHARED_INTERNAL; \
        S[Sp++] = (Frame){(uint8_t)(fn_), INIT, (uint8_t)(fp_), (uint8_t)(cp_), 0, 0, 0}; \
        if (Sp > peak) peak = Sp; \
    } while (0)
/* Is a string wanted that begins with what has been emitted and then `b`? */
#define WANTED(b) \
    (gen < P ? (((klass >> (P - 1 - gen)) & 1u) == (uint32_t)(b) && (tree[0] & 2)) \
             : bit_of(tree, (node << 1) | (uint32_t)(b)))
#define DESCEND(b) \
    do { \
        prefix = (prefix << 1) | (uint32_t)(b); \
        gen++; \
        if (gen > P) node = (node << 1) | (uint32_t)(b); \
        else if (gen == P) node = 1; \
    } while (0)
#define ASCEND() \
    do { \
        if (gen > P) node >>= 1; \
        gen--; \
        prefix >>= 1; \
    } while (0)
#define MOVED() (at = gen < P ? &s->count_top[gen] : &s->count[node])
/* All bits of a string are out: it is found, with the graph as it stands. */
#define LEAF() \
    do { \
        RevGraph rev; \
        uint64_t own = steps_of(s, node - size); \
        uint32_t h = node; \
        if (!export_checked(G, Gn, X, prefix, &rev)) return REV_SHARED_INTERNAL; \
        bit_clear(tree, h); \
        while (h > 1 && !bit_of(tree, h ^ 1u)) { \
            h >>= 1; \
            bit_clear(tree, h); \
        } \
        stats->found++; \
        stats->string_steps += own; \
        if (hooks->found && hooks->found(hooks->found_ctx, prefix, &rev, own)) return REV_SHARED_STOPPED; \
    } while (0)

    G[0] = (Node){UND, {UND, UND}};
    if (P == 0) node = 1;
    MOVED();
    if (tree[0] & 2) PUSH(CTRL, 0, 0);
    while (Sp > 0) {
        Frame *f = &S[Sp - 1];
        int fp = f->fp, cp = f->cp;
        steps++;
        ++*at;
        if (steps >= next_poll) {
            if (hooks->poll(hooks->poll_ctx)) return REV_SHARED_STOPPED;
            next_poll = steps + every;
        }
        if (f->fn == CTRL) {
            switch (f->st) {
            case INIT: {
                int d = G[fp].color;
                if (d == UND) {
                    f->st = LOOP_C;
                    f->it = 0;
                } else {
                    int nxt = G[cp].link[d];
                    if (nxt == UND) {
                        f->dir = (uint8_t)d;
                        f->st = LOOP_L;
                        f->it = 0;
                    } else {
                        int cc = G[nxt].color;
                        f->nxt = (uint8_t)nxt;
                        if (cc == UND) { /* either bit: the choice */
                            f->st = LOOP_B;
                            f->it = 0;
                            choices++;
                            break;
                        }
                        if (!WANTED(cc)) {
                            Sp--;
                            break;
                        }
                        DESCEND(cc);
                        if (gen == X) {
                            LEAF();
                            ASCEND();
                            Sp--;
                            break;
                        }
                        MOVED();
                        f->st = MATCH;
                        PUSH(FLOW, fp, nxt);
                    }
                }
                break;
            }
            case LOOP_B: /* a step the search for one string does not have */
                --*at;
                if (f->it < 2) {
                    int bit = f->it++;
                    if (!WANTED(bit)) break;
                    G[f->nxt].color = (uint8_t)bit;
                    DESCEND(bit);
                    if (gen == X) {
                        LEAF();
                        ASCEND();
                        G[f->nxt].color = UND;
                        break;
                    }
                    MOVED();
                    f->st = LOOP_B_R;
                    PUSH(FLOW, fp, f->nxt);
                } else {
                    Sp--;
                }
                break;
            case LOOP_B_R:
                G[f->nxt].color = UND;
                ASCEND();
                MOVED();
                f->st = LOOP_B;
                break;
            case LOOP_C:
                if (f->it < 2) {
                    G[fp].color = f->it++;
                    f->st = LOOP_C_R;
                    PUSH(CTRL, fp, cp);
                } else {
                    Sp--;
                }
                break;
            case LOOP_C_R:
                G[fp].color = UND;
                f->st = LOOP_C;
                break;
            case LOOP_L:
                if (f->it < Gn) {
                    G[cp].link[f->dir] = f->it++;
                    f->st = LOOP_L_R;
                    PUSH(CTRL, fp, cp);
                } else {
                    G[cp].link[f->dir] = UND;
                    f->st = TRY_N;
                }
                break;
            case LOOP_L_R:
                G[cp].link[f->dir] = UND;
                f->st = LOOP_L;
                break;
            case TRY_N:
                if (Gn < max_nodes) {
                    G[cp].link[f->dir] = (uint8_t)Gn;
                    G[Gn++] = (Node){UND, {UND, UND}};
                    f->st = TRY_N_R;
                    PUSH(CTRL, fp, cp);
                } else {
                    Sp--;
                }
                break;
            case TRY_N_R:
                Gn--;
                G[cp].link[f->dir] = UND;
                Sp--;
                break;
            case MATCH:
                ASCEND();
                MOVED();
                Sp--;
                break;
            default:
                return REV_SHARED_INTERNAL;
            }
        } else {
            switch (f->st) {
            case INIT: {
                int d = G[cp].color, nxt;
                if (d == UND) return REV_SHARED_INTERNAL; /* the node just arrived at has its color */
                nxt = G[fp].link[d];
                if (nxt == UND) {
                    f->dir = (uint8_t)d;
                    f->st = LOOP_L;
                    f->it = 0;
                } else {
                    f->st = SIMPLE_R;
                    PUSH(CTRL, nxt, cp);
                }
                break;
            }
            case LOOP_L:
                if (f->it < Gn) {
                    G[fp].link[f->dir] = f->it++;
                    f->st = LOOP_L_R;
                    PUSH(FLOW, fp, cp);
                } else {
                    G[fp].link[f->dir] = UND;
                    f->st = TRY_N;
                }
                break;
            case LOOP_L_R:
                G[fp].link[f->dir] = UND;
                f->st = LOOP_L;
                break;
            case TRY_N:
                if (Gn < max_nodes) {
                    G[fp].link[f->dir] = (uint8_t)Gn;
                    G[Gn++] = (Node){UND, {UND, UND}};
                    f->st = TRY_N_R;
                    PUSH(FLOW, fp, cp);
                } else {
                    Sp--;
                }
                break;
            case TRY_N_R:
                Gn--;
                G[fp].link[f->dir] = UND;
                Sp--;
                break;
            case SIMPLE_R:
                Sp--;
                break;
            default:
                return REV_SHARED_INTERNAL;
            }
        }
    }
#undef PUSH
#undef WANTED
#undef DESCEND
#undef ASCEND
#undef MOVED
#undef LEAF

    /*
     * The pass is over.  Before anything is concluded from it, the two
     * things the conclusions rest on are checked.
     *
     * Every step was counted at one beginning, the three of each choice at
     * none.
     */
    for (int d = 0; d < P; d++) counted += s->count_top[d];
    for (uint32_t h = 1; h < size; h++) counted += s->count[h];
    if (counted != steps - 3 * choices || gen != 0 || Gn != 1) return REV_SHARED_INTERNAL;
    /*
     * A beginning is marked exactly if a string below it still is, and the
     * strings still marked are those that were wanted and not found.  So
     * every string that is left was wanted, with all its beginnings, from
     * the first step to the last: marks are only ever taken away.
     */
    for (uint32_t h = size - 1; h >= 1; h--)
        if (bit_of(tree, h) != (bit_of(tree, 2 * h) | bit_of(tree, 2 * h + 1))) return REV_SHARED_INTERNAL;
    {
        uint64_t left = 0;
        for (uint32_t j = 0; j < size; j++) {
            if (!bit_of(tree, size + j)) continue;
            if (!bit_of(s->asked, j)) return REV_SHARED_INTERNAL;
            left++;
            stats->string_steps += steps_of(s, j);
        }
        if (left + stats->found != stats->wanted) return REV_SHARED_INTERNAL;
    }
    stats->steps = steps;
    stats->choices = choices;
    stats->peak_stack = peak;
    s->done = 1;
    return REV_SHARED_DONE;
}
