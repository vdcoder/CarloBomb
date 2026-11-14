/*
 * journal_digest: the searches of a construction journal as three numbers.
 *
 *   journal_digest WORK
 *
 * WORK is the work directory of a FixedRunBomb construction (a level of a
 * ladder: LADDER/work-L).  Prints how many searches its journal holds, their
 * logical steps, and the record checksum of record_check.h over every
 * search (string, node budget, result, logical steps).
 *
 * shared_search --count prints the same three numbers for the searches it
 * covers.  Equal numbers say that the shared search, for every string and
 * budget the ladder searched, knows the result the ladder got and the exact
 * number of steps the ladder's solver took: without the journal having left
 * the machine it is on.
 *
 * Reads the directory and writes nothing.  It takes no lock, so it can be
 * pointed at the directory of a finished level while another level runs;
 * not at the directory of the level that is running.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fixed_run_build.h"
#include "record_check.h"

typedef struct {
    uint64_t searches[3], steps[3], check, other, half;
    uint64_t by_budget[FRB_MAX_NODES + 1][2];
} Seen;

static int visit(void *ctx, const FrbSegmentInfo *seg, const FrbRecord *rec) {
    Seen *s = ctx;
    (void)seg;
    if (!rec || rec->type != FRB_REC_SEARCH) return 0;
    if (rec->result > 1 || rec->budget > FRB_MAX_NODES) { /* stopped by a limit: says nothing, and is not a search here */
        s->searches[2]++;
        s->steps[2] += rec->steps;
        return 0;
    }
    if (rec->target >= s->half) s->other++;
    s->searches[rec->result]++;
    s->steps[rec->result] += rec->steps;
    s->by_budget[rec->budget][rec->result]++;
    s->check += record_check(rec->target, rec->budget, rec->result, rec->steps);
    return 0;
}

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
    FrbConfig cfg;
    FrbJournalInfo info;
    Seen s;
    char err[256] = "";
    FrbError e;
    if (argc != 2 || argv[1][0] == '-') return fprintf(stderr, "usage: journal_digest WORK\n"), 2;
    e = frb_config_read(argv[1], &cfg, err, sizeof err);
    if (e != FRB_OK) return fprintf(stderr, "journal_digest: %s: %s\n", argv[1], err[0] ? err : frb_error_str(e)), 1;
    memset(&s, 0, sizeof s);
    s.half = cfg.symmetry ? (uint64_t)1 << (cfg.bits - 1) : (uint64_t)1 << cfg.bits;
    e = frb_journal_scan(argv[1], &cfg, visit, &s, &info, err, sizeof err);
    if (e != FRB_OK) return fprintf(stderr, "journal_digest: %s: %s\n", argv[1], err[0] ? err : frb_error_str(e)), 1;

    printf("%s: strings of %d bits", argv[1], cfg.bits);
    if (cfg.base_bits) printf(", built on the Bomb of %d bits", cfg.base_bits);
    printf("\n  journal: %s records in %u segments, %s bytes\n", commas(info.records), (unsigned)info.segments, commas(info.bytes));
    printf("  %-6s %16s %16s\n", "budget", "found a REV", "found none");
    for (int n = 1; n <= FRB_MAX_NODES; n++)
        if (s.by_budget[n][0] || s.by_budget[n][1])
            printf("  %-6d %16s %16s\n", n, commas(s.by_budget[n][0]), commas(s.by_budget[n][1]));
    printf("  searches: %s (%s found a REV, %s found none), %s logical steps\n", commas(s.searches[0] + s.searches[1]),
           commas(s.searches[0]), commas(s.searches[1]), commas(s.steps[0] + s.steps[1]));
    if (s.searches[2])
        printf("  and %s searches stopped by a limit, %s steps: not counted above or below\n", commas(s.searches[2]), commas(s.steps[2]));
    if (s.other) printf("  %s of the searches are of strings the symmetry leaves to their complements\n", commas(s.other));
    printf("  record checksum (every search: string, budget, result, steps): %016llx\n", (unsigned long long)s.check);
    return 0;
}
