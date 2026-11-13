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

#include "topology.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const uint8_t frb_magic[8] = {'C', 'B', 'F', 'R', 'B', 0x0D, 0x0A, 0x1A};

/* Header field offsets.  Bytes 0..13 and 16..33 mean what they mean in a
 * fixed-width header.  Byte 14 is the block shift where that has the bits of
 * a node, and byte 15, its entry size, is 0: there are no entries of a fixed
 * size. */
enum {
    C_MAGIC = 0,
    C_VERSION = 8,
    C_HEADER_SIZE = 10,
    C_BITS = 12,
    C_CAPACITY = 13,
    C_BLOCK_SHIFT = 14,
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
    C_OTHER_REVS = 56,
    C_HEADER_CRC = 60
};

/* Class directory row offsets. */
enum {
    D_COUNT = 0,
    D_BITS = 4,
    D_KIND = 5,      /* 0: colors and links; 1: the rank of the graph */
    D_RESERVED1 = 6, /* 2 bytes */
    D_OFFSET = 8,
    D_BYTES = 16,
    D_CRC = 24,
    D_RESERVED2 = 28 /* 4 bytes */
};

/* What the layout this format had as a draft wrote into bytes 13 and 14. */
enum { DRAFT_CAPACITY = 31, DRAFT_INDEX_BYTES_MAX = 4 };

enum { IO_CHUNK = 1 << 22, TAIL_BYTES = 8, BLOCK = 1 << FRB_COMPACT_BLOCK_SHIFT };

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

static inline uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }

static uint64_t align8(uint64_t v) { return (v + 7u) & ~(uint64_t)7; }

#if defined(_WIN32)
static int length64(FILE *f, uint64_t *length) {
    __int64 end;
    if (_fseeki64(f, 0, SEEK_END) != 0 || (end = _ftelli64(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
static int seek_start(FILE *f) { return _fseeki64(f, 0, SEEK_SET); }
#else
static int length64(FILE *f, uint64_t *length) {
    off_t end;
    if (fseeko(f, 0, SEEK_END) != 0 || (end = ftello(f)) < 0) return -1;
    *length = (uint64_t)end;
    return 0;
}
static int seek_start(FILE *f) { return fseeko(f, 0, SEEK_SET); }
#endif

/* ---- one record ------------------------------------------------------------ */

static int link_bits(int nodes) {
    int w = 0;
    while ((1 << w) < nodes) w++;
    return w;
}

int frb_compact_record_bits(int nodes) {
    if (nodes < 1 || nodes > FRB_COMPACT_MAX_NODES) return 0;
    if (nodes > FRB_COMPACT_PLAIN_NODES) return frb_topology_rank_bits(nodes);
    return nodes + 2 * nodes * link_bits(nodes);
}

/* Colors and links of a REV of up to 5 nodes as one number: the colors in
 * the low N bits, node 0 lowest; above them, node after node, its link on
 * color 0 and its link on color 1. */
static uint64_t plain_pack(const FrbTopology *t, uint32_t colors) {
    uint64_t code = 0;
    int w = link_bits(t->n);
    for (int i = t->n - 1; i >= 0; i--) {
        code = (code << w) | t->link[1][i];
        code = (code << w) | t->link[0][i];
    }
    return (code << t->n) | colors;
}

/* 0 if a link leads to a node that is not there. */
static int plain_unpack(uint64_t code, int nodes, FrbTopology *t, uint32_t *colors) {
    int w = link_bits(nodes);
    uint32_t mask = (1u << w) - 1u;
    memset(t, 0, sizeof *t);
    t->n = (uint8_t)nodes;
    *colors = (uint32_t)code & ((1u << nodes) - 1u);
    code >>= nodes;
    for (int i = 0; i < nodes; i++) {
        uint32_t l0 = (uint32_t)code & mask, l1 = (uint32_t)(code >> w) & mask;
        if (l0 >= (uint32_t)nodes || l1 >= (uint32_t)nodes) return 0;
        t->link[0][i] = (uint8_t)l0;
        t->link[1][i] = (uint8_t)l1;
        code >>= 2 * w;
    }
    return 1;
}

/* Record number `index` of a class of records of `width` bits (63 at most):
 * bit k of the class is bit k % 8 of its byte k / 8.  Reads 8 bytes from the
 * first byte of the record, and a ninth where the record reaches into one,
 * which the 8 zero bytes at the end of the file make safe for the last record
 * of the last class.  (No record of the format does reach into a ninth byte:
 * the widest has 58 bits, and a multiple of 58 is an even bit.) */
static inline uint64_t record_at(const uint8_t *class_bytes, uint64_t index, unsigned width) {
    uint64_t bit = index * width;
    const uint8_t *p = class_bytes + (bit >> 3);
    unsigned shift = (unsigned)(bit & 7u);
    uint64_t v = get64(p) >> shift;
    if (shift + width > 64) v |= (uint64_t)p[8] << (64 - shift);
    return v & (((uint64_t)1 << width) - 1u);
}

/* Into zeroed bytes. */
static void record_put(uint8_t *class_bytes, uint64_t index, unsigned width, uint64_t value) {
    uint64_t bit = index * width;
    uint8_t *p = class_bytes + (bit >> 3);
    unsigned shift = (unsigned)(bit & 7u), bytes = (shift + width + 7) / 8;
    uint64_t low = value << shift;
    for (unsigned i = 0; i < bytes && i < 8; i++) p[i] |= (uint8_t)((low >> (8 * i)) & 0xFFu);
    if (bytes > 8) p[8] |= (uint8_t)(value >> (64 - shift));
}

/* ---- layout ---------------------------------------------------------------- */

FrbError frb_compact_layout(FrbCompactInfo *info, int bits) {
    uint64_t sum = 0, offset;
    if (!info || bits < 1 || bits > FRB_MAX_BITS) return FRB_ERR_ARG;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) sum += info->class_count[n];
    if (sum != ((uint64_t)1 << (bits - 1))) return FRB_ERR_ARG;
    info->bits = bits;
    info->block_shift = FRB_COMPACT_BLOCK_SHIFT;
    info->count = (uint32_t)1 << bits;
    info->stored = (uint32_t)1 << (bits - 1);
    info->max_nodes = 0;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++)
        if (info->class_count[n]) info->max_nodes = n;
    info->nav_offset = FRB_COMPACT_INDEX_OFFSET;
    info->nav_bytes = align8(((uint64_t)info->stored + 7u) / 8u);
    info->nodes_offset = info->nav_offset + info->nav_bytes;
    info->nodes_bytes = align8(((uint64_t)info->stored + 1u) / 2u);
    info->blocks_offset = info->nodes_offset + info->nodes_bytes;
    info->block_rows = ((info->stored - 1u) >> FRB_COMPACT_BLOCK_SHIFT) + 1u;
    info->blocks_bytes = align8((uint64_t)info->block_rows * (uint64_t)info->max_nodes * 4u);
    offset = info->blocks_offset + info->blocks_bytes;
    info->class_count[0] = info->class_bits[0] = info->class_crc[0] = 0;
    info->class_offset[0] = info->class_bytes[0] = 0;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) {
        info->class_bits[n] = (uint32_t)frb_compact_record_bits(n);
        info->class_offset[n] = offset;
        info->class_bytes[n] = align8(((uint64_t)info->class_count[n] * info->class_bits[n] + 7u) / 8u);
        offset += info->class_bytes[n];
    }
    info->file_bytes = offset + TAIL_BYTES;
    return FRB_OK;
}

static void write_header(const FrbCompactInfo *info, uint8_t *h) {
    memset(h, 0, FRB_COMPACT_INDEX_OFFSET);
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) {
        uint8_t *row = h + FRB_HEADER_BYTES + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES;
        put32(row + D_COUNT, info->class_count[n]);
        row[D_BITS] = (uint8_t)info->class_bits[n];
        row[D_KIND] = n > FRB_COMPACT_PLAIN_NODES;
        put64(row + D_OFFSET, info->class_offset[n]);
        put64(row + D_BYTES, info->class_bytes[n]);
        put32(row + D_CRC, info->class_crc[n]);
    }
    memcpy(h + C_MAGIC, frb_magic, sizeof frb_magic);
    put16(h + C_VERSION, FRB_FORMAT_COMPACT);
    put16(h + C_HEADER_SIZE, FRB_HEADER_BYTES);
    h[C_BITS] = (uint8_t)info->bits;
    h[C_CAPACITY] = FRB_COMPACT_MAX_NODES;
    h[C_BLOCK_SHIFT] = FRB_COMPACT_BLOCK_SHIFT;
    h[C_ENTRY_SIZE] = 0;
    put32(h + C_COUNT, info->count);
    put32(h + C_FLAGS, FRB_FLAG_COMPLETE);
    put32(h + C_WITNESSES, info->count);
    put32(h + C_MINIMAL, info->count);
    h[C_MAX_MINIMAL] = (uint8_t)info->max_nodes;
    h[C_MAX_WITNESS] = (uint8_t)info->max_nodes;
    put16(h + C_SOURCE_VERSION, (unsigned)info->source_version);
    put32(h + C_DIRECTORY_CRC,
          frb_crc32(0, h + FRB_HEADER_BYTES, FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES));
    put32(h + C_INDEX_CRC, info->index_crc);
    put32(h + C_SOURCE_CRC, info->source_table_crc);
    put64(h + C_FILE_BYTES, info->file_bytes);
    put32(h + C_OTHER_REVS, info->other_revs);
    put32(h + C_HEADER_CRC, frb_crc32(0, h, C_HEADER_CRC));
}

/* The first 448 bytes of a file: everything in them checked. */
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
    if (h[C_CAPACITY] != FRB_COMPACT_MAX_NODES || h[C_BLOCK_SHIFT] != FRB_COMPACT_BLOCK_SHIFT || h[C_ENTRY_SIZE] != 0)
        return FRB_ERR_HEADER; /* the draft layout, among others: 31 and the bytes of an ordinal there */
    if (get32(h + C_DIRECTORY_CRC) != frb_crc32(0, dir, FRB_COMPACT_MAX_NODES * FRB_COMPACT_ROW_BYTES))
        return FRB_ERR_HEADER;

    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++)
        info->class_count[n] = get32(dir + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES + D_COUNT);
    /* Everything else in the directory follows from the counts. */
    if (frb_compact_layout(info, h[C_BITS]) != FRB_OK) return FRB_ERR_HEADER;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) {
        const uint8_t *row = dir + (size_t)(n - 1) * FRB_COMPACT_ROW_BYTES;
        if (row[D_BITS] != info->class_bits[n] || row[D_KIND] != (n > FRB_COMPACT_PLAIN_NODES) ||
            row[D_RESERVED1] || row[D_RESERVED1 + 1] || get64(row + D_OFFSET) != info->class_offset[n] ||
            get64(row + D_BYTES) != info->class_bytes[n] || get32(row + D_RESERVED2) != 0)
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
    /* A source that could not have held strings of this length is no source. */
    if (info->source_version && info->bits > frb_format(info->source_version)->max_bits) return FRB_ERR_HEADER;
    info->directory_crc = get32(h + C_DIRECTORY_CRC);
    info->index_crc = get32(h + C_INDEX_CRC);
    info->source_table_crc = get32(h + C_SOURCE_CRC);
    info->other_revs = get32(h + C_OTHER_REVS);
    if (info->other_revs > info->count) return FRB_ERR_HEADER;
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

int frb_compact_file_is_draft(const char *path) {
    uint8_t h[16];
    FILE *f = path ? fopen(path, "rb") : NULL;
    size_t got;
    if (!f) return 0;
    got = fread(h, 1, sizeof h, f);
    fclose(f);
    return got == sizeof h && memcmp(h, frb_magic, sizeof frb_magic) == 0 &&
           get16(h + C_VERSION) == FRB_FORMAT_COMPACT && h[C_CAPACITY] == DRAFT_CAPACITY &&
           h[C_BLOCK_SHIFT] >= 1 && h[C_BLOCK_SHIFT] <= DRAFT_INDEX_BYTES_MAX && h[C_ENTRY_SIZE] == 0;
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

/* ---- a compact Bomb in use ----------------------------------------------- */

struct FrbCompact {
    FrbCompactInfo info;
    const uint8_t *base; /* the whole file */
    void *mapping;       /* not NULL: base is a mapping of this length */
    size_t mapping_bytes;
    uint8_t *heap;       /* else the copy that base points to */
    /* into base, for the lookups */
    const uint8_t *nav, *nodes, *blocks;
    const uint8_t *classes[FRB_COMPACT_MAX_NODES + 1];
    uint32_t mask; /* 2^L - 1 */
};

void frb_compact_close(FrbCompact *b) {
    if (!b) return;
#if !defined(_WIN32)
    if (b->mapping) munmap(b->mapping, b->mapping_bytes);
#endif
    free(b->heap);
    free(b);
}

const FrbCompactInfo *frb_compact_info(const FrbCompact *b) { return b ? &b->info : NULL; }

/* Header and directory checked, the file in memory; nothing else looked at. */
static FrbError load(const char *path, FrbCompact **bomb) {
    FrbCompact *b;
    FILE *f;
    FrbError e;
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
        else if (seek_start(f) != 0) e = FRB_ERR_IO;
        for (size_t done = 0; e == FRB_OK && done < bytes;) {
            size_t now = bytes - done < (size_t)IO_CHUNK ? bytes - done : (size_t)IO_CHUNK;
            if (fread(b->heap + done, 1, now, f) != now) e = FRB_ERR_IO;
            done += now;
        }
        b->base = b->heap;
    }
    fclose(f);
    if (e != FRB_OK) {
        frb_compact_close(b);
        return e;
    }
    b->nav = b->base + b->info.nav_offset;
    b->nodes = b->base + b->info.nodes_offset;
    b->blocks = b->base + b->info.blocks_offset;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) b->classes[n] = b->base + b->info.class_offset[n];
    b->mask = b->info.count - 1u;
    frb_topology_init(); /* before there can be a second thread asking */
    *bomb = b;
    return FRB_OK;
}

/* The navigation bit at a place: bit place % 8 of byte place / 8. */
static inline unsigned nav_bit(const uint8_t *nav, uint64_t place) {
    return (unsigned)(nav[place >> 3] >> (place & 7u)) & 1u;
}

/* The node count of stored string s: the low half of byte s / 2 for an even s, the high half for an odd one. */
static inline unsigned nodes_at(const uint8_t *nodes, uint32_t s) {
    return (unsigned)(nodes[s >> 1] >> (4u * (s & 1u))) & 15u;
}

/*
 * The L-bit string that answers the string of `bits` bits (1..L).
 *
 * The bit of a stored string of k bits is at place 2^(k-1) + its value: the
 * strings that start with 0, without that 0, as the nodes of a binary tree
 * in level order.  A string that starts with 1 takes the way of its
 * complement and the complement of where that leads.
 */
static inline uint32_t leaf_of(const FrbCompact *b, int bits, uint32_t value) {
    int L = b->info.bits;
    uint32_t mirror = 0u - ((value >> (bits - 1)) & 1u);
    uint64_t place;
    value ^= mirror & (uint32_t)((((uint64_t)1 << bits) - 1u));
    place = ((uint64_t)1 << (bits - 1)) + value;
    for (int k = bits; k < L; k++) place = 2 * place + nav_bit(b->nav, place);
    return (uint32_t)(place - ((uint64_t)1 << (L - 1))) ^ (mirror & b->mask);
}

/* How many of the first `limit` (0..16) of the sixteen node counts in a word are the one of the pattern. */
static inline uint32_t count_equal(uint64_t word, uint64_t pattern, unsigned limit) {
    uint64_t x = word ^ pattern; /* a node count that is the one: 0000 */
    uint64_t equal = ~(x | (x >> 1) | (x >> 2) | (x >> 3)) & 0x1111111111111111ull;
    if (limit < 16) equal &= ((uint64_t)1 << (4 * limit)) - 1u;
    equal = (equal + (equal >> 4)) & 0x0F0F0F0F0F0F0F0Full; /* two a byte, then the eight bytes added up */
    return (uint32_t)((equal * 0x0101010101010101ull) >> 56);
}

/* How many stored strings before s have n nodes: the count kept for its
 * block of 256 plus those before it within the block. */
static inline uint32_t rank_of(const FrbCompact *b, uint32_t s, unsigned n) {
    uint32_t block = s >> FRB_COMPACT_BLOCK_SHIFT;
    uint32_t r = get32(b->blocks + ((size_t)block * (size_t)b->info.max_nodes + (n - 1u)) * 4u);
    const uint8_t *p = b->nodes + (size_t)block * (BLOCK / 2);
    uint64_t pattern = 0x1111111111111111ull * n;
    unsigned words = (s & (BLOCK - 1u)) >> 4;
    for (unsigned i = 0; i < words; i++) r += count_equal(get64(p + 8u * i), pattern, 16);
    return r + count_equal(get64(p + 8u * words), pattern, s & 15u);
}

/* The REV of stored string s (which starts with 0), numbered and colored. */
static FrbError stored_rev(const FrbCompact *b, uint32_t s, FrbTopology *t, uint32_t *colors) {
    unsigned n = nodes_at(b->nodes, s);
    uint64_t record;
    uint32_t place;
    if (n < 1 || n > (unsigned)b->info.max_nodes) return FRB_ERR_ENTRY;
    place = rank_of(b, s, n);
    if (place >= b->info.class_count[n]) return FRB_ERR_ENTRY;
    record = record_at(b->classes[n], place, b->info.class_bits[n]);
    if (n <= FRB_COMPACT_PLAIN_NODES) {
        if (!plain_unpack(record, (int)n, t, colors)) return FRB_ERR_ENTRY;
    } else {
        FrbTopology canonical;
        uint8_t order[FRB_TOPOLOGY_MAX_NODES];
        uint32_t found = 0;
        int reached;
        if (!frb_topology_unrank((int)n, record, &canonical)) return FRB_ERR_ENTRY;
        /* the colors, and with them the order in which the machine reaches the nodes: the numbering */
        reached = frb_topology_find_colors(&canonical, s, b->info.bits, &found, order);
        if (reached == 0) return FRB_ERR_OUTPUT; /* no colors with which this graph emits the string */
        /* It emits the string and does not need all its nodes for it: not the record of a REV of
         * this many nodes for this string, and no numbering of its nodes follows from the string. */
        if (reached != (int)n) return FRB_ERR_ENTRY;
        frb_topology_renumber(&canonical, found, order, t, colors);
    }
    return FRB_OK;
}

/* The REV of any string of L bits. */
static FrbError rev_of_leaf(const FrbCompact *b, uint32_t leaf, RevGraph *g) {
    FrbTopology t;
    uint32_t colors = 0, flip = leaf >> (b->info.bits - 1);
    FrbError e = stored_rev(b, flip ? leaf ^ b->mask : leaf, &t, &colors);
    if (e != FRB_OK) {
        g->node_count = 0;
        return e;
    }
    if (flip) { /* the complement: the other colors, the two links of every node exchanged */
        for (int i = 0; i < t.n; i++) {
            uint8_t l0 = t.link[0][i];
            t.link[0][i] = t.link[1][i];
            t.link[1][i] = l0;
        }
        colors ^= (1u << t.n) - 1u;
    }
    frb_topology_to_graph(&t, colors, g);
    return FRB_OK;
}

uint32_t frb_compact_extension(const FrbCompact *b, int bits, uint32_t value) { return leaf_of(b, bits, value); }

int frb_compact_nodes(const FrbCompact *b, int bits, uint32_t value) {
    uint32_t leaf = leaf_of(b, bits, value);
    uint32_t flip = 0u - (leaf >> (b->info.bits - 1));
    return (int)nodes_at(b->nodes, leaf ^ (flip & b->mask));
}

FrbError frb_compact_graph(const FrbCompact *b, int bits, uint32_t value, RevGraph *g) {
    if (!b || !g || bits < 1 || bits > b->info.bits || value > (b->mask >> (b->info.bits - bits))) return FRB_ERR_ARG;
    return rev_of_leaf(b, leaf_of(b, bits, value), g);
}

int frb_compact_compare(const FrbCompact *b, int bits, uint32_t value, int nodes[2]) {
    int n0, n1;
    if (!b || bits < 0 || bits >= b->info.bits || (uint64_t)value >> bits) return -1;
    n0 = frb_compact_nodes(b, bits + 1, 2 * value);
    n1 = frb_compact_nodes(b, bits + 1, 2 * value + 1);
    if (nodes) nodes[0] = n0, nodes[1] = n1;
    return n0 < n1 ? 0 : n1 < n0 ? 1 : FRB_COMPACT_TIE;
}

FrbError frb_compact_file_lookup(const char *path, int bits, uint32_t value, FrbCompactInfo *info,
                                 RevGraph *g, uint32_t *extension) {
    FrbCompact *b = NULL;
    RevGraph graph;
    uint32_t leaf;
    FrbError e;
    if (!g) g = &graph;
    g->node_count = 0;
    e = load(path, &b);
    if (e != FRB_OK) {
        if (info) memset(info, 0, sizeof *info);
        return e;
    }
    if (info) *info = b->info;
    if (bits < 1 || bits > b->info.bits || value > (b->mask >> (b->info.bits - bits))) {
        frb_compact_close(b);
        return FRB_ERR_ARG;
    }
    leaf = leaf_of(b, bits, value);
    e = rev_of_leaf(b, leaf, g);
    if (e == FRB_OK && ((leaf >> (b->info.bits - bits)) != value || frb_graph_output(g, b->info.bits) != leaf))
        e = FRB_ERR_OUTPUT; /* not an extension of the string, or not the string of its REV */
    if (e == FRB_OK && extension) *extension = leaf;
    frb_compact_close(b);
    return e;
}

FrbError frb_compact_file_compare(const char *path, int bits, uint32_t value, FrbCompactInfo *info, int *which,
                                  int nodes[2]) {
    FrbCompact *b = NULL;
    int counts[2] = {0, 0}, answer;
    FrbError e;
    if (!which) return FRB_ERR_ARG;
    e = load(path, &b);
    if (e != FRB_OK) {
        if (info) memset(info, 0, sizeof *info);
        return e;
    }
    if (info) *info = b->info;
    answer = frb_compact_compare(b, bits, value, counts);
    if (answer < 0) e = FRB_ERR_ARG;
    else if (counts[0] < 1 || counts[0] > b->info.max_nodes || counts[1] < 1 || counts[1] > b->info.max_nodes)
        e = FRB_ERR_ENTRY; /* nothing was validated: a node count that is none */
    if (e == FRB_OK) {
        *which = answer;
        if (nodes) nodes[0] = counts[0], nodes[1] = counts[1];
    }
    frb_compact_close(b);
    return e;
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

static int all_zero(const uint8_t *p, uint64_t bytes) {
    for (uint64_t i = 0; i < bytes; i++)
        if (p[i]) return 0;
    return 1;
}

/* Everything after the header and directory. */
static FrbError validate(const FrbCompact *b, uint32_t *bad_index) {
    const FrbCompactInfo *info = &b->info;
    uint32_t running[FRB_COMPACT_MAX_NODES + 1] = {0};
    uint8_t waiting[FRB_MAX_BITS + 1]; /* [k]: fewest nodes under a string of k bits after the first that ends in 0 */
    int L = info->bits, top = L - 1;   /* the stored strings without their first bit, 0, have L-1 bits */

    if (crc_of(b->nav, info->class_offset[1] - info->nav_offset) != info->index_crc) return FRB_ERR_CHECKSUM;
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++)
        if (crc_of(b->classes[n], info->class_bytes[n]) != info->class_crc[n]) return FRB_ERR_CHECKSUM;

    /* What no string stands on is zero: place 0 of the navigation bits and the places after the
     * last, the node counts after the last, the rows after the last, the bits after the last
     * record of a class, the last 8 bytes. */
    if (nav_bit(b->nav, 0)) return FRB_ERR_HEADER;
    for (uint64_t place = info->stored; place < info->nav_bytes * 8u; place++)
        if (nav_bit(b->nav, place)) return FRB_ERR_HEADER;
    for (uint64_t s = info->stored; s < info->nodes_bytes * 2u; s++)
        if (nodes_at(b->nodes, (uint32_t)s)) return FRB_ERR_HEADER;
    {
        uint64_t used = (uint64_t)info->block_rows * (uint64_t)info->max_nodes * 4u;
        if (!all_zero(b->blocks + used, info->blocks_bytes - used)) return FRB_ERR_HEADER;
    }
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++) {
        uint64_t used = (uint64_t)info->class_count[n] * info->class_bits[n];
        if ((used & 7u) && (b->classes[n][used >> 3] >> (used & 7u)) != 0) return FRB_ERR_HEADER;
        if (!all_zero(b->classes[n] + (used + 7u) / 8u, info->class_bytes[n] - (used + 7u) / 8u)) return FRB_ERR_HEADER;
    }
    if (!all_zero(b->base + info->file_bytes - TAIL_BYTES, TAIL_BYTES)) return FRB_ERR_HEADER;

    /* The stored strings in order. */
    for (uint32_t s = 0; s < info->stored; s++) {
        unsigned n = nodes_at(b->nodes, s);
        FrbTopology t;
        uint32_t colors = 0, v = s;
        uint8_t least;
        FrbError e = FRB_OK;
        int k;
        if (n < 1 || n > (unsigned)info->max_nodes) e = FRB_ERR_ENTRY;
        /* the row of its block: the strings of each node count so far */
        for (int c = 1; e == FRB_OK && (s & (BLOCK - 1u)) == 0 && c <= info->max_nodes; c++)
            if (get32(b->blocks + ((size_t)(s >> FRB_COMPACT_BLOCK_SHIFT) * (size_t)info->max_nodes + (size_t)(c - 1)) * 4u) !=
                running[c])
                e = FRB_ERR_ENTRY;
        /* its REV: decoded, its colors found or, where they are stored, the REV run */
        if (e == FRB_OK) e = stored_rev(b, s, &t, &colors);
        if (e == FRB_OK && frb_topology_output(&t, colors, L) != s) e = FRB_ERR_OUTPUT;
        if (e != FRB_OK) {
            if (bad_index) *bad_index = s;
            return e;
        }
        running[n]++;
        /* A string that ends in 1 closes the strings it is the last extension of: for each, the
         * bit says whether the half that ends in 1 needs fewer nodes than the half that waited. */
        least = (uint8_t)n;
        for (k = top; k > 0 && (v & 1u); k--) {
            uint8_t zero = waiting[k];
            v >>= 1;
            if (nav_bit(b->nav, ((uint64_t)1 << (k - 1)) + v) != (unsigned)(least < zero)) {
                if (bad_index) *bad_index = v << (top - (k - 1));
                return FRB_ERR_ENTRY;
            }
            if (zero < least) least = zero;
        }
        waiting[k] = least;
    }
    for (int n = 1; n <= FRB_COMPACT_MAX_NODES; n++)
        if (running[n] != info->class_count[n]) return FRB_ERR_SUMMARY;
    return FRB_OK;
}

FrbError frb_compact_open(const char *path, FrbCompact **bomb, uint32_t *bad_index) {
    FrbCompact *b;
    FrbError e;
    if (!bomb) return FRB_ERR_ARG;
    *bomb = NULL;
    e = load(path, &b);
    if (e == FRB_OK) e = validate(b, bad_index);
    if (e != FRB_OK) {
        frb_compact_close(b);
        return e;
    }
    *bomb = b;
    return FRB_OK;
}

/* ---- to and from the fixed-width formats ---------------------------------- */

static int same_graph(const RevGraph *a, const RevGraph *b) {
    if (a->node_count != b->node_count) return 0;
    for (int i = 0; i < a->node_count; i++)
        if (a->nodes[i].color != b->nodes[i].color || a->nodes[i].links[0] != b->nodes[i].links[0] ||
            a->nodes[i].links[1] != b->nodes[i].links[1])
            return 0;
    return 1;
}

/* Bit L+1 of a REV of at most 12 nodes. */
static unsigned next_bit(const RevGraph *g, int L) {
    FrbTopology t;
    uint32_t colors = 0;
    if (!frb_topology_from_graph(g, &t, &colors)) return 0;
    return frb_topology_output(&t, colors, L + 1) & 1u;
}

FrbError frb_compact_against_table(const FrbCompact *b, const FrbTable *table, FrbCompactDifference *difference,
                                   uint32_t *bad_index) {
    /* [k]: for the string of k bits ending in 0 that waits for its other half, the lowest and the
     * highest of its extensions with the fewest nodes */
    uint32_t waiting_low[FRB_MAX_BITS + 1], waiting_high[FRB_MAX_BITS + 1];
    FrbCompactDifference d;
    int L;
    memset(&d, 0, sizeof d);
    if (difference) *difference = d;
    if (!b || !table || !table->entries || !table->format || table->bits != b->info.bits) return FRB_ERR_ARG;
    L = b->info.bits;
    for (uint32_t t = 0;; t++) {
        RevGraph mine, theirs;
        FrbStatus status;
        uint32_t low = t, high = t, v = t;
        int bits, nodes[2];
        /* String for string: the node count of the table, and a REV of that many nodes that emits it. */
        int ok = frb_entry_decode_as(table->format, frb_table_entry(table, t), &status, &theirs) == FRB_OK &&
                 status == FRB_MINIMAL && frb_compact_nodes(b, L, t) == theirs.node_count &&
                 frb_compact_graph(b, L, t, &mine) == FRB_OK && mine.node_count == theirs.node_count &&
                 rev_graph_validate(&mine) == REV_OK && frb_graph_output(&mine, L) == t;
        if (!ok) {
            if (bad_index) *bad_index = t;
            return FRB_ERR_EVIDENCE;
        }
        if (!same_graph(&mine, &theirs)) {
            d.other_revs++;
            if (L < 32 && next_bit(&mine, L) != next_bit(&theirs, L)) d.other_next_bit++;
        }
        /*
         * Shorter strings, worked out from the table alone.  A string ending
         * in 1 closes the strings it is the last extension of: for each, the
         * choices of the half ending in 0, which has waited, against those of
         * the half that just closed.
         */
        for (bits = L; bits > 1 && (v & 1u); bits--) {
            uint32_t zero_low = waiting_low[bits], zero_high = waiting_high[bits], answer;
            int n0 = frb_table_nodes(table, zero_low), n1 = frb_table_nodes(table, low), least = n0 < n1 ? n0 : n1;
            if (n0 < n1) low = zero_low, high = zero_high;
            else if (n0 == n1) low = zero_low; /* high stays: the highest under the half ending in 1 */
            v >>= 1;                           /* the string of bits-1 bits */
            answer = frb_compact_extension(b, bits - 1, v);
            /* one of its extensions with the fewest nodes; the lowest, or for a string that starts
             * with 1 the highest; and the comparison of its two halves */
            if ((answer >> (L - (bits - 1))) != v || frb_table_nodes(table, answer) != least ||
                frb_compact_nodes(b, bits - 1, v) != least || answer != (((v >> (bits - 2)) & 1u) ? high : low) ||
                frb_compact_compare(b, bits - 1, v, nodes) != (n0 < n1 ? 0 : n1 < n0 ? 1 : FRB_COMPACT_TIE) ||
                nodes[0] != n0 || nodes[1] != n1) {
                if (bad_index) *bad_index = v << (L - (bits - 1));
                return FRB_ERR_EVIDENCE;
            }
            if (answer != low) d.other_extension++;
        }
        if (t == b->mask) { /* the last string closes "1"; "0" waits: the two extensions of the empty string */
            int n0 = L > 1 ? frb_table_nodes(table, waiting_low[1]) : frb_table_nodes(table, 0);
            int n1 = frb_table_nodes(table, low);
            if (frb_compact_compare(b, 0, 0, nodes) != (n0 < n1 ? 0 : n1 < n0 ? 1 : FRB_COMPACT_TIE) ||
                nodes[0] != n0 || nodes[1] != n1) {
                if (bad_index) *bad_index = 0;
                return FRB_ERR_EVIDENCE;
            }
            break;
        }
        waiting_low[bits] = low; /* a string of that many bits, ending in 0 */
        waiting_high[bits] = high;
    }
    if (difference) *difference = d;
    return FRB_OK;
}

FrbError frb_compact_equals_table(const FrbCompact *b, const FrbTable *table, uint32_t *bad_index) {
    FrbCompactDifference d;
    FrbError e = frb_compact_against_table(b, table, &d, bad_index);
    if (e == FRB_OK && d.other_revs != b->info.other_revs) {
        if (bad_index) *bad_index = 0;
        e = FRB_ERR_EVIDENCE; /* the header counts other REVs than there are */
    }
    return e;
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
    /* What is on disk now, read as any reader would, against this Bomb: every REV the one it returns. */
    e = frb_file_read(path, &back, &info, NULL);
    if (e == FRB_OK) {
        FrbCompactDifference d;
        if (!info.complete || info.table_crc != crc || info.version != format->version) e = FRB_ERR_EVIDENCE;
        else e = frb_compact_against_table(b, &back, &d, NULL);
        if (e == FRB_OK && d.other_revs != 0) e = FRB_ERR_EVIDENCE;
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
        ok = seek_start(img->f) == 0;
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

/* One entry of the table as a graph and as links and colors: a certified minimum of at most 12 nodes. */
static FrbError table_rev(const FrbTable *table, uint32_t index, RevGraph *g, FrbTopology *t, uint32_t *colors) {
    FrbStatus status;
    FrbError e = frb_entry_decode_as(table->format, frb_table_entry(table, index), &status, g);
    if (e != FRB_OK) return e;
    if (status != FRB_MINIMAL) return FRB_ERR_INCOMPLETE;
    return frb_topology_from_graph(g, t, colors) ? FRB_OK : FRB_ERR_CAPACITY;
}

FrbError frb_compact_write(const char *path, const FrbTable *table, int source_version, uint32_t source_table_crc) {
    uint32_t running[FRB_COMPACT_MAX_NODES + 1] = {0}, mask, others = 0;
    uint8_t waiting[FRB_MAX_BITS + 1];
    uint8_t *nav, *nodes, *blocks;
    FrbCompactInfo info;
    FrbCompact *check = NULL;
    FrbSummary summary;
    Image img;
    char *tmp;
    size_t length;
    int L, top;
    FrbError e;

    if (!path || !table || !table->entries || !table->format) return FRB_ERR_ARG;
    e = frb_table_validate(table, &summary, NULL); /* every witness run */
    if (e != FRB_OK) return e;
    if (!summary.complete) return FRB_ERR_INCOMPLETE;
    for (int k = FRB_COMPACT_MAX_NODES + 1; k <= FRB_MAX_NODES; k++)
        if (summary.minimal_by_nodes[k]) return FRB_ERR_CAPACITY;
    L = table->bits;
    top = L - 1;
    mask = table->count - 1u;
    frb_topology_init();

    /* The classes of the half that is stored; and the two strings of a pair need the same. */
    memset(&info, 0, sizeof info);
    for (uint32_t s = 0; s < table->count / 2; s++) {
        int n = frb_table_nodes(table, s);
        if (n != frb_table_nodes(table, s ^ mask)) return FRB_ERR_EVIDENCE;
        info.class_count[n]++;
    }
    e = frb_compact_layout(&info, L);
    if (e != FRB_OK) return e;
    info.source_version = source_version;
    info.source_table_crc = source_table_crc;

    length = strlen(path);
    tmp = malloc(length + 5);
    if (!tmp) return FRB_ERR_MEMORY;
    memcpy(tmp, path, length);
    memcpy(tmp + length, ".tmp", 5);
    e = image_open(&img, tmp, info.file_bytes);
    if (e != FRB_OK) {
        free(tmp);
        return e;
    }
    nav = img.bytes + info.nav_offset;
    nodes = img.bytes + info.nodes_offset;
    blocks = img.bytes + info.blocks_offset;

    /* The stored strings in order: each takes the next place of its class. */
    for (uint32_t s = 0; e == FRB_OK && s < info.stored; s++) {
        RevGraph g, twin, returned, mirrored;
        FrbTopology t, back;
        uint32_t colors = 0, back_colors = 0, v = s;
        uint64_t record;
        uint8_t least;
        int n, k;
        if ((s & (BLOCK - 1u)) == 0)
            for (int c = 1; c <= info.max_nodes; c++)
                put32(blocks + ((size_t)(s >> FRB_COMPACT_BLOCK_SHIFT) * (size_t)info.max_nodes + (size_t)(c - 1)) * 4u,
                      running[c]);
        e = table_rev(table, s, &g, &t, &colors);
        if (e != FRB_OK) break;
        n = t.n;
        nodes[s >> 1] |= (uint8_t)(n << (4u * (s & 1u)));
        if (n <= FRB_COMPACT_PLAIN_NODES) {
            record = plain_pack(&t, colors);
            back = t;
            back_colors = colors;
        } else {
            /* The graph in its canonical numbering, as its rank; and what a reader makes of that:
             * the colors its search finds and the numbering that goes with them.  Every node must
             * be reached: a node the string does not need means the REV is no minimum. */
            FrbTopology canonical;
            uint8_t order[FRB_TOPOLOGY_MAX_NODES];
            uint32_t found = 0;
            if (frb_topology_canonical(&t, &canonical, NULL) != n ||
                frb_topology_find_colors(&canonical, s, L, &found, order) != n) {
                e = FRB_ERR_EVIDENCE;
                break;
            }
            record = frb_topology_rank(&canonical);
            frb_topology_renumber(&canonical, found, order, &back, &back_colors);
        }
        record_put(img.bytes + info.class_offset[n], running[n]++, info.class_bits[n], record);
        /* Is that the REV of the table, and its complement the REV the table has for the complement? */
        frb_topology_to_graph(&back, back_colors, &returned);
        frb_graph_complement(&returned, &mirrored);
        others += !same_graph(&returned, &g);
        {
            FrbTopology unused;
            uint32_t unused_colors = 0;
            e = table_rev(table, s ^ mask, &twin, &unused, &unused_colors);
            if (e != FRB_OK) break;
        }
        others += !same_graph(&mirrored, &twin);
        /* the navigation bits of the strings this one closes */
        least = (uint8_t)n;
        for (k = top; k > 0 && (v & 1u); k--) {
            uint8_t zero = waiting[k];
            uint64_t place;
            v >>= 1;
            place = ((uint64_t)1 << (k - 1)) + v;
            if (least < zero) nav[place >> 3] |= (uint8_t)(1u << (place & 7u));
            else least = zero;
        }
        waiting[k] = least;
    }
    if (e == FRB_OK) {
        info.other_revs = others;
        info.index_crc = crc_of(nav, info.class_offset[1] - info.nav_offset);
        for (int k = 1; k <= FRB_COMPACT_MAX_NODES; k++)
            info.class_crc[k] = crc_of(img.bytes + info.class_offset[k], info.class_bytes[k]);
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
