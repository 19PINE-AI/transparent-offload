/* FastWake clean-room reproduction (APNet 2023).
 *
 * Hand-written cooperative context switch. Saves only callee-saved registers
 * and the stack pointer (the C ABI lets the compiler treat everything else as
 * clobbered across the call), so there is no kernel transition and no signal
 * mask update — a switch is a few dozen instructions.
 *
 *   void fw_ctx_swap(void **save_sp, void *restore_sp)
 *       save_sp    : where to store the current SP (so we can resume later)
 *       restore_sp : SP to load (resume that fiber)
 *
 *   fw_fiber_trampoline : first-run entry; recovers the fiber* that
 *       fw_fiber_make() planted in a callee-saved slot and calls the C
 *       starter fw_fiber_start(fiber*).
 */

#if defined(__x86_64__)

    .text
    .globl fw_ctx_swap
    .type  fw_ctx_swap, @function
fw_ctx_swap:
    /* rdi = save_sp, rsi = restore_sp */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq  %rsp, (%rdi)      /* *save_sp = rsp */
    movq  %rsi, %rsp        /* rsp = restore_sp */
    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %rbx
    popq  %rbp
    ret
    .size fw_ctx_swap, .-fw_ctx_swap

    /* On first switch-in, fw_fiber_make arranged for r15 to hold the fiber*
     * and the return address to be this trampoline. Move it into rdi (1st arg)
     * and call the C starter. The starter does not return. */
    .globl fw_fiber_trampoline
    .type  fw_fiber_trampoline, @function
fw_fiber_trampoline:
    movq  %r15, %rdi
    andq  $-16, %rsp        /* keep 16-byte stack alignment for the callee */
    call  fw_fiber_start
    /* fw_fiber_start never returns; trap if it does. */
    ud2
    .size fw_fiber_trampoline, .-fw_fiber_trampoline

#elif defined(__aarch64__)

    .text
    .globl fw_ctx_swap
    .type  fw_ctx_swap, %function
fw_ctx_swap:
    /* x0 = save_sp, x1 = restore_sp. Save x19-x30 + fp. */
    sub  sp, sp, #112
    stp  x19, x20, [sp, #0]
    stp  x21, x22, [sp, #16]
    stp  x23, x24, [sp, #32]
    stp  x25, x26, [sp, #48]
    stp  x27, x28, [sp, #64]
    stp  x29, x30, [sp, #80]   /* fp, lr */
    mov  x2, sp
    str  x2, [x0]              /* *save_sp = sp */
    mov  sp, x1               /* sp = restore_sp */
    ldp  x19, x20, [sp, #0]
    ldp  x21, x22, [sp, #16]
    ldp  x23, x24, [sp, #32]
    ldp  x25, x26, [sp, #48]
    ldp  x27, x28, [sp, #64]
    ldp  x29, x30, [sp, #80]
    add  sp, sp, #112
    ret
    .size fw_ctx_swap, .-fw_ctx_swap

    .globl fw_fiber_trampoline
    .type  fw_fiber_trampoline, %function
fw_fiber_trampoline:
    /* fw_fiber_make planted fiber* in x19. */
    mov  x0, x19
    bl   fw_fiber_start
    brk  #0                   /* never returns */
    .size fw_fiber_trampoline, .-fw_fiber_trampoline

#else
#error "fw_switch.S: unsupported architecture"
#endif

/* Mark the stack as non-executable to silence the linker warning. */
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
    .section .note.GNU-stack,"",@progbits
#endif
