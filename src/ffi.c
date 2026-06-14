#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* for strdup */
#endif
/* ffi.c -- Foreign Function Interface for the L interpreter.
 *
 * Two execution paths, selected at compile time:
 *
 *   HAVE_FFI   -- libffi (full type support including double, any platform).
 *   _WIN32     -- bare Microsoft x64 ABI cast (built-in, no libffi needed).
 *                Supports types i/l/s/p/v.  Type 'd' (double) is not
 *                supported on this path because doubles use XMM registers.
 *
 * Primitives:
 *   (native lib func ret-type type val ...)  → result
 *   (ffi-open libpath)                       → T
 *
 * Type codes (strings or symbols):
 *   "v"  void      (return only)
 *   "i"  int32_t
 *   "l"  int64_t
 *   "d"  double    (libffi path only)
 *   "s"  char*     (L string → C const char*; C return → L string or NIL)
 *   "p"  pointer   (L integer → C void*; C return → L integer)
 */

#include "picolisp.h"
#include "callbacks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_FFI) || defined(_WIN32)

#ifdef HAVE_FFI
#  include <ffi.h>
#endif

#ifdef _WIN32
#  include <windows.h>
#  define FFI_DLLIB_OPEN(p)   ((void *)LoadLibraryA(p))
#  define FFI_DLLIB_SYM(h,n)  ((void *)GetProcAddress((HMODULE)(h),(n)))
#  define FFI_MAIN_HANDLE()   ((void *)GetModuleHandleA(NULL))
#else
#  include <dlfcn.h>
#  define FFI_DLLIB_OPEN(p)   dlopen((p), RTLD_LAZY | RTLD_GLOBAL)
#  define FFI_DLLIB_SYM(h,n)  dlsym((h),(n))
#  define FFI_MAIN_HANDLE()   dlopen(NULL, RTLD_LAZY)
/* Some CRT functions (atexit, signal) are local symbols on modern glibc
 * and invisible to dlsym. Provide compile-time pointers as fallback. */
#  include <signal.h>
typedef void (*sig_handler_t)(int);
static struct { const char *name; void *ptr; } ffi_builtins_[] = {
    { "atexit", NULL },
    { "signal", NULL },
    { NULL, NULL }
};
__attribute__((constructor)) static void ffi_init_builtins_(void) {
    /* Store pointers at runtime init to avoid compiler folding atexit→__cxa_atexit */
    int (*a)(void(*)(void)) = atexit;
    sig_handler_t (*s)(int, sig_handler_t) = signal;
    *(volatile void **)&ffi_builtins_[0].ptr = (void*)(intptr_t)a;
    *(volatile void **)&ffi_builtins_[1].ptr = (void*)(intptr_t)s;
}
static void *ffi_resolve_builtin_(const char *name) {
    for (int i = 0; ffi_builtins_[i].name; i++)
        if (strcmp(name, ffi_builtins_[i].name) == 0) return ffi_builtins_[i].ptr;
    return NULL;
}
#endif

/* On Windows without libffi we use a generic 8-argument function pointer.
 * The Microsoft x64 ABI passes integer/pointer arguments uniformly as 64-bit
 * values (RCX, RDX, R8, R9, then stack).  Padding to 8 args is safe because
 * the callee ignores registers/stack slots it doesn't declare. */
#if defined(_WIN32) && !defined(HAVE_FFI)
typedef int64_t (*win64_fn_t)(int64_t, int64_t, int64_t, int64_t,
                               int64_t, int64_t, int64_t, int64_t);
#  define MAX_FFI_ARGS 8
#else
#  define MAX_FFI_ARGS 32
#endif

/* =========================================================================
 * Library handle cache
 * ===================================================================== */

#define FFI_LIB_CACHE_SIZE 64

static struct {
    char *path;
    void *handle;
} ffi_lib_cache[FFI_LIB_CACHE_SIZE];
static int ffi_lib_cache_count = 0;

static void *ffi_open_lib(const char *path)
{
    /* NULL / empty path → main program */
    if (!path || path[0] == '\0') return FFI_MAIN_HANDLE();

    for (int i = 0; i < ffi_lib_cache_count; i++) {
        if (strcmp(ffi_lib_cache[i].path, path) == 0)
            return ffi_lib_cache[i].handle;
    }
    if (ffi_lib_cache_count >= FFI_LIB_CACHE_SIZE)
        pl_error_str("ffi: library cache full");

    void *h = FFI_DLLIB_OPEN(path);
    if (!h) pl_error_str("ffi: cannot open library");

    ffi_lib_cache[ffi_lib_cache_count].path   = strdup(path);
    ffi_lib_cache[ffi_lib_cache_count].handle = h;
    ffi_lib_cache_count++;
    return h;
}

/* =========================================================================
 * Type-code helpers
 * ===================================================================== */

static const char *type_code(Val v)
{
    if (IS_STR(v)) return str_ptr(STR_IDX(v));
    if (IS_SYM(v)) return sym_name(SYM_IDX(v));
    pl_error_str("native: type spec must be a string or symbol");
    return "v";
}

#ifdef HAVE_FFI
static ffi_type *ffi_type_for(const char *tc)
{
    switch (tc[0]) {
        case 'v': return &ffi_type_void;
        case 'i': return &ffi_type_sint32;
        case 'l': return &ffi_type_sint64;
        case 'd': return &ffi_type_double;
        case 's': return &ffi_type_pointer;
        case 'p': return &ffi_type_pointer;
        default:
            pl_error_str("native: unknown type code (use v/i/l/d/s/p)");
            return &ffi_type_void;
    }
}
#endif

/* =========================================================================
 * (native lib func ret-type [arg-type arg-val] ...) → result
 * ===================================================================== */

static Val prim_native(Val args, Val env)
{
    (void)env;

    Val lib_val  = CAR(args);  args = CDR(args);
    Val func_val = CAR(args);  args = CDR(args);
    Val ret_val  = CAR(args);  args = CDR(args);

    /* Resolve library */
    void *lib;
    if (IS_NIL(lib_val)) {
        lib = FFI_MAIN_HANDLE();
    } else if (IS_STR(lib_val)) {
        lib = ffi_open_lib(str_ptr(STR_IDX(lib_val)));
    } else {
        pl_error_str("native: lib must be a string (path) or NIL (main)");
        lib = NULL;
    }

    if (!IS_STR(func_val)) pl_error_str("native: function name must be a string");
    const char *fname = str_ptr(STR_IDX(func_val));
    void *fn = FFI_DLLIB_SYM(lib, fname);
#ifndef _WIN32
    /* Fallback: some symbols (e.g. atexit) are CRT-local on modern glibc
     * and invisible to dlsym. Try RTLD_DEFAULT, then our builtin table. */
    if (!fn) fn = dlsym(RTLD_DEFAULT, fname);
    if (!fn) fn = ffi_resolve_builtin_(fname);
#endif
    if (!fn) pl_error_str("native: symbol not found in library");

    const char *ret_code = type_code(ret_val);

    /* Parse alternating [type val] pairs */
    int nargs = val_list_len(args);
    if (nargs % 2 != 0) pl_error_str("native: args must come in type/value pairs");
    nargs /= 2;
    if (nargs > MAX_FFI_ARGS) pl_error_str("native: too many arguments");

    /* Per-argument storage */
    union { int32_t i; int64_t l; double d; const char *s; void *p; }
        astore[MAX_FFI_ARGS];
    memset(astore, 0, sizeof(astore));

#ifdef HAVE_FFI
    ffi_type *atypes[MAX_FFI_ARGS];
    void     *avalues[MAX_FFI_ARGS];
#elif defined(_WIN32)
    int64_t   avals[MAX_FFI_ARGS];   /* flat 64-bit array for win64_fn_t */
    memset(avals, 0, sizeof(avals));
#endif

    Val cur = args;
    for (int i = 0; i < nargs; i++) {
        const char *tc  = type_code(CAR(cur)); cur = CDR(cur);
        Val         val = CAR(cur);            cur = CDR(cur);

#ifdef HAVE_FFI
        atypes[i]  = ffi_type_for(tc);
        avalues[i] = &astore[i];
#endif

        switch (tc[0]) {
            case 'i':
                if (!IS_INT(val)) pl_error_str("native: expected int for 'i' arg");
                astore[i].i = INT_VAL(val);
#if defined(_WIN32) && !defined(HAVE_FFI)
                avals[i] = (int64_t)astore[i].i;
#endif
                break;
            case 'l':
                if (IS_INT(val))      astore[i].l = (int64_t)INT_VAL(val);
                else if (IS_BIG(val)) astore[i].l = (int64_t)strtoll(big_to_str(val), NULL, 10);
                else pl_error_str("native: expected integer for 'l' arg");
#if defined(_WIN32) && !defined(HAVE_FFI)
                avals[i] = astore[i].l;
#endif
                break;
            case 'd':
#if defined(_WIN32) && !defined(HAVE_FFI)
                pl_error_str("native: type 'd' (double) requires libffi on Windows");
#else
                if (IS_INT(val))      astore[i].d = (double)INT_VAL(val);
                else if (IS_BIG(val)) astore[i].d = (double)strtoll(big_to_str(val), NULL, 10);
                else pl_error_str("native: expected number for 'd' arg");
#endif
                break;
            case 's':
                if (!IS_STR(val)) pl_error_str("native: expected string for 's' arg");
                astore[i].s = str_ptr(STR_IDX(val));
#if defined(_WIN32) && !defined(HAVE_FFI)
                avals[i] = (int64_t)(uintptr_t)astore[i].s;
#endif
                break;
            case 'p':
                if (IS_CB(val)) {
#ifdef HAVE_FFI
                    uint32_t cidx = CB_IDX(val);
                    if (!cidx || cidx > g_cb_count || !g_callbacks[cidx].alive)
                        pl_error_str("native: callback has been freed");
                    astore[i].p = g_callbacks[cidx].fn_ptr;
#else
                    pl_error_str("native: TAG_CB requires HAVE_FFI");
#endif
                } else if (IS_INT(val)) {
                    astore[i].l = (int64_t)INT_VAL(val);
                    astore[i].p = (void *)(uintptr_t)(uint64_t)astore[i].l;
                } else if (IS_BIG(val)) {
                    astore[i].l = (int64_t)strtoll(big_to_str(val), NULL, 10);
                    astore[i].p = (void *)(uintptr_t)(uint64_t)astore[i].l;
                } else {
                    pl_error_str("native: expected integer or callback for 'p' arg");
                }
#if defined(_WIN32) && !defined(HAVE_FFI)
                avals[i] = (int64_t)(uintptr_t)astore[i].p;
#endif
                break;
            default:
                pl_error_str("native: unknown arg type code");
        }
    }

    /* ---- Dispatch the call ---- */
    union { int32_t i; int64_t l; double d; void *p; } result;
    memset(&result, 0, sizeof(result));

#ifdef HAVE_FFI
    ffi_type *rtype = ffi_type_for(ret_code);
    ffi_cif cif;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned int)nargs, rtype, atypes) != FFI_OK)
        pl_error_str("native: ffi_prep_cif failed");
    ffi_call(&cif, FFI_FN(fn), &result, avalues);
#elif defined(_WIN32)
    win64_fn_t wfn = (win64_fn_t)(uintptr_t)fn;
    result.l = wfn(avals[0], avals[1], avals[2], avals[3],
                   avals[4], avals[5], avals[6], avals[7]);
#endif

    /* ---- Convert result to a Lisp value ---- */
    switch (ret_code[0]) {
        case 'v': return NIL_VAL;
        case 'i': return MAKE_INT(result.i);
        case 'l': return big_from_int64(result.l);
        case 'd':
#if defined(_WIN32) && !defined(HAVE_FFI)
            pl_error_str("native: type 'd' return requires libffi on Windows");
            return NIL_VAL;
#else
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", result.d);
            return MAKE_STR(str_intern(buf, strlen(buf)));
        }
#endif
        case 's': {
            if (!result.p) return NIL_VAL;
            const char *s = (const char *)result.p;
            return MAKE_STR(str_intern(s, strlen(s)));
        }
        case 'p': return big_from_int64((int64_t)(uintptr_t)result.p);
        default:  return NIL_VAL;
    }
}

/* =========================================================================
 * (ffi-open libpath) → T
 *
 * Pre-loads a shared library.  Subsequent native calls to the same path
 * use the cached handle.  Errors out on failure.
 * ===================================================================== */

static Val prim_ffi_open(Val args, Val env)
{
    (void)env;
    Val path_val = CAR(args);
    if (!IS_STR(path_val)) pl_error_str("ffi-open: expected string (library path)");
    ffi_open_lib(str_ptr(STR_IDX(path_val)));
    return T_VAL;
}

/* =========================================================================
 * Registration
 * ===================================================================== */

void ffi_prims_register(void)
{
    prim_register("native",   prim_native,   3, -1);
    prim_register("ffi-open", prim_ffi_open, 1,  1);
    cb_prims_register();
}

#else  /* !HAVE_FFI && !_WIN32 */

void ffi_prims_register(void) {}

#endif /* HAVE_FFI || _WIN32 */
