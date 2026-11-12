/*
 * Bomb-backed FixedSolver: the exact minimum-node REV for any binary string
 * of 1 to B bits, read out of a complete FixedRunBomb(B).  B is 16 for the
 * table of Milestone 2.  No search is run, and none is linked: this module
 * depends on the REV core and on the FixedRunBomb format code only.
 *
 * Why a table for B-bit strings answers shorter ones.  A REV that emits a
 * string S of L < B bits goes on to emit some B-bit string that starts with
 * S, and every REV for such an extension is a REV for S.  So the minimum
 * node count of S is the smallest minimum among its 2^(B-L) extensions, and
 * the REV stored for that extension is a minimum-node REV for S.
 *
 * The prefix index makes that selection once, for every prefix, when the
 * Bomb is opened: one 16-bit number per prefix, the table index of its best
 * extension (fewest nodes; among equals, the lowest index).  For B = 16 that
 * is 65,536 numbers, 128 KiB, built in one pass.  A lookup is then one array
 * read.  The graphs stay in the Bomb.
 *
 * A RevFixedBomb is immutable once opened: any number of threads may query
 * one at the same time.
 *
 * README.md in this directory has the proofs, the limits and measurements.
 */
#ifndef CARLOBOMB_FIXED_BOMB_SOLVER_H
#define CARLOBOMB_FIXED_BOMB_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "fixed_run.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RevFixedBomb RevFixedBomb;

/*
 * Open a Bomb for solving.
 *
 * The file is validated in full as FORMAT.md section 6 requires: header,
 * length, both checksums, every entry decoded, every stored graph run on
 * the REV interpreter against the string it is filed under, and the header
 * counts.  On top of that the Bomb must be usable as a source of certified
 * minima:
 *
 *   - every entry must be a certified minimum, else FRB_ERR_INCOMPLETE;
 *   - a string and its complement must have the same node count, else
 *     FRB_ERR_EVIDENCE (complement symmetry makes any other table wrong).
 *
 * Any other FrbError is the reason the file was refused: FRB_ERR_IO if it
 * cannot be read, FRB_ERR_MAGIC, _VERSION, _HEADER, _SIZE, _CHECKSUM,
 * _ENTRY, _OUTPUT or _SUMMARY if it is not a valid FixedRunBomb.  Where an
 * entry is at fault *bad_index (if not NULL) is its table index.
 *
 * What opening cannot establish is that each stored node count really is a
 * minimum: that is the evidence of the Bomb's construction (see
 * bombs/fixed-run/README.md, and `carlobomb bomb verify --recertify`).
 */
FrbError rev_fixed_bomb_open(const char *path, RevFixedBomb **bomb, uint32_t *bad_index);

/*
 * The same from a table in memory, which is validated like a file's.  On
 * success the solver takes over the table's storage and *table is left
 * empty; on failure *table is unchanged.
 */
FrbError rev_fixed_bomb_from_table(FrbTable *table, RevFixedBomb **bomb, uint32_t *bad_index);

void rev_fixed_bomb_close(RevFixedBomb *bomb);

/* B: the Bomb's string length, and the longest target that can be solved. */
int rev_fixed_bomb_bits(const RevFixedBomb *bomb);

/* The largest minimum node count of any string of up to B bits. */
int rev_fixed_bomb_max_nodes(const RevFixedBomb *bomb);

/* ---- queries by number ---------------------------------------------------
 *
 * A string of `bits` bits is given as the integer whose binary digits it is,
 * first emitted bit most significant (the convention of FixedRunBomb
 * indices).  1 <= bits <= B and value < 2^bits; anything else returns 0.
 * Both are a single array read.
 */

/* The certified minimum node count of the string. */
int rev_fixed_bomb_min_nodes(const RevFixedBomb *bomb, int bits, uint32_t value);

/*
 * The table index of the B-bit extension whose entry answers the string:
 * the one with the fewest nodes and, among those, the lowest index (the
 * lexicographically first).  For bits = B it is the string itself.
 */
uint32_t rev_fixed_bomb_extension(const RevFixedBomb *bomb, int bits, uint32_t value);

/*
 * counts[n] = number of strings of `bits` bits whose minimum is n nodes, for
 * n = 0..FRB_NODE_CAPACITY (counts[0] stays 0).  Costs 2^bits lookups.
 */
void rev_fixed_bomb_histogram(const RevFixedBomb *bomb, int bits,
                              uint32_t counts[FRB_NODE_CAPACITY + 1]);

/* ---- solving ------------------------------------------------------------- */

typedef enum {
    REV_FIXED_BOMB_MINIMAL = 0,     /* *result is a certified minimum-node REV for the target */
    REV_FIXED_BOMB_PROVEN_UNSAT,    /* no REV within the node budget emits the target: its
                                       certified minimum, in info->min_nodes, is larger */
    REV_FIXED_BOMB_UNSUPPORTED,     /* the target is longer than B bits.  Nothing is said about
                                       its minimum; info->lower_bound is all the Bomb knows */
    REV_FIXED_BOMB_INVALID_INPUT,   /* NULL or empty target, a character other than '0' and
                                       '1', a negative budget, or no Bomb */
    REV_FIXED_BOMB_INTERNAL_ERROR   /* the stored graph failed verification: the Bomb in
                                       memory has been damaged since it was opened */
} RevFixedBombResult;

const char *rev_fixed_bomb_result_str(RevFixedBombResult r);

typedef struct {
    int bomb_bits;      /* B */
    int min_nodes;      /* certified minimum node count of the target; 0 if not known here */
    int lower_bound;    /* certified: no REV with fewer nodes emits the target.  Equals
                           min_nodes whenever that is known */
    uint32_t extension; /* MINIMAL, PROVEN_UNSAT: table index of the B-bit extension used */
} RevFixedBombInfo;

/*
 * The minimum-node REV for a target of 1..B characters '0' and '1'.
 *
 * max_nodes is a node budget: 0 means none.  With a budget below the
 * target's certified minimum the answer is REV_FIXED_BOMB_PROVEN_UNSAT, and
 * it is a proof: the minimum is exact.
 *
 * On REV_FIXED_BOMB_MINIMAL *result is the stored graph, complete and
 * executable, and it has been run on the interpreter against the target
 * once more before being returned.  On any other answer result->node_count
 * is 0.  result and info may be NULL.
 *
 * A target longer than B bits is REV_FIXED_BOMB_UNSUPPORTED, whatever the
 * budget.  The Bomb is never used to call a REV for a longer string minimal.
 */
RevFixedBombResult rev_fixed_bomb_solve(const RevFixedBomb *bomb, const char *target,
                                        size_t target_len, int max_nodes, RevGraph *result,
                                        RevFixedBombInfo *info);

/*
 * A certified lower bound on the node count of any REV that emits `target`,
 * for a target of any length; 0 for an invalid target.  For up to B bits it
 * is the minimum itself.  For longer targets it is not a minimum and is not
 * to be read as one.  It rests on two facts, proved in README.md:
 *
 *   - a REV that emits a string emits its first B bits, so it has at least
 *     as many nodes as those need;
 *   - if n nodes emit a string, n + 1 nodes emit the string with its first
 *     j bits removed, for any j.  So every stretch of the target that is
 *     looked up, minus one, is a bound as well.
 *
 * Costs one lookup per character.
 */
int rev_fixed_bomb_lower_bound(const RevFixedBomb *bomb, const char *target, size_t target_len);

/*
 * Benchmark interface: build the prefix index again from the table, exactly
 * as opening does.  It stores the values that are already there, but it does
 * write: not for use while other threads query the Bomb.
 */
void rev_fixed_bomb_reindex(RevFixedBomb *bomb);

#ifdef __cplusplus
}
#endif

#endif
