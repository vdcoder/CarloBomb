/*
 * FixedRunBomb construction: work directory, journal, restartable build,
 * worker processes, finalization and reports.
 *
 * A work directory holds
 *
 *     config.txt          the settings of the construction (text)
 *     journal/NNNNNN.frj  append-only segments of fixed-size, checksummed
 *                         records: one record per enumerated part, per
 *                         witness found by enumeration, and per LevelSolver
 *                         call
 *     lock                held while a build is running
 *
 * The journal is the only stored result.  Everything else, including the
 * table itself, is derived from it by replaying the records (FrbState), so
 * there is nothing to keep consistent with it and nothing an interruption
 * can leave half-written except the tail of the segment being appended to,
 * which the reader recognizes by its checksum and ignores.  No process ever
 * writes to a segment it did not create.
 *
 * FORMAT.md describes the journal on disk.  POSIX systems only.
 */
#ifndef CARLOBOMB_FIXED_RUN_BUILD_H
#define CARLOBOMB_FIXED_RUN_BUILD_H

#include <stdio.h>

#include "fixed_run.h"
#include "minimal_solver.h" /* RevLevelSolveFn */

#ifdef __cplusplus
extern "C" {
#endif

#define FRB_JOURNAL_VERSION 1

/*
 * Identifies the search whose results the journal records: the LevelSolver
 * move order (which decides the witness a search returns) and the definition
 * of a logical step.  Every engine in this repository implements revision 1
 * exactly, which is why records made by different engines can be mixed.  A
 * change to either must increase this number; a work directory made with
 * another revision is refused.
 */
#define FRB_SEARCH_REVISION 1

#define FRB_MAX_PASSES 8
#define FRB_MAX_WORKERS 1024
#define FRB_ENUMERATE_LIMIT 6 /* 7-node graphs number 8.7e13: not a job for this enumerator */
#define FRB_ENGINE_NAME 16 /* bytes kept in a segment header */
#define FRB_ERR_TEXT 256   /* suggested size of an error-text buffer */

typedef struct {
    /* Fixed when the work directory is created. */
    int bits;          /* target length, 1..31 */
    int format;        /* table format version: 1 (up to 16 bits), 2 (up to 24) or 3 (up to 31) */
    int symmetry;      /* 1: work on one string of each complement pair and derive the other */
    int enumerate_max; /* exhaustive enumeration for node counts 1..enumerate_max; 0 = none */
    /*
     * The ladder: a complete, certified Bomb for a shorter length that this
     * construction builds on.  0 = none.  A string cannot need fewer nodes
     * than its first base_bits bits, and the REV stored for those bits goes
     * on to emit one string of this length, which it thereby settles.
     */
    int base_bits;
    uint32_t base_crc;    /* table checksum of the base Bomb, from its header */
    char base_file[1024]; /* where it is */

    /* May be changed when a construction is resumed. */
    int max_nodes;     /* largest node budget searched, 1..node capacity of the format */
    int fallback_ring; /* 1: the ring stands in as a witness until a better one is known */
    int workers;       /* worker processes, 1..FRB_MAX_WORKERS */
    int passes;        /* number of entries of max_steps in use, 1..FRB_MAX_PASSES */
    uint64_t max_steps[FRB_MAX_PASSES]; /* step limit per LevelSolver call, one pass over the
                                           open targets for each; 0 = no limit */
    char engine[32];   /* engine name as given by the user; informational here */
} FrbConfig;

void frb_config_default(FrbConfig *cfg, int bits);

/* Make `file`, a complete Bomb for fewer bits, the base of the construction
 * described by cfg: reads its header, records its length, checksum and
 * absolute path, and turns enumeration off (the base supplies the bounds). */
FrbError frb_config_set_base(FrbConfig *cfg, const char *file, char *err, size_t errlen);
/* "1000000,100000000,0" -> max_steps and passes.  1 if the text is valid. */
int frb_parse_step_limits(const char *text, FrbConfig *cfg);
FrbError frb_config_check(const FrbConfig *cfg, char *err, size_t errlen);
FrbError frb_config_read(const char *dir, FrbConfig *cfg, char *err, size_t errlen);
FrbError frb_config_write(const char *dir, const FrbConfig *cfg);

/*
 * The state of a construction is held in memory while a round is planned
 * and while its table is written (frb_state_bytes).  FRB_ERR_MEMORY, with
 * the numbers in err, if this machine has less: said before anything is
 * started rather than found out hours into a run.  With a log, a
 * construction of a gigabyte or more is announced there.
 */
FrbError frb_check_memory(const FrbConfig *cfg, FILE *log, char *err, size_t errlen);

/* Create the directory (if needed) and an empty construction in it. */
FrbError frb_work_create(const char *dir, const FrbConfig *cfg, char *err, size_t errlen);

/* ---- journal records ---------------------------------------------------- */

typedef enum {
    FRB_REC_SESSION_BEGIN = 1,
    FRB_REC_SESSION_END = 2,
    FRB_REC_ENUM_WITNESS = 3, /* a graph found by enumeration, first of its part to emit `target` */
    FRB_REC_ENUM_PART = 4,    /* one part of the enumeration ran to completion */
    FRB_REC_SEARCH = 5        /* one LevelSolver call */
} FrbRecordType;

typedef struct {
    uint8_t type;        /* FrbRecordType */
    uint8_t result;      /* SEARCH: RevSolveResult (0 solved, 1 proven unsat, 2 resource limit) */
    uint8_t budget;      /* SEARCH: node budget.  ENUM_*: node count of the graphs */
    uint8_t limit;       /* SEARCH stopped by a limit: RevLevelLimit (1 stack, 2 steps) */
    uint32_t target;     /* SEARCH, ENUM_WITNESS: table index.  ENUM_PART: number of witnesses
                            the part reported.  SESSION_*: session number */
    uint64_t steps;      /* SEARCH: logical steps.  ENUM_PART: graphs run */
    uint64_t max_steps;  /* SEARCH: step limit in force, 0 = none */
    uint64_t elapsed_ns; /* SEARCH, ENUM_PART: duration of the call.  SESSION_END: of the session */
    uint32_t time;       /* when the record was written: Unix time in seconds, low 32 bits */
    uint32_t aux;        /* SEARCH: peak stack.  ENUM_*: part number.  SESSION_BEGIN: workers */
    uint8_t nodes;       /* node count of the graph below, 0 = no graph */
    uint8_t graph[FRB_MAX_GRAPH_BYTES]; /* packed in the construction's table format */
} FrbRecord;

typedef struct {
    uint32_t number;  /* the NNNNNN of the file name */
    uint32_t session; /* construction session that wrote it */
    uint32_t worker;  /* 0 = the coordinating process */
    uint64_t created; /* Unix time */
    char engine[FRB_ENGINE_NAME + 1];
    uint64_t bytes;      /* file length */
    uint64_t records;    /* valid records */
    uint64_t torn_bytes; /* bytes after the last valid record */
} FrbSegmentInfo;

/* A segment open for appending. */
typedef struct FrbSegment FrbSegment;
FrbError frb_segment_create(const char *dir, const FrbConfig *cfg, uint32_t number,
                            uint32_t session, uint32_t worker, FrbSegment **seg);
FrbError frb_segment_append(FrbSegment *seg, const FrbRecord *rec);
FrbError frb_segment_flush(FrbSegment *seg, int sync); /* sync: also force to disk */
FrbError frb_segment_close(FrbSegment *seg);           /* flushes and syncs */

/*
 * Read every segment in ascending number order.  visit is called once with
 * rec == NULL when a segment starts and then once per valid record; a
 * nonzero return stops the scan with FRB_ERR_EVIDENCE.  A segment may end
 * in an incomplete or unreadable tail (an interrupted writer): the tail is
 * skipped and counted.  A damaged record with valid records after it is an
 * error, as is a segment written with other settings.
 */
typedef int (*FrbRecordVisit)(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec);

typedef struct {
    uint32_t segments, torn_segments, next_segment;
    uint64_t bytes, records, torn_bytes;
} FrbJournalInfo;

FrbError frb_journal_scan(const char *dir, const FrbConfig *cfg, FrbRecordVisit visit, void *ctx,
                          FrbJournalInfo *info, char *err, size_t errlen);

/* ---- state derived from the journal ------------------------------------- */

typedef enum {
    FRB_SRC_NONE = 0,
    FRB_SRC_ENUM = 1,   /* exhaustive enumeration */
    FRB_SRC_SEARCH = 2, /* LevelSolver */
    FRB_SRC_RING = 3,   /* the ring construction (no search); its graph is not stored */
    FRB_SRC_BASE = 4    /* the REV the base Bomb stores for the first bits, run on */
} FrbSource;

typedef struct {
    uint8_t nodes;        /* node count of the best known witness, 0 = none */
    uint8_t source;       /* FrbSource of that witness */
    uint8_t mirrored;     /* 1: it is the complement of a witness found for the complement string */
    uint8_t enum_nodes;   /* smallest node count at which enumeration found the string, 0 = never */
    uint8_t unsat_max;    /* largest node budget a search proved to have no solution, 0 = none */
    uint8_t lower_bound;  /* derived: every node budget below this is excluded */
    uint8_t status;       /* derived: FrbStatus */
    uint8_t conflict;     /* derived: the recorded results contradict each other */
    uint8_t limit_budget; /* node budget of the search a resource limit stopped, 0 = none */
    uint8_t limit_kind;   /* its RevLevelLimit */
    uint16_t part;            /* enumeration part of the witness, if it came from enumeration */
    uint32_t searches;        /* LevelSolver calls made for this string itself */
    uint64_t limit_max_steps; /* step limit in force when that search stopped, 0 = none */
    uint64_t steps;           /* logical steps of those calls */
    uint64_t search_ns;       /* their duration */
} FrbTargetState;

typedef struct {
    uint64_t searches, solved, unsat, limited; /* a long construction makes over 2^32 of them */
    uint64_t steps, ns;
} FrbBudgetEffort;

typedef struct {
    int bits, symmetry, enumerate_max, base_bits;
    const FrbFormat *format; /* of the table this construction produces */
    uint32_t count;          /* 2^bits */
    FrbTargetState *targets;
    uint8_t *graphs;         /* count * format->graph_bytes: the packed witness of each target
                                (zero where there is none, or where it is the ring) */

    /* Stage A: enumeration, by node count. */
    uint8_t part_done[FRB_NODE_CAPACITY + 1][(FRB_NODE_CAPACITY * FRB_NODE_CAPACITY + 7) / 8];
    uint32_t parts_done[FRB_NODE_CAPACITY + 1];
    uint64_t enum_graphs[FRB_NODE_CAPACITY + 1];
    uint64_t enum_ns[FRB_NODE_CAPACITY + 1];
    int enum_complete; /* derived: all graphs of 1..enum_complete nodes have been run */

    /* The base Bomb: how many targets its stored REVs settled. */
    uint32_t base_settled;

    /* Stage B: LevelSolver calls, by node budget. */
    FrbBudgetEffort budget[FRB_MAX_NODES + 1];

    /* Sessions and files. */
    uint32_t sessions, sessions_unfinished;
    uint32_t last_session;          /* highest session number in any segment header */
    uint64_t session_ns;            /* wall-clock time of the sessions that ended normally */
    uint64_t first_time, last_time; /* Unix time of the first and last record */
    FrbJournalInfo journal;

    /* Derived counts. */
    uint32_t by_status[5];
    uint32_t conflicts;
} FrbState;

/* Bytes of one journal record for a table format (64, 80, 96), and of the
 * state of a construction in memory: what a session needs at its peak. */
int frb_journal_record_bytes(const FrbFormat *format);
uint64_t frb_state_bytes(int bits, const FrbFormat *format);

/* The packed witness of a target in the state (format->graph_bytes bytes). */
const uint8_t *frb_state_graph(const FrbState *state, uint32_t index);

/* Replay the journal.  cfg supplies the settings segments must match and the
 * two that shape the derived state (fallback_ring, max_nodes). */
FrbError frb_state_load(const char *dir, const FrbConfig *cfg, FrbState **state, char *err,
                        size_t errlen);
void frb_state_free(FrbState *state);

/* The table the state amounts to.  Every witness is re-run as it is stored. */
FrbError frb_state_table(const FrbState *state, FrbTable *table);

/* ---- running a construction --------------------------------------------- */

typedef struct {
    RevLevelSolveFn solve;   /* the LevelSolver engine */
    uint64_t limit_jobs;     /* 0 = no cap; else stop after this many jobs (a job is one
                                enumeration part or one target's search) */
    uint64_t sample;         /* 0 = all; else search only this many open targets, chosen at
                                random (for pilot runs) */
    uint64_t seed;           /* of that choice */
    double time_limit;       /* seconds; 0 = none.  Checked between jobs */
    double progress_seconds; /* interval between progress lines; 0 = none */
    double sync_seconds;     /* interval between forced writes of the journal to disk */
    uint64_t min_free_bytes; /* stop cleanly when the file system of the work directory has
                                less free space than this; 0 = do not watch */
    FILE *log;               /* progress and notices; NULL = silent */
} FrbRunOptions;

typedef struct {
    int complete;      /* every target has a certified minimum */
    int interrupted;   /* a signal ended the session */
    int stopped_early; /* the job cap, the sample or the time limit ended it */
    int disk_low;      /* free disk space fell below min_free_bytes */
    int rounds;
    uint64_t jobs, searches, steps, graphs;
    double seconds;
} FrbRunResult;

void frb_run_options_default(FrbRunOptions *opt);

/*
 * Continue the construction in dir from wherever its journal stands: the
 * remaining enumeration, then one pass over the open targets per entry of
 * cfg->max_steps.  The same call starts a new construction and resumes an
 * old one.  SIGINT and SIGTERM end the session cleanly at the next job
 * boundary; any other death loses at most the records not yet on disk.
 */
FrbError frb_work_run(const char *dir, const FrbConfig *cfg, const FrbRunOptions *opt,
                      FrbRunResult *result, char *err, size_t errlen);

/* Ask a running frb_work_run in this process to stop, as SIGINT would. */
void frb_request_stop(void);

/*
 * Write the table as a FixedRunBomb file.  Unless allow_partial is set this
 * refuses (FRB_ERR_INCOMPLETE, no file written) when any target lacks a
 * certified minimum.  With allow_partial the file is written without the
 * complete flag, which readers report.  Contradictory evidence is refused
 * either way.
 */
FrbError frb_work_finalize(const char *dir, const char *output, int allow_partial,
                           FrbSummary *summary, char *err, size_t errlen);

/*
 * Check a table against the evidence in a work directory: every entry the
 * table calls a certified minimum must be certified, with the same node
 * count, by the journal.
 */
FrbError frb_verify_evidence(const FrbTable *table, const FrbState *state, uint32_t *bad_index,
                             char *err, size_t errlen);

/*
 * Recompute minima from nothing: run the MinimalSolver from node budget 1
 * for every target (or `sample` randomly chosen ones) and compare with the
 * table.  No use is made of the journal, of enumeration or of symmetry.
 */
typedef struct {
    uint64_t checked;    /* targets solved again */
    uint64_t agree;      /* certified entries whose node count was reproduced */
    uint64_t disagree;   /* certified entries with a different minimum: an error */
    uint64_t tightened;  /* uncertified entries whose witness is larger than the minimum */
    uint64_t undecided;  /* the step limit stopped the recomputation */
    uint64_t steps;
    uint32_t first_bad;  /* index of the first disagreement */
    uint64_t hardest;    /* frb_recertify_hardest: entries certified at the largest minimum */
    int nodes;           /* frb_recertify_hardest: that largest minimum */
} FrbRecertify;

FrbError frb_recertify(const FrbTable *table, RevLevelSolveFn solve, uint64_t max_steps,
                       uint64_t sample, uint64_t seed, FrbRecertify *out);

/*
 * The same for exactly the entries that decide the table's headline number:
 * those certified at the largest minimum in it.  All of them in table order,
 * or the first `limit` (0 = all).  That no string needs more is shown by the
 * table's own witnesses, each of which a reader runs; that these strings
 * need as much is what is recomputed here.
 */
FrbError frb_recertify_hardest(const FrbTable *table, RevLevelSolveFn solve, uint64_t max_steps,
                               uint64_t limit, FrbRecertify *out);

/* ---- the ladder ---------------------------------------------------------
 *
 * Build complete Bombs for lengths start + 1, start + 2, ..., to_bits, each
 * from the one before, in one directory:
 *
 *     DIR/fixed-run-NN.frb   the complete Bomb for NN bits
 *     DIR/work-NN/           the construction of level NN (journal: evidence)
 *
 * The starting Bomb is the longest fixed-run-NN.frb in DIR, or, if there is
 * none, `from_file`, which is copied in.  Each level is finalized and read
 * back before the next begins, so the longest complete Bomb is always on
 * disk.  Calling it again continues wherever it stopped.
 */
typedef struct {
    int to_bits;
    int levels_done;    /* levels completed by this call */
    int top_bits;       /* longest complete Bomb in the directory when it returned */
    int complete;       /* top_bits == to_bits */
    FrbRunResult last;  /* of the last session run */
} FrbLadderResult;

FrbError frb_ladder_run(const char *dir, const char *from_file, int to_bits, const FrbConfig *settings,
                        const FrbRunOptions *opt, FrbLadderResult *result, char *err, size_t errlen);

/* ---- reports (statistics.c) --------------------------------------------- */

/* Everything `bomb status` prints: settings, both stages, histogram, effort. */
void frb_report_state(FILE *out, const char *dir, const FrbConfig *cfg, const FrbState *state);

/* One target: what is known, and every journal record that says so. */
FrbError frb_report_target(FILE *out, const char *dir, const FrbConfig *cfg,
                           const FrbState *state, uint32_t index, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
