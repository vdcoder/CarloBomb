/*
 * colorless_cold: lookups with nothing in memory.
 *
 *   colorless_cold --fixed FILE --compact FILE [--reads N]
 *
 * FILE of --fixed is a complete fixed-width Bomb and FILE of --compact the
 * compact file made from it (`carlobomb bomb compact`).  Both are opened for
 * reading only.  Before each measurement the operating system is asked to
 * drop the file's pages from its cache (posix_fadvise DONTNEED), so that
 * every read goes to the disk; then N random strings of L bits are looked
 * up, each timed by itself.
 *
 *   fixed-width   the entry of the string: one read
 *   compact       the index entry of the string, then its REV: two reads
 *   one REV       one read of a REV at a random place in the REVs of the
 *                 compact file: what a layout costs whose index is small
 *                 enough to be held in memory (A+B: 13 MB at 24 bits)
 *
 * and how long it takes to read an index into memory from a cold file: the
 * first 13 MB, and the 101 MB of the compact file's ordinals.
 *
 * What is measured is this machine's disk more than any format; the point
 * is the number of reads.
 */
#include "timer.h" /* first: may need feature-test macros set by the build */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compact.h"
#include "fixed_run.h"

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rnd64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state * 0x2545F4914F6CDD1Dull;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Ask for the file's pages to be dropped; 0 if the system refuses. */
static int evict(const char *path) {
    int fd = open(path, O_RDONLY), ok;
    if (fd < 0) return 0;
    ok = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

static void report(const char *what, double *us, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += us[i];
    qsort(us, n, sizeof *us, cmp_double);
    printf("  %-34s %9.1f %9.1f %9.1f %9.1f %10.1f\n", what, sum / (double)n, us[n / 2],
           us[(size_t)((double)(n - 1) * 0.95)], us[(size_t)((double)(n - 1) * 0.99)], us[n - 1]);
}

static double read_bytes(const char *path, uint64_t bytes) {
    static char buf[1 << 20];
    int fd = open(path, O_RDONLY);
    double t0 = cb_now();
    if (fd < 0) return -1.0;
    for (uint64_t done = 0; done < bytes;) {
        size_t want = bytes - done < sizeof buf ? (size_t)(bytes - done) : sizeof buf;
        ssize_t got = read(fd, buf, want);
        if (got <= 0) break;
        done += (uint64_t)got;
    }
    close(fd);
    return cb_now() - t0;
}

int main(int argc, char **argv) {
    const char *fixed = NULL, *compact = NULL;
    FrbCompactInfo cinfo;
    FrbFileInfo finfo;
    size_t reads = 2000;
    uint32_t *strings;
    uint64_t *places;
    double *us;
    int L, fd;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fixed") && i + 1 < argc) fixed = argv[++i];
        else if (!strcmp(argv[i], "--compact") && i + 1 < argc) compact = argv[++i];
        else if (!strcmp(argv[i], "--reads") && i + 1 < argc) reads = (size_t)strtoull(argv[++i], NULL, 10);
        else fixed = NULL, i = argc;
    }
    if (!fixed || !compact || reads < 10) {
        fprintf(stderr, "usage: colorless_cold --fixed FILE --compact FILE [--reads N]\n");
        return 2;
    }
    if (frb_file_info(fixed, &finfo) != FRB_OK || frb_compact_file_info(compact, &cinfo) != FRB_OK ||
        finfo.bits != cinfo.bits) {
        fprintf(stderr, "colorless_cold: need a fixed-width Bomb and its compact file\n");
        return 1;
    }
    L = finfo.bits;
    strings = malloc(reads * sizeof *strings);
    places = malloc(reads * sizeof *places);
    us = malloc(reads * sizeof *us);
    if (!strings || !places || !us) return 1;
    for (size_t i = 0; i < reads; i++) {
        strings[i] = (uint32_t)(rnd64() >> 33) & (cinfo.count - 1u);
        places[i] = cinfo.pool_offset + (rnd64() >> 11) % (cinfo.file_bytes - cinfo.pool_offset - 64);
    }
    printf("FixedRunBomb(%d): %s and %s; %zu random strings of %d bits\n", L, fixed, compact, reads, L);
    printf("\nOne lookup with the file out of the cache, microseconds\n");
    printf("  %-34s %9s %9s %9s %9s %10s\n", "", "mean", "p50", "p95", "p99", "max");

    if (!evict(fixed)) printf("  (the system would not drop the pages of %s)\n", fixed);
    for (size_t i = 0; i < reads; i++) {
        RevGraph g;
        FrbStatus status;
        double t0 = cb_now();
        if (frb_file_lookup(fixed, strings[i], NULL, &status, &g) != FRB_OK) return 1;
        us[i] = (cb_now() - t0) * 1e6;
    }
    report("fixed-width: one read", us, reads);

    if (!evict(compact)) printf("  (the system would not drop the pages of %s)\n", compact);
    for (size_t i = 0; i < reads; i++) {
        RevGraph g;
        double t0 = cb_now();
        if (frb_compact_file_lookup(compact, L, strings[i], NULL, &g, NULL) != FRB_OK) return 1;
        us[i] = (cb_now() - t0) * 1e6;
    }
    report("compact as built: two reads", us, reads);

    evict(compact);
    fd = open(compact, O_RDONLY);
    if (fd < 0) return 1;
    for (size_t i = 0; i < reads; i++) {
        uint8_t rec[64];
        double t0 = cb_now();
        if (pread(fd, rec, sizeof rec, (off_t)places[i]) != (ssize_t)sizeof rec) return 1;
        us[i] = (cb_now() - t0) * 1e6;
    }
    close(fd);
    report("one REV, index in memory: one read", us, reads);

    /* the same with everything in the cache, for the difference: once to
     * bring the pages in, then timed */
    for (size_t i = 0; i < reads; i++) {
        RevGraph g;
        FrbStatus status;
        frb_file_lookup(fixed, strings[i], NULL, &status, &g);
        frb_compact_file_lookup(compact, L, strings[i], NULL, &g, NULL);
    }
    for (size_t i = 0; i < reads; i++) {
        RevGraph g;
        FrbStatus status;
        double t0 = cb_now();
        frb_file_lookup(fixed, strings[i], NULL, &status, &g);
        us[i] = (cb_now() - t0) * 1e6;
    }
    printf("\nThe same again, now from the cache\n");
    report("fixed-width", us, reads);
    for (size_t i = 0; i < reads; i++) {
        RevGraph g;
        double t0 = cb_now();
        frb_compact_file_lookup(compact, L, strings[i], NULL, &g, NULL);
        us[i] = (cb_now() - t0) * 1e6;
    }
    report("compact as built", us, reads);

    printf("\nReading an index into memory from a cold file\n");
    {
        uint64_t small = ((uint64_t)cinfo.count + 7) / 8 + (uint64_t)cinfo.count / 2 + ((uint64_t)cinfo.count >> 8) * 36u;
        uint64_t large = cinfo.index_entries * (uint64_t)cinfo.index_bytes;
        double t;
        evict(compact);
        t = read_bytes(compact, small);
        printf("  %12llu bytes (one bit per shorter string, node counts, checkpoints)   %7.3f s\n",
               (unsigned long long)small, t);
        evict(compact);
        t = read_bytes(compact, large);
        printf("  %12llu bytes (the compact file's ordinal for every string)          %7.3f s\n",
               (unsigned long long)large, t);
    }
    free(strings);
    free(places);
    free(us);
    return 0;
}
