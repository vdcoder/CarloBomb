/*
 * Representation "b4": the reference kernel's.  Nodes in memory, 4 bytes
 * each (color, link 0, link 1, unused), -1 = undecided.  rbx = node array.
 * A slot designator is the address of the link byte.
 */
#include "asm_layout.h"

#ifdef CB_FRAME8
#error "b4 slot designators are pointers and do not fit an 8-byte frame"
#endif

.macro G_ENTER
    lea     rbx, [rdi + CB_ST_NODES]
    mov     dword ptr [rbx], -1
.endm

.macro G_EXPORT                         /* already in place */
.endm

.macro G_FP_D u
    movsx   eax, byte ptr [rbx + rsi*4]
    test    eax, eax
    js      \u
.endm

.macro G_LINK_CP u
    lea     rcx, [rbx + rdx*4 + 1]
    add     rcx, rax
    movsx   eax, byte ptr [rcx]
    test    eax, eax
    js      \u
.endm

.macro G_LINK_FP u
    lea     rcx, [rbx + rsi*4 + 1]
    add     rcx, rax
    movsx   eax, byte ptr [rcx]
    test    eax, eax
    js      \u
.endm

.macro G_TARGET_HOOK
.endm

.macro G_COLOR_TEST ok, set
    movsx   edi, byte ptr [rbx + rax*4]
    cmp     edi, ebp
    je      \ok
    test    edi, edi
    js      \set
.endm

.macro G_COLOR_SET_T
    mov     byte ptr [rbx + rax*4], bpl
.endm

.macro G_COLOR_CLR_CP
    mov     byte ptr [rbx + rdx*4], -1
.endm

.macro G_FP_WHITE
    mov     byte ptr [rbx + rsi*4], 0
.endm

.macro G_FP_BLACK
    mov     byte ptr [rbx + rsi*4], 1
.endm

.macro G_FP_CLR
    mov     byte ptr [rbx + rsi*4], -1
.endm

.macro G_LINK_SET_IT
    mov     byte ptr [rcx], al
.endm

.macro G_LINK_CLR
    mov     byte ptr [rcx], -1
.endm

.macro G_LINK_SET_NEW
    mov     byte ptr [rcx], r11b
    mov     dword ptr [rbx + r11*4], -1
.endm

.macro G_LINK_GET
    movsx   eax, byte ptr [rcx]
.endm
