/*
 * CarloBomb LevelSolver search kernel, AMD64, System V ABI, GNU as.
 *
 *     int CB_KERNEL(CbAsmState *state, int max_nodes);
 *
 * Performs the same search as search() in level_solver.c: same move order,
 * same undo actions, same first solution, same step count, same peak stack
 * depth, same behavior at the step and stack limits.  See README.md in this
 * directory for the derivation and measurements.
 *
 * How it differs from the C code
 * ------------------------------
 * The C solver stores (function, state) in each frame and re-dispatches
 * through a switch on every step.  Here a frame's state is a position in
 * this code:
 *
 *   - Where C pushes a frame and the next step is that frame's INIT, the
 *     code simply continues into the INIT logic.
 *   - Where C pops a frame, control returns to the code position at which
 *     the parent pushed it (the parent's *_R or MATCH state).
 *   - Loop counters and saved values that can be read back from the graph
 *     are not stored at all.
 *
 * Every C step is still counted individually (STEP), every C push is still
 * checked against the stack capacity and the peak depth (PUSHCHK), and both
 * happen in the same order relative to graph updates as in C.  The comments
 * name the C state each STEP stands for.
 *
 * Two ways of returning to the parent are provided, chosen when assembling:
 *
 *   CB_USE_RET = 0   Explicit stack only.  Each frame is 16 bytes of static
 *                    storage: the address to resume at and one saved value.
 *                    Pop is an indirect jump through the frame.
 *   CB_USE_RET = 1   Push is `call`, pop is `ret`, so the processor's
 *                    return predictor follows the search.  Each frame is one
 *                    8-byte return address on the machine stack (at most
 *                    8 * max_stack bytes) plus one saved value in static
 *                    storage.  Depth is derived from rsp.
 *
 * Node (4 bytes):  +0 color  +1 link[0]  +2 link[1]      (-1 = undecided)
 *
 * Registers
 *   rbx  node array                 r13d node budget
 *   esi  fp                         r11d node count (Gn)
 *   edx  cp                         r10  steps remaining before the limit
 *   r8   end of target              r9   gen - target_len (counts up to 0)
 *   r14  deepest stack position so far
 *   r12  top frame            (CB_USE_RET = 0)
 *   r15  static frame array minus machine stack position (CB_USE_RET = 1)
 *   rax, rcx, rbp, rdi  scratch
 *
 * Every routine below preserves esi, edx, r9, r11 and the graph: on return
 * the search state is exactly what it was when the frame was pushed.
 *
 * Baseline x86-64 instructions only (no SSE/AVX/BMI).  Position independent.
 */
#include "asm_layout.h"

#ifndef CB_USE_RET
#define CB_USE_RET 0
#endif
#ifndef CB_KERNEL
#define CB_KERNEL cb_amd64_search
#endif

#define ST_FIELD(off) [rbx - CB_ST_NODES + (off)]

    .intel_syntax noprefix
    .text

/* One step of the C search loop: stop if the step limit is reached. */
.macro STEP
    sub     r10, 1
    jb      .Llimit_steps
.endm

#if CB_USE_RET

/* A push is about to happen: check capacity, record a new peak depth. */
.macro PUSHCHK
    cmp     rsp, r14
    jbe     7f
8:
    .subsection 1
7:  call    .Lgrow
    jmp     8b
    .previous
.endm

.macro CALLF target
    call    \target
.endm

.macro RETF
    ret
.endm

/* Return if condition: all share one `ret`, the return predictor does not
 * care where it is. */
.macro JRET cc
    j\cc    .Lret
.endm

.macro SAVE reg
    mov     qword ptr [rsp + r15], \reg
.endm

.macro RESTORE reg
    mov     \reg, qword ptr [rsp + r15]
.endm

#else /* explicit stack */

.macro PUSHCHK
    cmp     r12, r14
    jae     7f
8:
    .subsection 1
7:  cmp     r12, qword ptr ST_FIELD(CB_ST_SCRATCH + 8)
    je      .Loverflow
    lea     r14, [r12 + 16]
    jmp     8b
    .previous
.endm

.macro CALLF target
    lea     rdi, [rip + 6f]
    mov     qword ptr [r12], rdi
    add     r12, 16
    jmp     \target
6:
.endm

.macro RETF
    sub     r12, 16
    jmp     qword ptr [r12]
.endm

/* Each conditional return gets its own indirect jump, so the predictor can
 * learn them separately. */
.macro JRET cc
    j\cc    7f
    .subsection 1
7:  sub     r12, 16
    jmp     qword ptr [r12]
    .previous
.endm

.macro SAVE reg
    mov     qword ptr [r12 + 8], \reg
.endm

.macro RESTORE reg
    mov     \reg, qword ptr [r12 + 8]
.endm

#endif

/* ebp = target(gen) */
.macro TARGET_BIT
    movzx   ebp, byte ptr [r8 + r9]
    and     ebp, 1
.endm

    .globl  CB_KERNEL
    .type   CB_KERNEL, @function
CB_KERNEL:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r13d, esi
    lea     rbx, [rdi + CB_ST_NODES]
    mov     r8, qword ptr [rdi + CB_ST_TARGET]
    mov     r9, qword ptr [rdi + CB_ST_TARGET_LEN]
    add     r8, r9
    neg     r9
    mov     r10, qword ptr [rdi + CB_ST_STEP_LIMIT]
    mov     eax, dword ptr [rdi + CB_ST_MAX_STACK]

#if CB_USE_RET
    /* depth d  <=>  rsp = base - 8*d;  its saved value lives in
     * frames[max_stack + 1 - d], addressed as [rsp + r15]. */
    mov     qword ptr ST_FIELD(CB_ST_SCRATCH), rsp          /* base */
    lea     rcx, [rax*8]
    mov     r14, rsp
    sub     r14, rcx
    mov     qword ptr ST_FIELD(CB_ST_SCRATCH + 8), r14      /* rsp at full depth */
    lea     r15, [rdi + CB_ST_FRAMES + 8 + rax*8]
    sub     r15, rsp
    mov     r14, rsp
#else
    /* depth d  <=>  r12 = frames + 16*d; frame 0 only holds the exit. */
    lea     r12, [rdi + CB_ST_FRAMES]
    shl     eax, 4
    add     rax, r12
    mov     qword ptr ST_FIELD(CB_ST_SCRATCH + 8), rax      /* r12 at full depth */
    mov     r14, r12
#endif

    mov     dword ptr [rbx], -1         /* G[0] = all undecided */
    mov     r11d, 1                     /* Gn = 1 */
    xor     esi, esi
    xor     edx, edx
    PUSHCHK                             /* push(CTRL, 0, 0) */
    CALLF   .Lctrl_init
    mov     eax, CB_RES_UNSAT           /* stack empty */
    xor     ecx, ecx
    jmp     .Lfinish

/* ---- CTRL(fp = esi, cp = edx) ------------------------------------------ */

    .balign 16
.Lctrl_init:
    STEP                                        /* CTRL INIT */
    movsx   eax, byte ptr [rbx + rsi*4]         /* d = G[fp].color */
    test    eax, eax
    js      .Lctrl_color
    lea     rcx, [rbx + rdx*4 + 1]
    add     rcx, rax                            /* rcx = &G[cp].links[d] */
    movsx   eax, byte ptr [rcx]                 /* nxt */
    test    eax, eax
    js      .Lctrl_link

    /* eax = nxt */
    TARGET_BIT
    movsx   ecx, byte ptr [rbx + rax*4]         /* cc = G[nxt].color */
    test    ecx, ecx
    js      .Lmatch_set
    cmp     ecx, ebp
    JRET    ne                                  /* wrong color: pop */

/* Rest of a CTRL INIT whose output bit matched.  eax = nxt.
 * .Lmatch_ok:  the node already had the right color   (C: set = 0)
 * .Lmatch_set: the node is uncolored, ebp = target bit (C: set = 1) */
.Lmatch_ok:
    inc     r9                                  /* ++gen == target_len ? */
    jz      .Lsolved
    SAVE    rdx
    mov     edx, eax
    PUSHCHK                                     /* push(FLOW, fp, nxt) */
    CALLF   .Lflow_init
    STEP                                        /* CTRL MATCH */
    dec     r9
    RESTORE rdx
    RETF

    .balign 16
.Lmatch_set:
    mov     byte ptr [rbx + rax*4], bpl
    inc     r9
    jz      .Lsolved
    SAVE    rdx
    mov     edx, eax
    PUSHCHK                                     /* push(FLOW, fp, nxt) */
    CALLF   .Lflow_init
    STEP                                        /* CTRL MATCH */
    mov     byte ptr [rbx + rdx*4], -1          /* edx is still nxt */
    dec     r9
    RESTORE rdx
    RETF

/* G[fp].color undecided: try WHITE, then BLACK. */
.Lctrl_color:
    STEP                                        /* LOOP_C, it = 0 */
    mov     byte ptr [rbx + rsi*4], 0
    PUSHCHK
    CALLF   .Lctrl_init
    STEP                                        /* LOOP_C_R */
    mov     byte ptr [rbx + rsi*4], -1
    STEP                                        /* LOOP_C, it = 1 */
    mov     byte ptr [rbx + rsi*4], 1
    PUSHCHK
    CALLF   .Lctrl_init
    STEP                                        /* LOOP_C_R */
    mov     byte ptr [rbx + rsi*4], -1
    STEP                                        /* LOOP_C, it = 2: pop */
    RETF

/* G[cp].links[d] undecided (rcx points at it): try nodes 0..Gn-1, then a
 * new node.  The child of each attempt is a CTRL frame with the same fp and
 * cp whose INIT finds d and the link decided, so its INIT is done inline. */
    .balign 16
.Lctrl_link:
    xor     eax, eax                            /* it = 0 */
.Lctrl_link_next:
    STEP                                        /* LOOP_L */
    cmp     eax, r11d
    jae     .Lctrl_try_n
    mov     byte ptr [rcx], al
    PUSHCHK                                     /* push(CTRL, fp, cp) */
    STEP                                        /* child: CTRL INIT, nxt = it */
    TARGET_BIT
    movsx   edi, byte ptr [rbx + rax*4]         /* cc */
    test    edi, edi
    js      .Lctrl_link_set
    cmp     edi, ebp
    je      .Lctrl_link_ok
    /* wrong color: the child pops at once */
    STEP                                        /* LOOP_L_R */
    mov     byte ptr [rcx], -1
    inc     eax
    jmp     .Lctrl_link_next

.Lctrl_link_ok:
    SAVE    rcx
    CALLF   .Lmatch_ok
    jmp     .Lctrl_link_back
.Lctrl_link_set:
    SAVE    rcx
    CALLF   .Lmatch_set
.Lctrl_link_back:
    STEP                                        /* LOOP_L_R */
    RESTORE rcx
    movsx   eax, byte ptr [rcx]                 /* it, read back from the link */
    mov     byte ptr [rcx], -1
    inc     eax
    jmp     .Lctrl_link_next

.Lctrl_try_n:                                   /* (LOOP_L with it == Gn was the step above) */
    STEP                                        /* TRY_N */
    cmp     r11d, r13d
    JRET    ae                                  /* budget exhausted: pop */
    mov     byte ptr [rcx], r11b
    mov     dword ptr [rbx + r11*4], -1         /* new node, all undecided */
    mov     eax, r11d
    inc     r11d
    PUSHCHK                                     /* push(CTRL, fp, cp) */
    SAVE    rcx
    STEP                                        /* child: CTRL INIT, nxt = new node */
    TARGET_BIT
    CALLF   .Lmatch_set
    STEP                                        /* TRY_N_R */
    RESTORE rcx
    dec     r11d
    mov     byte ptr [rcx], -1
    RETF

/* ---- FLOW(fp = esi, cp = edx) ------------------------------------------ */

    .balign 16
.Lflow_init:
    STEP                                        /* FLOW INIT */
    movsx   eax, byte ptr [rbx + rdx*4]         /* d = G[cp].color */
    test    eax, eax
    js      .Lflow_color
    lea     rcx, [rbx + rsi*4 + 1]
    add     rcx, rax                            /* rcx = &G[fp].links[d] */
    movsx   eax, byte ptr [rcx]                 /* nxt */
    test    eax, eax
    js      .Lflow_link

/* Rest of a FLOW INIT whose link is decided.  eax = nxt. */
.Lflow_simple:
    SAVE    rsi
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, nxt, cp) */
    CALLF   .Lctrl_init
    STEP                                        /* SIMPLE_R */
    RESTORE rsi
    RETF

/* G[fp].links[d] undecided (rcx points at it). */
    .balign 16
.Lflow_link:
    xor     eax, eax
.Lflow_link_next:
    STEP                                        /* LOOP_L */
    cmp     eax, r11d
    jae     .Lflow_try_n
    mov     byte ptr [rcx], al
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    SAVE    rcx
    STEP                                        /* child: FLOW INIT, nxt = it */
    CALLF   .Lflow_simple
    STEP                                        /* LOOP_L_R */
    RESTORE rcx
    movsx   eax, byte ptr [rcx]
    mov     byte ptr [rcx], -1
    inc     eax
    jmp     .Lflow_link_next

.Lflow_try_n:
    STEP                                        /* TRY_N */
    cmp     r11d, r13d
    JRET    ae
    mov     byte ptr [rcx], r11b
    mov     dword ptr [rbx + r11*4], -1
    mov     eax, r11d
    inc     r11d
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    SAVE    rcx
    STEP                                        /* child: FLOW INIT, nxt = new node */
    CALLF   .Lflow_simple
    STEP                                        /* TRY_N_R */
    RESTORE rcx
    dec     r11d
    mov     byte ptr [rcx], -1
    RETF

/* G[cp].color undecided in FLOW.  The C code handles this case, so it is
 * kept, but no input reaches it: a FLOW frame's cp was colored by the CTRL
 * step that pushed it. */
.Lflow_color:
    STEP                                        /* LOOP_C, it = 0 */
    mov     byte ptr [rbx + rdx*4], 0
    PUSHCHK
    CALLF   .Lflow_init
    STEP                                        /* LOOP_C_R */
    mov     byte ptr [rbx + rdx*4], -1
    STEP                                        /* LOOP_C, it = 1 */
    mov     byte ptr [rbx + rdx*4], 1
    PUSHCHK
    CALLF   .Lflow_init
    STEP                                        /* LOOP_C_R */
    mov     byte ptr [rbx + rdx*4], -1
    STEP                                        /* LOOP_C, it = 2: pop */
    RETF

#if CB_USE_RET
.Lret:
    ret

/* Called from PUSHCHK with rsp where the new frame would be. */
.Lgrow:
    cmp     rsp, qword ptr ST_FIELD(CB_ST_SCRATCH + 8)
    jb      .Loverflow
    mov     r14, rsp
    ret
#endif

/* ---- exits ------------------------------------------------------------- */

.Lsolved:
    mov     eax, CB_RES_SOLVED
    xor     ecx, ecx                            /* CB_LIM_NONE */
    jmp     .Lfinish
.Llimit_steps:
    xor     r10d, r10d                          /* nothing remains */
    mov     eax, CB_RES_LIMIT
    mov     ecx, CB_LIM_STEPS
    jmp     .Lfinish
.Loverflow:
    mov     eax, CB_RES_LIMIT
    mov     ecx, CB_LIM_STACK
.Lfinish:
    lea     rdi, [rbx - CB_ST_NODES]
    mov     dword ptr [rdi + CB_ST_LIMIT_HIT], ecx
    mov     rcx, qword ptr [rdi + CB_ST_STEP_LIMIT]
    sub     rcx, r10
    mov     qword ptr [rdi + CB_ST_STEPS], rcx
    mov     dword ptr [rdi + CB_ST_NODE_COUNT], r11d
#if CB_USE_RET
    mov     rsp, qword ptr [rdi + CB_ST_SCRATCH]            /* drop all frames */
    mov     rcx, rsp
    sub     rcx, r14
    shr     rcx, 3
#else
    lea     rcx, [rdi + CB_ST_FRAMES]
    sub     r14, rcx
    mov     rcx, r14
    shr     rcx, 4
#endif
    mov     dword ptr [rdi + CB_ST_PEAK_STACK], ecx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret
    .size   CB_KERNEL, . - CB_KERNEL

    .section .note.GNU-stack, "", @progbits
