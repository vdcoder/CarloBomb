/* Tests for the parameterized C LevelSolver. */
#include "level_solver.h"
#include "test_util.h"

/* Solve with the given budget; on success run every witness check. */
static RevSolveResult solve_checked(const char *t, int budget, RevGraph *g) {
    size_t len = strlen(t);
    RevSolveResult r = rev_level_solve(t, len, budget, g);
    if (r == REV_SOLVE_SOLVED) t_check_witness(g, t, len, budget);
    else CHECK(g->node_count == 0);
    return r;
}

static void expect_solved(const char *t, int budget) {
    RevGraph g;
    RevSolveResult r = solve_checked(t, budget, &g);
    CHECK_MSG(r == REV_SOLVE_SOLVED, "target %s budget %d: %s", t, budget, rev_solve_result_str(r));
}

static void expect_unsat(const char *t, int budget) {
    RevGraph g;
    RevSolveResult r = solve_checked(t, budget, &g);
    CHECK_MSG(r == REV_SOLVE_PROVEN_UNSAT, "target %s budget %d: %s", t, budget,
              rev_solve_result_str(r));
}

static void test_basic(void) {
    RevGraph g;

    /* Single bits and constants have one-node solutions. */
    const char *one_node[] = {"0", "1", "00000000", "11111111"};
    for (size_t i = 0; i < sizeof one_node / sizeof *one_node; i++) {
        CHECK(solve_checked(one_node[i], 1, &g) == REV_SOLVE_SOLVED);
        CHECK(g.node_count == 1);
        CHECK(solve_checked(one_node[i], 5, &g) == REV_SOLVE_SOLVED);
    }

    /*
     * A budget is an upper bound, not a request for the smallest graph.  The
     * preserved move order tries WHITE before BLACK and only then a new
     * node, so for "1" with room to spare the first solution found has two
     * nodes even though one suffices.  (legacy_original.c starts at budget 2
     * and so reports two nodes for this target.)  Minimality is the
     * MinimalSolver's job.
     */
    CHECK(solve_checked("1", 2, &g) == REV_SOLVE_SOLVED);
    CHECK(g.node_count == 2);
    CHECK(solve_checked("0", 2, &g) == REV_SOLVE_SOLVED);
    CHECK(g.node_count == 1);

    /* One node can only emit a constant, so these need more than one. */
    const char *multi[] = {"01", "10", "01010101", "10101010"};
    for (size_t i = 0; i < sizeof multi / sizeof *multi; i++) {
        expect_unsat(multi[i], 1);
        CHECK(solve_checked(multi[i], 2, &g) == REV_SOLVE_SOLVED);
        CHECK(g.node_count == 2);
    }

    /* Short periodic and irregular strings, generous budget. */
    const char *misc[] = {"001001001001", "011011011011", "000100010001", "110110110110",
                          "0110100110010110", "1011000111", "00101110101", "1110010001101"};
    for (size_t i = 0; i < sizeof misc / sizeof *misc; i++) expect_solved(misc[i], 8);
}

static void test_historical(void) {
    /* legacy_original.c reports nothing at budget 2 and this graph at 3. */
    RevGraph g;
    expect_unsat("010010001", 1);
    expect_unsat("010010001", 2);
    CHECK(solve_checked("010010001", 3, &g) == REV_SOLVE_SOLVED);
    CHECK(g.node_count == 3);
    CHECK(g.nodes[0].color == REV_BLACK && g.nodes[0].links[0] == 2 && g.nodes[0].links[1] == 1);
    CHECK(g.nodes[1].color == REV_WHITE && g.nodes[1].links[0] == 0 && g.nodes[1].links[1] == 0);
    CHECK(g.nodes[2].color == REV_WHITE && g.nodes[2].links[0] == 1 && g.nodes[2].links[1] == 2);
    /* With a looser budget the first solution in move order has 4 nodes. */
    CHECK(solve_checked("010010001", 10, &g) == REV_SOLVE_SOLVED);
    CHECK(g.node_count == 4);
}

static void test_invalid_input(void) {
    RevGraph g;
    g.node_count = 77;
    CHECK(rev_level_solve("", 0, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(g.node_count == 0);
    CHECK(rev_level_solve(NULL, 3, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("01a1", 4, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("0121", 4, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("01 1", 4, 3, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("0101", 4, 0, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("0101", 4, -1, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("0101", 4, REV_LEVEL_MAX_NODES + 1, &g) == REV_SOLVE_INVALID_INPUT);
    CHECK(rev_level_solve("0101", 4, 3, NULL) == REV_SOLVE_INVALID_INPUT);

    /* The length argument, not a terminator, bounds the target. */
    CHECK(rev_level_solve("0100zz", 4, 3, &g) == REV_SOLVE_SOLVED);
    t_check_witness(&g, "0100", 4, 3);

    /* The largest supported budget is accepted. */
    CHECK(rev_level_solve("0101", 4, REV_LEVEL_MAX_NODES, &g) == REV_SOLVE_SOLVED);
}

static void test_stack_limit(void) {
    /*
     * A constant target on one node costs two frames per bit, so a target of
     * REV_LEVEL_MAX_STACK bits must fill the stack, while one of
     * REV_LEVEL_MAX_STACK / 6 bits is inside the documented safe range.
     * The answer is trivially "solvable", which is the point: the solver
     * must say RESOURCE_LIMIT, not PROVEN_UNSAT.
     */
    static char zeros[REV_LEVEL_MAX_STACK + 1];
    RevGraph g;
    RevLevelStats st;
    memset(zeros, '0', REV_LEVEL_MAX_STACK);

    RevSolveResult r = rev_level_solve_ex(zeros, REV_LEVEL_MAX_STACK, 1, NULL, &g, &st);
    CHECK(r == REV_SOLVE_RESOURCE_LIMIT);
    CHECK(st.limit == REV_LIMIT_STACK);
    CHECK(st.peak_stack == REV_LEVEL_MAX_STACK);
    CHECK(g.node_count == 0);

    size_t safe = REV_LEVEL_MAX_STACK / 6;
    r = rev_level_solve_ex(zeros, safe, 1, NULL, &g, &st);
    CHECK(r == REV_SOLVE_SOLVED);
    CHECK(st.limit == REV_LIMIT_NONE && st.peak_stack < REV_LEVEL_MAX_STACK);
    CHECK(rev_verify_prefix(&g, zeros, safe, NULL) == REV_OK);

    /* The solver is usable again after hitting the limit. */
    expect_solved("010010001", 3);
}

static void test_step_limit(void) {
    const char *sat = "0110100110010110";
    const char *unsat = "010010001"; /* at budget 2 */
    RevGraph g;
    RevLevelStats st, st2;
    RevLevelOptions opt;

    /* Solvable instance. */
    CHECK(rev_level_solve_ex(sat, strlen(sat), 8, NULL, &g, &st) == REV_SOLVE_SOLVED);
    CHECK(st.steps > 1 && st.limit == REV_LIMIT_NONE && st.peak_stack > 0);

    opt.max_steps = st.steps; /* exactly enough */
    CHECK(rev_level_solve_ex(sat, strlen(sat), 8, &opt, &g, &st2) == REV_SOLVE_SOLVED);
    CHECK(st2.steps == st.steps);

    opt.max_steps = st.steps - 1; /* one short */
    CHECK(rev_level_solve_ex(sat, strlen(sat), 8, &opt, &g, &st2) == REV_SOLVE_RESOURCE_LIMIT);
    CHECK(st2.limit == REV_LIMIT_STEPS && st2.steps == st.steps - 1 && g.node_count == 0);

    /* Unsolvable instance: a cut-off search must not claim a proof. */
    CHECK(rev_level_solve_ex(unsat, strlen(unsat), 2, NULL, &g, &st) == REV_SOLVE_PROVEN_UNSAT);
    opt.max_steps = st.steps;
    CHECK(rev_level_solve_ex(unsat, strlen(unsat), 2, &opt, &g, &st2) == REV_SOLVE_PROVEN_UNSAT);
    for (uint64_t k = 1; k < st.steps; k++) {
        opt.max_steps = k;
        RevSolveResult r = rev_level_solve_ex(unsat, strlen(unsat), 2, &opt, &g, &st2);
        CHECK_MSG(r == REV_SOLVE_RESOURCE_LIMIT, "max_steps %llu gave %s", (unsigned long long)k,
                  rev_solve_result_str(r));
    }
}

static void test_repeatable(void) {
    /* Static search state is fully reset between calls. */
    const char *t = "1011000111";
    RevGraph a, b;
    RevLevelStats sa, sb;
    CHECK(rev_level_solve_ex(t, strlen(t), 6, NULL, &a, &sa) == REV_SOLVE_SOLVED);
    expect_unsat("01", 1);
    CHECK(rev_level_solve_ex(t, strlen(t), 6, NULL, &b, &sb) == REV_SOLVE_SOLVED);
    CHECK(a.node_count == b.node_count);
    CHECK(memcmp(a.nodes, b.nodes, (size_t)a.node_count * sizeof(RevNode)) == 0);
    CHECK(sa.steps == sb.steps && sa.peak_stack == sb.peak_stack);
}

static void test_monotone_budget(void) {
    /* Once solvable at budget N, solvable at every larger budget. */
    const char *targets[] = {"010010001", "0110100110", "1110010001", "0001011101"};
    for (size_t i = 0; i < sizeof targets / sizeof *targets; i++) {
        int seen = 0;
        for (int n = 1; n <= 8; n++) {
            RevGraph g;
            RevSolveResult r = solve_checked(targets[i], n, &g);
            CHECK(r == REV_SOLVE_SOLVED || r == REV_SOLVE_PROVEN_UNSAT);
            if (seen) CHECK_MSG(r == REV_SOLVE_SOLVED, "%s lost at budget %d", targets[i], n);
            if (r == REV_SOLVE_SOLVED) seen = 1;
        }
        CHECK(seen);
    }
}

int main(void) {
    test_basic();
    test_historical();
    test_invalid_input();
    test_stack_limit();
    test_step_limit();
    test_repeatable();
    test_monotone_budget();
    return t_report("test_level_solver");
}
