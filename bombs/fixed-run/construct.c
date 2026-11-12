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
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* MAP_ANONYMOUS */
#endif

#include "fixed_run_build.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "level_solver.h"

/* First line of config.txt.  Layout 1 is what a construction of up to 16
 * bits without a base Bomb has always written, and still does; layout 2 adds
 * the table format and the base Bomb. */
#define CONFIG_MAGIC_1 "carlobomb-fixed-run-work 1"
#define CONFIG_MAGIC_2 "carlobomb-fixed-run-work 2"

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
    const FrbFormat *format = frb_format_for_bits(bits);
    memset(cfg, 0, sizeof *cfg);
    cfg->bits = bits;
    cfg->format = format ? format->version : FRB_FORMAT_VERSION;
    cfg->symmetry = 1;
    cfg->enumerate_max = 5;
    cfg->max_nodes = format ? format->node_capacity : FRB_NODE_CAPACITY;
    cfg->fallback_ring = 1;
    cfg->workers = 1;
    cfg->passes = 1;
    cfg->max_steps[0] = 0;
    strcpy(cfg->engine, "c");
}

FrbError frb_config_check(const FrbConfig *cfg, char *err, size_t errlen) {
    const FrbFormat *format;
    if (!cfg) return FRB_ERR_ARG;
    if (cfg->bits < 1 || cfg->bits > FRB_MAX_BITS) {
        set_err(err, errlen, "bits must be 1..%d", FRB_MAX_BITS);
        return FRB_ERR_ARG;
    }
    format = frb_format(cfg->format);
    if (!format || cfg->bits > format->max_bits) {
        set_err(err, errlen, "table format %d cannot hold %d-bit targets", cfg->format, cfg->bits);
        return FRB_ERR_ARG;
    }
    if (cfg->max_nodes < 1 || cfg->max_nodes > format->node_capacity) {
        set_err(err, errlen, "max-nodes must be 1..%d (the capacity of an entry)",
                format->node_capacity);
        return FRB_ERR_ARG;
    }
    if (cfg->enumerate_max < 0 || cfg->enumerate_max > FRB_ENUMERATE_LIMIT) {
        set_err(err, errlen, "enumerate-max must be 0..%d", FRB_ENUMERATE_LIMIT);
        return FRB_ERR_ARG;
    }
    if (cfg->base_bits < 0 || cfg->base_bits >= cfg->bits) {
        set_err(err, errlen, "a base Bomb must be for fewer bits than the %d of the construction",
                cfg->bits);
        return FRB_ERR_ARG;
    }
    if (cfg->base_file[sizeof cfg->base_file - 1] != '\0' || strpbrk(cfg->base_file, "\r\n") ||
        (cfg->base_bits != 0) != (cfg->base_file[0] != '\0')) {
        set_err(err, errlen, "bad base Bomb file name");
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

/* `file` as a path that does not depend on the current directory. */
static int absolute_path(const char *file, char *out, size_t cap) {
    char cwd[2048];
    int n;
    if (file[0] == '/') n = snprintf(out, cap, "%s", file);
    else if (!getcwd(cwd, sizeof cwd)) return 0;
    else n = snprintf(out, cap, "%s/%s", cwd, file);
    return n > 0 && (size_t)n < cap && !strpbrk(out, "\r\n");
}

FrbError frb_config_set_base(FrbConfig *cfg, const char *file, char *err, size_t errlen) {
    FrbFileInfo info;
    FrbError e;
    if (!cfg || !file) return FRB_ERR_ARG;
    e = frb_file_info(file, &info);
    if (e != FRB_OK) {
        set_err(err, errlen, "cannot build on %s: %s", file, frb_error_str(e));
        return e;
    }
    if (!info.complete) {
        set_err(err, errlen, "%s is not a complete Bomb: nothing can be built on it", file);
        return FRB_ERR_INCOMPLETE;
    }
    if (info.bits >= cfg->bits) {
        set_err(err, errlen, "%s is a Bomb for %d-bit targets; a construction for %d bits needs a shorter one",
                file, info.bits, cfg->bits);
        return FRB_ERR_ARG;
    }
    if (!absolute_path(file, cfg->base_file, sizeof cfg->base_file)) {
        cfg->base_file[0] = '\0';
        set_err(err, errlen, "the file name of the base Bomb is too long");
        return FRB_ERR_ARG;
    }
    cfg->base_bits = info.bits;
    cfg->base_crc = info.table_crc;
    cfg->enumerate_max = 0; /* the base supplies the lower bounds */
    return FRB_OK;
}

static int config_path(char *buf, size_t cap, const char *dir, const char *name) {
    int n = snprintf(buf, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

FrbError frb_config_write(const char *dir, const FrbConfig *cfg) {
    char path[4096], text[4096];
    int layout2, n;
    if (!dir || frb_config_check(cfg, NULL, 0) != FRB_OK) return FRB_ERR_ARG;
    if (!config_path(path, sizeof path, dir, "config.txt")) return FRB_ERR_ARG;
    layout2 = cfg->format != FRB_FORMAT_VERSION || cfg->base_bits != 0;
    n = snprintf(text, sizeof text,
                 "%s\n"
                 "# Settings of a FixedRunBomb construction.  The first five are fixed for\n"
                 "# the life of this directory; the rest may be changed on resume.\n"
                 "journal_version %d\n"
                 "search_revision %d\n"
                 "bits %d\n"
                 "symmetry %d\n"
                 "enumerate_max %d\n",
                 layout2 ? CONFIG_MAGIC_2 : CONFIG_MAGIC_1, cfg->format, FRB_SEARCH_REVISION,
                 cfg->bits, cfg->symmetry ? 1 : 0, cfg->enumerate_max);
    if (layout2 && n > 0 && (size_t)n < sizeof text) {
        n += snprintf(text + n, sizeof text - (size_t)n,
                      "# Also fixed: the table format, and the complete Bomb for fewer bits this\n"
                      "# construction builds on (0 = none), identified by its table checksum.\n"
                      "# base_file says where that Bomb is and may be corrected if it moves.\n"
                      "format %d\n"
                      "base_bits %d\n",
                      cfg->format, cfg->base_bits);
        if (cfg->base_bits && n > 0 && (size_t)n < sizeof text)
            n += snprintf(text + n, sizeof text - (size_t)n, "base_crc %08x\nbase_file %s\n",
                          (unsigned)cfg->base_crc, cfg->base_file);
    }
    if (n > 0 && (size_t)n < sizeof text)
        n += snprintf(text + n, sizeof text - (size_t)n,
                      "max_nodes %d\n"
                      "fallback %s\n"
                      "engine %s\n"
                      "workers %d\n"
                      "max_steps ",
                      cfg->max_nodes, cfg->fallback_ring ? "ring" : "none", cfg->engine, cfg->workers);
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
    char path[4096], line[2048], key[64], value[256];
    unsigned seen = 0, want;
    int version = -1, revision = -1, lineno = 0, layout = 0;
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
            if (strcmp(line, CONFIG_MAGIC_1) == 0) layout = 1;
            else if (strcmp(line, CONFIG_MAGIC_2) == 0) layout = 2;
            else {
                fclose(f);
                set_err(err, errlen, "%s: not a FixedRunBomb work directory of a known version", path);
                return FRB_ERR_INCOMPATIBLE;
            }
            cfg->format = FRB_FORMAT_VERSION; /* layout 1 has no other */
            continue;
        }
        if (line[0] == '#' || line[0] == '\0') continue;
        if (layout == 2 && strncmp(line, "base_file ", 10) == 0) { /* may contain spaces */
            if (n - 10 >= sizeof cfg->base_file || n == 10) goto bad;
            memcpy(cfg->base_file, line + 10, n - 10 + 1);
            seen |= 8192u;
            continue;
        }
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
        else if (layout == 2 && !strcmp(key, "format")) { if (!parse_int(value, &cfg->format)) goto bad; seen |= 1024u; }
        else if (layout == 2 && !strcmp(key, "base_bits")) { if (!parse_int(value, &cfg->base_bits)) goto bad; seen |= 2048u; }
        else if (layout == 2 && !strcmp(key, "base_crc")) {
            char *end;
            unsigned long crc = strtoul(value, &end, 16);
            if (end == value || *end != '\0' || strlen(value) != 8) goto bad;
            cfg->base_crc = (uint32_t)crc;
            seen |= 4096u;
        }
        else goto bad;
    }
    fclose(f);
    want = 1023u;
    if (layout == 2) want |= 1024u | 2048u | (cfg->base_bits ? 4096u | 8192u : 0u);
    if (seen != want) {
        set_err(err, errlen, "%s: incomplete (a setting is missing, or one too many)", path);
        return FRB_ERR_INCOMPATIBLE;
    }
    if (!frb_format(cfg->format) || version != cfg->format) {
        set_err(err, errlen, "%s: journal format version %d and table format %d; this program reads "
                             "versions 1 and 2 of both, and they must agree",
                path, version, cfg->format);
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

/* 1: asked to stop.  2: the time limit passed.  3: the disk is nearly full. */
static volatile sig_atomic_t g_stop;
static uint64_t g_deadline_ns; /* 0 = none */

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
    size_t n = 0, cap = 0;
    Job *list = NULL;
    *jobs = NULL;
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
        if (n == cap) { /* grows with the work there is, not with the table */
            size_t grown_cap = cap ? cap * 2 : 4096;
            Job *grown = realloc(list, grown_cap * sizeof *list);
            if (!grown) {
                free(list);
                return 0;
            }
            list = grown;
            cap = grown_cap;
        }
        memset(&list[n], 0, sizeof list[n]);
        list[n].kind = JOB_SEARCH;
        list[n].lo = (uint8_t)lo;
        list[n].hi = (uint8_t)hi;
        list[n].target = i;
        n++;
    }
    if (!list) list = calloc(1, sizeof *list); /* "nothing to do" is not "out of memory" */
    *jobs = list;
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
    uint64_t found_jobs;               /* search jobs that ended with a REV: a certified minimum */
    uint32_t found[FRB_MAX_NODES + 1]; /* those, by node count */
} Progress;

static void progress_add(Progress *total, const Progress *p) {
    total->jobs += p->jobs;
    total->searches += p->searches;
    total->steps += p->steps;
    total->graphs += p->graphs;
    total->found_jobs += p->found_jobs;
    for (int n = 0; n <= FRB_MAX_NODES; n++) total->found[n] += p->found[n];
}

/*
 * How the jobs of a round are shared out.  Every worker takes the next few
 * jobs nobody has taken, from a counter in memory all of them share, until
 * there are none left.  A slow processor core therefore does fewer jobs
 * instead of holding the round up, and which worker ran which job has no
 * bearing on the result: a job's records depend on the job alone.
 */
typedef struct {
    atomic_uint_fast64_t next;
} Cursor;

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
    const FrbFormat *format;
    FrbSegment *coord; /* the coordinator's own journal segment */
    uint32_t number;   /* session number */
    uint32_t next_segment;
    uint64_t start_ns;
    uint64_t last_disk_ns;
    /* the round in progress, for progress lines */
    int round_kind;
    size_t round_jobs;
    uint64_t round_start_ns, last_progress_ns;
    uint64_t certified_before; /* targets with a certified minimum when the round was planned */
    uint64_t per_job;          /* targets one search job settles: 2 under symmetry */
    char round_label[96];
};

/* 1234567 -> "1,234,567", in one of a few static buffers used in turn. */
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

static const char *duration(double seconds) {
    static char buf[64];
    if (seconds < 120) snprintf(buf, sizeof buf, "%.0f s", seconds);
    else
        snprintf(buf, sizeof buf, "%.0f s (%d h %02d min)", seconds, (int)(seconds / 3600),
                 (int)(seconds / 60) % 60);
    return buf;
}

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
        fprintf(log, ", about %s left\n",
                duration(rt * (double)(S->round_jobs - p->jobs) / (double)p->jobs));
    else fprintf(log, "\n");
    if (S->round_kind == JOB_SEARCH && p->found_jobs) {
        uint64_t all = (uint64_t)1 << S->cfg->bits;
        uint64_t certified = S->certified_before + S->per_job * p->found_jobs;
        fprintf(log, "             certified: %s of %s strings (%.2f%%), %s open.  Minimum nodes of those\n"
                     "             settled in this round:",
                commas(certified), commas(all), 100.0 * (double)certified / (double)all,
                commas(all - certified));
        for (int n = 1; n <= FRB_MAX_NODES; n++)
            if (p->found[n]) fprintf(log, "  %d: %s", n, commas(S->per_job * p->found[n]));
        fprintf(log, "\n");
    }
    fflush(log);
}

/* Free space left for the journal, against the limit asked for. */
static void watch_disk(Session *S) {
    struct statvfs v;
    uint64_t now = now_ns(), free_bytes;
    if (!S->opt->min_free_bytes || g_stop) return;
    if (S->last_disk_ns && now - S->last_disk_ns < 2000000000ull) return;
    S->last_disk_ns = now;
    if (statvfs(S->dir, &v) != 0) return;
    free_bytes = (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
    if (free_bytes >= S->opt->min_free_bytes) return;
    g_stop = 3;
    if (S->opt->log) {
        fprintf(S->opt->log,
                "Only %s bytes are free where %s is (the limit is %s): stopping cleanly.\n"
                "Everything recorded so far is kept; free some space and run the same command again.\n",
                commas(free_bytes), S->dir, commas(S->opt->min_free_bytes));
        fflush(S->opt->log);
    }
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
            watch_disk(w->session);
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
    c->error = frb_graph_pack_as(c->w->session->format, g, rec.graph);
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
                frb_graph_pack_as(w->session->format, &g, rec.graph) != FRB_OK) {
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
        if (r == REV_SOLVE_SOLVED) {
            /* Every smaller budget is excluded: by what was known when the
             * job was planned, and by the searches just made. */
            w->p.found_jobs++;
            w->p.found[rec.nodes]++;
        }
        if (r != REV_SOLVE_PROVEN_UNSAT) break;
    }
    return FRB_OK;
}

/* One job; 1 if the worker is to go on. */
static int run_job(Worker *w, const Job *job, FrbError *e) {
    if (should_stop()) {
        w->p.state = 2;
        return 0;
    }
    *e = job->kind == JOB_ENUM ? do_enum(w, job) : do_search(w, job);
    if (*e == FRB_ERR_STOPPED) {
        *e = FRB_OK;
        w->p.state = 2;
        return 0;
    }
    if (*e != FRB_OK) return 0;
    w->p.jobs++;
    *e = housekeeping(w, 0);
    return *e == FRB_OK;
}

/* The worker's share of the round: `chunk` jobs at a time from the shared
 * cursor, or, without one, jobs first, first + stride, ... */
static FrbError run_jobs(Worker *w, const Job *jobs, size_t count, Cursor *cursor, size_t chunk,
                         size_t first, size_t stride) {
    FrbError e = FRB_OK, e2;
    int go = 1;
    if (cursor) {
        while (go) {
            uint64_t from = atomic_fetch_add(&cursor->next, (uint_fast64_t)chunk);
            if (from >= count) break;
            for (uint64_t i = from; go && i < from + chunk && i < count; i++)
                go = run_job(w, &jobs[i], &e);
        }
    } else {
        for (size_t i = first; go && i < count; i += stride) go = run_job(w, &jobs[i], &e);
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
                       int workers, Cursor *cursor, size_t chunk, uint32_t segment, int fd) {
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
    e = run_jobs(&w, jobs, count, cursor, chunk, (size_t)k, (size_t)workers);
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
    Cursor *cursor = NULL;
    size_t chunk = 1;
    int fds[2], started = 0, forwarded = 0, failed = 0;
    FrbError e = FRB_OK;
    if (!latest || !pids || pipe(fds) != 0) {
        free(latest);
        free(pids);
        set_err(err, errlen, "cannot start worker processes");
        return FRB_ERR_IO;
    }
#if defined(MAP_ANONYMOUS) && ATOMIC_LLONG_LOCK_FREE == 2
    {
        void *shared = mmap(NULL, sizeof *cursor, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                            -1, 0);
        if (shared != MAP_FAILED) {
            cursor = shared;
            atomic_init(&cursor->next, 0);
        }
    }
#endif
    if (S->round_kind == JOB_SEARCH) { /* small against the round, large against the hand-out */
        chunk = count / ((size_t)workers * 64u);
        if (chunk < 1) chunk = 1;
        if (chunk > 32) chunk = 32;
    }
    frb_segment_flush(S->coord, 0);
    fflush(NULL); /* nothing buffered may be written twice */
    for (int k = 0; k < workers; k++) {
        uint32_t segment = S->next_segment++;
        pid_t pid = fork();
        if (pid < 0) break; /* go on with the workers there are; the rest is replanned */
        if (pid == 0) {
            close(fds[0]);
            child_main(S, jobs, count, max_steps, k, workers, cursor, chunk, segment, fds[1]);
        }
        pids[started++] = pid;
    }
    close(fds[1]);
    if (started == 0) {
        close(fds[0]);
        free(latest);
        free(pids);
        if (cursor) munmap(cursor, sizeof *cursor);
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
        watch_disk(S);
        if (should_stop() && !forwarded) { /* a signal sent to this process only, or a limit */
            for (int k = 0; k < started; k++) kill(pids[k], SIGTERM);
            forwarded = 1;
        }
        memset(total, 0, sizeof *total);
        for (int k = 0; k < workers; k++) progress_add(total, &latest[k]);
        progress_line(S, total, 0);
    }
    close(fds[0]);

    for (int k = 0; k < started; k++) {
        int status = 0;
        while (waitpid(pids[k], &status, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) failed++;
    }
    memset(total, 0, sizeof *total);
    for (int k = 0; k < workers; k++) progress_add(total, &latest[k]);
    if (failed) {
        set_err(err, errlen, "%d worker process%s failed; what they had recorded is kept", failed,
                failed == 1 ? "" : "es");
        e = FRB_ERR_IO;
    }
    if (cursor) munmap(cursor, sizeof *cursor);
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
        e = run_jobs(&w, jobs, count, NULL, 1, 0, 1);
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

/*
 * A number that changes whenever a round has achieved anything: a part
 * enumerated, a witness found, a budget excluded, a limit recorded at a new
 * budget or with a new size.  A round that leaves it unchanged would be
 * planned again unchanged, so the session stops instead of looping.
 */
static uint64_t progress_mark(const FrbState *s) {
    uint64_t mark = 0;
    for (int n = 1; n <= FRB_NODE_CAPACITY; n++) mark += s->parts_done[n];
    for (uint32_t i = 0; i < s->count; i++) {
        const FrbTargetState *ts = &s->targets[i];
        mark = mark * 0x100000001B3ull + ts->lower_bound + ((uint64_t)ts->nodes << 8) +
               ((uint64_t)ts->limit_budget << 16) + ((uint64_t)ts->limit_kind << 24) +
               (ts->limit_max_steps << 32) + (ts->limit_max_steps >> 32);
    }
    return mark;
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

/* frb_work_run.  If final_state is not NULL and the call succeeds, it
 * receives the state as the last replay left it, for the caller to free. */
static FrbError work_run(const char *dir, const FrbConfig *cfg, const FrbRunOptions *opt,
                         FrbRunResult *result, FrbState **final_state, char *err, size_t errlen) {
    struct sigaction action, old_int, old_term;
    FrbRunResult local;
    FrbState *state = NULL;
    FrbRecord rec;
    Session S;
    uint64_t jobs_left, sample_left;
    int lock_fd, sampling;
    FrbError e, e2;

    if (final_state) *final_state = NULL;
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
    S.format = frb_format(cfg->format);
    S.per_job = cfg->symmetry ? 2 : 1;
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

    if (opt->log && cfg->base_bits) {
        fprintf(opt->log,
                "FixedRunBomb(%d) on the complete %d-bit Bomb: %s of %s strings are settled by its REVs\n"
                "without search; %s have a certified minimum now, %s are open.\n",
                cfg->bits, cfg->base_bits, commas(state->base_settled), commas(state->count),
                commas(state->by_status[FRB_MINIMAL]),
                commas(state->count - state->by_status[FRB_MINIMAL]));
        fflush(opt->log);
    }

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
        uint64_t mark = progress_mark(state);
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
        watch_disk(&S);
        if (should_stop()) {
            free(jobs);
            break;
        }

        /* The workers need the jobs and nothing else: the state, which is
         * most of the memory of a large construction, goes before they are
         * started and is replayed from the journal after they are done. */
        S.certified_before = state->by_status[FRB_MINIMAL];
        frb_state_free(state);
        state = NULL;

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
        e = frb_state_load(dir, cfg, &state, err, errlen);
        if (e != FRB_OK) break;
        if (!should_stop() && (total.jobs == 0 || progress_mark(state) == mark)) {
            set_err(err, errlen, "a round of %llu jobs changed nothing; stopping rather than repeat it",
                    (unsigned long long)total.jobs);
            e = FRB_ERR_EVIDENCE;
        }
    }
    if (e == FRB_OK && state) e = first_conflict(state, err, errlen);

    result->interrupted = g_stop == 1;
    if (g_stop == 2) result->stopped_early = 1;
    if (g_stop == 3) result->disk_low = 1;
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
    if (e == FRB_OK && final_state) *final_state = state;
    else frb_state_free(state);
    close(lock_fd); /* releases the lock, unless workers are still alive */
    return e;
}

FrbError frb_work_run(const char *dir, const FrbConfig *cfg, const FrbRunOptions *opt,
                      FrbRunResult *result, char *err, size_t errlen) {
    return work_run(dir, cfg, opt, result, NULL, err, errlen);
}

/* ---- finalization and deep checks --------------------------------------- */

/* The table a state amounts to, validated, written and read back.  Frees
 * the state as soon as the table has been made from it. */
static FrbError finalize_state(FrbState *state, const char *output, int allow_partial,
                               FrbSummary *summary, char *err, size_t errlen) {
    FrbTable table, back;
    FrbError e = first_conflict(state, err, errlen);
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
            if (back.count != table.count || back.format != table.format ||
                memcmp(back.entries, table.entries,
                       (size_t)table.count * (size_t)table.format->entry_bytes) != 0)
                e = FRB_ERR_IO;
            frb_table_destroy(&back);
        }
        if (e != FRB_OK) set_err(err, errlen, "%s does not read back as written", output);
    }
    frb_table_destroy(&table);
    return e;
}

FrbError frb_work_finalize(const char *dir, const char *output, int allow_partial,
                           FrbSummary *summary, char *err, size_t errlen) {
    FrbConfig cfg;
    FrbState *state = NULL;
    FrbSummary local;
    FrbError e;
    if (!dir || !output) return FRB_ERR_ARG;
    if (!summary) summary = &local;
    memset(summary, 0, sizeof *summary);
    e = frb_config_read(dir, &cfg, err, errlen);
    if (e != FRB_OK) return e;
    e = frb_state_load(dir, &cfg, &state, err, errlen);
    if (e != FRB_OK) return e;
    return finalize_state(state, output, allow_partial, summary, err, errlen);
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
        ms = rev_minimal_solve_with(solve, target, (size_t)table->bits, table->format->node_capacity,
                                    &options, &mine, &info);
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

/* ---- the ladder --------------------------------------------------------- */

static int ladder_file(char *buf, size_t cap, const char *dir, int bits) {
    int n = snprintf(buf, cap, "%s/fixed-run-%02d.frb", dir, bits);
    return n > 0 && (size_t)n < cap;
}

static int ladder_work(char *buf, size_t cap, const char *dir, int bits) {
    int n = snprintf(buf, cap, "%s/work-%02d", dir, bits);
    return n > 0 && (size_t)n < cap;
}

/* The longest Bomb in the directory; 0 if there is none. */
static FrbError ladder_top(const char *dir, int *top, char *err, size_t errlen) {
    char path[4096];
    *top = 0;
    for (int bits = FRB_MAX_BITS; bits >= 1; bits--) {
        FrbFileInfo info;
        struct stat st;
        FrbError e;
        if (!ladder_file(path, sizeof path, dir, bits)) return FRB_ERR_ARG;
        if (stat(path, &st) != 0) continue;
        e = frb_file_info(path, &info);
        if (e != FRB_OK || !info.complete || info.bits != bits) {
            set_err(err, errlen,
                    "%s is not a complete %d-bit Bomb (%s); move it out of the way and run again to "
                    "rebuild that level",
                    path, bits, e != FRB_OK ? frb_error_str(e) : "incomplete, or of another length");
            return e != FRB_OK ? e : FRB_ERR_INCOMPATIBLE;
        }
        *top = bits;
        return FRB_OK;
    }
    return FRB_OK;
}

/* Validate the starting Bomb in full and copy it into the directory. */
static FrbError ladder_start(const char *dir, const char *from_file, int *top, FILE *log, char *err,
                             size_t errlen) {
    char path[4096];
    FrbFileInfo info;
    FrbTable table;
    uint32_t bad = 0;
    FrbError e;
    if (!from_file) {
        set_err(err, errlen, "%s holds no Bomb to climb from; name a complete one with --from", dir);
        return FRB_ERR_ARG;
    }
    e = frb_file_read(from_file, &table, &info, &bad);
    if (e != FRB_OK) {
        set_err(err, errlen, "cannot start from %s: %s", from_file, frb_error_str(e));
        return e;
    }
    if (!info.complete) {
        frb_table_destroy(&table);
        set_err(err, errlen, "%s is not a complete Bomb: nothing can be built on it", from_file);
        return FRB_ERR_INCOMPLETE;
    }
    if (!ladder_file(path, sizeof path, dir, info.bits)) {
        frb_table_destroy(&table);
        return FRB_ERR_ARG;
    }
    e = frb_file_write(path, &table);
    frb_table_destroy(&table);
    if (e != FRB_OK) {
        set_err(err, errlen, "cannot write %s", path);
        return e;
    }
    if (log) {
        fprintf(log, "Starting from %s: a complete %d-bit Bomb, every REV run again; copied to %s\n",
                from_file, info.bits, path);
        fflush(log);
    }
    *top = info.bits;
    return FRB_OK;
}

/* ceil(2 * sqrt(L)) - 1, in integers. */
static int formula_nodes(int bits) {
    int k = 0;
    while (k * k < 4 * bits) k++;
    return k - 1;
}

typedef struct {
    uint64_t searches, steps, solver_ns, session_ns, journal_bytes, journal_records;
    uint32_t sessions, base_settled;
} LevelCost;

static void level_report(FILE *out, int bits, const char *file, const FrbSummary *sum,
                         const LevelCost *cost, int nodes_of_one, uint32_t table_crc) {
    int max = sum->max_minimal_nodes, formula = formula_nodes(bits);
    fprintf(out, "\n=== FixedRunBomb(%d) is complete: %s ===\n\n", bits, file);
    frb_report_summary(out, sum, bits);
    fprintf(out, "\nStrings that need the maximum of %d nodes: %s\n", max,
            commas(sum->minimal_by_nodes[max]));
    fprintf(out, "ceil(2*sqrt(%d)) - 1 = %d: the maximum %s\n", bits, formula,
            max == formula ? "equals it" : "DOES NOT equal it");
    fprintf(out, "0...01 needs %d nodes: %s\n", nodes_of_one,
            nodes_of_one == max ? "it is among the hardest strings" : "it is NOT among the hardest strings");
    fprintf(out, "\nConstruction of this level\n");
    fprintf(out, "  settled by the REVs of the Bomb below   %s strings\n", commas(cost->base_settled));
    fprintf(out, "  searches                                %s, %s logical steps\n",
            commas(cost->searches), commas(cost->steps));
    fprintf(out, "  solver time, all workers                %.1f s\n", (double)cost->solver_ns * 1e-9);
    fprintf(out, "  wall-clock time                         %.1f s in %u session(s)\n",
            (double)cost->session_ns * 1e-9, (unsigned)cost->sessions);
    fprintf(out, "  journal                                 %s bytes, %s records\n",
            commas(cost->journal_bytes), commas(cost->journal_records));
    fprintf(out, "  table checksum (CRC-32 in the header)   %08x\n", (unsigned)table_crc);
}

/* One level.  *done is set if it is complete and its Bomb written. */
static FrbError ladder_level(const char *dir, int level, const FrbConfig *settings,
                             const FrbRunOptions *opt, FrbRunResult *run, LevelCost *cost, int *done,
                             char *err, size_t errlen) {
    char base[4096], work[4096], out[4096], path[4096];
    const FrbFormat *format;
    FrbConfig cfg;
    FrbFileInfo info;
    FrbState *state = NULL;
    FrbSummary summary;
    FrbStatus status;
    RevGraph one;
    struct stat st;
    FrbError e;
    int fresh;

    *done = 0;
    memset(cost, 0, sizeof *cost);
    if (!ladder_file(base, sizeof base, dir, level - 1) || !ladder_file(out, sizeof out, dir, level) ||
        !ladder_work(work, sizeof work, dir, level) || !config_path(path, sizeof path, work, "config.txt"))
        return FRB_ERR_ARG;
    fresh = stat(path, &st) != 0;
    if (fresh) {
        frb_config_default(&cfg, level);
        format = frb_format(settings->format);
        if (format && level <= format->max_bits) { /* a format asked for, where it can hold the level */
            cfg.format = format->version;
            cfg.max_nodes = format->node_capacity;
        }
        cfg.symmetry = settings->symmetry ? 1 : 0;
        e = frb_config_set_base(&cfg, base, err, errlen);
        if (e != FRB_OK) return e;
    } else {
        e = frb_config_read(work, &cfg, err, errlen);
        if (e != FRB_OK) return e;
        e = frb_file_info(base, &info);
        if (e != FRB_OK) {
            set_err(err, errlen, "cannot read %s: %s", base, frb_error_str(e));
            return e;
        }
        if (cfg.bits != level || cfg.base_bits != level - 1 || cfg.base_crc != info.table_crc) {
            set_err(err, errlen,
                    "%s was not started on the Bomb that is now %s; move it out of the way to build "
                    "level %d again from that one",
                    work, base, level);
            return FRB_ERR_INCOMPATIBLE;
        }
        if (!absolute_path(base, cfg.base_file, sizeof cfg.base_file)) return FRB_ERR_ARG;
    }
    format = frb_format(cfg.format);
    /* What may differ from run to run. */
    cfg.workers = settings->workers;
    cfg.fallback_ring = settings->fallback_ring;
    cfg.passes = settings->passes;
    memcpy(cfg.max_steps, settings->max_steps, sizeof cfg.max_steps);
    memcpy(cfg.engine, settings->engine, sizeof cfg.engine);
    cfg.max_nodes = settings->max_nodes > 0 && settings->max_nodes < format->node_capacity
                        ? settings->max_nodes
                        : format->node_capacity;
    e = frb_config_check(&cfg, err, errlen);
    if (e != FRB_OK) return e;
    if (fresh) {
        e = frb_work_create(work, &cfg, err, errlen);
    } else {
        e = frb_config_write(work, &cfg);
        if (e != FRB_OK) set_err(err, errlen, "cannot write %s", path);
    }
    if (e != FRB_OK) return e;

    if (opt->log) {
        fprintf(opt->log, "\n--- Level %d: %s strings, in %s%s ---\n", level,
                commas((uint64_t)1 << level), work, fresh ? "" : " (continued)");
        fflush(opt->log);
    }
    e = work_run(work, &cfg, opt, run, &state, err, errlen);
    if (e != FRB_OK) return e;
    if (!run->complete) {
        frb_state_free(state);
        return FRB_OK;
    }
    for (int b = 1; b <= FRB_MAX_NODES; b++) {
        cost->searches += state->budget[b].searches;
        cost->steps += state->budget[b].steps;
        cost->solver_ns += state->budget[b].ns;
    }
    cost->sessions = state->sessions;
    /* The session that just ended is in the state as begun, not as ended. */
    cost->session_ns = state->session_ns + (uint64_t)(run->seconds * 1e9);
    cost->journal_bytes = state->journal.bytes;
    cost->journal_records = state->journal.records;
    cost->base_settled = state->base_settled;

    e = finalize_state(state, out, 0, &summary, err, errlen); /* frees the state */
    if (e != FRB_OK) return e;
    e = frb_file_lookup(out, 1, &info, &status, &one); /* the string 0...01 */
    if (e != FRB_OK) {
        set_err(err, errlen, "%s does not read back as written", out);
        return e;
    }
    if (opt->log) {
        level_report(opt->log, level, out, &summary, cost, one.node_count, info.table_crc);
        fflush(opt->log);
    }
    if (snprintf(path, sizeof path, "%s/level-%02d.txt", dir, level) < (int)sizeof path) {
        FILE *f = fopen(path, "w"); /* the same report, to keep */
        if (f) {
            level_report(f, level, out, &summary, cost, one.node_count, info.table_crc);
            fclose(f);
        }
    }
    *done = 1;
    return FRB_OK;
}

FrbError frb_ladder_run(const char *dir, const char *from_file, int to_bits, const FrbConfig *settings,
                        const FrbRunOptions *opt, FrbLadderResult *result, char *err, size_t errlen) {
    FrbLadderResult local;
    FrbRunOptions level_opt;
    uint64_t start = now_ns(), prev_steps = 0;
    int top = 0, lock_fd;
    FrbError e;

    if (!result) result = &local;
    memset(result, 0, sizeof *result);
    result->to_bits = to_bits;
    if (!dir || !settings || !opt || !opt->solve) return FRB_ERR_ARG;
    if (to_bits < 2 || to_bits > FRB_MAX_BITS) {
        set_err(err, errlen, "the ladder climbs to 2..%d bits", FRB_MAX_BITS);
        return FRB_ERR_ARG;
    }
    if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
        set_err(err, errlen, "cannot create directory %s", dir);
        return FRB_ERR_IO;
    }
    lock_fd = lock_dir(dir, err, errlen, &e);
    if (lock_fd < 0) return e;
    e = ladder_top(dir, &top, err, errlen);
    if (e == FRB_OK && top == 0) e = ladder_start(dir, from_file, &top, opt->log, err, errlen);
    else if (e == FRB_OK && opt->log) {
        fprintf(opt->log, "The longest complete Bomb in %s is for %d bits%s\n", dir, top,
                top >= to_bits ? ": nothing to do." : "; continuing from it.");
        fflush(opt->log);
    }

    level_opt = *opt;
    for (int level = top + 1; e == FRB_OK && level <= to_bits; level++) {
        LevelCost cost;
        int done = 0;
        if (opt->time_limit > 0) { /* one limit for the whole climb */
            double left = opt->time_limit - (double)(now_ns() - start) * 1e-9;
            if (left <= 0) {
                result->last.stopped_early = 1;
                break;
            }
            level_opt.time_limit = left;
        }
        e = ladder_level(dir, level, settings, &level_opt, &result->last, &cost, &done, err, errlen);
        if (e != FRB_OK || !done) break;
        top = level;
        result->levels_done++;
        if (opt->log && level < to_bits && cost.steps && cost.session_ns) {
            /* What is left, from this level's cost and the growth from level
             * to level: measured if two levels are known, else 3.5, the
             * middle of what the pilot measured between 20 and 24 bits. */
            double growth = prev_steps ? (double)cost.steps / (double)prev_steps : 3.5;
            double level_s = (double)cost.session_ns * 1e-9, rest = 0, next;
            if (growth < 2.0) growth = 2.0;
            if (growth > 6.0) growth = 6.0;
            next = level_s;
            for (int l = level + 1; l <= to_bits; l++) {
                next *= growth;
                rest += next;
            }
            fprintf(opt->log,
                    "\nLevels %d to %d are still to do.  If the cost goes on growing by %.1f per level,\n"
                    "they take about %s.\n",
                    level + 1, to_bits, growth, duration(rest));
            fflush(opt->log);
        }
        prev_steps = cost.steps;
    }
    result->top_bits = top;
    result->complete = e == FRB_OK && top >= to_bits;
    close(lock_fd);
    return e;
}
