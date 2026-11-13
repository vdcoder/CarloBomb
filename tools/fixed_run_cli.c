/*
 * carlobomb bomb: the FixedRunBomb commands.
 *
 *   carlobomb bomb build    --bits L --work-dir DIR [settings] [run options]
 *   carlobomb bomb resume   --work-dir DIR [settings that may change] [run options]
 *   carlobomb bomb status   --work-dir DIR [--target S]
 *   carlobomb bomb finalize --work-dir DIR --output FILE [--partial]
 *   carlobomb bomb verify   --file FILE [--work-dir DIR] [--recertify [--engine E] [--sample K]]
 *   carlobomb bomb lookup   --file FILE --target S [--out GRAPH]
 *   carlobomb bomb ladder   --dir DIR --to L [--from FILE] [settings] [run options]
 *   carlobomb bomb ladder   --dir DIR --status
 *
 * Exit status: 0 success and, where the question arises, a complete
 * certified table; 1 a check failed or the construction broke; 2 usage,
 * invalid input or an incompatible work directory; 3 valid but not complete.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include "fixed_run_cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_engine.h"
#include "compact.h"
#include "fixed_run.h"
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
#include "fixed_run_build.h"
#endif

enum { X_OK = 0, X_FAIL = 1, X_USAGE = 2, X_PARTIAL = 3 };

typedef struct {
    const char *work_dir, *file, *output, *target, *out, *engine, *max_steps, *symmetry, *fallback;
    const char *dir, *from, *base, *against;
    long bits, max_nodes, workers, enumerate_max, to, format, index_bytes; /* -1: not given */
    long long sample, limit_jobs, min_free;                   /* -1: not given */
    unsigned long long seed;
    double time_limit, progress; /* < 0: not given */
    int partial, recertify, quiet, status, hardest;
} BombArgs;

void fixed_run_cli_usage(void) {
    fprintf(stderr,
            "  carlobomb bomb build    --bits L --work-dir DIR [--max-nodes N] [--engine E]\n"
            "                          [--workers W] [--enumerate-max K] [--symmetry on|off]\n"
            "                          [--fallback ring|none] [--max-steps K[,K...]]\n"
            "                          [--base FILE]\n"
            "      start building the table of minimum-node REVs for every L-bit string\n"
            "      (L up to %d); with --base, on a complete Bomb for fewer bits\n"
            "  carlobomb bomb resume   --work-dir DIR [--max-nodes N] [--engine E] [--workers W]\n"
            "                          [--fallback ring|none] [--max-steps K[,K...]]\n"
            "      continue an interrupted or limited construction\n"
            "  carlobomb bomb status   --work-dir DIR [--target S]\n"
            "      progress, the minimum-node histogram and search effort; or one target\n"
            "  carlobomb bomb finalize --work-dir DIR --output FILE [--partial]\n"
            "      write the table; refuses an incomplete one unless --partial\n"
            "  carlobomb bomb verify   --file FILE [--work-dir DIR] [--recertify] [--sample K]\n"
            "                          [--hardest] [--against SHORTER.frb]\n"
            "      check a table: structure, every witness, symmetry, evidence, recomputation\n"
            "      (of all strings, of K random ones, or of those that need the most nodes),\n"
            "      and agreement with a complete Bomb for fewer bits\n"
            "  carlobomb bomb lookup   --file FILE --target S [--out GRAPH]\n"
            "      print the REV stored for S\n"
            "  carlobomb bomb compact  --file FILE --output COMPACT [--index-bytes 1..4]\n"
            "      write a complete Bomb in the compact format: every REV in the space its\n"
            "      node count needs, with the answer for every shorter string in the file\n"
            "  carlobomb bomb expand   --file COMPACT --output FILE\n"
            "      the fixed-width file a compact Bomb was made from, byte for byte\n"
            "  carlobomb bomb ladder   --dir DIR --to L [--from FILE] [--workers W] [--engine E]\n"
            "                          [--max-steps K[,K...]] [--max-nodes N]\n"
            "      build complete Bombs for every length up to L, each from the one before,\n"
            "      starting from the longest in DIR or from FILE; run again to continue\n"
            "  carlobomb bomb ladder   --dir DIR --status\n"
            "      the Bombs a ladder directory holds and the level in progress\n"
            "build, resume and ladder also take --sample K [--seed S] (search only K random\n"
            "open targets), --limit-jobs K, --time-limit SECONDS, --progress SECONDS,\n"
            "--min-free MIB (stop cleanly when the disk has less free space), --quiet.\n"
            "The engine defaults to amd64-auto where it exists, else c.\n",
            FRB_MAX_BITS);
}

static int usage_error(const char *message) {
    if (message) fprintf(stderr, "%s\n", message);
    fprintf(stderr, "usage:\n");
    fixed_run_cli_usage();
    return X_USAGE;
}

static int parse_ll(const char *s, long long *out) {
    char *end;
    long long v;
    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno || *s == '\0' || *end != '\0' || v < 0) return 0;
    *out = v;
    return 1;
}

static int parse_double(const char *s, double *out) {
    char *end;
    double v = strtod(s, &end);
    if (*s == '\0' || *end != '\0' || !(v >= 0) || v > 1e12) return 0;
    *out = v;
    return 1;
}

static int parse_bomb_args(int argc, char **argv, BombArgs *a) {
    memset(a, 0, sizeof *a);
    a->bits = a->max_nodes = a->workers = a->enumerate_max = a->to = a->format = a->index_bytes = -1;
    a->sample = a->limit_jobs = a->min_free = -1;
    a->time_limit = a->progress = -1;
    for (int i = 3; i < argc; i++) {
        const char *k = argv[i], *v;
        long long n;
        if (!strcmp(k, "--partial")) { a->partial = 1; continue; }
        if (!strcmp(k, "--recertify")) { a->recertify = 1; continue; }
        if (!strcmp(k, "--quiet")) { a->quiet = 1; continue; }
        if (!strcmp(k, "--status")) { a->status = 1; continue; }
        if (!strcmp(k, "--hardest")) { a->hardest = 1; a->recertify = 1; continue; }
        v = i + 1 < argc ? argv[++i] : NULL;
        if (!v) { fprintf(stderr, "missing value for %s\n", k); return 0; }
        if (!strcmp(k, "--work-dir")) a->work_dir = v;
        else if (!strcmp(k, "--file")) a->file = v;
        else if (!strcmp(k, "--output")) a->output = v;
        else if (!strcmp(k, "--target")) a->target = v;
        else if (!strcmp(k, "--out")) a->out = v;
        else if (!strcmp(k, "--engine")) a->engine = v;
        else if (!strcmp(k, "--max-steps")) a->max_steps = v;
        else if (!strcmp(k, "--symmetry")) a->symmetry = v;
        else if (!strcmp(k, "--fallback")) a->fallback = v;
        else if (!strcmp(k, "--dir")) a->dir = v;
        else if (!strcmp(k, "--from")) a->from = v;
        else if (!strcmp(k, "--base")) a->base = v;
        else if (!strcmp(k, "--against")) a->against = v;
        else if (!strcmp(k, "--time-limit")) {
            if (!parse_double(v, &a->time_limit)) goto bad;
        } else if (!strcmp(k, "--progress")) {
            if (!parse_double(v, &a->progress)) goto bad;
        } else if (!parse_ll(v, &n)) {
            goto bad;
        } else if (!strcmp(k, "--bits")) a->bits = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--max-nodes")) a->max_nodes = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--workers")) a->workers = n > 1000000 ? 1000000 : (long)n;
        else if (!strcmp(k, "--enumerate-max")) a->enumerate_max = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--to")) a->to = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--format")) a->format = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--index-bytes")) a->index_bytes = n > 1000 ? 1000 : (long)n;
        else if (!strcmp(k, "--min-free")) a->min_free = n > ((long long)1 << 40) ? (long long)1 << 40 : n;
        else if (!strcmp(k, "--sample")) a->sample = n;
        else if (!strcmp(k, "--limit-jobs")) a->limit_jobs = n;
        else if (!strcmp(k, "--seed")) a->seed = (unsigned long long)n;
        else { fprintf(stderr, "unknown option %s\n", k); return 0; }
        continue;
    bad:
        fprintf(stderr, "bad value for %s: %s\n", k, v);
        return 0;
    }
    return 1;
}

static void print_graph(const RevGraph *g) {
    printf("Node   Color   Link0   Link1\n");
    for (int i = 0; i < g->node_count; i++)
        printf("%-6d %-7s %-7d %d\n", i, g->nodes[i].color == REV_WHITE ? "W (0)" : "B (1)",
               g->nodes[i].links[0], g->nodes[i].links[1]);
}

static const char *status_text(FrbStatus s) {
    switch (s) {
    case FRB_EMPTY: return "EMPTY (no witness, nothing known)";
    case FRB_WITNESS: return "WITNESS (valid; minimum node count not certified)";
    case FRB_MINIMAL: return "MINIMAL (certified minimum node count)";
    case FRB_UNRESOLVED: return "UNRESOLVED (searched without a final answer; no witness)";
    case FRB_UNSUPPORTED: return "UNSUPPORTED (recorded as needing more nodes than an entry holds)";
    }
    return "?";
}

/* ---- lookup ------------------------------------------------------------- */

/* A compact Bomb answers a string of any length up to its own from the file. */
static int lookup_compact(const BombArgs *a, int bits, uint32_t index) {
    char extension[FRB_MAX_BITS + 1];
    FrbCompactInfo info;
    RevGraph g;
    uint32_t emitted = 0;
    FrbError e = frb_compact_file_info(a->file, &info);
    if (e == FRB_ERR_IO) {
        fprintf(stderr, "cannot read %s\n", a->file);
        return X_USAGE;
    }
    if (e != FRB_OK) {
        printf("File: %s\nVerification: FAIL (%s)\n", a->file, frb_error_str(e));
        return X_FAIL;
    }
    if (bits > info.bits) {
        fprintf(stderr, "%s holds targets of up to %d bits; %s has %d bits\n", a->file, info.bits, a->target,
                bits);
        return X_USAGE;
    }
    e = frb_compact_file_lookup(a->file, bits, index, &info, &g, &emitted);
    printf("Target: %s (%d bits; %s, compact, %d-bit Bomb)\n", a->target, bits, a->file, info.bits);
    if (e != FRB_OK) {
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        return X_FAIL;
    }
    printf("Status: %s\n", status_text(FRB_MINIMAL));
    if (bits < info.bits) {
        frb_target_format(emitted, info.bits, extension);
        printf("Extension: %s: the first of the %d-bit strings starting with the target that need\n"
               "the fewest nodes\n",
               extension, info.bits);
    }
    printf("Nodes: %d\n\n", g.node_count);
    print_graph(&g);
    printf("\nVerification: PASS\n");
    if (a->out) {
        FILE *f = fopen(a->out, "w");
        if (!f || rev_graph_write(f, &g) != REV_OK || fclose(f) != 0) {
            fprintf(stderr, "cannot write %s\n", a->out);
            return X_USAGE;
        }
        printf("Graph written to %s\n", a->out);
    }
    return X_OK;
}

static int cmd_lookup(const BombArgs *a) {
    FrbFileInfo info;
    FrbStatus status;
    RevGraph g;
    uint32_t index;
    int bits;
    FrbError e;
    if (!a->file || !a->target) return usage_error("lookup needs --file and --target");
    if (!frb_target_parse(a->target, &bits, &index)) {
        fprintf(stderr, "invalid target: must be 1..%d characters 0 and 1\n", FRB_MAX_BITS);
        return X_USAGE;
    }
    if (frb_file_is_compact(a->file) == 1) return lookup_compact(a, bits, index);
    e = frb_file_info(a->file, &info);
    if (e == FRB_ERR_IO) {
        fprintf(stderr, "cannot read %s\n", a->file);
        return X_USAGE;
    }
    if (e != FRB_OK) {
        printf("File: %s\nVerification: FAIL (%s)\n", a->file, frb_error_str(e));
        return X_FAIL;
    }
    if (bits != info.bits) {
        fprintf(stderr, "%s holds %d-bit targets; %s has %d bits\n", a->file, info.bits, a->target,
                bits);
        return X_USAGE;
    }
    e = frb_file_lookup(a->file, index, &info, &status, &g);
    printf("Target: %s (entry %u of %s)\n", a->target, (unsigned)index, a->file);
    if (e != FRB_OK) {
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        return X_FAIL;
    }
    printf("Status: %s\n", status_text(status));
    if (!info.complete) printf("Table: PARTIAL (not every entry is a certified minimum)\n");
    if (g.node_count == 0) return X_PARTIAL;
    printf("Nodes: %d\n\n", g.node_count);
    print_graph(&g);
    /* frb_file_lookup has run the graph; say so in the tool's usual words. */
    printf("\nVerification: PASS\n");
    if (a->out) {
        FILE *f = fopen(a->out, "w");
        if (!f || rev_graph_write(f, &g) != REV_OK || fclose(f) != 0) {
            fprintf(stderr, "cannot write %s\n", a->out);
            return X_USAGE;
        }
        printf("Graph written to %s\n", a->out);
    }
    return status == FRB_MINIMAL ? X_OK : X_PARTIAL;
}

#ifdef CARLOBOMB_HAVE_BOMB_BUILD

/* ---- engines ------------------------------------------------------------ */

/*
 * The engine for a construction.  "auto" (and no name at all) means
 * amd64-auto where this build and this CPU have it, else the C engine.  A
 * named amd64 engine that is not available here also falls back to C, with
 * a notice: every engine returns the same results, graphs and step counts,
 * so the fallback changes speed and nothing else.
 */
static int select_engine(const char *requested, char *chosen, size_t cap, RevLevelSolveFn *fn) {
    char name[64]; /* a copy: `requested` may be the very buffer `chosen` points to */
    int rc;
    if (requested) {
        if (strlen(requested) >= sizeof name) {
            fprintf(stderr, "unknown engine %s\n", requested);
            return 0;
        }
        strcpy(name, requested);
        requested = name;
    }
    if (!requested || !strcmp(requested, "auto")) {
        if (cli_engine_select("amd64-auto", fn) == CLI_ENGINE_OK) {
            snprintf(chosen, cap, "amd64-auto");
            return 1;
        }
        requested = "c";
    }
    rc = cli_engine_select(requested, fn);
    if (rc == CLI_ENGINE_UNKNOWN) {
        fprintf(stderr, "unknown engine %s\n", requested);
        return 0;
    }
    if (rc != CLI_ENGINE_OK) {
        fprintf(stderr, "note: engine %s is not available here (%s); using the C engine, whose\n"
                        "results are identical\n",
                requested,
                rc == CLI_ENGINE_NOT_BUILT ? "this build has no AMD64 assembly solver"
                                           : "this CPU lacks an instruction set it needs");
        requested = "c";
        cli_engine_select("c", fn);
    }
    if (strlen(requested) >= cap) {
        fprintf(stderr, "engine name too long\n");
        return 0;
    }
    snprintf(chosen, cap, "%s", requested);
    return 1;
}

static int exit_for(FrbError e) {
    switch (e) {
    case FRB_OK: return X_OK;
    case FRB_ERR_ARG:
    case FRB_ERR_INCOMPATIBLE:
    case FRB_ERR_BUSY:
    case FRB_ERR_EXISTS:
    case FRB_ERR_UNSUPPORTED: return X_USAGE;
    case FRB_ERR_INCOMPLETE: return X_PARTIAL;
    default: return X_FAIL;
    }
}

static int fail(FrbError e, const char *err) {
    fprintf(stderr, "error: %s\n", err && err[0] ? err : frb_error_str(e));
    return exit_for(e);
}

/* ---- build and resume --------------------------------------------------- */

/* A construction reads and writes the fixed-width formats only. */
static int refuse_compact_base(const char *path, const char *option) {
    if (!path || frb_file_is_compact(path) != 1) return 0;
    fprintf(stderr,
            "error: %s is in the compact format.  A construction builds on a fixed-width file:\n"
            "`carlobomb bomb expand --file %s --output FILE` gives back the one it was made\n"
            "from, byte for byte; name that with %s.\n",
            path, path, option);
    return 1;
}

static int apply_changeable(const BombArgs *a, FrbConfig *cfg) {
    if (a->max_nodes >= 0) cfg->max_nodes = (int)a->max_nodes;
    if (a->workers >= 0) cfg->workers = (int)a->workers;
    if (a->max_steps && !frb_parse_step_limits(a->max_steps, cfg)) {
        fprintf(stderr, "bad value for --max-steps: %s (1..%d numbers separated by commas; "
                        "0 = no limit)\n",
                a->max_steps, FRB_MAX_PASSES);
        return 0;
    }
    if (a->fallback) {
        if (!strcmp(a->fallback, "ring")) cfg->fallback_ring = 1;
        else if (!strcmp(a->fallback, "none")) cfg->fallback_ring = 0;
        else {
            fprintf(stderr, "--fallback must be ring or none\n");
            return 0;
        }
    }
    return 1;
}

static void run_options(const BombArgs *a, RevLevelSolveFn solve, FrbRunOptions *opt) {
    frb_run_options_default(opt);
    opt->solve = solve;
    opt->log = a->quiet ? NULL : stdout;
    if (a->sample >= 0) opt->sample = (uint64_t)a->sample;
    if (a->limit_jobs >= 0) opt->limit_jobs = (uint64_t)a->limit_jobs;
    if (a->time_limit >= 0) opt->time_limit = a->time_limit;
    if (a->progress >= 0) opt->progress_seconds = a->progress;
    if (a->min_free >= 0) opt->min_free_bytes = (uint64_t)a->min_free << 20;
    opt->seed = a->seed;
}

static int run_session(const BombArgs *a, const FrbConfig *cfg, RevLevelSolveFn solve) {
    char err[FRB_ERR_TEXT] = "";
    FrbRunOptions opt;
    FrbRunResult res;
    FrbState *state = NULL;
    FrbError e;

    run_options(a, solve, &opt);

    if (!a->quiet) {
        printf("FixedRunBomb(%d): building in %s with engine %s, %d worker process%s\n", cfg->bits,
               a->work_dir, cfg->engine, cfg->workers, cfg->workers == 1 ? "" : "es");
        fflush(stdout);
    }
    e = frb_work_run(a->work_dir, cfg, &opt, &res, err, sizeof err);
    if (e != FRB_OK) return fail(e, err);

    if (!a->quiet) {
        printf("Session: %llu jobs in %.2f s (%llu graphs enumerated, %llu searches, %.4g steps)\n\n",
               (unsigned long long)res.jobs, res.seconds, (unsigned long long)res.graphs,
               (unsigned long long)res.searches, (double)res.steps);
        e = frb_state_load(a->work_dir, cfg, &state, err, sizeof err);
        if (e != FRB_OK) return fail(e, err);
        frb_report_state(stdout, a->work_dir, cfg, state);
        frb_state_free(state);
        printf("\n");
        if (res.complete) {
            printf("Next: carlobomb bomb finalize --work-dir %s --output fixed-run-%d.frb\n",
                   a->work_dir, cfg->bits);
        } else if (res.interrupted) {
            printf("Interrupted.  Everything recorded so far is kept.\n"
                   "Continue with: carlobomb bomb resume --work-dir %s\n",
                   a->work_dir);
        } else if (res.disk_low) {
            printf("Stopped because the disk is nearly full.  Everything recorded so far is kept.\n"
                   "Free some space, then: carlobomb bomb resume --work-dir %s\n",
                   a->work_dir);
        } else if (res.stopped_early) {
            printf("Stopped at the requested job cap, sample size or time limit.\n"
                   "Continue with: carlobomb bomb resume --work-dir %s\n",
                   a->work_dir);
        } else {
            printf("These settings can do no more for the targets still open.  Resume with a\n"
                   "larger --max-steps (0 = no limit) or --max-nodes:\n"
                   "  carlobomb bomb resume --work-dir %s --max-steps 0\n",
                   a->work_dir);
        }
    }
    return res.complete ? X_OK : X_PARTIAL;
}

static int cmd_build(const BombArgs *a) {
    char err[FRB_ERR_TEXT] = "";
    RevLevelSolveFn solve = NULL;
    FrbConfig cfg;
    FrbError e;
    if (!a->work_dir || a->bits < 0) return usage_error("build needs --bits and --work-dir");
    frb_config_default(&cfg, (int)a->bits);
    if (a->format >= 0) { /* a newer format than the length needs, for a test of it */
        const FrbFormat *format = frb_format((int)a->format);
        if (!format) return usage_error("--format must be 1, 2 or 3");
        cfg.format = format->version;
        cfg.max_nodes = format->node_capacity;
    }
    if (refuse_compact_base(a->base, "--base")) return X_USAGE;
    if (a->base) {
        e = frb_config_set_base(&cfg, a->base, err, sizeof err);
        if (e != FRB_OK) return fail(e == FRB_ERR_IO ? FRB_ERR_ARG : e, err);
    }
    if (a->enumerate_max >= 0) cfg.enumerate_max = (int)a->enumerate_max;
    if (a->symmetry) {
        if (!strcmp(a->symmetry, "on")) cfg.symmetry = 1;
        else if (!strcmp(a->symmetry, "off")) cfg.symmetry = 0;
        else return usage_error("--symmetry must be on or off");
    }
    if (!apply_changeable(a, &cfg)) return X_USAGE;
    if (!select_engine(a->engine, cfg.engine, sizeof cfg.engine, &solve)) return X_USAGE;
    e = frb_config_check(&cfg, err, sizeof err);
    if (e == FRB_OK) e = frb_check_memory(&cfg, a->quiet ? NULL : stdout, err, sizeof err);
    if (e == FRB_ERR_MEMORY) return fail(FRB_ERR_ARG, err); /* nothing was created */
    if (e == FRB_OK) e = frb_work_create(a->work_dir, &cfg, err, sizeof err);
    if (e != FRB_OK) return fail(e, err);
    return run_session(a, &cfg, solve);
}

static int cmd_resume(const BombArgs *a) {
    char err[FRB_ERR_TEXT] = "";
    RevLevelSolveFn solve = NULL;
    FrbConfig cfg, before;
    FrbError e;
    if (!a->work_dir) return usage_error("resume needs --work-dir");
    e = frb_config_read(a->work_dir, &cfg, err, sizeof err);
    if (e != FRB_OK) return fail(e == FRB_ERR_IO ? FRB_ERR_ARG : e, err);
    before = cfg;
    /* What the journal was written with cannot change under it. */
    if ((a->bits >= 0 && a->bits != cfg.bits) ||
        (a->enumerate_max >= 0 && a->enumerate_max != cfg.enumerate_max) ||
        (a->symmetry && strcmp(a->symmetry, cfg.symmetry ? "on" : "off") != 0)) {
        fprintf(stderr,
                "error: %s was started with --bits %d --symmetry %s --enumerate-max %d; these are\n"
                "fixed for the life of a work directory.  Start another one for other settings.\n",
                a->work_dir, cfg.bits, cfg.symmetry ? "on" : "off", cfg.enumerate_max);
        return X_USAGE;
    }
    if (!apply_changeable(a, &cfg)) return X_USAGE;
    if (!select_engine(a->engine ? a->engine : cfg.engine, cfg.engine, sizeof cfg.engine, &solve))
        return X_USAGE;
    e = frb_config_check(&cfg, err, sizeof err);
    if (e != FRB_OK) return fail(e, err);
    if (memcmp(&before, &cfg, sizeof cfg) != 0) {
        e = frb_config_write(a->work_dir, &cfg);
        if (e != FRB_OK) return fail(e, "cannot update config.txt");
    }
    return run_session(a, &cfg, solve);
}

/* ---- status and finalize ------------------------------------------------ */

static int cmd_status(const BombArgs *a) {
    char err[FRB_ERR_TEXT] = "";
    FrbConfig cfg;
    FrbState *state = NULL;
    FrbError e;
    int code;
    if (!a->work_dir) return usage_error("status needs --work-dir");
    e = frb_config_read(a->work_dir, &cfg, err, sizeof err);
    if (e != FRB_OK) return fail(e == FRB_ERR_IO ? FRB_ERR_ARG : e, err);
    e = frb_state_load(a->work_dir, &cfg, &state, err, sizeof err);
    if (e != FRB_OK) return fail(e, err);
    if (a->target) {
        uint32_t index;
        int bits;
        if (!frb_target_parse(a->target, &bits, &index) || bits != cfg.bits) {
            fprintf(stderr, "invalid target: this construction is for %d-bit strings\n", cfg.bits);
            frb_state_free(state);
            return X_USAGE;
        }
        e = frb_report_target(stdout, a->work_dir, &cfg, state, index, err, sizeof err);
        code = e != FRB_OK ? fail(e, err)
                           : state->targets[index].conflict ? X_FAIL
                           : state->targets[index].status == FRB_MINIMAL ? X_OK : X_PARTIAL;
    } else {
        frb_report_state(stdout, a->work_dir, &cfg, state);
        code = state->conflicts ? X_FAIL
                                : state->by_status[FRB_MINIMAL] == state->count ? X_OK : X_PARTIAL;
    }
    frb_state_free(state);
    return code;
}

static int cmd_finalize(const BombArgs *a) {
    char err[FRB_ERR_TEXT] = "";
    FrbFileInfo info;
    FrbSummary sum;
    FrbError e;
    if (!a->work_dir || !a->output) return usage_error("finalize needs --work-dir and --output");
    e = frb_work_finalize(a->work_dir, a->output, a->partial, &sum, err, sizeof err);
    if (e == FRB_ERR_INCOMPLETE) {
        fprintf(stderr,
                "error: %s\n"
                "Nothing was written.  Resume the construction, or pass --partial to write the\n"
                "table as it stands, marked as partial.\n",
                err);
        return X_PARTIAL;
    }
    if (e != FRB_OK) return fail(e == FRB_ERR_IO && !err[0] ? FRB_ERR_ARG : e, err);
    e = frb_file_info(a->output, &info);
    if (e != FRB_OK) return fail(e, "the file just written cannot be read");
    printf("Wrote %s: %llu bytes\n\n", a->output,
           (unsigned long long)(FRB_HEADER_BYTES + (uint64_t)sum.count * (uint64_t)info.entry_bytes));
    frb_report_summary(stdout, &sum, info.bits);
    return sum.complete ? X_OK : X_PARTIAL;
}

/* ---- ladder ------------------------------------------------------------- */

static int ladder_status(const char *dir) {
    char path[4096];
    int levels = 0, top = 0;
    printf("Ladder in %s\n\n", dir);
    printf("  Bits      Strings   Largest minimum   Table CRC-32   Format   File\n");
    for (int bits = 1; bits <= FRB_MAX_BITS; bits++) {
        FrbFileInfo info;
        FILE *probe;
        FrbError e;
        snprintf(path, sizeof path, "%s/fixed-run-%02d.frb", dir, bits);
        probe = fopen(path, "rb");
        if (!probe) continue;
        fclose(probe);
        e = frb_file_info(path, &info);
        if (e != FRB_OK || info.bits != bits) {
            printf("  %4d   INVALID (%s): %s\n", bits, frb_error_str(e), path);
            continue;
        }
        printf("  %4d   %10u   %15d       %08x   %6d   %s%s\n", bits, (unsigned)info.count,
               info.max_minimal_nodes, (unsigned)info.table_crc, info.version, path,
               info.complete ? "" : "   (NOT COMPLETE)");
        levels++;
        if (info.complete) top = bits;
    }
    if (!levels) printf("  none\n");
    for (int bits = top + 1; bits <= FRB_MAX_BITS; bits++) {
        FILE *probe;
        snprintf(path, sizeof path, "%s/work-%02d/config.txt", dir, bits);
        probe = fopen(path, "r");
        if (!probe) continue;
        fclose(probe);
        printf("\nLevel %d is under construction.  Its progress:\n"
               "  carlobomb bomb status --work-dir %s/work-%02d\n",
               bits, dir, bits);
    }
    if (top)
        printf("\nEvery file listed as complete holds a certified minimum-node REV for each string of\n"
               "its length.  The reports of the levels built here are in %s/level-NN.txt.\n",
               dir);
    return top ? X_OK : X_PARTIAL;
}

static int cmd_ladder(const BombArgs *a) {
    char err[FRB_ERR_TEXT] = "";
    RevLevelSolveFn solve = NULL;
    FrbConfig settings;
    FrbRunOptions opt;
    FrbLadderResult res;
    FrbError e;
    double t0;
    if (!a->dir) return usage_error("ladder needs --dir");
    if (a->status) return ladder_status(a->dir);
    if (a->to < 0) return usage_error("ladder needs --to (the length to climb to) or --status");
    if (refuse_compact_base(a->from, "--from")) return X_USAGE;

    frb_config_default(&settings, 1);
    settings.format = a->format >= 0 ? (int)a->format : 0; /* 0: what each length needs */
    settings.max_nodes = 0;                               /* 0: all an entry can hold */
    if (a->format >= 0 && !frb_format((int)a->format)) return usage_error("--format must be 1, 2 or 3");
    if (a->symmetry) {
        if (!strcmp(a->symmetry, "on")) settings.symmetry = 1;
        else if (!strcmp(a->symmetry, "off")) settings.symmetry = 0;
        else return usage_error("--symmetry must be on or off");
    }
    if (!apply_changeable(a, &settings)) return X_USAGE;
    if (settings.workers < 1 || settings.workers > FRB_MAX_WORKERS) {
        fprintf(stderr, "error: workers must be 1..%d\n", FRB_MAX_WORKERS);
        return X_USAGE;
    }
    if (!select_engine(a->engine, settings.engine, sizeof settings.engine, &solve)) return X_USAGE;
    run_options(a, solve, &opt);
    if (a->progress < 0) opt.progress_seconds = 30; /* hours of it: no need for a line every 5 s */
    if (a->min_free < 0) opt.min_free_bytes = (uint64_t)2048 << 20;

    if (!a->quiet) {
        printf("Ladder to FixedRunBomb(%ld) in %s: engine %s, %d worker process%s\n", a->to, a->dir,
               settings.engine, settings.workers, settings.workers == 1 ? "" : "es");
        fflush(stdout);
    }
    t0 = cb_now();
    e = frb_ladder_run(a->dir, a->from, (int)a->to, &settings, &opt, &res, err, sizeof err);
    if (e != FRB_OK) {
        if (res.top_bits && !a->quiet)
            printf("\nThe longest complete Bomb in %s is for %d bits.\n", a->dir, res.top_bits);
        return fail(e == FRB_ERR_IO && !err[0] ? FRB_ERR_ARG : e, err);
    }
    if (a->quiet) return res.complete ? X_OK : X_PARTIAL;

    printf("\n%d level%s completed in %.1f s.  The longest complete Bomb in %s is for %d bits:\n"
           "  %s/fixed-run-%02d.frb\n",
           res.levels_done, res.levels_done == 1 ? "" : "s", cb_now() - t0, a->dir, res.top_bits, a->dir,
           res.top_bits);
    if (res.complete) {
        printf("\nThe ladder is complete.  To check the result independently of this run:\n"
               "  carlobomb bomb verify --file %s/fixed-run-%02d.frb --work-dir %s/work-%02d\n",
               a->dir, res.top_bits, a->dir, res.top_bits);
        return X_OK;
    }
    if (res.last.interrupted)
        printf("\nInterrupted.  Everything recorded so far is kept.\n");
    else if (res.last.disk_low)
        printf("\nStopped because the disk is nearly full.  Everything recorded so far is kept.\n");
    else if (res.last.stopped_early)
        printf("\nStopped at the requested job cap, sample size or time limit.\n");
    else
        printf("\nLevel %d cannot be completed with these settings: a search was stopped by its\n"
               "step limit, or --max-nodes is too small.  See\n"
               "  carlobomb bomb status --work-dir %s/work-%02d\n",
               res.top_bits + 1, a->dir, res.top_bits + 1);
    printf("Run the same command again to continue from where it stopped.\n");
    return X_PARTIAL;
}

#endif /* CARLOBOMB_HAVE_BOMB_BUILD */

/* ---- the compact format -------------------------------------------------- */

static const char *commas_u64(uint64_t v) {
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

/* The class directory, and the sizes of the three parts of the file. */
static void print_compact_layout(const FrbCompactInfo *info) {
    uint64_t index = info->index_entries * (uint64_t)info->index_bytes;
    uint64_t pool = info->file_bytes - info->pool_offset;
    printf("  Nodes   Link bits   Bytes per REV          REVs            Bytes\n");
    for (int n = 1; n <= info->max_nodes; n++)
        printf("  %5d   %9d   %13u   %11s   %14s\n", n, frb_compact_link_bits(n), (unsigned)info->class_stride[n],
               commas_u64(info->class_count[n]),
               commas_u64((uint64_t)info->class_count[n] * (uint64_t)info->class_stride[n]));
    printf("  REVs                 %15s bytes\n", commas_u64(pool));
    printf("  prefix index         %15s bytes (%s strings of 1 to %d bits, %d byte%s each)\n", commas_u64(index),
           commas_u64(info->index_entries), info->bits, info->index_bytes, info->index_bytes == 1 ? "" : "s");
    printf("  header, directory    %15s bytes (with the padding before the REVs)\n",
           commas_u64(info->pool_offset - index));
    printf("  file                 %15s bytes\n", commas_u64(info->file_bytes));
}

/* A conversion never replaces a file. */
static int output_is_free(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    fclose(f);
    fprintf(stderr, "error: %s exists.  A conversion never replaces a file: choose another name, or\n"
                    "remove the file yourself if it is not needed.\n",
            path);
    return 0;
}

static int cmd_compact(const BombArgs *a) {
    char target[FRB_MAX_BITS + 1];
    FrbCompactInfo cinfo;
    FrbFileInfo info;
    FrbTable t;
    uint64_t source_bytes;
    uint32_t bad = 0;
    FrbError e;
    if (!a->file || !a->output) return usage_error("compact needs --file and --output");
    if (frb_file_is_compact(a->file) == 1) {
        fprintf(stderr, "error: %s is in the compact format already\n", a->file);
        return X_USAGE;
    }
    e = frb_file_read(a->file, &t, &info, &bad); /* header, length, both checksums, every REV run */
    if (e == FRB_ERR_IO) {
        fprintf(stderr, "cannot read %s\n", a->file);
        return X_USAGE;
    }
    if (e == FRB_OK && !output_is_free(a->output)) {
        frb_table_destroy(&t);
        return X_USAGE;
    }
    if (e != FRB_OK) {
        frb_target_format(bad, info.bits >= 1 && info.bits <= FRB_MAX_BITS ? info.bits : 1, target);
        printf("Source: %s\nVerification: FAIL (%s%s%s)\nNothing was written.\n", a->file, frb_error_str(e),
               e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT ? ", target " : "",
               e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT ? target : "");
        return X_FAIL;
    }
    source_bytes = frb_file_bytes(t.format, t.bits);
    printf("Source: %s\n  format version %d, %d-bit targets, %s bytes, table CRC-32 %08x; validated in full\n",
           a->file, info.version, info.bits, commas_u64(source_bytes), (unsigned)info.table_crc);
    if (!info.complete) {
        frb_table_destroy(&t);
        printf("  it is a partial table: only a complete Bomb can be written in the compact format,\n"
               "  which has no place for an entry without a certified minimum.  Nothing was written.\n");
        return X_PARTIAL;
    }
    fflush(stdout);
    e = frb_compact_write(a->output, &t, info.version, info.table_crc, a->index_bytes > 0 ? (int)a->index_bytes : 0);
    frb_table_destroy(&t);
    if (e == FRB_ERR_ARG) {
        fprintf(stderr, "error: --index-bytes must be %d..4 for %d-bit targets\n", (info.bits + 7) / 8, info.bits);
        return X_USAGE;
    }
    if (e != FRB_OK) {
        fprintf(stderr, "error: cannot write %s: %s.  Nothing was written.\n", a->output, frb_error_str(e));
        return X_FAIL;
    }
    e = frb_compact_file_info(a->output, &cinfo);
    if (e != FRB_OK) {
        fprintf(stderr, "error: the file just written cannot be read: %s\n", frb_error_str(e));
        return X_FAIL;
    }
    printf("Compact: %s\n", a->output);
    print_compact_layout(&cinfo);
    printf("  %.1f%% of the source\n", 100.0 * (double)cinfo.file_bytes / (double)source_bytes);
    printf("Before it got its name the new file was read back in full: every REV decoded and run,\n"
           "every string of %d bits with the REV the source has for it, every shorter string answered\n"
           "by its extension with the fewest nodes, the lowest among equals.  The source is unchanged;\n"
           "`carlobomb bomb expand` gives it back from the compact file, byte for byte.\n",
           cinfo.bits);
    return X_OK;
}

static int cmd_expand(const BombArgs *a) {
    FrbCompact *c = NULL;
    const FrbCompactInfo *cinfo;
    const FrbFormat *format;
    uint32_t bad = 0, crc = 0;
    FrbError e;
    if (!a->file || !a->output) return usage_error("expand needs --file and --output");
    if (frb_file_is_compact(a->file) != 1) {
        fprintf(stderr, "error: %s is not a Bomb in the compact format\n", a->file);
        return X_USAGE;
    }
    e = frb_compact_open(a->file, &c, &bad);
    if (e != FRB_OK) {
        printf("File: %s\nVerification: FAIL (%s)\nNothing was written.\n", a->file, frb_error_str(e));
        return X_FAIL;
    }
    cinfo = frb_compact_info(c);
    format = frb_format(cinfo->source_version);
    if (!format) format = frb_format_for_bits(cinfo->bits);
    printf("Compact: %s\n  %d-bit targets, %s bytes; validated in full\n", a->file, cinfo->bits,
           commas_u64(cinfo->file_bytes));
    if (cinfo->source_version) { /* is it the table it says it is?  Before anything is written. */
        e = frb_compact_table_crc(c, format, &crc);
        if (e != FRB_OK || crc != cinfo->source_table_crc) {
            printf("  it says it was made from a version-%d file with table CRC-32 %08x, but the table it\n"
                   "  holds has %08x.  Nothing was written.\n",
                   cinfo->source_version, (unsigned)cinfo->source_table_crc, (unsigned)crc);
            frb_compact_close(c);
            return X_FAIL;
        }
    }
    if (!output_is_free(a->output)) {
        frb_compact_close(c);
        return X_USAGE;
    }
    fflush(stdout);
    e = frb_compact_expand(c, format, a->output, &crc);
    if (e != FRB_OK) {
        fprintf(stderr, "error: cannot write %s: %s.  Nothing was written.\n", a->output, frb_error_str(e));
        frb_compact_close(c);
        return X_FAIL;
    }
    printf("Wrote %s: format version %d, %d-bit targets, %s bytes, table CRC-32 %08x\n", a->output,
           format->version, cinfo->bits, commas_u64(frb_file_bytes(format, cinfo->bits)), (unsigned)crc);
    if (cinfo->source_version)
        printf("That is the version and table checksum of the file the compact one was made from: the same\n"
               "table, byte for byte.  ");
    printf("The new file was read back in full and compared with the compact\none, REV for REV.\n");
    frb_compact_close(c);
    return X_OK;
}

static int verify_compact(const BombArgs *a) {
    char target[FRB_MAX_BITS + 1];
    FrbCompact *c = NULL;
    const FrbCompactInfo *info;
    FrbCompactInfo head;
    uint32_t bad = 0;
    FrbError e;
    if (a->work_dir || a->recertify) {
        fprintf(stderr,
                "--work-dir and --recertify check a fixed-width file against how it was built.  For a\n"
                "compact file, check the file it was made from (`carlobomb bomb expand` regenerates it)\n"
                "and this one against it with --against.\n");
        return X_USAGE;
    }
    printf("File: %s\n", a->file);
    e = frb_compact_file_info(a->file, &head);
    if (e == FRB_OK) e = frb_compact_open(a->file, &c, &bad);
    if (e != FRB_OK) {
        if (e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT) {
            frb_target_format(bad, head.bits, target);
            printf("Verification: FAIL (%s: target %s)\n", frb_error_str(e), target);
        } else {
            printf("Verification: FAIL (%s)\n", frb_error_str(e));
        }
        return X_FAIL;
    }
    info = frb_compact_info(c);
    printf("Compact format (version %d), %d-bit targets, %u REVs", FRB_FORMAT_COMPACT, info->bits,
           (unsigned)info->count);
    if (info->source_version)
        printf("; made from a version-%d file with table CRC-32 %08x", info->source_version,
               (unsigned)info->source_table_crc);
    printf("\nHeader, class directory, length and checksums ............... PASS\n");
    printf("Every REV decoded and run for %2d bits, ordinals in order ..... PASS (%u REVs)\n", info->bits,
           (unsigned)info->count);
    printf("Every shorter string: the better of its two extensions ...... PASS (%s index entries)\n",
           commas_u64(info->index_entries));
    e = frb_compact_check_symmetry(c, &bad);
    if (e != FRB_OK) {
        frb_target_format(bad, info->bits, target);
        printf("Complement symmetry ......................................... FAIL (target %s)\n", target);
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        frb_compact_close(c);
        return X_FAIL;
    }
    printf("Complement symmetry of REVs and node counts ................. PASS\n");
    if (info->source_version) {
        uint32_t crc = 0;
        e = frb_compact_table_crc(c, NULL, &crc);
        if (e != FRB_OK || crc != info->source_table_crc) {
            printf("The table it holds has the checksum of its source ........... FAIL (%08x, not %08x)\n",
                   (unsigned)crc, (unsigned)info->source_table_crc);
            printf("Verification: FAIL (%s)\n", frb_error_str(e != FRB_OK ? e : FRB_ERR_EVIDENCE));
            frb_compact_close(c);
            return X_FAIL;
        }
        printf("The table it holds has the checksum of its source ........... PASS (%08x)\n", (unsigned)crc);
    }
    if (a->against) {
        FrbFileInfo other_info;
        FrbTable other, mine;
        e = frb_file_read(a->against, &other, &other_info, &bad);
        if (e != FRB_OK) {
            printf("Against %s: cannot use it (%s)\nVerification: FAIL (%s)\n", a->against, frb_error_str(e),
                   frb_error_str(e));
            frb_compact_close(c);
            return e == FRB_ERR_IO ? X_USAGE : X_FAIL;
        }
        if (other_info.bits == info->bits) { /* the fixed-width file of the same Bomb */
            e = frb_compact_equals_table(c, &other, &bad);
            printf("Against the fixed-width file %s:\n  every REV and the answer for every shorter string the same ... %s\n",
                   a->against, e == FRB_OK ? "PASS" : "FAIL");
        } else if (other_info.bits < info->bits) { /* a complete Bomb for fewer bits */
            e = frb_compact_to_table(c, NULL, &mine);
            if (e == FRB_OK) {
                e = frb_table_check_chain(&mine, &other, &bad);
                frb_table_destroy(&mine);
            }
            printf("Against the complete %d-bit Bomb %s: prefix minima and stretches ... %s\n", other_info.bits,
                   a->against, e == FRB_OK ? "PASS" : "FAIL");
        } else {
            fprintf(stderr, "--against takes a Bomb for the same or fewer bits than the %d of the file checked\n",
                    info->bits);
            frb_table_destroy(&other);
            frb_compact_close(c);
            return X_USAGE;
        }
        frb_table_destroy(&other);
        if (e != FRB_OK) {
            frb_target_format(bad, info->bits, target);
            printf("  first difference at target %s\nVerification: FAIL (%s)\n", target, frb_error_str(e));
            frb_compact_close(c);
            return X_FAIL;
        }
    }
    printf("\n");
    print_compact_layout(info);
    {
        int formula = 0;
        while (formula * formula < 4 * info->bits) formula++;
        printf("\nCOMPLETE: every string of %d bits has its certified minimum-node REV here.  The largest\n"
               "is %d nodes, needed by %s strings; ceil(2*sqrt(%d)) - 1 = %d.\n",
               info->bits, info->max_nodes, commas_u64(info->class_count[info->max_nodes]), info->bits, formula - 1);
    }
    printf("That the node counts are minima is certified by the construction of the fixed-width file\n"
           "this was made from, not by this file.\n");
    printf("\nVerification: PASS (complete certified table, compact format)\n");
    frb_compact_close(c);
    return X_OK;
}

/* ---- verify ------------------------------------------------------------- */

static int cmd_verify(const BombArgs *a) {
    char target[FRB_MAX_BITS + 1];
    FrbFileInfo info;
    FrbSummary sum;
    FrbTable t;
    uint32_t bad = 0;
    FrbError e;
    FILE *probe;
    if (!a->file) return usage_error("verify needs --file");
    probe = fopen(a->file, "rb");
    if (!probe) {
        fprintf(stderr, "cannot read %s\n", a->file);
        return X_USAGE;
    }
    fclose(probe);

    if (frb_file_is_compact(a->file) == 1) return verify_compact(a);

    printf("File: %s\n", a->file);
    e = frb_file_read(a->file, &t, &info, &bad);
    if (e == FRB_ERR_ENTRY || e == FRB_ERR_OUTPUT) {
        frb_target_format(bad, info.bits, target);
        printf("Verification: FAIL (%s: entry %u, target %s)\n", frb_error_str(e), (unsigned)bad,
               target);
        return X_FAIL;
    }
    if (e != FRB_OK) {
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        return X_FAIL;
    }
    printf("Format version %d, %d-bit targets, %u entries, table CRC-32 %08x\n", info.version,
           info.bits, (unsigned)info.count, (unsigned)info.table_crc);
    printf("Header, length, checksums, counts and flags ......... PASS\n");
    printf("Every entry decoded and every graph run for %2d bits .. PASS (%u witnesses)\n", info.bits,
           (unsigned)info.witnesses);

    e = frb_table_check_symmetry(&t, &bad);
    if (e != FRB_OK) {
        frb_target_format(bad, t.bits, target);
        printf("Complement symmetry .................................. FAIL (target %s)\n", target);
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        frb_table_destroy(&t);
        return X_FAIL;
    }
    printf("Complement symmetry of witnesses and minima .......... PASS\n");

    if (a->against) {
        FrbFileInfo short_info;
        FrbTable shorter;
        e = frb_file_read(a->against, &shorter, &short_info, &bad);
        if (e != FRB_OK) {
            printf("Against %s: cannot use it (%s)\n", a->against, frb_error_str(e));
            printf("Verification: FAIL (%s)\n", frb_error_str(e));
            frb_table_destroy(&t);
            return e == FRB_ERR_IO ? X_USAGE : X_FAIL;
        }
        if (short_info.bits >= t.bits) {
            fprintf(stderr, "--against takes a Bomb for fewer bits than the %d of the file checked; %s is for %d\n",
                    t.bits, a->against, short_info.bits);
            frb_table_destroy(&shorter);
            frb_table_destroy(&t);
            return X_USAGE;
        }
        e = frb_table_check_chain(&t, &shorter, &bad);
        frb_table_destroy(&shorter);
        if (e != FRB_OK) {
            frb_target_format(bad, t.bits, target);
            printf("Against the %d-bit Bomb %s: FAIL (%s%s%s)\n", short_info.bits, a->against,
                   frb_error_str(e), e == FRB_ERR_EVIDENCE ? ", first at target " : "",
                   e == FRB_ERR_EVIDENCE ? target : "");
            printf("Verification: FAIL (%s)\n", frb_error_str(e));
            frb_table_destroy(&t);
            return X_FAIL;
        }
        printf("Against the complete %d-bit Bomb %s:\n"
               "  the minimum of every %d-bit string is the smallest among its extensions here,\n"
               "  and no %d consecutive bits of a string need over one node more than it ... PASS\n",
               short_info.bits, a->against, short_info.bits, short_info.bits);
    }

#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    if (a->work_dir) {
        char err[FRB_ERR_TEXT] = "";
        FrbConfig cfg;
        FrbState *state = NULL;
        e = frb_config_read(a->work_dir, &cfg, err, sizeof err);
        if (e == FRB_OK) e = frb_state_load(a->work_dir, &cfg, &state, err, sizeof err);
        if (e == FRB_OK) e = frb_verify_evidence(&t, state, &bad, err, sizeof err);
        frb_state_free(state);
        if (e != FRB_OK) {
            printf("Certified minima against the journal ................. FAIL\n  %s\n",
                   err[0] ? err : frb_error_str(e));
            printf("Verification: FAIL (%s)\n", frb_error_str(e));
            frb_table_destroy(&t);
            return X_FAIL;
        }
        printf("Certified minima against the journal in %s: PASS (%u backed by its records)\n",
               a->work_dir, (unsigned)info.minimal);
    }
    if (a->recertify) {
        char engine[32];
        RevLevelSolveFn solve = NULL;
        FrbConfig limits;
        FrbRecertify r;
        double t0;
        frb_config_default(&limits, t.bits);
        if (a->max_steps && (!frb_parse_step_limits(a->max_steps, &limits) || limits.passes != 1)) {
            fprintf(stderr, "--recertify takes a single --max-steps value\n");
            frb_table_destroy(&t);
            return X_USAGE;
        }
        if (!select_engine(a->engine, engine, sizeof engine, &solve)) {
            frb_table_destroy(&t);
            return X_USAGE;
        }
        fflush(stdout);
        t0 = cb_now();
        if (a->hardest) {
            e = frb_recertify_hardest(&t, solve, limits.max_steps[0],
                                      a->sample > 0 ? (uint64_t)a->sample : 0, &r);
            printf("The %llu strings certified at the largest minimum, %d nodes, recomputed from node\n"
                   "budget 1, engine %s: %s\n",
                   (unsigned long long)r.hardest, r.nodes, engine, e == FRB_OK ? "PASS" : "FAIL");
        } else {
            e = frb_recertify(&t, solve, limits.max_steps[0], a->sample > 0 ? (uint64_t)a->sample : 0,
                              a->seed, &r);
            printf("Minima recomputed from node budget 1, engine %s: %s\n", engine,
                   e == FRB_OK ? "PASS" : "FAIL");
        }
        printf("  %llu targets solved again in %.2f s (%.4g steps): %llu certified minima reproduced,\n"
               "  %llu differ, %llu uncertified witnesses are larger than the minimum, %llu undecided\n",
               (unsigned long long)r.checked, cb_now() - t0, (double)r.steps,
               (unsigned long long)r.agree, (unsigned long long)r.disagree,
               (unsigned long long)r.tightened, (unsigned long long)r.undecided);
        if (e != FRB_OK) {
            frb_target_format(r.first_bad, t.bits, target);
            printf("  first difference: target %s\n", target);
            printf("Verification: FAIL (%s)\n", frb_error_str(e));
            frb_table_destroy(&t);
            return X_FAIL;
        }
    }
#else
    if (a->work_dir || a->recertify) {
        fprintf(stderr, "--work-dir and --recertify are not available on this platform\n");
        frb_table_destroy(&t);
        return X_USAGE;
    }
#endif

    e = frb_table_validate(&t, &sum, NULL);
    if (e != FRB_OK) {
        frb_table_destroy(&t);
        printf("Verification: FAIL (%s)\n", frb_error_str(e));
        return X_FAIL;
    }
    printf("\n");
    frb_report_summary(stdout, &sum, info.bits);
    if (sum.complete) frb_report_hardest(stdout, &t, 16);
    frb_table_destroy(&t);
    printf("\nVerification: PASS (%s)\n", sum.complete ? "complete certified table" : "partial table");
    return sum.complete ? X_OK : X_PARTIAL;
}

int fixed_run_cli(int argc, char **argv) {
    BombArgs a;
    const char *sub = argc > 2 ? argv[2] : NULL;
    if (!sub) return usage_error("bomb needs a command");
    if (!strcmp(sub, "help") || !strcmp(sub, "--help") || !strcmp(sub, "-h")) {
        fprintf(stderr, "usage:\n");
        fixed_run_cli_usage();
        return X_OK;
    }
    if (!parse_bomb_args(argc, argv, &a)) return usage_error(NULL);
    if (!strcmp(sub, "verify")) return cmd_verify(&a);
    if (!strcmp(sub, "lookup")) return cmd_lookup(&a);
    if (!strcmp(sub, "compact")) return cmd_compact(&a);
    if (!strcmp(sub, "expand")) return cmd_expand(&a);
#ifdef CARLOBOMB_HAVE_BOMB_BUILD
    if (!strcmp(sub, "build")) return cmd_build(&a);
    if (!strcmp(sub, "resume")) return cmd_resume(&a);
    if (!strcmp(sub, "status")) return cmd_status(&a);
    if (!strcmp(sub, "finalize")) return cmd_finalize(&a);
    if (!strcmp(sub, "ladder")) return cmd_ladder(&a);
#else
    if (!strcmp(sub, "build") || !strcmp(sub, "resume") || !strcmp(sub, "status") ||
        !strcmp(sub, "finalize") || !strcmp(sub, "ladder")) {
        fprintf(stderr, "bomb %s needs a POSIX system; verify and lookup work everywhere\n", sub);
        return X_USAGE;
    }
#endif
    fprintf(stderr, "unknown bomb command %s\n", sub);
    return usage_error(NULL);
}
