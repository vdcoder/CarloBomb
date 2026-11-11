/*
 * Differential test: AMD64 assembly LevelSolver against the C LevelSolver.
 *
 * For identical (target, budget, limits) both must return the same status,
 * the same graph (every field, after completion), the same step count, the
 * same peak stack depth and the same limit reason.  Every solution is also
 * checked with the independent interpreter.
 *
 * Because sanitizers do not instrument assembly, memory safety of the
 * kernel is tested separately here:
 *   - guard bytes around its node and frame arrays (checked by the wrapper
 *     after every search, and again below);
 *   - runs on a state block placed between inaccessible pages, so any access
 *     outside the block faults;
 *   - a register-preservation check through a small assembly caller
 *     (abi_check_sysv.S) on every kind of exit.
 *
 * Usage: test_asm_equivalence [random_cases]
 *   With a number, that many extra randomized cases are run (see test_fuzz).
 */
#define _DEFAULT_SOURCE /* MAP_ANONYMOUS */

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "asm_solver.h"
#include "test_util.h"

int cb_abi_check(CbAsmState *state, int max_nodes, int *kernel_result);

static long cases, solved;

static void fill_target(char *t, int len, unsigned v) {
    for (int i = 0; i < len; i++) t[i] = (char)('0' + ((v >> (len - 1 - i)) & 1));
    t[len] = '\0';
}

/* Compare the two solvers on one input.  Returns the C solver's status. */
static RevSolveResult compare_opt(const char *t, size_t len, int max, uint64_t max_steps) {
    RevGraph gc, ga;
    RevLevelStats sc, sa;
    RevLevelOptions opt = {max_steps};
    RevSolveResult rc = rev_level_solve_ex(t, len, max, &opt, &gc, &sc);
    RevSolveResult ra = rev_level_solve_amd64_ex(t, len, max, &opt, &ga, &sa);
    cases++;

    int same = rc == ra && sc.steps == sa.steps && sc.peak_stack == sa.peak_stack &&
               sc.limit == sa.limit && gc.node_count == ga.node_count;
    if (same && gc.node_count > 0)
        same = memcmp(gc.nodes, ga.nodes, (size_t)gc.node_count * sizeof(RevNode)) == 0;
    CHECK_MSG(same,
              "len %zu %.40s max %d max_steps %llu: C %s steps %llu peak %d limit %d nodes %d | "
              "ASM %s steps %llu peak %d limit %d nodes %d",
              len, t, max, (unsigned long long)max_steps, rev_solve_result_str(rc),
              (unsigned long long)sc.steps, sc.peak_stack, (int)sc.limit, gc.node_count,
              rev_solve_result_str(ra), (unsigned long long)sa.steps, sa.peak_stack, (int)sa.limit,
              ga.node_count);
    CHECK(rc != REV_SOLVE_INTERNAL_ERROR && ra != REV_SOLVE_INTERNAL_ERROR);
    if (ra == REV_SOLVE_SOLVED) {
        solved++;
        if (len < 4096) t_check_witness(&ga, t, len, max);
        else CHECK(rev_verify_prefix(&ga, t, len, NULL) == REV_OK);
    }
    return rc;
}

static RevSolveResult compare(const char *t, int max) { return compare_opt(t, strlen(t), max, 0); }

/* The corpus of test_legacy_equivalence.c, case for case. */
static void test_corpus(void) {
    char t[64];
    long before = cases;
    for (int len = 1; len <= 10; len++)
        for (unsigned v = 0; v < (1u << len); v++) {
            fill_target(t, len, v);
            for (int max = 1; max <= 5; max++) compare(t, max);
        }
    unsigned long long x = 0x9E3779B97F4A7C15ull;
    for (int n = 0; n < 200; n++) {
        int len = 11 + n % 8;
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            t[i] = (char)('0' + (int)((x >> 32) & 1));
        }
        t[len] = '\0';
        for (int max = 1; max <= 10; max++)
            if (compare(t, max) == REV_SOLVE_SOLVED) break;
    }
    for (int max = 2; max <= 10; max++) compare("010010001", max);
    printf("corpus: %ld cases\n", cases - before);
    CHECK(cases - before == 11327);
}

static const char *bench_targets[] = {
    "010010001",
    "00000000",
    "01010101",
    "0110100110010110",
    "001011011101111011111",
    "110100100010000100000",
    "0100011011000001010011",
    "01101001100101101001011001101001",
    "0001101100101110100111",
    "000001000110010100111010",
};
#define N_BENCH (sizeof bench_targets / sizeof *bench_targets)

/* Benchmark workloads: every budget from 1 up to the first that succeeds,
 * and then generous budgets, where the search shape is different. */
static void test_benchmark_targets(void) {
    for (size_t i = 0; i < N_BENCH; i++) {
        int n;
        for (n = 1; n <= 10; n++)
            if (compare(bench_targets[i], n) == REV_SOLVE_SOLVED) break;
        CHECK(n <= 10);
        compare(bench_targets[i], 12);
        compare(bench_targets[i], 64);
    }
}

/* Step limits: the two solvers must stop at the same step with the same
 * peak depth, for every limit, including the exact step of success. */
static void test_step_limits(void) {
    struct { const char *t; int max; } in[] = {
        {"010010001", 2}, {"010010001", 3}, {"010010001", 10}, {"1", 2}, {"0", 1},
        {"01010101", 2}, {"0110100110010110", 5}, {"0110100110010110", 6},
        {"001011011101111011111", 7}, {"1110010001101", 4}, {"00101110101", 64},
    };
    unsigned long long x = 88172645463325252ull;
    for (size_t i = 0; i < sizeof in / sizeof *in; i++) {
        RevGraph g;
        RevLevelStats st;
        size_t len = strlen(in[i].t);
        rev_level_solve_ex(in[i].t, len, in[i].max, NULL, &g, &st);
        uint64_t total = st.steps;
        uint64_t dense = total < 3000 ? total : 3000;
        for (uint64_t k = 1; k <= dense + 2; k++) compare_opt(in[i].t, len, in[i].max, k);
        if (total > 3000) {
            for (int j = 0; j < 300; j++) {
                x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                compare_opt(in[i].t, len, in[i].max, 1 + x % total);
            }
            for (uint64_t k = total - 3; k <= total + 2; k++)
                compare_opt(in[i].t, len, in[i].max, k);
        }
    }
}

/* Deep stacks: long targets around the point where the C solver's stack
 * fills.  Both must flip from SOLVED to RESOURCE_LIMIT at the same length. */
static void test_stack_boundary(void) {
    static char buf[REV_LEVEL_MAX_STACK + 64];
    const char *patterns[] = {"0", "1", "01", "10", "001", "011", "0010111"};
    for (size_t p = 0; p < sizeof patterns / sizeof *patterns; p++) {
        size_t plen = strlen(patterns[p]);
        for (size_t i = 0; i < sizeof buf - 1; i++) buf[i] = patterns[p][i % plen];
        int flips = 0, prev_solved = 1;
        /* coarse sweep, then every length near REV_LEVEL_MAX_STACK / 2 */
        for (size_t len = 500; len <= REV_LEVEL_MAX_STACK + 32; len += 487) {
            RevSolveResult r = compare_opt(buf, len, 4, 0);
            if ((r == REV_SOLVE_SOLVED) != prev_solved) { flips++; prev_solved = !prev_solved; }
        }
        CHECK_MSG(flips == 1, "pattern %s: expected one SOLVED->LIMIT transition", patterns[p]);
        for (size_t len = REV_LEVEL_MAX_STACK / 2 - 12; len <= REV_LEVEL_MAX_STACK / 2 + 12; len++)
            compare_opt(buf, len, 4, 0);
    }
}

/* Stack capacity as a parameter: with exactly the peak depth the search
 * needs, nothing changes; with one frame less it must stop with a stack
 * limit at that depth; never anything else, and never a guard violation. */
static void test_stack_capacity(void) {
    for (size_t i = 0; i < N_BENCH; i++) {
        const char *t = bench_targets[i];
        size_t len = strlen(t);
        if (len > 22) continue; /* keep this test quick */
        RevGraph g, gc;
        RevLevelStats full, st;
        int n = 1;
        while (rev_level_solve_ex(t, len, n, NULL, &gc, &full) != REV_SOLVE_SOLVED) n++;
        int peak = full.peak_stack;

        RevSolveResult r = rev_level_solve_amd64_cap(t, len, n, NULL, peak, &g, &st);
        CHECK(r == REV_SOLVE_SOLVED && st.steps == full.steps && st.peak_stack == peak);
        CHECK(g.node_count == gc.node_count &&
              memcmp(g.nodes, gc.nodes, (size_t)g.node_count * sizeof(RevNode)) == 0);

        uint64_t prev_steps = 0;
        for (int cap = 1; cap < peak; cap++) {
            r = rev_level_solve_amd64_cap(t, len, n, NULL, cap, &g, &st);
            CHECK_MSG(r == REV_SOLVE_RESOURCE_LIMIT && st.limit == REV_LIMIT_STACK &&
                          st.peak_stack == cap && g.node_count == 0,
                      "%s cap %d: %s limit %d peak %d", t, cap, rev_solve_result_str(r),
                      (int)st.limit, st.peak_stack);
            /* a larger stack never fills sooner */
            CHECK(st.steps >= prev_steps && st.steps <= full.steps);
            prev_steps = st.steps;
            CHECK(cb_amd64_guards_intact());
        }
    }
    RevGraph g;
    CHECK(rev_level_solve_amd64_cap("01", 2, 2, NULL, 0, &g, NULL) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64_cap("01", 2, 2, NULL, REV_LEVEL_MAX_STACK + 1, &g, NULL) ==
          REV_SOLVE_INVALID_INPUT);
}

static void test_invalid_input(void) {
    RevGraph g;
    g.node_count = 77;
    CHECK(rev_level_solve_amd64("", 0, 3, &g) == REV_SOLVE_INVALID_INPUT && g.node_count == 0);
    CHECK(rev_level_solve_amd64(NULL, 3, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("01a1", 4, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("0101", 4, 0, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("0101", 4, -1, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("0101", 4, REV_LEVEL_MAX_NODES + 1, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("0101", 4, 3, NULL) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve_amd64("0100zz", 4, 3, &g) == REV_SOLVE_SOLVED);
    t_check_witness(&g, "0100", 4, 3);
}

/* ---- kernel called directly, on a state block fenced by guard pages ----- */

/* A CbAsmState with an inaccessible page directly after it (at_end) or
 * directly before it. */
static CbAsmState *fenced_state(int at_end) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t body = (sizeof(CbAsmState) + page - 1) / page * page;
    unsigned char *m = mmap(NULL, body + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return NULL;
    if (mprotect(m + page, body, PROT_READ | PROT_WRITE) != 0) return NULL;
    unsigned char *p = at_end ? m + page + body - sizeof(CbAsmState) : m + page;
    memset(p, 0x5A, sizeof(CbAsmState));
    return (CbAsmState *)(void *)p;
}

static void run_direct(CbAsmState *s, const char *t, size_t len, int max, uint64_t max_steps,
                       int cap) {
    RevGraph ref;
    RevLevelStats rs;
    RevLevelOptions opt = {max_steps};
    RevSolveResult want = rev_level_solve_amd64_cap(t, len, max, &opt, cap, &ref, &rs);

    memset(s, 0x5A, sizeof *s); /* the kernel must not depend on old contents */
    s->target = t;
    s->target_len = len;
    s->step_limit = max_steps ? max_steps : UINT64_MAX;
    s->max_stack = (uint32_t)cap;

    int result = -1;
    int clobbered = cb_abi_check(s, max, &result);
    CHECK_MSG(clobbered == 0, "callee-saved register mask %d, target %.30s max %d", clobbered, t,
              max);
    CHECK_MSG(result == (int)want, "direct %d wrapper %d", result, (int)want);
    CHECK(s->steps == rs.steps && (int)s->peak_stack == rs.peak_stack &&
          s->limit_hit == (uint32_t)rs.limit);
    int guards = 1;
    for (int i = 0; i < CB_ASM_GUARD; i++)
        if (s->guard0[i] != 0x5A || s->guard1[i] != 0x5A || s->guard2[i] != 0x5A) guards = 0;
    CHECK(guards);
    /* inputs are read-only to the kernel */
    CHECK(s->target == t && s->target_len == len && s->max_stack == (uint32_t)cap);
    if (result == CB_RES_SOLVED) {
        CHECK((int)s->node_count == ref.node_count);
        for (int i = 0; i < ref.node_count && i < CB_ASM_MAX_NODES; i++) {
            int c = s->nodes[i][0], l0 = s->nodes[i][1], l1 = s->nodes[i][2];
            CHECK((c < 0 ? 0 : c) == ref.nodes[i].color);
            CHECK((l0 < 0 ? 0 : l0) == ref.nodes[i].links[0]);
            CHECK((l1 < 0 ? 0 : l1) == ref.nodes[i].links[1]);
        }
    }
}

static void test_direct_and_abi(void) {
    static char zeros[REV_LEVEL_MAX_STACK + 1];
    memset(zeros, '0', REV_LEVEL_MAX_STACK);
    for (int at_end = 0; at_end <= 1; at_end++) {
        CbAsmState *s = fenced_state(at_end);
        CHECK(s != NULL);
        if (!s) return;
        /* every kind of exit: solved, unsat, step limit, stack limit */
        run_direct(s, "010010001", 9, 3, 0, REV_LEVEL_MAX_STACK);
        run_direct(s, "010010001", 9, 2, 0, REV_LEVEL_MAX_STACK);
        run_direct(s, "010010001", 9, 3, 40, REV_LEVEL_MAX_STACK);
        run_direct(s, "010010001", 9, 3, 0, 7);
        run_direct(s, "0", 1, 1, 0, 1);
        run_direct(s, "01", 2, 2, 0, 1);
        /* the full stack, the full node array, and both at once */
        run_direct(s, zeros, REV_LEVEL_MAX_STACK, 1, 0, REV_LEVEL_MAX_STACK);
        run_direct(s, zeros, REV_LEVEL_MAX_STACK / 2 - 2, 1, 0, REV_LEVEL_MAX_STACK);
        run_direct(s, "0110100110010110", 16, 64, 0, REV_LEVEL_MAX_STACK);
        run_direct(s, "01101001100101101001011001101001", 32, 64, 0, REV_LEVEL_MAX_STACK);
        for (size_t i = 0; i < N_BENCH; i++)
            if (strlen(bench_targets[i]) <= 22) {
                run_direct(s, bench_targets[i], strlen(bench_targets[i]), 7, 0,
                           REV_LEVEL_MAX_STACK);
                run_direct(s, bench_targets[i], strlen(bench_targets[i]), 5, 0,
                           REV_LEVEL_MAX_STACK);
                run_direct(s, bench_targets[i], strlen(bench_targets[i]), 7, 5000, 30);
            }
    }
}

/* All 64 nodes in use: a target whose first solution in move order needs
 * many nodes when the budget allows them. */
static void test_many_nodes(void) {
    char t[80];
    unsigned long long x = 0x2545F4914F6CDD1Dull;
    int most = 0;
    for (int n = 0; n < 40; n++) {
        int len = 40 + n;
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            t[i] = (char)('0' + (int)((x >> 32) & 1));
        }
        t[len] = '\0';
        RevGraph g;
        RevLevelOptions opt = {200000};
        compare_opt(t, (size_t)len, 64, opt.max_steps);
        if (rev_level_solve_amd64_ex(t, (size_t)len, 64, &opt, &g, NULL) == REV_SOLVE_SOLVED &&
            g.node_count > most)
            most = g.node_count;
    }
    printf("largest graph returned at budget 64: %d nodes\n", most);
    CHECK(most >= 10);

    /*
     * Longer targets fill the whole node array before the search is cut
     * off.  The two solvers must still agree at the cut, and the kernel's
     * node count at that moment shows that the last node was really in use.
     */
    static char big[400];
    CbAsmState *s = fenced_state(1);
    CHECK(s != NULL);
    if (!s) return;
    int fullest = 0;
    for (int n = 0; n < 6; n++) {
        int len = 150 + 40 * n;
        for (int i = 0; i < len; i++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            big[i] = (char)('0' + (int)((x >> 32) & 1));
        }
        for (uint64_t limit = 20000; limit <= 320000; limit *= 2) {
            compare_opt(big, (size_t)len, 64, limit);
            run_direct(s, big, (size_t)len, 64, limit, REV_LEVEL_MAX_STACK);
            if ((int)s->node_count > fullest) fullest = (int)s->node_count;
        }
    }
    printf("most nodes in use when a budget-64 search was cut off: %d\n", fullest);
    CHECK(fullest == CB_ASM_MAX_NODES);
}

/*
 * Optional randomized run: `test_asm_equivalence N` adds N random cases to
 * the fixed ones.  Targets of 1..40 bits in several styles, budgets 1..9 and
 * occasionally 64, almost always with a step limit so that every case is
 * bounded.  Each case is the full C-versus-assembly comparison; one in eight
 * also re-runs the assembly with the stack capped at exactly the depth the
 * search needs, and at one frame less.
 */
static void test_fuzz(long n) {
    char t[64];
    unsigned long long x = 0xD1B54A32D192ED03ull;
    long capped = 0;
#define NEXT() (x ^= x << 13, x ^= x >> 7, x ^= x << 17, x)
    for (long i = 0; i < n; i++) {
        unsigned long long r = NEXT();
        int len = 1 + (int)(r % 40);
        int style = (int)((r >> 8) % 4);
        int period = 1 + (int)((r >> 12) % 7);
        for (int k = 0; k < len; k++) {
            unsigned long long b = NEXT();
            int bit;
            if (style == 0) bit = (int)((b >> 33) & 1);                 /* uniform */
            else if (style == 1) bit = ((b >> 33) % 8) == 0;            /* mostly zeros */
            else if (style == 2) bit = ((b >> 33) % 8) != 0;            /* mostly ones */
            else bit = k < period ? (int)((b >> 33) & 1) : t[k - period] - '0'; /* periodic */
            t[k] = (char)('0' + bit);
        }
        t[len] = '\0';
        int max = 1 + (int)((r >> 16) % 9);
        if ((r >> 24) % 16 == 0) max = 64;

        uint64_t limit;
        switch ((r >> 32) % 4) {
        case 0: limit = 1 + (r >> 40) % 2000; break;
        case 1: limit = 20000; break;
        case 2: limit = 60000; break;
        default: limit = (len <= 12 && max <= 5) ? 0 : 5000; break; /* 0 = unlimited */
        }
        compare_opt(t, (size_t)len, max, limit);

        if ((r >> 36) % 8 == 0) {
            RevGraph gc, ga;
            RevLevelStats sc, sa;
            RevLevelOptions opt = {limit};
            RevSolveResult rc = rev_level_solve_ex(t, (size_t)len, max, &opt, &gc, &sc);
            int peak = sc.peak_stack;
            RevSolveResult ra = rev_level_solve_amd64_cap(t, (size_t)len, max, &opt, peak, &ga, &sa);
            CHECK_MSG(ra == rc && sa.steps == sc.steps && sa.peak_stack == peak &&
                          sa.limit == sc.limit && ga.node_count == gc.node_count,
                      "cap == peak changed the result: %s max %d limit %llu", t, max,
                      (unsigned long long)limit);
            if (peak > 1) {
                ra = rev_level_solve_amd64_cap(t, (size_t)len, max, &opt, peak - 1, &ga, &sa);
                CHECK_MSG(ra == REV_SOLVE_RESOURCE_LIMIT && sa.limit == REV_LIMIT_STACK &&
                              sa.peak_stack == peak - 1 && sa.steps <= sc.steps,
                          "cap == peak - 1: %s max %d: %s limit %d peak %d", t, max,
                          rev_solve_result_str(ra), (int)sa.limit, sa.peak_stack);
            }
            capped++;
        }
    }
#undef NEXT
    printf("fuzz: %ld random cases, %ld of them also with capped stacks\n", n, capped);
}

int main(int argc, char **argv) {
    test_corpus();
    test_benchmark_targets();
    test_step_limits();
    test_stack_boundary();
    test_stack_capacity();
    test_invalid_input();
    test_direct_and_abi();
    test_many_nodes();
    if (argc > 1) test_fuzz(atol(argv[1]));
    CHECK(cb_amd64_guards_intact());
    printf("%ld differential cases, %ld solved\n", cases, solved);
    return t_report("test_asm_equivalence");
}
