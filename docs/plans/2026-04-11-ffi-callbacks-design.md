# FFI Callbacks Implementation Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add C-callable Lisp callbacks to the FFI layer, with bundled prebuilt libffi for Windows, a real cross-platform demo, and a tested C helper library.

**Architecture:** New `TAG_CB` NaN-box type backed by a 256-slot pool. libffi closures bridge C function pointers to Lisp lambdas. GC marks the lambda inside each live slot; sweep calls `ffi_closure_free` on unreachable slots. `native` automatically extracts the `fn_ptr` when a `TAG_CB` val is passed as type `"p"`.

**Tech Stack:** libffi (ffi_closure_alloc / ffi_prep_closure_loc), existing NaN-box tag system, existing pipe-pool pattern, existing GC mark/sweep hooks.

---

## Section 1 — Bundle prebuilt libffi for Windows

**Files touched:**
- Create: `deps/libffi/include/ffi.h`, `ffitarget.h`
- Create: `deps/libffi/lib/libffi.lib`
- Create: `deps/libffi/bin/libffi-8.dll`
- Modify: `Makefile.win` — link libffi by default (always set `HAVE_FFI=1`)
- Modify: `Makefile` — no change needed (already detects via pkg-config)

**Approach:** Download the official libffi Windows x64 prebuilt package (from the libffi GitHub releases or NuGet `libffi` package). Extract headers + static lib + DLL into `deps/libffi/`. Copy `libffi-8.dll` to `build/` as part of the Windows build target so the exe can find it at runtime.

**Makefile.win change:**
```
# libffi is always enabled on Windows (bundled in deps/libffi/)
CFLAGS  = $(CFLAGS) /DHAVE_FFI=1 /Ideps\libffi\include
LIBS    = $(LIBS) deps\libffi\lib\libffi.lib
```
Add a post-build step: `copy deps\libffi\bin\libffi-8.dll build\`

---

## Section 2 — New TAG_CB type and callback pool

**Files touched:**
- Modify: `src/picolisp.h` — add `TAG_CB`, `MAKE_CB`, `CB_IDX`, `IS_CB`, `CbSlot`, pool externs
- Create: `src/callbacks.c` — pool, dispatch, ffi-callback primitive, GC hooks
- Create: `src/callbacks.h` — declarations
- Modify: `src/heap.c` — call `gc_mark_callbacks()` and `gc_sweep_callbacks()` in GC cycle
- Modify: `src/prims.c` — register `ffi-callback` primitive; add `mem-read-i32` primitive
- Modify: `Makefile` and `Makefile.win` — add `callbacks.c` to SRCS (gated on `HAVE_FFI`)
- Modify: `src/ffi.c` — extract `fn_ptr` when a `TAG_CB` val is passed as type `"p"`

**TAG_CB definition (picolisp.h):**
```c
#define TAG_CB    UINT64_C(0x7FF6)
#define IS_CB(v)  (VAL_TAG(v) == TAG_CB)
#define CB_IDX(v) ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define MAKE_CB(idx) MAKE_VAL(TAG_CB, (uint64_t)(uint32_t)(idx))

#define CB_POOL_SIZE 256

typedef struct {
    ffi_cif      cif;          /* describes the function signature           */
    ffi_closure *closure;      /* ffi_closure_alloc block (executable mem)   */
    void        *fn_ptr;       /* callable C function pointer within closure */
    Val          lambda;       /* the Lisp function to dispatch to           */
    uint8_t      alive;
    uint8_t      mark;
} CbSlot;

extern CbSlot   g_callbacks[CB_POOL_SIZE];
extern uint32_t g_cb_count;
```

**Callback pool (callbacks.c):**
```c
CbSlot   g_callbacks[CB_POOL_SIZE];
uint32_t g_cb_count = 0;

static uint32_t alloc_cb_slot(void) {
    // scan for alive==0 slot first (reuse), then extend g_cb_count
}
```

**C dispatch trampoline:**
```c
static void cb_dispatch(ffi_cif *cif, void *ret, void **args, void *user_data) {
    uint32_t idx = (uint32_t)(uintptr_t)user_data;
    CbSlot  *slot = &g_callbacks[idx];
    // Box each arg: int32→MAKE_INT, int64→bignum/int, pointer→MAKE_INT(cast)
    // Build arg list, call eval_apply(slot->lambda, arg_list)
    // Unbox return value into *ret according to cif->rtype
}
```

**ffi-callback primitive:**
```c
// (ffi-callback "ret-type" "arg-types" fn) → <callback>
// Parses type strings, calls ffi_closure_alloc + ffi_prep_closure_loc
// Returns MAKE_CB(slot_idx)
```

**mem-read-i32 primitive:**
```c
// (mem-read-i32 ptr byte-offset) → integer
// Lets Lisp dereference a C int* in callbacks (needed for qsort comparator)
// ptr is an integer-sized pointer value; reads *(int32_t*)((char*)ptr + offset)
```

**GC hooks (heap.c additions):**
```c
void gc_mark_callbacks(void) {
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (g_callbacks[i].alive) {
            // mark bit already set? no — mark bit is set when the TAG_CB Val
            // is encountered during gc_mark_val (IS_CB branch)
            // Here we just mark the lambda so it stays alive
            gc_mark_val(g_callbacks[i].lambda);
        }
    }
}

void gc_sweep_callbacks(void) {
    for (uint32_t i = 1; i <= g_cb_count; i++) {
        if (g_callbacks[i].alive) {
            if (!g_callbacks[i].mark) {
                ffi_closure_free(g_callbacks[i].closure);
                g_callbacks[i].alive = 0;
            } else {
                g_callbacks[i].mark = 0;   // reset for next cycle
            }
        }
    }
}
```

In `gc_mark_val` (heap.c), add IS_CB branch:
```c
} else if (IS_CB(cur)) {
    uint32_t idx = CB_IDX(cur);
    if (idx && idx <= g_cb_count && g_callbacks[idx].alive) {
        g_callbacks[idx].mark = 1;
        // lambda is marked by gc_mark_callbacks() to avoid re-entrancy issues
    }
}
```

**ffi.c change — extract fn_ptr from TAG_CB:**
```c
// In the "p" argument handling section:
if (IS_CB(arg_val)) {
    uint32_t idx = CB_IDX(arg_val);
    argv_storage[i] = (uint64_t)(uintptr_t)g_callbacks[idx].fn_ptr;
} else {
    argv_storage[i] = (uint64_t)VAL_INT(arg_val);  // existing path
}
```

---

## Section 3 — C helper library for tests

**Files touched:**
- Create: `tests/helpers/cb_helper.c`
- Modify: `Makefile` — add `build/cb_helper.so` target
- Modify: `Makefile.win` — add `build\cb_helper.dll` target

**cb_helper.c:**
```c
// Exported test helpers that accept function pointer callbacks

int call1(int (*fn)(int), int x)           { return fn(x); }
int call2(int (*fn)(int,int), int a, int b) { return fn(a, b); }
void call0(void (*fn)(void))               { fn(); }
int call_and_add(int (*fn)(int), int x, int extra) { return fn(x) + extra; }
```

Built as:
- Linux: `gcc -shared -fPIC -o build/cb_helper.so tests/helpers/cb_helper.c`
- Windows: `cl /LD tests\helpers\cb_helper.c /Fe:build\cb_helper.dll`

---

## Section 4 — Demo: demos/23-callbacks.l

**Files touched:**
- Create: `demos/23-callbacks.l`

**Three examples:**

**1. atexit shutdown hook (cross-platform)**
```lisp
(setq libc (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))
(setq exit-hook (ffi-callback "v" "" (lambda () (println "Goodbye from Lisp!"))))
(native libc "atexit" "i" "p" exit-hook)
(println "atexit registered — will fire at process exit")
```

**2. qsort with Lisp comparator (cross-platform)**
```lisp
; Allocate a 5-element int32 array via malloc, fill it, sort, read back
(setq arr (native libc "malloc" "p" "i" 20))
; fill via a helper write loop using mem-write-i32 (or pack/memcpy)
; define comparator: reads two int32s via mem-read-i32, subtracts
(setq int-cmp (ffi-callback "i" "pp"
  (lambda (a b)
    (- (mem-read-i32 a 0) (mem-read-i32 b 0)))))
(native libc "qsort" "v" "p" arr "i" 5 "i" 4 "p" int-cmp)
; read sorted values back via mem-read-i32
```
NOTE: Requires `mem-read-i32` AND `mem-write-i32` primitives.

**3. Signal handler — POSIX only**
```lisp
(when (equal *os* "posix")
  (setq handler (ffi-callback "v" "i"
    (lambda (sig) (println "Signal received:" sig))))
  (native "libc.so.6" "signal" "p" "i" 10 "p" handler)
  (println "SIGUSR1 handler installed"))
```

---

## Section 5 — Tests: tests/test_callbacks.l

**Files touched:**
- Create: `tests/test_callbacks.l`

**Test cases:**
```lisp
(setq helper (if (equal *os* "windows") "build/cb_helper.dll" "build/cb_helper.so"))
(ffi-open helper)

; Basic call1: callback receives one int, returns one int
(setq double-cb (ffi-callback "i" "i" (lambda (x) (* x 2))))
(test 14 (native helper "call1" "i" "p" double-cb "i" 7))

; Basic call2: callback receives two ints
(setq add-cb (ffi-callback "i" "ii" (lambda (a b) (+ a b))))
(test 9 (native helper "call2" "i" "p" add-cb "i" 4 "i" 5))

; Void callback: fires for side effects
(setq fired NIL)
(setq flag-cb (ffi-callback "v" "" (lambda () (setq fired T))))
(native helper "call0" "v" "p" flag-cb)
(test T fired)

; Closure captures lexical state
(setq n 10)
(setq add-n-cb (ffi-callback "i" "i" (lambda (x) (+ x n))))
(test 17 (native helper "call1" "i" "p" add-n-cb "i" 7))

; GC: callback survives a forced GC while referenced
(gc)
(test 20 (native helper "call1" "i" "p" double-cb "i" 10))

; Multiple concurrent callbacks work independently
(setq cb1 (ffi-callback "i" "i" (lambda (x) (+ x 1))))
(setq cb2 (ffi-callback "i" "i" (lambda (x) (* x 3))))
(test 8  (native helper "call1" "i" "p" cb1 "i" 7))
(test 21 (native helper "call1" "i" "p" cb2 "i" 7))
```

---

## Implementation Order

1. Download + verify libffi Windows prebuilt, place in `deps/libffi/`
2. Update `Makefile.win` to always enable `HAVE_FFI`
3. Add `TAG_CB`, `CbSlot`, pool to `picolisp.h`
4. Implement `src/callbacks.c` + `src/callbacks.h`
5. Add `mem-read-i32` / `mem-write-i32` to `src/prims.c`
6. Wire GC hooks into `src/heap.c`
7. Update `src/ffi.c` to extract `fn_ptr` from TAG_CB vals
8. Add `callbacks.c` to both Makefiles (gated on HAVE_FFI)
9. Build + compile `tests/helpers/cb_helper.c`
10. Add `build/cb_helper.so|dll` to Makefiles
11. Write `tests/test_callbacks.l`
12. Write `demos/23-callbacks.l`
13. Run all tests — verify existing FFI tests still pass
14. Commit
