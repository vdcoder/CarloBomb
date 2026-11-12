/*
 * Representation "hyb": colors in register bitmaps (g_colbm.inc), links in
 * memory as one byte each, two per node, -1 = undecided.  Any node budget.
 * rbx = link bytes (the start of state->nodes is used as storage).
 * A slot designator is the link's index, 2*node + d.
 */
#include "skel_defs.inc"
#include "g_colbm.inc"

.macro G_ENTER
    lea     rbx, [rdi + CB_ST_NODES]
    mov     word ptr [rbx], -1
    xor     r13d, r13d
    xor     r15d, r15d
.endm

/* Expand the link pairs to 4-byte nodes in place, last node first so that
 * nothing is overwritten before it is read. */
.macro G_EXPORT
    mov     ecx, r11d
47: sub     ecx, 1
    js      48f
    movzx   edx, word ptr [rbx + rcx*2]         /* dl = link 0, dh = link 1 */
    shl     edx, 8
    G_COLOR_BYTE
    or      eax, edx
    mov     dword ptr [rbx + rcx*4], eax
    jmp     47b
48:
.endm

.macro G_LINK_CP u
    lea     ecx, [rax + rdx*2]
    movsx   eax, byte ptr [rbx + rcx]
    test    eax, eax
    js      \u
.endm

.macro G_LINK_FP u
    lea     ecx, [rax + rsi*2]
    movsx   eax, byte ptr [rbx + rcx]
    test    eax, eax
    js      \u
.endm

.macro G_LINK_SET_IT
    mov     byte ptr [rbx + rcx], al
.endm

.macro G_LINK_CLR
    mov     byte ptr [rbx + rcx], -1
.endm

.macro G_LINK_SET_NEW
    mov     byte ptr [rbx + rcx], r11b
    mov     word ptr [rbx + r11*2], -1
.endm

.macro G_LINK_GET
    movsx   eax, byte ptr [rbx + rcx]
.endm
