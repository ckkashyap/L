/* print.c -- Printer for Lisp values in the L (lispIsPerfect9) interpreter.
 *
 * Provides pl_print (readable) and pl_prin (raw) output, plus helpers.
 */

#include "picolisp.h"
#include "bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Port helpers
 * ===================================================================== */

static void port_putc(Port *p, int c)
{
    p->putc_fn(c, p->ctx);
}

static void port_puts(Port *p, const char *s)
{
    if (p->puts_fn) { p->puts_fn(s, p->ctx); return; }
    while (*s) port_putc(p, (unsigned char)*s++);
}

/* =========================================================================
 * File-backed port
 * ===================================================================== */

typedef struct { FILE *f; } FilePortCtx;

static void file_port_putc(int c, void *ctx)
{
    fputc(c, ((FilePortCtx *)ctx)->f);
}

static void file_port_puts(const char *s, void *ctx)
{
    fputs(s, ((FilePortCtx *)ctx)->f);
}

/* Static storage so callers need not manage lifetime. */
static FilePortCtx s_stdout_ctx;
static FilePortCtx s_stderr_ctx;

Port make_file_port(FILE *f)
{
    /* Use static storage for stdout/stderr; allocate for others. */
    FilePortCtx *ctx;
    if (f == stdout) {
        s_stdout_ctx.f = f;
        ctx = &s_stdout_ctx;
    } else if (f == stderr) {
        s_stderr_ctx.f = f;
        ctx = &s_stderr_ctx;
    } else {
        /* For arbitrary files, heap-allocate (caller responsible for cleanup
         * if they need it, but in practice only stdout/stderr are used this
         * way at startup). */
        ctx = (FilePortCtx *)malloc(sizeof(FilePortCtx));
        if (!ctx) { fprintf(stderr, "print: out of memory\n"); exit(1); }
        ctx->f = f;
    }
    Port p;
    p.putc_fn = file_port_putc;
    p.puts_fn = file_port_puts;
    p.ctx     = ctx;
    return p;
}

/* =========================================================================
 * Buffer-backed port  (for val_to_str)
 * ===================================================================== */

typedef struct { char *buf; size_t len; size_t cap; } BufCtx;

static void buf_putc(int c, void *ctx)
{
    BufCtx *b = (BufCtx *)ctx;
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->buf = (char *)realloc(b->buf, b->cap);
        if (!b->buf) { fprintf(stderr, "print: out of memory\n"); exit(1); }
    }
    b->buf[b->len++] = (char)c;
    b->buf[b->len]   = '\0';
}

Port make_buf_port(char **buf_out, size_t *len_out)
{
    BufCtx *b = (BufCtx *)malloc(sizeof(BufCtx));
    if (!b) { fprintf(stderr, "print: out of memory\n"); exit(1); }
    b->buf = (char *)malloc(64);
    if (!b->buf) { fprintf(stderr, "print: out of memory\n"); exit(1); }
    b->buf[0] = '\0';
    b->len = 0;
    b->cap = 64;

    /* We pass ownership of the BufCtx to the caller via the Port's ctx.
     * val_to_str extracts and frees it. */
    (void)buf_out; (void)len_out; /* these are used by val_to_str below */

    Port p;
    p.putc_fn = buf_putc;
    p.puts_fn = NULL;            /* fall back to per-char fast path */
    p.ctx     = b;
    return p;
}

/* =========================================================================
 * Cycle detection
 * ===================================================================== */

#define CYCLE_SET_SIZE 8192

typedef struct {
    uint32_t slots[CYCLE_SET_SIZE];
    uint32_t count;
    int      initialized;
} CycleSet;

static bool cycle_check(CycleSet *cs, uint32_t idx)
{
    if (!cs->initialized) {
        memset(cs->slots, 0xFF, sizeof(cs->slots)); /* 0xFFFFFFFF = empty */
        cs->count       = 0;
        cs->initialized = 1;
    }
    uint32_t h = (idx * 2654435761u) & (CYCLE_SET_SIZE - 1);
    while (cs->slots[h] != 0xFFFFFFFFu) {
        if (cs->slots[h] == idx) return true; /* cycle detected */
        h = (h + 1) & (CYCLE_SET_SIZE - 1);
    }
    if (cs->count >= CYCLE_SET_SIZE * 3 / 4) {
        /* Table too full -- treat as potential cycle to avoid infinite loop */
        return true;
    }
    cs->slots[h] = idx;
    cs->count++;
    return false;
}

/* =========================================================================
 * Forward declarations
 * ===================================================================== */

static void print_val(Val v, Port *port, bool readable, CycleSet *cs);

/* =========================================================================
 * Integer printing
 * ===================================================================== */

static void print_int(int32_t n, Port *port)
{
    /* Handle INT32_MIN specially to avoid UB on negation */
    if (n == INT32_MIN) {
        port_puts(port, "-2147483648");
        return;
    }
    char buf[16];
    int  i = 0;
    bool neg = false;
    if (n < 0) { neg = true; n = -n; }
    if (n == 0) { port_putc(port, '0'); return; }
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    if (neg) buf[i++] = '-';
    /* Reverse and emit */
    for (int j = i - 1; j >= 0; j--)
        port_putc(port, (unsigned char)buf[j]);
}

/* =========================================================================
 * Symbol name printing
 * We print the raw name; no escaping needed for ordinary output.
 * ===================================================================== */

static void print_sym(uint32_t idx, Port *port)
{
    const char *name = sym_name(idx);
    if (name) port_puts(port, name);
}

/* =========================================================================
 * String printing -- readable mode (with quotes and escapes)
 * ===================================================================== */

static void print_str_readable(uint32_t idx, Port *port)
{
    const char *s = str_ptr(idx);
    port_putc(port, '"');
    if (s) {
        while (*s) {
            unsigned char ch = (unsigned char)*s++;
            switch (ch) {
                case '\n': port_putc(port, '\\'); port_putc(port, 'n');  break;
                case '\t': port_putc(port, '\\'); port_putc(port, 't');  break;
                case '\r': port_putc(port, '\\'); port_putc(port, 'r');  break;
                case '\\': port_putc(port, '\\'); port_putc(port, '\\'); break;
                case '"':  port_putc(port, '\\'); port_putc(port, '"');  break;
                default:   port_putc(port, ch);  break;
            }
        }
    }
    port_putc(port, '"');
}

/* =========================================================================
 * String printing -- raw mode (no quotes, no escaping)
 * ===================================================================== */

static void print_str_raw(uint32_t idx, Port *port)
{
    const char *s = str_ptr(idx);
    if (s) port_puts(port, s);
}

/* =========================================================================
 * List / cons printing
 * ===================================================================== */

static void print_list(Val v, Port *port, bool readable, CycleSet *cs)
{
    port_putc(port, '(');
    bool first = true;

    while (IS_CONS(v)) {
        if (!first) port_putc(port, ' ');
        first = false;

        uint32_t idx = CONS_IDX(v);
        if (cycle_check(cs, idx)) {
            port_puts(port, "...");
            port_putc(port, ')');
            return;
        }

        print_val(g_cells[idx].car, port, readable, cs);
        v = g_cells[idx].cdr;
    }

    if (!IS_NIL(v)) {
        port_puts(port, " . ");
        print_val(v, port, readable, cs);
    }

    port_putc(port, ')');
}

/* =========================================================================
 * Core print dispatcher
 * ===================================================================== */

static void print_val(Val v, Port *port, bool readable, CycleSet *cs)
{
    if (IS_NIL(v)) {
        port_puts(port, "NIL");
        return;
    }

    if (IS_INT(v)) {
        print_int(INT_VAL(v), port);
        return;
    }

    if (IS_SYM(v)) {
        uint32_t idx = SYM_IDX(v);
        if (idx == g_sym_T) {
            port_puts(port, "T");
        } else {
            print_sym(idx, port);
        }
        return;
    }

    if (IS_STR(v)) {
        uint32_t idx = STR_IDX(v);
        if (readable) {
            print_str_readable(idx, port);
        } else {
            print_str_raw(idx, port);
        }
        return;
    }

    if (IS_BIG(v)) {
        char *s = big_to_str(v);
        if (s) { port_puts(port, s); free(s); }
        else   { port_puts(port, "#<bignum>"); }
        return;
    }

    if (IS_FLOAT(v)) {
        char *s = float_to_str(v);
        if (s) { port_puts(port, s); free(s); }
        else   { port_puts(port, "#<float>"); }
        return;
    }

    if (IS_CONS(v)) {
        /* Check if this is a closure or macro (car is lambda/macro symbol) */
        Val car = g_cells[CONS_IDX(v)].car;
        if (IS_SYM(car)) {
            uint32_t sym = SYM_IDX(car);
            if (sym == g_sym_lambda) {
                port_puts(port, "#<lambda>");
                return;
            }
            if (sym == g_sym_macro || sym == g_sym_dm || sym == g_sym_dmacro) {
                port_puts(port, "#<macro>");
                return;
            }
        }
        /* Ordinary list */
        CycleSet cs2;
        cs2.initialized = 0;
        cs2.count       = 0;
        /* Use the passed-in cs for the outer context, but initialise a fresh
         * one for each top-level list so that sibling lists don't false-alarm.
         * However, to detect real cycles in deeply nested structures we use
         * the same cs passed in. */
        (void)cs2;
        print_list(v, port, readable, cs);
        return;
    }

    if (IS_PRIM(v)) {
        uint32_t idx = PRIM_IDX(v);
        port_puts(port, "#<primitive ");
        if (idx < (uint32_t)g_prim_count && g_prims[idx].name) {
            port_puts(port, g_prims[idx].name);
        } else {
            print_int((int32_t)idx, port);
        }
        port_putc(port, '>');
        return;
    }

    if (IS_VEC(v)) {
        uint32_t idx = VEC_IDX(v);
        port_puts(port, "#<vec ");
        if (idx < g_vec_count) {
            print_int((int32_t)g_vec_pool[idx].len, port);
        } else {
            port_puts(port, "?");
        }
        port_putc(port, '>');
        return;
    }

    /* Unknown / EOF sentinel */
    port_puts(port, "#<unknown>");
}

/* =========================================================================
 * Public API
 * ===================================================================== */

void pl_print(Val v, Port *port)
{
    CycleSet cs;
    cs.initialized = 0;
    cs.count       = 0;
    print_val(v, port, /*readable=*/true, &cs);
}

void pl_prin(Val v, Port *port)
{
    CycleSet cs;
    cs.initialized = 0;
    cs.count       = 0;
    print_val(v, port, /*readable=*/false, &cs);
}

void pl_println(Val v, Port *port)
{
    pl_print(v, port);
    port_putc(port, '\n');
}

void pl_prinl(Val v, Port *port)
{
    pl_prin(v, port);
    port_putc(port, '\n');
}

char *val_to_str(Val v)
{
    BufCtx *b = (BufCtx *)malloc(sizeof(BufCtx));
    if (!b) return NULL;
    b->buf = (char *)malloc(64);
    if (!b->buf) { free(b); return NULL; }
    b->buf[0] = '\0';
    b->len = 0;
    b->cap = 64;

    Port p;
    p.putc_fn = buf_putc;
    p.puts_fn = NULL;
    p.ctx     = b;

    CycleSet cs;
    cs.initialized = 0;
    cs.count       = 0;
    print_val(v, &p, /*readable=*/true, &cs);

    /* Transfer ownership of the string buffer to the caller */
    char *result = b->buf;
    free(b);
    return result;
}
