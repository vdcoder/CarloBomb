/*
 * FixedRunBomb construction: work directory, job planning, worker processes,
 * restart, finalization and the two deep checks.
 *
 * A session is a loop of rounds.  Each round replays the journal, plans the
 * jobs that are still open, runs them and appends what happened:
 *
 *   Stage A   one round per node count 1..enumerate_max; a job is one part
 *             of the exhaustive enumeration of the graphs with that many
 *             nodes (enumerate.c).
 *   Stage B   one round per entry of max_steps; a job is one target, for
 *             which the LevelSolver is run at ascending node budgets from
 *             the first one not yet excluded.
 *
 * With more than one worker a round forks that many processes.  Job i of the
 * round goes to worker i mod W, each worker appends to a journal segment of
 * its own, and the coordinator only waits, relays progress and replays.  The
 * LevelSolver's static search state is therefore never shared.
 *
 * Restart needs no code of its own: a session that starts on a journal with
 * records in it plans only what those records leave open.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "fixed_run_build.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "level_solver.h"

#define CONFIG_MAGIC "carlobomb-fixed-run-work 1"

static void set_err(char *err, size_t errlen, const char *fmt, ...) {
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t seconds_ns(double s) { return s <= 0 ? 0 : (uint64_t)(s * 1e9); }

/* ---- configuration ------------------------------------------------------ */

void frb_config_default(FrbConfig *cfg, int bits) {
    memset(cfg, 0, sizeof *cfg);
    cfg->bits = bits;
    cfg->symmetry = 1;
    cfg->enumerate_max = 5;
    cfg->max_nodes = FRB_NODE_CAPACITY;
    cfg->fallback_ring = 1;
    cfg->workers = 1;
    cfg->passes = 1;
    cfg->max_steps[0] = 0;
    strcpy(cfg->engine, "c");
}

FrbError frb_config_check(const FrbConfig *cfg, char *err, size_t errlen) {
    if (!cfg) return FRB_ERR_ARG;
    if (cfg->bits < 1 || cfg->bits > FRB_MAX_BITS) {
        set_err(err, errlen, "bits must be 1..%d", FRB_MAX_BITS);
        return FRB_ERR_ARG;
    }
    if (cfg->max_nodes < 1 || cfg->max_nodes > FRB_NODE_CAPACITY) {
        set_err(err, errlen, "max-nodes must be 1..%d (the capacity of an entry)",
                FRB_NODE_CAPACITY);
        return FRB_ERR_ARG;
    }
    if (cfg->enumerate_max < 0 || cfg->enumerate_max > FRB_ENUMERATE_LIMIT) {
        set_err(err, errlen, "enumerate-max must be 0..%d", FRB_ENUMERATE_LIMIT);
        return FRB_ERR_ARG;
    }
    if (cfg->workers < 1 || cfg->workers > FRB_MAX_WORKERS) {
        set_err(err, errlen, "workers must be 1..%d", FRB_MAX_WORKERS);
        return FRB_ERR_ARG;
    }
    if (cfg->passes < 1 || cfg->passes > FRB_MAX_PASSES) {
        set_err(err, errlen, "max-steps takes 1..%d values", FRB_MAX_PASSES);
        return FRB_ERR_ARG;
    }
    if (cfg->engine[sizeof cfg->engine - 1] != '\0' || cfg->engine[0] == '\0' ||
        strpbrk(cfg->engine, " \t\r\n")) {
        set_err(err, errlen, "bad engine name");
        return FRB_ERR_ARG;
    }
    return FRB_OK;
}

static int config_path(char *buf, size_t cap, const char *dir, const char *name) {
    int n = snprintf(buf, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

FrbError frb_config_write(const char *dir, const FrbConfig *cfg) {
    char path[4096], text[1024];
    int n;
    if (!dir || frb_config_check(cfg, NULL, 0) != FRB_OK) return FRB_ERR_ARG;
    if (!config_path(path, sizeof path, dir, "config.txt")) return FRB_ERR_ARG;
    n = snprintf(text, sizeof text,
                 CONFIG_MAGIC "\n"
                              "# Settings of a FixedRunBomb construction.  The first five are fixed for\n"
                              "# the life of this directory; the rest may be changed on resume.\n"
                              "journal_version %d\n"
                              "search_revision %d\n"
                              "bits %d\n"
                              "symmetry %d\n"
                              "enumerate_max %d\n"
                              "max_nodes %d\n"
                              "fallback %s\n"
                              "engine %s\n"
                              "workers %d\n"
                              "max_steps ",
                 FRB_JOURNAL_VERSION, FRB_SEARCH_REVISION, cfg->bits, cfg->symmetry ? 1 : 0,
                 cfg->enumerate_max, cfg->max_nodes, cfg->fallback_ring ? "ring" : "none",
                 cfg->engine, cfg->workers);
    for (int i = 0; i < cfg->passes && n > 0 && (size_t)n < sizeof text; i++)
        n += snprintf(text + n, sizeof text - (size_t)n, "%s%llu", i ? "," : "",
                      (unsigned long long)cfg->max_steps[i]);
    if (n <= 0 || (size_t)n + 2 > sizeof text) return FRB_ERR_ARG;
    text[n++] = '\n';
    return frb_write_atomic(path, text, (size_t)n);
}

static int parse_int(const char *s, int *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < -1000000 || v > 1000000) return 0;
    *out = (int)v;
    return 1;
}

int frb_parse_step_limits(const char *s, FrbConfig *cfg) {
    if (!s || !cfg) return 0;
    cfg->passes = 0;
    while (*s) {
        char *end;
        unsigned long long v;
        if (cfg->passes == FRB_MAX_PASSES || *s < '0' || *s > '9') return 0;
        errno = 0;
        v = strtoull(s, &end, 10);
        if (errno || end == s) return 0;
        cfg->max_steps[cfg->passes++] = (uint64_t)v;
        if (*end == ',') end++;
        else if (*end != '\0') return 0;
        if (*end == '\0' && end[-1] == ',') return 0;
        s = end;
    }
    return cfg->passes > 0;
}

FrbError frb_config_read(const char *dir, FrbConfig *cfg, char *err, size_t errlen) {
    char path[4096], line[512], key[64], value[256];
    unsigned seen = 0;
    int version = -1, revision = -1, lineno = 0;
    FILE *f;
    if (!dir || !cfg) return FRB_ERR_ARG;
    memset(cfg, 0, sizeof *cfg);
    if (!config_path(path, sizeof path, dir, "config.txt")) return FRB_ERR_ARG;
    f = fopen(path, "r");
    if (!f) {
        set_err(err, errlen, "%s: no FixedRunBomb construction here (cannot open config.txt)", dir);
        return FRB_ERR_IO;
    }
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        lineno++;
        if (lineno == 1) {
            if (strcmp(line, CONFIG_MAGIC) != 0) {
                fclose(f);
                set_err(err, errlen, "%s: not a FixedRunBomb work directory of a known version", path);
                return FRB_ERR_INCOMPATIBLE;
            }
            continue;
        }
        if (line[0] == '#' || line[0] == '\0') continue;
        if (sscanf(line, "%63s %255s", key, value) != 2) goto bad;
        if (!strcmp(key, "journal_version")) { if (!parse_int(value, &version)) goto bad; seen |= 1u; }
        else if (!strcmp(key, "search_revision")) { if (!parse_int(value, &revision)) goto bad; seen |= 2u; }
        else if (!strcmp(key, "bits")) { if (!parse_int(value, &cfg->bits)) goto bad; seen |= 4u; }
        else if (!strcmp(key, "symmetry")) { if (!parse_int(value, &cfg->symmetry)) goto bad; seen |= 8u; }
        else if (!strcmp(key, "enumerate_max")) { if (!parse_int(value, &cfg->enumerate_max)) goto bad; seen |= 16u; }
        else if (!strcmp(key, "max_nodes")) { if (!parse_int(value, &cfg->max_nodes)) goto bad; seen |= 32u; }
        else if (!strcmp(key, "fallback")) {
            if (!strcmp(value, "ring")) cfg->fallback_ring = 1;
            else if (!strcmp(value, "none")) cfg->fallback_ring = 0;
            else goto bad;
            seen |= 64u;
        }
        else if (!strcmp(key, "engine")) {
            if (strlen(value) >= sizeof cfg->engine) goto bad;
            strcpy(cfg->engine, value);
            seen |= 128u;
        }
        else if (!strcmp(key, "workers")) { if (!parse_int(value, &cfg->workers)) goto bad; seen |= 256u; }
        else if (!strcmp(key, "max_steps")) { if (!frb_parse_step_limits(value, cfg)) goto bad; seen |= 512u; }
        else goto bad;
    }
    fclose(f);
    if (seen != 1023u) {
        set_err(err, errlen, "%s: incomplete (a setting is missing)", path);
        return FRB_ERR_INCOMPATIBLE;
    }
    if (version != FRB_JOURNAL_VERSION) {
        set_err(err, errlen, "%s: journal format version %d, this program reads version %d", path,
                version, FRB_JOURNAL_VERSION);
        return FRB_ERR_INCOMPATIBLE;
    }
    if (revision != FRB_SEARCH_REVISION) {
        set_err(err, errlen,
                "%s: made with search revision %d, this program is revision %d; "
                "results of different revisions cannot be mixed",
                path, revision, FRB_SEARCH_REVISION);
        return FRB_ERR_INCOMPATIBLE;
    }
    if (cfg->symmetry != 0 && cfg->symmetry != 1) goto invalid;
    {
        FrbError e = frb_config_check(cfg, err, errlen);
        if (e != FRB_OK) return FRB_ERR_INCOMPATIBLE;
    }
    return FRB_OK;
bad:
    fclose(f);
    set_err(err, errlen, "%s: line %d is not a setting this program knows", path, lineno);
    return FRB_ERR_INCOMPATIBLE;
invalid:
    set_err(err, errlen, "%s: impossible value", path);
    return FRB_ERR_INCOMPATIBLE;
}

FrbError frb_work_create(const char *dir, const FrbConfig *cfg, char *err, size_t errlen) {
    char path[4096];
    FrbJournalInfo info;
    struct stat st;
    FrbError e;
    if (!dir) return FRB_ERR_ARG;
    e = frb_config_check(cfg, err, errlen);
    if (e != FRB_OK) return e;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        set_err(err, errlen, "cannot create directory %s", dir);
        return FRB_ERR_IO;
    }
    if (!config_path(path, sizeof path, dir, "config.txt")) return FRB_ERR_ARG;
    if (stat(path, &st) == 0) {
        set_err(err, errlen, "%s already holds a construction; use `bomb resume` to continue it", dir);
        return FRB_ERR_EXISTS;
    }
    if (!config_path(path, sizeof path, dir, "journal")) return FRB_ERR_ARG;
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        set_err(err, errlen, "cannot create directory %s", path);
        return FRB_ERR_IO;
    }
    /* A journal without its settings is not something to build on. */
    e = frb_journal_scan(dir, cfg, NULL, NULL, &info, NULL, 0);
    if (e != FRB_OK || info.segments != 0) {
        set_err(err, errlen, "%s holds journal segments but no config.txt; not touching it", dir);
        return FRB_ERR_EXISTS;
    }
    e = frb_config_write(dir, cfg);
    if (e != FRB_OK) set_err(err, errlen, "cannot write %s/config.txt", dir);
    return e;
}

/* ---- stop requests ------------------------------------------------------ */

static volatile sig_atomic_t g_stop; /* 1: asked to stop.  2: the time limit passed */
static uint64_t g_deadline_ns;       /* 0 = none */

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

void frb_request_stop(void) { g_stop = 1; }

static int should_stop(void) {
    if (!g_stop && g_deadline_ns && now_ns() >= g_deadline_ns) g_stop = 2;
    return g_stop != 0;
}

/* ---- jobs --------------------------------------------------------------- */

enum { JOB_ENUM = 0, JOB_SEARCH = 1 };

typedef struct {
    uint8_t kind;
    uint8_t lo;      /* JOB_ENUM: node count.  JOB_SEARCH: first node budget */
    uint8_t hi;      /* JOB_SEARCH: last node budget */
    uint16_t part;   /* JOB_ENUM */
    uint32_t target; /* JOB_SEARCH */
} Job;

static uint64_t effective_limit(uint64_t max_steps) { return max_steps ? max_steps : UINT64_MAX; }

static size_t plan_enum(const FrbState *s, int nodes, Job **jobs) {
    int parts = frb_enum_part_count(nodes);
    size_t n = 0;
    Job *list = calloc((size_t)parts, sizeof *list);
    *jobs = list;
    if (!list) return 0;
    for (int p = 0; p < parts; p++) {
        if (s->part_done[nodes][p >> 3] & (1u << (p & 7))) continue;
        list[n].kind = JOB_ENUM;
        list[n].lo = (uint8_t)nodes;
        list[n].part = (uint16_t)p;
        n++;
    }
    return n;
}

/* The targets a pass with this step limit still has something to do for. */
static size_t plan_search(const FrbState *s, const FrbConfig *cfg, uint64_t max_steps, Job **jobs) {
    uint32_t top = (uint32_t)1 << (s->bits - 1);
    size_t n = 0;
    Job *list = calloc(s->count, sizeof *list);
    *jobs = list;
    if (!list) return 0;
    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        int lo, hi;
        if (s->symmetry && (i & top)) continue; /* its complement is the job */
        if (ts->status == FRB_MINIMAL || ts->conflict) continue;
        lo = ts->lower_bound;
        hi = cfg->max_nodes;
        if (ts->nodes && ts->nodes - 1 < hi) hi = ts->nodes - 1; /* a witness needs no search */
        if (lo > hi) continue;
        if (ts->limit_budget == lo) {
            if (ts->limit_kind == REV_LIMIT_STACK) continue; /* more steps will not help */
            if (effective_limit(ts->limit_max_steps) >= effective_limit(max_steps)) continue;
        }
        list[n].kind = JOB_SEARCH;
        list[n].lo = (uint8_t)lo;
        list[n].hi = (uint8_t)hi;
        list[n].target = i;
        n++;
    }
    return n;
}

static uint64_t prng_next(uint64_t *state) { /* splitmix64 */
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int cmp_job_target(const void *a, const void *b) {
    uint32_t x = ((const Job *)a)->target, y = ((const Job *)b)->target;
    return (x > y) - (x < y);
}

/* Keep `keep` of the n jobs, chosen uniformly, in ascending target order. */
static void sample_jobs(Job *jobs, size_t n, size_t keep, uint64_t seed) {
    uint64_t state = seed;
    for (size_t i = 0; i < keep; i++) {
        size_t j = i + (size_t)(prng_next(&state) % (uint64_t)(n - i));
        Job tmp = jobs[i];
        jobs[i] = jobs[j];
        jobs[j] = tmp;
    }
    qsort(jobs, keep, sizeof *jobs, cmp_job_target);
}

/* ---- running jobs ------------------------------------------------------- */

typedef struct {
    uint32_t worker;
    uint32_t state; /* 0 running, 1 finished its list, 2 stopped early, 3 failed */
    uint64_t jobs, searches, steps, graphs;
} Progress;

typedef struct Session Session;

typedef struct {
    Session *session;
    FrbSegment *seg;
    uint64_t max_steps; /* step limit of this round's searches */
    int pipe_fd;        /* progress goes here; -1: this is the coordinator itself */
    Progress p;
    uint64_t last_flush_ns, last_sync_ns, last_report_ns;
    char err[FRB_ERR_TEXT];
} Worker;

struct Session {
    const char *dir;
    const FrbConfig *cfg;
    const FrbRunOptions *opt;
    FrbSegment *coord; /* the coordinator's own journal segment */
    uint32_t number;   /* session number */
    uint32_t next_segment;
    uint64_t start_ns;
    /* the round in progress, for progress lines */
    int round_kind;
    size_t round_jobs;
    uint64_t round_start_ns, last_progress_ns;
    char round_label[96];
};

static void progress_line(Session *S, const Progress *p, int final) {
    FILE *log = S->opt->log;
    uint64_t now = now_ns();
    double t, rt;
    if (!log) return;
    if (!final) {
        if (S->opt->progress_seconds <= 0) return;
        if (now - S->last_progress_ns < seconds_ns(S->opt->progress_seconds)) return;
    }
    S->last_progress_ns = now;
    t = (double)(now - S->start_ns) * 1e-9;
    rt = (double)(now - S->round_start_ns) * 1e-9;
    if (S->round_kind == JOB_ENUM) {
        fprintf(log, "[%8.1f s] %s: %llu/%zu parts, %.4g graphs", t, S->round_label,
                (unsigned long long)p->jobs, S->round_jobs, (double)p->graphs);
    } else {
        fprintf(log, "[%8.1f s] %s: %llu/%zu targets, %llu searches, %.4g steps", t, S->round_label,
                (unsigned long long)p->jobs, S->round_jobs, (unsigned long long)p->searches,
                (double)p->steps);
        if (rt > 0.05) fprintf(log, " (%.3g steps/s)", (double)p->steps / rt);
    }
    if (final) fprintf(log, ", %.2f s\n", rt);
    else if (p->jobs > 0 && p->jobs < S->round_jobs)
        fprintf(log, ", about %.0f s left\n",
                rt * (double)(S->round_jobs - p->jobs) / (double)p->jobs);
    else fprintf(log, "\n");
    fflush(log);
}

static FrbError append(Worker *w, const FrbRecord *rec) {
    FrbError e = frb_segment_append(w->seg, rec);
    if (e != FRB_OK) set_err(w->err, sizeof w->err, "cannot write to the journal (disk full?)");
    return e;
}

/* Between jobs: push records out, force them to disk now and then, report. */
static FrbError housekeeping(Worker *w, int final) {
    const FrbRunOptions *opt = w->session->opt;
    uint64_t now = now_ns();
    FrbError e = FRB_OK;
    if (final || now - w->last_sync_ns >= seconds_ns(opt->sync_seconds)) {
        e = frb_segment_flush(w->seg, 1);
        w->last_sync_ns = w->last_flush_ns = now;
    } else if (now - w->last_flush_ns >= 200000000ull) {
        e = frb_segment_flush(w->seg, 0);
        w->last_flush_ns = now;
    }
    if (e != FRB_OK) set_err(w->err, sizeof w->err, "cannot write to the journal (disk full?)");
    if (final || now - w->last_report_ns >= 100000000ull) {
        w->last_report_ns = now;
        if (w->pipe_fd < 0) {
            progress_line(w->session, &w->p, 0);
        } else if (write(w->pipe_fd, &w->p, sizeof w->p) != (ssize_t)sizeof w->p) {
            g_stop = 1; /* nobody is listening: the coordinator is gone */
        }
    }
    return e;
}

typedef struct {
    Worker *w;
    int nodes, part;
    uint32_t count;
    FrbError error;
} EnumCtx;

static int enum_found(void *ctx, uint32_t index, const RevGraph *g) {
    EnumCtx *c = ctx;
    FrbRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_ENUM_WITNESS;
    rec.budget = (uint8_t)c->nodes;
    rec.aux = (uint32_t)c->part;
    rec.target = index;
    rec.time = (uint32_t)time(NULL);
    rec.nodes = (uint8_t)c->nodes;
    c->error = frb_graph_pack(g, rec.graph);
    if (c->error == FRB_OK) c->error = append(c->w, &rec);
    if (c->error != FRB_OK) return 1;
    c->count++;
    return 0;
}

static FrbError do_enum(Worker *w, const Job *job) {
    const FrbConfig *cfg = w->session->cfg;
    FrbEnumPart part = {job->lo, cfg->bits, job->part, cfg->symmetry ? 1 : 0};
    EnumCtx ctx = {w, job->lo, job->part, 0, FRB_OK};
    FrbRecord rec;
    uint64_t graphs = 0, t0 = now_ns();
    FrbError e = frb_enumerate_part(&part, enum_found, &ctx, &g_stop, &graphs);
    w->p.graphs += graphs;
    if (ctx.error != FRB_OK) return ctx.error;
    if (e != FRB_OK) return e; /* FRB_ERR_STOPPED: the witnesses stay, the part stays open */
    if (graphs != frb_enum_part_graphs(job->lo, cfg->symmetry ? 1 : 0)) {
        set_err(w->err, sizeof w->err, "enumeration of %d-node part %d ran %llu graphs", job->lo,
                job->part, (unsigned long long)graphs);
        return FRB_ERR_EVIDENCE;
    }
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_ENUM_PART;
    rec.budget = job->lo;
    rec.aux = job->part;
    rec.target = ctx.count;
    rec.steps = graphs;
    rec.elapsed_ns = now_ns() - t0;
    rec.time = (uint32_t)time(NULL);
    return append(w, &rec);
}

static FrbError do_search(Worker *w, const Job *job) {
    const FrbConfig *cfg = w->session->cfg;
    RevLevelOptions options = {w->max_steps};
    char target[FRB_MAX_BITS + 1];
    frb_target_format(job->target, cfg->bits, target);

    for (int b = job->lo; b <= job->hi; b++) {
        RevLevelStats st;
        RevGraph g;
        FrbRecord rec;
        RevSolveResult r;
        uint64_t t0;
        FrbError e;
        if (should_stop()) return FRB_ERR_STOPPED;
        memset(&st, 0, sizeof st);
        t0 = now_ns();
        r = w->session->opt->solve(target, (size_t)cfg->bits, b, &options, &g, &st);

        memset(&rec, 0, sizeof rec);
        rec.type = FRB_REC_SEARCH;
        rec.result = (uint8_t)r;
        rec.budget = (uint8_t)b;
        rec.target = job->target;
        rec.steps = st.steps;
        rec.max_steps = w->max_steps;
        rec.elapsed_ns = now_ns() - t0;
        rec.time = (uint32_t)time(NULL);
        rec.aux = (uint32_t)st.peak_stack;
        switch (r) {
        case REV_SOLVE_SOLVED:
            /* The engine has verified its witness; do it again here, where
             * the record is made. */
            if (g.node_count < 1 || g.node_count > b ||
                rev_verify_prefix(&g, target, (size_t)cfg->bits, NULL) != REV_OK ||
                frb_graph_pack(&g, rec.graph) != FRB_OK) {
                set_err(w->err, sizeof w->err, "engine returned an invalid witness for %s at budget %d",
                        target, b);
                return FRB_ERR_EVIDENCE;
            }
            rec.nodes = (uint8_t)g.node_count;
            break;
        case REV_SOLVE_PROVEN_UNSAT:
            break;
        case REV_SOLVE_RESOURCE_LIMIT:
            /* Recorded as what it is.  It excludes nothing. */
            rec.limit = (uint8_t)st.limit;
            if (st.limit != REV_LIMIT_STACK && st.limit != REV_LIMIT_STEPS) {
                set_err(w->err, sizeof w->err, "engine stopped for no stated reason on %s", target);
                return FRB_ERR_EVIDENCE;
            }
            break;
        default:
            set_err(w->err, sizeof w->err, "engine failed on %s at budget %d: %s", target, b,
                    rev_solve_result_str(r));
            return FRB_ERR_EVIDENCE;
        }
        e = append(w, &rec);
        if (e != FRB_OK) return e;
        w->p.searches++;
        w->p.steps += st.steps;
        if (r != REV_SOLVE_PROVEN_UNSAT) break;
    }
    return FRB_OK;
}

/* Jobs first, first + stride, ... of the round. */
static FrbError run_jobs(Worker *w, const Job *jobs, size_t count, size_t first, size_t stride) {
    FrbError e = FRB_OK, e2;
    for (size_t i = first; i < count; i += stride) {
        if (should_stop()) {
            w->p.state = 2;
            break;
        }
        e = jobs[i].kind == JOB_ENUM ? do_enum(w, &jobs[i]) : do_search(w, &jobs[i]);
        if (e == FRB_ERR_STOPPED) {
            e = FRB_OK;
            w->p.state = 2;
            break;
        }
        if (e != FRB_OK) break;
        w->p.jobs++;
        e = housekeeping(w, 0);
        if (e != FRB_OK) break;
    }
    if (e != FRB_OK) w->p.state = 3;
    else if (w->p.state == 0) w->p.state = 1;
    e2 = housekeeping(w, 1);
    return e != FRB_OK ? e : e2;
}

static void worker_init(Worker *w, Session *S, FrbSegment *seg, uint64_t max_steps, int pipe_fd,
                        uint32_t index) {
    memset(w, 0, sizeof *w);
    w->session = S;
    w->seg = seg;
    w->max_steps = max_steps;
    w->pipe_fd = pipe_fd;
    w->p.worker = index;
    w->last_flush_ns = w->last_sync_ns = w->last_report_ns = now_ns();
}

/* A forked worker.  Never returns. */
static void child_main(Session *S, const Job *jobs, size_t count, uint64_t max_steps, int k,
                       int workers, uint32_t segment, int fd) {
    Worker w;
    FrbSegment *seg = NULL;
    FrbError e;
    signal(SIGPIPE, SIG_IGN); /* a vanished coordinator shows as a failed write */
    e = frb_segment_create(S->dir, S->cfg, segment, S->number, (uint32_t)k + 1u, &seg);
    if (e != FRB_OK) {
        fprintf(stderr, "worker %d: cannot create journal segment %06u\n", k + 1, (unsigned)segment);
        _exit(2);
    }
    worker_init(&w, S, seg, max_steps, fd, (uint32_t)k);
    e = run_jobs(&w, jobs, count, (size_t)k, (size_t)workers);
    if (frb_segment_close(seg) != FRB_OK && e == FRB_OK) {
        e = FRB_ERR_IO;
        set_err(w.err, sizeof w.err, "cannot write to the journal (disk full?)");
    }
    if (e != FRB_OK) {
        fprintf(stderr, "worker %d: %s\n", k + 1, w.err[0] ? w.err : frb_error_str(e));
        _exit(2);
    }
    _exit(0); /* no atexit handlers, no flushing of streams inherited from the coordinator */
}

static FrbError run_round_forked(Session *S, const Job *jobs, size_t count, uint64_t max_steps,
                                 int workers, Progress *total, char *err, size_t errlen) {
    Progress *latest = calloc((size_t)workers, sizeof *latest);
    pid_t *pids = calloc((size_t)workers, sizeof *pids);
    int fds[2], started = 0, forwarded = 0, failed = 0;
    FrbError e = FRB_OK;
    if (!latest || !pids || pipe(fds) != 0) {
        free(latest);
        free(pids);
        set_err(err, errlen, "cannot start worker processes");
        return FRB_ERR_IO;
    }
    frb_segment_flush(S->coord, 0);
    fflush(NULL); /* nothing buffered may be written twice */
    for (int k = 0; k < workers; k++) {
        uint32_t segment = S->next_segment++;
        pid_t pid = fork();
        if (pid < 0) break; /* go on with the workers there are; the rest is replanned */
        if (pid == 0) {
            close(fds[0]);
            child_main(S, jobs, count, max_steps, k, workers, segment, fds[1]);
        }
        pids[started++] = pid;
    }
    close(fds[1]);
    if (started == 0) {
        close(fds[0]);
        free(latest);
        free(pids);
        set_err(err, errlen, "cannot start worker processes");
        return FRB_ERR_IO;
    }
    if (started < workers && S->opt->log)
        fprintf(S->opt->log, "note: only %d of %d worker processes could be started\n", started,
                workers);

    for (;;) { /* until every worker has closed its end of the pipe */
        struct pollfd pfd = {fds[0], POLLIN, 0};
        int r = poll(&pfd, 1, 200);
        if (r > 0) {
            Progress p;
            ssize_t got = read(fds[0], &p, sizeof p);
            if (got == 0) break;
            if (got == (ssize_t)sizeof p && p.worker < (uint32_t)workers) latest[p.worker] = p;
            else if (got < 0 && errno != EINTR && errno != EAGAIN) break;
        } else if (r < 0 && errno != EINTR) {
            break;
        }
        if (should_stop() && !forwarded) { /* a signal sent to this process only */
            for (int k = 0; k < started; k++) kill(pids[k], SIGTERM);
            forwarded = 1;
        }
        memset(total, 0, sizeof *total);
        for (int k = 0; k < workers; k++) {
            total->jobs += latest[k].jobs;
            total->searches += latest[k].searches;
            total->steps += latest[k].steps;
            total->graphs += latest[k].graphs;
        }
        progress_line(S, total, 0);
    }
    close(fds[0]);

    for (int k = 0; k < started; k++) {
        int status = 0;
        while (waitpid(pids[k], &status, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) failed++;
    }
    memset(total, 0, sizeof *total);
    for (int k = 0; k < workers; k++) {
        total->jobs += latest[k].jobs;
        total->searches += latest[k].searches;
        total->steps += latest[k].steps;
        total->graphs += latest[k].graphs;
    }
    if (failed) {
        set_err(err, errlen, "%d worker process%s failed; what they had recorded is kept", failed,
                failed == 1 ? "" : "es");
        e = FRB_ERR_IO;
    }
    free(latest);
    free(pids);
    return e;
}

static FrbError run_round(Session *S, const Job *jobs, size_t count, uint64_t max_steps,
                          Progress *total, char *err, size_t errlen) {
    int workers = S->cfg->workers;
    FrbError e;
    if ((size_t)workers > count) workers = (int)count; /* never more processes than jobs */
    /* Up to 4 nodes the whole enumeration is a fraction of a second. */
    if (S->round_kind == JOB_ENUM && jobs[0].lo <= 4) workers = 1;
    S->round_jobs = count;
    S->round_start_ns = S->last_progress_ns = now_ns();
    memset(total, 0, sizeof *total);
    if (workers <= 1) {
        Worker w;
        worker_init(&w, S, S->coord, max_steps, -1, 0);
        e = run_jobs(&w, jobs, count, 0, 1);
        *total = w.p;
        if (e != FRB_OK) set_err(err, errlen, "%s", w.err[0] ? w.err : frb_error_str(e));
    } else {
        e = run_round_forked(S, jobs, count, max_steps, workers, total, err, errlen);
    }
    progress_line(S, total, 1);
    return e;
}

/* ---- sessions ----------------------------------------------------------- */

void frb_run_options_default(FrbRunOptions *opt) {
    memset(opt, 0, sizeof *opt);
    opt->solve = rev_level_solve_ex;
    opt->progress_seconds = 5;
    opt->sync_seconds = 10;
}

static int lock_dir(const char *dir, char *err, size_t errlen, FrbError *e) {
    char path[4096];
    int fd;
    *e = FRB_OK;
    if (!config_path(path, sizeof path, dir, "lock")) {
        *e = FRB_ERR_ARG;
        return -1;
    }
    fd = open(path, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        set_err(err, errlen, "cannot open %s", path);
        *e = FRB_ERR_IO;
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        set_err(err, errlen, "%s is in use by another build (or by workers it left running)", dir);
        *e = FRB_ERR_BUSY;
        return -1;
    }
    return fd;
}

static FrbError first_conflict(const FrbState *s, char *err, size_t errlen) {
    char target[FRB_MAX_BITS + 1];
    if (!s->conflicts) return FRB_OK;
    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        if (!ts->conflict) continue;
        frb_target_format(i, s->bits, target);
        set_err(err, errlen,
                "contradictory results for %u target(s), first %s: a %d-node witness, but node "
                "budgets up to %d are recorded as excluded (enumeration found it at %d nodes; "
                "0 = never)",
                (unsigned)s->conflicts, target, ts->nodes, ts->lower_bound - 1, ts->enum_nodes);
        break;
    }
    return FRB_ERR_EVIDENCE;
}

FrbError frb_work_run(const char *dir, const FrbConfig *cfg, const FrbRunOptions *opt,
                      FrbRunResult *result, char *err, size_t errlen) {
    struct sigaction action, old_int, old_term;
    FrbRunResult local;
    FrbState *state = NULL;
    FrbRecord rec;
    Session S;
    uint64_t jobs_left, sample_left;
    int lock_fd, sampling;
    FrbError e, e2;

    if (!result) result = &local;
    memset(result, 0, sizeof *result);
    if (!dir || !cfg || !opt || !opt->solve) return FRB_ERR_ARG;
    e = frb_config_check(cfg, err, errlen);
    if (e != FRB_OK) return e;
    lock_fd = lock_dir(dir, err, errlen, &e);
    if (lock_fd < 0) return e;
    e = frb_state_load(dir, cfg, &state, err, errlen);
    if (e != FRB_OK) {
        close(lock_fd);
        return e;
    }

    memset(&S, 0, sizeof S);
    S.dir = dir;
    S.cfg = cfg;
    S.opt = opt;
    S.number = state->last_session + 1u;
    S.next_segment = state->journal.next_segment;
    S.start_ns = now_ns();
    e = frb_segment_create(dir, cfg, S.next_segment++, S.number, 0, &S.coord);
    if (e != FRB_OK) {
        set_err(err, errlen, "cannot create a journal segment in %s", dir);
        frb_state_free(state);
        close(lock_fd);
        return e;
    }
    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SESSION_BEGIN;
    rec.target = S.number;
    rec.aux = (uint32_t)cfg->workers;
    rec.max_steps = cfg->max_steps[0];
    rec.time = (uint32_t)time(NULL);
    e = frb_segment_append(S.coord, &rec);
    if (e == FRB_OK) e = frb_segment_flush(S.coord, 1);

    g_stop = 0;
    g_deadline_ns = opt->time_limit > 0 ? S.start_ns + seconds_ns(opt->time_limit) : 0;
    memset(&action, 0, sizeof action);
    action.sa_handler = on_signal; /* no SA_RESTART: a signal must wake the coordinator */
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &old_int);
    sigaction(SIGTERM, &action, &old_term);

    jobs_left = opt->limit_jobs ? opt->limit_jobs : UINT64_MAX;
    sampling = opt->sample != 0;
    sample_left = opt->sample;

    while (e == FRB_OK && !should_stop()) {
        Progress total;
        Job *jobs = NULL;
        size_t count = 0;
        uint64_t limit = 0;
        int kind;

        e = first_conflict(state, err, errlen);
        if (e != FRB_OK) break;
        if (result->rounds >= 4096) { /* cannot happen; never loop forever on a journal */
            set_err(err, errlen, "construction is not converging");
            e = FRB_ERR_EVIDENCE;
            break;
        }

        if (state->enum_complete < cfg->enumerate_max) {
            int nodes = state->enum_complete + 1;
            kind = JOB_ENUM;
            count = plan_enum(state, nodes, &jobs);
            snprintf(S.round_label, sizeof S.round_label, "stage A, %d-node graphs", nodes);
        } else {
            kind = JOB_SEARCH;
            for (int pass = 0; pass < cfg->passes && count == 0; pass++) {
                free(jobs);
                limit = cfg->max_steps[pass];
                count = plan_search(state, cfg, limit, &jobs);
            }
            if (limit) snprintf(S.round_label, sizeof S.round_label,
                                "stage B, at most %llu steps per search", (unsigned long long)limit);
            else snprintf(S.round_label, sizeof S.round_label, "stage B, no step limit");
        }
        if (!jobs) {
            e = FRB_ERR_MEMORY;
            break;
        }
        if (count == 0) { /* nothing these settings can still do */
            free(jobs);
            break;
        }
        if (jobs_left == 0 || (sampling && kind == JOB_SEARCH && sample_left == 0)) {
            result->stopped_early = 1;
            free(jobs);
            break;
        }
        if (sampling && kind == JOB_SEARCH) {
            size_t keep = count < sample_left ? count : (size_t)sample_left;
            sample_jobs(jobs, count, keep, opt->seed);
            count = keep;
        }
        if (count > jobs_left) count = (size_t)jobs_left;

        S.round_kind = kind;
        e = run_round(&S, jobs, count, limit, &total, err, errlen);
        free(jobs);
        result->rounds++;
        result->jobs += total.jobs;
        result->searches += total.searches;
        result->steps += total.steps;
        result->graphs += total.graphs;
        jobs_left -= total.jobs < jobs_left ? total.jobs : jobs_left;
        if (sampling && kind == JOB_SEARCH)
            sample_left -= total.jobs < sample_left ? total.jobs : sample_left;
        if (e != FRB_OK) break;

        if (frb_segment_flush(S.coord, 0) != FRB_OK) {
            set_err(err, errlen, "cannot write to the journal (disk full?)");
            e = FRB_ERR_IO;
            break;
        }
        frb_state_free(state);
        state = NULL;
        e = frb_state_load(dir, cfg, &state, err, errlen);
        if (e != FRB_OK) break;
        if (total.jobs == 0 && !should_stop()) {
            set_err(err, errlen, "a round finished without completing a job");
            e = FRB_ERR_IO;
        }
    }
    if (e == FRB_OK && state) e = first_conflict(state, err, errlen);

    result->interrupted = g_stop == 1;
    if (g_stop == 2) result->stopped_early = 1;
    result->seconds = (double)(now_ns() - S.start_ns) * 1e-9;
    result->complete = e == FRB_OK && state && state->by_status[FRB_MINIMAL] == state->count;

    memset(&rec, 0, sizeof rec);
    rec.type = FRB_REC_SESSION_END;
    rec.target = S.number;
    rec.result = (uint8_t)(result->complete ? 0 : 1);
    rec.elapsed_ns = now_ns() - S.start_ns;
    rec.time = (uint32_t)time(NULL);
    e2 = frb_segment_append(S.coord, &rec);
    if (frb_segment_close(S.coord) != FRB_OK || e2 != FRB_OK) {
        if (e == FRB_OK) {
            set_err(err, errlen, "cannot write to the journal (disk full?)");
            e = FRB_ERR_IO;
        }
    }
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    g_deadline_ns = 0;
    frb_state_free(state);
    close(lock_fd); /* releases the lock, unless workers are still alive */
    return e;
}

/* ---- finalization and deep checks --------------------------------------- */

FrbError frb_work_finalize(const char *dir, const char *output, int allow_partial,
                           FrbSummary *summary, char *err, size_t errlen) {
    FrbConfig cfg;
    FrbState *state = NULL;
    FrbTable table, back;
    FrbSummary local;
    FrbError e;
    if (!dir || !output) return FRB_ERR_ARG;
    if (!summary) summary = &local;
    memset(summary, 0, sizeof *summary);
    e = frb_config_read(dir, &cfg, err, errlen);
    if (e != FRB_OK) return e;
    e = frb_state_load(dir, &cfg, &state, err, errlen);
    if (e != FRB_OK) return e;
    e = first_conflict(state, err, errlen);
    if (e != FRB_OK) {
        frb_state_free(state);
        return e;
    }
    e = frb_state_table(state, &table);
    frb_state_free(state);
    if (e != FRB_OK) {
        set_err(err, errlen, "the journal does not amount to a valid table: %s", frb_error_str(e));
        return e;
    }
    e = frb_table_validate(&table, summary, NULL);
    if (e == FRB_OK && !summary->complete && !allow_partial) {
        set_err(err, errlen,
                "%u of %u targets have no certified minimum; a complete table cannot be written",
                (unsigned)(summary->count - summary->by_status[FRB_MINIMAL]),
                (unsigned)summary->count);
        e = FRB_ERR_INCOMPLETE;
    }
    if (e == FRB_OK) {
        e = frb_file_write(output, &table);
        if (e != FRB_OK) set_err(err, errlen, "cannot write %s", output);
    }
    if (e == FRB_OK) { /* read it back as a stranger would */
        e = frb_file_read(output, &back, NULL, NULL);
        if (e == FRB_OK) {
            if (back.count != table.count ||
                memcmp(back.entries, table.entries, (size_t)table.count * FRB_ENTRY_BYTES) != 0)
                e = FRB_ERR_IO;
            frb_table_destroy(&back);
        }
        if (e != FRB_OK) set_err(err, errlen, "%s does not read back as written", output);
    }
    frb_table_destroy(&table);
    return e;
}

FrbError frb_verify_evidence(const FrbTable *table, const FrbState *state, uint32_t *bad_index,
                             char *err, size_t errlen) {
    char target[FRB_MAX_BITS + 1];
    if (!table || !state) return FRB_ERR_ARG;
    if (table->bits != state->bits) {
        set_err(err, errlen, "the table is for %d-bit targets, the work directory for %d-bit ones",
                table->bits, state->bits);
        return FRB_ERR_INCOMPATIBLE;
    }
    for (uint32_t i = 0; i < table->count; i++) {
        const FrbTargetState *ts = &state->targets[i];
        RevGraph g;
        FrbStatus st;
        FrbError e = frb_table_get(table, i, &st, &g);
        if (e != FRB_OK) {
            if (bad_index) *bad_index = i;
            return e;
        }
        if (st != FRB_MINIMAL) continue;
        if (ts->status != FRB_MINIMAL || ts->nodes != g.node_count) {
            frb_target_format(i, table->bits, target);
            if (bad_index) *bad_index = i;
            if (ts->status == FRB_MINIMAL)
                set_err(err, errlen, "%s: the table certifies %d nodes, the journal certifies %d",
                        target, g.node_count, ts->nodes);
            else
                set_err(err, errlen,
                        "%s: the table certifies %d nodes, the journal certifies nothing "
                        "(budgets below %d are excluded, best witness has %d nodes)",
                        target, g.node_count, ts->lower_bound, ts->nodes);
            return FRB_ERR_EVIDENCE;
        }
    }
    return FRB_OK;
}

FrbError frb_recertify(const FrbTable *table, RevLevelSolveFn solve, uint64_t max_steps,
                       uint64_t sample, uint64_t seed, FrbRecertify *out) {
    RevLevelOptions options = {max_steps};
    char target[FRB_MAX_BITS + 1];
    uint32_t *order;
    uint64_t state = seed, todo;
    if (!table || !table->entries || !solve || !out) return FRB_ERR_ARG;
    memset(out, 0, sizeof *out);
    order = malloc((size_t)table->count * sizeof *order);
    if (!order) return FRB_ERR_MEMORY;
    for (uint32_t i = 0; i < table->count; i++) order[i] = i;
    todo = table->count;
    if (sample && sample < todo) {
        todo = sample;
        for (uint32_t i = 0; i < todo; i++) {
            uint32_t j = i + (uint32_t)(prng_next(&state) % (uint64_t)(table->count - i));
            uint32_t tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
    }
    for (uint64_t k = 0; k < todo; k++) {
        uint32_t i = order[k];
        RevMinimalInfo info;
        RevGraph g, mine;
        FrbStatus st;
        RevMinimalStatus ms;
        FrbError e = frb_table_get(table, i, &st, &g);
        if (e != FRB_OK) {
            free(order);
            out->first_bad = i;
            return e;
        }
        frb_target_format(i, table->bits, target);
        ms = rev_minimal_solve_with(solve, target, (size_t)table->bits, FRB_NODE_CAPACITY, &options,
                                    &mine, &info);
        out->checked++;
        out->steps += info.steps;
        if (ms == REV_MIN_UNKNOWN) {
            out->undecided++;
        } else if (st == FRB_MINIMAL) {
            if (ms == REV_MIN_MINIMAL && info.node_count == g.node_count) out->agree++;
            else if (out->disagree++ == 0) out->first_bad = i;
        } else if (st == FRB_WITNESS) {
            if (ms != REV_MIN_MINIMAL || info.node_count > g.node_count) {
                if (out->disagree++ == 0) out->first_bad = i; /* a witness below the minimum */
            } else if (info.node_count < g.node_count) {
                out->tightened++;
            }
        }
    }
    free(order);
    return out->disagree ? FRB_ERR_EVIDENCE : FRB_OK;
}
