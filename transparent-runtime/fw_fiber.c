/* FastWake clean-room reproduction (APNet 2023).
 * Fiber bootstrap: lay out a fresh stack so the first fw_ctx_swap into it lands
 * in fw_fiber_trampoline with the fiber* available in a callee-saved register.
 */
#define _GNU_SOURCE
#include "fw_fiber.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern void fw_fiber_trampoline(void);

/* Called from the asm trampoline on a fiber's first activation. */
void fw_fiber_start(fw_fiber *f)
{
    f->entry(f->arg);
    /* If entry returns, hand control back to the linked (scheduler) fiber so we
     * never run off the end of the stack. */
    if (f->link)
        fw_fiber_switch(f, f->link);
    fprintf(stderr, "[fw_fiber] entry returned with no link; aborting\n");
    abort();
}

void fw_fiber_make(fw_fiber *f, size_t stack_size,
                   void (*entry)(void *), void *arg, fw_fiber *link)
{
    if (stack_size < 16384) stack_size = 16384;
    /* round up to page multiple */
    stack_size = (stack_size + 4095) & ~((size_t)4095);
    void *base = NULL;
    if (posix_memalign(&base, 4096, stack_size) != 0) { abort(); }
    memset(base, 0, stack_size);

    f->stack_base = base;
    f->stack_size = stack_size;
    f->entry = entry;
    f->arg   = arg;
    f->link  = link;

    /* Build the initial saved-register frame so fw_ctx_swap "returns" into the
     * trampoline. The slot layout MUST match the push order in fw_switch.S.
     *
     * x86_64 pop order:  r15, r14, r13, r12, rbx, rbp, then ret.
     *   We put fiber* in the r15 slot and trampoline in the ret slot.
     * aarch64 layout: x19..x30(lr); fiber* in x19, trampoline in lr (x30). */
    char *top = (char *)base + stack_size;
    /* keep 16-byte alignment headroom */
    top = (char *)((uintptr_t)top & ~(uintptr_t)0xF);

#if defined(__x86_64__)
    /* 7 quadwords: r15,r14,r13,r12,rbx,rbp,ret */
    uintptr_t *sp = (uintptr_t *)top;
    sp -= 8;                 /* leave a little slack + keep alignment */
    sp[0] = (uintptr_t)f;                    /* r15 <- fiber* */
    sp[1] = 0;                               /* r14 */
    sp[2] = 0;                               /* r13 */
    sp[3] = 0;                               /* r12 */
    sp[4] = 0;                               /* rbx */
    sp[5] = 0;                               /* rbp */
    sp[6] = (uintptr_t)fw_fiber_trampoline;  /* ret address */
    f->sp = sp;
#elif defined(__aarch64__)
    /* 112-byte frame: x19,x20,x21,x22,x23,x24,x25,x26,x27,x28,x29(fp),x30(lr) */
    uintptr_t *sp = (uintptr_t *)top;
    sp -= 14;                                /* 112 bytes / 8 = 14 slots */
    memset(sp, 0, 14 * sizeof(uintptr_t));
    sp[0]  = (uintptr_t)f;                   /* x19 <- fiber* */
    sp[11] = (uintptr_t)fw_fiber_trampoline; /* x30 (lr) slot, index 11 */
    f->sp = sp;
#else
#error "fw_fiber_make: unsupported architecture"
#endif
}

void fw_fiber_free(fw_fiber *f)
{
    if (f && f->stack_base) { free(f->stack_base); f->stack_base = NULL; }
}
