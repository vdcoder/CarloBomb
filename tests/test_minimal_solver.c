/* Tests for the MinimalSolver coordinator. */
#include "minimal_solver.h"
#include "test_util.h"

static void expect_minimal(const char *t, int expect_nodes) {
    size_t len = strlen(t);
    RevGraph g;
    RevMinimalInfo info;
    RevMinimalStatus s = rev_minimal_solve(t, len, 0, NULL, &g, &info);
    CHECK_MSG(s == REV_MIN_MINIMAL, "%s: %s", t, rev_minimal_status_str(s));
    if (s != REV_MIN_MINIMAL) return;
    t_check_witness(&g, t, len, expect_nodes);
    CHECK_MSG(info.node_count == expect_nodes, "%s: %d nodes", t, info.node_count);
    CHECK(g.node_count == info.node_count);
    CHECK(info.lower_bound == info.node_count && info.last_budget == info.node_count);

    /* The certificate, re-derived here: every smaller budget is UNSAT. */
    for (int n = 1; n < info.node_count; n++) {
        RevGraph tmp;
        CHECK(rev_level_solve(t, len, n, &tmp) == REV_SOLVE_PROVEN_UNSAT);
    }
}

static void test_known(void) {
    expect_minimal("0", 1);
    expect_minimal("1", 1);
    expect_minimal("00000000", 1);
    expect_minimal("11111111", 1);
    expect_minimal("01", 2);
    expect_minimal("01010101", 2);
    expect_minimal("10101010", 2);
    expect_minimal("010010001", 3); /* agrees with legacy_original.c */
}

static void test_budget_exhausted(void) {
    /* A cap below the true minimum reports the cap, not impossibility. */
    const char *t = "010010001";
    RevGraph g;
    RevMinimalInfo info;
    RevMinimalStatus s = rev_minimal_solve(t, strlen(t), 2, NULL, &g, &info);
    CHECK(s == REV_MIN_BUDGET_EXHAUSTED);
    CHECK(info.lower_bound == 3 && info.last_budget == 2 && g.node_count == 0);
}

static void test_unknown_on_limit(void) {
    const char *t = "0110100110010110";
    RevGraph g;
    RevMinimalInfo full, info;
    RevLevelOptions opt;

    CHECK(rev_minimal_solve(t, strlen(t), 0, NULL, &g, &full) == REV_MIN_MINIMAL);
    CHECK(full.node_count >= 3);

    /* Too few steps for even the first budget. */
    opt.max_steps = 3;
    RevMinimalStatus s = rev_minimal_solve(t, strlen(t), 0, &opt, &g, &info);
    CHECK(s == REV_MIN_UNKNOWN);
    CHECK(info.limit == REV_LIMIT_STEPS && info.lower_bound == 1 && info.last_budget == 1);
    CHECK(g.node_count == 0);

    /*
     * Enough steps for the small budgets but not the last one: the solver
     * must stop with UNKNOWN and still report the lower bound it did prove.
     */
    RevLevelStats st;
    RevGraph tmp;
    uint64_t easy = 0;
    for (int n = 1; n < full.node_count; n++) {
        CHECK(rev_level_solve_ex(t, strlen(t), n, NULL, &tmp, &st) == REV_SOLVE_PROVEN_UNSAT);
        if (st.steps > easy) easy = st.steps;
    }
    CHECK(rev_level_solve_ex(t, strlen(t), full.node_count, NULL, &tmp, &st) == REV_SOLVE_SOLVED);
    if (st.steps > easy) {
        opt.max_steps = easy;
        s = rev_minimal_solve(t, strlen(t), 0, &opt, &g, &info);
        CHECK(s == REV_MIN_UNKNOWN);
        CHECK(info.lower_bound == full.node_count && info.last_budget == full.node_count);
        CHECK(info.limit == REV_LIMIT_STEPS);
    } else {
        printf("note: partial-progress case not exercised for this target\n");
    }

    /* Stack exhaustion is also UNKNOWN, never BUDGET_EXHAUSTED. */
    static char zeros[REV_LEVEL_MAX_STACK + 1];
    memset(zeros, '0', REV_LEVEL_MAX_STACK);
    s = rev_minimal_solve(zeros, REV_LEVEL_MAX_STACK, 4, NULL, &g, &info);
    CHECK(s == REV_MIN_UNKNOWN && info.limit == REV_LIMIT_STACK && info.lower_bound == 1);
}

static void test_invalid(void) {
    RevGraph g;
    CHECK(rev_minimal_solve("", 0, 0, NULL, &g, NULL) == REV_MIN_INVALID_INPUT);
    CHECK(rev_minimal_solve("012", 3, 0, NULL, &g, NULL) == REV_MIN_INVALID_INPUT);
    CHECK(rev_minimal_solve(NULL, 3, 0, NULL, &g, NULL) == REV_MIN_INVALID_INPUT);
    CHECK(rev_minimal_solve("01", 2, -1, NULL, &g, NULL) == REV_MIN_INVALID_INPUT);
    CHECK(rev_minimal_solve("01", 2, REV_LEVEL_MAX_NODES + 1, NULL, &g, NULL) ==
          REV_MIN_INVALID_INPUT);
    CHECK(rev_minimal_solve("01", 2, 0, NULL, NULL, NULL) == REV_MIN_INVALID_INPUT);
}

int main(void) {
    test_known();
    test_budget_exhausted();
    test_unknown_on_limit();
    test_invalid();
    return t_report("test_minimal_solver");
}
