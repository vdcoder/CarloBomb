/*
 * ABI check for the AMD64 search kernel (System V).
 *
 *     int cb_abi_check(CbAsmState *state, int max_nodes, int *kernel_result);
 *
 * Loads every callee-saved register with a distinct sentinel, calls
 * cb_amd64_search, and returns a bit mask of what was not preserved:
 *   1 rbx   2 rbp   4 r12   8 r13   16 r14   32 r15   64 rsp
 * 0 means the kernel honored the calling convention on this call.
 */
    .intel_syntax noprefix
    .text
    .globl  cb_abi_check
    .type   cb_abi_check, @function
cb_abi_check:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15
    push    rdx                         /* result pointer; also aligns the stack */
    mov     qword ptr [rip + .Lsaved_rsp], rsp
    movabs  rbx, 0x1111111111111111
    movabs  rbp, 0x2222222222222222
    movabs  r12, 0x3333333333333333
    movabs  r13, 0x4444444444444444
    movabs  r14, 0x5555555555555555
    movabs  r15, 0x6666666666666666
    call    cb_amd64_search@PLT
    xor     ecx, ecx
    movabs  rdx, 0x1111111111111111
    cmp     rbx, rdx
    je      1f
    or      ecx, 1
1:  movabs  rdx, 0x2222222222222222
    cmp     rbp, rdx
    je      1f
    or      ecx, 2
1:  movabs  rdx, 0x3333333333333333
    cmp     r12, rdx
    je      1f
    or      ecx, 4
1:  movabs  rdx, 0x4444444444444444
    cmp     r13, rdx
    je      1f
    or      ecx, 8
1:  movabs  rdx, 0x5555555555555555
    cmp     r14, rdx
    je      1f
    or      ecx, 16
1:  movabs  rdx, 0x6666666666666666
    cmp     r15, rdx
    je      1f
    or      ecx, 32
1:  cmp     rsp, qword ptr [rip + .Lsaved_rsp]
    je      1f
    or      ecx, 64
    mov     rsp, qword ptr [rip + .Lsaved_rsp]
1:  pop     rdx
    mov     dword ptr [rdx], eax
    mov     eax, ecx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret
    .size   cb_abi_check, . - cb_abi_check

    .bss
    .balign 8
.Lsaved_rsp:
    .quad   0
    .section .note.GNU-stack, "", @progbits
