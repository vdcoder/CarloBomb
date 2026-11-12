/*
 * Hardware event counts around a region of code, through Linux
 * perf_event_open: cycles, instructions, branches and branch misses of the
 * calling thread in user mode, counted as one group so that all four cover
 * exactly the same interval.
 *
 * Many virtual machines and containers expose no hardware counters; then
 * cb_perf_open returns 0 and the caller reports that and goes on without.
 * cb_perf_open_software opens a group of software events instead, read with
 * the same calls, as a check of this code on such machines.
 */
#ifndef CARLOBOMB_PERF_COUNTERS_H
#define CARLOBOMB_PERF_COUNTERS_H

#include <stdint.h>

enum { CB_PERF_CYCLES, CB_PERF_INSTRUCTIONS, CB_PERF_BRANCHES, CB_PERF_BRANCH_MISSES, CB_PERF_N };

typedef struct {
    int fd[CB_PERF_N]; /* fd[0] is the group leader */
    int n;             /* events in the group; 0 = not open */
    int error;         /* errno of the failed open */
} CbPerf;

#if defined(__linux__)

#include <errno.h>
#include <linux/perf_event.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int cb_perf_open_group(CbPerf *p, uint32_t type, const uint64_t *config, int n) {
    p->n = 0;
    p->error = 0;
    for (int i = 0; i < n; i++) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.type = type;
        a.size = sizeof a;
        a.config = config[i];
        a.disabled = i == 0; /* members follow the leader */
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED |
                        PERF_FORMAT_TOTAL_TIME_RUNNING;
        long fd = syscall(SYS_perf_event_open, &a, 0, -1, i == 0 ? -1 : p->fd[0], 0);
        if (fd < 0) {
            p->error = errno;
            for (int k = 0; k < i; k++) close(p->fd[k]);
            return 0;
        }
        p->fd[i] = (int)fd;
    }
    p->n = n;
    return 1;
}

/* Cycles, instructions, branches, branch misses.  Returns 1 if available. */
static int cb_perf_open(CbPerf *p) {
    static const uint64_t hw[CB_PERF_N] = {
        PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS,
        PERF_COUNT_HW_BRANCH_INSTRUCTIONS, PERF_COUNT_HW_BRANCH_MISSES};
    return cb_perf_open_group(p, PERF_TYPE_HARDWARE, hw, CB_PERF_N);
}

/* Task clock (ns), page faults, context switches, CPU migrations. */
static int cb_perf_open_software(CbPerf *p) {
    static const uint64_t sw[CB_PERF_N] = {
        PERF_COUNT_SW_TASK_CLOCK, PERF_COUNT_SW_PAGE_FAULTS,
        PERF_COUNT_SW_CONTEXT_SWITCHES, PERF_COUNT_SW_CPU_MIGRATIONS};
    return cb_perf_open_group(p, PERF_TYPE_SOFTWARE, sw, CB_PERF_N);
}

static void cb_perf_start(const CbPerf *p) {
    if (!p->n) return;
    ioctl(p->fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(p->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

/*
 * Stop counting and read the group.  Returns 1 if the counts are exact, 0 if
 * they could not be read or the group was not on the counters the whole time
 * (the kernel was multiplexing it), in which case they are not used.
 */
static int cb_perf_stop(const CbPerf *p, uint64_t out[CB_PERF_N]) {
    struct {
        uint64_t nr, enabled, running, value[CB_PERF_N];
    } r;
    if (!p->n) return 0;
    ioctl(p->fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    if (read(p->fd[0], &r, sizeof r) < (long)(3 * sizeof(uint64_t)) || r.nr != (uint64_t)p->n)
        return 0;
    for (int i = 0; i < CB_PERF_N; i++) out[i] = i < p->n ? r.value[i] : 0;
    return r.enabled == r.running;
}

static const char *cb_perf_error(const CbPerf *p) { return strerror(p->error); }

#else /* not Linux */

static int cb_perf_open(CbPerf *p) { p->n = 0; p->error = 0; return 0; }
static int cb_perf_open_software(CbPerf *p) { p->n = 0; p->error = 0; return 0; }
static void cb_perf_start(const CbPerf *p) { (void)p; }
static int cb_perf_stop(const CbPerf *p, uint64_t out[CB_PERF_N]) { (void)p; (void)out; return 0; }
static const char *cb_perf_error(const CbPerf *p) { (void)p; return "not supported on this system"; }

#endif

#endif
