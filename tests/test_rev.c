/* Tests for the independent REV interpreter, validator and text format. */
#include "test_util.h"

static RevGraph graph2(int c0, int a0, int b0, int c1, int a1, int b1) {
    RevGraph g;
    memset(&g, 0, sizeof g);
    g.node_count = 2;
    g.nodes[0] = (RevNode){c0, {a0, b0}};
    g.nodes[1] = (RevNode){c1, {a1, b1}};
    return g;
}

static void test_hand_traces(void) {
    char out[64];

    /* One node: output is its color forever. */
    RevGraph g;
    memset(&g, 0, sizeof g);
    g.node_count = 1;
    g.nodes[0] = (RevNode){REV_WHITE, {0, 0}};
    CHECK(rev_run(&g, 8, out) == REV_OK && strcmp(out, "00000000") == 0);
    g.nodes[0].color = REV_BLACK;
    CHECK(rev_run(&g, 8, out) == REV_OK && strcmp(out, "11111111") == 0);

    /*
     * Traced by hand from SPEC.md.  0: W [1,0]   1: B [0,1]
     *   CTRL d=c[0]=0 cp=l[0][0]=1 emit 1 | FLOW d=c[1]=1 fp=l[0][1]=0
     *   CTRL d=c[0]=0 cp=l[1][0]=0 emit 0 | FLOW d=c[0]=0 fp=l[0][0]=1
     *   CTRL d=c[1]=1 cp=l[0][1]=0 emit 0 | FLOW d=c[0]=0 fp=l[1][0]=0
     *   CTRL d=c[0]=0 cp=l[0][0]=1 emit 1 | ... period 3
     * A single-pointer walk of the same graph would emit 1111..., so this
     * also guards against collapsing fp and cp into one pointer.
     */
    g = graph2(REV_WHITE, 1, 0, REV_BLACK, 0, 1);
    CHECK(rev_run(&g, 9, out) == REV_OK && strcmp(out, "100100100") == 0);

    /*
     * 0: B [0,1]   1: W [1,0]
     *   CTRL d=1 cp=l[0][1]=1 emit 0 | FLOW d=c[1]=0 fp=l[0][0]=0
     *   CTRL d=1 cp=l[1][1]=0 emit 1 | FLOW d=c[0]=1 fp=l[0][1]=1
     *   CTRL d=c[1]=0 cp=l[0][0]=0 emit 1 | FLOW d=c[0]=1 fp=l[1][1]=0
     *   CTRL d=1 cp=l[0][1]=1 emit 0 | ... period 3
     */
    g = graph2(REV_BLACK, 0, 1, REV_WHITE, 1, 0);
    CHECK(rev_run(&g, 7, out) == REV_OK && strcmp(out, "0110110") == 0);

    /* Explicit machine state after each step of the trace above. */
    RevMachine m;
    rev_machine_reset(&m);
    CHECK(m.fp == 0 && m.cp == 0 && m.emitted == 0);
    CHECK(rev_machine_next_bit(&g, &m) == 0 && m.fp == 0 && m.cp == 1);
    CHECK(rev_machine_next_bit(&g, &m) == 1 && m.fp == 0 && m.cp == 0);
    CHECK(rev_machine_next_bit(&g, &m) == 1 && m.fp == 1 && m.cp == 0);
    CHECK(rev_machine_next_bit(&g, &m) == 0 && m.fp == 0 && m.cp == 1);
    CHECK(m.emitted == 4);

    /* The graph the historical solver prints for 010010001. */
    memset(&g, 0, sizeof g);
    g.node_count = 3;
    g.nodes[0] = (RevNode){REV_BLACK, {2, 1}};
    g.nodes[1] = (RevNode){REV_WHITE, {0, 0}};
    g.nodes[2] = (RevNode){REV_WHITE, {1, 2}};
    CHECK(rev_run(&g, 9, out) == REV_OK && strcmp(out, "010010001") == 0);
}

static void test_prefix_property(void) {
    /* Stopping early never changes the bits already emitted. */
    RevGraph g = graph2(REV_BLACK, 0, 1, REV_WHITE, 1, 0);
    char full[33], part[33];
    CHECK(rev_run(&g, 32, full) == REV_OK);
    for (size_t n = 0; n <= 32; n++) {
        CHECK(rev_run(&g, n, part) == REV_OK);
        CHECK(strlen(part) == n && memcmp(part, full, n) == 0);
    }
    CHECK(rev_run(&g, 0, part) == REV_OK && part[0] == '\0');
}

static void test_verify_prefix(void) {
    RevGraph g = graph2(REV_WHITE, 1, 0, REV_BLACK, 0, 1); /* 100100100... */
    size_t at = 99;
    CHECK(rev_verify_prefix(&g, "1", 1, NULL) == REV_OK);
    CHECK(rev_verify_prefix(&g, "100100", 6, NULL) == REV_OK);
    CHECK(rev_verify_prefix(&g, "100101", 6, &at) == REV_ERR_MISMATCH && at == 5);
    CHECK(rev_verify_prefix(&g, "0", 1, &at) == REV_ERR_MISMATCH && at == 0);
    CHECK(rev_verify_prefix(&g, "", 0, NULL) == REV_ERR_TARGET);
    CHECK(rev_verify_prefix(&g, "10x", 3, NULL) == REV_ERR_TARGET);
    CHECK(rev_verify_prefix(&g, NULL, 3, NULL) == REV_ERR_NULL);
    CHECK(rev_verify_prefix(NULL, "1", 1, NULL) == REV_ERR_NULL);
    /* len, not a terminator, bounds the target. */
    CHECK(rev_verify_prefix(&g, "100zzz", 3, NULL) == REV_OK);
}

static void test_validate(void) {
    char out[8];
    RevGraph g = graph2(REV_WHITE, 1, 0, REV_BLACK, 0, 1);
    CHECK(rev_graph_validate(&g) == REV_OK);
    CHECK(rev_graph_validate(NULL) == REV_ERR_NULL);

    RevGraph b = g;
    b.node_count = 0;
    CHECK(rev_graph_validate(&b) == REV_ERR_NODE_COUNT);
    b.node_count = REV_MAX_NODES + 1;
    CHECK(rev_graph_validate(&b) == REV_ERR_NODE_COUNT);

    b = g; b.nodes[1].color = REV_UNDECIDED;
    CHECK(rev_graph_validate(&b) == REV_ERR_COLOR);
    b = g; b.nodes[0].color = 2;
    CHECK(rev_graph_validate(&b) == REV_ERR_COLOR);
    b = g; b.nodes[0].links[1] = 2; /* == node_count */
    CHECK(rev_graph_validate(&b) == REV_ERR_LINK);
    b = g; b.nodes[1].links[0] = REV_UNDECIDED;
    CHECK(rev_graph_validate(&b) == REV_ERR_LINK);

    /* The interpreter refuses to run an invalid graph. */
    CHECK(rev_run(&b, 4, out) == REV_ERR_LINK);
    CHECK(rev_verify_prefix(&b, "1", 1, NULL) == REV_ERR_LINK);

    CHECK(rev_target_validate("0101", 4) == REV_OK);
    CHECK(rev_target_validate("0121", 4) == REV_ERR_TARGET);
    CHECK(rev_target_validate("01 1", 4) == REV_ERR_TARGET);
    CHECK(rev_target_validate("", 0) == REV_ERR_TARGET);
}

static RevStatus read_text(const char *text, RevGraph *g) {
    FILE *f = tmpfile();
    if (!f) return REV_ERR_IO;
    fputs(text, f);
    rewind(f);
    RevStatus st = rev_graph_read(f, g);
    fclose(f);
    return st;
}

static void test_text_format(void) {
    RevGraph g, r;
    memset(&g, 0, sizeof g);
    g.node_count = 3;
    g.nodes[0] = (RevNode){REV_BLACK, {2, 1}};
    g.nodes[1] = (RevNode){REV_WHITE, {0, 0}};
    g.nodes[2] = (RevNode){REV_WHITE, {1, 2}};

    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        CHECK(rev_graph_write(f, &g) == REV_OK);
        rewind(f);
        CHECK(rev_graph_read(f, &r) == REV_OK);
        CHECK(r.node_count == 3 && memcmp(r.nodes, g.nodes, 3 * sizeof(RevNode)) == 0);
        fclose(f);
    }

    CHECK(read_text("# comment\nREV 1\n\nnodes 2  # two\n0 W 1 0\n1 1 0 1\n", &r) == REV_OK);
    CHECK(r.node_count == 2 && r.nodes[0].color == REV_WHITE && r.nodes[1].color == REV_BLACK);
    CHECK(r.nodes[0].links[0] == 1 && r.nodes[1].links[1] == 1);

    CHECK(read_text("", &r) == REV_ERR_FORMAT);
    CHECK(read_text("REV 2\nnodes 1\n0 W 0 0\n", &r) == REV_ERR_FORMAT);
    CHECK(read_text("REV 1\nnodes 0\n", &r) == REV_ERR_NODE_COUNT);
    CHECK(read_text("REV 1\nnodes 2\n0 W 1 0\n", &r) == REV_ERR_FORMAT);          /* short */
    CHECK(read_text("REV 1\nnodes 1\n0 W 0 0\n1 W 0 0\n", &r) == REV_ERR_FORMAT); /* extra */
    CHECK(read_text("REV 1\nnodes 2\n1 W 1 0\n0 B 0 1\n", &r) == REV_ERR_FORMAT); /* order */
    CHECK(read_text("REV 1\nnodes 1\n0 X 0 0\n", &r) == REV_ERR_FORMAT);
    CHECK(read_text("REV 1\nnodes 1\n0 W 0 0 7\n", &r) == REV_ERR_FORMAT);
    CHECK(read_text("REV 1\nnodes 1\n0 W 0 1\n", &r) == REV_ERR_LINK);

    /* An invalid graph is never written. */
    RevGraph bad = g;
    bad.nodes[0].links[0] = 3;
    f = tmpfile();
    if (f) {
        CHECK(rev_graph_write(f, &bad) == REV_ERR_LINK);
        fclose(f);
    }
}

int main(void) {
    test_hand_traces();
    test_prefix_property();
    test_verify_prefix();
    test_validate();
    test_text_format();
    return t_report("test_rev");
}
