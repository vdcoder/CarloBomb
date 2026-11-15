/*
 * The record checksum: one number for a set of searches, whatever order they
 * come in.  A search is (string, node budget, result, logical steps): what
 * the ladder writes in its journal for every LevelSolver call, and what the
 * shared search can say of every string and budget it covers.  Each search
 * is hashed to 64 bits and the hashes are added.
 *
 * It tells two sets of searches apart unless something went wrong twice in
 * ways that cancel.  It is not a cryptographic digest and does not need to
 * be: it is there to catch mistakes.
 */
#ifndef CARLOBOMB_EXPERIMENT_RECORD_CHECK_H
#define CARLOBOMB_EXPERIMENT_RECORD_CHECK_H

#include <stdint.h>

static inline uint64_t record_mix(uint64_t z) { /* the finalizer of SplitMix64 */
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* string: its table index.  result: 0 a REV was found, 1 there is none. */
static inline uint64_t record_check(uint32_t string, int budget, int result, uint64_t steps) {
    uint64_t what = (uint64_t)string | ((uint64_t)(unsigned)budget << 32) | ((uint64_t)(unsigned)result << 40);
    return record_mix(record_mix(what) ^ steps);
}

#endif
