#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* coro.c -- Coroutines (Windows Fibers / POSIX ucontext) + make-parameter. */

#include "picolisp.h"
#include "coro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef CORO_POOL_SIZE
#define CORO_POOL_SIZE 4096
#endif

Coro     g_coros[CORO_POOL_SIZE];
uint32_t g_coro_count = 0;
static uint32_t g_cur_coro = UINT32_MAX;  /* UINT32_MAX = main thread */

void coro_init(void) {
    memset(g_coros, 0, sizeof(g_coros));
    g_coro_count = 0;
    g_cur_coro   = UINT32_MAX;
}

/* Save the coroutine's GC root and bind stack slices to heap, then shrink
 * the global stacks back to the levels they were at before the coro ran. */
static void coro_save_stacks(uint32_t idx)
{
    Coro *c = &g_coros[idx];

    int gc_count = g_gc_root_top - c->gc_root_base;
    if (gc_count > 0) {
        free(c->saved_gc_roots);
        c->saved_gc_roots = malloc((size_t)gc_count * sizeof(Val));
        if (!c->saved_gc_roots) pl_error_str("OOM in coroutine yield (gc roots)");
        memcpy(c->saved_gc_roots,
               g_gc_roots + c->gc_root_base,
               (size_t)gc_count * sizeof(Val));
    } else {
        free(c->saved_gc_roots);
        c->saved_gc_roots = NULL;
    }
    c->saved_gc_root_count = gc_count;
    g_gc_root_top = c->gc_root_base;

    int b_count = g_bind_top - c->bind_base;
    if (b_count > 0) {
        free(c->saved_binds);
        c->saved_binds = malloc((size_t)b_count * sizeof(BindFrame));
        if (!c->saved_binds) pl_error_str("OOM in coroutine yield (binds)");
        memcpy(c->saved_binds,
               g_bind_stack + c->bind_base,
               (size_t)b_count * sizeof(BindFrame));
    } else {
        free(c->saved_binds);
        c->saved_binds = NULL;
    }
    c->saved_bind_count = b_count;
    g_bind_top = c->bind_base;
}

/* Restore the coroutine's previously saved GC root and bind stack slices
 * into the global stacks, starting at the current top (which becomes the
 * new base for this resume).  Must be called before switching to the coro. */
static void coro_restore_stacks(uint32_t idx)
{
    Coro *c = &g_coros[idx];

    c->gc_root_base = g_gc_root_top;
    c->bind_base    = g_bind_top;

    if (c->saved_gc_root_count > 0) {
        memcpy(g_gc_roots + c->gc_root_base,
               c->saved_gc_roots,
               (size_t)c->saved_gc_root_count * sizeof(Val));
        g_gc_root_top += c->saved_gc_root_count;
        free(c->saved_gc_roots);
        c->saved_gc_roots = NULL;
    }
    c->saved_gc_root_count = 0;

    if (c->saved_bind_count > 0) {
        memcpy(g_bind_stack + c->bind_base,
               c->saved_binds,
               (size_t)c->saved_bind_count * sizeof(BindFrame));
        g_bind_top += c->saved_bind_count;
        free(c->saved_binds);
        c->saved_binds = NULL;
    }
    c->saved_bind_count = 0;
}

/* GC helper: mark all Lisp values stored in alive coroutine slots. */
void gc_mark_coros(void)
{
    for (uint32_t i = 0; i < g_coro_count; i++) {
        Coro *c = &g_coros[i];
        if (!c->alive) continue;
        gc_mark_val(c->fn);
        gc_mark_val(c->send_val);
        gc_mark_val(c->yield_val);
        for (int j = 0; j < c->saved_gc_root_count; j++)
            gc_mark_val(c->saved_gc_roots[j]);
        for (int j = 0; j < c->saved_bind_count; j++)
            gc_mark_val(c->saved_binds[j].saved);
    }
}

/* (co-alive? coro) -- T if coroutine is still alive, NIL otherwise. */
static Val prim_co_alivep(Val args, Val env)
{
    (void)env;
    Val coro_val = CAR(args);
    if (!IS_CORO(coro_val)) return NIL_VAL;
    uint32_t idx = CORO_IDX(coro_val);
    if (idx >= g_coro_count) return NIL_VAL;
    return g_coros[idx].alive ? T_VAL : NIL_VAL;
}

/* =========================================================================
 * Platform: Windows Fibers
 * ===================================================================== */
#ifdef _WIN32
#include <windows.h>

static LPVOID g_main_fiber = NULL;

/* Return the fiber handle for idx, or g_main_fiber for UINT32_MAX. */
static LPVOID coro_caller_fiber(uint32_t caller_idx)
{
    return (caller_idx == UINT32_MAX) ? g_main_fiber : g_coros[caller_idx].fiber;
}

static VOID CALLBACK coro_fiber_fn(LPVOID param)
{
    uint32_t idx = (uint32_t)(uintptr_t)param;
    Coro    *c   = &g_coros[idx];

    Val fn  = c->fn;
    Val arg = c->send_val;
    PUSH_ROOT(fn);
    Val call_args = pl_cons(arg, NIL_VAL);
    POP_ROOT();
    Val result = pl_apply(fn, call_args, NIL_VAL);

    c->yield_val  = result;
    c->alive      = 0;
    g_gc_root_top = c->gc_root_base;
    g_bind_top    = c->bind_base;
    uint32_t caller = c->caller_coro_idx;
    g_cur_coro = caller;
    SwitchToFiber(coro_caller_fiber(caller));
    for (;;) SwitchToFiber(coro_caller_fiber(c->caller_coro_idx));
}

static Val prim_co(Val args, Val env)
{
    (void)env;
    if (!g_main_fiber) {
        g_main_fiber = ConvertThreadToFiber(NULL);
        if (!g_main_fiber) pl_error_str("co: ConvertThreadToFiber failed");
    }
    if (g_coro_count >= CORO_POOL_SIZE)
        pl_error_str("co: coroutine pool exhausted");

    Val fn = IS_CONS(args) ? CAR(args) : NIL_VAL;
    uint32_t idx = g_coro_count++;
    Coro *c = &g_coros[idx];
    memset(c, 0, sizeof(Coro));
    c->fn              = fn;
    c->alive           = 1;
    c->send_val        = NIL_VAL;
    c->yield_val       = NIL_VAL;
    c->caller_coro_idx = UINT32_MAX;

    c->fiber = CreateFiber(0, coro_fiber_fn, (LPVOID)(uintptr_t)idx);
    if (!c->fiber) pl_error_str("co: CreateFiber failed");
    return MAKE_CORO(idx);
}

static Val prim_yield(Val args, Val env)
{
    (void)env;
    if (g_cur_coro == UINT32_MAX)
        pl_error_str("yield: called outside a coroutine");

    uint32_t idx = g_cur_coro;
    Coro    *c   = &g_coros[idx];
    c->yield_val = IS_CONS(args) ? CAR(args) : NIL_VAL;
    coro_save_stacks(idx);

    uint32_t caller = c->caller_coro_idx;
    g_cur_coro = caller;
    SwitchToFiber(coro_caller_fiber(caller));
    return c->send_val;
}

static Val prim_co_resume(Val args, Val env)
{
    (void)env;
    Val coro_val = CAR(args);
    Val send_val = CADR(args);
    if (!IS_CORO(coro_val)) pl_error_str("co-resume: first argument must be a coroutine");
    uint32_t idx = CORO_IDX(coro_val);
    if (idx >= g_coro_count) pl_error_str("co-resume: invalid coroutine");
    Coro *c = &g_coros[idx];
    if (!c->alive) return NIL_VAL;

    c->send_val        = send_val;
    c->caller_coro_idx = g_cur_coro;
    coro_restore_stacks(idx);
    g_cur_coro = idx;
    SwitchToFiber(c->fiber);

    if (!c->alive && c->fiber) { DeleteFiber(c->fiber); c->fiber = NULL; }
    return c->yield_val;
}

/* =========================================================================
 * Platform: POSIX ucontext  (Linux, macOS, BSDs)
 * ===================================================================== */
#else /* !_WIN32 */
#include <ucontext.h>

#define CORO_STACK_SIZE (512 * 1024)   /* 512 KB per coroutine */

/* Per-coroutine context: the ucontext_t and its heap-allocated stack.
 * Stored via c->fiber (void *). */
typedef struct {
    ucontext_t ctx;
    void      *stack;
} PosixCoroCtx;

/* The main thread's saved context.  No stack needed -- restoring it returns
 * execution to wherever swapcontext was called in the main fiber. */
static ucontext_t g_main_ctx;

/* Return a pointer to the ucontext for slot idx (UINT32_MAX → main). */
static ucontext_t *get_ucontext(uint32_t idx)
{
    if (idx == UINT32_MAX) return &g_main_ctx;
    return &((PosixCoroCtx *)g_coros[idx].fiber)->ctx;
}

/* Fiber entry point -- called by makecontext with the coro index as an int. */
static void coro_entry(int idx_i)
{
    uint32_t idx = (uint32_t)idx_i;
    Coro    *c   = &g_coros[idx];

    Val fn  = c->fn;
    Val arg = c->send_val;
    PUSH_ROOT(fn);
    Val call_args = pl_cons(arg, NIL_VAL);
    POP_ROOT();
    Val result = pl_apply(fn, call_args, NIL_VAL);

    c->yield_val  = result;
    c->alive      = 0;
    g_gc_root_top = c->gc_root_base;
    g_bind_top    = c->bind_base;
    uint32_t caller = c->caller_coro_idx;
    g_cur_coro = caller;
    /* swapcontext saves our (dead) context and restores the caller's. */
    swapcontext(get_ucontext(idx), get_ucontext(caller));
    /* Unreachable -- loop defensively. */
    for (;;) swapcontext(get_ucontext(idx), get_ucontext(c->caller_coro_idx));
}

static Val prim_co(Val args, Val env)
{
    (void)env;
    if (g_coro_count >= CORO_POOL_SIZE)
        pl_error_str("co: coroutine pool exhausted");

    Val fn = IS_CONS(args) ? CAR(args) : NIL_VAL;
    uint32_t idx = g_coro_count++;
    Coro *c = &g_coros[idx];
    memset(c, 0, sizeof(Coro));
    c->fn              = fn;
    c->alive           = 1;
    c->send_val        = NIL_VAL;
    c->yield_val       = NIL_VAL;
    c->caller_coro_idx = UINT32_MAX;

    PosixCoroCtx *pctx = malloc(sizeof(PosixCoroCtx));
    if (!pctx) pl_error_str("co: OOM allocating coroutine context");
    pctx->stack = malloc(CORO_STACK_SIZE);
    if (!pctx->stack) { free(pctx); pl_error_str("co: OOM allocating coroutine stack"); }

    getcontext(&pctx->ctx);
    pctx->ctx.uc_stack.ss_sp   = pctx->stack;
    pctx->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    pctx->ctx.uc_link          = NULL;   /* we handle return manually */
    makecontext(&pctx->ctx, (void(*)())coro_entry, 1, (int)idx);

    c->fiber = pctx;
    return MAKE_CORO(idx);
}

static Val prim_yield(Val args, Val env)
{
    (void)env;
    if (g_cur_coro == UINT32_MAX)
        pl_error_str("yield: called outside a coroutine");

    uint32_t idx = g_cur_coro;
    Coro    *c   = &g_coros[idx];
    c->yield_val = IS_CONS(args) ? CAR(args) : NIL_VAL;
    coro_save_stacks(idx);

    uint32_t caller = c->caller_coro_idx;
    g_cur_coro = caller;
    /* Save our context and restore the caller's. */
    swapcontext(get_ucontext(idx), get_ucontext(caller));
    /* We resume here on the next co-resume call. */
    return c->send_val;
}

static Val prim_co_resume(Val args, Val env)
{
    (void)env;
    Val coro_val = CAR(args);
    Val send_val = CADR(args);
    if (!IS_CORO(coro_val)) pl_error_str("co-resume: first argument must be a coroutine");
    uint32_t idx = CORO_IDX(coro_val);
    if (idx >= g_coro_count) pl_error_str("co-resume: invalid coroutine");
    Coro *c = &g_coros[idx];
    if (!c->alive) return NIL_VAL;

    c->send_val        = send_val;
    c->caller_coro_idx = g_cur_coro;
    coro_restore_stacks(idx);

    uint32_t my_idx = g_cur_coro;   /* save before overwriting */
    g_cur_coro = idx;
    /* Save our context and switch to the coroutine. */
    swapcontext(get_ucontext(my_idx), get_ucontext(idx));
    /* The coroutine yielded or finished. */

    if (!c->alive && c->fiber) {
        PosixCoroCtx *pctx = (PosixCoroCtx *)c->fiber;
        free(pctx->stack);
        free(pctx);
        c->fiber = NULL;
    }
    return c->yield_val;
}

#endif /* _WIN32 */

/* =========================================================================
 * make-parameter
 *
 * Returns a parameter object: a cons cell tagged with the symbol
 * *parameter* whose CDR holds the current value.  The eval/apply layer
 * is expected to give parameter objects getter/setter semantics when
 * called as a procedure.
 * ===================================================================== */
static Val prim_make_parameter(Val args, Val env)
{
    (void)env;
    Val init = IS_CONS(args) ? CAR(args) : NIL_VAL;
    uint32_t tag_idx = sym_intern("*parameter*", 11);
    PUSH_ROOT(init);
    Val param = pl_cons(MAKE_SYM(tag_idx), init);
    POP_ROOT();
    return param;
}

void coro_prims_register(void) {
    prim_register("co",             prim_co,             1, -1);
    prim_register("yield",          prim_yield,          1,  1);
    prim_register("co-resume",      prim_co_resume,      2,  2);
    prim_register("co-alive?",      prim_co_alivep,      1,  1);
    prim_register("make-parameter", prim_make_parameter, 1,  1);
}
