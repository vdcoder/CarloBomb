/*
 * FixedRunBomb file format: entries, table, file.  See FORMAT.md.
 *
 * Every multi-byte number is assembled and taken apart byte by byte, so the
 * file does not depend on the byte order, the alignment or the structure
 * layout of the machine that wrote it.
 */
#include "fixed_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static const uint8_t frb_magic[8] = {'C', 'B', 'F', 'R', 'B', 0x0D, 0x0A, 0x1A};

/* Header field offsets (FORMAT.md). */
enum {
    H_MAGIC = 0,
    H_VERSION = 8,
    H_HEADER_SIZE = 10,
    H_BITS = 12,
    H_CAPACITY = 13,
    H_NODE_BITS = 14,
    H_ENTRY_SIZE = 15,
    H_COUNT = 16,
    H_FLAGS = 20,
    H_WITNESSES = 24,
    H_MINIMAL = 28,
    H_MAX_MINIMAL = 32,
    H_MAX_WITNESS = 33,
    H_RESERVED1 = 34, /* 2 bytes */
    H_TABLE_CRC = 36,
    H_RESERVED2 = 40, /* 20 bytes */
    H_HEADER_CRC = 60
};

const char *frb_error_str(FrbError e) {
    switch (e) {
    case FRB_OK: return "ok";
    case FRB_ERR_ARG: return "invalid argument";
    case FRB_ERR_MEMORY: return "out of memory";
    case FRB_ERR_IO: return "i/o error";
    case FRB_ERR_MAGIC: return "not a FixedRunBomb file";
    case FRB_ERR_VERSION: return "unsupported format version";
    case FRB_ERR_HEADER: return "damaged or impossible header";
    case FRB_ERR_SIZE: return "file length does not match the header";
    case FRB_ERR_CHECKSUM: return "table checksum mismatch";
    case FRB_ERR_ENTRY: return "malformed entry";
    case FRB_ERR_OUTPUT: return "stored graph does not emit its target";
    case FRB_ERR_SUMMARY: return "header counts or flags disagree with the table";
    case FRB_ERR_CAPACITY: return "graph has more than 16 nodes";
    case FRB_ERR_STOPPED: return "stopped before finishing";
    case FRB_ERR_INCOMPLETE: return "table is not complete";
    case FRB_ERR_JOURNAL: return "damaged construction journal";
    case FRB_ERR_INCOMPATIBLE: return "incompatible work directory";
    case FRB_ERR_EVIDENCE: return "contradictory recorded results";
    case FRB_ERR_BUSY: return "work directory is in use";
    case FRB_ERR_EXISTS: return "work directory already holds a construction";
    case FRB_ERR_UNSUPPORTED: return "not supported on this platform";
    }
    return "unknown error";
}

const char *frb_status_str(FrbStatus s) {
    switch (s) {
    case FRB_EMPTY: return "EMPTY";
    case FRB_WITNESS: return "WITNESS";
    case FRB_MINIMAL: return "MINIMAL";
    case FRB_UNRESOLVED: return "UNRESOLVED";
    case FRB_UNSUPPORTED: return "UNSUPPORTED";
    }
    return "?";
}

/* ---- little-endian integers and bit fields ------------------------------ */

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Bit k of a packed graph is bit (k mod 8) of byte (k div 8). */
static void put_bits(uint8_t *buf, unsigned pos, unsigned width, unsigned value) {
    for (unsigned k = 0; k < width; k++)
        if ((value >> k) & 1u) buf[(pos + k) >> 3] |= (uint8_t)(1u << ((pos + k) & 7u));
}

static unsigned get_bits(const uint8_t *buf, unsigned pos, unsigned width) {
    unsigned v = 0;
    for (unsigned k = 0; k < width; k++)
        v |= (((unsigned)buf[(pos + k) >> 3] >> ((pos + k) & 7u)) & 1u) << k;
    return v;
}

uint32_t frb_crc32(uint32_t crc, const void *data, size_t size) {
    static uint32_t table[256];
    static int ready;
    const uint8_t *p = data;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            table[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* ---- targets and graphs ------------------------------------------------- */

int frb_target_parse(const char *text, int *bits, uint32_t *index) {
    uint32_t v = 0;
    size_t n;
    if (!text) return 0;
    n = strlen(text);
    if (n < 1 || n > FRB_MAX_BITS) return 0;
    for (size_t i = 0; i < n; i++) {
        if (text[i] != '0' && text[i] != '1') return 0;
        v = (v << 1) | (uint32_t)(text[i] - '0');
    }
    if (bits) *bits = (int)n;
    if (index) *index = v;
    return 1;
}

void frb_target_format(uint32_t index, int bits, char out[FRB_MAX_BITS + 1]) {
    if (bits < 0) bits = 0;
    if (bits > FRB_MAX_BITS) bits = FRB_MAX_BITS;
    for (int i = 0; i < bits; i++) out[i] = (char)('0' + ((index >> (bits - 1 - i)) & 1u));
    out[bits] = '\0';
}

uint32_t frb_graph_output(const RevGraph *g, int bits) {
    RevMachine m;
    uint32_t v = 0;
    rev_machine_reset(&m);
    for (int i = 0; i < bits; i++) v = (v << 1) | (uint32_t)rev_machine_next_bit(g, &m);
    return v;
}

void frb_graph_complement(const RevGraph *in, RevGraph *out) {
    int n = in->node_count;
    out->node_count = n;
    for (int i = 0; i < n && i < REV_MAX_NODES; i++) {
        RevNode node = in->nodes[i]; /* in and out may be the same graph */
        out->nodes[i].color = node.color == REV_WHITE ? REV_BLACK : REV_WHITE;
        out->nodes[i].links[0] = node.links[1];
        out->nodes[i].links[1] = node.links[0];
    }
}

void frb_ring_witness(uint32_t index, int bits, RevGraph *out) {
    memset(out, 0, sizeof *out);
    out->node_count = bits;
    for (int i = 0; i < bits; i++) {
        int next = (i + 1) % bits;
        out->nodes[i].links[0] = next;
        out->nodes[i].links[1] = next;
        /* output bit i (first bit = most significant) is the color of node i + 1 */
        out->nodes[next].color = (int)((index >> (bits - 1 - i)) & 1u);
    }
}

/* ---- entries ------------------------------------------------------------ */

FrbError frb_graph_pack(const RevGraph *g, uint8_t out[FRB_GRAPH_BYTES]) {
    if (!g || !out) return FRB_ERR_ARG;
    if (rev_graph_validate(g) != REV_OK) return FRB_ERR_ARG;
    if (g->node_count > FRB_NODE_CAPACITY) return FRB_ERR_CAPACITY;
    memset(out, 0, FRB_GRAPH_BYTES);
    for (int i = 0; i < g->node_count; i++) {
        unsigned v = (unsigned)g->nodes[i].color | ((unsigned)g->nodes[i].links[0] << 1) |
                     ((unsigned)g->nodes[i].links[1] << 5);
        put_bits(out, (unsigned)(FRB_NODE_BITS * i), FRB_NODE_BITS, v);
    }
    return FRB_OK;
}

FrbError frb_graph_unpack(const uint8_t in[FRB_GRAPH_BYTES], int node_count, RevGraph *g) {
    if (!in || !g) return FRB_ERR_ARG;
    memset(g, 0, sizeof *g);
    if (node_count < 1 || node_count > FRB_NODE_CAPACITY) return FRB_ERR_ENTRY;
    for (int i = 0; i < node_count; i++) {
        unsigned v = get_bits(in, (unsigned)(FRB_NODE_BITS * i), FRB_NODE_BITS);
        int l0 = (int)((v >> 1) & 15u), l1 = (int)((v >> 5) & 15u);
        if (l0 >= node_count || l1 >= node_count) return FRB_ERR_ENTRY;
        g->nodes[i].color = (int)(v & 1u);
        g->nodes[i].links[0] = l0;
        g->nodes[i].links[1] = l1;
    }
    for (int i = node_count; i < FRB_NODE_CAPACITY; i++)
        if (get_bits(in, (unsigned)(FRB_NODE_BITS * i), FRB_NODE_BITS) != 0) return FRB_ERR_ENTRY;
    g->node_count = node_count;
    return FRB_OK;
}

static int has_witness(FrbStatus s) { return s == FRB_WITNESS || s == FRB_MINIMAL; }

FrbError frb_entry_encode(FrbStatus status, const RevGraph *g, uint8_t out[FRB_ENTRY_BYTES]) {
    if (!out || (int)status < 0 || (int)status > (int)FRB_UNSUPPORTED) return FRB_ERR_ARG;
    memset(out, 0, FRB_ENTRY_BYTES);
    if (has_witness(status)) {
        FrbError e;
        if (!g) return FRB_ERR_ARG;
        e = frb_graph_pack(g, out);
        if (e != FRB_OK) return e;
        out[FRB_GRAPH_BYTES] = (uint8_t)(((unsigned)status << 5) | (unsigned)g->node_count);
    } else {
        if (g) return FRB_ERR_ARG;
        out[FRB_GRAPH_BYTES] = (uint8_t)((unsigned)status << 5);
    }
    return FRB_OK;
}

FrbError frb_entry_decode(const uint8_t in[FRB_ENTRY_BYTES], FrbStatus *status, RevGraph *g) {
    RevGraph local;
    unsigned meta, code, nodes;
    if (!in) return FRB_ERR_ARG;
    if (!g) g = &local;
    meta = in[FRB_GRAPH_BYTES];
    code = meta >> 5;
    nodes = meta & 31u;
    memset(g, 0, sizeof *g);
    if (code > (unsigned)FRB_UNSUPPORTED) return FRB_ERR_ENTRY;
    if (status) *status = (FrbStatus)code;
    if (has_witness((FrbStatus)code)) return frb_graph_unpack(in, (int)nodes, g);
    if (nodes != 0) return FRB_ERR_ENTRY;
    for (int i = 0; i < FRB_GRAPH_BYTES; i++)
        if (in[i] != 0) return FRB_ERR_ENTRY;
    return FRB_OK;
}

/* ---- table -------------------------------------------------------------- */

FrbError frb_table_create(FrbTable *t, int bits) {
    if (!t) return FRB_ERR_ARG;
    t->bits = 0;
    t->count = 0;
    t->entries = NULL;
    if (bits < 1 || bits > FRB_MAX_BITS) return FRB_ERR_ARG;
    t->entries = calloc((size_t)1 << bits, FRB_ENTRY_BYTES); /* all zero = FRB_EMPTY */
    if (!t->entries) return FRB_ERR_MEMORY;
    t->bits = bits;
    t->count = (uint32_t)1 << bits;
    return FRB_OK;
}

void frb_table_destroy(FrbTable *t) {
    if (!t) return;
    free(t->entries);
    t->entries = NULL;
    t->count = 0;
    t->bits = 0;
}

FrbError frb_table_put(FrbTable *t, uint32_t index, FrbStatus status, const RevGraph *g) {
    uint8_t entry[FRB_ENTRY_BYTES];
    FrbError e;
    if (!t || !t->entries || index >= t->count) return FRB_ERR_ARG;
    e = frb_entry_encode(status, g, entry);
    if (e != FRB_OK) return e;
    if (g && frb_graph_output(g, t->bits) != index) return FRB_ERR_OUTPUT;
    memcpy(t->entries + (size_t)index * FRB_ENTRY_BYTES, entry, FRB_ENTRY_BYTES);
    return FRB_OK;
}

static FrbError check_entry(const uint8_t *entry, int bits, uint32_t index, FrbStatus *status,
                            RevGraph *g) {
    FrbError e = frb_entry_decode(entry, status, g);
    if (e != FRB_OK) return e;
    if (g->node_count > 0 && frb_graph_output(g, bits) != index) return FRB_ERR_OUTPUT;
    return FRB_OK;
}

FrbError frb_table_get(const FrbTable *t, uint32_t index, FrbStatus *status, RevGraph *g) {
    RevGraph local;
    FrbStatus st;
    if (!t || !t->entries || index >= t->count) return FRB_ERR_ARG;
    if (!g) g = &local;
    if (!status) status = &st;
    return check_entry(t->entries + (size_t)index * FRB_ENTRY_BYTES, t->bits, index, status, g);
}

FrbError frb_table_validate(const FrbTable *t, FrbSummary *summary, uint32_t *bad_index) {
    FrbSummary s;
    RevGraph g;
    if (!t || !t->entries || t->bits < 1 || t->bits > FRB_MAX_BITS ||
        t->count != (uint32_t)1 << t->bits)
        return FRB_ERR_ARG;
    memset(&s, 0, sizeof s);
    s.count = t->count;
    for (uint32_t i = 0; i < t->count; i++) {
        FrbStatus st;
        FrbError e = check_entry(t->entries + (size_t)i * FRB_ENTRY_BYTES, t->bits, i, &st, &g);
        if (e != FRB_OK) {
            if (bad_index) *bad_index = i;
            return e;
        }
        s.by_status[st]++;
        if (st == FRB_MINIMAL) {
            s.minimal_by_nodes[g.node_count]++;
            if (g.node_count > s.max_minimal_nodes) s.max_minimal_nodes = g.node_count;
        } else if (st == FRB_WITNESS) {
            s.witness_by_nodes[g.node_count]++;
        }
        if (g.node_count > s.max_witness_nodes) s.max_witness_nodes = g.node_count;
    }
    s.complete = s.by_status[FRB_MINIMAL] == s.count;
    if (summary) *summary = s;
    return FRB_OK;
}

FrbError frb_table_check_symmetry(const FrbTable *t, uint32_t *bad_index) {
    uint32_t mask;
    if (!t || !t->entries || t->count != (uint32_t)1 << t->bits) return FRB_ERR_ARG;
    mask = t->count - 1u;
    for (uint32_t i = 0; i < t->count; i++) {
        RevGraph g, c, other;
        FrbStatus st, st_other;
        FrbError e = frb_table_get(t, i, &st, &g);
        if (e == FRB_OK) e = frb_table_get(t, i ^ mask, &st_other, &other);
        if (e == FRB_OK && g.node_count > 0) {
            frb_graph_complement(&g, &c);
            if (rev_graph_validate(&c) != REV_OK || frb_graph_output(&c, t->bits) != (i ^ mask))
                e = FRB_ERR_EVIDENCE;
            /* The complement of this witness is a witness for the other
             * string, so a minimum certified there cannot be larger. */
            if (st_other == FRB_MINIMAL && other.node_count > g.node_count) e = FRB_ERR_EVIDENCE;
            if (st == FRB_MINIMAL && st_other == FRB_MINIMAL && other.node_count != g.node_count)
                e = FRB_ERR_EVIDENCE;
        }
        if (e != FRB_OK) {
            if (bad_index) *bad_index = i;
            return e;
        }
    }
    return FRB_OK;
}

/* ---- file image --------------------------------------------------------- */

FrbError frb_image_build(const FrbTable *t, uint8_t **image, size_t *size) {
    FrbSummary s;
    uint8_t *buf;
    size_t table_bytes, total;
    FrbError e;
    if (!image || !size) return FRB_ERR_ARG;
    *image = NULL;
    *size = 0;
    e = frb_table_validate(t, &s, NULL);
    if (e != FRB_OK) return e;
    table_bytes = (size_t)t->count * FRB_ENTRY_BYTES;
    total = FRB_HEADER_BYTES + table_bytes;
    buf = calloc(1, total);
    if (!buf) return FRB_ERR_MEMORY;

    memcpy(buf + H_MAGIC, frb_magic, sizeof frb_magic);
    put16(buf + H_VERSION, FRB_FORMAT_VERSION);
    put16(buf + H_HEADER_SIZE, FRB_HEADER_BYTES);
    buf[H_BITS] = (uint8_t)t->bits;
    buf[H_CAPACITY] = FRB_NODE_CAPACITY;
    buf[H_NODE_BITS] = FRB_NODE_BITS;
    buf[H_ENTRY_SIZE] = FRB_ENTRY_BYTES;
    put32(buf + H_COUNT, t->count);
    put32(buf + H_FLAGS, s.complete ? FRB_FLAG_COMPLETE : 0u);
    put32(buf + H_WITNESSES, s.by_status[FRB_WITNESS] + s.by_status[FRB_MINIMAL]);
    put32(buf + H_MINIMAL, s.by_status[FRB_MINIMAL]);
    buf[H_MAX_MINIMAL] = (uint8_t)s.max_minimal_nodes;
    buf[H_MAX_WITNESS] = (uint8_t)s.max_witness_nodes;
    memcpy(buf + FRB_HEADER_BYTES, t->entries, table_bytes);
    put32(buf + H_TABLE_CRC, frb_crc32(0, buf + FRB_HEADER_BYTES, table_bytes));
    put32(buf + H_HEADER_CRC, frb_crc32(0, buf, H_HEADER_CRC));

    *image = buf;
    *size = total;
    return FRB_OK;
}

/* Everything that can be said about a header alone. */
static FrbError parse_header(const uint8_t *h, size_t available, FrbFileInfo *info) {
    FrbFileInfo local;
    if (!info) info = &local;
    memset(info, 0, sizeof *info);
    if (available < sizeof frb_magic || memcmp(h + H_MAGIC, frb_magic, sizeof frb_magic) != 0)
        return FRB_ERR_MAGIC;
    if (available < H_HEADER_SIZE) return FRB_ERR_SIZE;
    info->version = (int)get16(h + H_VERSION);
    if (info->version != FRB_FORMAT_VERSION) return FRB_ERR_VERSION;
    if (available < FRB_HEADER_BYTES) return FRB_ERR_SIZE;
    if (get16(h + H_HEADER_SIZE) != FRB_HEADER_BYTES) return FRB_ERR_HEADER;
    if (get32(h + H_HEADER_CRC) != frb_crc32(0, h, H_HEADER_CRC)) return FRB_ERR_HEADER;

    info->bits = h[H_BITS];
    info->node_capacity = h[H_CAPACITY];
    info->count = get32(h + H_COUNT);
    info->flags = get32(h + H_FLAGS);
    info->witnesses = get32(h + H_WITNESSES);
    info->minimal = get32(h + H_MINIMAL);
    info->max_minimal_nodes = h[H_MAX_MINIMAL];
    info->max_witness_nodes = h[H_MAX_WITNESS];
    info->table_crc = get32(h + H_TABLE_CRC);
    info->complete = (info->flags & FRB_FLAG_COMPLETE) != 0;

    if (info->bits < 1 || info->bits > FRB_MAX_BITS) return FRB_ERR_HEADER;
    if (info->node_capacity != FRB_NODE_CAPACITY || h[H_NODE_BITS] != FRB_NODE_BITS ||
        h[H_ENTRY_SIZE] != FRB_ENTRY_BYTES)
        return FRB_ERR_HEADER;
    if (info->count != (uint32_t)1 << info->bits) return FRB_ERR_HEADER;
    if (info->flags & ~FRB_FLAG_COMPLETE) return FRB_ERR_HEADER;
    for (int i = H_RESERVED1; i < H_TABLE_CRC; i++)
        if (h[i] != 0) return FRB_ERR_HEADER;
    for (int i = H_RESERVED2; i < H_HEADER_CRC; i++)
        if (h[i] != 0) return FRB_ERR_HEADER;
    return FRB_OK;
}

static FrbError check_summary(const FrbSummary *s, const FrbFileInfo *info) {
    if (info->witnesses != s->by_status[FRB_WITNESS] + s->by_status[FRB_MINIMAL] ||
        info->minimal != s->by_status[FRB_MINIMAL] ||
        info->max_minimal_nodes != s->max_minimal_nodes ||
        info->max_witness_nodes != s->max_witness_nodes || info->complete != s->complete)
        return FRB_ERR_SUMMARY;
    return FRB_OK;
}

FrbError frb_image_parse(const uint8_t *image, size_t size, FrbTable *t, FrbFileInfo *info,
                         uint32_t *bad_index) {
    FrbFileInfo local;
    FrbSummary s;
    FrbTable table;
    size_t table_bytes;
    FrbError e;
    if (t) {
        t->bits = 0;
        t->count = 0;
        t->entries = NULL;
    }
    if (!image) return FRB_ERR_ARG;
    if (!info) info = &local;
    e = parse_header(image, size, info);
    if (e != FRB_OK) return e;
    table_bytes = (size_t)info->count * FRB_ENTRY_BYTES;
    if (size != FRB_HEADER_BYTES + table_bytes) return FRB_ERR_SIZE;
    if (frb_crc32(0, image + FRB_HEADER_BYTES, table_bytes) != info->table_crc)
        return FRB_ERR_CHECKSUM;

    e = frb_table_create(&table, info->bits);
    if (e != FRB_OK) return e;
    memcpy(table.entries, image + FRB_HEADER_BYTES, table_bytes);
    e = frb_table_validate(&table, &s, bad_index);
    if (e == FRB_OK) e = check_summary(&s, info);
    if (e != FRB_OK || !t) frb_table_destroy(&table);
    else *t = table;
    return e;
}

/* ---- files -------------------------------------------------------------- */

FrbError frb_write_atomic(const char *path, const void *data, size_t size) {
    char *tmp;
    size_t n;
    FILE *f;
    int ok;
    if (!path || (!data && size)) return FRB_ERR_ARG;
    n = strlen(path);
    tmp = malloc(n + 5);
    if (!tmp) return FRB_ERR_MEMORY;
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".tmp", 5);

    f = fopen(tmp, "wb");
    if (!f) {
        free(tmp);
        return FRB_ERR_IO;
    }
    ok = size == 0 || fwrite(data, 1, size, f) == size;
    ok = ok && fflush(f) == 0;
#if !defined(_WIN32)
    ok = ok && fsync(fileno(f)) == 0; /* the data is on disk before the name changes */
#endif
    ok = (fclose(f) == 0) && ok;
#if defined(_WIN32)
    if (ok) remove(path); /* rename does not replace on Windows; not atomic there */
#endif
    ok = ok && rename(tmp, path) == 0;
    if (!ok) remove(tmp);
    free(tmp);
    return ok ? FRB_OK : FRB_ERR_IO;
}

FrbError frb_file_write(const char *path, const FrbTable *t) {
    uint8_t *image;
    size_t size;
    FrbError e = frb_image_build(t, &image, &size);
    if (e != FRB_OK) return e;
    e = frb_write_atomic(path, image, size);
    free(image);
    return e;
}

FrbError frb_file_read(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index) {
    /* One byte more than the largest valid file, so that excess is noticed. */
    const size_t cap = FRB_HEADER_BYTES + ((size_t)1 << FRB_MAX_BITS) * FRB_ENTRY_BYTES + 1;
    uint8_t *buf;
    size_t got;
    FILE *f;
    FrbError e;
    if (t) {
        t->bits = 0;
        t->count = 0;
        t->entries = NULL;
    }
    if (!path) return FRB_ERR_ARG;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return FRB_ERR_MEMORY;
    }
    got = fread(buf, 1, cap, f);
    if (ferror(f)) e = FRB_ERR_IO;
    else e = frb_image_parse(buf, got, t, info, bad_index);
    fclose(f);
    free(buf);
    return e;
}

FrbError frb_file_info(const char *path, FrbFileInfo *info) {
    uint8_t header[FRB_HEADER_BYTES];
    FrbFileInfo local;
    size_t got;
    long end;
    FILE *f;
    FrbError e;
    if (!path) return FRB_ERR_ARG;
    if (!info) info = &local;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    got = fread(header, 1, sizeof header, f);
    e = ferror(f) ? FRB_ERR_IO : parse_header(header, got, info);
    if (e == FRB_OK) {
        long expect = (long)(FRB_HEADER_BYTES + (size_t)info->count * FRB_ENTRY_BYTES);
        if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0) e = FRB_ERR_IO;
        else if (end != expect) e = FRB_ERR_SIZE;
    }
    fclose(f);
    return e;
}

FrbError frb_file_lookup(const char *path, uint32_t index, FrbFileInfo *info, FrbStatus *status,
                         RevGraph *g) {
    uint8_t header[FRB_HEADER_BYTES], entry[FRB_ENTRY_BYTES];
    FrbFileInfo local;
    RevGraph graph;
    FrbStatus st;
    size_t got;
    long end;
    FILE *f;
    FrbError e;
    if (!path) return FRB_ERR_ARG;
    if (!info) info = &local;
    if (!g) g = &graph;
    if (!status) status = &st;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    got = fread(header, 1, sizeof header, f);
    e = ferror(f) ? FRB_ERR_IO : parse_header(header, got, info);
    if (e == FRB_OK) {
        long expect = (long)(FRB_HEADER_BYTES + (size_t)info->count * FRB_ENTRY_BYTES);
        if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0) e = FRB_ERR_IO;
        else if (end != expect) e = FRB_ERR_SIZE;
    }
    if (e == FRB_OK && index >= info->count) e = FRB_ERR_ARG;
    if (e == FRB_OK) {
        long at = (long)(FRB_HEADER_BYTES + (size_t)index * FRB_ENTRY_BYTES);
        if (fseek(f, at, SEEK_SET) != 0 || fread(entry, 1, sizeof entry, f) != sizeof entry)
            e = FRB_ERR_IO;
        else e = check_entry(entry, info->bits, index, status, g);
    }
    fclose(f);
    return e;
}

/* ---- report ------------------------------------------------------------- */

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

void frb_report_summary(FILE *out, const FrbSummary *s, int bits) {
    uint32_t cumulative = 0;
    int last = 0;
    if (!out || !s) return;
    for (int n = 1; n <= FRB_NODE_CAPACITY; n++)
        if (s->minimal_by_nodes[n] || s->witness_by_nodes[n]) last = n;

    fprintf(out, "Targets: %s (%d-bit strings)\n", commas(s->count), bits);
    fprintf(out, "  certified minimum        %s\n", commas(s->by_status[FRB_MINIMAL]));
    fprintf(out, "  witness, not certified   %s\n", commas(s->by_status[FRB_WITNESS]));
    fprintf(out, "  no witness               %s\n",
            commas(s->by_status[FRB_EMPTY] + s->by_status[FRB_UNRESOLVED] +
                   s->by_status[FRB_UNSUPPORTED]));
    if (s->by_status[FRB_UNSUPPORTED])
        fprintf(out, "  of those, marked as needing more than %d nodes: %s\n", FRB_NODE_CAPACITY,
                commas(s->by_status[FRB_UNSUPPORTED]));

    fprintf(out, "\n  Nodes   Certified minimum   Cumulative     Share   Witness only\n");
    for (int n = 1; n <= last; n++) {
        cumulative += s->minimal_by_nodes[n];
        fprintf(out, "  %5d   %17s   %10s   %6.2f%%   %12s\n", n, commas(s->minimal_by_nodes[n]),
                commas(cumulative), 100.0 * (double)cumulative / (double)s->count,
                commas(s->witness_by_nodes[n]));
    }
    if (last < FRB_NODE_CAPACITY)
        fprintf(out, "  %2d-%-2d   %17s   %10s   %6.2f%%   %12s\n", last + 1, FRB_NODE_CAPACITY, "0",
                commas(cumulative), 100.0 * (double)cumulative / (double)s->count, "0");

    fprintf(out, "\n");
    if (s->complete) {
        fprintf(out,
                "COMPLETE: every target has a certified minimum.  The largest is %d nodes,\n"
                "which is therefore the exact maximum over all %d-bit strings.\n",
                s->max_minimal_nodes, bits);
    } else {
        fprintf(out, "PARTIAL: %s of %s targets have no certified minimum.\n",
                commas(s->count - s->by_status[FRB_MINIMAL]), commas(s->count));
        if (s->max_minimal_nodes)
            fprintf(out,
                    "The largest certified minimum, %d nodes, is a lower bound on the maximum over\n"
                    "all %d-bit strings, not the maximum itself.\n",
                    s->max_minimal_nodes, bits);
        if (s->max_witness_nodes)
            fprintf(out, "Largest node count of any stored witness: %d.\n", s->max_witness_nodes);
    }
}
