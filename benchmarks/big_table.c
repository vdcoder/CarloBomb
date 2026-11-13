/*
 * big_table: a table of full size without the search, to check that files of
 * many gigabytes are written, addressed and read correctly.
 *
 *   big_table --bits L --out FILE [--keep]
 *
 * Writes a partial table for L bits in which a few entries hold the ring of
 * their string and all others are empty: the first and last entries, the
 * middle, and the entries on either side of every power-of-two byte offset
 * from 2^31 up, which is where a 32-bit position would go wrong.  Then reads
 * it back every way the library can: the header, single entries by position,
 * and the whole table through a mapping with every entry decoded and every
 * ring run.  Needs the disk space of the table (44 bytes per string from 25
 * bits) and next to no memory.  The file is removed unless --keep is given.
 *
 * A table made here certifies nothing and is marked as what it is: every
 * entry is EMPTY or WITNESS, none MINIMAL, and the complete flag is not set.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_run.h"

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int fail(const char *what, FrbError e) {
    fprintf(stderr, "big_table: %s: %s\n", what, frb_error_str(e));
    return 1;
}

int main(int argc, char **argv) {
    const char *out = NULL;
    const FrbFormat *format;
    uint32_t marked[64], count;
    size_t n = 0, at = 0;
    FrbWriter *writer = NULL;
    FrbSummary summary;
    FrbFileInfo info;
    FrbTable table;
    RevGraph g;
    FrbStatus status;
    uint64_t bytes;
    uint32_t crc = 0, bad = 0;
    int bits = 0, keep = 0;
    double t0;
    FrbError e;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--keep")) keep = 1;
        else if (!strcmp(argv[i], "--bits") && i + 1 < argc) bits = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else bits = 0;
    }
    format = frb_format_for_bits(bits);
    if (!format || !out) {
        fprintf(stderr, "usage: big_table --bits L --out FILE [--keep]   (L = 1..%d)\n", FRB_MAX_BITS);
        return 2;
    }
    count = (uint32_t)1 << bits;
    bytes = frb_file_bytes(format, bits);

    /* The entries that get a ring: the ends, the middle, and both sides of
     * every power-of-two offset from 2^31 that lies inside the file. */
    marked[n++] = 0;
    if (count > 1) marked[n++] = 1;
    marked[n++] = count / 2;
    if (count > 2) marked[n++] = count - 2;
    marked[n++] = count - 1;
    for (int p = 31; p < 40 && ((uint64_t)1 << p) < bytes; p++) {
        uint32_t i = (uint32_t)((((uint64_t)1 << p) - FRB_HEADER_BYTES) / (uint64_t)format->entry_bytes);
        if (i > 0 && n < 60) marked[n++] = i - 1; /* ends below the boundary */
        if (n < 60) marked[n++] = i;              /* straddles it, or starts on it */
        if (i + 1 < count && n < 60) marked[n++] = i + 1;
    }
    qsort(marked, n, sizeof marked[0], cmp_u32);
    {
        size_t unique = 0;
        for (size_t k = 0; k < n; k++)
            if (unique == 0 || marked[unique - 1] != marked[k]) marked[unique++] = marked[k];
        n = unique;
    }

    printf("FixedRunBomb(%d), format version %d: %u entries of %d bytes, %llu bytes\n", bits, format->version,
           (unsigned)count, format->entry_bytes, (unsigned long long)bytes);
    t0 = cb_now();
    e = frb_writer_open(out, bits, format, &writer);
    if (e != FRB_OK) return fail("cannot create the file", e);
    for (uint32_t i = 0;; i++) {
        if (at < n && marked[at] == i) {
            frb_ring_witness(i, bits, &g);
            e = frb_writer_put(writer, FRB_WITNESS, &g);
            at++;
        } else {
            e = frb_writer_put(writer, FRB_EMPTY, NULL);
        }
        if (e != FRB_OK) {
            frb_writer_abort(writer);
            return fail("writing", e);
        }
        if (i == count - 1) break;
    }
    e = frb_writer_finish(writer, &summary, &crc);
    if (e != FRB_OK) return fail("finishing the file", e);
    printf("written entry by entry in %.1f s: %u rings among %u empty entries, table CRC-32 %08x\n", cb_now() - t0,
           (unsigned)summary.by_status[FRB_WITNESS], (unsigned)summary.by_status[FRB_EMPTY], (unsigned)crc);
    if (summary.by_status[FRB_WITNESS] != n || summary.by_status[FRB_EMPTY] != count - n || summary.complete)
        return fail("the summary is wrong", FRB_ERR_SUMMARY);

    /* The header alone. */
    e = frb_file_info(out, &info);
    if (e != FRB_OK) return fail("header", e);
    if (info.bits != bits || info.count != count || info.table_crc != crc || info.complete ||
        info.witnesses != n || info.minimal != 0 || info.max_witness_nodes != bits)
        return fail("the header does not say what was written", FRB_ERR_HEADER);

    /* Single entries by position: each ring where it was put, its neighbours empty. */
    t0 = cb_now();
    for (size_t k = 0; k < n; k++) {
        for (int d = -1; d <= 1; d++) {
            uint32_t i = marked[k] + (uint32_t)d;
            int is_ring = 0;
            if ((d < 0 && marked[k] == 0) || (d > 0 && marked[k] == count - 1)) continue;
            for (size_t j = 0; j < n; j++) is_ring |= marked[j] == i;
            e = frb_file_lookup(out, i, NULL, &status, &g);
            if (e != FRB_OK) return fail("lookup", e);
            if (is_ring ? (status != FRB_WITNESS || g.node_count != bits || frb_graph_output(&g, bits) != i)
                        : (status != FRB_EMPTY || g.node_count != 0)) {
                fprintf(stderr, "big_table: entry %u at byte %llu is not what was written\n", (unsigned)i,
                        (unsigned long long)frb_entry_offset(format, i));
                return 1;
            }
        }
    }
    printf("%zu rings and their neighbours read back by position in %.3f s; the last at byte %llu\n", n,
           cb_now() - t0, (unsigned long long)frb_entry_offset(format, marked[n - 1]));

    /* The whole table: length, both checksums, every entry decoded, every ring run. */
    t0 = cb_now();
    e = frb_file_read(out, &table, &info, &bad);
    if (e != FRB_OK) {
        fprintf(stderr, "big_table: first bad entry %u\n", (unsigned)bad);
        return fail("reading the whole table", e);
    }
    for (size_t k = 0; k < n; k++)
        if (frb_table_nodes(&table, marked[k]) != bits) return fail("a ring is missing", FRB_ERR_ENTRY);
    printf("whole table read and validated in %.1f s (%s)\n", cb_now() - t0,
           table.mapping ? "through a mapping of the file" : "into memory");
    frb_table_destroy(&table);

    if (!keep) remove(out);
    printf("PASS\n");
    return 0;
}
