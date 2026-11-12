/*
 * Colors as two bitmaps in registers, one bit per node (up to 64 nodes):
 *   r13  bit n set = node n's color is decided
 *   r15  bit n set = node n is BLACK (only meaningful where r13 is set;
 *        kept clear otherwise)
 *   r8   nodes whose color is decided and equal to the current target bit,
 *        recomputed whenever the target bit is loaded
 * Reading a color is a register bit test instead of a dependent memory load,
 * and "does this node have the target color" is a single test against r8.
 */
#ifndef CB_OWN_FP_D
.macro G_FP_D u
    bt      r13, rsi
    jnc     \u
    xor     eax, eax
    bt      r15, rsi
    setc    al
.endm
#endif

.macro G_TARGET_HOOK                    /* r8 = r13 & (ebp ? r15 : ~r15) */
    lea     r8, [rbp - 1]
    xor     r8, r15
    and     r8, r13
.endm

.macro G_COLOR_TEST ok, set
    bt      r8, rax
    jc      \ok
    bt      r13, rax
    jnc     \set
.endm

.macro G_COLOR_SET_T
    bts     r13, rax
#ifdef CB_BMI2
    shlx    rdi, rbp, rax
#else
    mov     rdi, rbp
    mov     ecx, eax
    shl     rdi, cl
#endif
    or      r15, rdi
.endm

.macro G_COLOR_CLR_CP
    btr     r13, rdx
    btr     r15, rdx
.endm

.macro G_FP_WHITE
    bts     r13, rsi
.endm

.macro G_FP_BLACK
    bts     r15, rsi
.endm

.macro G_FP_CLR
    btr     r13, rsi
    btr     r15, rsi
.endm

/* eax = color byte of node rcx in the 4-byte layout (0, 1 or 0xff). */
.macro G_COLOR_BYTE
    mov     eax, 255
    bt      r13, rcx
    jnc     49f
    xor     eax, eax
    bt      r15, rcx
    setc    al
49:
.endm

#ifdef CB_BULK_ANY
/* Jump to \u if any of nodes 0..r11-1 has no color yet. */
.macro G_BULK_ANY_UNCOLORED u
    mov     rdi, r13
    not     rdi
    bzhi    rdi, rdi, r11
    test    rdi, rdi
    jnz     \u
.endm
#endif
