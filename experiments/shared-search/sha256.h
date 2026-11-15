/*
 * SHA-256 (FIPS 180-4), for the digests of the shared-search experiment.
 * One header, no dependencies.  sha256_selftest() checks the standard's own
 * examples.
 */
#ifndef CARLOBOMB_EXPERIMENT_SHA256_H
#define CARLOBOMB_EXPERIMENT_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Sha256;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t sha256_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void sha256_compress(Sha256 *s, const uint8_t *p) {
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha256_init(Sha256 *s) {
    static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, h0, sizeof h0);
    s->bytes = 0;
    s->used = 0;
}

static void sha256_update(Sha256 *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->bytes += n;
    while (n) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->block + s->used, p, take);
        s->used += take, p += take, n -= take;
        if (s->used == 64) {
            sha256_compress(s, s->block);
            s->used = 0;
        }
    }
}

/* out: 64 hexadecimal digits and a NUL. */
static void sha256_hex(Sha256 *s, char out[65]) {
    uint64_t bits = s->bytes * 8u;
    uint8_t pad[72] = {0x80};
    size_t n = (s->used < 56 ? 56 : 120) - s->used;
    for (int i = 0; i < 8; i++) pad[n + (size_t)i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, pad, n + 8);
    for (int i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", (unsigned)s->h[i]);
}

/* 1 if the examples of the standard come out right. */
static int sha256_selftest(void) {
    static const char *const abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    static const char *const empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    static const char *const two = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    static const char *const million = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    Sha256 s;
    char hex[65], a[1000];
    sha256_init(&s);
    sha256_update(&s, "abc", 3);
    sha256_hex(&s, hex);
    if (strcmp(hex, abc) != 0) return 0;
    sha256_init(&s);
    sha256_hex(&s, hex);
    if (strcmp(hex, empty) != 0) return 0;
    sha256_init(&s);
    sha256_update(&s, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
    sha256_hex(&s, hex);
    if (strcmp(hex, two) != 0) return 0;
    memset(a, 'a', sizeof a);
    sha256_init(&s);
    for (int i = 0; i < 1000; i++) sha256_update(&s, a, sizeof a);
    sha256_hex(&s, hex);
    return strcmp(hex, million) == 0;
}

#endif
