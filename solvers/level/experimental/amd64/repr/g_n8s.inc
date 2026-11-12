/*
 * Representation "n8s": as n8g (whole graph in registers, budgets up to 8),
 * with the link nibbles split by link number instead of interleaved:
 *
 *     link d of node n is the nibble at bit 4n + 32d of rbx.
 *
 * Both links of a node then sit 32 bits apart, so a CTRL INIT can extract
 * both with shifts that do not wait for the color of fp, and pick one with a
 * conditional move once the color bit is known.  That takes the color test
 * out of the address computation and shortens the dependency chain from
 * (fp, cp) to the next node.
 * A slot designator is the nibble's bit position.
 */
#include "skel_defs.inc"
#define CB_OWN_FP_D 1
#include "g_colbm.inc"

/* The skeleton always invokes G_FP_D immediately before G_LINK_CP; here the
 * first only checks that the color is decided and the second reads it. */
.macro G_FP_D u
    bt      r13, rsi
    jnc     \u
.endm

.macro G_LINK_CP u
    lea     ecx, [rdx*4]                        /* link 0 of cp */
#ifdef CB_BMI2
    lea     edi, [rdx*4 + 32]                   /* link 1 of cp */
    shrx    rax, rbx, rcx
    shrx    rdi, rbx, rdi
#else
    mov     rax, rbx
    shr     rax, cl
    mov     rdi, rax
    shr     rdi, 32
#endif
    bt      r15, rsi                            /* CF = color of fp */
    cmovc   eax, edi
    and     eax, 15
    cmp     eax, 8
    jae     41f
    .subsection 1
41: bt      r15, rsi                            /* d again, as a mask */
    sbb     edi, edi
    and     edi, 32
    add     ecx, edi                            /* slot = 4*cp + 32*d */
    jmp     \u
    .previous
.endm

.macro G_LINK_FP u                      /* in eax = d */
    shl     eax, 5
    lea     ecx, [rax + rsi*4]
#ifdef CB_BMI2
    shrx    rax, rbx, rcx
#else
    mov     rax, rbx
    shr     rax, cl
#endif
    and     eax, 15
    cmp     eax, 8
    jae     \u
.endm

.macro G_ENTER
    mov     rbx, -1                             /* every link undecided */
    xor     r13d, r13d
    xor     r15d, r15d
.endm

.macro N8S_NIBBLE_TO_BYTE               /* eax = nibble -> link byte in al */
    and     eax, 15
    cmp     eax, 8
    sbb     edx, edx
    not     edx
    or      eax, edx
.endm

.macro G_EXPORT
    mov     r9, rbx
    lea     r8, [rdi + CB_ST_NODES]
    xor     ecx, ecx
47: cmp     ecx, r11d
    jae     48f
    mov     eax, r9d
    N8S_NIBBLE_TO_BYTE                          /* link 0 */
    mov     byte ptr [r8 + rcx*4 + 1], al
    mov     rax, r9
    shr     rax, 32
    N8S_NIBBLE_TO_BYTE                          /* link 1 */
    mov     byte ptr [r8 + rcx*4 + 2], al
    G_COLOR_BYTE
    mov     byte ptr [r8 + rcx*4], al
    /* next node: both halves move down one nibble */
    mov     rax, r9
    shr     rax, 4
    movabs  rdx, 0x0FFFFFFF0FFFFFFF
    and     rax, rdx
    mov     r9, rax
    inc     ecx
    jmp     47b
48:
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
#ifdef CB_BMI2
    shrx    rax, rbx, rcx
#else
    mov     rax, rbx
    shr     rax, cl
#endif
    and     eax, 15
.endm

#ifdef CB_BULK_ANY
/*
 * rdi = existing nodes n for which CTRL(n, cp) is not certain to fail in its
 * INIT.  That INIT looks only at the color c of n and at link c of cp, so
 * all white n behave alike and all black n behave alike: it fails at once
 * exactly when link c of cp is decided and leads to a node whose color is
 * decided and is not the target bit.
 */
.macro G_BULK_FLOW_WORK
    mov     L_TMP, eax
    lea     eax, [rdx*4]
    shrx    rax, rbx, rax                       /* links of cp: nibble 0 and nibble 8 */
    mov     rdi, rax
    shr     rdi, 32
    and     edi, 15                             /* link 1 of cp (15 = undecided) */
    and     eax, 15                             /* link 0 of cp */
    mov     rcx, r13
    xor     rcx, r8                             /* decided nodes of the wrong color */
    bt      rcx, rax
    sbb     rax, rax                            /* all ones: a white n fails at once */
    bt      rcx, rdi
    sbb     rdi, rdi                            /* all ones: a black n fails at once */
    mov     rcx, r13
    xor     rcx, r15                            /* white nodes */
    and     rax, rcx
    and     rdi, r15                            /* black nodes */
    or      rdi, rax                            /* certain failures */
    mov     rcx, -1
    bzhi    rcx, rcx, r11                       /* existing nodes */
    andn    rdi, rdi, rcx
    mov     eax, L_TMP
.endm
#endif
