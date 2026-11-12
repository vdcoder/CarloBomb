/*
 * FixedRunBomb file format: entries, table, file.  See FORMAT.md.
 *
 * Every multi-byte number is assembled and taken apart byte by byte, so the
 * file does not depend on the byte order, the alignment or the structure
 * layout of the machine that wrote it.
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64 /* files of more than 2 GB where off_t would otherwise be 32 bits */
#endif

#include "fixed_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/types.h>
#include <unistd.h>
#endif

/*
 * A table of 26 bits is longer than 2^31 bytes and one of 27 bits longer
 * than 2^32: file positions are 64-bit numbers here, whatever `long` is.
 */
#if defined(_WIN32)
static int seek64(FILE *f, uint64_t at) { return _fseeki64(f, (__int64)at, SEEK_SET); }
static int length64(FILE *f, uint64_t *length) {
    __int64 end;
    if (_fseeki64(f, 0, SEEK_END) != 0 || (end = _ftelli64(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
#else
typedef char frb_off_t_is_64_bits[sizeof(off_t) >= 8 ? 1 : -1];
static int seek64(FILE *f, uint64_t at) { return fseeko(f, (off_t)at, SEEK_SET); }
static int length64(FILE *f, uint64_t *length) {
    off_t end;
    if (fseeko(f, 0, SEEK_END) != 0 || (end = ftello(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
#endif

/* Bytes read or written at a time: small against memory, large against a system call. */
enum { IO_CHUNK = 1 << 22 };

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

/* ---- formats ------------------------------------------------------------ */

static const FrbFormat frb_formats[3] = {
    {1, 16, 16, 4, 9, 18, 19},
    {2, 24, 24, 5, 11, 33, 34},
    {3, 31, 31, 5, 11, 43, 44},
};

const FrbFormat *frb_format(int version) {
    return version >= 1 && version <= 3 ? &frb_formats[version - 1] : NULL;
}

const FrbFormat *frb_format_for_bits(int bits) {
    if (bits < 1 || bits > FRB_MAX_BITS) return NULL;
    for (int v = 0; v < 3; v++)
        if (bits <= frb_formats[v].max_bits) return &frb_formats[v];
    return NULL;
}

/* The arithmetic of the layout, in 64 bits: nothing here can overflow for
 * any length a format allows (the largest file is 64 + 44 * 2^31 bytes). */
uint64_t frb_entry_offset(const FrbFormat *format, uint32_t index) {
    return format ? FRB_HEADER_BYTES + (uint64_t)index * (uint64_t)format->entry_bytes : 0;
}

uint64_t frb_file_bytes(const FrbFormat *format, int bits) {
    if (!format || bits < 1 || bits > format->max_bits) return 0;
    return FRB_HEADER_BYTES + ((uint64_t)format->entry_bytes << bits);
}

/*
 * Bit k of a packed graph is bit (k mod 8) of byte (k div 8).  A field of up
 * to 11 bits starting anywhere lies within three consecutive bytes.
 */
static void put_field(uint8_t *buf, int nbytes, unsigned pos, unsigned value) {
    unsigned byte = pos >> 3;
    uint32_t v = (uint32_t)value << (pos & 7u);
    buf[byte] |= (uint8_t)(v & 0xFFu);
    if ((int)byte + 1 < nbytes) buf[byte + 1] |= (uint8_t)((v >> 8) & 0xFFu);
    if ((int)byte + 2 < nbytes) buf[byte + 2] |= (uint8_t)((v >> 16) & 0xFFu);
}

static unsigned get_field(const uint8_t *buf, int nbytes, unsigned pos, unsigned width) {
    unsigned byte = pos >> 3;
    uint32_t v = buf[byte];
    if ((int)byte + 1 < nbytes) v |= (uint32_t)buf[byte + 1] << 8;
    if ((int)byte + 2 < nbytes) v |= (uint32_t)buf[byte + 2] << 16;
    return (unsigned)((v >> (pos & 7u)) & ((1u << width) - 1u));
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

FrbError frb_graph_pack_as(const FrbFormat *f, const RevGraph *g, uint8_t *out) {
    if (!f || !g || !out) return FRB_ERR_ARG;
    if (rev_graph_validate(g) != REV_OK) return FRB_ERR_ARG;
    if (g->node_count > f->node_capacity) return FRB_ERR_CAPACITY;
    memset(out, 0, (size_t)f->graph_bytes);
    for (int i = 0; i < g->node_count; i++) {
        unsigned v = (unsigned)g->nodes[i].color | ((unsigned)g->nodes[i].links[0] << 1) |
                     ((unsigned)g->nodes[i].links[1] << (1 + f->link_bits));
        put_field(out, f->graph_bytes, (unsigned)(f->node_bits * i), v);
    }
    return FRB_OK;
}

FrbError frb_graph_unpack_as(const FrbFormat *f, const uint8_t *in, int node_count, RevGraph *g) {
    unsigned link_mask, used_bits;
    if (!f || !in || !g) return FRB_ERR_ARG;
    g->node_count = 0;
    if (node_count < 1 || node_count > f->node_capacity) return FRB_ERR_ENTRY;
    link_mask = (1u << f->link_bits) - 1u;
    for (int i = 0; i < node_count; i++) {
        unsigned v = get_field(in, f->graph_bytes, (unsigned)(f->node_bits * i), (unsigned)f->node_bits);
        int l0 = (int)((v >> 1) & link_mask), l1 = (int)((v >> (1 + f->link_bits)) & link_mask);
        if (l0 >= node_count || l1 >= node_count) return FRB_ERR_ENTRY;
        g->nodes[i].color = (int)(v & 1u);
        g->nodes[i].links[0] = l0;
        g->nodes[i].links[1] = l1;
    }
    /* Every bit above the last node is zero. */
    used_bits = (unsigned)(f->node_bits * node_count);
    if ((used_bits & 7u) && (in[used_bits >> 3] >> (used_bits & 7u)) != 0) return FRB_ERR_ENTRY;
    for (int k = (int)((used_bits + 7u) >> 3); k < f->graph_bytes; k++)
        if (in[k] != 0) return FRB_ERR_ENTRY;
    g->node_count = node_count;
    return FRB_OK;
}

static int has_witness(FrbStatus s) { return s == FRB_WITNESS || s == FRB_MINIMAL; }

FrbError frb_entry_encode_as(const FrbFormat *f, FrbStatus status, const RevGraph *g, uint8_t *out) {
    if (!f || !out || (int)status < 0 || (int)status > (int)FRB_UNSUPPORTED) return FRB_ERR_ARG;
    memset(out, 0, (size_t)f->entry_bytes);
    if (has_witness(status)) {
        FrbError e;
        if (!g) return FRB_ERR_ARG;
        e = frb_graph_pack_as(f, g, out);
        if (e != FRB_OK) return e;
        out[f->graph_bytes] = (uint8_t)(((unsigned)status << 5) | (unsigned)g->node_count);
    } else {
        if (g) return FRB_ERR_ARG;
        out[f->graph_bytes] = (uint8_t)((unsigned)status << 5);
    }
    return FRB_OK;
}

FrbError frb_entry_decode_as(const FrbFormat *f, const uint8_t *in, FrbStatus *status, RevGraph *g) {
    RevGraph local;
    unsigned meta, code, nodes;
    if (!f || !in) return FRB_ERR_ARG;
    if (!g) g = &local;
    meta = in[f->graph_bytes];
    code = meta >> 5;
    nodes = meta & 31u;
    g->node_count = 0;
    if (code > (unsigned)FRB_UNSUPPORTED) return FRB_ERR_ENTRY;
    if (status) *status = (FrbStatus)code;
    if (has_witness((FrbStatus)code)) return frb_graph_unpack_as(f, in, (int)nodes, g);
    if (nodes != 0) return FRB_ERR_ENTRY;
    for (int i = 0; i < f->graph_bytes; i++)
        if (in[i] != 0) return FRB_ERR_ENTRY;
    return FRB_OK;
}

/* Version 1 by its original names. */
FrbError frb_graph_pack(const RevGraph *g, uint8_t out[FRB_GRAPH_BYTES]) {
    return frb_graph_pack_as(&frb_formats[0], g, out);
}

FrbError frb_graph_unpack(const uint8_t in[FRB_GRAPH_BYTES], int node_count, RevGraph *g) {
    if (g) memset(g, 0, sizeof *g);
    return frb_graph_unpack_as(&frb_formats[0], in, node_count, g);
}

FrbError frb_entry_encode(FrbStatus status, const RevGraph *g, uint8_t out[FRB_ENTRY_BYTES]) {
    return frb_entry_encode_as(&frb_formats[0], status, g, out);
}

FrbError frb_entry_decode(const uint8_t in[FRB_ENTRY_BYTES], FrbStatus *status, RevGraph *g) {
    if (g) memset(g, 0, sizeof *g);
    return frb_entry_decode_as(&frb_formats[0], in, status, g);
}

/* ---- table -------------------------------------------------------------- */

FrbError frb_table_create_as(FrbTable *t, int bits, const FrbFormat *format) {
    if (!t) return FRB_ERR_ARG;
    t->bits = 0;
    t->count = 0;
    t->entries = NULL;
    t->format = NULL;
    if (!format || bits < 1 || bits > format->max_bits) return FRB_ERR_ARG;
    /* A table that does not fit the address space is not a table in memory. */
    if (frb_file_bytes(format, bits) > (uint64_t)SIZE_MAX) return FRB_ERR_MEMORY;
    t->entries = calloc((size_t)1 << bits, (size_t)format->entry_bytes); /* all zero = FRB_EMPTY */
    if (!t->entries) return FRB_ERR_MEMORY;
    t->bits = bits;
    t->count = (uint32_t)1 << bits;
    t->format = format;
    return FRB_OK;
}

FrbError frb_table_create(FrbTable *t, int bits) {
    const FrbFormat *format = frb_format_for_bits(bits);
    if (!format) {
        if (t) {
            t->bits = 0;
            t->count = 0;
            t->entries = NULL;
            t->format = NULL;
        }
        return FRB_ERR_ARG;
    }
    return frb_table_create_as(t, bits, format);
}

void frb_table_destroy(FrbTable *t) {
    if (!t) return;
    free(t->entries);
    t->entries = NULL;
    t->count = 0;
    t->bits = 0;
    t->format = NULL;
}

static int table_ok(const FrbTable *t) {
    return t && t->entries && t->format && t->bits >= 1 && t->bits <= t->format->max_bits &&
           t->count == (uint32_t)1 << t->bits;
}

const uint8_t *frb_table_entry(const FrbTable *t, uint32_t index) {
    return t->entries + (size_t)index * (size_t)t->format->entry_bytes;
}

int frb_table_nodes(const FrbTable *t, uint32_t index) {
    return frb_table_entry(t, index)[t->format->graph_bytes] & 31;
}

FrbError frb_table_put(FrbTable *t, uint32_t index, FrbStatus status, const RevGraph *g) {
    uint8_t entry[FRB_MAX_ENTRY_BYTES];
    FrbError e;
    if (!table_ok(t) || index >= t->count) return FRB_ERR_ARG;
    e = frb_entry_encode_as(t->format, status, g, entry);
    if (e != FRB_OK) return e;
    if (g && frb_graph_output(g, t->bits) != index) return FRB_ERR_OUTPUT;
    memcpy(t->entries + (size_t)index * (size_t)t->format->entry_bytes, entry, (size_t)t->format->entry_bytes);
    return FRB_OK;
}

static FrbError check_entry(const FrbFormat *f, const uint8_t *entry, int bits, uint32_t index,
                            FrbStatus *status, RevGraph *g) {
    FrbError e = frb_entry_decode_as(f, entry, status, g);
    if (e != FRB_OK) return e;
    if (g->node_count > 0 && frb_graph_output(g, bits) != index) return FRB_ERR_OUTPUT;
    return FRB_OK;
}

FrbError frb_table_get(const FrbTable *t, uint32_t index, FrbStatus *status, RevGraph *g) {
    RevGraph local;
    FrbStatus st;
    if (!table_ok(t) || index >= t->count) return FRB_ERR_ARG;
    if (!g) g = &local;
    if (!status) status = &st;
    return check_entry(t->format, frb_table_entry(t, index), t->bits, index, status, g);
}

FrbError frb_table_validate(const FrbTable *t, FrbSummary *summary, uint32_t *bad_index) {
    FrbSummary s;
    RevGraph g;
    if (!table_ok(t)) return FRB_ERR_ARG;
    memset(&s, 0, sizeof s);
    s.count = t->count;
    for (uint32_t i = 0; i < t->count; i++) {
        FrbStatus st;
        FrbError e = check_entry(t->format, frb_table_entry(t, i), t->bits, i, &st, &g);
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
    if (!table_ok(t)) return FRB_ERR_ARG;
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

FrbError frb_table_check_chain(const FrbTable *longer, const FrbTable *shorter, uint32_t *bad_index) {
    uint32_t short_mask;
    uint8_t *least;
    int shift;
    if (!table_ok(longer) || !table_ok(shorter) || shorter->bits >= longer->bits) return FRB_ERR_ARG;
    for (int which = 0; which < 2; which++) { /* certified minima only, in both */
        const FrbTable *t = which ? shorter : longer;
        for (uint32_t i = 0; i < t->count; i++)
            if ((frb_table_entry(t, i)[t->format->graph_bytes] >> 5) != (unsigned)FRB_MINIMAL) {
                if (bad_index) *bad_index = which ? i << (longer->bits - shorter->bits) : i;
                return FRB_ERR_INCOMPLETE;
            }
    }
    shift = longer->bits - shorter->bits;
    short_mask = shorter->count - 1u;
    least = malloc(shorter->count);
    if (!least) return FRB_ERR_MEMORY;
    memset(least, 255, shorter->count);
    for (uint32_t i = 0; i < longer->count; i++) {
        int n = frb_table_nodes(longer, i);
        if (n < least[i >> shift]) least[i >> shift] = (uint8_t)n;
        /* A REV for the string emits its first bits: they cannot need more.
         * And n nodes for the string give n + 1 for the string without its
         * first j bits, which emits the stretch that starts there. */
        for (int j = 0; j <= shift; j++) {
            uint32_t stretch = (i >> (shift - j)) & short_mask;
            if (frb_table_nodes(shorter, stretch) > n + (j ? 1 : 0)) {
                if (bad_index) *bad_index = i;
                free(least);
                return FRB_ERR_EVIDENCE;
            }
        }
    }
    /* A REV for a short string goes on to emit one of its extensions. */
    for (uint32_t q = 0; q < shorter->count; q++)
        if (least[q] != frb_table_nodes(shorter, q)) {
            if (bad_index) *bad_index = q << shift;
            free(least);
            return FRB_ERR_EVIDENCE;
        }
    free(least);
    return FRB_OK;
}

/* ---- header ------------------------------------------------------------- */

static void build_header(const FrbTable *t, const FrbSummary *s, uint32_t table_crc,
                         uint8_t h[FRB_HEADER_BYTES]) {
    memset(h, 0, FRB_HEADER_BYTES);
    memcpy(h + H_MAGIC, frb_magic, sizeof frb_magic);
    put16(h + H_VERSION, (unsigned)t->format->version);
    put16(h + H_HEADER_SIZE, FRB_HEADER_BYTES);
    h[H_BITS] = (uint8_t)t->bits;
    h[H_CAPACITY] = (uint8_t)t->format->node_capacity;
    h[H_NODE_BITS] = (uint8_t)t->format->node_bits;
    h[H_ENTRY_SIZE] = (uint8_t)t->format->entry_bytes;
    put32(h + H_COUNT, t->count);
    put32(h + H_FLAGS, s->complete ? FRB_FLAG_COMPLETE : 0u);
    put32(h + H_WITNESSES, s->by_status[FRB_WITNESS] + s->by_status[FRB_MINIMAL]);
    put32(h + H_MINIMAL, s->by_status[FRB_MINIMAL]);
    h[H_MAX_MINIMAL] = (uint8_t)s->max_minimal_nodes;
    h[H_MAX_WITNESS] = (uint8_t)s->max_witness_nodes;
    put32(h + H_TABLE_CRC, table_crc);
    put32(h + H_HEADER_CRC, frb_crc32(0, h, H_HEADER_CRC));
}

/* Everything that can be said about a header alone. */
static FrbError parse_header(const uint8_t *h, size_t available, FrbFileInfo *info,
                             const FrbFormat **format) {
    FrbFileInfo local;
    const FrbFormat *f;
    if (!info) info = &local;
    memset(info, 0, sizeof *info);
    if (format) *format = NULL;
    if (available < sizeof frb_magic || memcmp(h + H_MAGIC, frb_magic, sizeof frb_magic) != 0)
        return FRB_ERR_MAGIC;
    if (available < H_HEADER_SIZE) return FRB_ERR_SIZE;
    info->version = (int)get16(h + H_VERSION);
    f = frb_format(info->version);
    if (!f) return FRB_ERR_VERSION;
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
    info->entry_bytes = f->entry_bytes;

    if (info->bits < 1 || info->bits > f->max_bits) return FRB_ERR_HEADER;
    if (info->node_capacity != f->node_capacity || h[H_NODE_BITS] != f->node_bits ||
        h[H_ENTRY_SIZE] != f->entry_bytes)
        return FRB_ERR_HEADER;
    if (info->count != (uint32_t)1 << info->bits) return FRB_ERR_HEADER;
    if (info->flags & ~FRB_FLAG_COMPLETE) return FRB_ERR_HEADER;
    for (int i = H_RESERVED1; i < H_TABLE_CRC; i++)
        if (h[i] != 0) return FRB_ERR_HEADER;
    for (int i = H_RESERVED2; i < H_HEADER_CRC; i++)
        if (h[i] != 0) return FRB_ERR_HEADER;
    if (format) *format = f;
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

static size_t table_bytes(const FrbTable *t) {
    return (size_t)t->count * (size_t)t->format->entry_bytes;
}

/* ---- file image --------------------------------------------------------- */

FrbError frb_image_build(const FrbTable *t, uint8_t **image, size_t *size) {
    FrbSummary s;
    uint8_t *buf;
    FrbError e;
    if (!image || !size) return FRB_ERR_ARG;
    *image = NULL;
    *size = 0;
    e = frb_table_validate(t, &s, NULL);
    if (e != FRB_OK) return e;
    buf = malloc(FRB_HEADER_BYTES + table_bytes(t));
    if (!buf) return FRB_ERR_MEMORY;
    memcpy(buf + FRB_HEADER_BYTES, t->entries, table_bytes(t));
    build_header(t, &s, frb_crc32(0, t->entries, table_bytes(t)), buf);
    *image = buf;
    *size = FRB_HEADER_BYTES + table_bytes(t);
    return FRB_OK;
}

FrbError frb_image_parse(const uint8_t *image, size_t size, FrbTable *t, FrbFileInfo *info,
                         uint32_t *bad_index) {
    const FrbFormat *format;
    FrbFileInfo local;
    FrbSummary s;
    FrbTable table;
    size_t bytes;
    FrbError e;
    if (t) {
        t->bits = 0;
        t->count = 0;
        t->entries = NULL;
        t->format = NULL;
    }
    if (!image) return FRB_ERR_ARG;
    if (!info) info = &local;
    e = parse_header(image, size, info, &format);
    if (e != FRB_OK) return e;
    bytes = (size_t)info->count * (size_t)format->entry_bytes;
    if (size != FRB_HEADER_BYTES + bytes) return FRB_ERR_SIZE;
    if (frb_crc32(0, image + FRB_HEADER_BYTES, bytes) != info->table_crc) return FRB_ERR_CHECKSUM;

    e = frb_table_create_as(&table, info->bits, format);
    if (e != FRB_OK) return e;
    memcpy(table.entries, image + FRB_HEADER_BYTES, bytes);
    e = frb_table_validate(&table, &s, bad_index);
    if (e == FRB_OK) e = check_summary(&s, info);
    if (e != FRB_OK || !t) frb_table_destroy(&table);
    else *t = table;
    return e;
}

/* ---- files -------------------------------------------------------------- */

/* Replace `path` with the two parts, one after the other, atomically. */
static FrbError write_parts(const char *path, const void *a, size_t na, const void *b, size_t nb) {
    char *tmp;
    size_t n;
    FILE *f;
    int ok;
    if (!path || (!a && na) || (!b && nb)) return FRB_ERR_ARG;
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
    ok = na == 0 || fwrite(a, 1, na, f) == na;
    for (size_t done = 0; ok && done < nb;) { /* a part may be many gigabytes */
        size_t now = nb - done < (size_t)IO_CHUNK ? nb - done : (size_t)IO_CHUNK;
        ok = fwrite((const char *)b + done, 1, now, f) == now;
        done += now;
    }
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

FrbError frb_write_atomic(const char *path, const void *data, size_t size) {
    return write_parts(path, data, size, NULL, 0);
}

FrbError frb_file_write(const char *path, const FrbTable *t) {
    uint8_t header[FRB_HEADER_BYTES];
    FrbSummary s;
    FrbError e = frb_table_validate(t, &s, NULL);
    if (e != FRB_OK) return e;
    build_header(t, &s, frb_crc32(0, t->entries, table_bytes(t)), header);
    return write_parts(path, header, sizeof header, t->entries, table_bytes(t));
}

/* Header, length, table bytes and checksum; entries are decoded only if asked. */
static FrbError load_file(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index,
                          int validate) {
    uint8_t header[FRB_HEADER_BYTES];
    const FrbFormat *format;
    FrbFileInfo local;
    FrbTable table;
    uint64_t length;
    uint32_t crc = 0;
    size_t got, bytes;
    FILE *f;
    FrbError e;
    if (t) {
        t->bits = 0;
        t->count = 0;
        t->entries = NULL;
        t->format = NULL;
    }
    if (!path) return FRB_ERR_ARG;
    if (!info) info = &local;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    got = fread(header, 1, sizeof header, f);
    e = ferror(f) ? FRB_ERR_IO : parse_header(header, got, info, &format);
    if (e != FRB_OK) {
        fclose(f);
        return e;
    }
    if (length64(f, &length) != 0) e = FRB_ERR_IO;
    else if (length != frb_file_bytes(format, info->bits)) e = FRB_ERR_SIZE;
    if (e == FRB_OK) e = frb_table_create_as(&table, info->bits, format);
    if (e != FRB_OK) {
        fclose(f);
        return e;
    }
    bytes = table_bytes(&table);
    if (seek64(f, FRB_HEADER_BYTES) != 0) e = FRB_ERR_IO;
    for (size_t done = 0; e == FRB_OK && done < bytes;) { /* in pieces, the checksum with them */
        size_t now = bytes - done < (size_t)IO_CHUNK ? bytes - done : (size_t)IO_CHUNK;
        if (fread(table.entries + done, 1, now, f) != now) e = FRB_ERR_IO;
        else crc = frb_crc32(crc, table.entries + done, now);
        done += now;
    }
    fclose(f);
    if (e == FRB_OK && crc != info->table_crc) e = FRB_ERR_CHECKSUM;
    if (e == FRB_OK && validate) {
        FrbSummary s;
        e = frb_table_validate(&table, &s, bad_index);
        if (e == FRB_OK) e = check_summary(&s, info);
    }
    if (e != FRB_OK || !t) frb_table_destroy(&table);
    else *t = table;
    return e;
}

FrbError frb_file_read(const char *path, FrbTable *t, FrbFileInfo *info, uint32_t *bad_index) {
    return load_file(path, t, info, bad_index, 1);
}

FrbError frb_file_read_raw(const char *path, FrbTable *t, FrbFileInfo *info) {
    return load_file(path, t, info, NULL, 0);
}

/* Open, read and check the header, check the length.  The caller closes. */
static FrbError open_checked(const char *path, FrbFileInfo *info, const FrbFormat **format, FILE **out) {
    uint8_t header[FRB_HEADER_BYTES];
    uint64_t length;
    size_t got;
    FILE *f;
    FrbError e;
    *out = NULL;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    got = fread(header, 1, sizeof header, f);
    e = ferror(f) ? FRB_ERR_IO : parse_header(header, got, info, format);
    if (e == FRB_OK) {
        if (length64(f, &length) != 0) e = FRB_ERR_IO;
        else if (length != frb_file_bytes(*format, info->bits)) e = FRB_ERR_SIZE;
    }
    if (e != FRB_OK) {
        fclose(f);
        return e;
    }
    *out = f;
    return FRB_OK;
}

FrbError frb_file_info(const char *path, FrbFileInfo *info) {
    const FrbFormat *format;
    FrbFileInfo local;
    FILE *f;
    FrbError e;
    if (!path) return FRB_ERR_ARG;
    if (!info) info = &local;
    e = open_checked(path, info, &format, &f);
    if (e == FRB_OK) fclose(f);
    return e;
}

FrbError frb_file_lookup(const char *path, uint32_t index, FrbFileInfo *info, FrbStatus *status,
                         RevGraph *g) {
    uint8_t entry[FRB_MAX_ENTRY_BYTES];
    const FrbFormat *format;
    FrbFileInfo local;
    RevGraph graph;
    FrbStatus st;
    FILE *f;
    FrbError e;
    if (!path) return FRB_ERR_ARG;
    if (!info) info = &local;
    if (!g) g = &graph;
    if (!status) status = &st;
    e = open_checked(path, info, &format, &f);
    if (e != FRB_OK) return e;
    if (index >= info->count) e = FRB_ERR_ARG;
    if (e == FRB_OK) {
        if (seek64(f, frb_entry_offset(format, index)) != 0 ||
            fread(entry, 1, (size_t)format->entry_bytes, f) != (size_t)format->entry_bytes)
            e = FRB_ERR_IO;
        else e = check_entry(format, entry, info->bits, index, status, g);
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
    const FrbFormat *format = frb_format_for_bits(bits);
    int capacity = format ? format->node_capacity : FRB_MAX_NODES;
    uint32_t cumulative = 0;
    int last = 0;
    if (!out || !s) return;
    for (int n = 1; n <= FRB_MAX_NODES; n++)
        if (s->minimal_by_nodes[n] || s->witness_by_nodes[n]) last = n;
    if (last > capacity) capacity = last;

    fprintf(out, "Targets: %s (%d-bit strings)\n", commas(s->count), bits);
    fprintf(out, "  certified minimum        %s\n", commas(s->by_status[FRB_MINIMAL]));
    fprintf(out, "  witness, not certified   %s\n", commas(s->by_status[FRB_WITNESS]));
    fprintf(out, "  no witness               %s\n",
            commas(s->by_status[FRB_EMPTY] + s->by_status[FRB_UNRESOLVED] +
                   s->by_status[FRB_UNSUPPORTED]));
    if (s->by_status[FRB_UNSUPPORTED])
        fprintf(out, "  of those, marked as needing more than %d nodes: %s\n", capacity,
                commas(s->by_status[FRB_UNSUPPORTED]));

    fprintf(out, "\n  Nodes   Certified minimum   Cumulative     Share   Witness only\n");
    for (int n = 1; n <= last; n++) {
        cumulative += s->minimal_by_nodes[n];
        fprintf(out, "  %5d   %17s   %10s   %6.2f%%   %12s\n", n, commas(s->minimal_by_nodes[n]),
                commas(cumulative), 100.0 * (double)cumulative / (double)s->count,
                commas(s->witness_by_nodes[n]));
    }
    if (last < capacity)
        fprintf(out, "  %2d-%-2d   %17s   %10s   %6.2f%%   %12s\n", last + 1, capacity, "0",
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

void frb_report_hardest(FILE *out, const FrbTable *t, int list_max) {
    char target[FRB_MAX_BITS + 1];
    uint32_t count = 0, listed = 0;
    int max = 0, formula = 0, one;
    if (!out || !table_ok(t)) return;
    for (uint32_t i = 0; i < t->count; i++) {
        int n = frb_table_nodes(t, i);
        if (n > max) {
            max = n;
            count = 0;
        }
        if (n == max) count++;
    }
    while (formula * formula < 4 * t->bits) formula++; /* ceil(2 * sqrt(L)) */
    formula--;
    one = frb_table_nodes(t, t->bits > 1 ? 1u : 0u);
    fprintf(out, "\nThe hardest %d-bit strings\n", t->bits);
    fprintf(out, "  largest minimum                  %d nodes\n", max);
    fprintf(out, "  strings that need it             %s\n", commas(count));
    fprintf(out, "  ceil(2*sqrt(%d)) - 1              %d: the largest minimum %s\n", t->bits, formula,
            max == formula ? "equals it" : "DOES NOT equal it");
    frb_target_format(t->bits > 1 ? 1u : 0u, t->bits, target);
    fprintf(out, "  %s needs  %d: %s\n", target, one,
            one == max ? "it is among the hardest" : "it is NOT among the hardest");
    if (list_max <= 0) return;
    if (count / 2 > (uint32_t)list_max)
        fprintf(out, "  the first %d that begin with 0, in table order (each stands for its complement too):\n",
                list_max);
    else
        fprintf(out, "  those that begin with 0 (each stands for its complement too):\n");
    for (uint32_t i = 0; i < t->count / 2 && listed < (uint32_t)list_max; i++) {
        if (frb_table_nodes(t, i) != max) continue;
        frb_target_format(i, t->bits, target);
        fprintf(out, "    %s\n", target);
        listed++;
    }
}
