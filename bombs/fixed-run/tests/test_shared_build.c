/*
 * Tests of the construction by shared search: work-directory layout 4, the
 * pass record as the boundary at which results become evidence, recovery
 * from a process that dies anywhere, and passes made again.
 *
 * Usage: test_shared_build [scratch-directory]
 *
 * The reference throughout is the ladder that searches string by string:
 * the same table, byte for byte, and the same searches, record for record.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* usleep */
#endif

#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fixed_run.h"
#include "fixed_run_build.h"
#include "level_solver.h"
#include "test_util.h"

static char base_dir[1024];
static char last_err[FRB_ERR_TEXT];

static uint64_t rng_state = 0x0123456789ABCDEFull;
static uint64_t rnd64(void) { /* splitmix64 */
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t rnd(uint32_t n) { return (uint32_t)(rnd64() % n); }

/* dir/name.  The same path is the same string, and stays where it is: the
 * tests hold on to paths for as long as they run. */
static const char *sub(const char *dir, const char *name) {
    enum { PATHS = 4096, ROOM = 2048 };
    static char *known[PATHS];
    static int count;
    char joined[ROOM];
    size_t a = strlen(dir), b = strlen(name);
    if (a + b + 2 > sizeof joined) {
        fprintf(stderr, "path too long\n");
        exit(2);
    }
    memcpy(joined, dir, a);
    joined[a] = '/';
    memcpy(joined + a + 1, name, b + 1);
    for (int k = 0; k < count; k++)
        if (strcmp(known[k], joined) == 0) return known[k];
    if (count == PATHS) {
        fprintf(stderr, "too many paths\n");
        exit(2);
    }
    known[count] = malloc(a + b + 2);
    memcpy(known[count], joined, a + b + 2);
    return known[count++];
}

static const char *in_base(const char *name) { return sub(base_dir, name); }

static const char *level_file(const char *dir, int bits) {
    char name[32];
    snprintf(name, sizeof name, "fixed-run-%02d.frb", bits);
    return sub(dir, name);
}

static const char *level_work(const char *dir, int bits) {
    char name[32];
    snprintf(name, sizeof name, "work-%02d", bits);
    return sub(dir, name);
}

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *de;
        while (d && (de = readdir(d)) != NULL) {
            char child[2800];
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            snprintf(child, sizeof child, "%s/%s", path, de->d_name);
            remove_tree(child);
        }
        if (d) closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static uint8_t *slurp(const char *path, size_t *size) {
    struct stat st;
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    *size = 0;
    if (!f || stat(path, &st) != 0) {
        if (f) fclose(f);
        return NULL;
    }
    buf = malloc((size_t)st.st_size + 1);
    *size = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    return buf;
}

static void spill(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fwrite(data, 1, size, f) == size);
    fclose(f);
}

static int same_file(const char *a, const char *b) {
    size_t na, nb;
    uint8_t *da = slurp(a, &na), *db = slurp(b, &nb);
    int same = da && db && na == nb && na > 0 && memcmp(da, db, na) == 0;
    free(da);
    free(db);
    return same;
}

static FrbRunOptions quiet_options(void) {
    FrbRunOptions opt;
    frb_run_options_default(&opt);
    opt.progress_seconds = 0;
    return opt;
}

/* ---- what the tests build on --------------------------------------------- */

enum { FOOT = 8, TOP = 12 }; /* a direct 8-bit Bomb, and ladders on it up to 12 bits */

static const char *foot;    /* the 8-bit Bomb */
static const char *classic; /* the ladder that searches string by string: the reference */

static void ladder_settings(FrbConfig *settings, int workers, int shared, int class_bits) {
    frb_config_default(settings, 1);
    settings->workers = workers;
    settings->format = 0;
    settings->max_nodes = 0;
    settings->shared = shared;
    settings->class_bits = class_bits; /* below 0: what the level takes by itself */
}

static void build_reference(void) {
    const char *dir = in_base("direct");
    FrbRunOptions opt = quiet_options();
    FrbLadderResult lr;
    FrbRunResult res;
    FrbConfig cfg, settings;
    foot = in_base("foot-08.frb");
    classic = in_base("classic");
    remove_tree(dir);
    remove_tree(classic);
    frb_config_default(&cfg, FOOT);
    cfg.enumerate_max = 4;
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_work_run(dir, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete);
    CHECK(frb_work_finalize(dir, foot, 0, NULL, last_err, sizeof last_err) == FRB_OK);
    ladder_settings(&settings, 2, 0, 0);
    CHECK_MSG(frb_ladder_run(classic, foot, TOP, &settings, &opt, &lr, last_err, sizeof last_err) == FRB_OK &&
                  lr.complete,
              "%s", last_err);
}

/* Every search of a journal that counts, in the order of string and budget. */
typedef struct {
    uint32_t target;
    uint8_t budget, result, nodes;
    uint64_t steps;
    uint8_t graph[FRB_MAX_GRAPH_BYTES];
} Search;

typedef struct {
    Search *list;
    size_t n, cap;
    uint64_t passes, others;
} Searches;

static int collect_search(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Searches *s = ctx;
    (void)seg;
    if (!rec) return 0;
    if (rec->type == FRB_REC_PASS) s->passes++;
    if (rec->type != FRB_REC_SEARCH) return 0;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4096;
        s->list = realloc(s->list, s->cap * sizeof *s->list);
    }
    memset(&s->list[s->n], 0, sizeof s->list[0]);
    s->list[s->n].target = rec->target;
    s->list[s->n].budget = rec->budget;
    s->list[s->n].result = rec->result;
    s->list[s->n].nodes = rec->nodes;
    s->list[s->n].steps = rec->steps;
    memcpy(s->list[s->n].graph, rec->graph, sizeof rec->graph);
    s->n++;
    return 0;
}

static int cmp_search(const void *a, const void *b) {
    const Search *x = a, *y = b;
    if (x->target != y->target) return x->target < y->target ? -1 : 1;
    return (int)x->budget - (int)y->budget;
}

static Searches searches_of(const char *work) {
    Searches s;
    FrbConfig cfg;
    memset(&s, 0, sizeof s);
    CHECK_MSG(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK, "%s", last_err);
    CHECK(frb_journal_scan(work, &cfg, collect_search, &s, NULL, last_err, sizeof last_err) == FRB_OK);
    if (s.n) qsort(s.list, s.n, sizeof *s.list, cmp_search);
    return s;
}

/* The searches of two journals: the same strings at the same budgets with the
 * same results, REVs and logical steps. */
static void same_searches(const char *work_a, const char *work_b) {
    Searches a = searches_of(work_a), b = searches_of(work_b);
    size_t differ = 0;
    CHECK_MSG(a.n == b.n && a.n > 0, "%zu searches in %s, %zu in %s", a.n, work_a, b.n, work_b);
    for (size_t i = 0; i < a.n && i < b.n; i++)
        differ += (size_t)(memcmp(&a.list[i], &b.list[i], sizeof a.list[0]) != 0);
    CHECK_MSG(differ == 0, "%zu of %zu searches differ between %s and %s", differ, a.n, work_a, work_b);
    free(a.list);
    free(b.list);
}

/* The state of a work directory against that of the reference level: every
 * string certified, and the searches that count the same in number, result
 * and steps at every budget.  (After a recovery the journal holds more
 * records than count; the state is what they amount to.) */
static void same_state(const char *work, int bits) {
    FrbConfig cfg, ref_cfg;
    FrbState *s = NULL, *ref = NULL;
    CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_config_read(level_work(classic, bits), &ref_cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK_MSG(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_OK, "%s", last_err);
    CHECK(frb_state_load(level_work(classic, bits), &ref_cfg, &ref, last_err, sizeof last_err) == FRB_OK);
    if (s && ref) {
        uint64_t pass_wanted = 0, searches = 0;
        CHECK(s->by_status[FRB_MINIMAL] == s->count && s->conflicts == 0);
        for (int b = 1; b <= FRB_MAX_NODES; b++) {
            CHECK_MSG(s->budget[b].searches == ref->budget[b].searches && s->budget[b].solved == ref->budget[b].solved &&
                          s->budget[b].unsat == ref->budget[b].unsat && s->budget[b].steps == ref->budget[b].steps &&
                          s->budget[b].limited == 0,
                      "%d bits, budget %d: %llu searches and %llu steps here, %llu and %llu in the ladder", bits, b,
                      (unsigned long long)s->budget[b].searches, (unsigned long long)s->budget[b].steps,
                      (unsigned long long)ref->budget[b].searches, (unsigned long long)ref->budget[b].steps);
            CHECK(s->pass[b].wanted == s->budget[b].searches && s->pass[b].found == s->budget[b].solved);
            pass_wanted += s->pass[b].wanted;
            searches += s->budget[b].searches;
        }
        CHECK(pass_wanted == searches && searches > 0);
        for (uint32_t i = 0; i < s->count; i++)
            if (s->targets[i].nodes != ref->targets[i].nodes || s->targets[i].lower_bound != ref->targets[i].lower_bound) {
                CHECK_MSG(0, "%d bits, string %u: %d nodes here, %d in the ladder", bits, (unsigned)i,
                          s->targets[i].nodes, ref->targets[i].nodes);
                break;
            }
    }
    frb_state_free(s);
    frb_state_free(ref);
}

/* ---- the settings of a directory ------------------------------------------ */

static void test_config(void) {
    const char *dir = in_base("config");
    char path[1400];
    size_t size;
    uint8_t *text;
    FrbConfig cfg, back;

    CHECK(frb_default_class_bits(31) == 8 && frb_default_class_bits(16) == 8 && frb_default_class_bits(12) == 4 &&
          frb_default_class_bits(8) == 0 && frb_default_class_bits(1) == 0);

    remove_tree(dir);
    frb_config_default(&cfg, 12);
    CHECK(cfg.shared == 0 && cfg.class_bits == 0);
    cfg.shared = 1;
    cfg.class_bits = 5;
    cfg.enumerate_max = 3;
    strcpy(cfg.engine, FRB_SHARED_ENGINE);
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    snprintf(path, sizeof path, "%s/config.txt", dir);
    text = slurp(path, &size);
    CHECK(text != NULL);
    if (text) {
        text[size] = '\0';
        CHECK(strncmp((char *)text, "carlobomb-fixed-run-work 4\n", 27) == 0);
        CHECK(strstr((char *)text, "\nsearch shared\n") != NULL && strstr((char *)text, "\nclass_bits 5\n") != NULL);
        CHECK(strstr((char *)text, "\nformat 1\n") != NULL && strstr((char *)text, "\nbase_bits 0\n") != NULL);
    }
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_OK);
    CHECK(back.shared == 1 && back.class_bits == 5 && back.bits == 12 && back.enumerate_max == 3 &&
          strcmp(back.engine, FRB_SHARED_ENGINE) == 0);

    /* A layout-4 file says both things, and only what a layout-4 file may say. */
    if (text) {
        char *line = strstr((char *)text, "class_bits 5\n");
        size_t at = (size_t)(line - (char *)text);
        uint8_t *cut = malloc(size);
        memcpy(cut, text, at);
        memcpy(cut + at, text + at + 13, size - at - 13);
        spill(path, cut, size - 13);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        memcpy(cut, text, size);
        memcpy(strstr((char *)cut, "search shared"), "search string", 13);
        spill(path, cut, size);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        memcpy(cut, text, size);
        cut[25] = '2'; /* the same lines under the name of layout 2 */
        spill(path, cut, size);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        cut[25] = '3'; /* a layout this program does not know */
        spill(path, cut, size);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_ERR_INCOMPATIBLE);
        spill(path, text, size);
        CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_OK);
        free(cut);
    }
    free(text);

    /* What a shared search cannot be asked for. */
    cfg.class_bits = 13;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.class_bits = -1;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.class_bits = 5;
    cfg.max_steps[0] = 1000;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.max_steps[0] = 0;
    cfg.passes = 2;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.passes = 1;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_OK);
    cfg.shared = 0; /* classes without a shared search */
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    frb_config_default(&cfg, 31);
    cfg.shared = 1;
    cfg.class_bits = 4; /* classes of 2^27 strings */
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);
    cfg.class_bits = 5;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_OK);
    cfg.class_bits = FRB_MAX_CLASS_BITS + 1;
    CHECK(frb_config_check(&cfg, last_err, sizeof last_err) == FRB_ERR_ARG);

    /* A directory of the other kind keeps its layout and its meaning. */
    remove_tree(dir);
    frb_config_default(&cfg, 12);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    text = slurp(path, &size);
    CHECK(text && strncmp((char *)text, "carlobomb-fixed-run-work 1\n", 27) == 0);
    free(text);
    CHECK(frb_config_read(dir, &back, last_err, sizeof last_err) == FRB_OK && back.shared == 0);
}

/* ---- the same table and the same searches --------------------------------- */

static void shared_ladder(const char *dir, int workers, int class_bits) {
    FrbRunOptions opt = quiet_options();
    FrbLadderResult lr;
    FrbConfig settings;
    remove_tree(dir);
    ladder_settings(&settings, workers, 1, class_bits);
    CHECK_MSG(frb_ladder_run(dir, foot, TOP, &settings, &opt, &lr, last_err, sizeof last_err) == FRB_OK && lr.complete,
              "%d workers, classes of %d bits: %s", workers, class_bits, last_err);
}

static void test_same_as_ladder(void) {
    static const struct { int workers, class_bits; } how[] = {{1, 3}, {3, -1}, {2, 0}, {3, 6}, {1, 9}};
    for (size_t k = 0; k < sizeof how / sizeof how[0]; k++) {
        const char *dir = in_base("shared");
        shared_ladder(dir, how[k].workers, how[k].class_bits);
        for (int bits = FOOT + 1; bits <= TOP; bits++) {
            const char *work = level_work(dir, bits);
            FrbReplay replay;
            FrbConfig cfg;
            FrbState *s = NULL;
            FrbTable t;
            FrbFileInfo info;
            Searches all;
            int want_bits = how[k].class_bits < 0 ? frb_default_class_bits(bits)
                                                  : how[k].class_bits > bits ? frb_default_class_bits(bits) : how[k].class_bits;
            CHECK_MSG(same_file(level_file(dir, bits), level_file(classic, bits)),
                      "%d bits, %d workers, classes of %d bits: not the ladder's table", bits, how[k].workers,
                      how[k].class_bits);
            same_searches(work, level_work(classic, bits));
            same_state(work, bits);

            CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
            CHECK(cfg.shared == 1 && cfg.class_bits == want_bits && strcmp(cfg.engine, FRB_SHARED_ENGINE) == 0);
            all = searches_of(work);
            CHECK(all.passes > 0);
            /* The table is certified by this journal as by any other. */
            CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_OK);
            CHECK(frb_file_read(level_file(dir, bits), &t, &info, NULL) == FRB_OK);
            if (s) {
                uint64_t passes = 0;
                CHECK(frb_verify_evidence(&t, s, NULL, last_err, sizeof last_err) == FRB_OK);
                CHECK(s->uncommitted == 0 && s->passes_repeated == 0 && s->shared == 1);
                for (int b = 1; b <= FRB_MAX_NODES; b++) passes += s->pass[b].passes;
                CHECK(passes == all.passes);
            }
            frb_table_destroy(&t);
            frb_state_free(s);

            /* Every pass made again, both ways: every record of every pass. */
            CHECK_MSG(frb_replay_passes(work, &cfg, FRB_REPLAY_SHARED, NULL, 0, 0, &replay, last_err, sizeof last_err) ==
                          FRB_OK,
                      "%s", last_err);
            CHECK(replay.passes == all.passes && replay.passes_admitted == all.passes && replay.records == all.n &&
                  replay.differ == 0);
            if (bits <= FOOT + 2 || k == 0) {
                CHECK_MSG(frb_replay_passes(work, &cfg, FRB_REPLAY_LEVEL, rev_level_solve_ex, 0, 0, &replay, last_err,
                                            sizeof last_err) == FRB_OK,
                          "%s", last_err);
                CHECK(replay.passes == all.passes && replay.records == all.n && replay.differ == 0);
            }
            /* A sample of the passes. */
            CHECK(frb_replay_passes(work, &cfg, FRB_REPLAY_LEVEL, rev_level_solve_ex, 2, 7, &replay, last_err,
                                    sizeof last_err) == FRB_OK);
            CHECK(replay.passes == (all.passes < 2 ? all.passes : 2) && replay.passes_admitted == all.passes &&
                  replay.differ == 0 && replay.records > 0 && replay.records <= all.n);
            free(all.list);
        }
    }
    /* A ladder that searches string by string has no passes to make again. */
    {
        FrbConfig cfg;
        FrbReplay replay;
        CHECK(frb_config_read(level_work(classic, TOP), &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_replay_passes(level_work(classic, TOP), &cfg, FRB_REPLAY_SHARED, NULL, 0, 0, &replay, last_err,
                                sizeof last_err) == FRB_ERR_ARG);
    }
}

/* Without a base Bomb: enumeration first, then the shared search from where it ends. */
static void test_direct(void) {
    for (int enumerate_max = 0; enumerate_max <= 3; enumerate_max += 3) {
        const char *a = in_base("direct-a"), *b = in_base("direct-b");
        const char *fa = in_base("direct-a.frb"), *fb = in_base("direct-b.frb");
        FrbRunOptions opt = quiet_options();
        FrbRunResult res;
        FrbReplay replay;
        FrbConfig cfg;
        remove_tree(a);
        remove_tree(b);
        frb_config_default(&cfg, 10);
        cfg.enumerate_max = enumerate_max;
        cfg.workers = 2;
        CHECK(frb_work_create(a, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_run(a, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete);
        CHECK(frb_work_finalize(a, fa, 0, NULL, last_err, sizeof last_err) == FRB_OK);
        cfg.shared = 1;
        cfg.class_bits = 3;
        strcpy(cfg.engine, FRB_SHARED_ENGINE);
        CHECK(frb_work_create(b, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK_MSG(frb_work_run(b, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete, "%s", last_err);
        CHECK(frb_work_finalize(b, fb, 0, NULL, last_err, sizeof last_err) == FRB_OK);
        CHECK_MSG(same_file(fa, fb), "10 bits, enumeration to %d nodes: not the same table", enumerate_max);
        same_searches(a, b);
        CHECK(frb_replay_passes(b, &cfg, FRB_REPLAY_LEVEL, rev_level_solve_ex, 0, 0, &replay, last_err, sizeof last_err) ==
              FRB_OK);
        CHECK(replay.differ == 0 && replay.records > 0);
    }
}

/* ---- what is admitted as evidence ------------------------------------------ */

/* The records of a finished level, to make other journals of. */
typedef struct {
    FrbRecord *rec;
    size_t n, cap;
} Tape;

static int tape_record(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Tape *t = ctx;
    (void)seg;
    if (!rec) return 0;
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 4096;
        t->rec = realloc(t->rec, t->cap * sizeof *t->rec);
    }
    t->rec[t->n++] = *rec;
    return 0;
}

/* A work directory with the settings of `from` and the given records in one segment. */
static void write_journal(const char *dir, const char *from, const FrbRecord *rec, size_t n) {
    FrbSegment *seg = NULL;
    FrbConfig cfg;
    remove_tree(dir);
    CHECK(frb_config_read(from, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_work_create(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_segment_create(dir, &cfg, 1, 1, 0, &seg) == FRB_OK);
    for (size_t i = 0; i < n; i++) CHECK(frb_segment_append(seg, &rec[i]) == FRB_OK);
    CHECK(frb_segment_close(seg) == FRB_OK);
}

static FrbError load(const char *dir, FrbState **state) {
    FrbConfig cfg;
    *state = NULL;
    CHECK(frb_config_read(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    last_err[0] = '\0';
    return frb_state_load(dir, &cfg, state, last_err, sizeof last_err);
}

/* What `bomb status --target` lists of a string: how many of its journal
 * records it marks as counting for nothing. */
static int listed_as_nothing(const char *dir, const FrbState *s, uint32_t index) {
    char line[512];
    FrbConfig cfg;
    int marked = 0;
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (!f) return -1;
    CHECK(frb_config_read(dir, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_report_target(f, dir, &cfg, s, index, last_err, sizeof last_err) == FRB_OK);
    rewind(f);
    while (fgets(line, sizeof line, f))
        if (strstr(line, "counts for nothing")) marked++;
    fclose(f);
    return marked;
}

/* The pass record at place `at` of a tape made right again for the search
 * records in front of it: their number, how many found a REV, their steps
 * and their checksum. */
static void repair_pass(Tape *t, size_t at, const FrbFormat *format) {
    FrbPass p;
    size_t first = at;
    CHECK(frb_pass_unpack(&t->rec[at], &p));
    while (first > 0 && t->rec[first - 1].type == FRB_REC_SEARCH) first--;
    p.wanted = (uint32_t)(at - first);
    p.found = 0;
    p.string_steps = 0;
    p.checksum = 0;
    for (size_t i = first; i < at; i++) {
        p.found += t->rec[i].result == REV_SOLVE_SOLVED;
        p.string_steps += t->rec[i].steps;
        p.checksum = frb_pass_checksum(p.checksum, &t->rec[i], format);
    }
    {
        uint32_t time = t->rec[at].time;
        frb_pass_pack(&p, &t->rec[at]);
        t->rec[at].time = time;
    }
}

static Tape tape_copy(const Tape *t) {
    Tape c = *t;
    c.rec = malloc((t->n + 64) * sizeof *c.rec);
    memcpy(c.rec, t->rec, t->n * sizeof *c.rec);
    c.cap = t->n + 64;
    return c;
}

static void tape_remove(Tape *t, size_t at, size_t count) {
    memmove(&t->rec[at], &t->rec[at + count], (t->n - at - count) * sizeof *t->rec);
    t->n -= count;
}

static void tape_insert(Tape *t, size_t at, const FrbRecord *rec, size_t count) {
    FrbRecord *copy = malloc(count * sizeof *copy); /* rec may be records of the tape itself */
    memcpy(copy, rec, count * sizeof *copy);
    t->rec = realloc(t->rec, (t->n + count + 64) * sizeof *t->rec);
    memmove(&t->rec[at + count], &t->rec[at], (t->n - at) * sizeof *t->rec);
    memcpy(&t->rec[at], copy, count * sizeof *t->rec);
    t->n += count;
    free(copy);
}

static void test_admission(void) {
    const int bits = FOOT + 2;
    const char *dir = in_base("admit-src"), *work, *made = in_base("admit");
    const char *out = in_base("admit.frb");
    const FrbFormat *format = frb_format_for_bits(bits);
    FrbRunOptions opt = quiet_options();
    FrbRunResult res;
    FrbState *s;
    FrbConfig cfg;
    Tape tape, t;
    FrbPass p;
    size_t passes = 0, pass_at[64], pass_first[64];
    size_t big = 0; /* a pass with several records, some with a REV and some without */
    uint32_t certified_all;

    shared_ladder(dir, 1, 3); /* one worker: one segment holds the level */
    work = level_work(dir, bits);
    memset(&tape, 0, sizeof tape);
    CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_journal_scan(work, &cfg, tape_record, &tape, NULL, last_err, sizeof last_err) == FRB_OK);
    for (size_t i = 0; i < tape.n && passes < 64; i++)
        if (tape.rec[i].type == FRB_REC_PASS) {
            size_t first = i;
            while (first > 0 && tape.rec[first - 1].type == FRB_REC_SEARCH) first--;
            pass_at[passes] = i;
            pass_first[passes] = first;
            CHECK(frb_pass_unpack(&tape.rec[i], &p) && p.wanted == i - first && p.class_bits == 3);
            if (p.found >= 2 && p.wanted - p.found >= 2 && !big) big = passes;
            passes++;
        }
    CHECK(passes >= 8 && big > 0);
    CHECK(frb_pass_unpack(&tape.rec[pass_at[big]], &p));

    /* As it was written: complete. */
    write_journal(made, work, tape.rec, tape.n);
    CHECK(load(made, &s) == FRB_OK);
    certified_all = s ? s->by_status[FRB_MINIMAL] : 0;
    CHECK(s && certified_all == s->count && s->uncommitted == 0);
    if (s) CHECK(listed_as_nothing(made, s, tape.rec[pass_first[passes - 1]].target) == 0);
    frb_state_free(s);

    /* The last pass record gone: its search records are there and count for
     * nothing.  The level is not complete, and going on makes that pass again. */
    t = tape_copy(&tape);
    {
        size_t last = pass_at[passes - 1], batch = last - pass_first[passes - 1];
        tape_remove(&t, last, t.n - last); /* and what came after it */
        write_journal(made, work, t.rec, t.n);
        CHECK(load(made, &s) == FRB_OK);
        CHECK(s && s->uncommitted == batch && s->by_status[FRB_MINIMAL] < certified_all && s->conflicts == 0);
        /* and the report of a string says so of each of them */
        if (s)
            for (size_t i = pass_first[passes - 1]; i < last; i++)
                CHECK(listed_as_nothing(made, s, t.rec[i].target) == 1);
        frb_state_free(s);
        CHECK(frb_work_finalize(made, out, 0, NULL, last_err, sizeof last_err) == FRB_ERR_INCOMPLETE);
        CHECK(frb_config_read(made, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_run(made, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK && res.complete);
        CHECK(frb_work_finalize(made, out, 0, NULL, last_err, sizeof last_err) == FRB_OK);
        CHECK(same_file(out, level_file(classic, bits)));
        same_state(made, bits);
    }
    free(t.rec);

    /* Every pass record gone: a journal of searches that no pass vouches for. */
    t = tape_copy(&tape);
    for (size_t i = t.n; i-- > 0;)
        if (t.rec[i].type == FRB_REC_PASS) tape_remove(&t, i, 1);
    write_journal(made, work, t.rec, t.n);
    CHECK(load(made, &s) == FRB_OK);
    if (s) {
        uint64_t counted = 0;
        for (int b = 1; b <= FRB_MAX_NODES; b++) counted += s->budget[b].searches + s->pass[b].passes;
        CHECK(counted == 0 && s->uncommitted > 0 && s->by_status[FRB_MINIMAL] == s->base_settled);
    }
    frb_state_free(s);
    free(t.rec);

#define REFUSED(what) \
    do { \
        write_journal(made, work, t.rec, t.n); \
        CHECK_MSG(load(made, &s) == FRB_ERR_JOURNAL, "%s: %s", what, last_err[0] ? last_err : "admitted"); \
        frb_state_free(s); \
        free(t.rec); \
    } while (0)

    /* A search record of a pass missing. */
    t = tape_copy(&tape);
    tape_remove(&t, pass_first[big] + 1, 1);
    REFUSED("a search record missing");
    /* One too many: the first of the next pass moved in front of this pass record. */
    t = tape_copy(&tape);
    {
        FrbRecord moved = t.rec[pass_at[big] + 1];
        CHECK(moved.type == FRB_REC_SEARCH);
        tape_remove(&t, pass_at[big] + 1, 1);
        tape_insert(&t, pass_at[big], &moved, 1);
    }
    REFUSED("a search record of another pass");
    /* The steps of a search changed. */
    t = tape_copy(&tape);
    t.rec[pass_first[big]].steps += 1;
    REFUSED("steps changed");
    /* Its result changed, the sums left alone: a record that says "no REV" of a string that has one. */
    t = tape_copy(&tape);
    for (size_t i = pass_first[big]; i < pass_at[big]; i++)
        if (t.rec[i].result == REV_SOLVE_SOLVED) {
            t.rec[i].result = REV_SOLVE_PROVEN_UNSAT;
            t.rec[i].nodes = 0;
            memset(t.rec[i].graph, 0, sizeof t.rec[i].graph);
            break;
        }
    REFUSED("a result changed");
    /* Two records in the other order, the pass record made to fit. */
    t = tape_copy(&tape);
    {
        FrbRecord swap = t.rec[pass_first[big]];
        t.rec[pass_first[big]] = t.rec[pass_first[big] + 1];
        t.rec[pass_first[big] + 1] = swap;
        repair_pass(&t, pass_at[big], format);
    }
    REFUSED("out of order");
    /* The same string twice. */
    t = tape_copy(&tape);
    tape_insert(&t, pass_first[big], &t.rec[pass_first[big]], 1);
    repair_pass(&t, pass_at[big] + 1, format);
    REFUSED("a string twice");
    /* A string left out, the pass record made to fit: the pass did not search all that was open. */
    t = tape_copy(&tape);
    tape_remove(&t, pass_first[big] + 1, 1);
    repair_pass(&t, pass_at[big] - 1, format);
    REFUSED("a string left out");
    /* A string of another class among them. */
    t = tape_copy(&tape);
    t.rec[pass_at[big] - 1].target ^= (uint32_t)1 << (bits - 2);
    repair_pass(&t, pass_at[big], format);
    REFUSED("another class");
    /* A search at another budget among them. */
    t = tape_copy(&tape);
    t.rec[pass_first[big]].budget += 1;
    repair_pass(&t, pass_at[big], format);
    REFUSED("another budget");
    /* A search that was stopped by a limit. */
    t = tape_copy(&tape);
    for (size_t i = pass_first[big]; i < pass_at[big]; i++)
        if (t.rec[i].result == REV_SOLVE_PROVEN_UNSAT) {
            t.rec[i].result = REV_SOLVE_RESOURCE_LIMIT;
            t.rec[i].limit = REV_LIMIT_STEPS;
            t.rec[i].max_steps = 5;
            break;
        }
    repair_pass(&t, pass_at[big], format);
    REFUSED("a limit");
    /* The pass record of another cut of the strings. */
    t = tape_copy(&tape);
    t.rec[pass_at[big]].limit = 4;
    REFUSED("other class bits");
    /* Of a class of strings that start with 1. */
    t = tape_copy(&tape);
    t.rec[pass_at[big]].target |= 4;
    REFUSED("a class the symmetry leaves out");
    /* A pass record with nothing in front of it. */
    t = tape_copy(&tape);
    {
        FrbRecord lone = t.rec[pass_at[big]];
        tape_insert(&t, pass_at[big] + 1, &lone, 1);
    }
    REFUSED("an empty pass");
    /* A session record in the middle of a pass: what stood before it is not of the pass. */
    t = tape_copy(&tape);
    {
        FrbRecord note;
        memset(&note, 0, sizeof note);
        note.type = FRB_REC_SESSION_END;
        tape_insert(&t, pass_first[big] + 1, &note, 1);
    }
    REFUSED("a pass interrupted by another record");
    /*
     * Passes out of their place.  The passes of a class are of rising
     * budgets, each on what the one before left open: with the first of two
     * gone, the second searched strings that are still open at a smaller
     * budget.
     */
    {
        size_t early = passes, late = passes;
        for (size_t a = 0; a < passes && late == passes; a++)
            for (size_t b = a + 1; b < passes; b++) {
                FrbPass pa, pb;
                frb_pass_unpack(&tape.rec[pass_at[a]], &pa);
                frb_pass_unpack(&tape.rec[pass_at[b]], &pb);
                if (pa.klass == pb.klass && pb.budget == pa.budget + 1 && pa.found < pa.wanted) {
                    early = a;
                    late = b;
                    break;
                }
            }
        CHECK(late < passes);
        if (late < passes) {
            t = tape_copy(&tape);
            tape_remove(&t, pass_first[early], pass_at[early] + 1 - pass_first[early]);
            REFUSED("a pass above a budget still open");
            /* The two exchanged: the later one comes first and is refused for the same reason. */
            t = tape_copy(&tape);
            {
                size_t n_early = pass_at[early] + 1 - pass_first[early], n_late = pass_at[late] + 1 - pass_first[late];
                FrbRecord *e = malloc(n_early * sizeof *e), *l = malloc(n_late * sizeof *l);
                memcpy(e, &t.rec[pass_first[early]], n_early * sizeof *e);
                memcpy(l, &t.rec[pass_first[late]], n_late * sizeof *l);
                tape_remove(&t, pass_first[late], n_late);
                tape_remove(&t, pass_first[early], n_early);
                tape_insert(&t, pass_first[early], l, n_late);
                tape_insert(&t, pass_first[early] + n_late, e, n_early);
                free(e);
                free(l);
            }
            REFUSED("passes exchanged");
        }
    }
    /* A string that was not open (the REV of the base settled it) searched all the same. */
    t = tape_copy(&tape);
    {
        FrbRecord extra = t.rec[pass_first[big]];
        FrbConfig c;
        int found = 0;
        CHECK(frb_config_read(work, &c, last_err, sizeof last_err) == FRB_OK);
        CHECK(load(work, &s) == FRB_OK);
        for (uint32_t i = (p.klass << (bits - 3)); s && i < ((p.klass + 1) << (bits - 3)) && !found; i++)
            if (s->targets[i].source == FRB_SRC_BASE && i < t.rec[pass_first[big]].target) {
                extra.target = i;
                extra.result = REV_SOLVE_PROVEN_UNSAT;
                extra.nodes = 0;
                memset(extra.graph, 0, sizeof extra.graph);
                found = 1;
            }
        frb_state_free(s);
        if (found) {
            tape_insert(&t, pass_first[big], &extra, 1);
            repair_pass(&t, pass_at[big] + 1, format);
            REFUSED("a string that was not open");
        } else {
            free(t.rec);
        }
    }
#undef REFUSED

    /* The same pass twice, saying the same: admitted once. */
    t = tape_copy(&tape);
    {
        size_t n = pass_at[big] + 1 - pass_first[big];
        FrbRecord *again = malloc(n * sizeof *again);
        memcpy(again, &t.rec[pass_first[big]], n * sizeof *again);
        tape_insert(&t, pass_at[passes - 1] + 1, again, n);
        free(again);
        write_journal(made, work, t.rec, t.n);
        CHECK_MSG(load(made, &s) == FRB_OK, "%s", last_err);
        CHECK(s && s->passes_repeated == 1 && s->by_status[FRB_MINIMAL] == s->count);
        frb_state_free(s);
        same_state(made, bits);
        CHECK(frb_work_finalize(made, out, 0, NULL, last_err, sizeof last_err) == FRB_OK);
        CHECK(same_file(out, level_file(classic, bits)));
    }
    free(t.rec);
    /* Twice, saying something else the second time: a contradiction. */
    t = tape_copy(&tape);
    {
        size_t n = pass_at[big] + 1 - pass_first[big], at = pass_at[passes - 1] + 1;
        FrbRecord *again = malloc(n * sizeof *again);
        memcpy(again, &t.rec[pass_first[big]], n * sizeof *again);
        again[0].steps += 2;
        tape_insert(&t, at, again, n);
        repair_pass(&t, at + n - 1, format);
        free(again);
        write_journal(made, work, t.rec, t.n);
        CHECK(load(made, &s) == FRB_ERR_EVIDENCE);
        frb_state_free(s);
    }
    free(t.rec);

    /*
     * What the reader cannot see: a pass that is whole and in its place and
     * says something false, the steps of one search changed and the pass
     * record made to fit.  It is admitted.  Making the pass again finds it,
     * with the LevelSolver and with the SharedSolver.
     */
    t = tape_copy(&tape);
    {
        FrbReplay replay;
        t.rec[pass_first[big] + 1].steps += 1;
        repair_pass(&t, pass_at[big], format);
        write_journal(made, work, t.rec, t.n);
        CHECK(load(made, &s) == FRB_OK);
        frb_state_free(s);
        CHECK(frb_config_read(made, &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_replay_passes(made, &cfg, FRB_REPLAY_LEVEL, rev_level_solve_ex, 0, 0, &replay, last_err,
                                sizeof last_err) == FRB_ERR_EVIDENCE);
        CHECK(replay.differ == 1 && strstr(last_err, "another number of steps") != NULL);
        CHECK(frb_replay_passes(made, &cfg, FRB_REPLAY_SHARED, NULL, 0, 0, &replay, last_err, sizeof last_err) ==
              FRB_ERR_EVIDENCE);
        CHECK(replay.differ == 1);
    }
    free(t.rec);
    /* The same with a REV that is a REV of the string, and not the first the search finds. */
    t = tape_copy(&tape);
    {
        FrbReplay replay;
        int changed = 0;
        for (size_t i = 0; i < t.n && !changed; i++) {
            RevGraph g;
            if (t.rec[i].type != FRB_REC_SEARCH || t.rec[i].result != REV_SOLVE_SOLVED) continue;
            if (frb_graph_unpack_as(format, t.rec[i].graph, t.rec[i].nodes, &g) != FRB_OK) continue;
            for (int n = 0; n < g.node_count && !changed; n++)
                for (int l = 0; l < 2 && !changed; l++) {
                    RevGraph h = g;
                    h.nodes[n].links[l] = (h.nodes[n].links[l] + 1) % h.node_count;
                    if (frb_graph_output(&h, bits) == t.rec[i].target) {
                        size_t at = i;
                        CHECK(frb_graph_pack_as(format, &h, t.rec[i].graph) == FRB_OK);
                        while (t.rec[at].type != FRB_REC_PASS) at++;
                        repair_pass(&t, at, format);
                        changed = 1;
                    }
                }
        }
        CHECK(changed);
        write_journal(made, work, t.rec, t.n);
        CHECK(load(made, &s) == FRB_OK);
        frb_state_free(s);
        CHECK(frb_replay_passes(made, &cfg, FRB_REPLAY_LEVEL, rev_level_solve_ex, 0, 0, &replay, last_err,
                                sizeof last_err) == FRB_ERR_EVIDENCE);
        CHECK(replay.differ == 1 && strstr(last_err, "another REV") != NULL);
        CHECK(frb_replay_passes(made, &cfg, FRB_REPLAY_SHARED, NULL, 0, 0, &replay, last_err, sizeof last_err) ==
              FRB_ERR_EVIDENCE);
    }
    free(t.rec);

    /* A pass record where strings are searched one by one is not a record of that journal. */
    {
        Tape c;
        memset(&c, 0, sizeof c);
        CHECK(frb_config_read(level_work(classic, bits), &cfg, last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_journal_scan(level_work(classic, bits), &cfg, tape_record, &c, NULL, last_err, sizeof last_err) ==
              FRB_OK);
        tape_insert(&c, c.n, &tape.rec[pass_at[big]], 1);
        write_journal(made, level_work(classic, bits), c.rec, c.n);
        CHECK(load(made, &s) == FRB_ERR_JOURNAL);
        frb_state_free(s);
        free(c.rec);
    }
    free(tape.rec);
}

/* ---- a process that dies ---------------------------------------------------- */

static void copy_file(const char *from, const char *to, size_t keep) {
    size_t size;
    uint8_t *data = slurp(from, &size);
    CHECK(data != NULL);
    if (data) spill(to, data, keep < size ? keep : size);
    free(data);
}

/* The segments of a work directory, in order. */
static int list_segments(const char *work, char names[][32], int cap) {
    DIR *d = opendir(sub(work, "journal"));
    struct dirent *de;
    int n = 0;
    while (d && (de = readdir(d)) != NULL)
        if (strstr(de->d_name, ".frj") && n < cap) snprintf(names[n++], 32, "%.31s", de->d_name);
    if (d) closedir(d);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcmp(names[j], names[i]) < 0) {
                char tmp[32];
                memcpy(tmp, names[i], 32);
                memcpy(names[i], names[j], 32);
                memcpy(names[j], tmp, 32);
            }
    return n;
}

/* Go on with the level in `work` until it is complete; its table has to be the ladder's. */
static void finish_and_compare(const char *work, int bits, const char *what, unsigned long long where) {
    const char *out = in_base("recovered.frb");
    FrbRunOptions opt = quiet_options();
    FrbRunResult res;
    FrbConfig cfg;
    FrbError e;
    CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    cfg.workers = 1;
    e = frb_work_run(work, &cfg, &opt, &res, last_err, sizeof last_err);
    CHECK_MSG(e == FRB_OK && res.complete, "%s at %llu: going on fails: %s", what, where, last_err);
    if (e != FRB_OK) return;
    unlink(out);
    CHECK(frb_work_finalize(work, out, 0, NULL, last_err, sizeof last_err) == FRB_OK);
    CHECK_MSG(same_file(out, level_file(classic, bits)), "%s at %llu: not the ladder's table", what, where);
    same_state(work, bits);
}

/*
 * Death while the journal is written.  Whatever a process had written when
 * it died, down to the byte, is a beginning of what it would have written.
 * So: the journal of a finished level, cut off after every record and inside
 * records, and the construction taken up from there.
 */
static void test_cut_journal(int bits, int workers, int class_bits, int trials, int every_record) {
    const char *dir = in_base("cut-src"), *work, *made = in_base("cut");
    char names[64][32];
    size_t sizes[64], records;
    int segments, record_bytes = frb_journal_record_bytes(frb_format_for_bits(bits));
    shared_ladder(dir, workers, class_bits);
    work = level_work(dir, bits);
    segments = list_segments(work, names, 64);
    CHECK(segments >= 1 && segments <= 64);
    for (int k = 0; k < segments; k++) free(slurp(sub(sub(work, "journal"), names[k]), &sizes[k]));
    records = (sizes[0] - 64) / (size_t)record_bytes;
    CHECK(!every_record || segments == 1);
    if (every_record) trials += 2 * (int)(records + 1);

    for (int trial = 0; trial < trials; trial++) {
        size_t cut_total = 0;
        remove_tree(made);
        CHECK(mkdir(made, 0777) == 0 && mkdir(sub(made, "journal"), 0777) == 0);
        copy_file(sub(work, "config.txt"), sub(made, "config.txt"), (size_t)-1);
        for (int k = 0; k < segments; k++) {
            size_t keep;
            if (every_record) {
                /* One writer: after every record in turn, and somewhere inside the record after it. */
                size_t r = (size_t)trial / 2 % (records + 1);
                keep = 64 + r * (size_t)record_bytes + (trial % 2 ? rnd((uint32_t)record_bytes) : 0);
                if ((size_t)trial >= 2 * (records + 1)) keep = rnd((uint32_t)sizes[0] + 1); /* then anywhere */
            } else {
                /* Several writers, each dead at a place of its own; or alive to the end. */
                keep = rnd(4) == 0 ? sizes[k] : rnd((uint32_t)sizes[k] + 1);
            }
            if (keep > sizes[k]) keep = sizes[k];
            cut_total += keep;
            if (keep == 0 && rnd(2)) continue; /* it had not even made its file */
            copy_file(sub(sub(work, "journal"), names[k]), sub(sub(made, "journal"), names[k]), keep);
        }
        finish_and_compare(made, bits, "journal cut", (unsigned long long)cut_total);
    }
}

/*
 * Death while searching.  A construction in a process of its own, set to
 * kill itself (SIGKILL: no handler runs, nothing is flushed) when it has
 * made a given number of steps; then again from where that left it, to die
 * at another place; and then taken to its end.
 */
static uint64_t steps_of_level(const char *work) {
    FrbConfig cfg;
    FrbState *s = NULL;
    uint64_t steps = 0;
    CHECK(frb_config_read(work, &cfg, last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_state_load(work, &cfg, &s, last_err, sizeof last_err) == FRB_OK);
    for (int b = 1; s && b <= FRB_MAX_NODES; b++) steps += s->pass[b].steps;
    frb_state_free(s);
    return steps;
}

/* 1 if the process was killed, or its workers were. */
static int run_to_death(const char *work, int workers, uint64_t die_after) {
    pid_t pid;
    int status = 0;
    fflush(NULL);
    pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        FrbRunOptions opt = quiet_options();
        FrbRunResult res;
        FrbConfig cfg;
        char err[FRB_ERR_TEXT];
        FrbError e;
        if (frb_config_read(work, &cfg, err, sizeof err) != FRB_OK) _exit(9);
        cfg.workers = workers;
        opt.die_after_steps = die_after;
        e = frb_work_run(work, &cfg, &opt, &res, err, sizeof err);
        _exit(e == FRB_OK ? (res.complete ? 0 : 3) : 4); /* 4: its workers died */
    }
    while (waitpid(pid, &status, 0) < 0) {}
    if (WIFSIGNALED(status)) {
        CHECK(WTERMSIG(status) == SIGKILL);
        return 1;
    }
    CHECK_MSG(WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 4), "exit status %d",
              WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return WIFEXITED(status) && WEXITSTATUS(status) == 4;
}

static void test_killed(int bits, int workers, int class_bits, int trials) {
    const char *dir = in_base("kill-src"), *made = in_base("kill");
    FrbConfig settings, cfg;
    uint64_t total;
    int deaths = 0;
    shared_ladder(dir, 1, class_bits);
    total = steps_of_level(level_work(dir, bits));
    CHECK(total > 10000);
    ladder_settings(&settings, workers, 1, class_bits);
    for (int trial = 0; trial < trials; trial++) {
        /* The first death anywhere in the level, early and late ones among them. */
        uint64_t first = trial == 0 ? 1 : trial == 1 ? total - 1 : 1 + rnd64() % (total / (uint64_t)workers);
        remove_tree(made);
        frb_config_default(&cfg, bits);
        cfg.shared = 1;
        cfg.class_bits = class_bits;
        strcpy(cfg.engine, FRB_SHARED_ENGINE);
        CHECK(frb_config_set_base(&cfg, level_file(classic, bits - 1), last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_create(made, &cfg, last_err, sizeof last_err) == FRB_OK);
        deaths += run_to_death(made, workers, first);
        if (trial % 2) deaths += run_to_death(made, workers, 1 + rnd64() % (total / (uint64_t)workers)); /* and a second */
        if (trial % 4 == 3) deaths += run_to_death(made, 1, 1 + rnd64() % 5000);                         /* and at once */
        finish_and_compare(made, bits, "killed after steps", (unsigned long long)first);
    }
    /* Not every run dies: one that is to die in the last steps of the level may be done first. */
    CHECK_MSG(deaths >= trials * 3 / 4, "%d deaths in %d trials", deaths, trials);
}

/* Asked to stop by a signal at some moment of the search: nothing of the pass in hand is written. */
static void test_signalled(void) {
    const int bits = TOP;
    const char *made = in_base("signalled");
    int interrupted = 0;
    for (int trial = 0; trial < 12; trial++) {
        FrbConfig cfg;
        FrbState *s = NULL;
        pid_t pid;
        int status = 0;
        remove_tree(made);
        frb_config_default(&cfg, bits);
        cfg.shared = 1;
        cfg.class_bits = 2 + trial % 3;
        strcpy(cfg.engine, FRB_SHARED_ENGINE);
        CHECK(frb_config_set_base(&cfg, level_file(classic, bits - 1), last_err, sizeof last_err) == FRB_OK);
        CHECK(frb_work_create(made, &cfg, last_err, sizeof last_err) == FRB_OK);
        fflush(NULL);
        pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            FrbRunOptions opt = quiet_options();
            FrbRunResult res;
            char err[FRB_ERR_TEXT];
            cfg.workers = 1 + trial % 2;
            _exit(frb_work_run(made, &cfg, &opt, &res, err, sizeof err) == FRB_OK ? (res.interrupted ? 5 : 0) : 4);
        }
        usleep((useconds_t)(1000 * (1 + trial * 2)));
        kill(pid, SIGTERM);
        while (waitpid(pid, &status, 0) < 0) {}
        /* Done before the signal; stopped by it; or killed by it, having not yet taken signals in hand. */
        CHECK_MSG((WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 5)) ||
                      (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM),
                  "status %d", status);
        interrupted += WIFEXITED(status) && WEXITSTATUS(status) == 5;
        CHECK(load(made, &s) == FRB_OK);
        CHECK(s && s->uncommitted == 0 && s->conflicts == 0); /* a pass given up leaves nothing behind */
        frb_state_free(s);
        finish_and_compare(made, bits, "signalled in trial", (unsigned long long)trial);
    }
    (void)interrupted; /* how many of them the signal caught at work depends on the machine */
}

/* Asked to stop, as SIGINT asks: the pass in hand is given up and nothing of it is written. */
static void test_stopped(void) {
    const int bits = TOP;
    const char *made = in_base("stopped");
    FrbRunOptions opt = quiet_options();
    FrbRunResult res;
    FrbConfig cfg;
    FrbState *s = NULL;
    int rounds = 0;
    remove_tree(made);
    frb_config_default(&cfg, bits);
    cfg.shared = 1;
    cfg.class_bits = 4;
    strcpy(cfg.engine, FRB_SHARED_ENGINE);
    CHECK(frb_config_set_base(&cfg, level_file(classic, bits - 1), last_err, sizeof last_err) == FRB_OK);
    CHECK(frb_work_create(made, &cfg, last_err, sizeof last_err) == FRB_OK);
    /* A class at a time. */
    opt.limit_jobs = 1;
    do {
        CHECK(frb_work_run(made, &cfg, &opt, &res, last_err, sizeof last_err) == FRB_OK);
        CHECK(res.complete || res.stopped_early);
        CHECK(load(made, &s) == FRB_OK);
        CHECK(s && s->uncommitted == 0 && s->conflicts == 0);
        frb_state_free(s);
    } while (!res.complete && ++rounds < 64);
    CHECK(res.complete && rounds >= 3);
    finish_and_compare(made, bits, "a class at a time", 0);
}

int main(int argc, char **argv) {
    snprintf(base_dir, sizeof base_dir, "%s", argc > 1 ? argv[1] : "shared_build_test_work");
    remove_tree(base_dir);
    CHECK(mkdir(base_dir, 0777) == 0);
    build_reference();
    test_config();
    test_same_as_ladder();
    test_direct();
    test_admission();
    test_cut_journal(FOOT + 2, 1, 3, 40, 1); /* after every record of a level, and inside every record */
    test_cut_journal(TOP, 1, 4, 60, 0);
    test_cut_journal(TOP, 3, 5, 80, 0);
    test_killed(FOOT + 3, 1, 3, 24);
    test_killed(TOP, 3, 4, 16);
    test_stopped();
    test_signalled();
    if (!t_failures) remove_tree(base_dir);
    return t_report("test_shared_build");
}
