/* FastWake clean-room reproduction (APNet 2023).
 *
 * Minimal cooperative fiber (user-level thread) with a hand-written context
 * switch that saves ONLY callee-saved registers + SP. Unlike swapcontext(3) it
 * issues no sigprocmask syscall, so a switch is tens of nanoseconds — this is
 * the user-space embodiment of the paper's switch_to() fast context switch
 * (Sec. 3.1), and it is what lets the per-core dispatcher hand the core to a
 * waiting worker at near-polling latency.
 *
 * Supported: x86_64 (SysV) and aarch64. Both architectures the paper evaluates.
 */
#ifndef FW_FIBER_H
#define FW_FIBER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fw_fiber {
    void   *sp;          /* saved stack pointer (the whole state lives here) */
    void   *stack_base;  /* malloc'd stack, NULL for the bootstrap/scheduler fiber */
    size_t  stack_size;
    void  (*entry)(void *);
    void   *arg;
    struct fw_fiber *link; /* where to return if entry() ever returns */
} fw_fiber;

/* Assembly primitive: save callee-saved state into *save_sp, load *restore.
 * Defined in fw_switch.S. */
extern void fw_ctx_swap(void **save_sp, void *restore_sp);

/* Initialize a runnable fiber with its own stack. entry(arg) runs on first
 * switch-in. `link` is the fiber to switch back to if entry returns. */
void fw_fiber_make(fw_fiber *f, size_t stack_size,
                   void (*entry)(void *), void *arg, fw_fiber *link);

/* The bootstrap fiber represents the current (scheduler/dispatcher) context;
 * its sp is filled in on the first switch away from it. */
static inline void fw_fiber_init_self(fw_fiber *self)
{
    self->sp = NULL; self->stack_base = NULL; self->stack_size = 0;
    self->entry = NULL; self->arg = NULL; self->link = NULL;
}

/* Switch from `from` (state saved) to `to` (state restored). */
static inline void fw_fiber_switch(fw_fiber *from, fw_fiber *to)
{
    fw_ctx_swap(&from->sp, to->sp);
}

void fw_fiber_free(fw_fiber *f);

#ifdef __cplusplus
}
#endif
#endif /* FW_FIBER_H */
