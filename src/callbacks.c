#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "picolisp.h"
#include "callbacks.h"

#ifdef HAVE_FFI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Pool storage (definitions -- declared extern in callbacks.h)
 * ===================================================================== */
CbSlot   g_callbacks[CB_POOL_SIZE];
uint32_t g_cb_count = 0;

/* All cross-TU functions used here are declared in picolisp.h:
 *   pl_apply, pl_cons, gc_mark_val, big_from_int64, big_to_str,
 *   pl_error_str, prim_register, str_intern, str_ptr.
 * No additional forward declarations needed.
 */

/* =========================================================================
 * cb_alloc_slot -- find a free slot; reuse dead slots first.
 * Slots are 1-based: index 0 is unused so MAKE_CB(0) can serve as
 * a sentinel.  g_cb_count is a high-water mark of used indices.
 * ===================================================================== */
static uint32_t cb_alloc_slot(void)
{
    /* Scan existing slots for a dead one (alive == 0). */
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (!g_callbacks[i].alive)
            return i;
    }
    /* No dead slot -- extend the high-water mark. */
    if (g_cb_count + 1 >= CB_POOL_SIZE)
        pl_error_str("ffi-callback: callback pool exhausted");
    return ++g_cb_count;
}

/* =========================================================================
 * ftype_for -- map a single type-code char to an ffi_type*
 * ===================================================================== */
static ffi_type *ftype_for(char c)
{
    switch (c) {
        case 'v': return &ffi_type_void;
        case 'i': return &ffi_type_sint32;
        case 'l': return &ffi_type_sint64;
        case 'p': /* fall through */
        case 's': return &ffi_type_pointer;
        case 'd': return &ffi_type_double;
        default:
            pl_error_str("ffi-callback: unknown type code (valid: v i l p s d)");
            return &ffi_type_sint32;  /* unreachable */
    }
}

/* =========================================================================
 * box_arg -- convert one libffi argument pointer to a Lisp Val.
 * raw is the pointer in libffi's args[] array -- it points AT the value.
 * ===================================================================== */
static Val box_arg(ffi_type *ft, void *raw)
{
    if (ft == &ffi_type_sint32) {
        return MAKE_INT(*(int32_t *)raw);
    }
    if (ft == &ffi_type_sint64) {
        return big_from_int64(*(int64_t *)raw);
    }
    if (ft == &ffi_type_pointer) {
        /* Wrap the pointer value as a bignum so the full 64-bit address
         * is preserved without truncation to 32 bits. */
        return big_from_int64((int64_t)(uintptr_t)(*(void **)raw));
    }
    /* double callback args: not yet supported for boxing into Lisp */
    pl_error_str("ffi-callback: 'd' argument type not yet supported in callbacks (use 'i' or 'p')");
    return NIL_VAL; /* unreachable */
}

/* =========================================================================
 * unbox_ret -- write a Lisp Val back into the libffi return-value slot.
 * ===================================================================== */
static void unbox_ret(ffi_type *rtype, void *ret, Val result)
{
    if (rtype == &ffi_type_void) {
        /* Nothing to write. */
        return;
    }

    if (rtype == &ffi_type_sint32) {
        int32_t v = 0;
        if (IS_INT(result))
            v = INT_VAL(result);
        else if (IS_BIG(result))
            v = (int32_t)strtoll(big_to_str(result), NULL, 10);
        memcpy(ret, &v, sizeof(v));
        return;
    }

    if (rtype == &ffi_type_sint64) {
        int64_t v = 0;
        if (IS_INT(result))
            v = (int64_t)INT_VAL(result);
        else if (IS_BIG(result))
            v = (int64_t)strtoll(big_to_str(result), NULL, 10);
        memcpy(ret, &v, sizeof(v));
        return;
    }

    if (rtype == &ffi_type_pointer) {
        uint64_t addr = 0;
        if (IS_INT(result))
            addr = (uint64_t)(int64_t)INT_VAL(result);
        else if (IS_BIG(result))
            addr = (uint64_t)strtoll(big_to_str(result), NULL, 10);
        void *p = (void *)(uintptr_t)addr;
        memcpy(ret, &p, sizeof(p));
        return;
    }

    /* double: zero-fill for safety */
    double dv = 0.0;
    memcpy(ret, &dv, sizeof(dv));
}

/* =========================================================================
 * cb_dispatch -- libffi closure trampoline; called whenever C code invokes
 * the generated callback function pointer.
 * ===================================================================== */
static void cb_dispatch(ffi_cif *cif, void *ret, void **args, void *user_data)
{
    uint32_t idx  = (uint32_t)(uintptr_t)user_data;
    CbSlot  *slot = &g_callbacks[idx];

    /* Build argument list in Lisp order: iterate nargs-1 downto 0 so that
     * consing from the back gives a forward-ordered list. */
    unsigned int nargs = cif->nargs;
    Val arg_list = NIL_VAL;
    for (unsigned int i = nargs; i-- > 0; ) {
        Val boxed = box_arg(cif->arg_types[i], args[i]);
        arg_list  = pl_cons(boxed, arg_list);
    }

    /* Invoke the Lisp function. */
    Val result = pl_apply(slot->lambda, arg_list, NIL_VAL);

    /* Write the return value back to the C caller. */
    unbox_ret(cif->rtype, ret, result);
}

/* =========================================================================
 * prim_ffi_callback -- Lisp primitive: (ffi-callback "ret" "args" fn)
 *   "ret"  -- single type-code character string, e.g. "i" or "v"
 *   "args" -- string of type-code chars, one per argument, e.g. "il"
 *   fn     -- a Lisp lambda (or any callable)
 * Returns a TAG_CB value wrapping the slot index.
 * ===================================================================== */
static Val prim_ffi_callback(Val args, Val env)
{
    (void)env;

    /* Extract three arguments. */
    Val ret_type_val  = CAR(args);  args = CDR(args);
    Val arg_types_val = CAR(args);  args = CDR(args);
    Val fn            = CAR(args);

    if (!IS_STR(ret_type_val))
        pl_error_str("ffi-callback: return type must be a string");
    if (!IS_STR(arg_types_val))
        pl_error_str("ffi-callback: arg types must be a string");

    const char *ret_str  = str_ptr(STR_IDX(ret_type_val));
    const char *args_str = str_ptr(STR_IDX(arg_types_val));

    /* Parse return type. */
    ffi_type *rtype = ftype_for(ret_str[0]);

    /* Parse argument type string -- each character is one type code. */
    unsigned int nargs_count = (unsigned int)strlen(args_str);
    if (nargs_count > CB_MAX_ARGS)
        pl_error_str("ffi-callback: too many argument types (max CB_MAX_ARGS)");

    /* Allocate a slot. */
    uint32_t idx  = cb_alloc_slot();
    CbSlot  *slot = &g_callbacks[idx];
    memset(slot, 0, sizeof(*slot));

    /* Build arg_ftypes[] array inside the slot (must persist for cif lifetime). */
    for (unsigned int i = 0; i < nargs_count; i++) {
        slot->arg_ftypes[i] = ftype_for(args_str[i]);
    }

    /* Prepare the CIF. */
    if (ffi_prep_cif(&slot->cif, FFI_DEFAULT_ABI, nargs_count, rtype,
                     slot->arg_ftypes) != FFI_OK) {
        pl_error_str("ffi-callback: ffi_prep_cif failed");
    }

    /* Allocate an executable closure. */
    slot->closure = (ffi_closure *)ffi_closure_alloc(sizeof(ffi_closure),
                                                      &slot->fn_ptr);
    if (!slot->closure)
        pl_error_str("ffi-callback: ffi_closure_alloc failed");

    /* Install the dispatcher. */
    if (ffi_prep_closure_loc(slot->closure, &slot->cif, cb_dispatch,
                             (void *)(uintptr_t)idx,
                             slot->fn_ptr) != FFI_OK) {
        ffi_closure_free(slot->closure);
        slot->closure = NULL;
        pl_error_str("ffi-callback: ffi_prep_closure_loc failed");
    }

    slot->lambda = fn;
    slot->alive  = 1;
    slot->mark   = 0;

    return MAKE_CB(idx);
}

/* =========================================================================
 * GC hooks -- called from the GC mark/sweep cycle (heap.c via callbacks.h)
 * ===================================================================== */

void gc_mark_callbacks(void)
{
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (g_callbacks[i].alive && g_callbacks[i].mark) {
            gc_mark_val(g_callbacks[i].lambda);
        }
    }
}

void gc_sweep_callbacks(void)
{
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (g_callbacks[i].alive) {
            if (!g_callbacks[i].mark) {
                /* Unreachable -- free the closure. */
                ffi_closure_free(g_callbacks[i].closure);
                g_callbacks[i].closure = NULL;
                g_callbacks[i].fn_ptr  = NULL;
                g_callbacks[i].alive   = 0;
            } else {
                /* Reachable -- clear mark for next cycle. */
                g_callbacks[i].mark = 0;
            }
        }
    }
}

/* =========================================================================
 * cb_prims_register -- called from prims_init() to register ffi-callback
 * ===================================================================== */
void cb_prims_register(void)
{
    prim_register("ffi-callback", prim_ffi_callback, 3, 3);
}

#endif /* HAVE_FFI */
