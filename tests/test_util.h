/* Minimal test helpers shared by the test executables. */
#ifndef CARLOBOMB_TEST_UTIL_H
#define CARLOBOMB_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

#include "rev.h"

static int t_checks, t_failures;

#define CHECK(cond) \
    do { \
        t_checks++; \
        if (!(cond)) { \
            t_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_MSG(cond, ...) \
    do { \
        t_checks++; \
        if (!(cond)) { \
            t_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

static void t_check_witness(const RevGraph *g, const char *target, size_t len, int budget);

static int t_report(const char *name) {
    (void)t_check_witness; /* not every test uses it */
    printf("%s: %d checks, %d failures\n", name, t_checks, t_failures);
    return t_failures ? 1 : 0;
}

/*
 * The five acceptance checks for a solver witness, spelled out here rather
 * than delegated to a single library call: node count within budget, binary
 * colors, in-range links, independent execution, exact prefix match.
 */
static void t_check_witness(const RevGraph *g, const char *target, size_t len, int budget) {
    char out[4096];
    int ok = 1;
    CHECK_MSG(g->node_count >= 1 && g->node_count <= budget, "target %.*s nodes %d budget %d",
              (int)len, target, g->node_count, budget);
    if (g->node_count < 1 || g->node_count > REV_MAX_NODES) return;
    for (int i = 0; i < g->node_count; i++) {
        if (g->nodes[i].color != 0 && g->nodes[i].color != 1) ok = 0;
        for (int k = 0; k < 2; k++)
            if (g->nodes[i].links[k] < 0 || g->nodes[i].links[k] >= g->node_count) ok = 0;
    }
    CHECK_MSG(ok, "target %.*s: witness has an invalid color or link", (int)len, target);
    if (!ok || len >= sizeof out) return;
    CHECK(rev_run(g, len, out) == REV_OK);
    CHECK_MSG(memcmp(out, target, len) == 0, "target %.*s got %s", (int)len, target, out);
}

#endif
