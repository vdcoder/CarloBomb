/*
 * shared_search: one search for many strings at once.  An experiment; see
 * README.md in this directory and benchmarks/DISCOVERY.md.
 *
 *   shared_search --bits X [--prefix-bits P] [--class K | --sample N [--seed S]]
 *                 [--jobs J] [--base BOMB] [--check BOMB] [--max-budget N]
 *                 [--depths]
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"

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
} Shared;

static Shared *shared;
static uint8_t *found_nodes; /* per string of X bits that starts with 0: nodes of its REV, 0 = none */
static uint8_t *found_graph; /* 3 bytes a node, MAXN nodes; only with --check */
static uint8_t *need;        /* per string: 0 = not this job's business; else the first budget it is wanted at */

static int X, P = 1, want_depths, max_budget = MAXN;

/* ---- one process -------------------------------------------------------- */

static Node G[MAXN];
static Frame S[STACK];
static int Gn, Sp, gen;
static uint32_t prefix;
static uint64_t steps, leaves, depth_states[MAXX + 2];
static uint8_t *wanted[MAXX + 1]; /* [d]: a bit for every string of d bits */

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

static void search(int max) {
    G[0] = (Node){UND, {UND, UND}};
    Gn = 1; gen = 0; Sp = 0; prefix = 0;
    if (!is_wanted(0, 0)) return;
    PUSH(CTRL, 0, 0);
    while (Sp > 0) {
        Frame *f = &S[Sp - 1];
        int fp = f->fp, cp = f->cp;
        steps++;
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
                        f->st = MATCH; PUSH(FLOW, fp, nxt);
                    }
                }
                break;
            }
            case LOOP_B: /* the node the control pointer arrives at has no color: the bit emitted decides it */
                if (f->it < 2) {
                    int bit = f->it++;
                    if (!is_wanted(gen + 1, (prefix << 1) | (uint32_t)bit)) break;
                    G[f->nxt].color = (uint8_t)bit;
                    prefix = (prefix << 1) | (uint32_t)bit;
                    if (++gen == X) { leaf(); gen--; prefix >>= 1; G[f->nxt].color = UND; break; }
                    f->st = LOOP_B_R; PUSH(FLOW, fp, f->nxt);
                } else Sp--;
                break;
            case LOOP_B_R: G[f->nxt].color = UND; gen--; prefix >>= 1; f->st = LOOP_B; break;
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
            case MATCH: gen--; prefix >>= 1; Sp--; break;
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

static void run_class(uint32_t k) {
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
        if (wants) search(n);
        open -= leaves - l0;
        job_steps += steps;
        atomic_fetch_add(&shared->steps[n], steps);
        atomic_fetch_add(&shared->found[n], leaves - l0);
        atomic_fetch_add(&shared->wanted[n], wants);
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
    for (;;) {
        uint64_t i = atomic_fetch_add(&shared->next, 1);
        if (i >= count) break;
        run_class(classes[i]);
    }
    atomic_fetch_add(&shared->leaves, leaves);
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

int main(int argc, char **argv) {
    const char *check = NULL, *base_path = NULL;
    long one_class = -1, sample = 0;
    int jobs = 1;
    uint32_t half, classes_all, *classes;
    uint64_t class_count, total_steps = 0, total_found = 0, in_jobs = 0;
    double t0 = now();

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
        else if (!strcmp(argv[i], "--depths")) want_depths = 1;
        else X = 0, i = argc;
    }
    if (X < 2 || X > MAXX || P < 1 || P >= X || jobs < 1 || jobs > 256 || max_budget < 1 || max_budget > MAXN) {
        fprintf(stderr, "usage: shared_search --bits X [--prefix-bits P] [--class K | --sample N [--seed S]] [--jobs J]\n"
                        "                     [--base BOMB] [--check BOMB] [--max-budget N] [--depths]\n");
        return 2;
    }
    half = (uint32_t)1 << (X - 1);                 /* strings that start with 0 */
    classes_all = (uint32_t)1 << (P - 1);          /* classes that do */
    shared = share(sizeof *shared);
    found_nodes = share(half);
    need = share(half);
    if (check) found_graph = share((size_t)half * 3 * MAXN);

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

    /* What each string needs to begin with. */
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
                emitted = frb_graph_output(&g, X);
                other = emitted ^ 1u;
                found_nodes[emitted] = (uint8_t)(m | SETTLED); /* settled by the REV of the base */
                settled++;
                frb_target_format(other, X, text);
                lb = rev_fixed_bomb_lower_bound(base, text, (size_t)X); /* its first bits, and every suffix less one */
                need[other] = (uint8_t)(lb > m ? lb : m);
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
           base_path ? "; built on the Bomb one bit shorter" : "; from nothing", jobs, jobs == 1 ? "" : "es");
    fflush(stdout);

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

    printf("  %-6s %16s %16s %22s\n", "budget", "wanted", "found", "steps");
    for (int n = 1; n < MAXB; n++) {
        uint64_t s = atomic_load(&shared->steps[n]), f = atomic_load(&shared->found[n]), w = atomic_load(&shared->wanted[n]);
        if (!w && !s) continue;
        printf("  %-6d %16s %16s %22s\n", n, commas(w), commas(f), commas(s));
        total_steps += s;
        total_found += f;
    }
    {
        uint64_t settled = atomic_load(&shared->settled), still = 0;
        double seconds = now() - t0;
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            for (uint32_t v = first; v < first + count; v++) still += found_nodes[v] == 0;
        }
        printf("  total: %s steps; %s strings found by search", commas(total_steps), commas(total_found));
        if (base_path) printf(", %s settled by the REV of the base", commas(settled));
        printf(", %s without a REV; %.1f s wall clock\n", commas(still), seconds);
        printf("  largest job: %s steps", commas(atomic_load(&shared->job_steps_max)));
        if (class_count < classes_all)
            printf("; scaled to all %s classes: %.3e steps", commas(classes_all),
                   (double)total_steps * (double)classes_all / (double)class_count);
        printf("\n");
    }
    {
        uint64_t hist[MAXB] = {0};
        for (uint64_t c = 0; c < class_count; c++) {
            uint32_t first = classes[c] << (X - P), count = (uint32_t)1 << (X - P);
            for (uint32_t v = first; v < first + count; v++) hist[found_nodes[v] & 0x7F]++;
        }
        printf("  strings by nodes (both strings of a pair counted):");
        for (int n = 1; n < MAXB; n++)
            if (hist[n]) printf("  %d: %s", n, commas(2 * hist[n]));
        printf("\n");
    }
    if (want_depths) {
        printf("  states by bits emitted:");
        for (int d = 0; d <= X; d++) printf(" %llu", (unsigned long long)atomic_load(&shared->depth[d]));
        printf("\n");
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
                if (found_nodes[v] & SETTLED) { /* the REV of the base, run on: the ladder stores that very REV */
                    same_graph++;
                    continue;
                }
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
        if (other_nodes) return 1;
    }
    free(classes);
    return 0;
}
