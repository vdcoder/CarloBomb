/*
 * Construction journal: segment files, records, and the replay that turns
 * them into the state of a construction.  See FORMAT.md, "Journal".
 *
 * Everything known about a construction is derived here from the records,
 * each time it is needed.  The rules of that derivation are the rules of
 * evidence of the project and are kept in one place, derive() below:
 *
 *   - a witness counts only after it has been unpacked, validated and run on
 *     the interpreter of core/rev;
 *   - a node budget is excluded for a string only by a completed exhaustive
 *     enumeration that did not produce the string, by a search that returned
 *     PROVEN_UNSAT, or by the certified minimum of the string's first bits
 *     in the base Bomb the construction builds on.  A search stopped by a
 *     limit excludes nothing;
 *   - a minimum is certified only when a witness has exactly as many nodes
 *     as the first budget that is not excluded.
 *
 * Segments are read as streams, one record at a time, so a journal of any
 * size is replayed in constant memory beyond the state itself.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "fixed_run_build.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "level_solver.h"

static const uint8_t frj_magic[8] = {'C', 'B', 'F', 'R', 'J', 0x0D, 0x0A, 0x1A};

enum { SEG_HEADER_BYTES = 64, MAX_RECORD_BYTES = 80 };

/* Segment header field offsets. */
enum {
    S_MAGIC = 0,
    S_VERSION = 8,
    S_HEADER_SIZE = 10,
    S_BITS = 12,
    S_SYMMETRY = 13,
    S_ENUM_MAX = 14,
    S_RESERVED1 = 15,
    S_REVISION = 16,
    S_NUMBER = 20,
    S_SESSION = 24,
    S_WORKER = 28,
    S_CREATED = 32,
    S_ENGINE = 40, /* 16 bytes */
    S_RESERVED2 = 56,
    S_CRC = 60
};

/* Record field offsets.  The packed graph follows at R_GRAPH; after it come
 * reserved bytes up to the checksum in the last four bytes of the record.
 * Journal version 1 (18-byte graphs): 64 bytes.  Version 2 (33): 80 bytes. */
enum {
    R_TYPE = 0,
    R_RESULT = 1,
    R_BUDGET = 2,
    R_LIMIT = 3,
    R_TARGET = 4,
    R_STEPS = 8,
    R_MAX_STEPS = 16,
    R_ELAPSED = 24,
    R_TIME = 32,
    R_AUX = 36,
    R_NODES = 40,
    R_GRAPH = 41
};

static int record_bytes(const FrbFormat *f) { return (R_GRAPH + f->graph_bytes + 1 + 4 + 7) / 8 * 8; }

struct FrbSegment {
    FILE *f;
    const FrbFormat *format;
};

static void set_err(char *err, size_t errlen, const char *fmt, ...) {
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

static void put64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static int segment_path(char *buf, size_t cap, const char *dir, uint32_t number) {
    int n = snprintf(buf, cap, "%s/journal/%06u.frj", dir, (unsigned)number);
    return n > 0 && (size_t)n < cap;
}

/* ---- writing ------------------------------------------------------------ */

FrbError frb_segment_create(const char *dir, const FrbConfig *cfg, uint32_t number,
                            uint32_t session, uint32_t worker, FrbSegment **seg) {
    uint8_t h[SEG_HEADER_BYTES];
    char path[4096];
    FrbSegment *s;
    if (!dir || !cfg || !seg) return FRB_ERR_ARG;
    *seg = NULL;
    if (!frb_format(cfg->format) || !segment_path(path, sizeof path, dir, number)) return FRB_ERR_ARG;
    s = malloc(sizeof *s);
    if (!s) return FRB_ERR_MEMORY;
    s->format = frb_format(cfg->format);
    s->f = fopen(path, "wbx"); /* exclusive: a segment is never opened twice for writing */
    if (!s->f) {
        free(s);
        return FRB_ERR_IO;
    }
    memset(h, 0, sizeof h);
    memcpy(h + S_MAGIC, frj_magic, sizeof frj_magic);
    put16(h + S_VERSION, (unsigned)cfg->format); /* the journal version is the format version */
    put16(h + S_HEADER_SIZE, SEG_HEADER_BYTES);
    h[S_BITS] = (uint8_t)cfg->bits;
    h[S_SYMMETRY] = (uint8_t)(cfg->symmetry ? 1 : 0);
    h[S_ENUM_MAX] = (uint8_t)cfg->enumerate_max;
    put32(h + S_REVISION, FRB_SEARCH_REVISION);
    put32(h + S_NUMBER, number);
    put32(h + S_SESSION, session);
    put32(h + S_WORKER, worker);
    put64(h + S_CREATED, (uint64_t)time(NULL));
    {
        size_t len = strlen(cfg->engine); /* informational; cut to the field, NUL padded */
        memcpy(h + S_ENGINE, cfg->engine, len < FRB_ENGINE_NAME ? len : FRB_ENGINE_NAME);
    }
    put32(h + S_CRC, frb_crc32(0, h, S_CRC));
    if (fwrite(h, 1, sizeof h, s->f) != sizeof h || fflush(s->f) != 0) {
        fclose(s->f);
        free(s);
        return FRB_ERR_IO;
    }
    *seg = s;
    return FRB_OK;
}

static void record_encode(const FrbFormat *f, const FrbRecord *rec, uint8_t *out) {
    int size = record_bytes(f);
    memset(out, 0, (size_t)size);
    out[R_TYPE] = rec->type;
    out[R_RESULT] = rec->result;
    out[R_BUDGET] = rec->budget;
    out[R_LIMIT] = rec->limit;
    put32(out + R_TARGET, rec->target);
    put64(out + R_STEPS, rec->steps);
    put64(out + R_MAX_STEPS, rec->max_steps);
    put64(out + R_ELAPSED, rec->elapsed_ns);
    put32(out + R_TIME, rec->time);
    put32(out + R_AUX, rec->aux);
    out[R_NODES] = rec->nodes;
    memcpy(out + R_GRAPH, rec->graph, (size_t)f->graph_bytes);
    put32(out + size - 4, frb_crc32(0, out, (size_t)size - 4));
}

/* 1 if the bytes are a record written by record_encode. */
static int record_decode(const FrbFormat *f, const uint8_t *in, FrbRecord *rec) {
    int size = record_bytes(f);
    if (get32(in + size - 4) != frb_crc32(0, in, (size_t)size - 4)) return 0;
    for (int i = R_GRAPH + f->graph_bytes; i < size - 4; i++)
        if (in[i] != 0) return 0;
    if (in[R_TYPE] < FRB_REC_SESSION_BEGIN || in[R_TYPE] > FRB_REC_SEARCH) return 0;
    rec->type = in[R_TYPE];
    rec->result = in[R_RESULT];
    rec->budget = in[R_BUDGET];
    rec->limit = in[R_LIMIT];
    rec->target = get32(in + R_TARGET);
    rec->steps = get64(in + R_STEPS);
    rec->max_steps = get64(in + R_MAX_STEPS);
    rec->elapsed_ns = get64(in + R_ELAPSED);
    rec->time = get32(in + R_TIME);
    rec->aux = get32(in + R_AUX);
    rec->nodes = in[R_NODES];
    memset(rec->graph, 0, sizeof rec->graph);
    memcpy(rec->graph, in + R_GRAPH, (size_t)f->graph_bytes);
    return 1;
}

FrbError frb_segment_append(FrbSegment *seg, const FrbRecord *rec) {
    uint8_t buf[MAX_RECORD_BYTES];
    size_t size;
    if (!seg || !seg->f || !rec) return FRB_ERR_ARG;
    size = (size_t)record_bytes(seg->format);
    record_encode(seg->format, rec, buf);
    return fwrite(buf, 1, size, seg->f) == size ? FRB_OK : FRB_ERR_IO;
}

FrbError frb_segment_flush(FrbSegment *seg, int sync) {
    if (!seg || !seg->f) return FRB_ERR_ARG;
    if (fflush(seg->f) != 0) return FRB_ERR_IO;
    if (sync && fsync(fileno(seg->f)) != 0) return FRB_ERR_IO;
    return FRB_OK;
}

FrbError frb_segment_close(FrbSegment *seg) {
    FrbError e;
    if (!seg) return FRB_OK;
    e = seg->f ? frb_segment_flush(seg, 1) : FRB_OK;
    if (seg->f && fclose(seg->f) != 0 && e == FRB_OK) e = FRB_ERR_IO;
    free(seg);
    return e;
}

/* ---- reading ------------------------------------------------------------ */

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* Segment numbers present in dir/journal, ascending. */
static FrbError list_segments(const char *dir, uint32_t **numbers, size_t *count, char *err,
                              size_t errlen) {
    char path[4096], canon[32];
    size_t n = 0, cap = 0;
    uint32_t *list = NULL;
    struct dirent *de;
    DIR *d;
    *numbers = NULL;
    *count = 0;
    if (snprintf(path, sizeof path, "%s/journal", dir) >= (int)sizeof path) return FRB_ERR_ARG;
    d = opendir(path);
    if (!d) {
        set_err(err, errlen, "cannot read %s: not a FixedRunBomb work directory?", path);
        return FRB_ERR_IO;
    }
    while ((de = readdir(d)) != NULL) {
        char *end;
        unsigned long v = strtoul(de->d_name, &end, 10);
        if (end == de->d_name || strcmp(end, ".frj") != 0 || v > 0xFFFFFFFFul) continue;
        snprintf(canon, sizeof canon, "%06lu.frj", v);
        if (strcmp(canon, de->d_name) != 0) continue; /* only names this code writes */
        if (n == cap) {
            uint32_t *grown = realloc(list, (cap ? cap * 2 : 64) * sizeof *list);
            if (!grown) {
                free(list);
                closedir(d);
                return FRB_ERR_MEMORY;
            }
            list = grown;
            cap = cap ? cap * 2 : 64;
        }
        list[n++] = (uint32_t)v;
    }
    closedir(d);
    if (n) qsort(list, n, sizeof *list, cmp_u32);
    *numbers = list;
    *count = n;
    return FRB_OK;
}

/* Is there a valid record among the records that are still unread? */
static int any_record_ahead(FILE *f, const FrbFormat *format) {
    uint8_t buf[MAX_RECORD_BYTES];
    size_t size = (size_t)record_bytes(format);
    FrbRecord rec;
    while (fread(buf, 1, size, f) == size)
        if (record_decode(format, buf, &rec)) return 1;
    return 0;
}

static FrbError scan_segment(const char *dir, const FrbConfig *cfg, uint32_t number,
                             FrbRecordVisit visit, void *ctx, FrbJournalInfo *info, char *err,
                             size_t errlen) {
    const FrbFormat *format = frb_format(cfg->format);
    uint8_t h[SEG_HEADER_BYTES], buf[MAX_RECORD_BYTES];
    size_t rec_size = (size_t)record_bytes(format), got;
    char path[4096];
    FrbSegmentInfo seg;
    uint64_t size, valid_end;
    long end;
    FILE *f;
    FrbError e = FRB_OK;
    int header_ok;

    if (!segment_path(path, sizeof path, dir, number)) return FRB_ERR_ARG;
    f = fopen(path, "rb");
    if (!f) {
        set_err(err, errlen, "cannot read %s", path);
        return FRB_ERR_IO;
    }
    setvbuf(f, NULL, _IOFBF, (size_t)1 << 20);
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        set_err(err, errlen, "cannot read %s", path);
        return FRB_ERR_IO;
    }
    size = (uint64_t)end; /* a writer may be appending: this much is there now */
    memset(&seg, 0, sizeof seg);
    seg.number = number;
    seg.bytes = size;
    info->segments++;
    info->bytes += size;

    got = fread(h, 1, sizeof h, f);
    header_ok = got == sizeof h && memcmp(h + S_MAGIC, frj_magic, sizeof frj_magic) == 0 &&
                get32(h + S_CRC) == frb_crc32(0, h, S_CRC);
    if (!header_ok) {
        /* A writer that died while creating the file leaves nothing usable;
         * anything else with records in it is damage. */
        int records = got == sizeof h && any_record_ahead(f, format);
        fclose(f);
        if (records) {
            set_err(err, errlen, "%s: damaged header", path);
            return FRB_ERR_JOURNAL;
        }
        info->torn_segments++;
        info->torn_bytes += size;
        return FRB_OK;
    }
    if ((int)get16(h + S_VERSION) != cfg->format || get16(h + S_HEADER_SIZE) != SEG_HEADER_BYTES) {
        set_err(err, errlen, "%s: journal format version %u, this work directory uses version %d",
                path, get16(h + S_VERSION), cfg->format);
        e = FRB_ERR_INCOMPATIBLE;
    } else if (get32(h + S_REVISION) != FRB_SEARCH_REVISION) {
        set_err(err, errlen, "%s: written by search revision %u, this program is revision %d", path,
                (unsigned)get32(h + S_REVISION), FRB_SEARCH_REVISION);
        e = FRB_ERR_INCOMPATIBLE;
    } else if (h[S_BITS] != cfg->bits || h[S_SYMMETRY] != (cfg->symmetry ? 1 : 0) ||
               h[S_ENUM_MAX] != cfg->enumerate_max || get32(h + S_NUMBER) != number) {
        set_err(err, errlen,
                "%s: written with bits %d, symmetry %d, enumeration up to %d; "
                "the work directory is configured for %d, %d, %d",
                path, h[S_BITS], h[S_SYMMETRY], h[S_ENUM_MAX], cfg->bits, cfg->symmetry ? 1 : 0,
                cfg->enumerate_max);
        e = FRB_ERR_INCOMPATIBLE;
    }
    if (e != FRB_OK) {
        fclose(f);
        return e;
    }
    seg.session = get32(h + S_SESSION);
    seg.worker = get32(h + S_WORKER);
    seg.created = get64(h + S_CREATED);
    memcpy(seg.engine, h + S_ENGINE, FRB_ENGINE_NAME);
    seg.engine[FRB_ENGINE_NAME] = '\0';

    if (visit) {
        int v = visit(ctx, &seg, NULL);
        if (v) {
            fclose(f);
            return (FrbError)v;
        }
    }
    valid_end = SEG_HEADER_BYTES;
    while (fread(buf, 1, rec_size, f) == rec_size) {
        FrbRecord rec;
        if (!record_decode(format, buf, &rec)) {
            if (any_record_ahead(f, format)) {
                set_err(err, errlen, "%s: damaged record at byte %llu, with valid records after it",
                        path, (unsigned long long)valid_end);
                fclose(f);
                return FRB_ERR_JOURNAL;
            }
            break; /* the tail of an interrupted writer */
        }
        valid_end += rec_size;
        seg.records++;
        if (visit) {
            int v = visit(ctx, &seg, &rec);
            if (v) {
                fclose(f);
                return (FrbError)v;
            }
        }
    }
    fclose(f);
    seg.torn_bytes = size > valid_end ? size - valid_end : 0;
    info->records += seg.records;
    if (seg.torn_bytes) {
        info->torn_segments++;
        info->torn_bytes += seg.torn_bytes;
    }
    return FRB_OK;
}

FrbError frb_journal_scan(const char *dir, const FrbConfig *cfg, FrbRecordVisit visit, void *ctx,
                          FrbJournalInfo *info, char *err, size_t errlen) {
    FrbJournalInfo local;
    uint32_t *numbers;
    size_t count;
    FrbError e;
    if (!dir || !cfg || !frb_format(cfg->format)) return FRB_ERR_ARG;
    if (!info) info = &local;
    memset(info, 0, sizeof *info);
    info->next_segment = 1;
    e = list_segments(dir, &numbers, &count, err, errlen);
    if (e != FRB_OK) return e;
    for (size_t i = 0; i < count && e == FRB_OK; i++) {
        e = scan_segment(dir, cfg, numbers[i], visit, ctx, info, err, errlen);
        info->next_segment = numbers[i] + 1;
    }
    free(numbers);
    return e;
}

/* ---- replay ------------------------------------------------------------- */

typedef struct {
    FrbState *s;
    char *err;
    size_t errlen;
    uint32_t mask;
    /* The enumeration part whose witnesses are being read in this segment. */
    int cur_nodes, cur_part;
    uint32_t cur_count;
} Loader;

const uint8_t *frb_state_graph(const FrbState *state, uint32_t index) {
    return state->graphs + (size_t)index * (size_t)state->format->graph_bytes;
}

/* Which of two witnesses of the same size is kept: the base Bomb's first,
 * then enumeration, then search, then the ring. */
static int rank(int source) {
    switch (source) {
    case FRB_SRC_BASE: return 0;
    case FRB_SRC_ENUM: return 1;
    case FRB_SRC_SEARCH: return 2;
    default: return 3;
    }
}

static int better(const FrbTargetState *ts, int nodes, int source, int part) {
    if (ts->nodes == 0) return 1;
    if (nodes != ts->nodes) return nodes < ts->nodes;
    if (source != ts->source) return rank(source) < rank(ts->source);
    if (source == FRB_SRC_ENUM) return part < ts->part; /* first in enumeration order */
    return 0;                                           /* otherwise first read stays */
}

static void offer(FrbState *s, uint32_t index, int nodes, int source, int part, int mirrored,
                  const uint8_t *graph) {
    FrbTargetState *ts = &s->targets[index];
    size_t bytes = (size_t)s->format->graph_bytes;
    if (source == FRB_SRC_ENUM && (ts->enum_nodes == 0 || nodes < ts->enum_nodes))
        ts->enum_nodes = (uint8_t)nodes;
    if (!better(ts, nodes, source, part)) return;
    ts->nodes = (uint8_t)nodes;
    ts->source = (uint8_t)source;
    ts->mirrored = (uint8_t)mirrored;
    ts->part = (uint16_t)part;
    if (graph) memcpy(s->graphs + (size_t)index * bytes, graph, bytes);
    else memset(s->graphs + (size_t)index * bytes, 0, bytes);
}

static int bad_record(Loader *L, const FrbSegmentInfo *seg, const char *what) {
    set_err(L->err, L->errlen, "journal segment %06u, record %llu: %s", (unsigned)seg->number,
            (unsigned long long)seg->records, what);
    return (int)FRB_ERR_JOURNAL;
}

/* Admit a verified graph as a witness for `target` and, under symmetry, its
 * complement as a witness for the complementary target.  Nothing is taken on
 * trust: the complement is run as well.  Returns 0, or 1 if the complement
 * does not emit the complement (which no REV can do). */
static int admit(FrbState *s, uint32_t mask, uint32_t target, const RevGraph *g, int source, int part) {
    uint8_t packed[FRB_MAX_GRAPH_BYTES];
    RevGraph c;
    if (frb_graph_pack_as(s->format, g, packed) != FRB_OK) return 1;
    offer(s, target, g->node_count, source, part, 0, packed);
    if (s->symmetry) {
        frb_graph_complement(g, &c);
        if (frb_graph_output(&c, s->bits) != (target ^ mask)) return 1;
        if (frb_graph_pack_as(s->format, &c, packed) != FRB_OK) return 1;
        offer(s, target ^ mask, g->node_count, source, part, 1, packed);
    }
    return 0;
}

static int take_witness(Loader *L, const FrbSegmentInfo *seg, const FrbRecord *rec, int source,
                        int part) {
    FrbState *s = L->s;
    RevGraph g;
    if (frb_graph_unpack_as(s->format, rec->graph, rec->nodes, &g) != FRB_OK)
        return bad_record(L, seg, "malformed graph");
    if (frb_graph_output(&g, s->bits) != rec->target)
        return bad_record(L, seg, "graph does not emit the target it is recorded for");
    if (admit(s, L->mask, rec->target, &g, source, part))
        return bad_record(L, seg, "complement graph does not emit the complement target");
    return 0;
}

static void note_limit(FrbTargetState *ts, const FrbRecord *rec) {
    uint64_t was = ts->limit_max_steps ? ts->limit_max_steps : UINT64_MAX;
    uint64_t now = rec->max_steps ? rec->max_steps : UINT64_MAX;
    if (rec->budget < ts->limit_budget) return;
    if (rec->budget == ts->limit_budget && ts->limit_kind == rec->limit && now <= was) return;
    ts->limit_budget = rec->budget;
    ts->limit_kind = rec->limit;
    ts->limit_max_steps = rec->max_steps;
}

static int replay(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Loader *L = ctx;
    FrbState *s = L->s;

    if (!rec) { /* a new segment: a part left open by the previous one stays open */
        L->cur_nodes = L->cur_part = -1;
        L->cur_count = 0;
        if (seg->session > s->last_session) s->last_session = seg->session;
        return 0;
    }
    if (rec->time) {
        if (s->first_time == 0 || rec->time < s->first_time) s->first_time = rec->time;
        if (rec->time > s->last_time) s->last_time = rec->time;
    }

    switch ((FrbRecordType)rec->type) {
    case FRB_REC_SESSION_BEGIN:
        s->sessions++;
        s->sessions_unfinished++;
        return 0;

    case FRB_REC_SESSION_END:
        if (s->sessions_unfinished) s->sessions_unfinished--;
        s->session_ns += rec->elapsed_ns;
        return 0;

    case FRB_REC_ENUM_WITNESS: {
        int n = rec->budget, part = (int)rec->aux;
        if (n < 1 || n > FRB_NODE_CAPACITY || rec->nodes != n || rec->aux >= (uint32_t)(n * n) ||
            rec->target > L->mask)
            return bad_record(L, seg, "impossible enumeration witness");
        if (n != L->cur_nodes || part != L->cur_part) {
            L->cur_nodes = n;
            L->cur_part = part;
            L->cur_count = 0;
        }
        L->cur_count++;
        return take_witness(L, seg, rec, FRB_SRC_ENUM, part);
    }

    case FRB_REC_ENUM_PART: {
        int n = rec->budget, part = (int)rec->aux;
        uint32_t seen;
        if (n < 1 || n > FRB_NODE_CAPACITY || rec->aux >= (uint32_t)(n * n))
            return bad_record(L, seg, "impossible enumeration part");
        /* The part's claim "these are all the strings I found" is only as
         * good as the list in front of it. */
        seen = (n == L->cur_nodes && part == L->cur_part) ? L->cur_count : 0;
        L->cur_nodes = L->cur_part = -1;
        L->cur_count = 0;
        if (seen != rec->target)
            return bad_record(L, seg, "enumeration part does not match its witness records");
        if (rec->steps != frb_enum_part_graphs(n, s->symmetry))
            return bad_record(L, seg, "enumeration part ran the wrong number of graphs");
        if (!(s->part_done[n][part >> 3] & (1u << (part & 7)))) {
            s->part_done[n][part >> 3] |= (uint8_t)(1u << (part & 7));
            s->parts_done[n]++;
            s->enum_graphs[n] += rec->steps;
            s->enum_ns[n] += rec->elapsed_ns;
        }
        return 0;
    }

    case FRB_REC_SEARCH: {
        FrbTargetState *ts;
        FrbBudgetEffort *be;
        int b = rec->budget;
        if (b < 1 || b > s->format->node_capacity || rec->target > L->mask)
            return bad_record(L, seg, "impossible search record");
        ts = &s->targets[rec->target];
        be = &s->budget[b];
        ts->searches++;
        ts->steps += rec->steps;
        ts->search_ns += rec->elapsed_ns;
        be->searches++;
        be->steps += rec->steps;
        be->ns += rec->elapsed_ns;
        L->cur_nodes = L->cur_part = -1; /* a search ends any run of enumeration witnesses */
        switch ((RevSolveResult)rec->result) {
        case REV_SOLVE_SOLVED:
            be->solved++;
            if (rec->nodes < 1 || rec->nodes > b)
                return bad_record(L, seg, "search witness exceeds its node budget");
            return take_witness(L, seg, rec, FRB_SRC_SEARCH, 0);
        case REV_SOLVE_PROVEN_UNSAT:
            be->unsat++;
            if (rec->nodes != 0) return bad_record(L, seg, "unsat record carries a graph");
            if (b > ts->unsat_max) ts->unsat_max = (uint8_t)b;
            if (s->symmetry) {
                FrbTargetState *tc = &s->targets[rec->target ^ L->mask];
                if (b > tc->unsat_max) tc->unsat_max = (uint8_t)b;
            }
            return 0;
        case REV_SOLVE_RESOURCE_LIMIT:
            be->limited++;
            if (rec->nodes != 0 || (rec->limit != REV_LIMIT_STACK && rec->limit != REV_LIMIT_STEPS))
                return bad_record(L, seg, "impossible resource-limit record");
            note_limit(ts, rec);
            if (s->symmetry) note_limit(&s->targets[rec->target ^ L->mask], rec);
            return 0;
        default:
            return bad_record(L, seg, "search record with an unknown result");
        }
    }
    }
    return bad_record(L, seg, "unknown record type");
}

/*
 * The base Bomb.  For each of its strings q, with certified minimum m(q):
 * the stored REV is run for `bits` bits.  It must emit q first (it is run,
 * not trusted), and what it emits in all is one string of this construction
 * that therefore has a REV of m(q) nodes.  base_nodes[q] = m(q) is kept for
 * the lower bounds: no string that starts with q can need fewer.
 */
static FrbError seed_from_base(FrbState *s, const FrbConfig *cfg, uint8_t **base_nodes, char *err,
                               size_t errlen) {
    int shift = cfg->bits - cfg->base_bits;
    uint32_t mask = s->count - 1u;
    FrbFileInfo info;
    FrbTable base;
    uint8_t *nodes;
    FrbError e = frb_file_read_raw(cfg->base_file, &base, &info);
    *base_nodes = NULL;
    if (e != FRB_OK) {
        set_err(err, errlen, "cannot use the base Bomb %s: %s", cfg->base_file, frb_error_str(e));
        return e == FRB_ERR_IO ? FRB_ERR_IO : FRB_ERR_INCOMPATIBLE;
    }
    if (info.bits != cfg->base_bits || info.table_crc != cfg->base_crc) {
        set_err(err, errlen,
                "%s is not the Bomb this construction was started on (%d bits, checksum %08x; "
                "expected %d bits, %08x)",
                cfg->base_file, info.bits, (unsigned)info.table_crc, cfg->base_bits,
                (unsigned)cfg->base_crc);
        frb_table_destroy(&base);
        return FRB_ERR_INCOMPATIBLE;
    }
    if (!info.complete) {
        set_err(err, errlen, "the base Bomb %s is not complete: nothing can be built on it",
                cfg->base_file);
        frb_table_destroy(&base);
        return FRB_ERR_INCOMPLETE;
    }
    nodes = malloc(base.count);
    if (!nodes) {
        frb_table_destroy(&base);
        return FRB_ERR_MEMORY;
    }
    for (uint32_t q = 0; q < base.count && e == FRB_OK; q++) {
        FrbStatus st;
        RevGraph g;
        uint32_t emitted;
        e = frb_entry_decode_as(base.format, frb_table_entry(&base, q), &st, &g);
        if (e == FRB_OK && st != FRB_MINIMAL) e = FRB_ERR_INCOMPLETE;
        if (e != FRB_OK) break;
        emitted = frb_graph_output(&g, cfg->bits);
        if ((emitted >> shift) != q) {
            e = FRB_ERR_OUTPUT;
            break;
        }
        nodes[q] = (uint8_t)g.node_count;
        if (admit(s, mask, emitted, &g, FRB_SRC_BASE, 0)) e = FRB_ERR_OUTPUT;
    }
    frb_table_destroy(&base);
    if (e != FRB_OK) {
        set_err(err, errlen, "the base Bomb %s has a bad entry: %s", cfg->base_file, frb_error_str(e));
        free(nodes);
        return e;
    }
    *base_nodes = nodes;
    return FRB_OK;
}

/* The rules of evidence.  See the comment at the top of this file. */
static void derive(FrbState *s, const FrbConfig *cfg, const uint8_t *base_nodes) {
    int shift = cfg->base_bits ? cfg->bits - cfg->base_bits : 0;
    int capacity = s->format->node_capacity;
    int e = 0;

    for (int n = 1; n <= s->enumerate_max && n <= FRB_NODE_CAPACITY; n++) {
        if (s->parts_done[n] != (uint32_t)(n * n)) break;
        e = n;
    }
    s->enum_complete = e;
    memset(s->by_status, 0, sizeof s->by_status);
    s->conflicts = 0;
    s->base_settled = 0;

    for (uint32_t i = 0; i < s->count; i++) {
        FrbTargetState *ts = &s->targets[i];
        int excluded = ts->unsat_max; /* a search that found nothing with at most b nodes
                                         excludes every budget up to b */
        /* The ring emits the string with as many nodes as it has bits.  It
         * is generated when a table is written, not stored here. */
        if (cfg->fallback_ring && ts->nodes == 0 && s->bits <= capacity)
            offer(s, i, s->bits, FRB_SRC_RING, 0, 0, NULL);
        /* A complete enumeration of n-node graphs excludes budget n for
         * every string it did not produce. */
        if (e > excluded) {
            int upto = e;
            if (ts->nodes && ts->nodes - 1 < upto) upto = ts->nodes - 1;
            if (upto > excluded) excluded = upto;
        }
        /* A string cannot need fewer nodes than its first bits do. */
        if (base_nodes && base_nodes[i >> shift] - 1 > excluded) excluded = base_nodes[i >> shift] - 1;
        ts->lower_bound = (uint8_t)(excluded + 1);

        ts->conflict = 0;
        /* A witness inside an excluded budget. */
        if (ts->nodes && ts->lower_bound > ts->nodes) ts->conflict = 1;
        /* A witness smaller than anything a complete enumeration produced. */
        if (ts->nodes && ts->nodes <= e && ts->enum_nodes != ts->nodes) ts->conflict = 1;

        if (ts->nodes && !ts->conflict && ts->lower_bound == ts->nodes) ts->status = FRB_MINIMAL;
        else if (ts->nodes) ts->status = FRB_WITNESS;
        else if (ts->lower_bound > capacity) ts->status = FRB_UNSUPPORTED;
        else if (ts->lower_bound > 1 || ts->limit_budget) ts->status = FRB_UNRESOLVED;
        else ts->status = FRB_EMPTY;

        s->by_status[ts->status]++;
        s->conflicts += ts->conflict;
        if (ts->status == FRB_MINIMAL && ts->source == FRB_SRC_BASE) s->base_settled++;
    }
}

FrbError frb_state_load(const char *dir, const FrbConfig *cfg, FrbState **state, char *err,
                        size_t errlen) {
    uint8_t *base_nodes = NULL;
    Loader L;
    FrbState *s;
    FrbError e;
    if (!dir || !cfg || !state) return FRB_ERR_ARG;
    *state = NULL;
    e = frb_config_check(cfg, err, errlen);
    if (e != FRB_OK) return e;
    s = calloc(1, sizeof *s);
    if (!s) return FRB_ERR_MEMORY;
    s->bits = cfg->bits;
    s->symmetry = cfg->symmetry ? 1 : 0;
    s->enumerate_max = cfg->enumerate_max;
    s->base_bits = cfg->base_bits;
    s->format = frb_format(cfg->format);
    s->count = (uint32_t)1 << cfg->bits;
    s->targets = calloc(s->count, sizeof *s->targets);
    s->graphs = calloc(s->count, (size_t)s->format->graph_bytes);
    if (!s->targets || !s->graphs) {
        frb_state_free(s);
        set_err(err, errlen, "not enough memory for the state of a %d-bit construction", cfg->bits);
        return FRB_ERR_MEMORY;
    }
    memset(&L, 0, sizeof L);
    L.s = s;
    L.err = err;
    L.errlen = errlen;
    L.mask = s->count - 1u;
    L.cur_nodes = L.cur_part = -1;
    e = frb_journal_scan(dir, cfg, replay, &L, &s->journal, err, errlen);
    if (e == FRB_OK && cfg->base_bits) e = seed_from_base(s, cfg, &base_nodes, err, errlen);
    if (e != FRB_OK) {
        frb_state_free(s);
        return e;
    }
    derive(s, cfg, base_nodes);
    free(base_nodes);
    *state = s;
    return FRB_OK;
}

void frb_state_free(FrbState *state) {
    if (!state) return;
    free(state->targets);
    free(state->graphs);
    free(state);
}

FrbError frb_state_table(const FrbState *state, FrbTable *table) {
    FrbError e;
    if (!state || !table) return FRB_ERR_ARG;
    e = frb_table_create_as(table, state->bits, state->format);
    if (e != FRB_OK) return e;
    for (uint32_t i = 0; i < state->count && e == FRB_OK; i++) {
        const FrbTargetState *ts = &state->targets[i];
        RevGraph g;
        if (!ts->nodes) {
            e = frb_table_put(table, i, (FrbStatus)ts->status, NULL);
        } else if (ts->source == FRB_SRC_RING) {
            frb_ring_witness(i, state->bits, &g);
            e = frb_table_put(table, i, (FrbStatus)ts->status, &g);
        } else {
            e = frb_graph_unpack_as(state->format, frb_state_graph(state, i), ts->nodes, &g);
            if (e == FRB_OK) e = frb_table_put(table, i, (FrbStatus)ts->status, &g); /* runs it */
        }
    }
    if (e != FRB_OK) frb_table_destroy(table);
    return e;
}
