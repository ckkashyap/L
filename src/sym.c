#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* sym.c -- FNV-1a symbol table, string intern table, and well-known symbols.
 *
 * Both tables use open-address bucket arrays with singly-linked collision
 * chains stored inside the slot arrays themselves (index 0 is reserved as
 * the chain terminator / "not present" sentinel; all valid indices are
 * 1-based).
 *
 * String table  (g_strs / g_str_buckets)
 *   Deduplicates raw byte strings.  Each StrSlot owns a heap-allocated,
 *   NUL-terminated copy of the string and caches its FNV-1a hash.
 *
 * Symbol table  (g_syms / g_sym_buckets)
 *   Maps names (looked up via the string table) to symbol slots.  Each
 *   SymSlot stores: the name's string-table index, a global value (Val),
 *   a property list (Val), the cached name hash, and the chain next ptr.
 *
 * sym_init() bootstraps all well-known symbols defined in picolisp.h.
 */

#include "picolisp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Binary-safe string copy -- always copies exactly n bytes.
 * Unlike strndup, does NOT stop at NUL (needed for SHA-1 digests etc.)
 * ===================================================================== */
static char *pl_memdup(const char *s, size_t n) {
    char *buf = (char *)malloc(n + 1);
    if (!buf) return NULL;
    memcpy(buf, s, n);
    buf[n] = '\0'; /* NUL-terminate for C string compat */
    return buf;
}

/* =========================================================================
 * Global symbol table
 * ===================================================================== */
SymSlot  g_syms[SYM_SLOT_COUNT];
uint32_t g_sym_buckets[SYM_BUCKET_COUNT];
uint32_t g_sym_count = 0;

/* =========================================================================
 * Global string intern table
 * ===================================================================== */
StrSlot  g_strs[STR_SLOT_COUNT];
uint32_t g_str_buckets[STR_BUCKET_COUNT];
uint32_t g_str_count = 0;

/* =========================================================================
 * Well-known symbol indices -- initialised by sym_init()
 * ===================================================================== */
uint32_t g_sym_nil;
uint32_t g_sym_t;
uint32_t g_sym_quote;
uint32_t g_sym_quasiquote;
uint32_t g_sym_unquote;
uint32_t g_sym_unquote_splicing;
uint32_t g_sym_lambda;
uint32_t g_sym_macro;
uint32_t g_sym_at;
uint32_t g_sym_args;
uint32_t g_sym_version;
uint32_t g_sym_if;
uint32_t g_sym_when;
uint32_t g_sym_unless;
uint32_t g_sym_cond;
uint32_t g_sym_and;
uint32_t g_sym_or;
uint32_t g_sym_prog;
uint32_t g_sym_prog1;
uint32_t g_sym_prog2;
uint32_t g_sym_while;
uint32_t g_sym_loop;
uint32_t g_sym_for;
uint32_t g_sym_let;
uint32_t g_sym_letstar;
uint32_t g_sym_letrec;
uint32_t g_sym_letrecstar;
uint32_t g_sym_setq;
uint32_t g_sym_de;
uint32_t g_sym_dm;
uint32_t g_sym_dmacro;
uint32_t g_sym_case;
uint32_t g_sym_catch;
uint32_t g_sym_throw;
uint32_t g_sym_guard;
uint32_t g_sym_do;
uint32_t g_sym_make;
uint32_t g_sym_recur;
uint32_t g_sym_with;
uint32_t g_sym_link;
uint32_t g_sym_chain;
uint32_t g_sym_T;   /* alias: same index as g_sym_t                       */

/* =========================================================================
 * I/O port globals
 * ===================================================================== */
static void file_putc(int c, void *ctx)       { fputc(c, (FILE *)ctx);       }
static void file_puts(const char *s, void *ctx) { fputs(s, (FILE *)ctx);      }
static int  file_getc(void *ctx)               { return fgetc((FILE *)ctx);   }
static void file_ungetc(int c, void *ctx)      { ungetc(c, (FILE *)ctx);      }

Port    g_stdout_port;
Port    g_stderr_port;
Reader *g_current_reader = NULL;
Port   *g_current_output = NULL;

/* =========================================================================
 * Primitive function registry -- populated by prims_init()
 * ===================================================================== */
PrimDef g_prims[1024];
int     g_prim_count = 0;

/* =========================================================================
 * FNV-1a hash  (32-bit variant)
 * ===================================================================== */
static uint32_t fnv1a(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

/* =========================================================================
 * str_intern -- deduplicate a byte string; return 1-based StrSlot index.
 *
 * Algorithm:
 *   1. Hash the input with FNV-1a.
 *   2. Walk the bucket chain looking for an exact match (hash + bytes).
 *   3. On hit: return the existing slot index.
 *   4. On miss: allocate a new slot (g_str_count++, 1-based), copy the
 *      string with STRNDUP, store hash and len, prepend to bucket chain.
 * ===================================================================== */
uint32_t str_intern(const char *ptr, size_t len) {
    uint32_t h      = fnv1a(ptr, len);
    uint32_t bucket = h % STR_BUCKET_COUNT;

    /* Walk existing chain. */
    for (uint32_t idx = g_str_buckets[bucket]; idx != 0; idx = g_strs[idx].next) {
        StrSlot *sl = &g_strs[idx];
        if (sl->hash == h && sl->len == (uint32_t)len &&
            memcmp(sl->ptr, ptr, len) == 0) {
            return idx;   /* found existing interned string                */
        }
    }

    /* Not found -- allocate a new slot. */
    uint32_t idx = ++g_str_count;   /* 1-based                             */
    if (idx >= STR_SLOT_COUNT) {
        /* Hard limit reached; in a production build this would call
         * pl_error_str, but we cannot safely call it here before sym_init
         * completes, so abort.                                             */
        fprintf(stderr, "FATAL: string table overflow (count=%u, limit=%u)\n",
                idx, (unsigned)STR_SLOT_COUNT);
        abort();
    }

    StrSlot *sl  = &g_strs[idx];
    sl->ptr      = pl_memdup(ptr, len);  /* binary-safe heap copy           */
    sl->len      = (uint32_t)len;
    sl->hash     = h;
    /* Prepend to bucket chain. */
    sl->next             = g_str_buckets[bucket];
    g_str_buckets[bucket] = idx;

    return idx;
}

/* =========================================================================
 * sym_intern -- look up or create a symbol by name; return 1-based index.
 *
 * Algorithm:
 *   1. Hash the name.
 *   2. Walk the sym bucket chain; for each candidate compare the name by
 *      looking up its string slot.
 *   3. On hit: return the existing sym index.
 *   4. On miss: intern the name into the string table, allocate a new
 *      SymSlot with value=NIL_VAL and plist=NIL_VAL, prepend to bucket.
 * ===================================================================== */
uint32_t sym_intern(const char *name, size_t len) {
    uint32_t h      = fnv1a(name, len);
    uint32_t bucket = h % SYM_BUCKET_COUNT;

    /* Walk existing chain. */
    for (uint32_t idx = g_sym_buckets[bucket]; idx != 0; idx = g_syms[idx].next) {
        SymSlot *sl = &g_syms[idx];
        if (sl->hash == h) {
            /* Compare via the interned name string for exactness. */
            StrSlot *ns = &g_strs[sl->name_idx];
            if (ns->len == (uint32_t)len &&
                memcmp(ns->ptr, name, len) == 0) {
                return idx;   /* found existing symbol                     */
            }
        }
    }

    /* Not found -- intern the name string first, then allocate sym slot. */
    uint32_t name_idx = str_intern(name, len);

    uint32_t idx = ++g_sym_count;   /* 1-based                             */
    if (idx >= SYM_SLOT_COUNT) {
        fprintf(stderr, "FATAL: symbol table overflow\n");
        abort();
    }

    SymSlot *sl  = &g_syms[idx];
    sl->name_idx = name_idx;
    sl->value    = NIL_VAL;
    sl->plist    = NIL_VAL;
    sl->hash     = h;
    /* Prepend to bucket chain. */
    sl->next             = g_sym_buckets[bucket];
    g_sym_buckets[bucket] = idx;

    return idx;
}

/* =========================================================================
 * Helper accessors
 * ===================================================================== */

/* sym_name -- return the NUL-terminated name of a symbol by slot index.   */
const char *sym_name(uint32_t idx) {
    if (!idx) return "NIL";
    return g_strs[g_syms[idx].name_idx].ptr;
}

/* str_ptr -- return the NUL-terminated bytes for a string slot index.     */
const char *str_ptr(uint32_t idx) {
    if (!idx) return "";
    return g_strs[idx].ptr;
}

/* sym_val -- return the global value of a symbol Val (NIL if not a sym).  */
Val sym_val(Val s) {
    if (!IS_SYM(s)) return NIL_VAL;
    return g_syms[SYM_IDX(s)].value;
}

/* =========================================================================
 * sym_init -- bootstrap all well-known symbols and I/O ports.
 * Must be called once after heap_init() and before any other Lisp code.
 * ===================================================================== */
void sym_init(void) {
    memset(g_sym_buckets, 0, sizeof(g_sym_buckets));
    memset(g_str_buckets, 0, sizeof(g_str_buckets));
    g_sym_count = 0;
    g_str_count = 0;

    /* Core atoms */
    g_sym_nil              = sym_intern("NIL",              3);
    g_sym_t = g_sym_T     = sym_intern("T",                1);
    g_sym_quote            = sym_intern("quote",            5);
    g_sym_quasiquote       = sym_intern("quasiquote",      10);
    g_sym_unquote          = sym_intern("unquote",          7);
    g_sym_unquote_splicing = sym_intern("unquote-splicing",16);
    g_sym_lambda           = sym_intern("lambda",           6);
    g_sym_macro            = sym_intern("macro",            5);

    /* Special variables */
    g_sym_at               = sym_intern("@",               1);
    g_sym_args             = sym_intern("*args*",          6);
    g_sym_version          = sym_intern("*version*",       9);

    /* Control-flow special forms */
    g_sym_if               = sym_intern("if",              2);
    g_sym_when             = sym_intern("when",            4);
    g_sym_unless           = sym_intern("unless",          6);
    g_sym_cond             = sym_intern("cond",            4);
    g_sym_and              = sym_intern("and",             3);
    g_sym_or               = sym_intern("or",              2);

    /* Sequencing */
    g_sym_prog             = sym_intern("prog",            4);
    g_sym_prog1            = sym_intern("prog1",           5);
    g_sym_prog2            = sym_intern("prog2",           5);

    /* Iteration */
    g_sym_while            = sym_intern("while",           5);
    g_sym_loop             = sym_intern("loop",            4);
    g_sym_for              = sym_intern("for",             3);

    /* Binding forms */
    g_sym_let              = sym_intern("let",             3);
    g_sym_letstar          = sym_intern("let*",            4);
    g_sym_letrec           = sym_intern("letrec",          6);
    g_sym_letrecstar       = sym_intern("letrec*",         7);

    /* Assignment / definition */
    g_sym_setq             = sym_intern("setq",            4);
    g_sym_de               = sym_intern("de",              2);
    g_sym_dm               = sym_intern("dm",              2);
    g_sym_dmacro           = sym_intern("dmacro",          6);

    /* Exception / case */
    g_sym_case             = sym_intern("case",            4);
    g_sym_catch            = sym_intern("catch",           5);
    g_sym_throw            = sym_intern("throw",           5);
    g_sym_guard            = sym_intern("guard",           5);

    /* List construction */
    g_sym_do               = sym_intern("do",              2);
    g_sym_make             = sym_intern("make",            4);
    g_sym_recur            = sym_intern("recur",           5);
    g_sym_with             = sym_intern("with",            4);
    g_sym_link             = sym_intern("link",            4);
    g_sym_chain            = sym_intern("chain",           5);

    /* Give T its self-referential value so (= T T) works. */
    g_syms[g_sym_T].value  = MAKE_SYM(g_sym_T);

    /* Set up standard I/O ports. */
    g_stdout_port.putc_fn  = file_putc;
    g_stdout_port.puts_fn  = file_puts;
    g_stdout_port.ctx      = stdout;

    g_stderr_port.putc_fn  = file_putc;
    g_stderr_port.puts_fn  = file_puts;
    g_stderr_port.ctx      = stderr;

    g_current_output       = &g_stdout_port;

    /* g_current_reader is set when a file or string reader is opened;
     * it starts NULL (no active reader).                                   */
}
