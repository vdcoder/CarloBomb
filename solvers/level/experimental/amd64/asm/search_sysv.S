/*
 * CarloBomb LevelSolver search kernel, AMD64, System V ABI, GNU as.
 *
 *     int cb_amd64_search(CbAsmState *state, int max_nodes);
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
 *     code continues straight into the INIT logic.
 *   - Where C pops a frame, control goes to the position at which the
 *     parent pushed it (the parent's *_R or MATCH state).  If the parent is
 *     known at assembly time that is a fall-through; otherwise it is an
 *     indirect jump through the address the parent left in its frame.
 *   - Loop counters and other values that can be read back from the graph
 *     are not stored.
 *
 * Every C step is still counted (STEP, or STEPS for a run of steps with
 * nothing observable between them), every C push is still checked against
 * the stack capacity and the peak depth (PUSHCHK), and both happen in the
 * same order relative to each other as in C.  The comments name the C state
 * each counted step stands for.
 *
 * One C path is not transcribed: FLOW's LOOP_C / LOOP_C_R, taken when a FLOW
 * INIT finds G[cp].color undecided.  It cannot execute.  A FLOW frame is
 * pushed either by a CTRL INIT that has just matched G[cp].color against the
 * target, or by a FLOW frame with the same cp; and no frame changes a color
 * that was already decided when it was pushed.  The kernel relies on this:
 * the FLOW INIT that follows a match takes d from the target bit.
 *
 * Search stack: static storage, 16 bytes per frame, one slot per C frame.
 *   +0  address to resume at when the frame pushed above this one pops
 *   +8  one saved value (meaning depends on the resume position)
 * r12 points at the top frame; depth d <=> r12 = frames + 16*d.  Frame 0
 * holds only the address to resume at when the stack becomes empty.
 *
 * Node (4 bytes):  +0 color  +1 link[0]  +2 link[1]      (-1 = undecided)
 *
 * Registers
 *   rbx  node array                 r13d node budget
 *   esi  fp                         r11d node count (Gn)
 *   edx  cp                         r10  steps remaining before the limit
 *   r8   end of target              r9   gen - target_len (counts up to 0)
 *   r12  top frame                  r14  deepest r12 so far
 *   rax, rcx, rbp, rdi  scratch
 *
 * Every routine preserves esi, edx, r9, r11 and the graph: when a frame
 * pops, the search state is exactly what it was when it was pushed.
 *
 * Baseline x86-64 instructions only (no SSE/AVX/BMI).  Position independent.
 * The machine stack is used only for the five saved registers.
 */
#include "asm_layout.h"

#define ST_FIELD(off) [rbx - CB_ST_NODES + (off)]

    .intel_syntax noprefix
    .text

/* One step of the C search loop: stop if the step limit is reached. */
.macro STEP
    sub     r10, 1
    jb      .Llimit_steps
.endm

/* \n consecutive C steps with no push and no success test between them.
 * Equivalent to \n STEPs: if the limit falls inside the group the search
 * stops with steps == limit either way, and nothing else is reported. */
.macro STEPS n
    sub     r10, \n
    jb      .Llimit_steps
.endm

/* A C push is about to happen from the frame at r12: stop if the stack is
 * full, record a new peak depth.  The common case is one compare. */
.macro PUSHCHK
    cmp     r12, r14
    jae     7f
8:
    .subsection 1
7:  cmp     r12, qword ptr ST_FIELD(CB_ST_SCRATCH)
    je      .Loverflow
    lea     r14, [r12 + 16]
    jmp     8b
    .previous
.endm

/* Push a frame and run \target as that frame; execution continues after
 * the macro when the frame pops. */
.macro CALLF target
    lea     rdi, [rip + 6f]
    mov     qword ptr [r12], rdi
    add     r12, 16
    jmp     \target
6:
.endm

/* Pop the top frame. */
.macro RETF
    sub     r12, 16
    jmp     qword ptr [r12]
.endm

/* Pop if condition.  Each use gets its own indirect jump so the branch
 * predictor can learn them separately. */
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

/* ebp = target(gen) */
.macro TARGET_BIT
    movzx   ebp, byte ptr [r8 + r9]
    and     ebp, 1
.endm

/*
 * push(CTRL, fp = esi, cp = edx) has just been checked with PUSHCHK.
 * Perform that frame's INIT step here instead of pushing it.  If the frame
 * would pop at once (the node it reaches has the wrong color) nothing is
 * pushed at all; otherwise the frame is pushed and continues at the right
 * place.  Either way execution resumes after the macro when the CTRL frame
 * is gone.
 */
.macro PEEK_CTRL
    STEP                                        /* CTRL INIT */
    movsx   eax, byte ptr [rbx + rsi*4]         /* d = G[fp].color */
    test    eax, eax
    js      21f
    lea     rcx, [rbx + rdx*4 + 1]
    add     rcx, rax                            /* rcx = &G[cp].links[d] */
    movsx   eax, byte ptr [rcx]                 /* nxt */
    test    eax, eax
    js      22f
    TARGET_BIT
    movsx   ecx, byte ptr [rbx + rax*4]         /* cc = G[nxt].color */
    cmp     ecx, ebp
    je      23f
    test    ecx, ecx
    js      24f
29:
    .subsection 1
21: CALLF   .Lctrl_color
    jmp     29b
22: CALLF   .Lctrl_link
    jmp     29b
23: CALLF   .Lmatch_ok
    jmp     29b
24: CALLF   .Lmatch_set
    jmp     29b
    .previous
.endm

/*
 * Rest of a CTRL INIT whose output bit matched: eax = nxt, ebp = the bit,
 * which is now also G[nxt].color.  \undo = 1 if this frame colored the node
 * (C: set = 1) and must uncolor it again in its MATCH step.
 *
 * The FLOW frame that C pushes here always finds its color decided, so its
 * INIT is done inline in the frame slot above this one; only the CTRL frame
 * above that, if it survives its own INIT, is really pushed.
 */
.macro MATCH_TAIL undo
    inc     r9                                  /* ++gen == target_len ? */
    jz      .Lsolved
    SAVE    rdx
    mov     edx, eax                            /* cp = nxt */
    PUSHCHK                                     /* push(FLOW, fp, nxt) */
    add     r12, 16                             /* now in the FLOW frame's slot */
    STEP                                        /* FLOW INIT, d = ebp */
    lea     rcx, [rbx + rsi*4 + 1]
    add     rcx, rbp                            /* rcx = &G[fp].links[d] */
    movsx   eax, byte ptr [rcx]
    test    eax, eax
    js      31f
    SAVE    rsi
    mov     esi, eax                            /* fp = nxt */
    PUSHCHK                                     /* push(CTRL, nxt, cp) */
    PEEK_CTRL
    STEPS   2                                   /* SIMPLE_R, then CTRL MATCH */
    RESTORE rsi
    sub     r12, 16                             /* FLOW frame popped */
32:
    .if \undo
    mov     byte ptr [rbx + rdx*4], -1          /* edx is still nxt */
    .endif
    dec     r9
    RESTORE rdx
    RETF
    .subsection 1
31: sub     r12, 16                             /* link undecided: a real FLOW frame */
    CALLF   .Lflow_link
    STEP                                        /* CTRL MATCH */
    jmp     32b
    .previous
.endm

    .globl  cb_amd64_search
    .type   cb_amd64_search, @function
cb_amd64_search:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14

    mov     r13d, esi
    lea     rbx, [rdi + CB_ST_NODES]
    mov     r8, qword ptr [rdi + CB_ST_TARGET]
    mov     r9, qword ptr [rdi + CB_ST_TARGET_LEN]
    add     r8, r9
    neg     r9
    mov     r10, qword ptr [rdi + CB_ST_STEP_LIMIT]
    mov     eax, dword ptr [rdi + CB_ST_MAX_STACK]

    lea     r12, [rdi + CB_ST_FRAMES]
    shl     rax, 4
    add     rax, r12
    mov     qword ptr ST_FIELD(CB_ST_SCRATCH), rax          /* r12 at full depth */
    mov     r14, r12

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

.Lmatch_ok:                                     /* C: set = 0 */
    MATCH_TAIL 0

    .balign 16
.Lmatch_set:                                    /* C: set = 1 */
    mov     byte ptr [rbx + rax*4], bpl
    MATCH_TAIL 1

/* G[fp].color undecided: try WHITE, then BLACK. */
.Lctrl_color:
    STEP                                        /* LOOP_C, it = 0 */
    mov     byte ptr [rbx + rsi*4], 0
    PUSHCHK
    CALLF   .Lctrl_init
    STEPS   2                                   /* LOOP_C_R, then LOOP_C, it = 1 */
    mov     byte ptr [rbx + rsi*4], 1
    PUSHCHK
    CALLF   .Lctrl_init
    STEPS   2                                   /* LOOP_C_R, then LOOP_C, it = 2: pop */
    mov     byte ptr [rbx + rsi*4], -1
    RETF

/* G[cp].links[d] undecided (rcx points at it): try nodes 0..Gn-1, then a
 * new node.  The child of each attempt is a CTRL frame with the same fp and
 * cp whose INIT finds d and the link decided, so its INIT is done inline. */
    .balign 16
.Lctrl_link:
    xor     eax, eax                            /* it = 0 */
    TARGET_BIT                                  /* same bit for every attempt */
    STEP                                        /* LOOP_L */
.Lctrl_link_next:                               /* (this LOOP_L step is already counted) */
    cmp     eax, r11d
    jae     .Lctrl_try_n
    mov     byte ptr [rcx], al
    PUSHCHK                                     /* push(CTRL, fp, cp) */
    STEP                                        /* child: CTRL INIT, nxt = it */
    movsx   edi, byte ptr [rbx + rax*4]         /* cc */
    cmp     edi, ebp
    je      .Lctrl_link_ok
    test    edi, edi
    js      .Lctrl_link_set
    /* wrong color: the child pops at once */
    inc     eax
    STEPS   2                                   /* LOOP_L_R, then LOOP_L */
    jmp     .Lctrl_link_next

.Lctrl_link_ok:
    SAVE    rcx
    CALLF   .Lmatch_ok
    jmp     .Lctrl_link_back
.Lctrl_link_set:
    SAVE    rcx
    CALLF   .Lmatch_set
.Lctrl_link_back:
    STEPS   2                                   /* LOOP_L_R, then LOOP_L */
    RESTORE rcx
    movsx   eax, byte ptr [rcx]                 /* it, read back from the link */
    inc     eax
    TARGET_BIT
    jmp     .Lctrl_link_next

.Lctrl_try_n:
    mov     byte ptr [rcx], -1                  /* LOOP_L, it == Gn: link = UNDECIDED */
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

/* A pushed FLOW frame whose INIT step the pusher has already counted and
 * whose link is decided: eax = nxt.  Only the child of a FLOW TRY_N comes
 * here; every other FLOW INIT is done inline where the frame is pushed. */
    .balign 16
.Lflow_simple:
    SAVE    rsi
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, nxt, cp) */
    CALLF   .Lctrl_init
    STEP                                        /* SIMPLE_R */
    RESTORE rsi
    RETF

/* A pushed FLOW frame whose INIT step is already counted and whose link
 * G[fp].links[d] is undecided (rcx points at it): try nodes 0..Gn-1, then a
 * new node. */
    .balign 16
.Lflow_link:
    xor     eax, eax
    STEP                                        /* LOOP_L */
.Lflow_link_next:                               /* (this LOOP_L step is already counted) */
    cmp     eax, r11d
    jae     .Lflow_try_n
    mov     byte ptr [rcx], al
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    SAVE    rcx
    add     r12, 16                             /* the child FLOW frame's slot */
    STEP                                        /* child: FLOW INIT, nxt = it */
    SAVE    rsi
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, it, cp) */
    PEEK_CTRL
    STEPS   3                                   /* child SIMPLE_R, LOOP_L_R, LOOP_L */
    RESTORE rsi
    sub     r12, 16                             /* child FLOW frame popped */
    RESTORE rcx
    movsx   eax, byte ptr [rcx]                 /* it, read back from the link */
    inc     eax
    jmp     .Lflow_link_next

.Lflow_try_n:
    mov     byte ptr [rcx], -1                  /* LOOP_L, it == Gn: link = UNDECIDED */
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
    lea     rcx, [rdi + CB_ST_FRAMES]
    sub     r14, rcx
    shr     r14, 4
    mov     dword ptr [rdi + CB_ST_PEAK_STACK], r14d
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret
    .size   cb_amd64_search, . - cb_amd64_search

    .section .note.GNU-stack, "", @progbits
