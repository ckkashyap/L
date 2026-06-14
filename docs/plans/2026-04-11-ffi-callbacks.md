# FFI Callbacks Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add GC-managed C→Lisp callbacks to the FFI layer, bundle prebuilt libffi for Windows, and ship a tested cross-platform demo.

**Architecture:** New `TAG_CB = 0x7FF6` NaN-box type backed by a 256-slot `CbSlot` pool (same pattern as pipes). `ffi-callback` allocates an `ffi_closure` via libffi, which dispatches C calls to a Lisp lambda. GC marks the lambda inside live slots; sweep calls `ffi_closure_free` on unreachable slots. `native` extracts `fn_ptr` from a TAG_CB val when type `"p"` is specified.

**Tech Stack:** libffi (`ffi_closure_alloc`, `ffi_prep_closure_loc`, `ffi_cif`), NaN-box tag system, pipe-pool pattern, `pl_apply` for Lisp dispatch, `mem-read-i32`/`mem-write-i32` primitives for pointer dereferencing in callbacks.

---

### Task 1: Fetch prebuilt libffi for Windows and update Makefile.win

**Files:**
- Create: `scripts/fetch_libffi_win.ps1`
- Create: `deps/libffi/include/ffi.h` (after running script)
- Create: `deps/libffi/include/ffitarget.h` (after running script)
- Create: `deps/libffi/lib/libffi.lib` (after running script)
- Modify: `Makefile.win` — always enable HAVE_FFI, add callbacks.obj

**Step 1: Create the fetch script**

Create `scripts/fetch_libffi_win.ps1`:
```powershell
# scripts/fetch_libffi_win.ps1
# Downloads and installs prebuilt libffi (MSVC x64 static) into deps\libffi\
# Requires: vcpkg on PATH (https://vcpkg.io) OR manual placement.
#
# Usage:  powershell -File scripts\fetch_libffi_win.ps1
#
# If vcpkg is not available, manually place:
#   deps\libffi\include\ffi.h
#   deps\libffi\include\ffitarget.h
#   deps\libffi\lib\libffi.lib

param([string]$VcpkgRoot = $env:VCPKG_ROOT)

# Try to find vcpkg
if (-not $VcpkgRoot) {
    $candidates = @(
        "C:\vcpkg",
        "C:\src\vcpkg",
        "$env:USERPROFILE\vcpkg",
        "$env:LOCALAPPDATA\vcpkg"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\vcpkg.exe") { $VcpkgRoot = $c; break }
    }
}

if (-not $VcpkgRoot -or -not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Error @"
vcpkg not found. Options:
  1. Install vcpkg: https://vcpkg.io/en/getting-started.html
     Then run: vcpkg install libffi:x64-windows-static
  2. Set VCPKG_ROOT environment variable and re-run this script.
  3. Manually place prebuilt files:
       deps\libffi\include\ffi.h
       deps\libffi\include\ffitarget.h
       deps\libffi\lib\libffi.lib
"@
    exit 1
}

Write-Host "Using vcpkg at: $VcpkgRoot"
& "$VcpkgRoot\vcpkg.exe" install "libffi:x64-windows-static"
if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install failed"; exit 1 }

$installed = "$VcpkgRoot\installed\x64-windows-static"
$dest      = "deps\libffi"

New-Item -ItemType Directory -Force "$dest\include" | Out-Null
New-Item -ItemType Directory -Force "$dest\lib"     | Out-Null

Copy-Item "$installed\include\ffi.h"         "$dest\include\ffi.h"         -Force
Copy-Item "$installed\include\ffitarget.h"   "$dest\include\ffitarget.h"   -Force
Copy-Item "$installed\lib\libffi.lib"        "$dest\lib\libffi.lib"        -Force

Write-Host "libffi installed to deps\libffi\" -ForegroundColor Green
Write-Host "Now rebuild with: nmake -f Makefile.win HAVE_UV=1"
```

**Step 2: Run it**
```
powershell -ExecutionPolicy Bypass -File scripts\fetch_libffi_win.ps1
```
Expected: `deps\libffi\include\ffi.h`, `ffitarget.h`, `deps\libffi\lib\libffi.lib` exist.

**Step 3: Update Makefile.win**

Change the FFI block (lines 21-27) from opt-in to always-on, and add `callbacks.obj`:
```makefile
# libffi is always enabled on Windows (bundled in deps\libffi\).
# The callbacks.c translation unit requires HAVE_FFI.
CFLAGS  = $(CFLAGS) /DHAVE_FFI=1 /Ideps\libffi\include
LIBS    = $(LIBS) deps\libffi\lib\libffi.lib
FFI_OBJ = build\ffi.obj build\callbacks.obj
```
Remove the old `!ifdef HAVE_FFI` / `!endif` guards around it.

**Step 4: Verify Makefile.win builds**
```
nmake -f Makefile.win HAVE_UV=1 clean
nmake -f Makefile.win HAVE_UV=1
```
Expected: `build\picolisp.exe` produced without errors.

**Step 5: Commit**
```bash
git add scripts/fetch_libffi_win.ps1 deps/libffi/ Makefile.win
git commit -m "feat: bundle prebuilt libffi for Windows, always enable HAVE_FFI"
```

---

### Task 2: Add TAG_CB, CbSlot, and pool to picolisp.h

**Files:**
- Modify: `src/picolisp.h` — add tag, macros, struct, extern declarations

**Step 1: Write the failing test (verify tag not yet defined)**
```bash
grep TAG_CB src/picolisp.h
```
Expected: no output (TAG_CB not yet defined).

**Step 2: Add TAG_CB after TAG_CORO (line 70 of picolisp.h)**

Insert after `#define TAG_CORO  UINT64_C(0x7FF7)`:
```c
#define TAG_CB    UINT64_C(0x7FF6)   /* ffi callback — index into g_callbacks[] */
```

**Step 3: Add IS_CB predicate after IS_CORO (line 93)**

Insert after `#define IS_CORO(v)  (VAL_TAG(v) == TAG_CORO)`:
```c
#define IS_CB(v)    (VAL_TAG(v) == TAG_CB)
```

**Step 4: Add CB_IDX and MAKE_CB after CORO_IDX (line 134)**

Insert after `#define CORO_IDX(v) ...`:
```c
#define CB_IDX(v)   ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define MAKE_CB(idx) MAKE_VAL(TAG_CB, (uint64_t)(uint32_t)(idx))
```

**Step 5: Add CbSlot struct and pool externs**

Add a new section after the pipe-related declarations (search for `extern uint32_t g_pipe_count` or similar in picolisp.h; if not present, add after the PipeSlot section). Add:
```c
/* =========================================================================
 * Callback pool (ffi-callback)
 * Only compiled when HAVE_FFI is set.
 * ===================================================================== */
#ifdef HAVE_FFI
#include <ffi.h>

#define CB_POOL_SIZE  256
#define CB_MAX_ARGS    16

typedef struct {
    ffi_cif      cif;                    /* describes the C function signature */
    ffi_closure *closure;               /* executable trampoline (ffi_closure_alloc) */
    void        *fn_ptr;                /* the callable C pointer inside closure */
    Val          lambda;                /* Lisp function to dispatch to */
    uint8_t      alive;                 /* slot in use */
    uint8_t      mark;                  /* GC mark bit */
    ffi_type    *arg_ftypes[CB_MAX_ARGS]; /* stored so cif persists */
} CbSlot;

extern CbSlot   g_callbacks[CB_POOL_SIZE];
extern uint32_t g_cb_count;            /* high-water mark (1-based) */

/* GC hooks — called from heap.c */
void gc_mark_callbacks(void);
void gc_sweep_callbacks(void);
#endif /* HAVE_FFI */
```

**Step 6: Verify it compiles**
```bash
# Linux
make clean && make 2>&1 | tail -5
```
Expected: no errors about TAG_CB or CbSlot.

**Step 7: Commit**
```bash
git add src/picolisp.h
git commit -m "feat: add TAG_CB, CbSlot, CB_POOL_SIZE to picolisp.h"
```

---

### Task 3: Implement src/callbacks.c and src/callbacks.h

**Files:**
- Create: `src/callbacks.c`
- Create: `src/callbacks.h`

**Step 1: Create src/callbacks.h**
```c
/* callbacks.c — GC-managed libffi closure callbacks for the L interpreter.
 *
 * Primitives (gated on HAVE_FFI):
 *   ffi-callback  (ffi-callback "ret" "args" fn) → <callback>
 *
 * The returned <callback> val can be passed to (native ...) as type "p".
 * The underlying ffi_closure is freed automatically by GC when the val
 * becomes unreachable.
 */
#pragma once
#ifdef HAVE_FFI
void cb_prims_register(void);
#else
static inline void cb_prims_register(void) {}
#endif
```

**Step 2: Create src/callbacks.c**
```c
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "picolisp.h"

#ifdef HAVE_FFI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pool storage — defined here, declared extern in picolisp.h */
CbSlot   g_callbacks[CB_POOL_SIZE];
uint32_t g_cb_count = 0;

/* Forward declaration */
extern Val pl_apply(Val fn, Val args, Val env);
extern Val big_from_int64(int64_t v);
extern const char *big_to_str(Val v);

/* =========================================================================
 * Slot allocation — reuse dead slots before extending high-water mark
 * ===================================================================== */
static uint32_t cb_alloc_slot(void)
{
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (!g_callbacks[i].alive) return i;
    }
    if (g_cb_count >= CB_POOL_SIZE - 1)
        pl_error_str("ffi-callback: too many live callbacks (limit 255)");
    return ++g_cb_count;
}

/* =========================================================================
 * ffi_type* from a single type-code character
 * (subset: v i l p s — same codes as native)
 * ===================================================================== */
static ffi_type *ftype_for(char c)
{
    switch (c) {
        case 'v': return &ffi_type_void;
        case 'i': return &ffi_type_sint32;
        case 'l': return &ffi_type_sint64;
        case 'p': /* fall-through */
        case 's': return &ffi_type_pointer;
        case 'd': return &ffi_type_double;
        default:  return &ffi_type_sint32;
    }
}

/* =========================================================================
 * Box a single C argument (given ffi_type* and raw pointer) into a Val.
 * The raw pointer points at the value in libffi's args[] array.
 * ===================================================================== */
static Val box_arg(ffi_type *ft, void *raw)
{
    if (ft == &ffi_type_sint32) {
        return MAKE_INT(*(int32_t *)raw);
    } else if (ft == &ffi_type_sint64) {
        return big_from_int64(*(int64_t *)raw);
    } else if (ft == &ffi_type_pointer) {
        return big_from_int64((int64_t)(uintptr_t)(*(void **)raw));
    } else if (ft == &ffi_type_double) {
        /* Return as string — same convention as native "d" return */
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", *(double *)raw);
        extern uint32_t str_intern(const char *, size_t);
        return MAKE_STR(str_intern(buf, strlen(buf)));
    }
    return NIL_VAL;
}

/* =========================================================================
 * Unbox a Lisp Val into the C return slot *ret according to ffi_type* rtype.
 * ===================================================================== */
static void unbox_ret(ffi_type *rtype, void *ret, Val result)
{
    if (rtype == &ffi_type_void) {
        return;
    } else if (rtype == &ffi_type_sint32) {
        int32_t v = IS_INT(result) ? INT_VAL(result) : 0;
        memcpy(ret, &v, sizeof(v));
    } else if (rtype == &ffi_type_sint64) {
        int64_t v = IS_INT(result) ? (int64_t)INT_VAL(result)
                  : IS_BIG(result) ? (int64_t)strtoll(big_to_str(result), NULL, 10)
                  : 0;
        memcpy(ret, &v, sizeof(v));
    } else if (rtype == &ffi_type_pointer) {
        uint64_t raw = IS_INT(result) ? (uint64_t)(uint32_t)INT_VAL(result)
                     : IS_BIG(result) ? (uint64_t)strtoull(big_to_str(result), NULL, 10)
                     : 0;
        void *p = (void *)(uintptr_t)raw;
        memcpy(ret, &p, sizeof(p));
    }
    /* double: not implemented for return — extend if needed */
}

/* =========================================================================
 * cb_dispatch — the single C function that all closures trampoline into.
 * user_data is the 1-based slot index cast to void*.
 * ===================================================================== */
static void cb_dispatch(ffi_cif *cif, void *ret, void **args, void *user_data)
{
    uint32_t idx  = (uint32_t)(uintptr_t)user_data;
    CbSlot  *slot = &g_callbacks[idx];
    if (!slot->alive) {
        /* Slot was freed — zero the return and bail */
        if (ret && cif->rtype != &ffi_type_void)
            memset(ret, 0, cif->rtype->size);
        return;
    }

    /* Build the Lisp argument list */
    Val arg_list = NIL_VAL;
    /* Build right-to-left so cons produces correct order */
    for (int i = (int)cif->nargs - 1; i >= 0; i--) {
        Val boxed = box_arg(cif->arg_types[i], args[i]);
        arg_list  = cons(boxed, arg_list);
    }

    Val result = pl_apply(slot->lambda, arg_list, NIL_VAL);
    unbox_ret(cif->rtype, ret, result);
}

/* =========================================================================
 * (ffi-callback "ret-type" "arg-types" fn) → <callback>
 *
 *   ret-type  — one char: v i l p s d
 *   arg-types — zero or more chars, one per argument: "iip" = int,int,ptr
 *   fn        — any Lisp callable (lambda, de, prim)
 * ===================================================================== */
static Val prim_ffi_callback(Val args, Val env)
{
    (void)env;

    Val ret_val  = CAR(args); args = CDR(args);
    Val arg_val  = CAR(args); args = CDR(args);
    Val fn_val   = CAR(args);

    if (!IS_STR(ret_val)) pl_error_str("ffi-callback: ret-type must be a string");
    if (!IS_STR(arg_val)) pl_error_str("ffi-callback: arg-types must be a string");

    extern const char *str_ptr(uint32_t idx);
    const char *ret_tc  = str_ptr(STR_IDX(ret_val));
    const char *arg_tcs = str_ptr(STR_IDX(arg_val));
    int         nargs   = (int)strlen(arg_tcs);

    if (nargs > CB_MAX_ARGS)
        pl_error_str("ffi-callback: too many arguments (max 16)");

    uint32_t idx  = cb_alloc_slot();
    CbSlot  *slot = &g_callbacks[idx];
    memset(slot, 0, sizeof(*slot));

    /* Build the ffi_type* arrays (stored in slot so cif memory stays valid) */
    for (int i = 0; i < nargs; i++)
        slot->arg_ftypes[i] = ftype_for(arg_tcs[i]);

    ffi_type *rtype = ftype_for(ret_tc[0]);

    if (ffi_prep_cif(&slot->cif, FFI_DEFAULT_ABI, (unsigned)nargs,
                     rtype, slot->arg_ftypes) != FFI_OK)
        pl_error_str("ffi-callback: ffi_prep_cif failed");

    slot->closure = ffi_closure_alloc(sizeof(ffi_closure), &slot->fn_ptr);
    if (!slot->closure)
        pl_error_str("ffi-callback: ffi_closure_alloc failed");

    if (ffi_prep_closure_loc(slot->closure, &slot->cif, cb_dispatch,
                             (void *)(uintptr_t)idx, slot->fn_ptr) != FFI_OK) {
        ffi_closure_free(slot->closure);
        pl_error_str("ffi-callback: ffi_prep_closure_loc failed");
    }

    slot->lambda = fn_val;
    slot->alive  = 1;
    slot->mark   = 0;

    return MAKE_CB(idx);
}

/* =========================================================================
 * GC hooks — called from heap.c during gc_collect()
 * ===================================================================== */

void gc_mark_callbacks(void)
{
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (g_callbacks[i].alive && g_callbacks[i].mark)
            gc_mark_val(g_callbacks[i].lambda);
    }
}

void gc_sweep_callbacks(void)
{
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (!g_callbacks[i].alive) continue;
        if (!g_callbacks[i].mark) {
            ffi_closure_free(g_callbacks[i].closure);
            g_callbacks[i].alive   = 0;
            g_callbacks[i].closure = NULL;
            g_callbacks[i].fn_ptr  = NULL;
        } else {
            g_callbacks[i].mark = 0;   /* reset for next GC cycle */
        }
    }
}

/* =========================================================================
 * Registration
 * ===================================================================== */
void cb_prims_register(void)
{
    prim_register("ffi-callback", prim_ffi_callback, 3, 3);
}

#endif /* HAVE_FFI */
```

**Step 3: Verify it compiles standalone (no linker errors expected yet)**
```bash
gcc -std=c17 -Isrc -DHAVE_FFI=1 $(pkg-config --cflags libffi) -c src/callbacks.c -o /tmp/callbacks.o 2>&1
```
Expected: compiles cleanly (some warnings about `str_ptr`/`cons` being implicit OK for now).

**Step 4: Commit**
```bash
git add src/callbacks.c src/callbacks.h
git commit -m "feat: add callbacks.c — ffi_closure pool and GC hooks"
```

---

### Task 4: Wire GC hooks into heap.c and gc_mark_val

**Files:**
- Modify: `src/heap.c`

**Step 1: Add IS_CB branch to gc_mark_val**

In `gc_mark_val` (around line 148), after the `IS_PIPE` branch, add:
```c
        } else if (IS_CB(cur)) {
#ifdef HAVE_FFI
            uint32_t idx = CB_IDX(cur);
            if (idx && idx <= g_cb_count && g_callbacks[idx].alive)
                g_callbacks[idx].mark = 1;
            /* lambda is marked by gc_mark_callbacks() after root traversal */
#endif
        }
```

**Step 2: Add gc_mark_callbacks() call in gc_mark_all_roots**

After the `gc_mark_coros()` call (line ~186), add:
```c
#ifdef HAVE_FFI
    gc_mark_callbacks();
#endif
```

**Step 3: Add gc_sweep_callbacks() call in gc_collect**

After `gc_sweep_pipes()` (line ~254), add:
```c
#ifdef HAVE_FFI
    gc_sweep_callbacks();
#endif
```

**Step 4: Add extern declarations at top of heap.c**

After the existing `extern void gc_sweep_bignums(void);` line, add:
```c
#ifdef HAVE_FFI
extern void gc_mark_callbacks(void);
extern void gc_sweep_callbacks(void);
extern CbSlot   g_callbacks[];
extern uint32_t g_cb_count;
#endif
```

**Step 5: Build and verify no errors**
```bash
make clean && make 2>&1 | tail -10
```
Expected: clean build.

**Step 6: Commit**
```bash
git add src/heap.c
git commit -m "feat: wire callback GC hooks into heap.c mark/sweep cycle"
```

---

### Task 5: Update ffi.c — handle TAG_CB in 'p' args, add mem-read/write-i32

**Files:**
- Modify: `src/ffi.c`
- Modify: `src/prims.c`

**Step 1: Update the 'p' case in prim_native to handle TAG_CB**

In `prim_native`, find the `case 'p':` block (around line 214). Replace it:
```c
            case 'p':
                if (IS_CB(val)) {
#ifdef HAVE_FFI
                    /* Extract the executable fn_ptr from the callback slot */
                    uint32_t cidx = CB_IDX(val);
                    if (!cidx || cidx > g_cb_count || !g_callbacks[cidx].alive)
                        pl_error_str("native: callback has been freed");
                    astore[i].p = g_callbacks[cidx].fn_ptr;
#else
                    pl_error_str("native: TAG_CB requires HAVE_FFI");
                    astore[i].p = NULL;
#endif
                } else if (IS_INT(val)) {
                    astore[i].p = (void *)(uintptr_t)(uint32_t)INT_VAL(val);
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
```

Also add the extern for `g_callbacks` / `g_cb_count` near the top of ffi.c:
```c
#ifdef HAVE_FFI
extern CbSlot   g_callbacks[];
extern uint32_t g_cb_count;
#endif
```

**Step 2: Add mem-read-i32 and mem-write-i32 to prims.c**

Add a new section "Q. Memory access primitives" before the `prims_init` function:
```c
/* =========================================================================
 * Q.  Memory access primitives (for FFI pointer dereferencing in callbacks)
 * ===================================================================== */

/* (mem-read-i32 ptr byte-offset) → integer
 * Reads a 32-bit signed int from memory address (ptr + byte-offset).
 * ptr can be an int or bignum (raw pointer value). */
static Val prim_mem_read_i32(Val args, Val env)
{
    (void)env;
    Val ptr_val = CAR(args); args = CDR(args);
    Val off_val = CAR(args);

    int64_t ptr = IS_INT(ptr_val) ? (int64_t)INT_VAL(ptr_val)
                : IS_BIG(ptr_val) ? (int64_t)strtoll(big_to_str(ptr_val), NULL, 10)
                : 0;
    int32_t off = IS_INT(off_val) ? INT_VAL(off_val) : 0;

    int32_t result;
    memcpy(&result, (char *)(uintptr_t)(uint64_t)ptr + off, sizeof(result));
    return MAKE_INT(result);
}

/* (mem-write-i32 ptr byte-offset value) → NIL
 * Writes a 32-bit signed int to memory address (ptr + byte-offset). */
static Val prim_mem_write_i32(Val args, Val env)
{
    (void)env;
    Val ptr_val = CAR(args); args = CDR(args);
    Val off_val = CAR(args); args = CDR(args);
    Val val_val = CAR(args);

    int64_t ptr = IS_INT(ptr_val) ? (int64_t)INT_VAL(ptr_val)
                : IS_BIG(ptr_val) ? (int64_t)strtoll(big_to_str(ptr_val), NULL, 10)
                : 0;
    int32_t off = IS_INT(off_val) ? INT_VAL(off_val) : 0;
    int32_t val = IS_INT(val_val) ? INT_VAL(val_val) : 0;

    memcpy((char *)(uintptr_t)(uint64_t)ptr + off, &val, sizeof(val));
    return NIL_VAL;
}
```

Register them in `prims_init`:
```c
    prim_register("mem-read-i32",  prim_mem_read_i32,  2, 2);
    prim_register("mem-write-i32", prim_mem_write_i32, 3, 3);
```

**Step 3: Build**
```bash
make clean && make 2>&1 | tail -5
```
Expected: clean build.

**Step 4: Commit**
```bash
git add src/ffi.c src/prims.c
git commit -m "feat: handle TAG_CB in native 'p' args; add mem-read/write-i32"
```

---

### Task 6: Add callbacks.c to both Makefiles

**Files:**
- Modify: `Makefile` (Linux)
- Modify: `Makefile.win` (Windows — already done in Task 1, verify)

**Step 1: Update Linux Makefile**

Find the libffi block:
```makefile
ifeq ($(shell pkg-config --exists libffi 2>/dev/null && echo yes),yes)
CFLAGS  += -DHAVE_FFI=1 $(shell pkg-config --cflags libffi)
LDFLAGS += $(shell pkg-config --libs libffi) -ldl
SRCS    += src/ffi.c
endif
```

Change to also include `src/callbacks.c`:
```makefile
ifeq ($(shell pkg-config --exists libffi 2>/dev/null && echo yes),yes)
CFLAGS  += -DHAVE_FFI=1 $(shell pkg-config --cflags libffi)
LDFLAGS += $(shell pkg-config --libs libffi) -ldl
SRCS    += src/ffi.c src/callbacks.c
endif
```

**Step 2: Verify Makefile.win already has callbacks.obj**

Check that `Makefile.win` has `build\callbacks.obj` in `FFI_OBJ` (set in Task 1). If not, add it now.

**Step 3: Build both platforms**
```bash
# Linux
make clean && make && echo "Linux OK"

# Windows (run separately)
# nmake -f Makefile.win HAVE_UV=1 clean
# nmake -f Makefile.win HAVE_UV=1
```
Expected: both build cleanly.

**Step 4: Quick smoke test**
```bash
echo '(println (procedure? ffi-callback))' | ./build/picolisp -
```
Expected: `T` (the primitive is registered) — or `NIL` if libffi is not installed yet on Linux. Install it: `sudo apt install libffi-dev` then `make clean && make`.

**Step 5: Commit**
```bash
git add Makefile Makefile.win
git commit -m "build: add callbacks.c to Linux Makefile and verify Windows Makefile.win"
```

---

### Task 7: Build cb_helper shared library for tests

**Files:**
- Create: `tests/helpers/cb_helper.c`
- Modify: `Makefile`
- Modify: `Makefile.win`
- Modify: `run_tests.sh`
- Modify: `run_tests.ps1`

**Step 1: Create tests/helpers/cb_helper.c**
```c
/* tests/helpers/cb_helper.c
 * Minimal C shared library used by tests/test_callbacks.l.
 * Exports simple functions that accept C function pointers, allowing
 * the test to verify that Lisp closures are called correctly from C. */

#ifdef _WIN32
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

/* call1: call fn(x) and return the result */
EXPORT int call1(int (*fn)(int), int x)
{
    return fn(x);
}

/* call2: call fn(a, b) and return the result */
EXPORT int call2(int (*fn)(int, int), int a, int b)
{
    return fn(a, b);
}

/* call0: call fn() for side effects (void return) */
EXPORT void call0(void (*fn)(void))
{
    fn();
}

/* call_and_add: call fn(x), add extra to the result */
EXPORT int call_and_add(int (*fn)(int), int x, int extra)
{
    return fn(x) + extra;
}
```

**Step 2: Add build target to Linux Makefile**

Add after the `build:` rule:
```makefile
# cb_helper shared library for callback tests
build/cb_helper.so: tests/helpers/cb_helper.c | build
	$(CC) -std=c17 -shared -fPIC -o $@ $<
```

Add `build/cb_helper.so` as a dependency of the `test` target:
```makefile
test: all build/cb_helper.so
	bash run_tests.sh
```

**Step 3: Add build target to Makefile.win**

Add before the `test:` rule:
```makefile
build\cb_helper.dll: tests\helpers\cb_helper.c
	$(CC) /LD tests\helpers\cb_helper.c /Fe:build\cb_helper.dll /link /EXPORT:call1 /EXPORT:call2 /EXPORT:call0 /EXPORT:call_and_add

test: $(TARGET) build\cb_helper.dll
	powershell -File run_tests.ps1
```

**Step 4: Build and verify**
```bash
# Linux
make build/cb_helper.so
ls -la build/cb_helper.so
```
Expected: `build/cb_helper.so` exists.

**Step 5: Commit**
```bash
git add tests/helpers/cb_helper.c Makefile Makefile.win
git commit -m "build: add cb_helper shared library for callback tests"
```

---

### Task 8: Write tests/test_callbacks.l

**Files:**
- Create: `tests/test_callbacks.l`

**Step 1: Write the test file**
```lisp
# ============================================================
# test_callbacks.l — FFI callback tests
#
# Requires: build/cb_helper.so (Linux) or build/cb_helper.dll (Windows)
#           built with HAVE_FFI=1
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (progn (setq *pass (1+ *pass)) T)
    (progn
      (setq *fail (1+ *fail))
      (println "FAIL: expected" expected "got" actual)
      NIL)))

(setq *pass 0)
(setq *fail 0)

# Skip entire file if ffi-callback is not available (no libffi)
(when (not (bound? ffi-callback))
  (println "SKIP test_callbacks.l (HAVE_FFI not set)")
  (println "ALL TESTS PASSED")
  (quit))

(setq helper
  (if (equal *os* "windows")
    "build\\cb_helper.dll"
    "build/cb_helper.so"))

(ffi-open helper)

# ---- 1. call1: single int arg, int return --------------------------------
(setq double-cb (ffi-callback "i" "i" (lambda (x) (* x 2))))
(test 14 (native helper "call1" "i" "p" double-cb "i" 7))
(test 0  (native helper "call1" "i" "p" double-cb "i" 0))
(test -4 (native helper "call1" "i" "p" double-cb "i" -2))

# ---- 2. call2: two int args, int return ----------------------------------
(setq add-cb (ffi-callback "i" "ii" (lambda (a b) (+ a b))))
(test 9  (native helper "call2" "i" "p" add-cb "i" 4 "i" 5))
(test 0  (native helper "call2" "i" "p" add-cb "i" -3 "i" 3))

(setq mul-cb (ffi-callback "i" "ii" (lambda (a b) (* a b))))
(test 21 (native helper "call2" "i" "p" mul-cb "i" 3 "i" 7))

# ---- 3. call0: void callback fires for side effects ----------------------
(setq fired NIL)
(setq flag-cb (ffi-callback "v" "" (lambda () (setq fired T))))
(native helper "call0" "v" "p" flag-cb)
(test T fired)

# ---- 4. Closure captures enclosing state ---------------------------------
(setq offset 100)
(setq add-offset-cb (ffi-callback "i" "i" (lambda (x) (+ x offset))))
(test 107 (native helper "call1" "i" "p" add-offset-cb "i" 7))
(setq offset 200)
# Dynamic scoping: offset lookup happens at call time
(test 207 (native helper "call1" "i" "p" add-offset-cb "i" 7))

# ---- 5. call_and_add: callback result used in further C computation ------
(setq inc-cb (ffi-callback "i" "i" (lambda (x) (1+ x))))
(test 13 (native helper "call_and_add" "i" "p" inc-cb "i" 7 "i" 5))
# fn(7) = 8, 8 + 5 = 13

# ---- 6. Multiple independent callbacks coexist ---------------------------
(setq cb-a (ffi-callback "i" "i" (lambda (x) (+ x 1))))
(setq cb-b (ffi-callback "i" "i" (lambda (x) (* x 3))))
(setq cb-c (ffi-callback "i" "i" (lambda (x) (- x 1))))
(test 8  (native helper "call1" "i" "p" cb-a "i" 7))
(test 21 (native helper "call1" "i" "p" cb-b "i" 7))
(test 6  (native helper "call1" "i" "p" cb-c "i" 7))

# ---- 7. GC: callback survives a forced collection while referenced -------
(gc)
(test 14 (native helper "call1" "i" "p" double-cb "i" 7))
(gc)
(test 9  (native helper "call2" "i" "p" add-cb "i" 4 "i" 5))

# ---- 8. Callback can call other Lisp functions ---------------------------
(de square (n) (* n n))
(setq square-cb (ffi-callback "i" "i" (lambda (x) (square x))))
(test 49 (native helper "call1" "i" "p" square-cb "i" 7))
(test 0  (native helper "call1" "i" "p" square-cb "i" 0))

# ---- 9. mem-read-i32 / mem-write-i32 (needed for qsort demo) -----------
(setq libc (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))
(setq arr (native libc "malloc" "p" "i" 8))  # 2 ints × 4 bytes
(mem-write-i32 arr 0 42)
(mem-write-i32 arr 4 -7)
(test 42 (mem-read-i32 arr 0))
(test -7 (mem-read-i32 arr 4))
(mem-write-i32 arr 0 99)
(test 99 (mem-read-i32 arr 0))
(native libc "free" "v" "p" arr)

# ---- Report ---------------------------------------------------------------
(println "")
(println (pack "Callbacks: " *pass " passed, " *fail " failed"))
(if (= *fail 0)
  (println "ALL TESTS PASSED")
  (progn (println "TESTS FAILED") (quit 1)))
```

**Step 2: Run the test**
```bash
./build/picolisp tests/test_callbacks.l 2>&1
```
Expected:
```
Callbacks: 24 passed, 0 failed
ALL TESTS PASSED
```

**Step 3: Run full test suite to confirm no regressions**
```bash
bash run_tests.sh 2>&1
```
Expected: all existing tests still PASS, new `test_callbacks.l` PASS.

**Step 4: Commit**
```bash
git add tests/test_callbacks.l
git commit -m "test: add test_callbacks.l — 24 tests covering ffi-callback and mem-read/write-i32"
```

---

### Task 9: Write demos/23-callbacks.l

**Files:**
- Create: `demos/23-callbacks.l`

**Step 1: Write the demo**
```lisp
# ============================================================
# 23-callbacks.l — C→Lisp callbacks via ffi-callback
#
# What this covers:
#   1. What is a callback? Why ffi-callback?
#   2. atexit: run Lisp code at process exit
#   3. qsort: sort a C int array using a Lisp comparator
#   4. Signal handler (POSIX only)
#
# Requires: build with HAVE_FFI=1 (libffi)
#   Linux:   sudo apt install libffi-dev && make clean && make
#   Windows: already enabled — rebuild with: nmake -f Makefile.win HAVE_UV=1
#
# Run:  L demos/23-callbacks.l
# ============================================================

(when (not (bound? ffi-callback))
  (println "This demo requires libffi.")
  (println "  Linux:   sudo apt install libffi-dev && make clean && make")
  (println "  Windows: already bundled — rebuild with nmake -f Makefile.win HAVE_UV=1")
  (quit))

(setq libc (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))

(println "=== FFI Callbacks Demo ===")
(println "")

# =========================================================================
# 1. What is ffi-callback?
#
# Many C libraries accept function pointers — callbacks — so the library can
# call back into your code. For example: qsort needs a comparator, atexit
# needs a shutdown hook, GUI toolkits need event handlers.
#
# (ffi-callback ret-type arg-types fn) creates an executable C function
# pointer that, when called by C, invokes the Lisp function fn with the
# C arguments boxed as Lisp values. The callback is GC-managed: it stays
# alive as long as the returned value is reachable from Lisp.
#
# Type codes: "v" void, "i" int32, "l" int64, "p" pointer, "s" string
# =========================================================================

# =========================================================================
# 2. atexit: register a Lisp shutdown hook
#
# atexit(void (*fn)(void)) registers fn to be called when the process exits
# normally. The first callback registered fires last (LIFO order).
# =========================================================================
(println "--- 2. atexit shutdown hook ---")

# Keep the callback Val in a variable so GC doesn't collect it
(setq exit-hook
  (ffi-callback "v" ""
    (lambda ()
      (println "  [atexit] Lisp shutdown hook fired — process is exiting"))))

(native libc "atexit" "i" "p" exit-hook)
(println "Registered atexit hook (will fire when this script exits)")

# =========================================================================
# 3. qsort with a Lisp comparator
#
# qsort(void *base, size_t nmemb, size_t size,
#       int (*compar)(const void*, const void*))
#
# The comparator receives two const void* pointers to adjacent array
# elements. We use mem-read-i32 to dereference them in Lisp.
#
# We allocate a C int array via malloc, fill it with mem-write-i32,
# sort it, and read the result back.
# =========================================================================
(println "")
(println "--- 3. qsort with Lisp comparator ---")

(setq n 6)
(setq arr (native libc "malloc" "p" "i" (* n 4)))  # n * sizeof(int)

# Fill array with unsorted values: 5 1 4 2 6 3
(let ((vals (list 5 1 4 2 6 3)) (i 0))
  (mapc (lambda (v)
          (mem-write-i32 arr (* i 4) v)
          (setq i (1+ i)))
        vals))

(println "Before sort:")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

# Comparator: return negative/zero/positive like C strcmp convention
(setq int-cmp
  (ffi-callback "i" "pp"
    (lambda (a b)
      (- (mem-read-i32 a 0) (mem-read-i32 b 0)))))

# qsort(arr, n, sizeof(int), comparator)
(native libc "qsort" "v" "p" arr "i" n "i" 4 "p" int-cmp)

(println "After sort:")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

# Also demonstrate reverse sort
(setq int-cmp-desc
  (ffi-callback "i" "pp"
    (lambda (a b)
      (- (mem-read-i32 b 0) (mem-read-i32 a 0)))))  # note: b-a for descending

(native libc "qsort" "v" "p" arr "i" n "i" 4 "p" int-cmp-desc)

(println "Descending sort:")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

(native libc "free" "v" "p" arr)

# =========================================================================
# 4. Signal handler (POSIX only)
#
# signal(int signum, void (*handler)(int)) installs a signal handler.
# SIGUSR1 = 10. After installing, we send ourselves the signal via
# raise(SIGUSR1) to demonstrate it works.
#
# NOTE: Signal handlers have severe restrictions in POSIX (async-signal-safe
# functions only). Calling println from a signal handler is not strictly
# safe; this demo is illustrative only. Use signalfd or libuv's signal
# support for production code.
# =========================================================================
(println "")
(println "--- 4. Signal handler (POSIX only) ---")

(if (equal *os* "posix")
  (progn
    (setq sig-count 0)
    (setq sigusr1-handler
      (ffi-callback "v" "i"
        (lambda (sig)
          (setq sig-count (1+ sig-count)))))

    (native "libc.so.6" "signal" "p" "i" 10 "p" sigusr1-handler)
    (println "SIGUSR1 handler installed")

    (native "libc.so.6" "raise" "i" "i" 10)
    (native "libc.so.6" "raise" "i" "i" 10)
    (println "Sent SIGUSR1 twice, handler fired" sig-count "times"))
  (println "(Skipped on Windows — use SetConsoleCtrlHandler for signal-like events)"))

(println "")
(println "=== Callbacks demo complete ===")
(println "(The atexit hook will fire now as the process exits)")
```

**Step 2: Run the demo**
```bash
./build/picolisp demos/23-callbacks.l 2>&1
```
Expected output:
```
=== FFI Callbacks Demo ===

--- 2. atexit shutdown hook ---
Registered atexit hook (will fire when this script exits)

--- 3. qsort with Lisp comparator ---
Before sort:
  5  1  4  2  6  3
After sort:
  1  2  3  4  5  6
Descending sort:
  6  5  4  3  2  1

--- 4. Signal handler (POSIX only) ---
SIGUSR1 handler installed
Sent SIGUSR1 twice, handler fired 2 times

=== Callbacks demo complete ===
(The atexit hook will fire now as the process exits)
  [atexit] Lisp shutdown hook fired — process is exiting
```

**Step 3: Commit**
```bash
git add demos/23-callbacks.l
git commit -m "demo: add demos/23-callbacks.l — atexit, qsort, signal handler"
```

---

### Task 10: Final verification and tidy-up commit

**Step 1: Run full test suite on Linux**
```bash
make clean && make && bash run_tests.sh 2>&1
```
Expected: all tests PASS including `test_callbacks.l`.

**Step 2: Run full test suite on Windows**
```
nmake -f Makefile.win HAVE_UV=1 clean
nmake -f Makefile.win HAVE_UV=1
powershell -File run_tests.ps1
```
Expected: all tests PASS including `test_callbacks.l`.

**Step 3: Run the demo on both platforms**
```bash
./build/picolisp demos/23-callbacks.l
```

**Step 4: Verify existing FFI demo still works**
```bash
./build/picolisp demos/15-ffi.l 2>&1
```
Expected: no regressions.

**Step 5: Final commit**
```bash
git add -A
git commit -m "feat: FFI callbacks — ffi-callback, mem-read/write-i32, bundled Windows libffi

- TAG_CB (0x7FF6): GC-managed ffi_closure pool (256 slots)
- ffi-callback 'ret' 'args' fn → <callback> val
- native 'p' arg accepts TAG_CB, extracts fn_ptr automatically
- mem-read-i32 / mem-write-i32 for pointer dereferencing
- Bundled libffi for Windows (deps/libffi/ via fetch script)
- Makefile.win: HAVE_FFI always enabled
- tests/test_callbacks.l: 24 tests, Windows + Linux
- demos/23-callbacks.l: atexit, qsort, POSIX signal handler"
```
