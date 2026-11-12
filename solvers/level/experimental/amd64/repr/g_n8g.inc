/*
 * Representation "n8g": the whole graph in registers, for budgets up to 8.
 * Colors in bitmaps (g_colbm.inc).  Links in rbx, four bits each: the link d
 * of node n is the nibble at bit 4*(2n + d); 0-7 is a node, 15 is undecided.
 * Sixteen links fill the register exactly.
 * A slot designator is the nibble's bit position.
 * With CB_BMI2 the variable shifts are SHLX/SHRX and the clear is ANDN;
 * without, they are shifts by cl (the designator lives in rcx).
 */
#include "asm_layout.h"
#include "g_colbm.inc"

.macro LNK_READ                         /* eax = nibble at bit rcx */
#ifdef CB_BMI2
    shrx    rax, rbx, rcx
#else
    mov     rax, rbx
    shr     rax, cl
#endif
    and     eax, 15
.endm

.macro G_ENTER
    mov     rbx, -1                             /* every link undecided */
    xor     r13d, r13d
    xor     r15d, r15d
.endm

.macro G_EXPORT
    mov     r9, rbx
    lea     r8, [rdi + CB_ST_NODES]
    xor     ecx, ecx
47: cmp     ecx, r11d
    jae     48f
    mov     eax, r9d
    and     eax, 15                             /* link 0 */
    cmp     eax, 8
    sbb     edx, edx
    not     edx                                 /* -1 if undecided */
    or      eax, edx
    mov     byte ptr [r8 + rcx*4 + 1], al
    mov     eax, r9d
    shr     eax, 4
    and     eax, 15                             /* link 1 */
    cmp     eax, 8
    sbb     edx, edx
    not     edx
    or      eax, edx
    mov     byte ptr [r8 + rcx*4 + 2], al
    G_COLOR_BYTE
    mov     byte ptr [r8 + rcx*4], al
    shr     r9, 8
    inc     ecx
    jmp     47b
48:
.endm

.macro G_LINK_CP u
    lea     ecx, [rax + rdx*2]
    shl     ecx, 2
    LNK_READ
    cmp     eax, 8
    jae     \u
.endm

.macro G_LINK_FP u
    lea     ecx, [rax + rsi*2]
    shl     ecx, 2
    LNK_READ
    cmp     eax, 8
    jae     \u
.endm

.macro G_LINK_SET_IT                    /* nibble = eax, whatever it held */
    mov     edi, 15
#ifdef CB_BMI2
    shlx    rdi, rdi, rcx
    andn    rbx, rdi, rbx
    shlx    rdi, rax, rcx
#else
    shl     rdi, cl
    not     rdi
    and     rbx, rdi
    mov     edi, eax
    shl     rdi, cl
#endif
    or      rbx, rdi
.endm

.macro G_LINK_CLR                       /* nibble = 15 */
    mov     edi, 15
#ifdef CB_BMI2
    shlx    rdi, rdi, rcx
#else
    shl     rdi, cl
#endif
    or      rbx, rdi
.endm

.macro G_LINK_SET_NEW                   /* nibble 15 -> r11; node r11 is already blank */
    mov     edi, r11d
    xor     edi, 15
#ifdef CB_BMI2
    shlx    rdi, rdi, rcx
#else
    shl     rdi, cl
#endif
    xor     rbx, rdi
.endm

.macro G_LINK_GET
    LNK_READ
.endm
