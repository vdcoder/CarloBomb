/*
 * Shared control flow of the graph-representation experiment kernels.
 *
 * This is the search of ../asm/search_sysv.S (the reference kernel) with every
 * access to the graph replaced by a G_* macro, so that kernels which differ
 * only in how the graph is stored can be built from one body.  A kernel is a
 * small .S file that defines KERNEL (the symbol), includes one g_*.inc (the
 * representation) and then this file.
 *
 * Everything the reference kernel's header says about steps, pushes, frame
 * slots and the unreachable FLOW color path applies here unchanged.  The
 * differences from the reference kernel's body are bookkeeping only:
 *
 *   - gen is kept as a pointer into the target (r9) and compared with the end
 *     of the target in memory, which frees one register;
 *   - the stack limit, the node budget and the state pointer live in four
 *     stack slots instead of registers;
 *   - the target bit for a CTRL INIT is fetched before the FLOW INIT that
 *     precedes it rather than after, so that a representation can prepare
 *     whatever it derives from the bit (G_TARGET_HOOK) off the critical path.
 *
 * Registers owned by this file
 *   esi  fp                         r10  steps remaining before the limit
 *   edx  cp                         r11d node count (Gn)
 *   r9   &target[gen]               r12  top frame
 *   ebp  target bit at r9           r14  deepest r12 so far
 *   rax, rcx, rdi  scratch; rcx carries a link "slot designator" where noted
 * Registers owned by the representation: rbx, r13, r15, r8.
 *
 * What a representation must define (see g_b4.inc for the simplest one)
 *   G_ENTER              rdi = state.  Set up; graph = one node, all undecided.
 *   G_EXPORT             rdi = state.  Write nodes 0..r11-1 to state->nodes in
 *                        the 4-byte layout.  Preserve rdi, r10, r11, r14.
 *   G_FP_D u             eax = color of node fp (0 or 1); jump to u if undecided.
 *   G_LINK_CP u          in eax = d.  eax = link d of node cp.  If undecided,
 *                        jump to u with rcx = slot designator of that link.
 *   G_LINK_FP u          the same for node fp.
 *   G_TARGET_HOOK        ebp = target bit was just loaded.
 *   G_COLOR_TEST ok, set node in eax: jump to ok if its color equals ebp, to
 *                        set if it is undecided, fall through otherwise.
 *                        Preserve eax, rcx, ebp.
 *   G_COLOR_SET_T        color of node eax (undecided) = ebp.  Preserve eax, ebp.
 *   G_COLOR_CLR_CP       color of node edx = undecided.
 *   G_FP_WHITE / G_FP_BLACK / G_FP_CLR
 *                        color of node fp: undecided -> white -> black -> undecided.
 *   G_LINK_SET_IT        link at slot rcx = eax.  Preserve eax, rcx, ebp.
 *   G_LINK_CLR           link at slot rcx = undecided.  Preserve rcx.
 *   G_LINK_SET_NEW       link at slot rcx (undecided) = r11, and node r11
 *                        becomes a fresh all-undecided node.  Preserve rcx.
 *   G_LINK_GET           eax = link at slot rcx (decided).  Preserve rcx.
 * Macros may use rdi freely, and numeric labels 40-49.
 *
 * Optional: CB_BULK (needs BMI1/BMI2 and a representation whose colors are
 * the bitmaps of g_colbm.inc).  In a link loop, the attempts that are certain
 * to fail at once are not executed one by one: a bit scan finds the next
 * node worth trying and the failures before it are accounted for
 * arithmetically, with exactly the steps and stack depth they would have
 * used.  Whenever that could differ from doing them singly (the step limit
 * or the stack limit is near, or an existing node is uncolored) the per-node
 * code below is used instead.  The representation supplies
 *   G_BULK_ANY_UNCOLORED u   jump to u if an existing node is uncolored
 *   G_BULK_FLOW_WORK         rdi = existing nodes n for which CTRL(n, cp)
 *                            is not certain to fail in its INIT; r8 = nodes
 *                            of the target color; preserve eax, ebp, r8.
 *                            It runs once per FLOW link loop, before the
 *                            first attempt, when the link being tried is
 *                            still undecided: if that link is one of cp's
 *                            own, the nodes that would use it are therefore
 *                            never skipped.  Budgets up to 8 only.
 */
#include "skel_defs.inc"

    .intel_syntax noprefix
    .text

.macro STEP
    sub     r10, 1
    jb      .Llimit_steps
.endm

.macro STEPS n
    sub     r10, \n
    jb      .Llimit_steps
.endm

.macro PUSHCHK
    cmp     r12, r14
    jae     7f
8:
    .subsection 1
7:  cmp     r12, L_LIMIT
    je      .Loverflow
    lea     r14, [r12 + FRAME]
    jmp     8b
    .previous
.endm

#ifdef CB_FRAME8

/* Frame slot (8 bytes): +0 resume position as a 32-bit offset from the
 * start of the kernel, +4 one saved 32-bit value. */
.macro CALLF target
    .att_syntax prefix                  /* (label difference as an immediate) */
    movl    $(6f - .Lbase), (%r12)
    .intel_syntax noprefix
    add     r12, 8
    jmp     \target
6:
.endm

.macro RETF
    sub     r12, 8
    mov     eax, dword ptr [r12]
    lea     rdi, [rip + .Lbase]
    add     rax, rdi
    jmp     rax
.endm

.macro JRET cc
    j\cc    7f
    .subsection 1
7:  sub     r12, 8
    mov     eax, dword ptr [r12]
    lea     rdi, [rip + .Lbase]
    add     rax, rdi
    jmp     rax
    .previous
.endm

.macro SAVE reg                         /* reg is dx, si or cx */
    mov     dword ptr [r12 + 4], e\reg
.endm

.macro RESTORE reg
    mov     e\reg, dword ptr [r12 + 4]
.endm

#else

/* Frame slot (16 bytes): +0 address to resume at, +8 one saved value. */
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

.macro JRET cc
    j\cc    7f
    .subsection 1
7:  sub     r12, 16
    jmp     qword ptr [r12]
    .previous
.endm

.macro SAVE reg                         /* reg is dx, si or cx */
    mov     qword ptr [r12 + 8], r\reg
.endm

.macro RESTORE reg
    mov     r\reg, qword ptr [r12 + 8]
.endm

#endif

.macro TARGET_BIT
    movzx   ebp, byte ptr [r9]
    and     ebp, 1
    G_TARGET_HOOK
.endm

/* push(CTRL, fp, cp) has been checked; the target bit is in ebp.  Do the
 * frame's INIT here; push it only if it survives. */
.macro PEEK_CTRL
    STEP                                        /* CTRL INIT */
    G_FP_D  21f
    G_LINK_CP 22f
    G_COLOR_TEST 23f, 24f
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

/* Rest of a CTRL INIT whose output bit matched: eax = nxt, ebp = the bit,
 * which is also the color of nxt.  \undo = 1 if this frame colored it. */
.macro MATCH_TAIL undo
    inc     r9                                  /* ++gen == target_len ? */
    cmp     r9, L_TEND
    je      .Lsolved
    SAVE    dx
    mov     edx, eax                            /* cp = nxt */
    mov     eax, ebp                            /* d of the FLOW INIT below */
    TARGET_BIT                                  /* for the CTRL INIT below */
    PUSHCHK                                     /* push(FLOW, fp, nxt) */
    add     r12, FRAME                          /* now in the FLOW frame's slot */
    STEP                                        /* FLOW INIT */
    G_LINK_FP 31f
    SAVE    si
    mov     esi, eax                            /* fp = nxt */
    PUSHCHK                                     /* push(CTRL, nxt, cp) */
    PEEK_CTRL
    STEPS   2                                   /* SIMPLE_R, then CTRL MATCH */
    RESTORE si
    sub     r12, FRAME                          /* FLOW frame popped */
32:
    .if \undo
    G_COLOR_CLR_CP                              /* edx is still nxt */
    .endif
    dec     r9
    RESTORE dx
    RETF
    .subsection 1
31: sub     r12, FRAME                          /* link undecided: a real FLOW frame */
    CALLF   .Lflow_link
    STEP                                        /* CTRL MATCH */
    jmp     32b
    .previous
.endm

    .globl  KERNEL
    .type   KERNEL, @function
KERNEL:
.Lbase:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, LOCALS

    mov     L_STATE, rdi
    mov     L_MAX, esi
    mov     r9, qword ptr [rdi + CB_ST_TARGET]
    mov     rax, qword ptr [rdi + CB_ST_TARGET_LEN]
    add     rax, r9
    mov     L_TEND, rax
    mov     r10, qword ptr [rdi + CB_ST_STEP_LIMIT]
    mov     eax, dword ptr [rdi + CB_ST_MAX_STACK]
    lea     r12, [rdi + CB_ST_FRAMES]
    shl     rax, FRAME_SHIFT
    add     rax, r12
    mov     L_LIMIT, rax                        /* r12 at full depth */
    mov     r14, r12
    G_ENTER
    mov     r11d, 1                             /* Gn = 1 */
    xor     esi, esi
    xor     edx, edx
    PUSHCHK                                     /* push(CTRL, 0, 0) */
    CALLF   .Lctrl_init
    mov     eax, CB_RES_UNSAT                   /* stack empty */
    xor     ecx, ecx
    jmp     .Lfinish

/* ---- CTRL(fp = esi, cp = edx) ------------------------------------------ */

    .balign 16
.Lctrl_init:
    STEP                                        /* CTRL INIT */
    G_FP_D  .Lctrl_color
    G_LINK_CP .Lctrl_link
    TARGET_BIT
    G_COLOR_TEST .Lmatch_ok, .Lmatch_set
    RETF                                        /* wrong color: pop */

    .balign 16
.Lmatch_ok:                                     /* C: set = 0 */
    MATCH_TAIL 0

    .balign 16
.Lmatch_set:                                    /* C: set = 1 */
    G_COLOR_SET_T
    MATCH_TAIL 1

.Lctrl_color:
    STEP                                        /* LOOP_C, it = 0 */
    G_FP_WHITE
    PUSHCHK
    CALLF   .Lctrl_init
    STEPS   2                                   /* LOOP_C_R, then LOOP_C, it = 1 */
    G_FP_BLACK
    PUSHCHK
    CALLF   .Lctrl_init
    STEPS   2                                   /* LOOP_C_R, then LOOP_C, it = 2: pop */
    G_FP_CLR
    RETF

    .balign 16
.Lctrl_link:                                    /* rcx = slot of G[cp].links[d] */
#ifdef CB_BULK_CTRL
    SAVE    cx                                  /* kept in the frame for the whole loop */
    xor     eax, eax                            /* it = 0 */
    TARGET_BIT                                  /* same bit for every attempt */
    STEP                                        /* LOOP_L */
    G_BULK_ANY_UNCOLORED .Lctrl_link_next
.Lctrl_bulk:                                    /* eax = it, its LOOP_L step counted, r8 = nodes of the right color */
    cmp     eax, r11d
    jae     .Lctrl_bulk_exhausted
    cmp     r10, CB_BULK_MARGIN
    jb      .Lctrl_bulk_slow
    cmp     r12, L_LIMIT
    je      .Lctrl_bulk_slow
    shrx    rdi, r8, rax
    tzcnt   rcx, rdi                            /* nodes of the wrong color before the next right one */
    jc      .Lctrl_bulk_none
    lea     rdi, [rcx + rcx*2]                  /* each: child INIT, LOOP_L_R, LOOP_L */
    sub     r10, rdi
    add     eax, ecx                            /* it = the next node of the right color */
    RESTORE cx
    G_LINK_SET_IT
    PUSHCHK                                     /* push(CTRL, fp, cp); the skipped ones pushed to the same depth */
    STEP                                        /* child: CTRL INIT, color matches */
    CALLF   .Lmatch_ok
    STEPS   2                                   /* LOOP_L_R, then LOOP_L */
    RESTORE cx
    G_LINK_GET
    inc     eax
    TARGET_BIT
    jmp     .Lctrl_bulk
.Lctrl_bulk_none:                               /* every node left has the wrong color */
    mov     ecx, r11d
    sub     ecx, eax
    lea     rdi, [rcx + rcx*2]
    sub     r10, rdi
    PUSHCHK
.Lctrl_bulk_exhausted:
    RESTORE cx
    jmp     .Lctrl_try_n
.Lctrl_bulk_slow:
    RESTORE cx
#else
    xor     eax, eax                            /* it = 0 */
    TARGET_BIT                                  /* same bit for every attempt */
    STEP                                        /* LOOP_L */
#endif
.Lctrl_link_next:                               /* (this LOOP_L step is already counted) */
    cmp     eax, r11d
    jae     .Lctrl_try_n
    G_LINK_SET_IT
    PUSHCHK                                     /* push(CTRL, fp, cp) */
    STEP                                        /* child: CTRL INIT, nxt = it */
    G_COLOR_TEST .Lctrl_link_ok, .Lctrl_link_set
    inc     eax                                 /* wrong color: the child pops at once */
    STEPS   2                                   /* LOOP_L_R, then LOOP_L */
    jmp     .Lctrl_link_next

.Lctrl_link_ok:
    SAVE    cx
    CALLF   .Lmatch_ok
    jmp     .Lctrl_link_back
.Lctrl_link_set:
    SAVE    cx
    CALLF   .Lmatch_set
.Lctrl_link_back:
    STEPS   2                                   /* LOOP_L_R, then LOOP_L */
    RESTORE cx
    G_LINK_GET                                  /* it, read back from the link */
    inc     eax
    TARGET_BIT
    jmp     .Lctrl_link_next

.Lctrl_try_n:
    G_LINK_CLR                                  /* LOOP_L, it == Gn: link = UNDECIDED */
    STEP                                        /* TRY_N */
    cmp     r11d, L_MAX
    JRET    ae                                  /* budget exhausted: pop */
    G_LINK_SET_NEW
    mov     eax, r11d
    inc     r11d
    PUSHCHK                                     /* push(CTRL, fp, cp) */
    SAVE    cx
    STEP                                        /* child: CTRL INIT, nxt = new node */
    TARGET_BIT
    CALLF   .Lmatch_set
    STEP                                        /* TRY_N_R */
    RESTORE cx
    dec     r11d
    G_LINK_CLR
    RETF

/* ---- FLOW(fp = esi, cp = edx) ------------------------------------------ */

    .balign 16
.Lflow_simple:                                  /* child of a FLOW TRY_N: eax = nxt */
    SAVE    si
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, nxt, cp) */
    CALLF   .Lctrl_init
    STEP                                        /* SIMPLE_R */
    RESTORE si
    RETF

    .balign 16
.Lflow_link:                                    /* rcx = slot of G[fp].links[d] */
#ifdef CB_BULK_FLOW
    /* Which nodes need a real attempt depends only on state that is the same
     * at the start of every iteration of this loop, so it is worked out once
     * and kept in the frame above the slot designator (budgets up to 8:
     * eight bits). */
    SAVE    cx
    xor     eax, eax
    STEP                                        /* LOOP_L */
    TARGET_BIT
    G_BULK_FLOW_WORK                            /* rdi = nodes that need a real attempt */
    RESTORE cx
    shl     rdi, 8
    or      rcx, rdi
    SAVE    cx                                  /* slot | nodes << 8 */
.Lflow_bulk:                                    /* eax = it, its LOOP_L step counted */
    cmp     eax, r11d
    jae     .Lflow_bulk_exhausted
    cmp     r10, CB_BULK_MARGIN
    jb      .Lflow_bulk_slow
    lea     rdi, [r12 + FRAME]                  /* a skipped attempt pushes two frames */
    cmp     rdi, L_LIMIT
    jae     .Lflow_bulk_slow
    RESTORE cx
    mov     rdi, rcx
    shr     rdi, 8
    shrx    rdi, rdi, rax
    tzcnt   rcx, rdi                            /* certain failures before the next real attempt */
    jc      .Lflow_bulk_none
    lea     rdi, [rcx + rcx*4]                  /* each: FLOW INIT, CTRL INIT, SIMPLE_R, LOOP_L_R, LOOP_L */
    sub     r10, rdi
    lea     rdi, [r12 + 2*FRAME]                /* depth their pushes reached, if there were any */
    test    ecx, ecx
    cmovz   rdi, r14
    cmp     rdi, r14
    cmova   r14, rdi
    add     eax, ecx
    RESTORE cx
    movzx   ecx, cl
    G_LINK_SET_IT
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    add     r12, FRAME                          /* the child FLOW frame's slot */
    STEP                                        /* child: FLOW INIT, nxt = it */
    SAVE    si
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, it, cp) */
    TARGET_BIT
    PEEK_CTRL
    STEPS   3                                   /* child SIMPLE_R, LOOP_L_R, LOOP_L */
    RESTORE si
    sub     r12, FRAME
    RESTORE cx
    movzx   ecx, cl
    G_LINK_GET
    inc     eax
    jmp     .Lflow_bulk
.Lflow_bulk_none:                               /* every node left is a certain failure */
    mov     ecx, r11d
    sub     ecx, eax
    lea     rdi, [rcx + rcx*4]
    sub     r10, rdi
    lea     rdi, [r12 + 2*FRAME]
    cmp     rdi, r14
    cmova   r14, rdi
.Lflow_bulk_exhausted:
    RESTORE cx
    movzx   ecx, cl
    jmp     .Lflow_try_n
.Lflow_bulk_slow:
    RESTORE cx
    movzx   ecx, cl
#else
    xor     eax, eax
    STEP                                        /* LOOP_L */
#endif
.Lflow_link_next:                               /* (this LOOP_L step is already counted) */
    cmp     eax, r11d
    jae     .Lflow_try_n
    G_LINK_SET_IT
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    SAVE    cx
    add     r12, FRAME                          /* the child FLOW frame's slot */
    STEP                                        /* child: FLOW INIT, nxt = it */
    SAVE    si
    mov     esi, eax
    PUSHCHK                                     /* push(CTRL, it, cp) */
    TARGET_BIT
    PEEK_CTRL
    STEPS   3                                   /* child SIMPLE_R, LOOP_L_R, LOOP_L */
    RESTORE si
    sub     r12, FRAME                          /* child FLOW frame popped */
    RESTORE cx
    G_LINK_GET                                  /* it, read back from the link */
    inc     eax
    jmp     .Lflow_link_next

.Lflow_try_n:
    G_LINK_CLR                                  /* LOOP_L, it == Gn: link = UNDECIDED */
    STEP                                        /* TRY_N */
    cmp     r11d, L_MAX
    JRET    ae
    G_LINK_SET_NEW
    mov     eax, r11d
    inc     r11d
    PUSHCHK                                     /* push(FLOW, fp, cp) */
    SAVE    cx
    STEP                                        /* child: FLOW INIT, nxt = new node */
    CALLF   .Lflow_simple
    STEP                                        /* TRY_N_R */
    RESTORE cx
    dec     r11d
    G_LINK_CLR
    RETF

/* ---- exits ------------------------------------------------------------- */

.Lsolved:
    mov     rdi, L_STATE
    G_EXPORT
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
    mov     rdi, L_STATE
    mov     dword ptr [rdi + CB_ST_LIMIT_HIT], ecx
    mov     rcx, qword ptr [rdi + CB_ST_STEP_LIMIT]
    sub     rcx, r10
    mov     qword ptr [rdi + CB_ST_STEPS], rcx
    mov     dword ptr [rdi + CB_ST_NODE_COUNT], r11d
    lea     rcx, [rdi + CB_ST_FRAMES]
    sub     r14, rcx
    shr     r14, FRAME_SHIFT
    mov     dword ptr [rdi + CB_ST_PEAK_STACK], r14d
    add     rsp, LOCALS
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

    .subsection 1
.Lkernel_end:
    .previous
    .size   KERNEL, .Lkernel_end - KERNEL

    .section .note.GNU-stack, "", @progbits
