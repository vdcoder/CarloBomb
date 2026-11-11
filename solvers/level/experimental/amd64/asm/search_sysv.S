/*
 * CarloBomb LevelSolver search kernel, AMD64, System V ABI, GNU as.
 *
 *     int cb_amd64_search(CbAsmState *state, int max_nodes);
 *
 * Stage 1: direct translation of search() in level_solver.c.
 *
 * One central loop pops nothing and pushes nothing by itself: it counts a
 * step, loads the top frame and jumps through a table indexed by
 * (function, state), exactly like the C `while`/`switch`.  Every handler
 * below corresponds to one `case` of the C source.
 *
 * Frame (8 bytes):  +0 state index   +1 fp   +2 cp   +3 it
 *                   +4 dir           +5 nxt  +6 set
 * Node  (4 bytes):  +0 color  +1 link[0]  +2 link[1]      (-1 = undecided)
 *
 * Registers:
 *   rbx  node array                r12  stack top (one past the top frame)
 *   r15  frame array               rbp  end of usable frame array
 *   r14  highest stack top so far  r13d node budget
 *   r11d node count (Gn)           r10  steps remaining before the limit
 *   r8   end of target             r9   gen - target_len (counts up to 0)
 *   esi  fp of the top frame       edx  cp of the top frame
 *   rax, rcx, rdi  scratch
 *
 * Baseline x86-64 instructions only.  No calls, no stack use after the
 * prologue.
 */
#include "asm_layout.h"

    .intel_syntax noprefix
    .text

/* state indices: function * 9 + state */
#define C_INIT 0
#define C_LOOP_C 1
#define C_LOOP_C_R 2
#define C_LOOP_L 3
#define C_LOOP_L_R 4
#define C_TRY_N 5
#define C_TRY_N_R 6
#define C_MATCH 7
#define F_INIT 9
#define F_LOOP_C 10
#define F_LOOP_C_R 11
#define F_LOOP_L 12
#define F_LOOP_L_R 13
#define F_TRY_N 14
#define F_TRY_N_R 15
#define F_SIMPLE_R 17

#define FR_ST   byte ptr [r12 - 8]
#define FR_IT   byte ptr [r12 - 5]
#define FR_DIR  byte ptr [r12 - 4]
#define FR_NXT  byte ptr [r12 - 3]
#define FR_SET  byte ptr [r12 - 2]

/* push(fn, fp, cp), then back to the loop */
.macro PUSHF st, fp, cp
    cmp     r12, rbp
    je      .Loverflow
    mov     byte ptr [r12], \st
    mov     byte ptr [r12 + 1], \fp
    mov     byte ptr [r12 + 2], \cp
    add     r12, 8
    cmp     r12, r14
    cmova   r14, r12
    jmp     .Lloop
.endm

    .globl  cb_amd64_search
    .type   cb_amd64_search, @function
cb_amd64_search:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    push    rdi                         /* state pointer, reloaded at exit */

    mov     r13d, esi
    lea     rbx, [rdi + CB_ST_NODES]
    lea     r15, [rdi + CB_ST_FRAMES]
    mov     eax, dword ptr [rdi + CB_ST_MAX_STACK]
    lea     rbp, [r15 + rax*8]
    mov     r8, qword ptr [rdi + CB_ST_TARGET]
    mov     r9, qword ptr [rdi + CB_ST_TARGET_LEN]
    add     r8, r9
    neg     r9
    mov     r10, qword ptr [rdi + CB_ST_STEP_LIMIT]

    mov     dword ptr [rbx], -1         /* G[0] = all undecided */
    mov     r11d, 1                     /* Gn = 1 */
    mov     r12, r15                    /* Sp = 0 */
    mov     r14, r15
    xor     esi, esi
    xor     edx, edx
    PUSHF   C_INIT, sil, dl

.Lpop:
    sub     r12, 8
.Lloop:
    cmp     r12, r15
    je      .Lunsat
    sub     r10, 1
    jb      .Llimit_steps
    movzx   eax, FR_ST
    movzx   esi, byte ptr [r12 - 7]
    movzx   edx, byte ptr [r12 - 6]
    lea     rcx, [rip + .Ltable]
    movsxd  rax, dword ptr [rcx + rax*4]
    add     rax, rcx
    jmp     rax

/* ---- CTRL ------------------------------------------------------------- */

.Lc_init:
    movsx   eax, byte ptr [rbx + rsi*4]         /* d = G[fp].color */
    test    eax, eax
    js      1f
    lea     rcx, [rdx*4 + rax]
    movsx   ecx, byte ptr [rbx + rcx + 1]       /* nxt = G[cp].links[d] */
    test    ecx, ecx
    js      2f
    movzx   edi, byte ptr [r8 + r9]             /* target(gen) */
    and     edi, 1
    movsx   eax, byte ptr [rbx + rcx*4]         /* cc = G[nxt].color */
    test    eax, eax
    js      3f
    cmp     eax, edi
    jne     .Lpop
    mov     FR_SET, 0
    jmp     4f
3:  mov     byte ptr [rbx + rcx*4], dil
    mov     FR_SET, 1
4:  inc     r9                                  /* ++gen == target_len */
    jz      .Lsolved
    mov     FR_NXT, cl
    mov     FR_ST, C_MATCH
    PUSHF   F_INIT, sil, cl
1:  mov     FR_ST, C_LOOP_C
    mov     FR_IT, 0
    jmp     .Lloop
2:  mov     FR_DIR, al
    mov     FR_ST, C_LOOP_L
    mov     FR_IT, 0
    jmp     .Lloop

.Lc_loop_c:
    movzx   eax, FR_IT
    cmp     eax, 2
    jae     .Lpop
    mov     byte ptr [rbx + rsi*4], al
    inc     eax
    mov     FR_IT, al
    mov     FR_ST, C_LOOP_C_R
    PUSHF   C_INIT, sil, dl

.Lc_loop_c_r:
    mov     byte ptr [rbx + rsi*4], -1
    mov     FR_ST, C_LOOP_C
    jmp     .Lloop

.Lc_loop_l:
    movzx   ecx, FR_DIR
    lea     rcx, [rdx*4 + rcx]                  /* &G[cp].links[dir] - 1 */
    movzx   eax, FR_IT
    cmp     eax, r11d
    jae     1f
    mov     byte ptr [rbx + rcx + 1], al
    inc     eax
    mov     FR_IT, al
    mov     FR_ST, C_LOOP_L_R
    PUSHF   C_INIT, sil, dl
1:  mov     byte ptr [rbx + rcx + 1], -1
    mov     FR_ST, C_TRY_N
    jmp     .Lloop

.Lc_loop_l_r:
    movzx   ecx, FR_DIR
    lea     rcx, [rdx*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], -1
    mov     FR_ST, C_LOOP_L
    jmp     .Lloop

.Lc_try_n:
    cmp     r11d, r13d
    jae     .Lpop
    movzx   ecx, FR_DIR
    lea     rcx, [rdx*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], r11b
    mov     dword ptr [rbx + r11*4], -1
    inc     r11d
    mov     FR_ST, C_TRY_N_R
    PUSHF   C_INIT, sil, dl

.Lc_try_n_r:
    dec     r11d
    movzx   ecx, FR_DIR
    lea     rcx, [rdx*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], -1
    jmp     .Lpop

.Lc_match:
    cmp     FR_SET, 0
    je      1f
    movzx   eax, FR_NXT
    mov     byte ptr [rbx + rax*4], -1
1:  dec     r9
    jmp     .Lpop

/* ---- FLOW ------------------------------------------------------------- */

.Lf_init:
    movsx   eax, byte ptr [rbx + rdx*4]         /* d = G[cp].color */
    test    eax, eax
    js      1f
    lea     rcx, [rsi*4 + rax]
    movsx   ecx, byte ptr [rbx + rcx + 1]       /* nxt = G[fp].links[d] */
    test    ecx, ecx
    js      2f
    mov     FR_ST, F_SIMPLE_R
    PUSHF   C_INIT, cl, dl
1:  mov     FR_ST, F_LOOP_C
    mov     FR_IT, 0
    jmp     .Lloop
2:  mov     FR_DIR, al
    mov     FR_ST, F_LOOP_L
    mov     FR_IT, 0
    jmp     .Lloop

.Lf_loop_c:
    movzx   eax, FR_IT
    cmp     eax, 2
    jae     .Lpop
    mov     byte ptr [rbx + rdx*4], al
    inc     eax
    mov     FR_IT, al
    mov     FR_ST, F_LOOP_C_R
    PUSHF   F_INIT, sil, dl

.Lf_loop_c_r:
    mov     byte ptr [rbx + rdx*4], -1
    mov     FR_ST, F_LOOP_C
    jmp     .Lloop

.Lf_loop_l:
    movzx   ecx, FR_DIR
    lea     rcx, [rsi*4 + rcx]                  /* &G[fp].links[dir] - 1 */
    movzx   eax, FR_IT
    cmp     eax, r11d
    jae     1f
    mov     byte ptr [rbx + rcx + 1], al
    inc     eax
    mov     FR_IT, al
    mov     FR_ST, F_LOOP_L_R
    PUSHF   F_INIT, sil, dl
1:  mov     byte ptr [rbx + rcx + 1], -1
    mov     FR_ST, F_TRY_N
    jmp     .Lloop

.Lf_loop_l_r:
    movzx   ecx, FR_DIR
    lea     rcx, [rsi*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], -1
    mov     FR_ST, F_LOOP_L
    jmp     .Lloop

.Lf_try_n:
    cmp     r11d, r13d
    jae     .Lpop
    movzx   ecx, FR_DIR
    lea     rcx, [rsi*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], r11b
    mov     dword ptr [rbx + r11*4], -1
    inc     r11d
    mov     FR_ST, F_TRY_N_R
    PUSHF   F_INIT, sil, dl

.Lf_try_n_r:
    dec     r11d
    movzx   ecx, FR_DIR
    lea     rcx, [rsi*4 + rcx]
    mov     byte ptr [rbx + rcx + 1], -1
    jmp     .Lpop

/* ---- exits ------------------------------------------------------------ */

.Lsolved:
    mov     eax, CB_RES_SOLVED
    xor     ecx, ecx                            /* CB_LIM_NONE */
    jmp     .Lfinish
.Lunsat:
    mov     eax, CB_RES_UNSAT
    xor     ecx, ecx
    jmp     .Lfinish
.Llimit_steps:
    xor     r10d, r10d                          /* remaining = 0 */
    mov     eax, CB_RES_LIMIT
    mov     ecx, CB_LIM_STEPS
    jmp     .Lfinish
.Loverflow:
    mov     eax, CB_RES_LIMIT
    mov     ecx, CB_LIM_STACK
.Lfinish:
    pop     rdi
    mov     dword ptr [rdi + CB_ST_LIMIT_HIT], ecx
    mov     rcx, qword ptr [rdi + CB_ST_STEP_LIMIT]
    sub     rcx, r10
    mov     qword ptr [rdi + CB_ST_STEPS], rcx
    sub     r14, r15
    shr     r14, 3
    mov     dword ptr [rdi + CB_ST_PEAK_STACK], r14d
    mov     dword ptr [rdi + CB_ST_NODE_COUNT], r11d
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

    .section .rodata
    .balign 4
.Ltable:
    .long   .Lc_init - .Ltable
    .long   .Lc_loop_c - .Ltable
    .long   .Lc_loop_c_r - .Ltable
    .long   .Lc_loop_l - .Ltable
    .long   .Lc_loop_l_r - .Ltable
    .long   .Lc_try_n - .Ltable
    .long   .Lc_try_n_r - .Ltable
    .long   .Lc_match - .Ltable
    .long   .Lpop - .Ltable                     /* CTRL has no SIMPLE_R */
    .long   .Lf_init - .Ltable
    .long   .Lf_loop_c - .Ltable
    .long   .Lf_loop_c_r - .Ltable
    .long   .Lf_loop_l - .Ltable
    .long   .Lf_loop_l_r - .Ltable
    .long   .Lf_try_n - .Ltable
    .long   .Lf_try_n_r - .Ltable
    .long   .Lpop - .Ltable                     /* FLOW has no MATCH */
    .long   .Lpop - .Ltable                     /* F_SIMPLE_R: Sp-- */

    .text
    .size   cb_amd64_search, . - cb_amd64_search
    .section .note.GNU-stack, "", @progbits
