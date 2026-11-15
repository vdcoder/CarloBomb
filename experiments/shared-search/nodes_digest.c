/*
 * nodes_digest: what a Bomb says every string needs, as a histogram and as
 * one number that can be compared without moving the Bomb.
 *
 *   nodes_digest BOMB [--bits L]
 *
 * BOMB is a FixedRunBomb of either format.  It is opened as any reader opens
 * it, with every check that makes.  For each of the 2^L strings of L bits (L
 * is the Bomb's own length unless --bits says a shorter one), in the order
 * of their indices (the string's bits as a binary number, first bit most
 * significant), the certified minimum number of nodes is written as one
 * byte.  The digest is the SHA-256 of those 2^L bytes.
 *
 * shared_search prints the same digest for what it found.  Two digests that
 * are equal say that the two agree on the node count of every single
 * string; equal histograms say only that the counts add up alike.
 *
 * Reads the Bomb and writes nothing.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixed_bomb_solver.h"
#include "fixed_run.h"
#include "sha256.h"

static const char *commas(uint64_t v) {
    static char bufs[16][32];
    static int next;
    char digits[24], *out = bufs[next];
    int n, k = 0;
    next = (next + 1) % 16;
    n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    for (int i = 0; i < n; i++) {
        out[k++] = digits[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[k++] = ',';
    }
    out[k] = '\0';
    return out;
}

int main(int argc, char **argv) {
    enum { CHUNK = 1 << 16 };
    static uint8_t buf[CHUNK];
    const char *path = NULL;
    RevFixedBomb *bomb = NULL;
    uint64_t hist[FRB_MAX_NODES + 1] = {0}, total, unknown = 0;
    uint32_t bad = 0;
    int bits = 0, of_bomb;
    size_t used = 0;
    Sha256 sha;
    char hex[65];
    FrbError e;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bits") && i + 1 < argc) bits = atoi(argv[++i]);
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else path = NULL, i = argc;
    }
    if (!path) return fprintf(stderr, "usage: nodes_digest BOMB [--bits L]\n"), 2;
    if (!sha256_selftest()) return fprintf(stderr, "nodes_digest: SHA-256 fails the examples of its standard\n"), 1;
    e = rev_fixed_bomb_open(path, &bomb, &bad);
    if (e != FRB_OK) return fprintf(stderr, "nodes_digest: %s: %s\n", path, frb_error_str(e)), 1;
    of_bomb = rev_fixed_bomb_bits(bomb);
    if (!bits) bits = of_bomb;
    if (bits < 1 || bits > of_bomb) return fprintf(stderr, "nodes_digest: a Bomb of %d bits answers 1 to %d bits\n", of_bomb, of_bomb), 2;

    total = (uint64_t)1 << bits;
    sha256_init(&sha);
    for (uint64_t i = 0; i < total; i++) {
        int n = rev_fixed_bomb_min_nodes(bomb, bits, (uint32_t)i);
        if (n < 1 || n > FRB_MAX_NODES) unknown++, n = 0;
        hist[n]++;
        buf[used++] = (uint8_t)n;
        if (used == CHUNK) {
            sha256_update(&sha, buf, used);
            used = 0;
        }
    }
    sha256_update(&sha, buf, used);
    sha256_hex(&sha, hex);
    rev_fixed_bomb_close(bomb);

    printf("%s: a Bomb of %d bits; strings of %d bits: %s\n", path, of_bomb, bits, commas(total));
    printf("  strings by nodes:");
    for (int n = 1; n <= FRB_MAX_NODES; n++)
        if (hist[n]) printf("  %d: %s", n, commas(hist[n]));
    printf("\n");
    if (unknown) return printf("  %s strings without a certified minimum: no digest\n", commas(unknown)), 1;
    printf("  node-count digest (SHA-256 of %s bytes, the nodes of each string in index order):\n    %s\n", commas(total), hex);
    return 0;
}
