/*
 * The SharedSolver against the LevelSolver, string by string.
 *
 * What the SharedSolver reports for a wanted string has to be what the
 * LevelSolver reports for that string alone at that budget: found or not,
 * the same REV node for node, and the same number of logical steps.  Here
 * that is required of every string of every short length at every budget,
 * however the strings are cut into classes and whichever of them are
 * wanted, and of classes of long strings.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "level_solver.h"
#include "shared_solver.h"
#include "test_util.h"

static uint64_t rng_state = 20261115;
static uint64_t rnd(void) { /* splitmix64 */
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void text_of(uint32_t v, int bits, char *out) {
    for (int i = 0; i < bits; i++) out[i] = (char)('0' + ((v >> (bits - 1 - i)) & 1u));
    out[bits] = '\0';
}

/* What the LevelSolver says of one string, remembered: the same question is asked many times. */
typedef struct {
    uint8_t known, result, nodes;
    uint64_t steps;
    RevNode graph[REV_SHARED_MAX_NODES];
} Answer;

static uint64_t level_steps_total;

static void ask_level(uint32_t string, int bits, int budget, Answer *a) {
    char text[REV_SHARED_MAX_BITS + 1];
    RevLevelStats st;
    RevGraph g;
    RevSolveResult r;
    text_of(string, bits, text);
    memset(&st, 0, sizeof st);
    r = rev_level_solve_ex(text, (size_t)bits, budget, NULL, &g, &st);
    memset(a, 0, sizeof *a);
    a->known = 1;
    a->result = (uint8_t)r;
    a->steps = st.steps;
    level_steps_total += st.steps;
    if (r == REV_SOLVE_SOLVED) {
        a->nodes = (uint8_t)g.node_count;
        memcpy(a->graph, g.nodes, (size_t)g.node_count * sizeof g.nodes[0]);
    }
}

/* One pass, with everything it says held against the LevelSolver. */
typedef struct {
    int bits, budget;
    Answer *answers;     /* [string], or NULL: ask each time */
    uint8_t *seen;       /* a bit for each string of the class: reported found */
    uint32_t klass_first;
    uint64_t found, steps;
    int stop_after;      /* > 0: ask to stop when this many have been found */
} Pass;

static Answer *answer_for(Pass *p, uint32_t string, Answer *scratch) {
    Answer *a = p->answers ? &p->answers[string] : scratch;
    if (!a->known) ask_level(string, p->bits, p->budget, a);
    return a;
}

static int on_found(void *ctx, uint32_t string, const RevGraph *rev, uint64_t steps) {
    Pass *p = ctx;
    Answer scratch = {0}, *a = answer_for(p, string, &scratch);
    uint32_t j = string - p->klass_first;
    char text[REV_SHARED_MAX_BITS + 1];
    int same = rev->node_count == a->nodes;
    text_of(string, p->bits, text);
    CHECK_MSG(a->result == REV_SOLVE_SOLVED, "%s at %d nodes: found here, the LevelSolver says %s", text, p->budget,
              rev_solve_result_str((RevSolveResult)a->result));
    for (int i = 0; same && i < rev->node_count; i++)
        same = rev->nodes[i].color == a->graph[i].color && rev->nodes[i].links[0] == a->graph[i].links[0] &&
               rev->nodes[i].links[1] == a->graph[i].links[1];
    CHECK_MSG(same, "%s at %d nodes: another REV than the LevelSolver's", text, p->budget);
    CHECK_MSG(steps == a->steps, "%s at %d nodes: %llu steps here, %llu in the LevelSolver", text, p->budget,
              (unsigned long long)steps, (unsigned long long)a->steps);
    CHECK(rev_verify_prefix(rev, text, (size_t)p->bits, NULL) == REV_OK);
    CHECK_MSG(!((p->seen[j >> 3] >> (j & 7)) & 1), "%s reported twice", text);
    p->seen[j >> 3] |= (uint8_t)(1u << (j & 7));
    p->found++;
    p->steps += steps;
    return p->stop_after > 0 && p->found == (uint64_t)p->stop_after;
}

/*
 * A pass over `wanted` in class `klass`: every string wanted is found with
 * the LevelSolver's REV and steps, or left with the LevelSolver's steps for
 * "none".  Returns the number found; clears the found strings from `wanted`
 * if `consume`.
 */
static uint64_t pass_checked(RevShared *s, int bits, int class_bits, uint32_t klass, int budget, uint8_t *wanted,
                             Answer *answers, int consume) {
    uint32_t size = rev_shared_class_size(s), first = klass << (bits - class_bits);
    uint8_t *seen = calloc(size / 8 + 1, 1);
    RevSharedHooks hooks = {0};
    RevSharedStats st;
    Pass p = {bits, budget, answers, seen, first, 0, 0, 0};
    uint64_t asked = 0, none = 0, none_steps = 0;
    RevSharedResult r;
    hooks.found = on_found;
    hooks.found_ctx = &p;
    for (uint32_t j = 0; j < size; j++) asked += (uint64_t)((wanted[j >> 3] >> (j & 7)) & 1);
    r = rev_shared_pass(s, klass, budget, wanted, &hooks, &st);
    CHECK_MSG(r == REV_SHARED_DONE, "%d bits, class %u of %d bits, %d nodes: %s", bits, (unsigned)klass, class_bits, budget,
              rev_shared_result_str(r));
    if (r != REV_SHARED_DONE) {
        free(seen);
        return 0;
    }
    for (uint32_t j = 0; j < size; j++) {
        int was = (wanted[j >> 3] >> (j & 7)) & 1, got = (seen[j >> 3] >> (j & 7)) & 1;
        int left = rev_shared_unfound(s, j);
        CHECK(!got || was);                /* nothing found that was not wanted */
        CHECK(left == (was && !got));      /* what is left is what was wanted and not found */
        if (left) {
            Answer scratch = {0}, *a = answer_for(&p, first + j, &scratch);
            char text[REV_SHARED_MAX_BITS + 1];
            uint64_t steps = rev_shared_unfound_steps(s, j);
            text_of(first + j, bits, text);
            CHECK_MSG(a->result == REV_SOLVE_PROVEN_UNSAT, "%s at %d nodes: none here, the LevelSolver says %s", text, budget,
                      rev_solve_result_str((RevSolveResult)a->result));
            CHECK_MSG(steps == a->steps, "%s at %d nodes, none: %llu steps here, %llu in the LevelSolver", text, budget,
                      (unsigned long long)steps, (unsigned long long)a->steps);
            none++;
            none_steps += steps;
        } else {
            CHECK(rev_shared_unfound_steps(s, j) == 0);
        }
        if (got && consume) wanted[j >> 3] &= (uint8_t)~(1u << (j & 7));
    }
    CHECK(st.wanted == asked);
    CHECK(st.found == p.found);
    CHECK(p.found + none == asked);
    CHECK(st.string_steps == p.steps + none_steps);
    CHECK(st.steps >= 3 * st.choices);
    free(seen);
    return p.found;
}

/* Every string of `bits` bits, in classes of every size, budget after budget as a construction goes. */
static void whole_length(int bits, int max_budget) {
    uint32_t count = (uint32_t)1 << bits;
    /* The LevelSolver's answers do not depend on the cut: each is asked for once. */
    Answer *answers = calloc((size_t)count * (size_t)(max_budget + 1), sizeof *answers);
    CHECK(answers != NULL);
    if (!answers) return;
    for (int class_bits = 0; class_bits <= bits; class_bits++) {
        RevShared *s;
        uint32_t size, classes = (uint32_t)1 << class_bits;
        uint64_t found_all = 0;
        /* Each way of cutting costs a walk of the whole length: not every one for the longer lengths. */
        if (bits > 9 && class_bits != 0 && class_bits != 1 && class_bits != bits / 2 && class_bits != bits - 1 &&
            class_bits != bits)
            continue;
        if (bits > 11 && class_bits > bits / 2) continue;
        s = rev_shared_create(bits, class_bits);
        CHECK(s != NULL);
        if (!s) break;
        size = rev_shared_class_size(s);
        CHECK(size == count >> class_bits);
        for (uint32_t k = 0; k < classes; k++) {
            uint8_t *wanted = malloc(size / 8 + 1);
            memset(wanted, 0xFF, size / 8 + 1);
            for (int n = 1; n <= max_budget; n++)
                found_all += pass_checked(s, bits, class_bits, k, n, wanted, answers + (size_t)n * count, 1);
            free(wanted);
        }
        CHECK_MSG(found_all == count, "%d bits in classes of %d bits: %llu of %u strings found up to %d nodes", bits,
                  class_bits, (unsigned long long)found_all, (unsigned)count, max_budget);
        rev_shared_destroy(s);
    }
    free(answers);
}

/*
 * What a string is told does not depend on the company it is in: random
 * sets of wanted strings, among them strings that a smaller budget would
 * already have found and strings alone in their class.
 */
static void any_company(int bits, int budgets) {
    uint32_t count = (uint32_t)1 << bits;
    for (int n = 1; n <= budgets; n++) {
        Answer *answers = calloc(count, sizeof *answers);
        for (int round = 0; round < 6; round++) {
            int class_bits = (int)(rnd() % (uint64_t)(bits + 1));
            RevShared *s = rev_shared_create(bits, class_bits);
            uint32_t size = rev_shared_class_size(s), k = (uint32_t)(rnd() % ((uint64_t)1 << class_bits));
            uint8_t *wanted = calloc(size / 8 + 1, 1);
            int density = round % 3; /* half of them, one in sixteen, one */
            for (uint32_t j = 0; j < size; j++)
                if (density == 0 ? (rnd() & 1) : density == 1 ? (rnd() & 15) == 0 : 0) wanted[j >> 3] |= (uint8_t)(1u << (j & 7));
            if (density == 2) {
                uint32_t j = (uint32_t)(rnd() % size);
                wanted[j >> 3] |= (uint8_t)(1u << (j & 7));
            }
            pass_checked(s, bits, class_bits, k, n, wanted, answers, 0);
            free(wanted);
            rev_shared_destroy(s);
        }
        free(answers);
    }
}

/* Classes of long strings: a few strings with a long common beginning, at the budgets around their minimum. */
static void long_strings(int bits, int free_bits, int rounds, int max_budget) {
    int class_bits = bits - free_bits;
    RevShared *s = rev_shared_create(bits, class_bits);
    uint32_t size;
    CHECK(s != NULL);
    if (!s) return;
    size = rev_shared_class_size(s);
    for (int round = 0; round < rounds; round++) {
        uint32_t k = (uint32_t)(rnd() % ((uint64_t)1 << class_bits));
        uint8_t *wanted = malloc(size / 8 + 1);
        uint64_t found = 0;
        memset(wanted, 0xFF, size / 8 + 1);
        for (int n = max_budget - 2; n <= max_budget && found < size; n++)
            found += pass_checked(s, bits, class_bits, k, n, wanted, NULL, 1);
        free(wanted);
    }
    rev_shared_destroy(s);
}

/* A class of one string is the LevelSolver's own question: the steps differ by the choices and nothing else. */
static void alone(void) {
    for (int bits = 1; bits <= 14; bits++) {
        RevShared *s = rev_shared_create(bits, bits);
        CHECK(s != NULL && rev_shared_class_size(s) == 1);
        for (int trial = 0; trial < 12; trial++) {
            uint32_t v = (uint32_t)(rnd() % ((uint64_t)1 << bits));
            for (int n = 1; n <= 6; n++) {
                uint8_t wanted = 1;
                RevSharedStats st;
                Answer a;
                ask_level(v, bits, n, &a);
                CHECK(rev_shared_pass(s, v, n, &wanted, NULL, &st) == REV_SHARED_DONE);
                CHECK(st.wanted == 1 && st.found == (a.result == REV_SOLVE_SOLVED ? 1u : 0u));
                CHECK(st.string_steps == a.steps);
                if (a.result == REV_SOLVE_PROVEN_UNSAT) CHECK(st.steps == a.steps + 3 * st.choices);
                else CHECK(st.steps >= a.steps);
            }
        }
        rev_shared_destroy(s);
    }
}

static int poll_calls;
static int poll_stop_at;
static int on_poll(void *ctx) {
    (void)ctx;
    return ++poll_calls == poll_stop_at;
}

static void arguments_and_stops(void) {
    RevShared *s;
    uint8_t wanted[32];
    RevSharedHooks hooks = {0};
    RevSharedStats st, whole;
    uint8_t seen[32] = {0};
    Pass p = {8, 4, NULL, seen, 0, 0, 0, 3};
    memset(wanted, 0xFF, sizeof wanted);

    CHECK(rev_shared_create(0, 0) == NULL);
    CHECK(rev_shared_create(32, 8) == NULL);
    CHECK(rev_shared_create(8, 9) == NULL);
    CHECK(rev_shared_create(8, -1) == NULL);
    CHECK(rev_shared_create(31, 31 - REV_SHARED_MAX_FREE - 1) == NULL);
    CHECK(rev_shared_class_size(NULL) == 0);
    rev_shared_destroy(NULL);

    s = rev_shared_create(8, 0);
    CHECK(s != NULL);
    CHECK(rev_shared_pass(NULL, 0, 4, wanted, NULL, NULL) == REV_SHARED_INVALID);
    CHECK(rev_shared_pass(s, 0, 0, wanted, NULL, NULL) == REV_SHARED_INVALID);
    CHECK(rev_shared_pass(s, 0, REV_SHARED_MAX_NODES + 1, wanted, NULL, NULL) == REV_SHARED_INVALID);
    CHECK(rev_shared_pass(s, 1, 4, wanted, NULL, NULL) == REV_SHARED_INVALID); /* a class of no bits is class 0 */
    CHECK(rev_shared_pass(s, 0, 4, NULL, NULL, NULL) == REV_SHARED_INVALID);
    CHECK(!rev_shared_unfound(s, 0)); /* nothing is concluded from a pass that was not made */

    /* Nothing wanted: nothing done. */
    memset(wanted, 0, sizeof wanted);
    CHECK(rev_shared_pass(s, 0, 4, wanted, NULL, &st) == REV_SHARED_DONE);
    CHECK(st.wanted == 0 && st.found == 0 && st.steps == 0);
    memset(wanted, 0xFF, sizeof wanted);

    /* The whole pass, for comparison. */
    CHECK(rev_shared_pass(s, 0, 4, wanted, NULL, &whole) == REV_SHARED_DONE);
    CHECK(whole.wanted == 256 && whole.found > 3 && whole.found < 256);
    CHECK(whole.peak_stack > 0 && whole.peak_stack <= 6 * 8 + 16);

    /* Stopped by the one who is told of a string: no conclusions. */
    hooks.found = on_found;
    hooks.found_ctx = &p;
    CHECK(rev_shared_pass(s, 0, 4, wanted, &hooks, &st) == REV_SHARED_STOPPED);
    CHECK(p.found == 3);
    CHECK(!rev_shared_unfound(s, 255) && rev_shared_unfound_steps(s, 255) == 0);

    /* Stopped by the poll, at its first call and at a later one; and a poll that never stops changes nothing. */
    memset(&hooks, 0, sizeof hooks);
    hooks.poll = on_poll;
    hooks.poll_every = 100;
    poll_calls = 0;
    poll_stop_at = 1;
    CHECK(rev_shared_pass(s, 0, 4, wanted, &hooks, &st) == REV_SHARED_STOPPED);
    CHECK(poll_calls == 1);
    poll_calls = 0;
    poll_stop_at = 7;
    CHECK(rev_shared_pass(s, 0, 4, wanted, &hooks, &st) == REV_SHARED_STOPPED);
    CHECK(poll_calls == 7);
    poll_calls = 0;
    poll_stop_at = 0;
    CHECK(rev_shared_pass(s, 0, 4, wanted, &hooks, &st) == REV_SHARED_DONE);
    CHECK((uint64_t)poll_calls == whole.steps / 100);
    CHECK(st.steps == whole.steps && st.found == whole.found && st.string_steps == whole.string_steps &&
          st.choices == whole.choices);
    rev_shared_destroy(s);

    CHECK(strcmp(rev_shared_result_str(REV_SHARED_DONE), "done") == 0);
    CHECK(strcmp(rev_shared_result_str(REV_SHARED_INTERNAL), "internal error") == 0);
}

/* --deep: also every string of 13 bits, and classes of 28 and 31 bits (a quarter of a minute). */
int main(int argc, char **argv) {
    int deep = argc > 1 && strcmp(argv[1], "--deep") == 0;
    arguments_and_stops();
    alone();
    for (int bits = 1; bits <= 9; bits++) whole_length(bits, bits < 6 ? bits : 6);
    whole_length(10, 6);
    whole_length(11, 6);
    whole_length(12, 6);
    if (deep) whole_length(13, 7);
    any_company(10, 6);
    any_company(12, 6);
    any_company(14, 7);
    long_strings(16, 4, 4, 7);
    long_strings(20, 3, 3, 8);
    long_strings(24, 3, 2, 9);
    if (deep) long_strings(28, 3, 2, 10);
    if (deep) long_strings(31, 2, 1, 10);
    printf("LevelSolver steps compared with: %llu\n", (unsigned long long)level_steps_total);
    return t_report("test_shared_solver");
}
