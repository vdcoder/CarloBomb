/*
 * carlobomb: command-line front end.
 *
 *   carlobomb solve  --target S [--max-nodes N] [--max-steps K] [--out FILE]
 *   carlobomb level  --target S --nodes N       [--max-steps K] [--out FILE]
 *   carlobomb verify --target S --graph FILE
 *   carlobomb run    --graph FILE --bits L
 *   carlobomb bench  --target S [--max-nodes N] [--reps R]
 *   carlobomb bomb   build|resume|status|finalize|verify|lookup ...   (fixed_run_cli.c)
 *
 * solve, level and bench accept --engine c (default) or, in builds for Linux
 * x86-64, --engine amd64-NAME for an assembly kernel.  amd64-asm is the
 * reference assembly kernel; `carlobomb engines` lists the others.
 *
 * Exit status: 0 success, 1 no solution / verification failed,
 *              2 usage or invalid input, 3 resource limit (result unknown).
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_engine.h"
#include "fixed_run_cli.h"
#include "minimal_solver.h"

enum { EXIT_OK = 0, EXIT_NO = 1, EXIT_USAGE = 2, EXIT_LIMIT = 3 };

typedef struct {
    const char *target, *graph, *out, *engine_name;
    RevLevelSolveFn engine;
    long nodes, max_nodes, bits, reps;
    unsigned long long max_steps;
} Args;

static int usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  carlobomb solve  --target S [--max-nodes N] [--max-steps K] [--out FILE]\n"
            "      find a minimum-node REV for S, trying budgets 1..N (default %d)\n"
            "  carlobomb level  --target S --nodes N [--max-steps K] [--out FILE]\n"
            "      find a REV for S with at most N nodes, or prove there is none\n"
            "  carlobomb verify --target S --graph FILE\n"
            "      check that the REV in FILE emits S as a prefix\n"
            "  carlobomb run    --graph FILE --bits L\n"
            "      print the first L output bits of the REV in FILE\n"
            "  carlobomb bench  --target S [--max-nodes N] [--reps R]\n"
            "      time the minimal solve of S\n"
            "  carlobomb engines\n"
            "      list the search engines in this build\n"
            "S is a string of 0 and 1.  --max-steps limits search effort per budget.\n"
            "solve, level and bench take --engine c (default)%s.\n"
            "FixedRunBomb, the table of minimum-node REVs for every string of one length:\n",
            REV_LEVEL_MAX_NODES,
#ifdef CARLOBOMB_HAVE_AMD64_ASM
            " or amd64-NAME (experimental; `carlobomb engines` lists them)"
#else
            "; the amd64 engines are not part of this build"
#endif
    );
    fixed_run_cli_usage();
    return EXIT_USAGE;
}

static int parse_long(const char *s, long *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0') return 0;
    *out = v;
    return 1;
}

static int parse_args(int argc, char **argv, Args *a) {
    memset(a, 0, sizeof *a);
    a->nodes = a->bits = -1;
    a->reps = 0;
    a->engine = rev_level_solve_ex;
    a->engine_name = "c";
    for (int i = 2; i < argc; i += 2) {
        const char *k = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        long n;
        if (!v) { fprintf(stderr, "missing value for %s\n", k); return 0; }
        if (!strcmp(k, "--target")) a->target = v;
        else if (!strcmp(k, "--graph")) a->graph = v;
        else if (!strcmp(k, "--out")) a->out = v;
        else if (!strcmp(k, "--engine")) {
            switch (cli_engine_select(v, &a->engine)) {
            case CLI_ENGINE_OK: a->engine_name = v; break;
            case CLI_ENGINE_NO_ISA:
                fprintf(stderr, "engine %s needs an instruction set this CPU lacks\n", v);
                return 0;
            case CLI_ENGINE_NOT_BUILT:
                fprintf(stderr, "engine %s is not available in this build "
                                "(Linux x86-64 only)\n", v);
                return 0;
            default: fprintf(stderr, "unknown engine %s\n", v); return 0;
            }
        }
        else if (!parse_long(v, &n) || n < 0) { fprintf(stderr, "bad value for %s: %s\n", k, v); return 0; }
        else if (!strcmp(k, "--nodes")) a->nodes = n;
        else if (!strcmp(k, "--max-nodes")) a->max_nodes = n;
        else if (!strcmp(k, "--bits")) a->bits = n;
        else if (!strcmp(k, "--reps")) a->reps = n;
        else if (!strcmp(k, "--max-steps")) a->max_steps = (unsigned long long)n;
        else { fprintf(stderr, "unknown option %s\n", k); return 0; }
    }
    return 1;
}

static int need_target(const Args *a) {
    if (!a->target) { fprintf(stderr, "--target is required\n"); return 0; }
    if (rev_target_validate(a->target, strlen(a->target)) != REV_OK) {
        fprintf(stderr, "invalid target: must be a nonempty string of 0 and 1\n");
        return 0;
    }
    return 1;
}

static void print_table(const RevGraph *g) {
    printf("Node   Color   Link0   Link1\n");
    for (int i = 0; i < g->node_count; i++)
        printf("%-6d %-7s %-7d %d\n", i, g->nodes[i].color == REV_WHITE ? "W (0)" : "B (1)",
               g->nodes[i].links[0], g->nodes[i].links[1]);
}

/* Print a solution and re-verify it with the independent interpreter. */
static int report_solution(const RevGraph *g, const char *target, const char *out_path) {
    printf("Nodes: %d\nTarget: %s\n\n", g->node_count, target);
    print_table(g);
    RevStatus v = rev_verify_prefix(g, target, strlen(target), NULL);
    printf("\nVerification: %s\n", v == REV_OK ? "PASS" : "FAIL");
    if (v != REV_OK) return EXIT_NO;
    if (out_path) {
        FILE *f = fopen(out_path, "w");
        if (!f || rev_graph_write(f, g) != REV_OK || fclose(f) != 0) {
            fprintf(stderr, "cannot write %s\n", out_path);
            return EXIT_USAGE;
        }
        printf("Graph written to %s\n", out_path);
    }
    return EXIT_OK;
}

static int load_graph(const char *path, RevGraph *g) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    RevStatus st = rev_graph_read(f, g);
    fclose(f);
    if (st != REV_OK) { fprintf(stderr, "%s: %s\n", path, rev_status_str(st)); return 0; }
    return 1;
}

static int cmd_solve(const Args *a) {
    RevGraph g;
    RevMinimalInfo info;
    RevLevelOptions opt = {a->max_steps};
    if (!need_target(a)) return EXIT_USAGE;
    if (a->max_nodes > REV_LEVEL_MAX_NODES) {
        fprintf(stderr, "--max-nodes must be 1..%d\n", REV_LEVEL_MAX_NODES);
        return EXIT_USAGE;
    }
    int cap = a->max_nodes ? (int)a->max_nodes : REV_LEVEL_MAX_NODES;
    RevMinimalStatus s =
        rev_minimal_solve_with(a->engine, a->target, strlen(a->target), cap, &opt, &g, &info);
    switch (s) {
    case REV_MIN_MINIMAL:
        printf("REV solution (minimum: budgets 1..%d proven to have none)\n", info.node_count - 1);
        printf("Engine: %s\n", a->engine_name);
        printf("Search steps: %llu\n", (unsigned long long)info.steps);
        return report_solution(&g, a->target, a->out);
    case REV_MIN_BUDGET_EXHAUSTED:
        printf("No REV with at most %d nodes emits %s (exhaustively proven).\n", cap, a->target);
        printf("A solution, if any, needs at least %d nodes.\n", info.lower_bound);
        return EXIT_NO;
    case REV_MIN_UNKNOWN:
        printf("UNKNOWN: search stopped at node budget %d (%s).\n", info.last_budget,
               rev_level_limit_str(info.limit));
        printf("This is not a proof of impossibility.\n");
        printf("Proven so far: a solution needs at least %d node(s).\n", info.lower_bound);
        return EXIT_LIMIT;
    default:
        fprintf(stderr, "error: %s\n", rev_minimal_status_str(s));
        return EXIT_USAGE;
    }
}

static int cmd_level(const Args *a) {
    RevGraph g;
    RevLevelStats st;
    RevLevelOptions opt = {a->max_steps};
    if (!need_target(a)) return EXIT_USAGE;
    if (a->nodes < 1 || a->nodes > REV_LEVEL_MAX_NODES) {
        fprintf(stderr, "--nodes is required and must be 1..%d\n", REV_LEVEL_MAX_NODES);
        return EXIT_USAGE;
    }
    RevSolveResult r = a->engine(a->target, strlen(a->target), (int)a->nodes, &opt, &g, &st);
    switch (r) {
    case REV_SOLVE_SOLVED:
        printf("REV solution (node budget %ld; not necessarily minimum)\n", a->nodes);
        printf("Engine: %s\n", a->engine_name);
        printf("Search steps: %llu\n", (unsigned long long)st.steps);
        return report_solution(&g, a->target, a->out);
    case REV_SOLVE_PROVEN_UNSAT:
        printf("PROVEN_UNSAT: no REV with at most %ld nodes emits %s.\n", a->nodes, a->target);
        printf("Search steps: %llu\n", (unsigned long long)st.steps);
        return EXIT_NO;
    case REV_SOLVE_RESOURCE_LIMIT:
        printf("RESOURCE_LIMIT: search stopped (%s) after %llu steps.\n",
               rev_level_limit_str(st.limit), (unsigned long long)st.steps);
        printf("This is not a proof of impossibility.\n");
        return EXIT_LIMIT;
    default:
        fprintf(stderr, "error: %s\n", rev_solve_result_str(r));
        return EXIT_USAGE;
    }
}

static int cmd_verify(const Args *a) {
    RevGraph g;
    size_t at = 0;
    if (!need_target(a)) return EXIT_USAGE;
    if (!a->graph) { fprintf(stderr, "--graph is required\n"); return EXIT_USAGE; }
    if (!load_graph(a->graph, &g)) return EXIT_USAGE;
    printf("Nodes: %d\nTarget: %s\n\n", g.node_count, a->target);
    print_table(&g);
    RevStatus v = rev_verify_prefix(&g, a->target, strlen(a->target), &at);
    if (v == REV_OK) { printf("\nVerification: PASS\n"); return EXIT_OK; }
    printf("\nVerification: FAIL (%s", rev_status_str(v));
    if (v == REV_ERR_MISMATCH) printf(", first difference at bit %zu", at);
    printf(")\n");
    return EXIT_NO;
}

static int cmd_run(const Args *a) {
    RevGraph g;
    if (!a->graph || a->bits < 0) { fprintf(stderr, "--graph and --bits are required\n"); return EXIT_USAGE; }
    if (!load_graph(a->graph, &g)) return EXIT_USAGE;
    RevMachine m;
    rev_machine_reset(&m);
    for (long i = 0; i < a->bits; i++) putchar('0' + rev_machine_next_bit(&g, &m));
    putchar('\n');
    return EXIT_OK;
}

static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

static int cmd_bench(const Args *a) {
    enum { SAMPLES = 9 };
    RevGraph g;
    RevMinimalInfo info;
    double s[SAMPLES];
    if (!need_target(a)) return EXIT_USAGE;
    if (a->max_nodes > REV_LEVEL_MAX_NODES) return usage();
    int cap = a->max_nodes ? (int)a->max_nodes : REV_LEVEL_MAX_NODES;
    size_t len = strlen(a->target);

    long reps = a->reps;
    if (reps == 0) { /* calibrate to about 50 ms per sample */
        reps = 1;
        for (;;) {
            double t0 = cb_now();
            for (long i = 0; i < reps; i++)
                rev_minimal_solve_with(a->engine, a->target, len, cap, NULL, &g, &info);
            if (cb_now() - t0 >= 0.05 || reps >= (1L << 28)) break;
            reps *= 2;
        }
    }
    for (int k = 0; k < SAMPLES; k++) {
        double t0 = cb_now();
        for (long i = 0; i < reps; i++)
            rev_minimal_solve_with(a->engine, a->target, len, cap, NULL, &g, &info);
        s[k] = (cb_now() - t0) / (double)reps;
    }
    qsort(s, SAMPLES, sizeof s[0], cmp_double);
    RevMinimalStatus st = rev_minimal_solve_with(a->engine, a->target, len, cap, NULL, &g, &info);

    printf("Target: %s (length %zu)\n", a->target, len);
    printf("Solver: minimal (LevelSolver engine %s, budgets 1..%d)\n", a->engine_name, cap);
    printf("Status: %s\n", rev_minimal_status_str(st));
    if (st == REV_MIN_MINIMAL) printf("Nodes: %d\n", info.node_count);
    printf("Search steps: %llu\n", (unsigned long long)info.steps);
    printf("Peak stack: %d frames\n", info.peak_stack);
    printf("Time per solve: min %.3f us, median %.3f us (%d samples x %ld runs)\n", s[0] * 1e6,
           s[SAMPLES / 2] * 1e6, (int)SAMPLES, reps);
    return st == REV_MIN_MINIMAL ? EXIT_OK : st == REV_MIN_UNKNOWN ? EXIT_LIMIT : EXIT_NO;
}

static int cmd_engines(void) {
    printf("c                portable C LevelSolver (default)\n");
#ifdef CARLOBOMB_HAVE_AMD64_ASM
    for (int k = 0; k < cb_amd64_kernel_count(); k++) {
        const CbAmd64Kernel *info = cb_amd64_kernel_info(k);
        printf("amd64-%-10s %s%s\n", info->name, info->what,
               cb_amd64_kernel_available(k) ? "" : "  [not available on this CPU]");
    }
    unsigned isa = cb_amd64_isa();
    printf("\nInstruction sets found: BMI1/BMI2 %s, AVX-512 F/BW/VL %s\n",
           (isa & CB_ISA_BMI2) ? "yes" : "no", (isa & CB_ISA_AVX512) ? "yes" : "no");
    if (cb_amd64_kernel_select(cb_amd64_kernel_find("auto")) == 0) {
        printf("amd64-auto runs:");
        for (int n = 1, from = 1; n <= REV_LEVEL_MAX_NODES; n++) {
            int k = cb_amd64_kernel_used(n);
            if (n == REV_LEVEL_MAX_NODES || cb_amd64_kernel_used(n + 1) != k) {
                printf(" %s for node budgets %d-%d%s", cb_amd64_kernel_info(k)->name, from, n,
                       n == REV_LEVEL_MAX_NODES ? "\n" : ",");
                from = n + 1;
            }
        }
        cb_amd64_kernel_select(0);
    }
#endif
    return EXIT_OK;
}

int main(int argc, char **argv) {
    Args a;
    if (argc < 2) return usage();
    if (!strcmp(argv[1], "engines")) return cmd_engines();
    if (!strcmp(argv[1], "bomb")) return fixed_run_cli(argc, argv);
    if (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
        usage();
        return EXIT_OK;
    }
    if (!parse_args(argc, argv, &a)) return usage();
    if (!strcmp(argv[1], "solve")) return cmd_solve(&a);
    if (!strcmp(argv[1], "level")) return cmd_level(&a);
    if (!strcmp(argv[1], "verify")) return cmd_verify(&a);
    if (!strcmp(argv[1], "run")) return cmd_run(&a);
    if (!strcmp(argv[1], "bench")) return cmd_bench(&a);
    fprintf(stderr, "unknown command %s\n", argv[1]);
    return usage();
}
