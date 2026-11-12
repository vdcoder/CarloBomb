/*
 * Memory layout shared by search_sysv.S and asm_solver.c.
 *
 * The assembler source sees only the CB_ST_* / CB_RES_* / CB_LIM_* numbers.
 * The C source sees the same numbers and the structure itself, and
 * asm_solver.c checks each number against the structure with _Static_assert,
 * so the two cannot drift apart without a build failure.
 */
#ifndef CARLOBOMB_ASM_LAYOUT_H
#define CARLOBOMB_ASM_LAYOUT_H

#define CB_ASM_MAX_NODES 64  /* must equal REV_LEVEL_MAX_NODES */
#define CB_ASM_NODE_SIZE 4   /* bytes: color, link0, link1, unused */
#define CB_ASM_GUARD 64      /* bytes of guard pattern around each array */
#define CB_ASM_FRAME_SIZE 16 /* bytes of static storage per search frame */

/* Field offsets in CbAsmState. */
#define CB_ST_TARGET 0
#define CB_ST_TARGET_LEN 8
#define CB_ST_STEP_LIMIT 16
#define CB_ST_STEPS 24
#define CB_ST_PEAK_STACK 32
#define CB_ST_LIMIT_HIT 36
#define CB_ST_NODE_COUNT 40
#define CB_ST_MAX_STACK 44
#define CB_ST_SCRATCH 48
#define CB_ST_COUNTERS 56 /* scratch[1]: CbAsmCounters *, read by counting kernels only */
#define CB_ST_NODES 128
#define CB_ST_FRAMES 448

/* Return values of the kernel; equal to the RevSolveResult enumerators. */
#define CB_RES_SOLVED 0
#define CB_RES_UNSAT 1
#define CB_RES_LIMIT 2

/* Field offsets in CbAsmCounters (benchmark-only kernels, CB_COUNT). */
#define CB_CNT_BULK_STEPS 0
#define CB_CNT_BULK_SCANS 8
#define CB_CNT_BULK_LOOPS 16
#define CB_CNT_FALLBACK_STEPS 24
#define CB_CNT_FALLBACK_STACK 32
#define CB_CNT_FALLBACK_UNCOLORED 40

/* Values stored in limit_hit; equal to the RevLevelLimit enumerators. */
#define CB_LIM_NONE 0
#define CB_LIM_STACK 1
#define CB_LIM_STEPS 2

#ifndef __ASSEMBLER__

#include <stdint.h>

#include "level_solver.h" /* REV_LEVEL_MAX_STACK */

typedef struct {
    /* inputs */
    const char *target;   /* '0'/'1' characters, already validated */
    uint64_t target_len;  /* >= 1 */
    uint64_t step_limit;  /* steps allowed; UINT64_MAX = unlimited */
    /* outputs */
    uint64_t steps;       /* steps performed, same definition as the C solver */
    uint32_t peak_stack;  /* deepest search stack, in frames */
    uint32_t limit_hit;   /* CB_LIM_* */
    uint32_t node_count;  /* nodes in the graph when the kernel returned */
    /* input */
    uint32_t max_stack;   /* search stack capacity in frames, 1..REV_LEVEL_MAX_STACK */
    uint64_t scratch[2];  /* [0] kernel private; [1] the address of a CbAsmCounters
                             for the counting kernels (others ignore it) */

    uint8_t guard0[CB_ASM_GUARD];
    int8_t nodes[CB_ASM_MAX_NODES][CB_ASM_NODE_SIZE]; /* -1 = undecided */
    uint8_t guard1[CB_ASM_GUARD];
    /* Search-stack storage.  Its use is private to each kernel; none uses
     * more than CB_ASM_FRAME_SIZE bytes per frame or touches the guards. */
    uint64_t frames[(REV_LEVEL_MAX_STACK + 2) * (CB_ASM_FRAME_SIZE / 8)];
    uint8_t guard2[CB_ASM_GUARD];
} CbAsmState;

/*
 * Search accounting for the benchmark-only "-count" kernels.  The logical
 * step count in CbAsmState.steps is exact for every kernel and is what all
 * results and limits use.  These counters say how those steps were arrived
 * at in a kernel with bulk accounting:
 *
 *   steps - bulk_steps   steps the kernel executed one at a time
 *   bulk_steps           steps it charged arithmetically for link attempts
 *                        that were certain to fail, without executing them
 *
 * They accumulate over searches until cb_amd64_counters_reset().
 */
typedef struct {
    uint64_t bulk_steps;         /* steps accounted for in bulk (3 per attempt skipped) */
    uint64_t bulk_scans;         /* bit scans for the next attempt worth making */
    uint64_t bulk_loops;         /* CTRL link loops started in bulk mode */
    uint64_t fallback_steps;     /* times a loop left bulk mode: step limit near */
    uint64_t fallback_stack;     /* ... the search stack was full */
    uint64_t fallback_uncolored; /* loops not started in bulk mode: an existing node had no color */
} CbAsmCounters;

#endif /* !__ASSEMBLER__ */

#endif
