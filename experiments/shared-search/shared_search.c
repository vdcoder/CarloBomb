/*
 * shared_search: one search for many strings at once.  An experiment; see
 * README.md in this directory and benchmarks/DISCOVERY.md.
 *
 *   shared_search --bits X [--prefix-bits P] [--class K | --sample N [--seed S]]
 *                 [--jobs J] [--base BOMB] [--check BOMB] [--max-budget N]
 *                 [--bound prefix|suffix|all] [--journal WORK] [--count]
 *                 [--expect-totals SEARCHES:STEPS]
 *                 [--expect 1:2,2:6,...] [--out TABLE] [--expect-table-crc HEX]
 *                 [--jobs-out FILE] [--no-verify] [--depths]
 *
 * The LevelSolver proves "no REV of N nodes emits S" by a depth-first search
 * over partly decided graphs: the machine is run, every link and color it
 * meets undecided is tried in turn, and a branch dies when the bit it emits
 * is not the next bit of S.  Two strings that begin alike are searched alike
 * as far as they agree: the ladder repeats that part for each of them.
 *
 * This search has no single target.  A branch lives as long as SOME string
 * that is still wanted begins with what it has emitted, and a node that the
 * reference would give the target's color gets each color in turn.  Seen from
 * any one string, the states visited are those of the reference search for
 * it, in the same order: so the first REV found for a string is the REV the
 * reference finds, and a search that ends without one has shown what the
 * reference would have shown.  A state shared by many strings is visited
 * once.
 *
 * A job is the set of strings of X bits with one beginning of P bits (a
 * class); P = 1, the default, is all strings that start with 0 at once.  It runs one pass per node budget N,
 * from the smallest any of its strings can need.  Wanted in pass N: the
 * strings no smaller budget found.  A string found is no longer wanted from
 * that moment on.  When the pass ends, every string still wanted has no REV
 * of N nodes.  Jobs are independent of one another and of the order they run
 * in, so the outcome does not depend on how many processes there are.
 *
 * One string of each complement pair: those that start with 0.
 *
 * With --base, a complete Bomb for X-1 bits, a level is built as the ladder
 * builds it: the REV stored for the first X-1 bits settles one of the two
 * strings that begin with them, and the other is wanted from the budget its
 * first bits need (its last X-1 bits, less one node, if that is more).
 *
 * With --check, a complete Bomb for X bits, every string found is compared
 * with the table: the node count, and the REV link for link and color for
 * color.
 *
 * What is found is not taken from the search on trust.  Every REV is kept as
 * it was recorded and, when the search is over, run on the reference
 * interpreter of core/rev, which shares nothing with the search: it must be
 * a REV of the number of nodes recorded, emit the string it is recorded for,
 * visit every one of its nodes on the way, and its complement must emit the
 * complement.  (--no-verify keeps no REVs, for samples of lengths whose
 * REVs do not fit in memory.)  That shows each string needs AT MOST the
 * nodes found.  That it needs no fewer rests on the search having been
 * complete, for which this experiment leaves no record that another program
 * could check: agreement with a certified Bomb is evidence that it is, and
 * is not a certificate.
 *
 * THE LADDER'S OWN SEARCHES, OUT OF THIS ONE.  The steps this search makes
 * while the bits emitted are a beginning of S are exactly the steps of the
 * LevelSolver's search for S alone.  So by counting its steps by what had
 * been emitted (--count), this search knows, for every string and budget,
 * the number of logical steps the ladder's search of that string at that
 * budget takes: the number the ladder writes in its journal.  --journal
 * takes the work directory of a ladder level and compares every search in
 * its journal with this one's: the result, and the number of steps, which
 * have to be equal to the last digit.  --expect-totals does the same with no
 * more than the two totals the report of a finished level prints.  --bound
 * says where each string begins: at what its first X-1 bits need (prefix:
 * where the ladder begins it, and what --journal takes unless told
 * otherwise), at that or one less than its last X-1 bits need (suffix), or
 * at the most any stretch gives (all: the default here).  The table is the
 * same under all three; the searches made are not.
 *
 * A run of all classes prints the node-count digest: the SHA-256 of 2^X
 * bytes, byte i the number of nodes found for the string with index i (the
 * string's bits as a binary number, first bit most significant), for i from
 * 0 to 2^X - 1.  nodes_digest prints the same for a Bomb.  --expect takes
 * the histogram a certified Bomb has and fails on any difference.
 *
 * --out, with --base, writes the fixed-width table the ladder would write
 * for the level, through the library's writer (which runs every REV once
 * more), and prints its checksum.  THE FILE SAYS OF EVERY ENTRY THAT IT IS A
 * CERTIFIED MINIMUM, because that is the only way its checksum can be
 * compared with a certified table's; the experiment certifies nothing.  It
 * is for comparison and stays out of any place a Bomb is taken from.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"
#include "fixed_run_build.h"
#include "record_check.h"
#include "sha256.h"

#define UND 255
enum { MAXN = 12, MAXX = 31, STACK = 8 * MAXX + 64, MAXB = 16, SETTLED = 0x80 };
typedef struct { uint8_t color, link[2]; } Node;
typedef enum { CTRL, FLOW } Func;
typedef enum { INIT, LOOP_C, LOOP_C_R, LOOP_L, LOOP_L_R, TRY_N, TRY_N_R, MATCH, SIMPLE_R, LOOP_B, LOOP_B_R } State;
typedef struct { uint8_t fn, st, fp, cp, it, dir, nxt, set; } Frame;

/* ---- what all processes share ------------------------------------------ */

typedef struct {
    atomic_uint_fast64_t next;        /* the next job to take */
    atomic_uint_fast64_t steps[MAXB]; /* by node budget */
    atomic_uint_fast64_t found[MAXB];
    atomic_uint_fast64_t wanted[MAXB];
    atomic_uint_fast64_t leaves, jobs_done, settled, strings;
    atomic_uint_fast64_t depth[MAXX + 2];
    atomic_uint_fast64_t job_steps_max;
    atomic_uint_fast64_t as_one[MAXB][2];  /* [budget][found or not]: steps, counted as searches of one string each */
    atomic_uint_fast64_t jr_same, jr_other_steps, jr_other_result, jr_absent; /* against a journal */
    atomic_uint_fast64_t check;                                                /* record_check of every search */
    atomic_uint_fast64_t below[2][2]; /* [last bits, every stretch][searches, steps]: below what the base allows */
} Shared;

typedef struct { uint64_t wanted, found, steps; } JobPass; /* one class at one budget */

static Shared *shared;
static uint8_t *found_nodes; /* per string of X bits that starts with 0: nodes of its REV, 0 = none */
static uint8_t *found_graph; /* 3 bytes a node, MAXN nodes; not with --no-verify */
static uint8_t *need;        /* per string: 0 = not this job's business; else the first budget it is wanted at */
static JobPass *passes;      /* [job * MAXB + budget] */
static uint8_t *floor_of[2]; /* per string, when counting on a base: the bound from its last bits, and from every stretch */

/* The searches of a ladder's journal, by string: what the ladder's solver said and how many steps it took. */
typedef struct { uint64_t steps; uint8_t budget, result; } Searched;
static uint32_t *jr_first; /* [string]: its first record; [string + 1]: one past its last */
static Searched *jr_rec;

static int X, P = 1, want_depths, max_budget = MAXN;

/* ---- one process -------------------------------------------------------- */

static Node G[MAXN];
static Frame S[STACK];
static int Gn, Sp, gen;
static uint32_t prefix;
static uint64_t steps, leaves, depth_states[MAXX + 2];
static uint8_t *wanted[MAXX + 1]; /* [d]: a bit for every string of d bits */

/*
 * Steps by what had been emitted when they were made.  The steps made while
 * the emitted bits were a beginning of S are the steps of the reference
 * search for S alone (it has no choice of bit: the three steps of each such
 * choice are left out).  So the sum along the beginnings of S, when S is
 * found or when the pass ends, is the number of logical steps the
 * LevelSolver takes for S at this budget: the number a ladder's journal
 * records for that search.
 */
static int counting, pass_budget;
static uint64_t *count_low, count_top[MAXX + 1], *at; /* a job's part of the tree, and the beginnings above it */
static uint64_t one_steps[2], one_same, one_other_steps, one_other_result, one_absent, one_check, one_below[2][2];

static inline uint64_t *count_of(int d, uint32_t v) { /* the beginning of d bits with value v */
    if (d < P) return &count_top[d];
    return &count_low[((uint32_t)1 << (d - P)) | (v & (((uint32_t)1 << (d - P)) - 1u))];
}

static uint64_t steps_as_one(uint32_t v) { /* v: a string of X bits */
    uint64_t sum = 0;
    for (int d = 0; d < X; d++) sum += *count_of(d, v >> (X - d));
    return sum;
}

static void against_journal(uint32_t v, int result, uint64_t as_one) {
    int seen = 0;
    one_steps[result] += as_one;
    one_check += record_check(v, pass_budget, result, as_one);
    if (floor_of[0])
        for (int r = 0; r < 2; r++)
            if (pass_budget < floor_of[r][v]) one_below[r][0]++, one_below[r][1] += as_one;
    if (!jr_first) return;
    for (uint32_t i = jr_first[v]; i < jr_first[v + 1]; i++) {
        if (jr_rec[i].budget != pass_budget) continue;
        seen = 1;
        if (jr_rec[i].result != result) one_other_result++;
        else if (jr_rec[i].steps != as_one) {
            if (one_other_steps++ < 3)
                fprintf(stderr, "string %u at %d nodes: the journal has %llu steps, counted here %llu\n", v, pass_budget,
                        (unsigned long long)jr_rec[i].steps, (unsigned long long)as_one);
        } else one_same++;
    }
    if (!seen) one_absent++;
}

static inline int is_wanted(int d, uint32_t v) { return (wanted[d][v >> 3] >> (v & 7)) & 1; }

static void unwant(uint32_t v) {
    for (int d = X; d >= 0; d--) {
        wanted[d][v >> 3] &= (uint8_t)~(1u << (v & 7));
        if (d == 0 || is_wanted(d, v ^ 1u)) break; /* the other half still wants what is above */
        v >>= 1;
    }
}

static void leaf(void) {
    uint32_t v = prefix;
    leaves++;
    found_nodes[v] = (uint8_t)Gn;
    need[v] = 0;
    if (counting) against_journal(v, 0, steps_as_one(v));
    if (found_graph)
        for (int i = 0; i < Gn; i++) {
            uint8_t *g = found_graph + (size_t)v * 3 * MAXN + 3 * i;
            g[0] = G[i].color == UND ? 0 : G[i].color; /* what the search never decided: as the LevelSolver exports it */
            g[1] = G[i].link[0] == UND ? 0 : G[i].link[0];
            g[2] = G[i].link[1] == UND ? 0 : G[i].link[1];
        }
    unwant(v);
}

#define PUSH(fn_, fp_, cp_) \
    do { S[Sp++] = (Frame){(uint8_t)(fn_), INIT, (uint8_t)(fp_), (uint8_t)(cp_), 0, 0, 0, 0}; } while (0)

#define MOVED() \
    do { if (counting) at = count_of(gen, prefix); } while (0)

static void search(int max) {
    G[0] = (Node){UND, {UND, UND}};
    Gn = 1; gen = 0; Sp = 0; prefix = 0;
    if (!is_wanted(0, 0)) return;
    PUSH(CTRL, 0, 0);
    at = &count_top[0];
    while (Sp > 0) {
        Frame *f = &S[Sp - 1];
        int fp = f->fp, cp = f->cp;
        steps++;
        ++*at; /* stays where it is when not counting */
        if (f->fn == CTRL) {
            switch (f->st) {
            case INIT: {
                int d = G[fp].color;
                if (want_depths) depth_states[gen]++;
                if (d == UND) { f->st = LOOP_C; f->it = 0; }
                else {
                    int nxt = G[cp].link[d];
                    if (nxt == UND) { f->dir = (uint8_t)d; f->st = LOOP_L; f->it = 0; }
                    else {
                        int cc = G[nxt].color;
                        f->nxt = (uint8_t)nxt;
                        if (cc == UND) { f->st = LOOP_B; f->it = 0; break; } /* either bit */
                        if (!is_wanted(gen + 1, (prefix << 1) | (uint32_t)cc)) { Sp--; break; }
                        prefix = (prefix << 1) | (uint32_t)cc;
                        if (++gen == X) { leaf(); gen--; prefix >>= 1; Sp--; break; }
                        MOVED();
                        f->st = MATCH; PUSH(FLOW, fp, nxt);
                    }
                }
                break;
            }
            case LOOP_B: /* the node the control pointer arrives at has no color: the bit emitted decides it */
                --*at; /* a step the search for one string does not have */
                if (f->it < 2) {
                    int bit = f->it++;
                    if (!is_wanted(gen + 1, (prefix << 1) | (uint32_t)bit)) break;
                    G[f->nxt].color = (uint8_t)bit;
                    prefix = (prefix << 1) | (uint32_t)bit;
                    if (++gen == X) { leaf(); gen--; prefix >>= 1; G[f->nxt].color = UND; break; }
                    MOVED();
                    f->st = LOOP_B_R; PUSH(FLOW, fp, f->nxt);
                } else Sp--;
                break;
            case LOOP_B_R: G[f->nxt].color = UND; gen--; prefix >>= 1; MOVED(); f->st = LOOP_B; break;
            case LOOP_C:
                if (f->it < 2) { G[fp].color = f->it++; f->st = LOOP_C_R; PUSH(CTRL, fp, cp); }
                else Sp--;
                break;
            case LOOP_C_R: G[fp].color = UND; f->st = LOOP_C; break;
            case LOOP_L:
                if (f->it < Gn) { G[cp].link[f->dir] = f->it++; f->st = LOOP_L_R; PUSH(CTRL, fp, cp); }
                else { G[cp].link[f->dir] = UND; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[cp].link[f->dir] = UND; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[cp].link[f->dir] = (uint8_t)Gn;
                    G[Gn++] = (Node){UND, {UND, UND}};
                    f->st = TRY_N_R; PUSH(CTRL, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[cp].link[f->dir] = UND; Sp--; break;
            case MATCH: gen--; prefix >>= 1; MOVED(); Sp--; break;
            default: break;
            }
        } else {
            switch (f->st) {
            case INIT: {
                int d = G[cp].color, nxt = G[fp].link[d];
                if (nxt == UND) { f->dir = (uint8_t)d; f->st = LOOP_L; f->it = 0; }
                else { f->st = SIMPLE_R; PUSH(CTRL, nxt, cp); }
                break;
            }
            case LOOP_L:
                if (f->it < Gn) { G[fp].link[f->dir] = f->it++; f->st = LOOP_L_R; PUSH(FLOW, fp, cp); }
                else { G[fp].link[f->dir] = UND; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[fp].link[f->dir] = UND; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[fp].link[f->dir] = (uint8_t)Gn;
                    G[Gn++] = (Node){UND, {UND, UND}};
                    f->st = TRY_N_R; PUSH(FLOW, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[fp].link[f->dir] = UND; Sp--; break;
            case SIMPLE_R: Sp--; break;
            default: break;
            }
        }
    }
}

/* The strings of class k that are wanted at budget n, and everything above them. */
static uint64_t set_wanted(uint32_t k, int n) {
    uint32_t first = k << (X - P), count = (uint32_t)1 << (X - P);
    uint64_t wants = 0;
    for (int d = X; d >= 0; d--) { /* clear the part of each level this class can reach */
        uint32_t lo = d >= P ? (k << (d - P)) : (k >> (P - d));
        uint32_t span = d >= P ? ((uint32_t)1 << (d - P)) : 1u;
        for (uint32_t v = lo; v < lo + span; v++) wanted[d][v >> 3] &= (uint8_t)~(1u << (v & 7));
    }
    for (uint32_t v = first; v < first + count; v++) {
        if (!need[v] || need[v] > n) continue;
        wants++;
        for (int d = X; d >= 0; d--) {
            uint32_t a = v >> (X - d);
            if (is_wanted(d, a)) break;
            wanted[d][a >> 3] |= (uint8_t)(1u << (a & 7));
        }
    }
    return wants;
}

static void run_class(uint64_t job, uint32_t k) {
    uint32_t first = k << (X - P), count = (uint32_t)1 << (X - P);
    uint64_t job_steps = 0, open = 0;
    int low = MAXB;
    for (uint32_t v = first; v < first + count; v++)
        if (need[v]) {
            open++;
            if (need[v] < low) low = need[v];
        }
    for (int n = low; n <= max_budget && n < MAXB && open; n++) {
        uint64_t wants = set_wanted(k, n), l0 = leaves;
        steps = 0;
        pass_budget = n;
        if (counting) {
            memset(count_low, 0, ((size_t)2 << (X - P)) * sizeof *count_low);
            memset(count_top, 0, sizeof count_top);
            one_steps[0] = one_steps[1] = 0;
        }
        if (wants) search(n);
        if (counting) { /* what is still wanted has no REV of n nodes: what its own search would have cost */
            for (uint32_t v = first; v < first + count; v++)
                if (is_wanted(X, v)) against_journal(v, 1, steps_as_one(v));
            atomic_fetch_add(&shared->as_one[n][0], one_steps[0]);
            atomic_fetch_add(&shared->as_one[n][1], one_steps[1]);
        }
        open -= leaves - l0;
        job_steps += steps;
        atomic_fetch_add(&shared->steps[n], steps);
        atomic_fetch_add(&shared->found[n], leaves - l0);
        atomic_fetch_add(&shared->wanted[n], wants);
        passes[job * MAXB + (uint64_t)n] = (JobPass){wants, leaves - l0, steps}; /* no other process has this job */
    }
    atomic_fetch_add(&shared->jobs_done, 1);
    {
        uint_fast64_t was = atomic_load(&shared->job_steps_max);
        while (job_steps > was && !atomic_compare_exchange_weak(&shared->job_steps_max, &was, job_steps)) {
        }
    }
}

static void worker(const uint32_t *classes, uint64_t count) {
    for (int d = 0; d <= X; d++) {
        wanted[d] = calloc(((size_t)1 << d) / 8 + 1, 1);
        if (!wanted[d]) _exit(3);
    }
    at = &count_top[0];
    if (counting) {
        count_low = malloc(((size_t)2 << (X - P)) * sizeof *count_low);
        if (!count_low) _exit(3);
    }
    for (;;) {
        uint64_t i = atomic_fetch_add(&shared->next, 1);
        if (i >= count) break;
        run_class(i, classes[i]);
    }
    atomic_fetch_add(&shared->leaves, leaves);
    atomic_fetch_add(&shared->jr_same, one_same);
    atomic_fetch_add(&shared->jr_other_steps, one_other_steps);
    atomic_fetch_add(&shared->jr_other_result, one_other_result);
    atomic_fetch_add(&shared->jr_absent, one_absent);
    atomic_fetch_add(&shared->check, one_check);
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 2; k++) atomic_fetch_add(&shared->below[r][k], one_below[r][k]);
    free(count_low);
    for (int d = 0; d <= X; d++) atomic_fetch_add(&shared->depth[d], depth_states[d]);
    for (int d = 0; d <= X; d++) free(wanted[d]);
}

/* ---- the driver --------------------------------------------------------- */

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static void *share(size_t bytes) {
    void *p = mmap(NULL, bytes ? bytes : 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "shared_search: no memory for %zu bytes\n", bytes);
        exit(1);
    }
    return p;
}

static uint64_t rng_state;
static uint64_t rnd(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
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

/* ---- a ladder's journal ---------------------------------------------------- */

typedef struct {
    uint32_t half, *cursor;
    uint64_t searches, outside, steps, check;
    int filling;
} JournalRead;

static int journal_visit(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    JournalRead *j = ctx;
    (void)seg;
    if (!rec || rec->type != FRB_REC_SEARCH) return 0;
    if (rec->target >= j->half) { /* a string that starts with 1: the ladder leaves those to the symmetry */
        j->outside += (uint64_t)!j->filling;
        return 0;
    }
    if (!j->filling) {
        jr_first[rec->target + 1]++;
        j->searches++;
        j->steps += rec->steps;
        j->check += record_check(rec->target, rec->budget, rec->result, rec->steps);
    } else jr_rec[j->cursor[rec->target]++] = (Searched){rec->steps, rec->budget, rec->result};
    return 0;
}

/* Every search of the journal in `work`, by string.  Returns 0, or 1 with a message printed. */
static int journal_read(const char *work, const FrbConfig *cfg, uint32_t half, JournalRead *j) {
    FrbJournalInfo info;
    char err[256] = "";
    FrbError e;
    memset(j, 0, sizeof *j);
    j->half = half;
    jr_first = calloc((size_t)half + 1, sizeof *jr_first);
    j->cursor = malloc((size_t)half * sizeof *j->cursor);
    if (!jr_first || !j->cursor) return fprintf(stderr, "shared_search: no memory for the journal\n"), 1;
    e = frb_journal_scan(work, cfg, journal_visit, j, &info, err, sizeof err);
    if (e == FRB_OK) {
        for (uint32_t v = 0; v < half; v++) jr_first[v + 1] += jr_first[v];
        memcpy(j->cursor, jr_first, (size_t)half * sizeof *j->cursor);
        jr_rec = malloc((size_t)(j->searches ? j->searches : 1) * sizeof *jr_rec);
        if (!jr_rec) return fprintf(stderr, "shared_search: no memory for the journal\n"), 1;
        j->filling = 1;
        e = frb_journal_scan(work, cfg, journal_visit, j, &info, err, sizeof err);
    }
    free(j->cursor);
    if (e != FRB_OK) return fprintf(stderr, "shared_search: %s: %s\n", work, err[0] ? err : frb_error_str(e)), 1;
    return 0;
}

/* ---- what was recorded, read back ---------------------------------------- */

static void graph_put(uint32_t v, const RevGraph *g) {
    uint8_t *f = found_graph + (size_t)v * 3 * MAXN;
    for (int i = 0; i < g->node_count; i++) {
        f[3 * i] = (uint8_t)g->nodes[i].color;
        f[3 * i + 1] = (uint8_t)g->nodes[i].links[0];
        f[3 * i + 2] = (uint8_t)g->nodes[i].links[1];
    }
}

static void graph_get(uint32_t v, RevGraph *g) {
    const uint8_t *f = found_graph + (size_t)v * 3 * MAXN;
    g->node_count = found_nodes[v] & 0x7F;
    for (int i = 0; i < g->node_count; i++) {
        g->nodes[i].color = f[3 * i];
        g->nodes[i].links[0] = f[3 * i + 1];
        g->nodes[i].links[1] = f[3 * i + 2];
    }
}

typedef struct { uint64_t checked, good, not_a_rev, other_string, complement, idle_node, by_base; } Verified;

/*
 * The REV recorded for string v, on the reference interpreter.  Nothing of
 * the search is used: the bytes that were recorded, and core/rev.
 */
static void verify(uint32_t v, Verified *out) {
    RevGraph g, c;
    RevMachine m;
    uint32_t emitted = 0, visited = 1u, mask = (X == 32 ? 0u : ((uint32_t)1 << X)) - 1u;
    graph_get(v, &g);
    out->checked++;
    if (rev_graph_validate(&g) != REV_OK) { out->not_a_rev++; return; }
    rev_machine_reset(&m);
    for (int i = 0; i < X; i++) {
        emitted = (emitted << 1) | (uint32_t)rev_machine_next_bit(&g, &m);
        visited |= (1u << m.fp) | (1u << m.cp);
    }
    if (emitted != v) { out->other_string++; return; }
    if (visited != ((uint32_t)1 << g.node_count) - 1u) { out->idle_node++; return; } /* then fewer nodes would do */
    frb_graph_complement(&g, &c);
    if (frb_graph_output(&c, X) != (v ^ mask)) { out->complement++; return; }
    out->good++;
    out->by_base += (uint64_t)((found_nodes[v] & SETTLED) != 0);
}

/* "1:2,2:6,3:64": strings by nodes, both strings of a pair counted.  1 if well formed. */
static int parse_expect(const char *text, uint64_t expect[MAXB]) {
    const char *p = text;
    while (*p) {
        char *end;
        unsigned long n = strtoul(p, &end, 10);
        unsigned long long count;
        if (end == p || *end != ':' || n < 1 || n >= MAXB) return 0;
        p = end + 1;
        count = strtoull(p, &end, 10);
        if (end == p) return 0;
        expect[n] = count;
        p = *end == ',' ? end + 1 : end;
        if (*end && *end != ',') return 0;
    }
    return 1;
}

static int by_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv) {
    const char *check = NULL, *base_path = NULL, *out_path = NULL, *jobs_out = NULL, *expect_text = NULL;
    const char *expect_crc = NULL, *journal = NULL, *bound_text = NULL, *expect_totals = NULL;
    JournalRead jr;
    long one_class = -1, sample = 0;
    int jobs = 1, keep = 1, failed = 0, whole, bound = 2; /* 0: first bits; 1: and the last X-1; 2: every stretch */
    uint32_t half, classes_all, *classes;
    uint64_t class_count, total_steps = 0, total_found = 0, in_jobs = 0, unresolved = 0;
    uint64_t hist[MAXB] = {0}, expect[MAXB] = {0};
    double t0 = now(), t_search, t_done;

    rng_state = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bits") && i + 1 < argc) X = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prefix-bits") && i + 1 < argc) P = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--class") && i + 1 < argc) one_class = atol(argv[++i]);
        else if (!strcmp(argv[i], "--sample") && i + 1 < argc) sample = atol(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) rng_state = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--jobs") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--base") && i + 1 < argc) base_path = argv[++i];
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else if (!strcmp(argv[i], "--max-budget") && i + 1 < argc) max_budget = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--expect") && i + 1 < argc) expect_text = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "--expect-table-crc") && i + 1 < argc) expect_crc = argv[++i];
        else if (!strcmp(argv[i], "--jobs-out") && i + 1 < argc) jobs_out = argv[++i];
        else if (!strcmp(argv[i], "--journal") && i + 1 < argc) journal = argv[++i];
        else if (!strcmp(argv[i], "--bound") && i + 1 < argc) bound_text = argv[++i];
        else if (!strcmp(argv[i], "--count")) counting = 1;
        else if (!strcmp(argv[i], "--expect-totals") && i + 1 < argc) expect_totals = argv[++i], counting = 1;
        else if (!strcmp(argv[i], "--no-verify")) keep = 0;
        else if (!strcmp(argv[i], "--depths")) want_depths = 1;
        else X = 0, i = argc;
    }
    if (X < 2 || X > MAXX || P < 1 || P >= X || jobs < 1 || jobs > 256 || max_budget < 1 || max_budget > MAXN ||
        (expect_text && !parse_expect(expect_text, expect)) || (out_path && (!base_path || !keep)) ||
        (expect_crc && !out_path) || (check && !keep) || (journal && !base_path) || (bound_text && !base_path)) {
        fprintf(stderr, "usage: shared_search --bits X [--prefix-bits P] [--class K | --sample N [--seed S]] [--jobs J]\n"
                        "                     [--base BOMB [--bound prefix|suffix|all] [--journal WORK]] [--count]\n"
                        "                     [--expect-totals SEARCHES:STEPS]\n"
                        "                     [--check BOMB] [--max-budget N] [--expect 1:2,2:6,...]\n"
                        "                     [--out TABLE [--expect-table-crc HEX]] [--jobs-out FILE] [--no-verify] [--depths]\n"
                        "       --out, --bound and --journal need --base; --out and --check need the REVs, so not --no-verify\n");
        return 2;
    }
    if (bound_text) {
        bound = !strcmp(bound_text, "prefix") ? 0 : !strcmp(bound_text, "suffix") ? 1 : !strcmp(bound_text, "all") ? 2 : -1;
        if (bound < 0) return fprintf(stderr, "shared_search: --bound takes prefix, suffix or all\n"), 2;
    }
    if (!sha256_selftest()) return fprintf(stderr, "shared_search: SHA-256 fails the examples of its standard\n"), 1;
    half = (uint32_t)1 << (X - 1);                 /* strings that start with 0 */
    classes_all = (uint32_t)1 << (P - 1);          /* classes that do */
    shared = share(sizeof *shared);
    found_nodes = share(half);
    need = share(half);
    if (keep) found_graph = share((size_t)half * 3 * MAXN);

    /* Which classes. */
    if (one_class >= 0) {
        if ((uint64_t)one_class >= classes_all) return fprintf(stderr, "shared_search: classes are 0..%u\n", classes_all - 1), 2;
        classes = malloc(sizeof *classes);
        classes[0] = (uint32_t)one_class;
        class_count = 1;
    } else {
        classes = malloc(classes_all * sizeof *classes);
        for (uint32_t k = 0; k < classes_all; k++) classes[k] = k;
        class_count = classes_all;
        if (sample > 0 && (uint64_t)sample < classes_all) { /* the first `sample` of a shuffle */
            for (uint32_t k = 0; k < (uint32_t)sample; k++) {
                uint32_t j = k + (uint32_t)(rnd() % (classes_all - k)), t = classes[k];
                classes[k] = classes[j];
                classes[j] = t;
            }
            class_count = (uint64_t)sample;
        }
    }
    whole = class_count == classes_all;
    if (out_path && !whole) return fprintf(stderr, "shared_search: --out takes a run of every class\n"), 2;
    passes = share((size_t)class_count * MAXB * sizeof *passes);

    /* The journal of a ladder for this level: its searches are to come out of this one, step for step. */
    if (journal) {
        FrbConfig cfg;
        char err[256] = "";
        FrbError e = frb_config_read(journal, &cfg, err, sizeof err);
        if (e != FRB_OK) return fprintf(stderr, "shared_search: %s: %s\n", journal, err[0] ? err : frb_error_str(e)), 1;
        if (cfg.bits != X || cfg.base_bits != X - 1 || !cfg.symmetry)
            return fprintf(stderr, "shared_search: --journal takes the work directory of a level of %d bits built on the Bomb\n"
                                   "of %d bits with the symmetry\n", X, X - 1), 1;
        if (!bound_text) bound = 0; /* the searches a ladder makes are those from the bound it uses: its first bits */
        if (journal_read(journal, &cfg, half, &jr)) return 1;
        counting = 1;
    }

    /* What each string needs to begin with. */
    if (base_path && counting) floor_of[0] = share(half), floor_of[1] = share(half);
    if (base_path) {
        RevFixedBomb *base = NULL;
        uint64_t settled = 0;
        if (rev_fixed_bomb_open(base_path, &base, NULL) != FRB_OK || rev_fixed_bomb_bits(base) != X - 1)
            return fprintf(stderr, "shared_search: --base takes a complete Bomb of %d bits\n", X - 1), 1;
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            for (uint32_t p = first >> 1; p < (first + count) >> 1; p++) { /* each beginning of X-1 bits */
                char text[FRB_MAX_BITS + 1];
                RevFixedBombInfo info;
                RevGraph g;
                uint32_t emitted, other;
                int m, lb;
                frb_target_format(p, X - 1, text);
                if (rev_fixed_bomb_solve(base, text, (size_t)(X - 1), 0, &g, &info) != REV_FIXED_BOMB_MINIMAL) return 1;
                m = g.node_count;
                if (m > MAXN) return fprintf(stderr, "shared_search: a REV of %d nodes; this program holds %d\n", m, MAXN), 1;
                emitted = frb_graph_output(&g, X);
                other = emitted ^ 1u;
                found_nodes[emitted] = (uint8_t)(m | SETTLED); /* settled by the REV of the base */
                if (keep) graph_put(emitted, &g);
                settled++;
                frb_target_format(other, X, text);
                if (bound == 0) lb = m; /* what its first bits need */
                else if (bound == 1) lb = rev_fixed_bomb_min_nodes(base, X - 1, other & (half - 1u)) - 1; /* its last, less one */
                else lb = rev_fixed_bomb_lower_bound(base, text, (size_t)X); /* every stretch, less one */
                need[other] = (uint8_t)(lb > m ? lb : m);
                if (floor_of[0]) { /* what each rule would spare a ladder */
                    floor_of[0][other] = (uint8_t)(rev_fixed_bomb_min_nodes(base, X - 1, other & (half - 1u)) - 1);
                    floor_of[1][other] = (uint8_t)rev_fixed_bomb_lower_bound(base, text, (size_t)X);
                }
            }
        }
        atomic_store(&shared->settled, settled);
        rev_fixed_bomb_close(base);
    } else {
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            memset(need + first, 1, count);
        }
    }
    for (uint64_t c = 0; c < class_count; c++) in_jobs += (uint64_t)1 << (X - P);

    printf("Strings of %d bits that start with 0: %s in %s class%s of %s%s; %d process%s\n", X, commas(in_jobs),
           commas(class_count), class_count == 1 ? "" : "es", commas(classes_all),
           !base_path ? "; from nothing"
           : bound == 0 ? "; built on the Bomb one bit shorter, each string from what its first bits need"
           : bound == 1 ? "; built on the Bomb one bit shorter, each string from what its first and its last bits allow"
                        : "; built on the Bomb one bit shorter, each string from what every stretch of it allows",
           jobs, jobs == 1 ? "" : "es");
    fflush(stdout);

    t_search = now();
    for (int j = 1; j < jobs; j++) {
        pid_t pid = fork();
        if (pid < 0) return perror("fork"), 1;
        if (pid == 0) {
            worker(classes, class_count);
            _exit(0);
        }
    }
    worker(classes, class_count);
    for (int j = 1; j < jobs; j++) {
        int status = 0;
        if (wait(&status) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return fprintf(stderr, "a worker failed\n"), 1;
    }
    t_done = now();

    printf("  %-6s %16s %16s %22s\n", "budget", "wanted", "found", "steps");
    for (int n = 1; n < MAXB; n++) {
        uint64_t s = atomic_load(&shared->steps[n]), f = atomic_load(&shared->found[n]), w = atomic_load(&shared->wanted[n]);
        if (!w && !s) continue;
        printf("  %-6d %16s %16s %22s\n", n, commas(w), commas(f), commas(s));
        total_steps += s;
        total_found += f;
    }
    for (uint64_t c = 0; c < class_count; c++) {
        uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
        for (uint32_t v = first; v < first + count; v++) {
            hist[found_nodes[v] & 0x7F]++;
            unresolved += found_nodes[v] == 0;
        }
    }
    {
        uint64_t settled = atomic_load(&shared->settled);
        struct rusage self, children;
        getrusage(RUSAGE_SELF, &self);
        getrusage(RUSAGE_CHILDREN, &children);
        printf("  total: %s steps; %s strings found by search", commas(total_steps), commas(total_found));
        if (base_path) printf(", %s settled by the REV of the base", commas(settled));
        printf("\n  unresolved (no REV within %d nodes): %s\n", max_budget, commas(unresolved));
        printf("  wall clock: %.1f s to set up, %.1f s searching (%.3g steps a second, all processes)\n", t_search - t0,
               t_done - t_search, (double)total_steps / (t_done - t_search > 0 ? t_done - t_search : 1));
        printf("  memory: the largest process had %ld MB resident; the tables all processes share are %s MB\n",
               (self.ru_maxrss > children.ru_maxrss ? self.ru_maxrss : children.ru_maxrss) / 1024,
               commas(((uint64_t)half * (keep ? 2u + 3u * MAXN : 2u)) >> 20));
        if (unresolved) failed = 1;
    }
    {
        uint64_t *per = malloc((size_t)class_count * sizeof *per), most;
        for (uint64_t c = 0; c < class_count; c++) {
            per[c] = 0;
            for (int n = 0; n < MAXB; n++) per[c] += passes[c * MAXB + (uint64_t)n].steps;
        }
        qsort(per, (size_t)class_count, sizeof *per, by_u64);
        most = per[class_count - 1];
        printf("  jobs: %s; steps of a job: least %s, median %s, most %s (%.2f%% of all)", commas(class_count), commas(per[0]),
               commas(per[class_count / 2]), commas(most), total_steps ? 100.0 * (double)most / (double)total_steps : 0.0);
        if (!whole)
            printf("; scaled to all %s classes: %.3e steps", commas(classes_all),
                   (double)total_steps * (double)classes_all / (double)class_count);
        printf("\n");
        free(per);
    }
    printf("  strings by nodes (both strings of a pair counted):");
    for (int n = 1; n < MAXB; n++)
        if (hist[n]) printf("  %d: %s", n, commas(2 * hist[n]));
    printf("\n");
    if (expect_text) {
        int same = 1;
        for (int n = 1; n < MAXB; n++)
            if (2 * hist[n] != expect[n]) {
                printf("  NOT AS EXPECTED: %d nodes: %s strings, expected %s\n", n, commas(2 * hist[n]), commas(expect[n]));
                same = 0;
            }
        if (same) printf("  the histogram expected: every entry agrees\n");
        else failed = 1;
    }
    if (counting) { /* the same work as a ladder does it: one search for each string and budget */
        uint64_t searches = 0, as_one = 0;
        printf("  counted as searches of one string each, the searches a ladder makes and journals:\n");
        printf("  %-6s %16s %24s %24s\n", "budget", "searches", "steps where a REV", "steps where none");
        for (int n = 1; n < MAXB; n++) {
            uint64_t w = atomic_load(&shared->wanted[n]), a = atomic_load(&shared->as_one[n][0]), b = atomic_load(&shared->as_one[n][1]);
            if (!w) continue;
            printf("  %-6d %16s %24s %24s\n", n, commas(w), commas(a), commas(b));
            searches += w;
            as_one += a + b;
        }
        printf("  total: %s searches, %s logical steps: %.1f times the steps made here\n", commas(searches), commas(as_one),
               total_steps ? (double)as_one / (double)total_steps : 0.0);
        printf("  record checksum (every search: string, budget, result, steps): %016llx\n",
               (unsigned long long)atomic_load(&shared->check));
        if (floor_of[0] && bound == 0) /* the searches of a ladder without the rules: which of them a rule excludes */
            for (int r = 0; r < 2; r++) {
                uint64_t n = atomic_load(&shared->below[r][0]), st = atomic_load(&shared->below[r][1]);
                printf("  of them, at a budget %s excludes: %s searches, %s steps (%.3f%%)\n",
                       r ? "every stretch of the string, less one node," : "the last bits of the string, less one node,",
                       commas(n), commas(st), as_one ? 100.0 * (double)st / (double)as_one : 0.0);
            }
        if (expect_totals) { /* SEARCHES:STEPS, as the report of a finished level gives them */
            char *end;
            unsigned long long want_searches = strtoull(expect_totals, &end, 10), want_steps = *end == ':' ? strtoull(end + 1, NULL, 10) : 0;
            if (searches == want_searches && as_one == want_steps)
                printf("  the totals expected (%s searches, %s steps): both agree to the last digit\n", commas(want_searches),
                       commas(want_steps));
            else {
                printf("  NOT the totals expected: %s searches, %s steps\n", commas(want_searches), commas(want_steps));
                failed = 1;
            }
        }
        if (journal) {
            uint64_t same = atomic_load(&shared->jr_same), other_steps = atomic_load(&shared->jr_other_steps);
            uint64_t other_result = atomic_load(&shared->jr_other_result), absent = atomic_load(&shared->jr_absent);
            uint64_t met = same + other_steps + other_result;
            printf("  the journal: %s searches, %s logical steps, record checksum %016llx", commas(jr.searches), commas(jr.steps),
                   (unsigned long long)jr.check);
            if (jr.outside) printf(" (and %s of strings that start with 1, not compared)", commas(jr.outside));
            printf("\n  of them, with the same result and the same number of steps here: %s; with other steps: %s; with another\n"
                   "  result: %s; not made here: %s; made here and not in the journal: %s\n",
                   commas(same), commas(other_steps), commas(other_result), commas(whole ? jr.searches - met : 0), commas(absent));
            if (!whole) printf("  (a part of the classes: the searches of the others are in the journal and were not made here)\n");
            if (other_steps || other_result || absent || (whole && met != jr.searches)) failed = 1;
            else if (whole) printf("  every search of the journal: the same result, the same number of logical steps\n");
        }
    }
    if (want_depths) {
        printf("  states by bits emitted:");
        for (int d = 0; d <= X; d++) printf(" %llu", (unsigned long long)atomic_load(&shared->depth[d]));
        printf("\n");
    }

    if (jobs_out) { /* one line for each pass of each job */
        FILE *f = fopen(jobs_out, "w");
        if (!f) return fprintf(stderr, "shared_search: cannot write %s\n", jobs_out), 1;
        fprintf(f, "# bits %d prefix_bits %d: class budget wanted found steps\n", X, P);
        for (uint64_t c = 0; c < class_count; c++)
            for (int n = 1; n < MAXB; n++) {
                JobPass *q = &passes[c * MAXB + (uint64_t)n];
                if (q->wanted || q->steps)
                    fprintf(f, "%u %d %llu %llu %llu\n", classes[c], n, (unsigned long long)q->wanted,
                            (unsigned long long)q->found, (unsigned long long)q->steps);
            }
        if (fclose(f) != 0) return fprintf(stderr, "shared_search: cannot write %s\n", jobs_out), 1;
    }

    /* Every REV recorded, on the reference interpreter. */
    if (keep) {
        Verified ver = {0};
        double t = now();
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            for (uint32_t v = first; v < first + count; v++)
                if (found_nodes[v]) verify(v, &ver);
        }
        printf("  REVs run on the reference interpreter: %s; emit their string with every node, and their complement the\n"
               "  complement: %s (%s from the search, %s from the base)%s\n",
               commas(ver.checked), commas(ver.good), commas(ver.good - ver.by_base), commas(ver.by_base),
               ver.good == ver.checked ? "" : "  FAILURES:");
        if (ver.good != ver.checked) {
            printf("    not a REV: %s; emits another string: %s; has a node it never visits: %s; complement fails: %s\n",
                   commas(ver.not_a_rev), commas(ver.other_string), commas(ver.idle_node), commas(ver.complement));
            failed = 1;
        }
        printf("  (%.1f s)\n", now() - t);
    } else printf("  REVs not kept: nothing was run on the reference interpreter\n");

    /* The node counts of all 2^X strings, in the order of their indices: the second half is the first, backwards. */
    if (whole && !unresolved) {
        uint8_t *seq = malloc(half);
        Sha256 sha;
        char hex[65];
        if (!seq) return fprintf(stderr, "shared_search: no memory\n"), 1;
        for (uint32_t v = 0; v < half; v++) seq[v] = found_nodes[v] & 0x7F;
        sha256_init(&sha);
        sha256_update(&sha, seq, half);
        for (uint32_t v = 0; v < half / 2; v++) {
            uint8_t t = seq[v];
            seq[v] = seq[half - 1 - v];
            seq[half - 1 - v] = t;
        }
        sha256_update(&sha, seq, half);
        sha256_hex(&sha, hex);
        printf("  node-count digest (SHA-256 of %s bytes, the nodes of each string in index order):\n    %s\n",
               commas((uint64_t)half * 2), hex);
        free(seq);
    }

    if (check) {
        FrbTable t;
        FrbFileInfo info;
        uint64_t same_nodes = 0, same_graph = 0, other_nodes = 0, compared = 0, small_other = 0;
        FrbError e = frb_file_read_raw(check, &t, &info);
        if (e != FRB_OK || info.bits != X) return fprintf(stderr, "shared_search: cannot check against %s\n", check), 1;
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            for (uint32_t v = first; v < first + count; v++) {
                FrbStatus st;
                RevGraph g;
                int same = 1;
                if (frb_table_get(&t, v, &st, &g) != FRB_OK) return 1;
                compared++;
                if (g.node_count != (found_nodes[v] & 0x7F)) {
                    other_nodes++;
                    continue;
                }
                same_nodes++;
                for (int i = 0; i < g.node_count && same; i++) {
                    const uint8_t *f = found_graph + (size_t)v * 3 * MAXN + 3 * i;
                    same = g.nodes[i].color == f[0] && g.nodes[i].links[0] == f[1] && g.nodes[i].links[1] == f[2];
                }
                same_graph += (uint64_t)same;
                small_other += (uint64_t)(!same && g.node_count <= 5);
            }
        }
        printf("  against the table: %s strings; the node count of the table for %s, another for %s;\n"
               "  the REV of the table, link for link and color for color, for %s (of the %s others, %s have 5 nodes or fewer)\n",
               commas(compared), commas(same_nodes), commas(other_nodes), commas(same_graph), commas(same_nodes - same_graph),
               commas(small_other));
        frb_table_destroy(&t);
        if (other_nodes) failed = 1;
    }

    /* The table of the level, as the ladder writes it: a string that starts with 1 has the complement of its
     * complement's REV. */
    if (out_path && !failed) {
        FrbWriter *writer = NULL;
        FrbSummary summary;
        uint32_t crc = 0, mask = ((uint32_t)1 << X) - 1u;
        FrbError e = frb_writer_open(out_path, X, frb_format_for_bits(X), &writer);
        for (uint64_t i = 0; e == FRB_OK && i < (uint64_t)half * 2; i++) {
            RevGraph g, c;
            if (i < half) graph_get((uint32_t)i, &g);
            else {
                graph_get((uint32_t)i ^ mask, &c);
                frb_graph_complement(&c, &g);
            }
            e = frb_writer_put(writer, FRB_MINIMAL, &g); /* runs it */
            if (e != FRB_OK) frb_writer_abort(writer);
        }
        if (e == FRB_OK) e = frb_writer_finish(writer, &summary, &crc);
        if (e != FRB_OK) return fprintf(stderr, "shared_search: cannot write %s: %s\n", out_path, frb_error_str(e)), 1;
        printf("  table written for comparison (it says \"certified\" of every entry; this experiment certifies nothing):\n"
               "    %s\n    table checksum (CRC-32) %08x", out_path, (unsigned)crc);
        if (expect_crc) {
            if (strtoul(expect_crc, NULL, 16) == crc) printf(": the checksum expected");
            else {
                printf(": NOT the checksum expected, %s", expect_crc);
                failed = 1;
            }
        }
        printf("\n");
    } else if (out_path) printf("  no table written: see above\n");
    free(classes);
    return failed;
}
