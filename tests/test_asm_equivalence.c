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
 * Usage: test_asm_equivalence [random_cases [kernel]]
 *   With a number, that many extra randomized cases are run (see test_fuzz).
 *   All kernels in the registry are tested unless one is named.
 */
#define _DEFAULT_SOURCE /* MAP_ANONYMOUS */

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "asm_solver.h"
#include "test_util.h"

int cb_abi_check(CbAsmState *state, int max_nodes, int *kernel_result, CbAmd64KernelFn kernel);

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

    static CbAsmCounters direct_counters; /* for the counting kernels */
    memset(s, 0x5A, sizeof *s); /* the kernel must not depend on old contents */
    s->scratch[1] = (uint64_t)(uintptr_t)&direct_counters;
    s->target = t;
    s->target_len = len;
    s->step_limit = max_steps ? max_steps : UINT64_MAX;
    s->max_stack = (uint32_t)cap;

    int result = -1;
    int clobbered = cb_abi_check(s, max, &result, cb_amd64_kernel_resolve(max));
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
    (void)capped;
}

/*
 * Kernel selection by instruction set.  The limit lets the choice a CPU
 * without BMI2 or AVX-512 would get be exercised here: kernels that need the
 * masked sets must report unavailable, "auto" must fall back to baseline
 * kernels, and those must still agree with the C solver.
 */
static void test_isa_selection(void) {
    unsigned detected = cb_amd64_isa();
    int auto_k = cb_amd64_kernel_find("auto");
    CHECK(auto_k >= 0 && cb_amd64_kernel_select(auto_k) == 0);

    for (int pass = 0; pass < 2; pass++) {
        unsigned isa = pass == 0 ? detected : 0;
        cb_amd64_isa_limit(isa);
        CHECK(cb_amd64_isa() == isa);
        CHECK(cb_amd64_kernel_select(auto_k) == 0);
        for (int n = 1; n <= REV_LEVEL_MAX_NODES; n++) {
            const CbAmd64Kernel *k = cb_amd64_kernel_info(cb_amd64_kernel_used(n));
            const char *want = (isa & CB_ISA_BMI2) ? (n <= 8 ? "n8x" : "hybx")
                                                   : (n >= 4 && n <= 8 ? "n8s" : "hyb");
            CHECK_MSG(k && strcmp(k->name, want) == 0, "isa %u budget %d: auto uses %s, expected %s",
                      isa, n, k ? k->name : "?", want);
            CHECK(k && (k->isa & isa) == k->isa && k->max_nodes >= n);
        }
        for (int k = 0; k < cb_amd64_kernel_count(); k++) {
            /* available exactly when every kernel on its fallback chain is allowed */
            int ok = 1;
            for (int i = k; i >= 0; i = cb_amd64_kernel_info(i)->fallback) {
                const CbAmd64Kernel *info = cb_amd64_kernel_info(i);
                if ((info->isa & isa) != info->isa) ok = 0;
                if (info->max_nodes >= REV_LEVEL_MAX_NODES) break;
            }
            CHECK_MSG(cb_amd64_kernel_available(k) == ok, "isa %u kernel %s", isa,
                      cb_amd64_kernel_info(k)->name);
            if (!ok) CHECK(cb_amd64_kernel_select(k) != 0);
        }
        if (pass == 1) {
            /* the baseline selection, end to end */
            CHECK(cb_amd64_kernel_select(auto_k) == 0);
            test_corpus();
            test_benchmark_targets();
            /* limiting while a now-unavailable kernel is selected falls back to kernel 0 */
            cb_amd64_isa_limit(detected);
            int bmi = cb_amd64_kernel_find("n8x");
            if (cb_amd64_kernel_select(bmi) == 0) {
                cb_amd64_isa_limit(0);
                CHECK(cb_amd64_kernel_selected() == 0);
            }
        }
    }
    cb_amd64_isa_limit(~0u);
    CHECK(cb_amd64_isa() == detected);
    cb_amd64_kernel_select(0);
}

/*
 * Search-accounting counters of the "-count" kernels: they never change a
 * result, bulk steps are whole attempts (3 steps each) and never exceed the
 * logical steps, they are zero when the step limit keeps the kernel in
 * per-node mode, and they are substantial on a search-heavy target.
 */
static void test_counters(void) {
    int kc = cb_amd64_kernel_find("n8x-count");
    if (kc < 0 || cb_amd64_kernel_select(kc) != 0) {
        printf("counting kernels skipped: this CPU lacks an instruction set they need\n");
        return;
    }
    const CbAsmCounters *c = cb_amd64_counters();
    struct { const char *t; int max; } in[] = {
        {"0110100110010110", 6}, {"001011011101111011111", 7}, {"0001101100101110100111", 6},
        {"010010001", 3}, {"0110100110010110", 12},
    };
    for (size_t i = 0; i < sizeof in / sizeof *in; i++) {
        RevGraph g, gc;
        RevLevelStats st, sc;
        size_t len = strlen(in[i].t);
        RevSolveResult want = rev_level_solve_ex(in[i].t, len, in[i].max, NULL, &gc, &sc);

        cb_amd64_counters_reset();
        CHECK(c->bulk_steps == 0 && c->bulk_scans == 0 && c->bulk_loops == 0);
        RevSolveResult r = rev_level_solve_amd64_ex(in[i].t, len, in[i].max, NULL, &g, &st);
        CHECK(r == want && st.steps == sc.steps && st.peak_stack == sc.peak_stack);
        CHECK(c->bulk_steps % 3 == 0 && c->bulk_steps < st.steps);
        CHECK(c->bulk_scans >= c->bulk_loops);
        CHECK(c->fallback_steps == 0 && c->fallback_stack == 0 && c->fallback_uncolored == 0);
        if (st.steps > 100000)
            CHECK_MSG(c->bulk_steps * 20 > st.steps, "%s: only %llu of %llu steps in bulk", in[i].t,
                      (unsigned long long)c->bulk_steps, (unsigned long long)st.steps);

        /* counters accumulate until reset */
        uint64_t once = c->bulk_steps;
        CHECK(rev_level_solve_amd64_ex(in[i].t, len, in[i].max, NULL, &g, &st) == want);
        CHECK(c->bulk_steps == 2 * once);

        /* fewer steps allowed than the bulk margin: nothing may be done in bulk */
        RevLevelOptions opt = {200};
        cb_amd64_counters_reset();
        RevSolveResult rl = rev_level_solve_amd64_ex(in[i].t, len, in[i].max, &opt, &g, &st);
        RevSolveResult cl = rev_level_solve_ex(in[i].t, len, in[i].max, &opt, &gc, &sc);
        CHECK(rl == cl && st.steps == sc.steps);
        CHECK(c->bulk_steps == 0 && c->bulk_scans == 0);
        if (sc.steps == 200) CHECK(c->fallback_steps > 0 || c->bulk_loops == 0);
    }

    /* A full stack sends a link loop to the per-node code.  Which capacity
     * makes a CTRL link loop start exactly at the top is not known in
     * advance, so try them all; the results themselves are compared with
     * the C solver in test_stack_capacity. */
    RevGraph g;
    RevLevelStats st;
    uint64_t stack_fallbacks = 0;
    for (int cap = 1; cap <= 57; cap++) {
        cb_amd64_counters_reset();
        rev_level_solve_amd64_cap("001011011101111011111", 21, 7, NULL, cap, &g, &st);
        stack_fallbacks += c->fallback_stack;
        CHECK(c->bulk_steps % 3 == 0 && c->bulk_steps < st.steps + 3);
    }
    CHECK(stack_fallbacks > 0);
    cb_amd64_counters_reset();
    cb_amd64_kernel_select(0);
}

/* Everything above, for the selected kernel. */
static void test_selected_kernel(long fuzz_cases) {
    long cases0 = cases, solved0 = solved;
    int checks0 = t_checks, failures0 = t_failures;
    test_corpus();
    test_benchmark_targets();
    test_step_limits();
    test_stack_boundary();
    test_stack_capacity();
    test_invalid_input();
    test_direct_and_abi();
    test_many_nodes();
    if (fuzz_cases > 0) test_fuzz(fuzz_cases);
    CHECK(cb_amd64_guards_intact());
    printf("kernel %-10s %ld differential cases, %ld solved, %d checks, %d failures\n",
           cb_amd64_kernel_info(cb_amd64_kernel_selected())->name, cases - cases0,
           solved - solved0, t_checks - checks0, t_failures - failures0);
}

/*
 * Usage: test_asm_equivalence [random_cases [kernel]]
 * Runs every kernel this CPU supports, or only the named one.
 */
int main(int argc, char **argv) {
    long fuzz_cases = argc > 1 ? atol(argv[1]) : 0;
    const char *only = argc > 2 ? argv[2] : NULL;
    int ran = 0;
    for (int k = 0; k < cb_amd64_kernel_count(); k++) {
        const CbAmd64Kernel *info = cb_amd64_kernel_info(k);
        if (only && strcmp(only, info->name) != 0) continue;
        if (cb_amd64_kernel_select(k) != 0) {
            printf("kernel %-10s skipped: this CPU lacks an instruction set it needs\n", info->name);
            continue;
        }
        test_selected_kernel(fuzz_cases);
        ran++;
    }
    CHECK(ran > 0);
    cb_amd64_kernel_select(0);
    if (!only) {
        test_isa_selection();
        test_counters();
    }
    return t_report("test_asm_equivalence");
}
