/*
 * FixedRunBomb compact format (version 4).  See compact.h and FORMAT.md,
 * section 12.
 *
 * As in fixed_run.c every number is assembled and taken apart byte by byte.
 * Nothing in a file is believed: a reader recomputes every derived number of
 * the class directory from the class counts and compares, and a writer does
 * not give a file its name until it has been read back and compared with the
 * table it was made from.
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* MAP_NORESERVE */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "compact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const uint8_t frb_magic[8] = {'C', 'B', 'F', 'R', 'B', 0x0D, 0x0A, 0x1A};

/* Header field offsets.  Bytes 0..33 mean what they mean in a fixed-width
 * header, with entry size 0: there are no entries of a fixed size. */
enum {
    C_MAGIC = 0,
    C_VERSION = 8,
    C_HEADER_SIZE = 10,
    C_BITS = 12,
    C_CAPACITY = 13,
    C_INDEX_BYTES = 14,
    C_ENTRY_SIZE = 15,
    C_COUNT = 16,
    C_FLAGS = 20,
    C_WITNESSES = 24,
    C_MINIMAL = 28,
    C_MAX_MINIMAL = 32,
    C_MAX_WITNESS = 33,
    C_SOURCE_VERSION = 34,
    C_DIRECTORY_CRC = 36,
    C_INDEX_CRC = 40,
    C_SOURCE_CRC = 44,
    C_FILE_BYTES = 48,
    C_RESERVED = 56,
    C_HEADER_CRC = 60
};

/* Class directory row offsets. */
enum {
    D_COUNT = 0,
    D_START = 4,
    D_OFFSET = 8,
    D_STRIDE = 16,
    D_LINK_BITS = 20,
    D_RESERVED1 = 21, /* 3 bytes */
    D_CRC = 24,
    D_RESERVED2 = 28  /* 4 bytes */
};

enum { IO_CHUNK = 1 << 22 };

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

static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }

#if defined(_WIN32)
static int seek64(FILE *f, uint64_t at) { return _fseeki64(f, (__int64)at, SEEK_SET); }
static int length64(FILE *f, uint64_t *length) {
    __int64 end;
    if (_fseeki64(f, 0, SEEK_END) != 0 || (end = _ftelli64(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
#else
static int seek64(FILE *f, uint64_t at) { return fseeko(f, (off_t)at, SEEK_SET); }
static int length64(FILE *f, uint64_t *length) {
    off_t end;
    if (fseeko(f, 0, SEEK_END) != 0 || (end = ftello(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
#endif

/* ---- one REV of a class -------------------------------------------------- */

int frb_compact_link_bits(int nodes) {
    int w = 0;
    if (nodes < 1 || nodes > FRB_COMPACT_CLASSES) return -1;
    while ((1 << w) < nodes) w++;
    return w;
}

int frb_compact_stride(int nodes) {
    int w = frb_compact_link_bits(nodes);
    return w < 0 ? 0 : (nodes * (1 + 2 * w) + 7) / 8;
}

/* A field of up to 11 bits at bit position pos of a zeroed buffer. */
static void put_bits(uint8_t *buf, int nbytes, unsigned pos, unsigned value) {
    unsigned byte = pos >> 3;
    uint32_t v = (uint32_t)value << (pos & 7u);
    buf[byte] |= (uint8_t)(v & 0xFFu);
    if ((int)byte + 1 < nbytes) buf[byte + 1] |= (uint8_t)((v >> 8) & 0xFFu);
    if ((int)byte + 2 < nbytes) buf[byte + 2] |= (uint8_t)((v >> 16) & 0xFFu);
}

static unsigned get_bits(const uint8_t *buf, int nbytes, unsigned pos, unsigned width) {
    unsigned byte = pos >> 3;
    uint32_t v = buf[byte];
    if ((int)byte + 1 < nbytes) v |= (uint32_t)buf[byte + 1] << 8;
    if ((int)byte + 2 < nbytes) v |= (uint32_t)buf[byte + 2] << 16;
    return (unsigned)((v >> (pos & 7u)) & ((1u << width) - 1u));
}

FrbError frb_compact_pack(const RevGraph *g, uint8_t *out) {
    int n, w, stride;
    if (!g || !out) return FRB_ERR_ARG;
    if (rev_graph_validate(g) != REV_OK) return FRB_ERR_ARG;
    n = g->node_count;
    if (n > FRB_COMPACT_CLASSES) return FRB_ERR_CAPACITY;
    w = frb_compact_link_bits(n);
    stride = frb_compact_stride(n);
    memset(out, 0, (size_t)stride);
    for (int i = 0; i < n; i++) {
        unsigned v = (unsigned)g->nodes[i].color | ((unsigned)g->nodes[i].links[0] << 1) |
                     ((unsigned)g->nodes[i].links[1] << (1 + w));
        put_bits(out, stride, (unsigned)(i * (1 + 2 * w)), v);
    }
    return FRB_OK;
}

FrbError frb_compact_unpack(const uint8_t *in, int nodes, RevGraph *g) {
    int w = frb_compact_link_bits(nodes), stride, width;
    unsigned mask, used;
    if (!in || !g) return FRB_ERR_ARG;
    g->node_count = 0;
    if (w < 0) return FRB_ERR_ENTRY;
    stride = frb_compact_stride(nodes);
    width = 1 + 2 * w;
    mask = (1u << w) - 1u;
    for (int i = 0; i < nodes; i++) {
        unsigned v = get_bits(in, stride, (unsigned)(i * width), (unsigned)width);
        int l0 = (int)((v >> 1) & mask), l1 = (int)((v >> (1 + w)) & mask);
        if (l0 >= nodes || l1 >= nodes) return FRB_ERR_ENTRY;
        g->nodes[i].color = (int)(v & 1u);
        g->nodes[i].links[0] = l0;
        g->nodes[i].links[1] = l1;
    }
    used = (unsigned)(nodes * width); /* every bit after the last node is zero */
    if ((used & 7u) && (in[used >> 3] >> (used & 7u)) != 0) return FRB_ERR_ENTRY;
    g->node_count = nodes;
    return FRB_OK;
}

/* ---- layout ---------------------------------------------------------------- */

FrbError frb_compact_layout(FrbCompactInfo *info, int bits, int index_bytes) {
    uint64_t sum = 0, offset;
    uint32_t start = 0;
    int least;
    if (!info || bits < 1 || bits > 31) return FRB_ERR_ARG;
    least = (bits + 7) / 8;
    if (index_bytes == 0) index_bytes = least;
    if (index_bytes < least || index_bytes > 4) return FRB_ERR_ARG;
    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++) sum += info->class_count[n];
    if (sum != ((uint64_t)1 << bits)) return FRB_ERR_ARG;
    info->bits = bits;
    info->index_bytes = index_bytes;
    info->count = (uint32_t)1 << bits;
    info->index_entries = ((uint64_t)2 << bits) - 2;
    info->index_offset = FRB_COMPACT_INDEX_OFFSET;
    info->pool_offset = (info->index_offset + info->index_entries * (uint64_t)index_bytes + 7u) & ~(uint64_t)7;
    info->max_nodes = 0;
    info->class_count[0] = info->class_start[0] = info->class_stride[0] = info->class_crc[0] = 0;
    info->class_offset[0] = 0;
    offset = info->pool_offset;
    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++) {
        info->class_start[n] = start;
        info->class_stride[n] = (uint32_t)frb_compact_stride(n);
        info->class_offset[n] = offset;
        start += info->class_count[n];
        offset += (uint64_t)info->class_count[n] * (uint64_t)info->class_stride[n];
        if (info->class_count[n]) info->max_nodes = n;
    }
    info->file_bytes = offset;
    return FRB_OK;
}

/* Place in the prefix index of the string of `bits` bits with value `value`. */
static uint64_t index_place(int bits, uint32_t value) { return (((uint64_t)1 << bits) - 2u) + value; }

static uint32_t get_ordinal(const uint8_t *p, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static void put_ordinal(uint8_t *p, int bytes, uint32_t v) {
    for (int i = 0; i < bytes; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
}

/* Node count of the class an ordinal lies in; 0 if it lies in none. */
static int class_of(const FrbCompactInfo *info, uint32_t ordinal) {
    for (int n = 1; n <= info->max_nodes; n++)
        if (ordinal - info->class_start[n] < info->class_count[n]) return n;
    return 0;
}

static void write_header(const FrbCompactInfo *info, uint8_t *h) {
    memset(h, 0, FRB_COMPACT_INDEX_OFFSET);
    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++) {
        uint8_t *row = h + FRB_HEADER_BYTES + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES;
        put32(row + D_COUNT, info->class_count[n]);
        put32(row + D_START, info->class_start[n]);
        put64(row + D_OFFSET, info->class_offset[n]);
        put32(row + D_STRIDE, info->class_stride[n]);
        row[D_LINK_BITS] = (uint8_t)frb_compact_link_bits(n);
        put32(row + D_CRC, info->class_crc[n]);
    }
    memcpy(h + C_MAGIC, frb_magic, sizeof frb_magic);
    put16(h + C_VERSION, FRB_FORMAT_COMPACT);
    put16(h + C_HEADER_SIZE, FRB_HEADER_BYTES);
    h[C_BITS] = (uint8_t)info->bits;
    h[C_CAPACITY] = FRB_COMPACT_CLASSES;
    h[C_INDEX_BYTES] = (uint8_t)info->index_bytes;
    h[C_ENTRY_SIZE] = 0;
    put32(h + C_COUNT, info->count);
    put32(h + C_FLAGS, FRB_FLAG_COMPLETE);
    put32(h + C_WITNESSES, info->count);
    put32(h + C_MINIMAL, info->count);
    h[C_MAX_MINIMAL] = (uint8_t)info->max_nodes;
    h[C_MAX_WITNESS] = (uint8_t)info->max_nodes;
    put16(h + C_SOURCE_VERSION, (unsigned)info->source_version);
    put32(h + C_DIRECTORY_CRC,
          frb_crc32(0, h + FRB_HEADER_BYTES, FRB_COMPACT_CLASSES * FRB_COMPACT_ROW_BYTES));
    put32(h + C_INDEX_CRC, info->index_crc);
    put32(h + C_SOURCE_CRC, info->source_table_crc);
    put64(h + C_FILE_BYTES, info->file_bytes);
    put32(h + C_HEADER_CRC, frb_crc32(0, h, C_HEADER_CRC));
}

/* The first 1056 bytes of a file: everything in them checked. */
static FrbError parse_header(const uint8_t *h, size_t available, FrbCompactInfo *info) {
    const uint8_t *dir = h + FRB_HEADER_BYTES;
    memset(info, 0, sizeof *info);
    if (available < sizeof frb_magic || memcmp(h + C_MAGIC, frb_magic, sizeof frb_magic) != 0)
        return FRB_ERR_MAGIC;
    if (available < C_HEADER_SIZE) return FRB_ERR_SIZE;
    if (get16(h + C_VERSION) != FRB_FORMAT_COMPACT) return FRB_ERR_VERSION;
    if (available < FRB_COMPACT_INDEX_OFFSET) return FRB_ERR_SIZE;
    if (get16(h + C_HEADER_SIZE) != FRB_HEADER_BYTES) return FRB_ERR_HEADER;
    if (get32(h + C_HEADER_CRC) != frb_crc32(0, h, C_HEADER_CRC)) return FRB_ERR_HEADER;
    if (get32(h + C_DIRECTORY_CRC) != frb_crc32(0, dir, FRB_COMPACT_CLASSES * FRB_COMPACT_ROW_BYTES))
        return FRB_ERR_HEADER;
    if (h[C_CAPACITY] != FRB_COMPACT_CLASSES || h[C_ENTRY_SIZE] != 0) return FRB_ERR_HEADER;
    if (get32(h + C_RESERVED) != 0) return FRB_ERR_HEADER;

    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++)
        info->class_count[n] = get32(dir + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES + D_COUNT);
    /* Everything else in the directory follows from the counts. */
    if (frb_compact_layout(info, h[C_BITS], h[C_INDEX_BYTES]) != FRB_OK) return FRB_ERR_HEADER;
    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++) {
        const uint8_t *row = dir + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES;
        if (get32(row + D_START) != info->class_start[n] || get64(row + D_OFFSET) != info->class_offset[n] ||
            get32(row + D_STRIDE) != info->class_stride[n] || row[D_LINK_BITS] != frb_compact_link_bits(n) ||
            row[D_RESERVED1] || row[D_RESERVED1 + 1] || row[D_RESERVED1 + 2] || get32(row + D_RESERVED2) != 0)
            return FRB_ERR_HEADER;
        info->class_crc[n] = get32(row + D_CRC);
        if (info->class_count[n] == 0 && info->class_crc[n] != 0) return FRB_ERR_HEADER;
    }
    if (get32(h + C_COUNT) != info->count || get32(h + C_FLAGS) != FRB_FLAG_COMPLETE ||
        get32(h + C_WITNESSES) != info->count || get32(h + C_MINIMAL) != info->count ||
        h[C_MAX_MINIMAL] != info->max_nodes || h[C_MAX_WITNESS] != info->max_nodes ||
        get64(h + C_FILE_BYTES) != info->file_bytes)
        return FRB_ERR_HEADER;
    info->source_version = (int)get16(h + C_SOURCE_VERSION);
    if (info->source_version > 3) return FRB_ERR_HEADER;
    info->directory_crc = get32(h + C_DIRECTORY_CRC);
    info->index_crc = get32(h + C_INDEX_CRC);
    info->source_table_crc = get32(h + C_SOURCE_CRC);
    return FRB_OK;
}

int frb_file_is_compact(const char *path) {
    uint8_t h[10];
    FILE *f = path ? fopen(path, "rb") : NULL;
    size_t got;
    if (!f) return -1;
    got = fread(h, 1, sizeof h, f);
    fclose(f);
    if (got != sizeof h || memcmp(h, frb_magic, sizeof frb_magic) != 0) return -1;
    return get16(h + C_VERSION) == FRB_FORMAT_COMPACT;
}

/* Open, read and check header and directory, check the length.  The caller closes. */
static FrbError open_checked(const char *path, FrbCompactInfo *info, FILE **out) {
    uint8_t h[FRB_COMPACT_INDEX_OFFSET];
    uint64_t length;
    size_t got;
    FILE *f;
    FrbError e;
    *out = NULL;
    if (!path) return FRB_ERR_ARG;
    f = fopen(path, "rb");
    if (!f) return FRB_ERR_IO;
    got = fread(h, 1, sizeof h, f);
    e = ferror(f) ? FRB_ERR_IO : parse_header(h, got, info);
    if (e == FRB_OK) {
        if (length64(f, &length) != 0) e = FRB_ERR_IO;
        else if (length != info->file_bytes) e = FRB_ERR_SIZE;
    }
    if (e != FRB_OK) {
        fclose(f);
        return e;
    }
    *out = f;
    return FRB_OK;
}

FrbError frb_compact_file_info(const char *path, FrbCompactInfo *info) {
    FrbCompactInfo local;
    FILE *f;
    FrbError e = open_checked(path, info ? info : &local, &f);
    if (e == FRB_OK) fclose(f);
    return e;
}

FrbError frb_compact_file_lookup(const char *path, int bits, uint32_t value, FrbCompactInfo *info,
                                 RevGraph *g, uint32_t *extension) {
    uint8_t buf[64]; /* the longest REV is 43 bytes */
    FrbCompactInfo local;
    RevGraph graph;
    uint32_t ordinal, emitted;
    FILE *f;
    FrbError e;
    int n;
    if (!info) info = &local;
    if (!g) g = &graph;
    g->node_count = 0;
    e = open_checked(path, info, &f);
    if (e != FRB_OK) return e;
    if (bits < 1 || bits > info->bits || value >= ((uint32_t)1 << bits)) {
        fclose(f);
        return FRB_ERR_ARG;
    }
    if (seek64(f, info->index_offset + index_place(bits, value) * (uint64_t)info->index_bytes) != 0 ||
        fread(buf, 1, (size_t)info->index_bytes, f) != (size_t)info->index_bytes) {
        fclose(f);
        return FRB_ERR_IO;
    }
    ordinal = get_ordinal(buf, info->index_bytes);
    n = class_of(info, ordinal);
    if (!n) {
        fclose(f);
        return FRB_ERR_ENTRY;
    }
    if (seek64(f, info->class_offset[n] +
                      (uint64_t)(ordinal - info->class_start[n]) * (uint64_t)info->class_stride[n]) != 0 ||
        fread(buf, 1, info->class_stride[n], f) != info->class_stride[n])
        e = FRB_ERR_IO;
    fclose(f);
    if (e == FRB_OK) e = frb_compact_unpack(buf, n, g);
    if (e != FRB_OK) return e;
    emitted = frb_graph_output(g, info->bits);
    if ((emitted >> (info->bits - bits)) != value) return FRB_ERR_OUTPUT;
    if (extension) *extension = emitted;
    return FRB_OK;
}

/* ---- a compact Bomb in use ----------------------------------------------- */

struct FrbCompact {
    FrbCompactInfo info;
    const uint8_t *base; /* the whole file */
    void *mapping;       /* not NULL: base is a mapping of this length */
    size_t mapping_bytes;
    uint8_t *heap;       /* else the copy that base points to */
};

static uint32_t ordinal_at(const FrbCompact *b, uint64_t place) {
    return get_ordinal(b->base + b->info.index_offset + place * (uint64_t)b->info.index_bytes,
                       b->info.index_bytes);
}

static const uint8_t *graph_at(const FrbCompact *b, int n, uint32_t ordinal) {
    return b->base + b->info.class_offset[n] +
           (uint64_t)(ordinal - b->info.class_start[n]) * (uint64_t)b->info.class_stride[n];
}

void frb_compact_close(FrbCompact *b) {
    if (!b) return;
#if !defined(_WIN32)
    if (b->mapping) munmap(b->mapping, b->mapping_bytes);
#endif
    free(b->heap);
    free(b);
}

const FrbCompactInfo *frb_compact_info(const FrbCompact *b) { return b ? &b->info : NULL; }

uint32_t frb_compact_ordinal(const FrbCompact *b, int bits, uint32_t value) {
    return ordinal_at(b, index_place(bits, value));
}

int frb_compact_nodes_of_ordinal(const FrbCompact *b, uint32_t ordinal) { return class_of(&b->info, ordinal); }

int frb_compact_nodes(const FrbCompact *b, int bits, uint32_t value) {
    return class_of(&b->info, ordinal_at(b, index_place(bits, value)));
}

FrbError frb_compact_graph(const FrbCompact *b, int bits, uint32_t value, RevGraph *g) {
    uint32_t ordinal;
    int n;
    if (!b || !g || bits < 1 || bits > b->info.bits || value >= ((uint32_t)1 << bits)) return FRB_ERR_ARG;
    ordinal = ordinal_at(b, index_place(bits, value));
    n = class_of(&b->info, ordinal);
    if (!n) return FRB_ERR_ENTRY;
    return frb_compact_unpack(graph_at(b, n, ordinal), n, g);
}

static uint32_t crc_of(const uint8_t *p, uint64_t bytes) {
    uint32_t crc = 0;
    while (bytes) {
        size_t now = bytes < (uint64_t)IO_CHUNK ? (size_t)bytes : (size_t)IO_CHUNK;
        crc = frb_crc32(crc, p, now);
        p += now;
        bytes -= now;
    }
    return crc;
}

/* Everything after the header and directory. */
static FrbError validate(const FrbCompact *b, uint32_t *bad_index) {
    const FrbCompactInfo *info = &b->info;
    uint32_t next[FRB_COMPACT_CLASSES + 1] = {0};
    uint64_t index_end = info->index_offset + info->index_entries * (uint64_t)info->index_bytes;
    int L = info->bits;
    RevGraph g;

    if (crc_of(b->base + info->index_offset, index_end - info->index_offset) != info->index_crc)
        return FRB_ERR_CHECKSUM;
    for (uint64_t at = index_end; at < info->pool_offset; at++)
        if (b->base[at] != 0) return FRB_ERR_HEADER;
    for (int n = 1; n <= FRB_COMPACT_CLASSES; n++)
        if (crc_of(b->base + info->class_offset[n], (uint64_t)info->class_count[n] * info->class_stride[n]) !=
            info->class_crc[n])
            return FRB_ERR_CHECKSUM;

    /*
     * The leaves, in the order of the strings.  Within a class the ordinals
     * go up with the string, so the next string of a class must have the
     * next ordinal of that class: that is the order the format prescribes,
     * and that every ordinal is used exactly once follows from it.
     */
    for (uint32_t t = 0;; t++) {
        uint32_t ordinal = ordinal_at(b, index_place(L, t));
        int n = class_of(info, ordinal);
        FrbError e = FRB_OK;
        if (!n || ordinal != info->class_start[n] + next[n]) e = FRB_ERR_ENTRY;
        else if ((e = frb_compact_unpack(graph_at(b, n, ordinal), n, &g)) == FRB_OK &&
                 frb_graph_output(&g, L) != t)
            e = FRB_ERR_OUTPUT;
        if (e != FRB_OK) {
            if (bad_index) *bad_index = t;
            return e;
        }
        next[n]++;
        if (t == info->count - 1) break;
    }
    /* Every shorter string: the smaller of its two extensions by one bit. */
    for (int bits = L - 1; bits >= 1; bits--) {
        for (uint32_t v = 0; v < ((uint32_t)1 << bits); v++) {
            uint32_t zero = ordinal_at(b, index_place(bits + 1, 2 * v));
            uint32_t one = ordinal_at(b, index_place(bits + 1, 2 * v + 1));
            if (ordinal_at(b, index_place(bits, v)) != (one < zero ? one : zero)) {
                if (bad_index) *bad_index = v << (L - bits);
                return FRB_ERR_ENTRY;
            }
        }
    }
    return FRB_OK;
}

FrbError frb_compact_open(const char *path, FrbCompact **bomb, uint32_t *bad_index) {
    FrbCompact *b;
    FILE *f;
    FrbError e;
    if (!bomb) return FRB_ERR_ARG;
    *bomb = NULL;
    b = calloc(1, sizeof *b);
    if (!b) return FRB_ERR_MEMORY;
    e = open_checked(path, &b->info, &f);
    if (e != FRB_OK) {
        free(b);
        return e;
    }
    if (b->info.file_bytes > (uint64_t)SIZE_MAX) e = FRB_ERR_MEMORY;
#if !defined(_WIN32)
    if (e == FRB_OK && !getenv("CARLOBOMB_NO_MMAP")) {
        void *map = mmap(NULL, (size_t)b->info.file_bytes, PROT_READ, MAP_PRIVATE, fileno(f), 0);
        if (map != MAP_FAILED) {
            b->mapping = map;
            b->mapping_bytes = (size_t)b->info.file_bytes;
            b->base = map;
        }
    }
#endif
    if (e == FRB_OK && !b->base) { /* read it */
        size_t bytes = (size_t)b->info.file_bytes;
        b->heap = malloc(bytes);
        if (!b->heap) e = FRB_ERR_MEMORY;
        else if (seek64(f, 0) != 0) e = FRB_ERR_IO;
        for (size_t done = 0; e == FRB_OK && done < bytes;) {
            size_t now = bytes - done < (size_t)IO_CHUNK ? bytes - done : (size_t)IO_CHUNK;
            if (fread(b->heap + done, 1, now, f) != now) e = FRB_ERR_IO;
            done += now;
        }
        b->base = b->heap;
    }
    fclose(f);
    if (e == FRB_OK) e = validate(b, bad_index);
    if (e != FRB_OK) {
        frb_compact_close(b);
        return e;
    }
    *bomb = b;
    return FRB_OK;
}

FrbError frb_compact_check_symmetry(const FrbCompact *b, uint32_t *bad_index) {
    uint32_t mask;
    int L;
    if (!b) return FRB_ERR_ARG;
    L = b->info.bits;
    mask = b->info.count - 1u;
    for (uint32_t t = 0;; t++) {
        RevGraph g, c;
        FrbError e = frb_compact_graph(b, L, t, &g);
        if (e == FRB_OK && frb_compact_nodes(b, L, t ^ mask) != g.node_count) e = FRB_ERR_EVIDENCE;
        if (e == FRB_OK) {
            frb_graph_complement(&g, &c);
            if (rev_graph_validate(&c) != REV_OK || frb_graph_output(&c, L) != (t ^ mask)) e = FRB_ERR_EVIDENCE;
        }
        if (e != FRB_OK) {
            if (bad_index) *bad_index = t;
            return e;
        }
        if (t == mask) break;
    }
    return FRB_OK;
}

/* ---- to and from the fixed-width formats ---------------------------------- */

FrbError frb_compact_equals_table(const FrbCompact *b, const FrbTable *table, uint32_t *bad_index) {
    uint32_t waiting[FRB_MAX_BITS + 1]; /* [k]: the choice of a string of k bits ending in 0 */
    int L;
    if (!b || !table || !table->entries || !table->format || table->bits != b->info.bits) return FRB_ERR_ARG;
    L = b->info.bits;
    for (uint32_t t = 0;; t++) {
        /* String for string: the REV the table has. */
        RevGraph mine, theirs;
        FrbStatus status;
        uint32_t chosen = t, v = t;
        int bits;
        int same = frb_entry_decode_as(table->format, frb_table_entry(table, t), &status, &theirs) == FRB_OK &&
                   status == FRB_MINIMAL && frb_compact_graph(b, L, t, &mine) == FRB_OK &&
                   mine.node_count == theirs.node_count;
        for (int i = 0; same && i < mine.node_count; i++)
            same = mine.nodes[i].color == theirs.nodes[i].color &&
                   mine.nodes[i].links[0] == theirs.nodes[i].links[0] &&
                   mine.nodes[i].links[1] == theirs.nodes[i].links[1];
        if (!same) {
            if (bad_index) *bad_index = t;
            return FRB_ERR_EVIDENCE;
        }
        /*
         * Shorter strings, worked out from the table alone: each is answered
         * by its extension with the fewest nodes, the lowest index among
         * equals.  A string ending in 1 closes the strings it is the last
         * extension of: for each, the choice of the half ending in 0, which
         * has waited, against the choice of the half that just closed.
         */
        for (bits = L; bits > 1 && (v & 1u); bits--) {
            uint32_t zero = waiting[bits];
            if (frb_table_nodes(table, chosen) >= frb_table_nodes(table, zero)) chosen = zero;
            v >>= 1;
            if (frb_compact_ordinal(b, bits - 1, v) != frb_compact_ordinal(b, L, chosen)) {
                if (bad_index) *bad_index = v << (L - bits + 1);
                return FRB_ERR_EVIDENCE;
            }
        }
        waiting[bits] = chosen; /* a string of that many bits, ending in 0 unless it is the last of all */
        if (t == table->count - 1) break;
    }
    return FRB_OK;
}

static const FrbFormat *format_of(const FrbCompact *b, const FrbFormat *format) {
    if (!format) format = frb_format(b->info.source_version);
    if (!format) format = frb_format_for_bits(b->info.bits);
    return format && b->info.bits <= format->max_bits ? format : NULL;
}

FrbError frb_compact_to_table(const FrbCompact *b, const FrbFormat *format, FrbTable *table) {
    FrbError e;
    if (!b || !table) return FRB_ERR_ARG;
    format = format_of(b, format);
    if (!format) return FRB_ERR_ARG;
    e = frb_table_create_as(table, b->info.bits, format);
    for (uint32_t t = 0; e == FRB_OK; t++) {
        RevGraph g;
        e = frb_compact_graph(b, b->info.bits, t, &g);
        if (e == FRB_OK) e = frb_table_put(table, t, FRB_MINIMAL, &g); /* runs it */
        if (t == b->info.count - 1) break;
    }
    if (e != FRB_OK) frb_table_destroy(table);
    return e;
}

FrbError frb_compact_table_crc(const FrbCompact *b, const FrbFormat *format, uint32_t *table_crc) {
    uint8_t entry[FRB_MAX_ENTRY_BYTES];
    uint32_t crc = 0;
    if (!b || !table_crc) return FRB_ERR_ARG;
    format = format_of(b, format);
    if (!format) return FRB_ERR_ARG;
    for (uint32_t t = 0;; t++) {
        RevGraph g;
        FrbError e = frb_compact_graph(b, b->info.bits, t, &g);
        if (e == FRB_OK) e = frb_entry_encode_as(format, FRB_MINIMAL, &g, entry);
        if (e != FRB_OK) return e;
        crc = frb_crc32(crc, entry, (size_t)format->entry_bytes);
        if (t == b->info.count - 1) break;
    }
    *table_crc = crc;
    return FRB_OK;
}

FrbError frb_compact_expand(const FrbCompact *b, const FrbFormat *format, const char *path, uint32_t *table_crc) {
    FrbWriter *writer = NULL;
    FrbSummary summary;
    FrbFileInfo info;
    FrbTable back;
    uint32_t crc = 0;
    FrbError e;
    if (!b || !path) return FRB_ERR_ARG;
    format = format_of(b, format);
    if (!format) return FRB_ERR_ARG;
    e = frb_writer_open(path, b->info.bits, format, &writer);
    if (e != FRB_OK) return e;
    for (uint32_t t = 0;; t++) {
        RevGraph g;
        e = frb_compact_graph(b, b->info.bits, t, &g);
        if (e == FRB_OK) e = frb_writer_put(writer, FRB_MINIMAL, &g); /* runs it */
        if (e != FRB_OK) {
            frb_writer_abort(writer);
            return e;
        }
        if (t == b->info.count - 1) break;
    }
    e = frb_writer_finish(writer, &summary, &crc);
    if (e != FRB_OK) return e;
    /* What is on disk now, read as any reader would, against this Bomb. */
    e = frb_file_read(path, &back, &info, NULL);
    if (e == FRB_OK) {
        if (!info.complete || info.table_crc != crc || info.version != format->version) e = FRB_ERR_EVIDENCE;
        else e = frb_compact_equals_table(b, &back, NULL);
        frb_table_destroy(&back);
    }
    if (e != FRB_OK) {
        remove(path);
        return e;
    }
    if (table_crc) *table_crc = crc;
    return FRB_OK;
}

/* The bytes of a file being made: a shared mapping of the temporary file
 * where that can be had, so that a file larger than memory can be made;
 * else a buffer that is written out at the end. */
typedef struct {
    uint8_t *bytes;
    size_t size;
    int mapped;
    FILE *f;
} Image;

static FrbError image_open(Image *img, const char *tmp, uint64_t size) {
    memset(img, 0, sizeof *img);
    if (size > (uint64_t)SIZE_MAX) return FRB_ERR_MEMORY;
    img->size = (size_t)size;
    img->f = fopen(tmp, "w+b");
    if (!img->f) return FRB_ERR_IO;
#if !defined(_WIN32)
    if (!getenv("CARLOBOMB_NO_MMAP") && ftruncate(fileno(img->f), (off_t)size) == 0) {
        void *map = mmap(NULL, img->size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(img->f), 0);
        if (map != MAP_FAILED) {
            img->bytes = map;
            img->mapped = 1;
            return FRB_OK; /* a new file of that length is all zero */
        }
    }
#endif
    img->bytes = calloc(img->size, 1);
    if (!img->bytes) {
        fclose(img->f);
        img->f = NULL;
        remove(tmp);
        return FRB_ERR_MEMORY;
    }
    return FRB_OK;
}

/* Everything on disk, the file closed. */
static FrbError image_close(Image *img, int keep) {
    int ok = 1;
    if (!img->f) return FRB_OK;
#if !defined(_WIN32)
    if (img->mapped) {
        ok = !keep || msync(img->bytes, img->size, MS_SYNC) == 0;
        munmap(img->bytes, img->size);
        img->bytes = NULL;
    }
#endif
    if (!img->mapped && keep) {
        ok = seek64(img->f, 0) == 0;
        for (size_t done = 0; ok && done < img->size;) {
            size_t now = img->size - done < (size_t)IO_CHUNK ? img->size - done : (size_t)IO_CHUNK;
            ok = fwrite(img->bytes + done, 1, now, img->f) == now;
            done += now;
        }
    }
    if (!img->mapped) free(img->bytes);
    img->bytes = NULL;
    if (keep) ok = ok && fflush(img->f) == 0;
#if !defined(_WIN32)
    if (keep) ok = ok && fsync(fileno(img->f)) == 0;
#endif
    ok = (fclose(img->f) == 0) && ok;
    img->f = NULL;
    return ok ? FRB_OK : FRB_ERR_IO;
}

FrbError frb_compact_write(const char *path, const FrbTable *table, int source_version,
                           uint32_t source_table_crc, int index_bytes) {
    uint32_t next[FRB_COMPACT_CLASSES + 1] = {0};
    FrbCompactInfo info;
    FrbCompact *check = NULL;
    FrbSummary summary;
    Image img;
    char *tmp;
    size_t n;
    int L;
    FrbError e;

    if (!path || !table || !table->entries || !table->format) return FRB_ERR_ARG;
    e = frb_table_validate(table, &summary, NULL); /* every witness run */
    if (e != FRB_OK) return e;
    if (!summary.complete) return FRB_ERR_INCOMPLETE;
    L = table->bits;
    memset(&info, 0, sizeof info);
    for (int k = 1; k <= FRB_MAX_NODES && k <= FRB_COMPACT_CLASSES; k++)
        info.class_count[k] = summary.minimal_by_nodes[k];
    e = frb_compact_layout(&info, L, index_bytes);
    if (e != FRB_OK) return e;
    info.source_version = source_version;
    info.source_table_crc = source_table_crc;

    n = strlen(path);
    tmp = malloc(n + 5);
    if (!tmp) return FRB_ERR_MEMORY;
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".tmp", 5);
    e = image_open(&img, tmp, info.file_bytes);
    if (e != FRB_OK) {
        free(tmp);
        return e;
    }

    /* The strings in order: each takes the next ordinal of its class, so
     * every class fills from its start and the index from its start. */
    for (uint32_t t = 0; e == FRB_OK; t++) {
        RevGraph g;
        FrbStatus status;
        e = frb_entry_decode_as(table->format, frb_table_entry(table, t), &status, &g);
        if (e == FRB_OK) {
            int k = g.node_count;
            uint32_t ordinal = info.class_start[k] + next[k]++;
            put_ordinal(img.bytes + info.index_offset + index_place(L, t) * (uint64_t)info.index_bytes,
                        info.index_bytes, ordinal);
            e = frb_compact_pack(&g, img.bytes + info.class_offset[k] +
                                         (uint64_t)(ordinal - info.class_start[k]) * info.class_stride[k]);
        }
        if (t == info.count - 1) break;
    }
    for (int bits = L - 1; e == FRB_OK && bits >= 1; bits--) {
        for (uint32_t v = 0; v < ((uint32_t)1 << bits); v++) {
            const uint8_t *index = img.bytes + info.index_offset;
            uint32_t zero = get_ordinal(index + index_place(bits + 1, 2 * v) * (uint64_t)info.index_bytes,
                                        info.index_bytes);
            uint32_t one = get_ordinal(index + index_place(bits + 1, 2 * v + 1) * (uint64_t)info.index_bytes,
                                       info.index_bytes);
            put_ordinal(img.bytes + info.index_offset + index_place(bits, v) * (uint64_t)info.index_bytes,
                        info.index_bytes, one < zero ? one : zero);
        }
    }
    if (e == FRB_OK) {
        info.index_crc = crc_of(img.bytes + info.index_offset, info.index_entries * (uint64_t)info.index_bytes);
        for (int k = 1; k <= FRB_COMPACT_CLASSES; k++)
            info.class_crc[k] =
                crc_of(img.bytes + info.class_offset[k], (uint64_t)info.class_count[k] * info.class_stride[k]);
        write_header(&info, img.bytes);
    }
    if (e == FRB_OK) e = image_close(&img, 1);
    else image_close(&img, 0);

    /* Read it back as any reader would, and against the table, before it
     * is called by its name. */
    if (e == FRB_OK) e = frb_compact_open(tmp, &check, NULL);
    if (e == FRB_OK) e = frb_compact_equals_table(check, table, NULL);
    frb_compact_close(check);
    if (e == FRB_OK) {
#if defined(_WIN32)
        remove(path);
#endif
        if (rename(tmp, path) != 0) e = FRB_ERR_IO;
    }
    if (e != FRB_OK) remove(tmp);
    free(tmp);
    return e;
}
