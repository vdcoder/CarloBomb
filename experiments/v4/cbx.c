/*
 * CBX: a candidate layout for the compact Bomb (v4), as a file.  PROTOTYPE.
 * See cbx.h.  Writer, opener and the check against the source Bomb.
 */
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "cbx.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char cbx_magic[8] = {'C', 'B', 'X', '4', '\r', '\n', 0x1a, '\n'};

/* The header as it lies in the file: the structure of the machine that wrote
 * it.  A prototype; a format would be specified byte by byte. */
typedef struct {
    char magic[8];
    uint32_t layout, header_bytes;
    uint32_t bits, complement, colors, encoding, checkpoint_shift, max_nodes;
    uint32_t strings, stored;
    uint32_t class_count[CBX_CLASSES];
    uint8_t topo_bits[CBX_CLASSES], record_bits[CBX_CLASSES];
    uint8_t pad[2];
    uint64_t nav_offset, classes_offset, checkpoint_offset, pool_offset[CBX_CLASSES];
    uint64_t nav_bytes, classes_bytes, checkpoint_bytes, pool_bytes, file_bytes;
    uint32_t source_crc, source_version, body_crc, header_crc;
} Header;

_Static_assert(sizeof(Header) <= CBX_HEADER_BYTES, "the header has its 512 bytes");

const char *cbx_error_str(CbxError e) {
    switch (e) {
    case CBX_OK: return "ok";
    case CBX_ERR_ARG: return "invalid argument";
    case CBX_ERR_IO: return "cannot read or write the file";
    case CBX_ERR_MEMORY: return "out of memory";
    case CBX_ERR_EXISTS: return "the output file exists";
    case CBX_ERR_NOT_COMPLETE: return "the source is not a complete Bomb";
    case CBX_ERR_NODES: return "a REV has more nodes than this prototype takes";
    case CBX_ERR_NOT_SYMMETRIC: return "a string and its complement do not hold a REV and its complement";
    case CBX_ERR_NUMBERING: return "a REV does not come back from its canonical form by the numbering rule";
    case CBX_ERR_FORMAT: return "not a CBX file of this layout";
    case CBX_ERR_CHECKSUM: return "checksum mismatch";
    }
    return "?";
}

const char *cbx_encoding_str(CbxEncoding e) {
    return e == CBX_FIELDS ? "fields" : e == CBX_BASE_N ? "base-n" : e == CBX_RANK ? "rank" : "?";
}

static uint64_t align8(uint64_t v) { return (v + 7u) & ~(uint64_t)7; }

static unsigned topo_bits_of(unsigned n, CbxEncoding encoding) {
    unsigned bits = 0;
    uint64_t count;
    if (n <= CBX_SMALL || encoding == CBX_FIELDS) return 2 * n * cbx_link_bits(n);
    if (encoding == CBX_BASE_N) return n <= 9 ? cbx_bits_of_power(n, 2 * n) : 2 * cbx_base_n_half_bits(n);
    count = cl_canon_count((int)n);
    while (bits < 64 && ((uint64_t)1 << bits) < count) bits++;
    return bits;
}

static ClU128 pack_fields(const ClTopo *t) {
    ClU128 code = 0;
    unsigned w = cbx_link_bits(t->n);
    for (int i = t->n - 1; i >= 0; i--) {
        code = (code << w) | t->link[1][i];
        code = (code << w) | t->link[0][i];
    }
    return code;
}

static ClU128 pack_base_n(const ClTopo *t) {
    unsigned n = t->n;
    uint8_t d[2 * CBX_MAX_NODES];
    uint64_t lo = 0, hi = 0;
    for (unsigned i = 0; i < n; i++) {
        d[2 * i] = t->link[0][i];
        d[2 * i + 1] = t->link[1][i];
    }
    if (n <= 9) {
        for (int k = (int)(2 * n) - 1; k >= 0; k--) lo = lo * n + d[k];
        return lo;
    }
    for (int k = (int)n - 1; k >= 0; k--) {
        lo = lo * n + d[k];
        hi = hi * n + d[n + (unsigned)k];
    }
    return ((ClU128)hi << cbx_base_n_half_bits(n)) | lo;
}

static void put_record(uint8_t *pool, uint64_t index, unsigned width, ClU128 value) {
    uint64_t bit = index * (uint64_t)width;
    uint8_t *p = pool + (bit >> 3);
    ClU128 v;
    memcpy(&v, p, sizeof v);
    v |= value << (bit & 7u);
    memcpy(p, &v, sizeof v);
}

static int same_rev(const ClTopo *a, uint32_t ca, const ClTopo *b, uint32_t cb) { return cl_same_topo(a, b) && ca == cb; }

static CbxError stored_rev(const FrbTable *table, uint32_t t, ClTopo *topo, uint32_t *colors) {
    RevGraph g;
    FrbStatus status;
    if (frb_entry_decode_as(table->format, frb_table_entry(table, t), &status, &g) != FRB_OK || status != FRB_MINIMAL)
        return CBX_ERR_NOT_COMPLETE;
    if (g.node_count > CBX_MAX_NODES || !cl_from_graph(&g, topo, colors)) return CBX_ERR_NODES;
    return CBX_OK;
}

CbxError cbx_write(const char *path, const FrbTable *table, uint32_t source_crc, int source_version,
                   const CbxOptions *opt, CbxInfo *info_out) {
    Header h;
    CbxInfo info;
    uint32_t next[CBX_CLASSES] = {0}, running[CBX_CLASSES] = {0}, count, mask;
    uint8_t *image = NULL, *below = NULL, *pools[CBX_CLASSES] = {NULL};
    uint64_t *nav, *classes, at;
    uint32_t *checkpoint;
    char *tmp = NULL;
    FILE *f;
    size_t len;
    int L;
    CbxError e = CBX_OK;

    if (!path || !table || !table->entries || !opt || opt->checkpoint_shift < 4 || opt->checkpoint_shift > 12 ||
        (int)opt->encoding < 0 || (int)opt->encoding > (int)CBX_RANK)
        return CBX_ERR_ARG;
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return CBX_ERR_EXISTS;
    }
    L = table->bits;
    count = table->count;
    mask = count - 1u;
    if (L < 2) return CBX_ERR_ARG;

    memset(&info, 0, sizeof info);
    info.bits = L;
    info.opt = *opt;
    info.opt.complement = opt->complement != 0;
    info.opt.colors = opt->colors != 0;
    info.strings = count;
    info.stored = info.opt.complement ? count / 2 : count;
    info.source_crc = source_crc;
    info.source_version = source_version;

    /* the node count of every string: for A all of them, for B the stored ones */
    below = malloc(count);
    if (!below) return CBX_ERR_MEMORY;
    for (uint32_t t = 0;; t++) {
        int n = frb_table_nodes(table, t);
        if (n < 1 || n > CBX_MAX_NODES) {
            free(below);
            return n < 1 ? CBX_ERR_NOT_COMPLETE : CBX_ERR_NODES;
        }
        below[t] = (uint8_t)n;
        if (t < info.stored) {
            info.class_count[n]++;
            if (n > info.max_nodes) info.max_nodes = n;
        }
        if (t == mask) break;
    }
    for (unsigned n = 1; n < CBX_CLASSES; n++) {
        info.topo_bits[n] = (uint8_t)topo_bits_of(n, info.opt.encoding);
        info.record_bits[n] = (uint8_t)(info.topo_bits[n] + (info.opt.colors || n <= CBX_SMALL ? n : 0));
    }

    /* where everything goes */
    at = CBX_HEADER_BYTES;
    info.nav_offset = at; /* one bit for each stored string of L bits: as many shorter ones, and one unused */
    info.nav_bytes = align8(((uint64_t)info.stored + 7) / 8);
    at += info.nav_bytes;
    info.classes_offset = at;
    info.classes_bytes = (((uint64_t)info.stored + 15) / 16) * 8;
    at += info.classes_bytes;
    info.checkpoint_offset = at;
    info.checkpoint_bytes = align8((uint64_t)((info.stored >> info.opt.checkpoint_shift) + 1) *
                                   (uint64_t)info.max_nodes * sizeof(uint32_t));
    at += info.checkpoint_bytes;
    for (int n = 1; n <= info.max_nodes; n++) {
        uint64_t bytes = align8(((uint64_t)info.class_count[n] * info.record_bits[n] + 7) / 8);
        info.pool_offset[n] = at;
        info.pool_bytes += bytes;
        at += bytes;
    }
    info.file_bytes = at + 16; /* so that a record can always be read as two words */

    image = calloc((size_t)info.file_bytes, 1);
    if (!image) {
        free(below);
        return CBX_ERR_MEMORY;
    }
    nav = (uint64_t *)(void *)(image + info.nav_offset);
    classes = (uint64_t *)(void *)(image + info.classes_offset);
    checkpoint = (uint32_t *)(void *)(image + info.checkpoint_offset);
    for (int n = 1; n <= info.max_nodes; n++) pools[n] = image + info.pool_offset[n];

    /* B */
    for (uint32_t s = 0; s < info.stored; s++) {
        if ((s & (((uint32_t)1 << info.opt.checkpoint_shift) - 1u)) == 0)
            memcpy(checkpoint + (size_t)(s >> info.opt.checkpoint_shift) * (size_t)info.max_nodes, running + 1,
                   (size_t)info.max_nodes * sizeof(uint32_t));
        classes[s >> 4] |= (uint64_t)below[s] << (4 * (s & 15u));
        running[below[s]]++;
    }
    if ((info.stored & (((uint32_t)1 << info.opt.checkpoint_shift) - 1u)) == 0) /* the row after the last block */
        memcpy(checkpoint + (size_t)(info.stored >> info.opt.checkpoint_shift) * (size_t)info.max_nodes, running + 1,
               (size_t)info.max_nodes * sizeof(uint32_t));

    /* the REVs, by node count, in the order of their strings */
    for (uint32_t s = 0; e == CBX_OK && s < info.stored; s++) {
        ClTopo topo, canon, back;
        uint32_t colors = 0, kept = 0, back_colors = 0;
        unsigned n;
        ClU128 code;
        e = stored_rev(table, s, &topo, &colors);
        if (e != CBX_OK) break;
        n = topo.n;
        kept = colors;
        if (info.opt.complement) { /* the other string of the pair must hold the complement, exactly */
            ClTopo other, mirrored;
            uint32_t other_colors = 0;
            e = stored_rev(table, ~s & mask, &other, &other_colors);
            if (e != CBX_OK) break;
            cl_swap_ports(&topo, &mirrored);
            if (!same_rev(&mirrored, colors ^ ((1u << n) - 1u), &other, other_colors)) {
                e = CBX_ERR_NOT_SYMMETRIC;
                break;
            }
        }
        if (n <= CBX_SMALL || info.opt.encoding == CBX_FIELDS) {
            code = pack_fields(&topo);
        } else if (info.opt.encoding == CBX_BASE_N) {
            code = pack_base_n(&topo);
        } else {
            if (cl_canonical(&topo, colors, &canon, &kept, NULL) != (int)n) {
                e = CBX_ERR_NUMBERING; /* a node no link reaches: no canonical form */
                break;
            }
            /* with the colors kept the stored REV must come back by the rule */
            if (info.opt.colors &&
                (!cbx_verify_and_renumber(&canon, kept, s, L, &back, &back_colors) ||
                 !same_rev(&back, back_colors, &topo, colors))) {
                e = CBX_ERR_NUMBERING;
                break;
            }
            /* without them the reader numbers the nodes as its search reaches them: all must be reached */
            if (!info.opt.colors) {
                uint8_t perm[CL_MAX_NODES];
                uint32_t found = 0;
                if (!cl_recolor_order(&canon, s, L, (s >> (L - 1)) & 1u, &found, perm)) {
                    e = CBX_ERR_NUMBERING;
                    break;
                }
            }
            code = cl_canon_rank(&canon);
        }
        if (info.opt.colors || n <= CBX_SMALL) code = (code << n) | kept;
        put_record(pools[n], next[n]++, info.record_bits[n], code);
    }

    /* A: from the strings of L bits upwards, on the node counts alone.  The
     * bit of a string is 1 only where its extension by 1 needs fewer nodes
     * than its extension by 0.  With the complement only the strings that
     * start with 0 get a bit, at the place of the string without that 0; the
     * pairs were compared above. */
    for (int k = L - 1; e == CBX_OK && k >= 0; k--) {
        for (uint32_t v = 0; v < ((uint32_t)1 << k); v++) {
            uint8_t zero = below[2 * v], one = below[2 * v + 1];
            uint64_t place = 0;
            if (!info.opt.complement) place = ((uint64_t)1 << k) + v;
            else if (k >= 1 && v < ((uint32_t)1 << (k - 1))) place = ((uint64_t)1 << (k - 1)) + v;
            if (place && one < zero) nav[place >> 6] |= (uint64_t)1 << (place & 63u);
            below[v] = one < zero ? one : zero; /* in place: [v] is read before it is written only for 2v, 2v+1 >= v */
        }
    }
    free(below);
    if (e != CBX_OK) {
        free(image);
        return e;
    }

    memset(&h, 0, sizeof h);
    memcpy(h.magic, cbx_magic, sizeof h.magic);
    h.layout = CBX_LAYOUT;
    h.header_bytes = CBX_HEADER_BYTES;
    h.bits = (uint32_t)L;
    h.complement = (uint32_t)info.opt.complement;
    h.colors = (uint32_t)info.opt.colors;
    h.encoding = (uint32_t)info.opt.encoding;
    h.checkpoint_shift = (uint32_t)info.opt.checkpoint_shift;
    h.max_nodes = (uint32_t)info.max_nodes;
    h.strings = info.strings;
    h.stored = info.stored;
    memcpy(h.class_count, info.class_count, sizeof h.class_count);
    memcpy(h.topo_bits, info.topo_bits, sizeof h.topo_bits);
    memcpy(h.record_bits, info.record_bits, sizeof h.record_bits);
    h.nav_offset = info.nav_offset;
    h.classes_offset = info.classes_offset;
    h.checkpoint_offset = info.checkpoint_offset;
    memcpy(h.pool_offset, info.pool_offset, sizeof h.pool_offset);
    h.nav_bytes = info.nav_bytes;
    h.classes_bytes = info.classes_bytes;
    h.checkpoint_bytes = info.checkpoint_bytes;
    h.pool_bytes = info.pool_bytes;
    h.file_bytes = info.file_bytes;
    h.source_crc = source_crc;
    h.source_version = (uint32_t)source_version;
    {
        uint32_t crc = 0;
        for (uint64_t done = CBX_HEADER_BYTES; done < info.file_bytes;) {
            size_t now = info.file_bytes - done < (1u << 22) ? (size_t)(info.file_bytes - done) : (size_t)1 << 22;
            crc = frb_crc32(crc, image + done, now);
            done += now;
        }
        h.body_crc = info.body_crc = crc;
    }
    h.header_crc = frb_crc32(0, &h, sizeof h);
    memcpy(image, &h, sizeof h);

    len = strlen(path);
    tmp = malloc(len + 5);
    if (!tmp) {
        free(image);
        return CBX_ERR_MEMORY;
    }
    memcpy(tmp, path, len);
    memcpy(tmp + len, ".tmp", 5);
    f = fopen(tmp, "wb");
    if (!f) e = CBX_ERR_IO;
    for (uint64_t done = 0; e == CBX_OK && done < info.file_bytes;) {
        size_t now = info.file_bytes - done < (1u << 22) ? (size_t)(info.file_bytes - done) : (size_t)1 << 22;
        if (fwrite(image + done, 1, now, f) != now) e = CBX_ERR_IO;
        done += now;
    }
    if (f) {
        if (e == CBX_OK && (fflush(f) != 0 || fsync(fileno(f)) != 0)) e = CBX_ERR_IO;
        if (fclose(f) != 0 && e == CBX_OK) e = CBX_ERR_IO;
    }
    if (e == CBX_OK && rename(tmp, path) != 0) e = CBX_ERR_IO;
    if (e != CBX_OK) remove(tmp);
    free(tmp);
    free(image);
    if (e == CBX_OK && info_out) *info_out = info;
    return e;
}

CbxError cbx_open(const char *path, Cbx *c) {
    Header h;
    struct stat st;
    void *map;
    uint32_t crc;
    int fd;
    if (!path || !c) return CBX_ERR_ARG;
    memset(c, 0, sizeof *c);
    fd = open(path, O_RDONLY);
    if (fd < 0) return CBX_ERR_IO;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)CBX_HEADER_BYTES) {
        close(fd);
        return CBX_ERR_FORMAT;
    }
    map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return CBX_ERR_IO;
    memcpy(&h, map, sizeof h);
    crc = h.header_crc;
    h.header_crc = 0;
    if (memcmp(h.magic, cbx_magic, sizeof h.magic) != 0 || h.layout != CBX_LAYOUT ||
        h.header_bytes != CBX_HEADER_BYTES || frb_crc32(0, &h, sizeof h) != crc ||
        h.file_bytes != (uint64_t)st.st_size || h.bits < 2 || h.bits > 31 || h.max_nodes < 1 ||
        h.max_nodes > CBX_MAX_NODES || h.encoding > (uint32_t)CBX_RANK || h.checkpoint_shift < 4 ||
        h.checkpoint_shift > 12 || h.strings != (uint32_t)1 << h.bits ||
        h.stored != (h.complement ? h.strings / 2 : h.strings)) {
        munmap(map, (size_t)st.st_size);
        return CBX_ERR_FORMAT;
    }
    c->info.bits = (int)h.bits;
    c->info.max_nodes = (int)h.max_nodes;
    c->info.opt.complement = (int)h.complement;
    c->info.opt.colors = (int)h.colors;
    c->info.opt.encoding = (CbxEncoding)h.encoding;
    c->info.opt.checkpoint_shift = (int)h.checkpoint_shift;
    c->info.strings = h.strings;
    c->info.stored = h.stored;
    memcpy(c->info.class_count, h.class_count, sizeof h.class_count);
    memcpy(c->info.topo_bits, h.topo_bits, sizeof h.topo_bits);
    memcpy(c->info.record_bits, h.record_bits, sizeof h.record_bits);
    c->info.nav_offset = h.nav_offset;
    c->info.classes_offset = h.classes_offset;
    c->info.checkpoint_offset = h.checkpoint_offset;
    memcpy(c->info.pool_offset, h.pool_offset, sizeof h.pool_offset);
    c->info.nav_bytes = h.nav_bytes;
    c->info.classes_bytes = h.classes_bytes;
    c->info.checkpoint_bytes = h.checkpoint_bytes;
    c->info.pool_bytes = h.pool_bytes;
    c->info.file_bytes = h.file_bytes;
    c->info.source_crc = h.source_crc;
    c->info.source_version = (int)h.source_version;
    c->info.body_crc = h.body_crc;
    /* every section inside the file, in order, and what the counts need */
    {
        uint64_t at = CBX_HEADER_BYTES, sum = 0;
        int ok = h.nav_offset == at && h.nav_bytes == align8(((uint64_t)h.stored + 7) / 8);
        at += h.nav_bytes;
        ok = ok && h.classes_offset == at && h.classes_bytes == (((uint64_t)h.stored + 15) / 16) * 8;
        at += h.classes_bytes;
        ok = ok && h.checkpoint_offset == at &&
             h.checkpoint_bytes == align8((uint64_t)((h.stored >> h.checkpoint_shift) + 1) * h.max_nodes * sizeof(uint32_t));
        at += h.checkpoint_bytes;
        for (unsigned n = 1; ok && n <= h.max_nodes; n++) {
            unsigned topo = topo_bits_of(n, (CbxEncoding)h.encoding);
            ok = h.topo_bits[n] == topo && h.record_bits[n] == topo + (h.colors || n <= CBX_SMALL ? n : 0) &&
                 h.pool_offset[n] == at;
            at += align8(((uint64_t)h.class_count[n] * h.record_bits[n] + 7) / 8);
            sum += h.class_count[n];
        }
        if (!ok || sum != h.stored || at + 16 != h.file_bytes) {
            munmap(map, (size_t)st.st_size);
            memset(c, 0, sizeof *c);
            return CBX_ERR_FORMAT;
        }
    }
    (void)cl_canon_count(1); /* the tables of the rank are filled here, before there can be several readers */
    c->base = map;
    c->mapped = (size_t)st.st_size;
    c->nav = (const uint64_t *)(const void *)(c->base + h.nav_offset);
    c->classes = (const uint64_t *)(const void *)(c->base + h.classes_offset);
    c->checkpoint = (const uint32_t *)(const void *)(c->base + h.checkpoint_offset);
    for (unsigned n = 1; n <= h.max_nodes; n++) c->pool[n] = c->base + h.pool_offset[n];
    c->mask = h.strings - 1u;
    return CBX_OK;
}

void cbx_close(Cbx *c) {
    if (c && c->base) munmap((void *)(uintptr_t)c->base, c->mapped);
    if (c) memset(c, 0, sizeof *c);
}

CbxError cbx_check_body(const Cbx *c) {
    uint32_t crc = 0;
    if (!c || !c->base) return CBX_ERR_ARG;
    for (uint64_t done = CBX_HEADER_BYTES; done < c->info.file_bytes;) {
        size_t now = c->info.file_bytes - done < (1u << 22) ? (size_t)(c->info.file_bytes - done) : (size_t)1 << 22;
        crc = frb_crc32(crc, c->base + done, now);
        done += now;
    }
    return crc == c->info.body_crc ? CBX_OK : CBX_ERR_CHECKSUM;
}

CbxError cbx_check_against(const Cbx *c, const FrbTable *table, CbxCheck *check) {
    uint32_t waiting_lo[FRB_MAX_BITS + 1], waiting_hi[FRB_MAX_BITS + 1];
    int L, halved;
    if (!c || !c->base || !table || !check || table->bits != c->info.bits) return CBX_ERR_ARG;
    memset(check, 0, sizeof *check);
    L = c->info.bits;
    halved = c->info.opt.complement != 0;
    for (uint32_t t = 0;; t++) {
        ClTopo want, got;
        uint32_t want_colors = 0, colors = 0, leaf = 0, lo = t, hi = t, v = t;
        int bits;
        CbxError e = stored_rev(table, t, &want, &want_colors);
        if (e != CBX_OK) return e;
        check->strings++;
        if (cbx_nodes(c, L, t) != want.n) check->wrong_nodes++;
        if (!cbx_lookup(c, L, t, &got, &colors, &leaf) || leaf != t || !cl_verify(&got, colors, t, L)) {
            check->not_emitting++;
        } else {
            if (got.n != want.n) check->wrong_nodes++;
            if (!same_rev(&got, colors, &want, want_colors)) {
                check->not_stored++;
                check->not_stored_by_nodes[want.n]++;
                if (got.n == want.n && L < 32 &&
                    (cl_output(&got, colors, L + 1, NULL) & 1u) != (cl_output(&want, want_colors, L + 1, NULL) & 1u))
                    check->next_bit_differs++;
            }
        }
        /* Shorter strings, worked out from the table alone.  A string ending
         * in 1 closes the strings it is the last extension of.  For each of
         * them: lo and hi, the lowest and the highest of its extensions with
         * the fewest nodes, from those of its two halves. */
        for (bits = L; bits > 1 && (v & 1u); bits--) {
            uint32_t zlo = waiting_lo[bits], zhi = waiting_hi[bits], answer;
            int n0 = frb_table_nodes(table, zlo), n1 = frb_table_nodes(table, lo), least = n0 < n1 ? n0 : n1;
            if (n0 < n1) lo = zlo, hi = zhi;
            else if (n0 == n1) lo = zlo; /* hi stays: the highest under the extension by 1 */
            v >>= 1;                     /* the string of bits-1 bits */
            check->shorter++;
            answer = cbx_leaf(c, bits - 1, v);
            /* what has to hold: an extension of the string, with the fewest nodes */
            if ((answer >> (L - (bits - 1))) != v || frb_table_nodes(table, answer) != least ||
                cbx_nodes(c, bits - 1, v) != least)
                check->not_minimal++;
            /* and which one: the lowest; in a halved file the highest for a string that starts with 1 */
            if (answer != (halved && ((v >> (bits - 2)) & 1u) ? hi : lo)) check->wrong_extension++;
            if (cbx_compare(c, bits - 1, v, NULL) != (n0 < n1 ? 0 : n1 < n0 ? 1 : CBX_TIE)) check->wrong_comparison++;
        }
        if (t == c->mask) { /* the last string closes "1"; "0" is waiting: the two extensions of the empty string */
            int n0 = frb_table_nodes(table, waiting_lo[1]), n1 = frb_table_nodes(table, lo);
            uint32_t answer = cbx_leaf(c, 0, 0);
            if (frb_table_nodes(table, answer) != (n0 < n1 ? n0 : n1)) check->not_minimal++;
            if (answer != (n1 < n0 ? lo : waiting_lo[1])) check->wrong_extension++;
            if (cbx_compare(c, 0, 0, NULL) != (n0 < n1 ? 0 : n1 < n0 ? 1 : CBX_TIE)) check->wrong_comparison++;
            break;
        }
        waiting_lo[bits] = lo;
        waiting_hi[bits] = hi;
    }
    return CBX_OK;
}
