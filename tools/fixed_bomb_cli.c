/*
 * carlobomb fixed: the Bomb-backed FixedSolver.
 *
 *   carlobomb fixed --bomb FILE --target S [--max-nodes N] [--out GRAPH] [--profile]
 *   carlobomb fixed --bomb FILE --stats
 *
 * Every answer comes out of the table in FILE; nothing is searched.
 *
 * Exit status: 0 a certified minimum-node REV was returned (or --stats ran);
 * 1 the node budget is below the certified minimum, or the Bomb is damaged;
 * 2 usage, invalid input or an unreadable file; 3 no answer from this Bomb:
 * the target is longer than it covers, or the Bomb is not complete.
 */
#include "fixed_bomb_cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"

enum { X_OK = 0, X_NO = 1, X_USAGE = 2, X_UNKNOWN = 3 };

void fixed_bomb_cli_usage(void) {
    fprintf(stderr,
            "  carlobomb fixed --bomb FILE --target S [--max-nodes N] [--out GRAPH] [--profile]\n"
            "      the certified minimum-node REV for S (1 to 16 bits), looked up in a complete\n"
            "      FixedRunBomb; --profile also lists the minimum of every prefix of S\n"
            "  carlobomb fixed --bomb FILE --stats\n"
            "      minimum-node counts of all strings of every length the Bomb covers\n");
}

static int usage_error(const char *message) {
    if (message) fprintf(stderr, "%s\n", message);
    fprintf(stderr, "usage:\n");
    fixed_bomb_cli_usage();
    return X_USAGE;
}

static const char *commas(uint64_t v) {
    static char bufs[8][32];
    static int next;
    char digits[24], *out = bufs[next];
    int n, k = 0;
    next = (next + 1) % 8;
    n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    for (int i = 0; i < n; i++) {
        out[k++] = digits[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[k++] = ',';
    }
    out[k] = '\0';
    return out;
}

static void print_graph(const RevGraph *g) {
    printf("Node   Color   Link0   Link1\n");
    for (int i = 0; i < g->node_count; i++)
        printf("%-6d %-7s %-7d %d\n", i, g->nodes[i].color == REV_WHITE ? "W (0)" : "B (1)",
               g->nodes[i].links[0], g->nodes[i].links[1]);
}

/* Open the Bomb or say exactly why not. */
static int open_bomb(const char *path, RevFixedBomb **bomb) {
    char target[FRB_MAX_BITS + 1];
    FrbFileInfo info;
    uint32_t bad = 0;
    FILE *probe = fopen(path, "rb");
    FrbError e;
    if (!probe) {
        fprintf(stderr, "cannot read %s\n", path);
        return X_USAGE;
    }
    fclose(probe);
    e = rev_fixed_bomb_open(path, bomb, &bad);
    if (e == FRB_OK) return X_OK;
    printf("Bomb: %s\n", path);
    if (e == FRB_ERR_INCOMPLETE) {
        frb_file_info(path, &info);
        frb_target_format(bad, info.bits, target);
        printf("Refused: this Bomb is not completely certified (%u of %u entries are certified minima;\n"
               "the first that is not: %s).  A minimum can only be read out of a complete Bomb.\n"
               "Finish its construction with `carlobomb bomb resume` and `carlobomb bomb finalize`.\n",
               (unsigned)info.minimal, (unsigned)info.count, target);
        return X_UNKNOWN;
    }
    if (e == FRB_ERR_EVIDENCE) {
        frb_file_info(path, &info);
        frb_target_format(bad, info.bits, target);
        printf("Refused: %s and its complement are stored with different node counts, which cannot\n"
               "both be minima.  This Bomb is wrong.\n",
               target);
    } else if (e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT) {
        printf("Refused: %s (entry %u).\n", frb_error_str(e), (unsigned)bad);
    } else {
        printf("Refused: %s.\n", frb_error_str(e));
    }
    return X_NO;
}

static int cmd_stats(const char *path, const RevFixedBomb *bomb) {
    char target[FRB_MAX_BITS + 1];
    uint64_t total[FRB_NODE_CAPACITY + 1] = {0}, strings = 0;
    int bits = rev_fixed_bomb_bits(bomb), top = rev_fixed_bomb_max_nodes(bomb);
    double weighted = 0;

    printf("Minimum node count of every binary string of 1 to %d bits (%s)\n\n", bits, path);
    printf("  Bits   Strings");
    for (int n = 1; n <= top; n++) printf(" %8d", n);
    printf("   Largest    Mean\n");
    for (int len = 1; len <= bits; len++) {
        uint32_t counts[FRB_NODE_CAPACITY + 1];
        double sum = 0;
        int largest = 0;
        rev_fixed_bomb_histogram(bomb, len, counts);
        printf("  %4d  %8s", len, commas((uint64_t)1 << len));
        for (int n = 1; n <= top; n++) {
            if (counts[n]) printf(" %8s", commas(counts[n]));
            else printf(" %8s", "");
            total[n] += counts[n];
            sum += (double)n * (double)counts[n];
            if (counts[n]) largest = n;
        }
        printf("   %7d   %5.3f\n", largest, sum / (double)((uint64_t)1 << len));
        weighted += sum;
        strings += (uint64_t)1 << len;
    }
    printf("   all  %8s", commas(strings));
    for (int n = 1; n <= top; n++) printf(" %8s", commas(total[n]));
    printf("   %7d   %5.3f\n", top, weighted / (double)strings);

    printf("\n  The strings that need the most nodes, by length\n");
    printf("  Bits   Nodes     Count   First in order\n");
    for (int len = 1; len <= bits; len++) {
        uint32_t counts[FRB_NODE_CAPACITY + 1], first = 0;
        int largest = 0;
        rev_fixed_bomb_histogram(bomb, len, counts);
        for (int n = 1; n <= FRB_NODE_CAPACITY; n++)
            if (counts[n]) largest = n;
        while (rev_fixed_bomb_min_nodes(bomb, len, first) != largest) first++;
        frb_target_format(first, len, target);
        printf("  %4d   %5d  %8s   %s\n", len, largest, commas(counts[largest]), target);
    }
    printf("\nEvery count is a certified minimum read out of the Bomb; nothing was searched.\n");
    return X_OK;
}

static void print_profile(const RevFixedBomb *bomb, const char *target, int len) {
    char prefix[FRB_MAX_BITS + 1], ext[FRB_MAX_BITS + 1];
    int bits = rev_fixed_bomb_bits(bomb);
    printf("\nMinimum of every prefix\n");
    printf("  Bits   Prefix             Nodes   %d-bit extension used\n", bits);
    for (int k = 1; k <= len; k++) {
        uint32_t value = 0;
        for (int i = 0; i < k; i++) value = (value << 1) | (uint32_t)(target[i] - '0');
        frb_target_format(value, k, prefix);
        frb_target_format(rev_fixed_bomb_extension(bomb, k, value), bits, ext);
        printf("  %4d   %-16s   %5d   %s\n", k, prefix, rev_fixed_bomb_min_nodes(bomb, k, value), ext);
    }
}

int fixed_bomb_cli(int argc, char **argv) {
    const char *path = NULL, *target = NULL, *out = NULL;
    char ext[FRB_MAX_BITS + 1];
    RevFixedBomb *bomb = NULL;
    RevFixedBombInfo info;
    RevFixedBombResult r;
    RevGraph g;
    long max_nodes = 0;
    int stats = 0, profile = 0, code, bits;
    size_t len;

    for (int i = 2; i < argc; i++) {
        const char *k = argv[i], *v;
        if (!strcmp(k, "--stats")) { stats = 1; continue; }
        if (!strcmp(k, "--profile")) { profile = 1; continue; }
        if (!strcmp(k, "help") || !strcmp(k, "--help") || !strcmp(k, "-h")) {
            fprintf(stderr, "usage:\n");
            fixed_bomb_cli_usage();
            return X_OK;
        }
        v = i + 1 < argc ? argv[++i] : NULL;
        if (!v) {
            fprintf(stderr, "missing value for %s\n", k);
            return usage_error(NULL);
        }
        if (!strcmp(k, "--bomb")) path = v;
        else if (!strcmp(k, "--target")) target = v;
        else if (!strcmp(k, "--out")) out = v;
        else if (!strcmp(k, "--max-nodes")) {
            char *end;
            errno = 0;
            max_nodes = strtol(v, &end, 10);
            if (errno || *v == '\0' || *end != '\0' || max_nodes < 1 || max_nodes > 1000000) {
                fprintf(stderr, "bad value for --max-nodes: %s\n", v);
                return X_USAGE;
            }
        } else {
            fprintf(stderr, "unknown option %s\n", k);
            return usage_error(NULL);
        }
    }
    if (!path) return usage_error("fixed needs --bomb");
    if (!stats && !target) return usage_error("fixed needs --target (or --stats)");
    if (target && rev_target_validate(target, strlen(target)) != REV_OK) {
        fprintf(stderr, "invalid target: must be a nonempty string of 0 and 1\n");
        return X_USAGE;
    }

    code = open_bomb(path, &bomb);
    if (code != X_OK) return code;
    bits = rev_fixed_bomb_bits(bomb);
    if (stats) {
        code = cmd_stats(path, bomb);
        rev_fixed_bomb_close(bomb);
        return code;
    }

    len = strlen(target);
    r = rev_fixed_bomb_solve(bomb, target, len, (int)max_nodes, &g, &info);
    printf("Bomb: %s (FixedRunBomb(%d), complete: %s certified minima)\n", path, bits,
           commas((uint64_t)1 << bits));
    printf("Target: %s (%zu bit%s)\n", target, len, len == 1 ? "" : "s");
    switch (r) {
    case REV_FIXED_BOMB_MINIMAL:
        frb_target_format(info.extension, bits, ext);
        printf("Result: MINIMAL (certified minimum node count; read from the Bomb, no search)\n");
        printf("Nodes: %d\n", g.node_count);
        if ((int)len < bits)
            printf("Extension: %s (entry %u): the first of the %d-bit strings starting with the\n"
                   "           target that need no more than %d node%s\n",
                   ext, (unsigned)info.extension, bits, g.node_count, g.node_count == 1 ? "" : "s");
        else printf("Extension: %s (entry %u): the target itself\n", ext, (unsigned)info.extension);
        printf("\n");
        print_graph(&g);
        /* Run it once more here, with the interpreter, as every command does. */
        if (rev_verify_prefix(&g, target, len, NULL) != REV_OK) {
            printf("\nVerification: FAIL\n");
            code = X_NO;
            break;
        }
        printf("\nVerification: PASS\n");
        if (profile) print_profile(bomb, target, (int)len);
        if (out) {
            FILE *f = fopen(out, "w");
            if (!f || rev_graph_write(f, &g) != REV_OK || fclose(f) != 0) {
                fprintf(stderr, "cannot write %s\n", out);
                code = X_USAGE;
                break;
            }
            printf("Graph written to %s\n", out);
        }
        code = X_OK;
        break;
    case REV_FIXED_BOMB_PROVEN_UNSAT:
        frb_target_format(info.extension, bits, ext);
        printf("Result: PROVEN_UNSAT: no REV with at most %ld node%s emits the target.\n", max_nodes,
               max_nodes == 1 ? "" : "s");
        printf("Its certified minimum is %d nodes (entry %u, %s).\n", info.min_nodes,
               (unsigned)info.extension, ext);
        if (profile) print_profile(bomb, target, (int)len);
        code = X_NO;
        break;
    case REV_FIXED_BOMB_UNSUPPORTED:
        printf("Result: UNSUPPORTED: this Bomb answers targets of 1 to %d bits.\n", bits);
        printf("Nothing is claimed about the minimum of a longer target.  What the Bomb does\n"
               "establish is a lower bound: no REV with fewer than %d node%s emits it.\n",
               info.lower_bound, info.lower_bound == 1 ? "" : "s");
        printf("For the exact answer, search: carlobomb solve --target %s\n", target);
        code = X_UNKNOWN;
        break;
    default:
        printf("Result: %s\n", rev_fixed_bomb_result_str(r));
        code = X_NO;
        break;
    }
    rev_fixed_bomb_close(bomb);
    return code;
}
