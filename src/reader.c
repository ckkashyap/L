/* reader.c -- PicoLisp-compatible S-expression reader for L (lispIsPerfect9).
 *
 * Reads S-expressions from a character source (file or string).
 * Produces NaN-boxed Val values as defined in picolisp.h.
 */

#include "picolisp.h"
#include "bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * Reader context types
 * ===================================================================== */

typedef struct { FILE *f; } FileCtx;
typedef struct { const char *s; size_t pos; size_t len; } StrCtx;

static int file_getc_fn(void *ctx)
{
    return fgetc(((FileCtx *)ctx)->f);
}

static void file_ungetc_fn(int c, void *ctx)
{
    ungetc(c, ((FileCtx *)ctx)->f);
}

static int str_getc_fn(void *ctx)
{
    StrCtx *sc = (StrCtx *)ctx;
    if (sc->pos >= sc->len) return EOF;
    return (unsigned char)sc->s[sc->pos++];
}

static void str_ungetc_fn(int c, void *ctx)
{
    StrCtx *sc = (StrCtx *)ctx;
    if (sc->pos > 0 && c != EOF) sc->pos--;
}

/* =========================================================================
 * Public constructors
 * ===================================================================== */

/* reader_init_file / reader_init_string
 *
 * In-place initializers: the Reader must already be allocated at its final
 * address (e.g. as a stack variable) before calling these.  They set r->ctx
 * to point into r->u, which is safe because the struct won't move.
 */
void reader_init_file(Reader *r, FILE *f)
{
    r->getc_fn      = file_getc_fn;
    r->ungetc_fn    = file_ungetc_fn;
    r->u.file_ctx.f = f;
    r->ctx          = &r->u.file_ctx;
    r->line         = 1;
    r->col          = 0;
}

void reader_init_string(Reader *r, const char *s)
{
    r->getc_fn         = str_getc_fn;
    r->ungetc_fn       = str_ungetc_fn;
    r->u.str_ctx.s     = s;
    r->u.str_ctx.pos   = 0;
    r->u.str_ctx.len   = strlen(s);
    r->ctx             = &r->u.str_ctx;
    r->line            = 1;
    r->col             = 0;
}

/* Legacy wrappers -- still usable for simple (non-nested) cases but the
 * ctx pointer in the returned struct points into the callee stack frame,
 * which becomes invalid after return.  Kept only for the read_from_string
 * helper where the Reader never escapes the function.  All load paths use
 * the init functions above. */
Reader reader_from_file(FILE *f)
{
    Reader r;
    reader_init_file(&r, f);
    /* After the by-value return the embedded ctx pointer will be stale.
     * The only safe use is to immediately pass &r to read_one; callers
     * that store the Reader must use reader_init_file instead. */
    return r;
}

Reader reader_from_string(const char *s)
{
    Reader r;
    reader_init_string(&r, s);
    return r;
}

/* =========================================================================
 * Low-level character I/O
 * ===================================================================== */

static int reader_getc(Reader *r)
{
    int c = r->getc_fn(r->ctx);
    if (c == '\n') { r->line++; r->col = 0; }
    else if (c != EOF) { r->col++; }
    return c;
}

static void reader_ungetc(Reader *r, int c)
{
    if (c == '\n') { r->line--; }
    else if (c != EOF) { r->col--; }
    r->ungetc_fn(c, r->ctx);
}

/* =========================================================================
 * Dynamic buffer for atom / string accumulation
 * ===================================================================== */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} DynBuf;

static void buf_init(DynBuf *b)
{
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void buf_free(DynBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void buf_push(DynBuf *b, char c)
{
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->data = (char *)realloc(b->data, b->cap);
        if (!b->data) { fprintf(stderr, "reader: out of memory\n"); exit(1); }
    }
    b->data[b->len++] = c;
    b->data[b->len]   = '\0';
}

static void buf_reset(DynBuf *b)
{
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

/* =========================================================================
 * Number / symbol parsing
 * ===================================================================== */

/* Returns true if the buffer looks like a decimal integer. */
static bool looks_like_int(const char *s, size_t len)
{
    if (len == 0) return false;
    size_t i = 0;
    if (s[i] == '-') {
        i++;
        if (i >= len) return false; /* bare '-' is a symbol */
    }
    for (; i < len; i++) {
        if (!isdigit((unsigned char)s[i])) return false;
    }
    return true;
}

static Val parse_number_or_sym(const char *buf, size_t len)
{
    if (len == 0) return NIL_VAL;

    /* NIL */
    if (len == 3 && buf[0] == 'N' && buf[1] == 'I' && buf[2] == 'L')
        return NIL_VAL;

    /* T */
    if (len == 1 && buf[0] == 'T')
        return MAKE_SYM(g_sym_T);

    /* Attempt integer parse */
    if (looks_like_int(buf, len)) {
        char *end;
        long long ll = strtoll(buf, &end, 10);
        if (end == buf + len) {
            if (ll >= INT32_MIN && ll <= INT32_MAX) {
                return MAKE_INT((int32_t)ll);
            } else {
                return big_from_str(buf, 10);
            }
        }
    }

    /* Float literal: contains '.' with digits around it (e.g. 3.14, -0.5, .5) */
    {
        int has_dot = 0, has_digit = 0;
        size_t i = 0;
        if (i < len && (buf[i] == '-' || buf[i] == '+')) i++;
        for (; i < len; i++) {
            if (buf[i] == '.' && !has_dot) { has_dot = 1; continue; }
            if (buf[i] == 'e' || buf[i] == 'E') { i++; if (i < len && (buf[i]=='-'||buf[i]=='+')) i++; continue; }
            if (isdigit((unsigned char)buf[i])) { has_digit = 1; continue; }
            has_dot = 0; break; /* non-numeric char */
        }
        if (has_dot && has_digit) {
            return float_from_str(buf);
        }
    }

    /* Symbol -- intern it */
    uint32_t idx = sym_intern(buf, len);
    return MAKE_SYM(idx);
}

/* =========================================================================
 * Forward declarations
 * ===================================================================== */

static Val read_val(Reader *r);

/* =========================================================================
 * Whitespace / comment skipping
 * ===================================================================== */

static void skip_whitespace_and_comments(Reader *r)
{
    for (;;) {
        int c = reader_getc(r);
        if (c == EOF) return;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '#') {
            /* PicoLisp-style comment: # to end of line.
             * BUT '#' can also start character literals (#\x) and
             * other reader forms -- we only treat '#' as a comment
             * when it appears at the start of a token AND is followed
             * by a space or end-of-line.  Actually per spec: '#' to
             * end of line is the comment syntax.  We handle '#' reader
             * macros inside read_val; here we consume line comments
             * only when '#' is a standalone comment marker.
             *
             * To distinguish, peek at the next char:
             *   - if next char is '\\' -> not a comment, push back both
             *   - otherwise -> treat as line comment
             */
            int next = reader_getc(r);
            if (next == '\\') {
                /* character literal -- push both back */
                reader_ungetc(r, next);
                reader_ungetc(r, c);
                return;
            }
            /* It's a comment -- consume until end of line */
            if (next != '\n' && next != EOF) {
                for (;;) {
                    int cc = reader_getc(r);
                    if (cc == '\n' || cc == EOF) break;
                }
            }
            continue;
        }
        /* Not whitespace / comment -- put back and return */
        reader_ungetc(r, c);
        return;
    }
}

/* =========================================================================
 * Control-char escape inside atoms and strings
 * ^A..^Z = 1..26, ^[ = 27, ^\ = 28, ^] = 29, ^^= 30, ^_ = 31
 * ===================================================================== */

static int decode_caret_escape(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c == '[')  return 27;
    if (c == '\\') return 28;
    if (c == ']')  return 29;
    if (c == '^')  return 30;
    if (c == '_')  return 31;
    return c; /* unknown -- return as-is */
}

/* =========================================================================
 * String literal reading  "..."
 * ===================================================================== */

static Val read_string(Reader *r)
{
    DynBuf buf;
    buf_init(&buf);

    for (;;) {
        int c = reader_getc(r);
        if (c == EOF) {
            buf_free(&buf);
            pl_error_str("read error: unterminated string literal");
            return NIL_VAL;
        }
        if (c == '"') break;

        if (c == '\\') {
            int esc = reader_getc(r);
            switch (esc) {
                case 'n':  buf_push(&buf, '\n'); break;
                case 't':  buf_push(&buf, '\t'); break;
                case 'r':  buf_push(&buf, '\r'); break;
                case '\\': buf_push(&buf, '\\'); break;
                case '"':  buf_push(&buf, '"');  break;
                case EOF:
                    buf_free(&buf);
                    pl_error_str("read error: EOF in string escape");
                    return NIL_VAL;
                default:
                    buf_push(&buf, (char)esc);
                    break;
            }
        } else if (c == '^') {
            int esc = reader_getc(r);
            if (esc == EOF) {
                buf_free(&buf);
                pl_error_str("read error: EOF after ^ in string");
                return NIL_VAL;
            }
            buf_push(&buf, (char)decode_caret_escape(esc));
        } else {
            buf_push(&buf, (char)c);
        }
    }

    uint32_t idx = str_intern(buf.data ? buf.data : "", buf.len);
    buf_free(&buf);
    return MAKE_STR(idx);
}

/* =========================================================================
 * Atom / symbol reading
 * Reads until a delimiter: whitespace, (, ), ", ;, EOF.
 * '#' at start is handled by the caller; '#' inside an atom is valid.
 * ===================================================================== */

static bool is_delimiter(int c)
{
    if (c == EOF)  return true;
    if (c == '(' || c == ')') return true;
    if (c == '"')  return true;
    if (c == ';')  return true;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return true;
    return false;
}

static Val read_atom(Reader *r, int first_char)
{
    DynBuf buf;
    buf_init(&buf);

    /* Accumulate first char (already read by caller) */
    if (first_char == '^') {
        /* caret escape at start */
        int esc = reader_getc(r);
        if (esc == EOF) {
            buf_free(&buf);
            pl_error_str("read error: EOF after ^ in atom");
            return NIL_VAL;
        }
        buf_push(&buf, (char)decode_caret_escape(esc));
    } else {
        buf_push(&buf, (char)first_char);
    }

    for (;;) {
        int c = reader_getc(r);
        if (is_delimiter(c)) {
            if (c != EOF) reader_ungetc(r, c);
            break;
        }
        if (c == '^') {
            int esc = reader_getc(r);
            if (esc == EOF) {
                buf_free(&buf);
                pl_error_str("read error: EOF after ^ in atom");
                return NIL_VAL;
            }
            buf_push(&buf, (char)decode_caret_escape(esc));
        } else {
            buf_push(&buf, (char)c);
        }
    }

    Val v = parse_number_or_sym(buf.data ? buf.data : "", buf.len);
    buf_free(&buf);
    return v;
}

/* =========================================================================
 * Transient symbol |name| reading
 * ===================================================================== */

static Val read_transient_sym(Reader *r)
{
    DynBuf buf;
    buf_init(&buf);

    for (;;) {
        int c = reader_getc(r);
        if (c == EOF) {
            buf_free(&buf);
            pl_error_str("read error: unterminated transient symbol '|...|'");
            return NIL_VAL;
        }
        if (c == '|') break;
        buf_push(&buf, (char)c);
    }

    uint32_t idx = sym_intern(buf.data ? buf.data : "", buf.len);
    buf_free(&buf);
    return MAKE_SYM(idx);
}

/* =========================================================================
 * Character literal  #\x  #\space  #\tab  #\newline  #\return
 * ===================================================================== */

static Val read_char_literal(Reader *r)
{
    /* Already consumed '#' and '\' */
    DynBuf buf;
    buf_init(&buf);

    /* Read the name/char */
    int c = reader_getc(r);
    if (c == EOF) {
        pl_error_str("read error: EOF after #\\");
        return NIL_VAL;
    }
    buf_push(&buf, (char)c);

    /* Read rest of name if alphabetic */
    if (isalpha((unsigned char)c)) {
        for (;;) {
            int nc = reader_getc(r);
            if (is_delimiter(nc)) {
                if (nc != EOF) reader_ungetc(r, nc);
                break;
            }
            buf_push(&buf, (char)nc);
        }
    }

    Val result;
    if (buf.len == 1) {
        /* Single character */
        char s[2] = { buf.data[0], '\0' };
        uint32_t idx = str_intern(s, 1);
        result = MAKE_STR(idx);
    } else if (strcmp(buf.data, "space") == 0) {
        uint32_t idx = str_intern(" ", 1);
        result = MAKE_STR(idx);
    } else if (strcmp(buf.data, "tab") == 0) {
        uint32_t idx = str_intern("\t", 1);
        result = MAKE_STR(idx);
    } else if (strcmp(buf.data, "newline") == 0) {
        uint32_t idx = str_intern("\n", 1);
        result = MAKE_STR(idx);
    } else if (strcmp(buf.data, "return") == 0) {
        uint32_t idx = str_intern("\r", 1);
        result = MAKE_STR(idx);
    } else {
        /* Unknown name -- treat as symbol or single char */
        uint32_t idx = str_intern(buf.data, buf.len);
        result = MAKE_STR(idx);
    }

    buf_free(&buf);
    return result;
}

/* =========================================================================
 * List reading  (...)
 * ===================================================================== */

static Val read_list(Reader *r)
{
    /* We build the list using GC roots to stay safe across allocations.
     * Strategy: read elements one by one, consing in reverse, then reverse.
     * Actually we build forward using a tail pointer approach with roots. */

    skip_whitespace_and_comments(r);
    int c = reader_getc(r);
    if (c == ')') return NIL_VAL; /* empty list */
    reader_ungetc(r, c);

    /* head and tail of the list being built */
    Val head = NIL_VAL;
    Val tail = NIL_VAL;

    PUSH_ROOT(head);
    PUSH_ROOT(tail);

    for (;;) {
        skip_whitespace_and_comments(r);
        int ch = reader_getc(r);
        if (ch == EOF) {
            POP_ROOT(); POP_ROOT();
            pl_error_str("read error: EOF inside list");
            return NIL_VAL;
        }
        if (ch == ')') break;

        /* Dotted pair  (a . b) */
        if (ch == '.') {
            /* Peek: if next char is a delimiter, this is the dot of a pair */
            int nxt = reader_getc(r);
            reader_ungetc(r, nxt);
            if (is_delimiter(nxt)) {
                /* Read the cdr expression */
                Val cdr_val = read_val(r);
                /* Update the stored roots (head may be stale after alloc) */
                head = g_gc_roots[g_gc_root_top - 2];
                tail = g_gc_roots[g_gc_root_top - 1];

                if (IS_NIL(tail)) {
                    /* (. sym) -- variadic rest param: return sym bare */
                    skip_whitespace_and_comments(r);
                    int close2 = reader_getc(r);
                    POP_ROOT(); POP_ROOT();
                    if (close2 != ')') {
                        pl_error_str("read error: expected ')' after (. sym)");
                        return NIL_VAL;
                    }
                    return cdr_val;
                }
                /* Set cdr of current tail cell */
                g_cells[CONS_IDX(tail)].cdr = cdr_val;

                skip_whitespace_and_comments(r);
                int close = reader_getc(r);
                if (close != ')') {
                    POP_ROOT(); POP_ROOT();
                    pl_error_str("read error: expected ')' after dotted cdr");
                    return NIL_VAL;
                }
                head = g_gc_roots[g_gc_root_top - 2];
                POP_ROOT(); POP_ROOT();
                return head;
            }
            /* '.' is part of a symbol (e.g. method.field) -- put back */
            reader_ungetc(r, ch);
        } else {
            reader_ungetc(r, ch);
        }

        Val elem = read_val(r);

        /* Reload roots after potential GC */
        head = g_gc_roots[g_gc_root_top - 2];
        tail = g_gc_roots[g_gc_root_top - 1];

        /* Protect elem across the pl_cons call */
        PUSH_ROOT(elem);
        Val new_cell = pl_cons(elem, NIL_VAL);
        POP_ROOT(); /* elem */

        /* Reload all roots after pl_cons (may GC) */
        head = g_gc_roots[g_gc_root_top - 2];
        tail = g_gc_roots[g_gc_root_top - 1];

        if (IS_NIL(head)) {
            head = new_cell;
            tail = new_cell;
        } else {
            g_cells[CONS_IDX(tail)].cdr = new_cell;
            tail = new_cell;
        }

        /* Update the roots */
        g_gc_roots[g_gc_root_top - 2] = head;
        g_gc_roots[g_gc_root_top - 1] = tail;
    }

    head = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); POP_ROOT();
    return head;
}

/* =========================================================================
 * Reader macro wrappers: quote, quasiquote, unquote, unquote-splicing
 * ===================================================================== */

static Val wrap_reader_macro(Reader *r, uint32_t sym_idx)
{
    Val inner = read_val(r);
    PUSH_ROOT(inner);
    Val sym_val_v = MAKE_SYM(sym_idx);
    PUSH_ROOT(sym_val_v);

    /* (sym inner) = (cons sym (cons inner NIL)) */
    Val inner2 = g_gc_roots[g_gc_root_top - 2]; /* inner */
    Val tail = pl_cons(inner2, NIL_VAL);
    PUSH_ROOT(tail);

    Val sym2 = g_gc_roots[g_gc_root_top - 2]; /* sym_val_v */
    Val tail2 = g_gc_roots[g_gc_root_top - 1]; /* tail */
    Val result = pl_cons(sym2, tail2);

    POP_ROOT(); /* tail */
    POP_ROOT(); /* sym_val_v */
    POP_ROOT(); /* inner */
    return result;
}

/* =========================================================================
 * Main dispatcher
 * ===================================================================== */

static Val read_val(Reader *r)
{
    skip_whitespace_and_comments(r);

    int c = reader_getc(r);
    if (c == EOF) return EOF_VAL;

    switch (c) {
        /* List */
        case '(':
            return read_list(r);

        /* Closing paren -- should not appear here */
        case ')':
            pl_error_str("read error: unexpected ')'");
            return NIL_VAL;

        /* String */
        case '"':
            return read_string(r);

        /* Quote */
        case '\'':
            return wrap_reader_macro(r, g_sym_quote);

        /* Quasiquote */
        case '`':
            return wrap_reader_macro(r, g_sym_quasiquote);

        /* Unquote / unquote-splicing */
        case ',': {
            int nxt = reader_getc(r);
            if (nxt == '@') {
                return wrap_reader_macro(r, g_sym_unquote_splicing);
            }
            reader_ungetc(r, nxt);
            return wrap_reader_macro(r, g_sym_unquote);
        }

        /* Transient symbol |name| */
        case '|':
            return read_transient_sym(r);

        /* Hash-prefixed forms */
        case '#': {
            int nxt = reader_getc(r);
            if (nxt == '\\') {
                return read_char_literal(r);
            }
            /* Not a char literal -- treat '#' + nxt as start of atom
             * (e.g. #t #f in some dialects, or just a symbol) */
            reader_ungetc(r, nxt);
            /* Read it as an atom starting with '#' */
            return read_atom(r, '#');
        }

        /* Semicolons treated as line comments (Scheme style, secondary) */
        case ';': {
            for (;;) {
                int cc = reader_getc(r);
                if (cc == '\n' || cc == EOF) break;
            }
            return read_val(r);
        }

        /* Everything else: atom (number or symbol) */
        default:
            return read_atom(r, c);
    }
}

/* =========================================================================
 * Public API
 * ===================================================================== */

Val read_one(Reader *r)
{
    return read_val(r);
}

Val read_from_string(const char *s)
{
    Reader r;
    reader_init_string(&r, s);
    return read_one(&r);
}
