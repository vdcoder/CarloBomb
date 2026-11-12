/*
 * Representation family "p16": one 16-bit word per node,
 *
 *     bits 0-6   link 0   (0-63 = node, 64 = undecided)
 *     bits 7-13  link 1
 *     bits 14-15 color    (0 = WHITE, 1 = BLACK, 2 = undecided)
 *
 * All field logic is shared.  Where the words live is chosen by one of
 *   CB_P16_MEM  an array in memory; rbx = its address.  Any budget.
 *   CB_P16_GPR  four words in rbx.  Budgets up to 4.  Needs BMI2.
 *   CB_P16_XMM  eight words in xmm0.  Budgets up to 8.  Needs AVX-512BW/VL
 *               (VPERMW to read a word, masked VPBROADCASTW to write one)
 *               and BMI2.
 * through W_GET (read word) and W_PUT (write word from r8w).
 *
 * A slot designator is (node << 8) | shift, shift being 0 or 7, so that cl
 * is the field's shift count and ch the node.
 * Scratch used here beyond rdi: r8, r13, r15.
 */
#include "skel_defs.inc"

#define P16_BLANK 0xA040                /* both links 64, color 2 */

#if defined(CB_P16_MEM)

.macro W_GET dst, idx
    movzx   \dst, word ptr [rbx + \idx*2]
.endm

.macro W_PUT idx
    mov     word ptr [rbx + \idx*2], r8w
.endm

.macro G_ENTER
    lea     rbx, [rdi + CB_ST_NODES]
    mov     word ptr [rbx], P16_BLANK
.endm

#elif defined(CB_P16_GPR)

.macro W_GET dst, idx
    mov     r15, \idx
    shl     r15d, 4
    shrx    r15, rbx, r15
    movzx   \dst, r15w
.endm

.macro W_PUT idx
    mov     r15, \idx
    shl     r15d, 4
    mov     r13d, 0xFFFF
    shlx    r13, r13, r15
    andn    rbx, r13, rbx
    movzx   r8d, r8w
    shlx    r8, r8, r15
    or      rbx, r8
.endm

.macro G_ENTER
    movabs  rbx, 0xA040A040A040A040
.endm

#elif defined(CB_P16_XMM)

.macro W_GET dst, idx
    vmovq   xmm1, \idx
    vpermw  xmm1, xmm1, xmm0
    vmovd   r15d, xmm1
    movzx   \dst, r15w
.endm

.macro W_PUT idx
    mov     r13d, 1
    shlx    r13, r13, \idx
    kmovd   k1, r13d
    vpbroadcastw xmm0{k1}, r8d
.endm

.macro G_ENTER
    mov     eax, P16_BLANK
    vpbroadcastw xmm0, eax
.endm

#else
#error "define CB_P16_MEM, CB_P16_GPR or CB_P16_XMM"
#endif

/* Unpack nodes 0..r11-1 into the 4-byte layout, last node first (for the
 * memory variant the two arrays share storage). */
.macro P16_FIELD_TO_BYTE reg32, reg8, limit    /* value >= limit -> 0xff */
    cmp     \reg32, \limit
    sbb     r8d, r8d
    not     r8d
    or      \reg32, r8d
    movzx   \reg32, \reg8
.endm

.macro G_EXPORT
    lea     r9, [rdi + CB_ST_NODES]
    mov     ecx, r11d
47: sub     ecx, 1
    js      48f
    W_GET   eax, rcx
    mov     edx, eax
    shr     edx, 14
    P16_FIELD_TO_BYTE edx, dl, 2                /* color */
    mov     esi, eax
    and     esi, 127
    P16_FIELD_TO_BYTE esi, sil, 64              /* link 0 */
    shl     esi, 8
    or      edx, esi
    shr     eax, 7
    and     eax, 127
    P16_FIELD_TO_BYTE eax, al, 64               /* link 1 */
    shl     eax, 16
    or      edx, eax
    mov     dword ptr [r9 + rcx*4], edx
    jmp     47b
48:
.endm

.macro G_FP_D u
    W_GET   eax, rsi
    test    eax, 0x8000
    jnz     \u
    shr     eax, 14
.endm

/* eax = link edi of the node whose word is in eax; if undecided, build the
 * slot designator for (\node32, edi) out of line and go to \u. */
.macro P16_LINK_SELECT node32, u
    mov     ecx, eax
    shr     ecx, 7
    test    edi, edi
    cmovnz  eax, ecx
    and     eax, 127
    cmp     eax, 64
    jae     41f
    .subsection 1
41: lea     ecx, [rdi*8]
    sub     ecx, edi                            /* shift = 7*d */
    mov     edi, \node32
    shl     edi, 8
    or      ecx, edi
    jmp     \u
    .previous
.endm

.macro G_LINK_CP u
    mov     edi, eax
    W_GET   eax, rdx
    P16_LINK_SELECT edx, \u
.endm

.macro G_LINK_FP u
    mov     edi, eax
    W_GET   eax, rsi
    P16_LINK_SELECT esi, \u
.endm

.macro G_TARGET_HOOK
.endm

.macro G_COLOR_TEST ok, set
    W_GET   edi, rax
    shr     edi, 14
    cmp     edi, ebp
    je      \ok
    cmp     edi, 2
    jae     \set
.endm

.macro G_COLOR_SET_T
    W_GET   r8d, rax
    and     r8d, 0x3FFF
    mov     edi, ebp
    shl     edi, 14
    or      r8d, edi
    W_PUT   rax
.endm

.macro G_COLOR_CLR_CP
    W_GET   r8d, rdx
    and     r8d, 0x3FFF
    or      r8d, 0x8000
    W_PUT   rdx
.endm

.macro G_FP_WHITE
    W_GET   r8d, rsi
    and     r8d, 0x3FFF
    W_PUT   rsi
.endm

.macro G_FP_BLACK
    W_GET   r8d, rsi
    or      r8d, 0x4000
    W_PUT   rsi
.endm

.macro G_FP_CLR
    W_GET   r8d, rsi
    and     r8d, 0x3FFF
    or      r8d, 0x8000
    W_PUT   rsi
.endm

/* r8d = the slot's word with the slot's field cleared; edi = its node. */
.macro P16_SLOT_CLEARED
    movzx   edi, ch
    W_GET   r8d, rdi
    mov     r13d, 127
    shl     r13d, cl
    not     r13d
    and     r8d, r13d
.endm

.macro G_LINK_SET_IT
    P16_SLOT_CLEARED
    mov     r13d, eax
    shl     r13d, cl
    or      r8d, r13d
    W_PUT   rdi
.endm

.macro G_LINK_CLR
    P16_SLOT_CLEARED
    mov     r13d, 64
    shl     r13d, cl
    or      r8d, r13d
    W_PUT   rdi
.endm

.macro G_LINK_SET_NEW
    P16_SLOT_CLEARED
    mov     r13d, r11d
    shl     r13d, cl
    or      r8d, r13d
    W_PUT   rdi
    mov     r8d, P16_BLANK
    W_PUT   r11
.endm

.macro G_LINK_GET
    movzx   edi, ch
    W_GET   eax, rdi
    shr     eax, cl
    and     eax, 127
.endm
