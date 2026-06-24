/*
 * Cooperative-coroutine hazard experiment (the blind spot of the TSan thread method).
 *
 * Real transparent coroutines run on ONE OS thread and yield at the crypto
 * "offload" point. Any per-thread state a handler sets BEFORE the offload and
 * reads AFTER it can be clobbered by whichever coroutine ran during the park.
 * The TSan thread method cannot see this: it models each connection as its own
 * OS thread, so thread-local storage looks private when it is really shared.
 *
 * Two such states in the OpenSSL/C crypto path:
 *   - errno                         (classic coroutine hazard)
 *   - the OpenSSL per-thread error queue (ERR_*)  -> SSL_get_error() depends on it
 *
 * Model: 2 coroutines (ucontext) on one OS thread, scheduled round-robin, each
 * yielding once at its offload point per round. Each coroutine writes a marker
 * (errno + an ERR code) before the offload and verifies it after. We count how
 * often the post-offload read sees the OTHER coroutine's value.
 *
 *   mode naive : runtime does NOT save/restore per-thread state at yield.
 *   mode saved : runtime saves errno + drains/restores the ERR queue at yield
 *                (what a correct coroutine runtime -- and OpenSSL's own ASYNC_JOB
 *                machinery -- must do).  Expect 0 corruptions.
 */
#include <ucontext.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCO 2
#define ERRBUF 64

static int SAVE = 0;       /* save/restore per-thread state at yield? */
static int ROUNDS = 100000;
static long corruption_errno = 0, corruption_err = 0, checks = 0;

struct coro {
    ucontext_t ctx;
    int id;
    /* saved per-thread state while this coroutine is parked (mode saved) */
    int saved_errno;
    unsigned long saved_codes[ERRBUF];
    int n_saved;
    int done;
};
static struct coro co[NCO];
static ucontext_t sched_ctx;
static int current = -1;

/* drain the OpenSSL per-thread error queue into a buffer (save) */
static void err_drain(struct coro *c){
    c->n_saved = 0;
    unsigned long e;
    while ((e = ERR_get_error()) != 0 && c->n_saved < ERRBUF)
        c->saved_codes[c->n_saved++] = e;
}
/* restore a previously drained error queue */
static void err_restore(struct coro *c){
    ERR_clear_error();
    for (int i = 0; i < c->n_saved; i++)
        ERR_raise(ERR_GET_LIB(c->saved_codes[i]), ERR_GET_REASON(c->saved_codes[i]));
}

/* the offload yield: hand control back to the scheduler */
static void coro_yield(void){
    struct coro *c = &co[current];
    if (SAVE) { c->saved_errno = errno; err_drain(c); }
    swapcontext(&c->ctx, &sched_ctx);
    if (SAVE) { errno = c->saved_errno; err_restore(c); }
}

static void coro_body(void){
    struct coro *c = &co[current];
    for (int r = 0; r < ROUNDS; r++) {
        /* --- pre-offload: stamp per-thread state unique to this coroutine --- */
        int my_errno = 1000 + c->id;
        int my_reason = 0x100 + c->id;            /* ERR reason code marker */
        errno = my_errno;
        ERR_clear_error();
        ERR_raise(ERR_LIB_USER, my_reason);

        /* --- offload: park the coroutine (another coroutine runs now) --- */
        coro_yield();

        /* --- post-offload: the handler reads back the state it stamped --- */
        checks++;
        if (errno != my_errno) corruption_errno++;
        unsigned long top = ERR_peek_last_error();
        if (ERR_GET_REASON(top) != my_reason) corruption_err++;
    }
    c->done = 1;
    swapcontext(&c->ctx, &sched_ctx);   /* final return to scheduler */
}

int main(int argc, char **argv){
    if (argc > 1 && !strcmp(argv[1], "saved")) SAVE = 1;
    if (argc > 2) ROUNDS = atoi(argv[2]);

    SSL_library_init(); SSL_load_error_strings();
    ERR_clear_error();

    static char stacks[NCO][256*1024];
    for (int i = 0; i < NCO; i++) {
        co[i].id = i; co[i].done = 0; co[i].n_saved = 0;
        getcontext(&co[i].ctx);
        co[i].ctx.uc_stack.ss_sp = stacks[i];
        co[i].ctx.uc_stack.ss_size = sizeof stacks[i];
        co[i].ctx.uc_link = &sched_ctx;
        makecontext(&co[i].ctx, coro_body, 0);
    }

    /* round-robin scheduler: resume each non-done coroutine in turn */
    int live = NCO;
    while (live > 0) {
        live = 0;
        for (int i = 0; i < NCO; i++) {
            if (co[i].done) continue;
            current = i;
            swapcontext(&sched_ctx, &co[i].ctx);
            if (!co[i].done) live++;
        }
    }

    printf("[mode=%s rounds=%d] checks=%ld  errno_corruptions=%ld  errqueue_corruptions=%ld\n",
           SAVE?"saved":"naive", ROUNDS, checks, corruption_errno, corruption_err);
    if (!SAVE && (corruption_errno || corruption_err))
        printf("  -> per-thread state IS shared across coroutines (hazard confirmed)\n");
    if (SAVE && !corruption_errno && !corruption_err)
        printf("  -> save/restore at yield eliminates it\n");
    return 0;
}
