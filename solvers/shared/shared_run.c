/*
 * SharedSolver, the run kernel: the walk of shared_solver.c, made faster
 * without making it another walk.
 *
 * The reference walk keeps a frame for every bit a branch emits and takes
 * one turn of its loop for every step.  Most of its steps are spent where
 * nothing is to be decided: the links and colors the graph needs next are
 * set, so it runs, a bit at a time, until a bit is not wanted, and then
 * every frame of that stretch does its last step and goes.  Such a stretch
 * is a "run".  This kernel makes a run in a loop that keeps no frames, and
 * counts the steps of the frames it did not keep: a beginning that a run
 * only passes through has four of them, the first steps of its two frames
 * on the way down and their last ones on the way back.
 *
 * Everything else is the reference walk written as functions that call one
 * another where the reference pushes a frame: f_ctrl and ctrl_rest are a
 * CTRL frame, flow_slow and flow_links a FLOW frame, arrive what the first
 * step of a CTRL frame does once it knows the node it has come to.  The
 * moves are tried in the reference's order, so the first REV found for a
 * string is the reference's, and every step is counted at the beginning the
 * reference counts it at, so the steps reported for a string are the
 * reference's too.  tests/test_shared_solver.c holds this kernel to the
 * LevelSolver string by string, as it holds the reference, and to the
 * reference pass by pass: the strings found, in the order found, their
 * REVs, their steps, and the steps of the strings not found.
 *
 * Three things are kept otherwise than in the reference.
 *
 *   The graph.  Two masks say which nodes have a color and which of those
 *   are BLACK, and two words hold the links, four bits a node (15: not
 *   set).  A run reads them into registers and nothing else of the graph.
 *   That is why this kernel takes node budgets up to 15 and hands larger
 *   ones to the reference walk.
 *
 *   The clock.  One number, t: the steps made, less the three of each
 *   choice of bit, which belong to no beginning.  A beginning's count is
 *   brought up to date when the walk leaves it (`mark` is the clock when it
 *   came to stand there), not at every step.
 *
 *   What is wanted next.  `wm` holds, for where the walk stands, whether a
 *   wanted string goes on with 0 and whether one goes on with 1.
 *
 * A callback that asks to stop ends the pass by longjmp out of the walk;
 * nothing is concluded from a pass that was stopped, so nothing is tidied.
 */
#include "shared_internal.h"

#include <setjmp.h>
#include <string.h>

#define NIL 15u /* a link that is not set */
#define LINK(word, i) ((unsigned)((word) >> (4u * (i))) & 15u)

typedef struct {
    uint32_t colored, black; /* bit i: node i has a color; it is BLACK */
    uint64_t L[2];           /* four bits for each node: where its link 0 (its link 1) leads */
    unsigned Gn, max_nodes;
    /* Where the walk stands among the beginnings. */
    int gen;         /* bits emitted */
    uint32_t node;   /* the heap number of what they are, once gen >= P */
    unsigned wm;     /* bit b: a wanted string begins with what is emitted and then b */
    uint64_t *at;    /* the count of this beginning */
    uint64_t mark;   /* the clock up to which that count is up to date */
    uint64_t pending; /* steps of beginnings above this one that their counts do not have yet */
    /* The pass. */
    int X, P;
    uint32_t size, klass;
    uint8_t *tree;
    uint64_t *count;
    RevShared *s;
    uint64_t choices, next_poll, every;
    const RevSharedHooks *hooks;
    RevSharedStats *stats;
    jmp_buf out;
    RevSharedResult result;
} Walk;

static inline unsigned wanted_next(const Walk *w) {
    if (w->gen < w->P) return (w->tree[0] & 2) ? 1u << ((w->klass >> (w->P - 1 - w->gen)) & 1u) : 0u;
    return ((unsigned)w->tree[w->node >> 2] >> ((2u * w->node) & 7u)) & 3u;
}

static inline void stand(Walk *w) {
    w->at = w->gen < w->P ? &w->s->count_top[w->gen] : &w->count[w->node];
    w->wm = wanted_next(w);
}

static inline void descend(Walk *w, unsigned b, uint64_t t) {
    *w->at += t - w->mark;
    w->mark = t;
    w->gen++;
    if (w->gen > w->P) w->node = (w->node << 1) | b;
    else if (w->gen == w->P) w->node = 1;
    stand(w);
}

static inline void ascend(Walk *w, uint64_t t) {
    *w->at += t - w->mark;
    w->mark = t;
    if (w->gen > w->P) w->node >>= 1;
    w->gen--;
    stand(w);
}

static inline void set_link(Walk *w, unsigned d, unsigned i, unsigned to) {
    w->L[d] = (w->L[d] & ~((uint64_t)15 << (4u * i))) | ((uint64_t)to << (4u * i));
}

static void quit(Walk *w, RevSharedResult r) {
    w->result = r;
    longjmp(w->out, 1);
}

static void polled(Walk *w, uint64_t t) {
    if (w->hooks->poll(w->hooks->poll_ctx)) quit(w, REV_SHARED_STOPPED);
    w->next_poll = t + w->every;
}

/* The bit b completes a string: it is found, with the graph as it stands. */
static void leaf(Walk *w, unsigned b, uint64_t t) {
    RevShared *s = w->s;
    RevSharedNode G[REV_SHARED_RUN_MAX_NODES];
    RevGraph rev;
    uint32_t j = w->gen >= w->P ? ((w->node << 1) | b) - w->size : 0;
    uint32_t string = (w->klass << s->free_bits) | j, h = w->size + j;
    uint64_t own;
    *w->at += t - w->mark;
    w->mark = t;
    own = revsh_steps_of(s, j) + w->pending;
    for (unsigned i = 0; i < w->Gn; i++) {
        unsigned l0 = LINK(w->L[0], i), l1 = LINK(w->L[1], i);
        G[i].color = (uint8_t)((w->colored >> i) & 1u ? (w->black >> i) & 1u : REVSH_UND);
        G[i].link[0] = (uint8_t)(l0 == NIL ? REVSH_UND : l0);
        G[i].link[1] = (uint8_t)(l1 == NIL ? REVSH_UND : l1);
    }
    if (!revsh_export_checked(G, (int)w->Gn, w->X, string, &rev)) quit(w, REV_SHARED_INTERNAL);
    revsh_bit_clear(w->tree, h);
    while (h > 1 && !revsh_bit(w->tree, h ^ 1u)) {
        h >>= 1;
        revsh_bit_clear(w->tree, h);
    }
    w->stats->found++;
    w->stats->string_steps += own;
    if (w->hooks->found && w->hooks->found(w->hooks->found_ctx, string, &rev, own)) quit(w, REV_SHARED_STOPPED);
    w->wm = wanted_next(w);
}

static uint64_t f_ctrl(Walk *w, unsigned fp, unsigned cp, uint64_t t);
static uint64_t ctrl_rest(Walk *w, unsigned fp, unsigned cp, uint64_t t);
static uint64_t flow_links(Walk *w, unsigned fp, unsigned cp, unsigned d, uint64_t t);

/* A FLOW frame from its first step, as the reference makes it. */
static uint64_t flow_slow(Walk *w, unsigned fp, unsigned cp, uint64_t t) {
    unsigned d = (w->black >> cp) & 1u, n = LINK(w->L[d], fp);
    t++;
    if (n == NIL) return flow_links(w, fp, cp, d, t);
    return f_ctrl(w, n, cp, t) + 1;
}

/*
 * A run.  The bit `cc` of node `cp` has been emitted and is wanted, it is
 * not the last bit of a string, and the walk stands inside the class
 * (gen >= P): it goes down by that bit and on, FLOW frame and CTRL frame in
 * turn, for as long as the link and the colors they need are set and the
 * bit that comes is wanted.  Where that ends, at `node`, is one of four
 * things:
 *
 *   - the bit is not wanted: nothing more to do there;
 *   - the bit completes a string: leaf;
 *   - the FLOW frame's link is not set: flow_links tries every node for it;
 *   - the CTRL frame has something to decide: ctrl_rest.
 *
 * For the last three the walk is handed to the functions that keep frames,
 * and for that what only this loop knows is written down first (HAND):
 * where the walk stands, and that each beginning passed on the way down has
 * two steps its count does not have yet.  Then the way back: `tail` steps
 * for the beginning where the run ended, four for each one passed.
 *
 * t on return is the clock after the last step of the CTRL frame whose bit
 * this was, and the walk stands where it stood.
 */
static uint64_t run(Walk *w, unsigned fp, unsigned cp, unsigned cc, uint64_t t) {
    const uint32_t colored = w->colored, black = w->black, size = w->size;
    const uint64_t L0 = w->L[0], L1 = w->L[1];
    const uint8_t *const tree = w->tree;
    uint64_t *const count = w->count;
    uint32_t node = w->node;
    unsigned depth = 1, n;
    uint64_t tail;

    if (t >= w->next_poll) polled(w, t);
    count[node] += t - w->mark;
    node = (node << 1) | cc;
#define HAND() \
    (w->node = node, w->gen += (int)depth, w->at = &count[node], w->mark = t, \
     w->wm = ((unsigned)tree[node >> 2] >> ((2u * node) & 7u)) & 3u, w->pending += 2u * (depth - 1u))
#define TAKE() (w->gen -= (int)depth, w->pending -= 2u * (depth - 1u))
    for (;;) {
        uint32_t child;
        n = LINK(cc ? L1 : L0, fp); /* FLOW, first step: where fp's link leads */
        if (n == NIL) {
            HAND();
            t = flow_links(w, fp, cp, cc, t + 1);
            TAKE();
            tail = t - w->mark + 1;
            t += 1;
            break;
        }
        fp = n; /* CTRL, first step: fp's color, cp's link, the bit of the node it leads to */
        if (!((colored >> fp) & 1u)) goto decide;
        n = LINK((black >> fp) & 1u ? L1 : L0, cp);
        if (n == NIL || !((colored >> n) & 1u)) goto decide;
        cc = (black >> n) & 1u;
        child = (node << 1) | cc;
        if (!revsh_bit(tree, child)) {
            tail = 4;
            t += 4;
            break;
        }
        if (child >= size) {
            HAND();
            leaf(w, cc, t + 2);
            TAKE();
            tail = t - w->mark + 4;
            t += 4;
            break;
        }
        t += 2;
        node = child;
        cp = n;
        depth++;
        continue;
    decide:
        HAND();
        t = ctrl_rest(w, fp, cp, t + 2);
        TAKE();
        tail = t - w->mark + 2;
        t += 2;
        break;
    }
#undef HAND
#undef TAKE
    count[node] += tail;
    node >>= 1;
    while (--depth) {
        count[node] += 4;
        node >>= 1;
        t += 2;
    }
    w->node = node;
    w->at = &count[node];
    w->mark = t;
    w->wm = ((unsigned)tree[node >> 2] >> ((2u * node) & 7u)) & 3u;
    return t;
}

/* Down by a bit that is wanted and is not the last of a string, and back:
 * the FLOW frame below a CTRL frame, and that frame's last step. */
static inline uint64_t down(Walk *w, unsigned fp, unsigned n, unsigned b, uint64_t t) {
    if (w->gen >= w->P) return run(w, fp, n, b, t);
    descend(w, b, t);
    t = flow_slow(w, fp, n, t) + 1;
    ascend(w, t);
    return t;
}

/* Control has come to node n: what is left of the first step of a CTRL frame. */
static inline uint64_t arrive(Walk *w, unsigned fp, unsigned n, uint64_t t) {
    unsigned cc;
    if (!((w->colored >> n) & 1u)) { /* either bit: the choice, whose three steps t does not count */
        w->choices++;
        w->colored |= 1u << n;
        for (unsigned bit = 0; bit < 2; bit++) {
            if (!((w->wm >> bit) & 1u)) continue;
            if (bit) w->black |= 1u << n;
            if (w->gen + 1 == w->X) leaf(w, bit, t);
            else t = down(w, fp, n, bit, t);
        }
        w->black &= ~(1u << n);
        w->colored &= ~(1u << n);
        return t;
    }
    cc = (w->black >> n) & 1u;
    if (!((w->wm >> cc) & 1u)) return t;
    if (w->gen + 1 == w->X) {
        leaf(w, cc, t);
        return t;
    }
    return down(w, fp, n, cc, t);
}

/* A CTRL frame after its first step has been counted. */
static uint64_t ctrl_rest(Walk *w, unsigned fp, unsigned cp, uint64_t t) {
    unsigned d, n;
    if (!((w->colored >> fp) & 1u)) { /* fp has no color: each in turn */
        w->colored |= 1u << fp;
        t = f_ctrl(w, fp, cp, t + 1) + 1;
        w->black |= 1u << fp;
        t = f_ctrl(w, fp, cp, t + 1) + 1;
        w->black &= ~(1u << fp);
        w->colored &= ~(1u << fp);
        return t + 1;
    }
    d = (w->black >> fp) & 1u;
    n = LINK(w->L[d], cp);
    if (n != NIL) return arrive(w, fp, n, t);
    for (unsigned it = 0; it < w->Gn; it++) { /* cp's link is not set: every node there is */
        if (((w->colored >> it) & 1u) && !((w->wm >> ((w->black >> it) & 1u)) & 1u)) {
            t += 3; /* the link is set, the node's bit is not wanted, the link is taken back */
            continue;
        }
        set_link(w, d, cp, it);
        t = arrive(w, fp, it, t + 2) + 1;
    }
    t += 2;
    if (w->Gn < w->max_nodes) { /* and a new one */
        unsigned nn = w->Gn++;
        set_link(w, d, cp, nn);
        t = arrive(w, fp, nn, t + 1) + 1;
        w->Gn--;
    }
    set_link(w, d, cp, NIL);
    return t;
}

static uint64_t f_ctrl(Walk *w, unsigned fp, unsigned cp, uint64_t t) {
    t++;
    if (t >= w->next_poll) polled(w, t);
    return ctrl_rest(w, fp, cp, t);
}

/* The same, with the usual case in line: fp has its color and cp its link. */
static inline uint64_t ctrl_from(Walk *w, unsigned fp, unsigned cp, uint64_t t) {
    unsigned n;
    t++;
    if (((w->colored >> fp) & 1u) && (n = LINK(w->L[(w->black >> fp) & 1u], cp)) != NIL) return arrive(w, fp, n, t);
    return ctrl_rest(w, fp, cp, t);
}

/* A FLOW frame whose link is not set, after its first step: every node
 * there is, and a new one.  Each is a FLOW frame of its own in the
 * reference, whose first and last steps are the 2 and the 2 here. */
static uint64_t flow_links(Walk *w, unsigned fp, unsigned cp, unsigned d, uint64_t t) {
    for (unsigned it = 0; it < w->Gn; it++) {
        set_link(w, d, fp, it);
        t = ctrl_from(w, it, cp, t + 2) + 2;
    }
    t += 2;
    if (w->Gn < w->max_nodes) {
        unsigned nn = w->Gn++;
        set_link(w, d, fp, nn);
        t = f_ctrl(w, nn, cp, t + 1) + 2;
        w->Gn--;
    }
    set_link(w, d, fp, NIL);
    return t;
}

/* The walk, from the one node without color or links.  Kept apart so that
 * nothing but its two arguments is alive across the setjmp. */
static RevSharedResult walk_all(Walk *w, uint64_t *t) {
    if (setjmp(w->out)) return w->result;
    *t = f_ctrl(w, 0, 0, 0);
    return REV_SHARED_DONE;
}

RevSharedResult rev_shared_pass_run(RevShared *s, uint32_t klass, int max_nodes, const uint8_t *wanted,
                                    const RevSharedHooks *hooks, RevSharedStats *stats) {
    static const RevSharedHooks none;
    RevSharedStats local;
    RevSharedResult r;
    uint64_t t = 0;
    Walk w;

    if (max_nodes > REV_SHARED_RUN_MAX_NODES) return rev_shared_pass(s, klass, max_nodes, wanted, hooks, stats);
    if (!stats) stats = &local;
    if (!revsh_begin(s, klass, max_nodes, wanted, stats)) return REV_SHARED_INVALID;
    if (!hooks) hooks = &none;

    memset(&w, 0, sizeof w);
    w.s = s;
    w.tree = s->tree;
    w.count = s->count;
    w.X = s->bits;
    w.P = s->class_bits;
    w.size = s->size;
    w.klass = klass;
    w.max_nodes = (unsigned)max_nodes;
    w.hooks = hooks;
    w.stats = stats;
    w.every = hooks->poll_every ? hooks->poll_every : (uint64_t)1 << 20;
    w.next_poll = hooks->poll ? w.every : UINT64_MAX;
    w.Gn = 1;
    w.L[0] = w.L[1] = ~(uint64_t)0;
    w.node = 1;
    stand(&w);
    if (s->tree[0] & 2) {
        r = walk_all(&w, &t);
        if (r != REV_SHARED_DONE) return r;
    }
    *w.at += t - w.mark;

    /* The walk is back where it began, and has left the graph as it found it. */
    if (w.gen != 0 || w.Gn != 1 || w.pending || w.colored || w.black || ~w.L[0] || ~w.L[1]) return REV_SHARED_INTERNAL;
    return revsh_end(s, t + 3 * w.choices, w.choices, 0, stats);
}
