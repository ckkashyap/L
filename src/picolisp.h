#pragma once

/* picolisp.h -- Master header for the L (lispIsPerfect9) interpreter.
 * Included by every .c translation unit in the project.
 *
 * Layout
 * ------
 *   1.  MSVC compatibility shims
 *   2.  Standard headers
 *   3.  NaN-boxed value type (Val / uint64_t)
 *   4.  Tag constants
 *   5.  Core Val macros (tag, payload, constructors, predicates)
 *   6.  Heap / cell pool
 *   7.  Symbol & string tables
 *   8.  Well-known symbol indices
 *   9.  GC root stack
 *  10.  Primitive function type
 *  11.  Mutable vector pool
 *  12.  Dynamic bind stack
 *  13.  make/link accumulator globals
 *  14.  @ result register
 *  15.  I/O port abstraction
 *  16.  Coroutine pool (always compiled)
 *  17.  Function prototypes (heap, sym, reader, eval, print, prims,
 *                            bignum, main, coro, native_io, native_gfx)
 *  18.  Arithmetic helpers
 *  19.  Miscellaneous convenience macros
 */

/* =========================================================================
 * 1.  MSVC compatibility -- must come before any system header
 * ===================================================================== */
#include "msvc_compat.h"

/* =========================================================================
 * 2.  Standard headers
 * ===================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* malloc/free used throughout */

/* =========================================================================
 * 3.  NaN-boxed value type
 *
 * All Lisp values are represented as a single 64-bit unsigned integer.
 * The top 16 bits (63-48) encode the type tag; the lower 48 bits carry
 * either a payload index (for heap-allocated objects) or an immediate
 * integer value.
 *
 * IEEE 754 quiet NaN:  bits 63-52 = 0x7FF, bit 51 = 1 → 0x7FF8_xxxx_xxxx
 * We use tags ABOVE or equal to 0x7FF8 so that they never alias a valid
 * double, keeping the representation unambiguous.
 * ===================================================================== */
typedef uint64_t Val;

/* =========================================================================
 * 4.  Tag constants  (stored in bits 63-48)
 * ===================================================================== */
#define TAG_INT   UINT64_C(0x7FFF)   /* 32-bit signed integer (immediate)  */
#define TAG_SYM   UINT64_C(0x7FFA)   /* symbol -- index into g_syms[]        */
#define TAG_STR   UINT64_C(0x7FFB)   /* interned string -- index into g_strs */
#define TAG_CONS  UINT64_C(0x7FFC)   /* cons cell -- index into g_cells[]    */
#define TAG_PRIM  UINT64_C(0x7FF9)   /* primitive function -- index          */
#define TAG_BIG   UINT64_C(0xFFFE)   /* bignum -- index into g_bignums[]     */
#define TAG_FLOAT UINT64_C(0xFFFB)   /* libbf float -- index into g_bignums[]*/
#define TAG_VEC   UINT64_C(0xFFFC)   /* mutable vector -- index into pool    */
#define TAG_PIPE  UINT64_C(0x7FF8)   /* pipe/process handle -- pool index    */
#define TAG_CORO  UINT64_C(0x7FF7)   /* coroutine handle -- g_coros[] index  */
#define TAG_CB    UINT64_C(0x7FF6)   /* ffi callback -- index into g_callbacks[] */
#define TAG_NIL   UINT64_C(0xFFFF)   /* the empty list / NIL                */

/* =========================================================================
 * 5.  Core Val macros
 * ===================================================================== */

/* --- Raw tag / payload access ----------------------------------------- */
#define VAL_TAG(v)        ((uint64_t)(v) >> 48)
#define VAL_PAYLOAD(v)    ((uint64_t)(v) & UINT64_C(0x0000FFFFFFFFFFFF))
#define MAKE_VAL(tag, payload) \
    ((Val)(((uint64_t)(tag) << 48) | ((uint64_t)(payload) & UINT64_C(0x0000FFFFFFFFFFFF))))

/* --- Type predicates -------------------------------------------------- */
#define IS_NIL(v)   (VAL_TAG(v) == TAG_NIL)
#define IS_INT(v)   (VAL_TAG(v) == TAG_INT)
#define IS_SYM(v)   (VAL_TAG(v) == TAG_SYM)
#define IS_STR(v)   (VAL_TAG(v) == TAG_STR)
#define IS_CONS(v)  (VAL_TAG(v) == TAG_CONS)
#define IS_PRIM(v)  (VAL_TAG(v) == TAG_PRIM)
#define IS_BIG(v)   (VAL_TAG(v) == TAG_BIG)
#define IS_FLOAT(v) (VAL_TAG(v) == TAG_FLOAT)
#define IS_VEC(v)   (VAL_TAG(v) == TAG_VEC)
#define IS_PIPE(v)  (VAL_TAG(v) == TAG_PIPE)
#define IS_CORO(v)  (VAL_TAG(v) == TAG_CORO)
#define IS_CB(v)    (VAL_TAG(v) == TAG_CB)

/* A "pair" is any cons cell; an "atom" is anything that is not a pair. */
#define IS_PAIR(v)  IS_CONS(v)
#define IS_ATOM(v)  (!IS_CONS(v))

/* --- Immediate integer ------------------------------------------------- */
/*
 * The int32_t is stored zero-extended in the lower 32 bits of the payload.
 * We reinterpret through uint32_t to avoid UB on the cast back.
 */
#define MAKE_INT(i) \
    MAKE_VAL(TAG_INT, (uint64_t)(uint32_t)(int32_t)(i))
#define INT_VAL(v) \
    ((int32_t)(uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0x00000000FFFFFFFF)))

/* --- Singleton NIL ----------------------------------------------------- */
#define NIL_VAL  MAKE_VAL(TAG_NIL, 0)

/* --- EOF sentinel (never returned to user code) ----------------------- */
#define EOF_VAL  MAKE_VAL(UINT64_C(0xFFFD), 0)

/* --- Heap-object index extraction ------------------------------------- */
/* All heap objects use the lower 32 bits of the payload as an index.     */
#define SYM_IDX(v)   ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define STR_IDX(v)   ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define CONS_IDX(v)  ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define PRIM_IDX(v)  ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define BIG_IDX(v)   ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define FLOAT_IDX(v) ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define VEC_IDX(v)   ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define PIPE_IDX(v)  ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))

/* --- Heap-object Val constructors ------------------------------------- */
#define MAKE_SYM(idx)   MAKE_VAL(TAG_SYM,  (uint64_t)(uint32_t)(idx))
#define MAKE_STR(idx)   MAKE_VAL(TAG_STR,  (uint64_t)(uint32_t)(idx))
#define MAKE_CONS(idx)  MAKE_VAL(TAG_CONS, (uint64_t)(uint32_t)(idx))
#define MAKE_PRIM(idx)  MAKE_VAL(TAG_PRIM, (uint64_t)(uint32_t)(idx))
#define MAKE_BIG(idx)   MAKE_VAL(TAG_BIG,  (uint64_t)(uint32_t)(idx))
#define MAKE_FLOAT(idx) MAKE_VAL(TAG_FLOAT,(uint64_t)(uint32_t)(idx))
#define MAKE_VEC(idx)   MAKE_VAL(TAG_VEC,  (uint64_t)(uint32_t)(idx))
#define MAKE_PIPE(idx)  MAKE_VAL(TAG_PIPE, (uint64_t)(uint32_t)(idx))
#define MAKE_CORO(idx)  MAKE_VAL(TAG_CORO, (uint64_t)(uint32_t)(idx))
#define CORO_IDX(v)     ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define MAKE_CB(idx)    MAKE_VAL(TAG_CB, (uint64_t)(uint32_t)(idx))
#define CB_IDX(v)       ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))

/* --- Cons-cell accessors ---------------------------------------------- */
/* g_cells is declared below; the macros reference it directly so that
 * callers do not need to pass the array explicitly.                       */
#define CAR(v)   (g_cells[CONS_IDX(v)].car)
#define CDR(v)   (g_cells[CONS_IDX(v)].cdr)

/* Derived accessors */
#define CADR(v)    CAR(CDR(v))
#define CDDR(v)    CDR(CDR(v))
#define CADDR(v)   CAR(CDDR(v))
#define CDDDR(v)   CDR(CDDR(v))
#define CAAR(v)    CAR(CAR(v))
#define CAAAR(v)   CAR(CAAR(v))

/* =========================================================================
 * 6.  Heap / cell pool
 * ===================================================================== */
#define CELL_POOL_SIZE (32 * 1024 * 1024)  /* 32 M cons cells (~512 MB)  */

typedef struct {
    Val     car;
    Val     cdr;
    uint8_t mark;
    uint8_t _pad[7];
} Cell;

extern Cell     g_cells[CELL_POOL_SIZE];
extern uint32_t g_free_head;
extern uint32_t g_n_cells_used;
extern uint32_t g_cells_committed; /* how many cells have been initialized */

/* =========================================================================
 * 7.  Symbol & string tables
 * ===================================================================== */

/* --- Symbol table ----------------------------------------------------- */
#define SYM_BUCKET_COUNT (32  * 1024)        /*  32 K buckets              */
#define SYM_SLOT_COUNT   (128 * 1024)        /* 128 K symbol slots         */

typedef struct {
    uint32_t name_idx;   /* index into g_strs[] (interned name string)     */
    Val      value;      /* global/dynamic value binding                   */
    Val      plist;      /* property list                                  */
    uint32_t hash;       /* cached hash of the name                        */
    uint32_t next;       /* singly-linked bucket chain; 0 = end            */
} SymSlot;

extern SymSlot   g_syms[SYM_SLOT_COUNT];
extern uint32_t  g_sym_buckets[SYM_BUCKET_COUNT];
extern uint32_t  g_sym_count;

/* --- String intern table ---------------------------------------------- */
#define STR_BUCKET_COUNT (512  * 1024)       /* 512 K buckets              */
#define STR_SLOT_COUNT   (2    * 1024 * 1024) /*  2 M string slots         */

typedef struct {
    char    *ptr;        /* heap-allocated, NUL-terminated UTF-8 bytes     */
    uint32_t len;        /* byte length (not including NUL)                */
    uint32_t hash;       /* cached hash                                    */
    uint32_t next;       /* singly-linked bucket chain; 0 = end            */
} StrSlot;

extern StrSlot   g_strs[STR_SLOT_COUNT];
extern uint32_t  g_str_buckets[STR_BUCKET_COUNT];
extern uint32_t  g_str_count;

/* =========================================================================
 * 8.  Well-known symbol indices
 *
 * Every entry is an index into g_syms[], initialised by sym_init().
 * ===================================================================== */
extern uint32_t g_sym_nil;
extern uint32_t g_sym_t;
extern uint32_t g_sym_quote;
extern uint32_t g_sym_quasiquote;
extern uint32_t g_sym_unquote;
extern uint32_t g_sym_unquote_splicing;
extern uint32_t g_sym_lambda;
extern uint32_t g_sym_macro;
extern uint32_t g_sym_at;
extern uint32_t g_sym_args;
extern uint32_t g_sym_version;
extern uint32_t g_sym_if;
extern uint32_t g_sym_when;
extern uint32_t g_sym_unless;
extern uint32_t g_sym_cond;
extern uint32_t g_sym_and;
extern uint32_t g_sym_or;
extern uint32_t g_sym_prog;
extern uint32_t g_sym_prog1;
extern uint32_t g_sym_prog2;
extern uint32_t g_sym_while;
extern uint32_t g_sym_loop;
extern uint32_t g_sym_for;
extern uint32_t g_sym_let;
extern uint32_t g_sym_letstar;
extern uint32_t g_sym_letrec;
extern uint32_t g_sym_letrecstar;
extern uint32_t g_sym_setq;
extern uint32_t g_sym_de;
extern uint32_t g_sym_dm;
extern uint32_t g_sym_dmacro;
extern uint32_t g_sym_case;
extern uint32_t g_sym_catch;
extern uint32_t g_sym_throw;
extern uint32_t g_sym_guard;
extern uint32_t g_sym_do;
extern uint32_t g_sym_make;
extern uint32_t g_sym_recur;
extern uint32_t g_sym_with;
extern uint32_t g_sym_link;
extern uint32_t g_sym_chain;
extern uint32_t g_sym_T;   /* the T (truth) symbol index */

/* Convenience: the Val for the T symbol */
#define T_VAL  MAKE_SYM(g_sym_T)

/* =========================================================================
 * 9.  GC root stack
 *
 * Any local Val that must survive a GC cycle should be PUSH_ROOT()'d
 * before any allocation and POP_ROOT()'d afterwards.
 * ===================================================================== */
#define GC_ROOT_STACK_SIZE (256 * 1024)

extern Val g_gc_roots[GC_ROOT_STACK_SIZE];
extern int g_gc_root_top;

#define PUSH_ROOT(v)  (g_gc_roots[g_gc_root_top++] = (v))
#define POP_ROOT()    (g_gc_root_top--)
#define PEEK_ROOT()   (g_gc_roots[g_gc_root_top - 1])

/* =========================================================================
 * 10.  Primitive function type
 * ===================================================================== */
typedef Val (*PrimFn)(Val args, Val env);

typedef struct {
    const char *name;
    PrimFn      fn;
    int         min_args;   /* -1 = variadic with no minimum              */
    int         max_args;   /* -1 = variadic / unlimited                  */
} PrimDef;

extern PrimDef g_prims[];
extern int     g_prim_count;

/* =========================================================================
 * 11.  Mutable vector pool
 * ===================================================================== */
#define VEC_POOL_SIZE (64 * 1024)   /* 64 K vector slots                  */

typedef struct {
    Val      *data;   /* heap-allocated array of Val                       */
    uint32_t  len;    /* current number of elements                        */
    uint32_t  cap;    /* allocated capacity                                */
    uint8_t   mark;   /* GC mark bit                                       */
} VecSlot;

extern VecSlot   g_vec_pool[VEC_POOL_SIZE];
extern uint32_t  g_vec_count;

/* =========================================================================
 * 12.  Dynamic bind stack
 *
 * Used by dynamic-wind / fluid-let style bindings: each frame records the
 * symbol that was rebound and its saved previous value so that unbinding
 * can restore it.
 * ===================================================================== */
#define BIND_STACK_SIZE 65536

typedef struct {
    uint32_t sym_idx;
    Val      saved;
} BindFrame;

extern BindFrame g_bind_stack[BIND_STACK_SIZE];
extern int       g_bind_top;

/* =========================================================================
 * 13.  make / link accumulator globals
 *
 * (make ...) and (link ...) maintain a mutable list head/tail during
 * construction so that appending is O(1).
 * ===================================================================== */
extern Val g_make_head;
extern Val g_make_tail;

/* =========================================================================
 * 14.  @ result register
 *
 * Holds the result of the most recent expression evaluated in an
 * interactive top-level or (make ...) body.
 * ===================================================================== */
extern Val g_at;

/* =========================================================================
 * 15.  I/O port abstraction
 * ===================================================================== */

/* Reader -- character-at-a-time input source with one-character pushback.
 * The context is embedded in the struct itself so that nested load() calls
 * do not clobber a shared static context. */
typedef struct Reader {
    int  (*getc_fn  )(void *ctx);
    void (*ungetc_fn)(int c, void *ctx);
    void *ctx;
    int   line;
    int   col;
    /* Embedded context storage (avoids static globals, allows re-entrant loads) */
    union {
        struct { FILE *f; }                               file_ctx;
        struct { const char *s; size_t pos; size_t len; } str_ctx;
    } u;
} Reader;

/* Port -- output sink.  putc_fn is required (one char at a time);
 * puts_fn is optional (fast path for full-string writes; bulk APIs
 * like fputs are dramatically faster than fputc-per-char). */
typedef struct {
    void (*putc_fn)(int c, void *ctx);
    void (*puts_fn)(const char *s, void *ctx);   /* may be NULL */
    void *ctx;
} Port;

extern Port    g_stdout_port;
extern Port    g_stderr_port;
extern Reader *g_current_reader;
extern Port   *g_current_output;

/* =========================================================================
 * 16.  Coroutine pool  (always compiled)
 * ===================================================================== */
#define CORO_POOL_SIZE 4096

typedef struct {
    void      *fiber;               /* HANDLE from CreateFiber (Windows)   */
    Val        send_val;            /* value sent in via co-resume          */
    Val        yield_val;           /* value yielded or final return value  */
    int        alive;               /* 1 = running/suspended, 0 = done     */
    Val        fn;                  /* Lisp function body                  */
    /* GC root stack snapshot -- populated on yield, consumed on resume.   */
    int        gc_root_base;        /* g_gc_root_top when coro was resumed  */
    Val       *saved_gc_roots;      /* malloc'd slice; NULL when running   */
    int        saved_gc_root_count;
    /* Dynamic bind stack snapshot */
    int        bind_base;           /* g_bind_top when coro was resumed    */
    BindFrame *saved_binds;         /* malloc'd slice; NULL when running   */
    int        saved_bind_count;
    /* Who suspended us: UINT32_MAX = main thread, else g_coros[] index.
     * Set by co-resume before switching; used by yield/return to switch
     * back to the right fiber (enabling coroutines calling co-resume). */
    uint32_t   caller_coro_idx;
} Coro;

extern Coro     g_coros[];
extern uint32_t g_coro_count;

/* =========================================================================
 * 17.  Function prototypes
 * ===================================================================== */

/* --- heap.c ----------------------------------------------------------- */
void     heap_init(void);
uint32_t heap_alloc_cell(void);
Val      pl_cons(Val car, Val cdr);
void     gc_collect(void);
void     gc_mark_val(Val v);
void     mark_vec(uint32_t idx);

/* --- sym.c ------------------------------------------------------------ */
void        sym_init(void);
uint32_t    sym_intern(const char *name, size_t len);
uint32_t    str_intern(const char *ptr, size_t len);
const char *sym_name(uint32_t idx);
const char *str_ptr(uint32_t idx);
Val         sym_val(Val s);

/* --- reader.c --------------------------------------------------------- */
/* In-place initializers (preferred -- Reader must be at its final address). */
void   reader_init_file(Reader *r, FILE *f);
void   reader_init_string(Reader *r, const char *s);
Val    read_one(Reader *r);
Val    read_from_string(const char *s);

/* Convenience macros that initialize a Reader variable in-place.
 * Usage:  Reader r; make_file_reader(r, f);   -- not: Reader r = make_file_reader(f); */
#define make_file_reader(r, f)    reader_init_file(&(r),  (f))
#define make_string_reader(r, s)  reader_init_string(&(r), (s))

/* --- eval.c ----------------------------------------------------------- */
Val  eval(Val expr, Val env);
Val  eval_list(Val lst, Val env);
Val  eval_body(Val body, Val env);
Val  pl_apply(Val fn, Val args, Val env);
int  dynamic_bind(uint32_t sym_idx, Val new_val);
void dynamic_unbind_to(int mark);

/* --- print.c ---------------------------------------------------------- */
void  pl_print(Val v, Port *port);    /* print with escape sequences      */
void  pl_prin(Val v, Port *port);     /* print without surrounding quotes */
void  pl_println(Val v, Port *port);  /* pl_print + newline               */
void  pl_prinl(Val v, Port *port);    /* pl_prin  + newline               */
char *val_to_str(Val v);              /* heap-allocated string repr.      */
Port  make_file_port(FILE *f);
Port  make_buf_port(char **buf, size_t *len);

/* --- prims.c ---------------------------------------------------------- */
void prims_init(void);
void prim_register(const char *name, PrimFn fn, int min_args, int max_args);

/* --- bignum.c --------------------------------------------------------- */
void  bignum_init(void);
Val   big_from_int64(int64_t n);
Val   big_from_str(const char *s, int base);
Val   big_add(Val a, Val b);
Val   big_sub(Val a, Val b);
Val   big_mul(Val a, Val b);
Val   big_div(Val a, Val b);
Val   big_mod(Val a, Val b);
Val   big_rem(Val a, Val b);
Val   big_pow(Val a, Val b);
Val   big_gcd(Val a, Val b);
Val   big_lcm(Val a, Val b);
Val   big_abs(Val a);
Val   big_neg(Val a);
int   big_cmp(Val a, Val b);
Val   big_normalize(Val v);
Val   to_big(Val v);
char *big_to_str(Val v);
bool  big_is_zero(Val v);
bool  big_is_neg(Val v);
Val   big_bitand(Val a, Val b);
Val   big_bitor(Val a, Val b);
Val   big_bitxor(Val a, Val b);
Val   big_bitnot(Val a);
Val   big_shl(Val a, int n);
Val   big_shr(Val a, int n);
void  gc_mark_bignums(void);
void  gc_sweep_bignums(void);

/* --- main.c ----------------------------------------------------------- */
void load_file(const char *path);
void pl_error(const char *msg, Val irritant);
void pl_error_str(const char *msg);

/* --- coro.c --------------------------------------------------------------- */
void coro_init(void);
void gc_mark_coros(void);

/* --- pipe_posix / pipe_win32 ---------------------------------------------- */
void mark_pipe(uint32_t idx);
void gc_sweep_pipes(void);
void pipe_prims_register(void);

/* --- ffi.c ------------------------------------------------------------ */
#if defined(HAVE_FFI) || defined(_WIN32)
void ffi_prims_register(void);
#endif

/* =========================================================================
 * 18.  Arithmetic helpers
 *
 * Dispatch over INT / BIG transparently; promotion to bignum happens
 * automatically when int32 arithmetic would overflow.
 * ===================================================================== */
Val pl_add(Val a, Val b);
Val pl_sub(Val a, Val b);
Val pl_mul(Val a, Val b);
Val pl_div(Val a, Val b);
Val pl_mod(Val a, Val b);
Val pl_rem(Val a, Val b);
int pl_cmp(Val a, Val b);   /* returns -1, 0, or +1                       */
Val pl_max(Val a, Val b);
Val pl_min(Val a, Val b);
Val pl_abs(Val v);
Val pl_neg(Val v);

/* =========================================================================
 * 19.  Miscellaneous convenience macros
 * ===================================================================== */

/* List length -- walks CDR chain; stops at NIL (not a proper-list check). */
static inline int val_list_len(Val v)
{
    int n = 0;
    while (IS_CONS(v)) { v = CDR(v); ++n; }
    return n;
}

/* Quick boolean test matching PicoLisp semantics: NIL is false, all else
 * is true (including the integer 0, which is unusual but intentional).   */
#define IS_FALSE(v)  IS_NIL(v)
#define IS_TRUE(v)   (!IS_NIL(v))

/* Construct a singleton list. */
#define LIST1(a)         pl_cons((a), NIL_VAL)
#define LIST2(a, b)      pl_cons((a), LIST1(b))
#define LIST3(a, b, c)   pl_cons((a), LIST2((b), (c)))

/* Short-hands for common tag checks on raw uint64_t tag values. */
#define TAG_IS_HEAP(tag) \
    ((tag) == TAG_SYM || (tag) == TAG_STR || (tag) == TAG_CONS || \
     (tag) == TAG_PRIM || (tag) == TAG_BIG || (tag) == TAG_VEC || \
     (tag) == TAG_PIPE)
