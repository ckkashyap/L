/* src/callbacks.h
 * CbSlot pool declarations -- only included by files that implement FFI callbacks.
 * (heap.c, ffi.c, callbacks.c)
 *
 * Files that include this header must include picolisp.h first (for Val).
 * Do NOT include picolisp.h from here -- that would create a circular include.
 */
#pragma once
#ifdef HAVE_FFI

#include <ffi.h>

#define CB_POOL_SIZE  256
#define CB_MAX_ARGS    16

typedef struct {
    ffi_cif      cif;                      /* describes the C function signature */
    ffi_closure *closure;                  /* executable trampoline (ffi_closure_alloc) */
    void        *fn_ptr;                   /* the callable C pointer inside closure */
    Val          lambda;                   /* Lisp function to dispatch to */
    uint8_t      alive;                    /* slot in use */
    uint8_t      mark;                     /* GC mark bit */
    ffi_type    *arg_ftypes[CB_MAX_ARGS];  /* stored so cif persists */
} CbSlot;

extern CbSlot   g_callbacks[CB_POOL_SIZE];
extern uint32_t g_cb_count;               /* high-water mark (1-based) */

/* GC hooks -- called from heap.c */
void gc_mark_callbacks(void);
void gc_sweep_callbacks(void);

/* Primitive registration */
void cb_prims_register(void);

#else

static inline void cb_prims_register(void) {}

#endif /* HAVE_FFI */
