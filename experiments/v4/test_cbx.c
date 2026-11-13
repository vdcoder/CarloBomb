/*
 * Tests of the prototype container (cbx.h): every combination of the three
 * switches on small Bombs made the way the real ones are made, a table of
 * 12-node REVs for the wide records, and what the writer and the opener
 * refuse.
 *
 * Usage: test_cbx [prefix-for-scratch-files]
 */
#include <stdio.h>
#include <stdlib.h>

#include "cbx.h"
#include "minimal_solver.h"
#include "test_util.h"

static char scratch[1024] = "cbx_test";

static const char *file(const char *name) {
    static char bufs[4][1200];
    static int next;
    char *buf = bufs[next];
    next = (next + 1) % 4;
    snprintf(buf, sizeof bufs[0], "%s_%s", scratch, name);
    return buf;
}

static int exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/*
 * A complete table as the construction makes it: the strings that start
 * with 0 solved by the search (white tried first, nodes numbered as the
 * machine reaches them), the others their complements.
 */
static void solved_table(FrbTable *t, int bits, const FrbFormat *format) {
    char text[FRB_MAX_BITS + 1];
    CHECK(frb_table_create_as(t, bits, format) == FRB_OK);
    for (uint32_t i = 0; i < t->count / 2; i++) {
        RevGraph g, c;
        frb_target_format(i, bits, text);
        CHECK(rev_minimal_solve(text, (size_t)bits, 0, NULL, &g, NULL) == REV_MIN_MINIMAL);
        frb_graph_complement(&g, &c);
        CHECK(frb_table_put(t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(t, i ^ (t->count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
}

/* Fewest nodes among the L-bit extensions of a string, by scanning them. */
static int scan_nodes(const FrbTable *t, int bits, uint32_t value) {
    int shift = t->bits - bits, best = 99;
    for (uint32_t i = value << shift; i < (value + 1u) << shift; i++)
        if (frb_table_nodes(t, i) < best) best = frb_table_nodes(t, i);
    return best;
}

/* The lowest, or the highest, of the L-bit extensions of a string that need the fewest nodes. */
static uint32_t scan_extension(const FrbTable *t, int bits, uint32_t value, int highest) {
    int shift = t->bits - bits, best = scan_nodes(t, bits, value);
    uint32_t found = 0;
    for (uint32_t i = value << shift; i < (value + 1u) << shift; i++)
        if (frb_table_nodes(t, i) == best) {
            found = i;
            if (!highest) break;
        }
    return found;
}

static const CbxEncoding encodings[3] = {CBX_FIELDS, CBX_BASE_N, CBX_RANK};

static void test_variants(void) {
    unsigned long long files = 0, strings = 0;
    for (int bits = 2; bits <= 10; bits++) {
        FrbTable t;
        solved_table(&t, bits, frb_format(bits % 2 ? 2 : 1));
        for (int e = 0; e < 3; e++) {
            for (int colors = 0; colors < 2; colors++) {
                for (int half = 0; half < 2; half++) {
                    char path[1200];
                    CbxOptions opt = {half, colors, encodings[e], 4 + (bits + e) % 6};
                    CbxInfo info;
                    CbxCheck check;
                    Cbx c;
                    CbxError err;
                    snprintf(path, sizeof path, "%s", file("v.cbx"));
                    remove(path);
                    err = cbx_write(path, &t, 0xABCD0123u, t.format->version, &opt, &info);
                    CHECK_MSG(err == CBX_OK, "%d bits, %s: %s", bits, cbx_encoding_str(encodings[e]), cbx_error_str(err));
                    if (err != CBX_OK) continue;
                    CHECK(!exists(file("v.cbx.tmp")));
                    CHECK(cbx_open(path, &c) == CBX_OK && cbx_check_body(&c) == CBX_OK);
                    CHECK(c.info.bits == bits && c.info.opt.complement == half && c.info.opt.colors == colors &&
                          c.info.opt.encoding == encodings[e] && c.info.source_crc == 0xABCD0123u &&
                          c.info.file_bytes == info.file_bytes && c.info.stored == (half ? t.count / 2 : t.count));
                    CHECK(cbx_check_against(&c, &t, &check) == CBX_OK);
                    CHECK(check.strings == t.count && check.shorter == t.count - 2);
                    CHECK_MSG(check.wrong_nodes == 0 && check.not_emitting == 0 && check.not_minimal == 0 &&
                                  check.wrong_extension == 0 && check.wrong_comparison == 0,
                              "%d bits %s colors %d half %d: %llu wrong nodes, %llu not emitting, %llu not minimal, "
                              "%llu wrong extensions, %llu wrong comparisons",
                              bits, cbx_encoding_str(encodings[e]), colors, half,
                              (unsigned long long)check.wrong_nodes, (unsigned long long)check.not_emitting,
                              (unsigned long long)check.not_minimal, (unsigned long long)check.wrong_extension,
                              (unsigned long long)check.wrong_comparison);
                    /* With the colors kept the stored REV comes back.  Without them too, here:
                     * every REV of this table was found by the search or is the complement of
                     * one, and the reader guesses in the order the search did. */
                    CHECK_MSG(check.not_stored == 0, "%d bits %s colors %d half %d: %llu REVs are not the stored ones",
                              bits, cbx_encoding_str(encodings[e]), colors, half, (unsigned long long)check.not_stored);
                    /* Shorter strings against a scan of the table: the comparison on demand, the
                     * node count, and where the navigation bits lead.  That is the lowest of the
                     * extensions with the fewest nodes; in a halved file, for a string that starts
                     * with 1, the highest: complement in, complement out, for the REV as well. */
                    for (int k = 0; k < bits; k++) {
                        for (uint32_t v = 0; v < (1u << k); v++) {
                            int nodes[2], which = cbx_compare(&c, k, v, nodes);
                            int n0 = scan_nodes(&t, k + 1, 2 * v), n1 = scan_nodes(&t, k + 1, 2 * v + 1);
                            CHECK(nodes[0] == n0 && nodes[1] == n1 && which == (n0 < n1 ? 0 : n1 < n0 ? 1 : CBX_TIE));
                            CHECK(cbx_compare(&c, k, v, NULL) == which);
                            if (k) {
                                uint32_t leaf = cbx_leaf(&c, k, v), other = v ^ ((1u << k) - 1u);
                                CHECK(cbx_nodes(&c, k, v) == (n0 < n1 ? n0 : n1));
                                CHECK(frb_table_nodes(&t, leaf) == (n0 < n1 ? n0 : n1) && (leaf >> (bits - k)) == v);
                                CHECK(leaf == scan_extension(&t, k, v, half && ((v >> (k - 1)) & 1u)));
                                if (half) {
                                    ClTopo a, b, mirrored;
                                    uint32_t ca = 0, cb = 0;
                                    CHECK(cbx_leaf(&c, k, other) == (leaf ^ (t.count - 1u)));
                                    CHECK(cbx_lookup(&c, k, v, &a, &ca, NULL) && cbx_lookup(&c, k, other, &b, &cb, NULL));
                                    cl_swap_ports(&a, &mirrored);
                                    CHECK(cl_same_topo(&mirrored, &b) && cb == (ca ^ ((1u << a.n) - 1u)));
                                }
                            }
                        }
                    }
                    CHECK(cbx_leaf(&c, 0, 0) == scan_extension(&t, 0, 0, 0));
                    CHECK(c.info.nav_bytes == ((uint64_t)c.info.stored + 63) / 64 * 8); /* one bit a stored string */
                    cbx_close(&c);
                    remove(path);
                    files++;
                    strings += t.count;
                }
            }
        }
        frb_table_destroy(&t);
    }
    printf("variants: %llu files, the 12 combinations for tables of 2 to 10 bits; %llu strings each given its node\n"
           "          count and its stored REV, every shorter string its extension and its comparison\n",
           files, strings);
}

/* REVs of 12 nodes: records wider than a word. */
static void test_wide(void) {
    const int bits = 12;
    FrbTable t;
    int made = 0, refused = 0;
    CHECK(frb_table_create_as(&t, bits, frb_format(2)) == FRB_OK);
    for (uint32_t i = 0; i < t.count / 2; i++) { /* the ring of the string, and its complement */
        RevGraph g, c;
        frb_ring_witness(i, bits, &g);
        frb_graph_complement(&g, &c);
        CHECK(g.node_count == bits);
        CHECK(frb_table_put(&t, i, FRB_MINIMAL, &g) == FRB_OK);
        CHECK(frb_table_put(&t, i ^ (t.count - 1u), FRB_MINIMAL, &c) == FRB_OK);
    }
    for (int e = 0; e < 3; e++) {
        for (int colors = 0; colors < 2; colors++) {
            for (int half = 0; half < 2; half++) {
                char path[1200];
                CbxOptions opt = {half, colors, encodings[e], 6};
                CbxCheck check;
                Cbx c;
                CbxError err;
                snprintf(path, sizeof path, "%s", file("w.cbx"));
                remove(path);
                err = cbx_write(path, &t, 0, 2, &opt, NULL);
                if (err != CBX_OK) { /* a ring need not be numbered as the search numbers */
                    CHECK(encodings[e] == CBX_RANK && err == CBX_ERR_NUMBERING && !exists(path));
                    refused++;
                    continue;
                }
                CHECK(cbx_open(path, &c) == CBX_OK && cbx_check_body(&c) == CBX_OK);
                CHECK(c.info.max_nodes == 12);
                if (encodings[e] != CBX_RANK || colors) CHECK(c.info.record_bits[12] > 64); /* two words */
                CHECK(cbx_check_against(&c, &t, &check) == CBX_OK);
                CHECK(check.wrong_nodes == 0 && check.not_emitting == 0 && check.not_minimal == 0 &&
                      check.wrong_extension == 0 && check.wrong_comparison == 0);
                if (colors) CHECK(check.not_stored == 0);
                cbx_close(&c);
                remove(path);
                made++;
            }
        }
    }
    CHECK(made >= 8);
    frb_table_destroy(&t);
    printf("wide: a table of 12-node REVs in %d combinations (%d refused for their numbering), records of up to 108 bits\n",
           made, refused);
}

static void test_refusals(void) {
    char path[1200];
    FrbTable t, odd;
    CbxOptions opt = {0, 1, CBX_FIELDS, 8};
    CbxOptions half = {1, 1, CBX_FIELDS, 8};
    Cbx c;
    RevGraph ring, g;
    FrbStatus status;
    FILE *f;
    long size;
    uint8_t *image;
    snprintf(path, sizeof path, "%s", file("r.cbx"));
    remove(path);
    solved_table(&t, 8, frb_format(1));

    /* a pair that is not a REV and its complement: no halving */
    solved_table(&odd, 8, frb_format(1));
    CHECK(frb_table_get(&odd, 200, &status, &g) == FRB_OK);
    frb_ring_witness(200, 8, &ring);
    CHECK(frb_table_put(&odd, 200, FRB_MINIMAL, &ring) == FRB_OK);
    CHECK(cbx_write(path, &odd, 0, 1, &half, NULL) == CBX_ERR_NOT_SYMMETRIC && !exists(path));
    /* not complete */
    CHECK(frb_table_put(&odd, 200, FRB_WITNESS, &ring) == FRB_OK);
    CHECK(cbx_write(path, &odd, 0, 1, &opt, NULL) == CBX_ERR_NOT_COMPLETE && !exists(path));
    frb_table_destroy(&odd);
    /* arguments */
    opt.checkpoint_shift = 3;
    CHECK(cbx_write(path, &t, 0, 1, &opt, NULL) == CBX_ERR_ARG);
    opt.checkpoint_shift = 8;
    CHECK(cbx_write(path, NULL, 0, 1, &opt, NULL) == CBX_ERR_ARG);

    /* a file is never replaced */
    CHECK(cbx_write(path, &t, 0, 1, &opt, NULL) == CBX_OK);
    CHECK(cbx_write(path, &t, 0, 1, &opt, NULL) == CBX_ERR_EXISTS);
    CHECK(cbx_open(path, &c) == CBX_OK && cbx_check_body(&c) == CBX_OK);
    cbx_close(&c);

    /* damage: in the header the opener sees it, in the body the checksum does */
    f = fopen(path, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    image = malloc((size_t)size);
    CHECK(fread(image, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    for (long at = 0; at < size; at += at < 320 ? 1 : 97) {
        CbxError e;
        image[at] ^= 0x10;
        f = fopen(file("d.cbx"), "wb");
        CHECK(f && fwrite(image, 1, (size_t)size, f) == (size_t)size && fclose(f) == 0);
        e = cbx_open(file("d.cbx"), &c);
        if (e == CBX_OK) {
            CHECK(at >= CBX_HEADER_BYTES || at >= 312); /* past the header's fields: its unused bytes, or the body */
            if (at >= CBX_HEADER_BYTES) CHECK(cbx_check_body(&c) == CBX_ERR_CHECKSUM);
            cbx_close(&c);
        } else {
            CHECK(e == CBX_ERR_FORMAT && at < CBX_HEADER_BYTES);
        }
        image[at] ^= 0x10;
    }
    /* cut short, and not a CBX file at all */
    f = fopen(file("d.cbx"), "wb");
    CHECK(f && fwrite(image, 1, (size_t)size - 8, f) == (size_t)size - 8 && fclose(f) == 0);
    CHECK(cbx_open(file("d.cbx"), &c) == CBX_ERR_FORMAT);
    CHECK(frb_file_write(file("d.cbx"), &t) == FRB_OK);
    CHECK(cbx_open(file("d.cbx"), &c) == CBX_ERR_FORMAT);
    /* and a CBX file is not a Bomb to the readers of the formats */
    CHECK(frb_file_info(path, NULL) == FRB_ERR_MAGIC);
    remove(file("d.cbx"));
    remove(path);
    free(image);
    frb_table_destroy(&t);
    printf("refusals: a pair that is not a REV and its complement, a partial table, an existing file, a changed\n"
           "          byte anywhere, a short file, another kind of file\n");
}

int main(int argc, char **argv) {
    if (argc > 1) snprintf(scratch, sizeof scratch, "%s", argv[1]);
    test_variants();
    test_wide();
    test_refusals();
    return t_report("test_cbx");
}
