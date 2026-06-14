#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* prims.c -- Built-in primitive functions for the L (lispIsPerfect9) interpreter.
 *
 * All primitives have the signature:  Val fn(Val args, Val env)
 * where `args` is an already-evaluated argument list (a Lisp list).
 *
 * Sections
 * --------
 *  A.  Registration table & helpers
 *  B.  List / Cell primitives
 *  C.  Predicate primitives
 *  D.  Arithmetic primitives
 *  E.  Bignum helpers
 *  F.  String / Symbol primitives
 *  G.  Character primitives
 *  H.  I/O primitives
 *  I.  JSON encode / decode
 *  J.  Control primitives
 *  K.  Environment primitives
 *  L.  Mutable vector primitives
 *  M.  make / link / chain primitives
 *  N.  Parameter object primitives
 *  O.  Hash primitive
 *  P.  prims_init -- registration entry point
 */

#include "picolisp.h"
#include "bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

/* =========================================================================
 * A.  Registration table & helpers
 * ===================================================================== */

/* g_prims[] and g_prim_count are declared in sym.c (where they are defined)
 * and declared extern in picolisp.h.  We only need the registration function
 * and the init routine here.                                               */

void prim_register(const char *name, PrimFn fn, int min_args, int max_args)
{
    uint32_t sidx = sym_intern(name, strlen(name));
    int idx = g_prim_count++;
    g_prims[idx].name     = name;
    g_prims[idx].fn       = fn;
    g_prims[idx].min_args = min_args;
    g_prims[idx].max_args = max_args;
    g_syms[sidx].value    = MAKE_PRIM(idx);
}

/* --- Argument-checking helpers ----------------------------------------- */

static Val expect_1(const char *name, Val args)
{
    if (!IS_CONS(args))
        pl_error_str(name);   /* message already has context */
    return CAR(args);
}

static void expect_n(const char *name, Val args, int n)
{
    int got = val_list_len(args);
    if (got != n) pl_error_str(name);
}

static Val arg1(Val args) { return CAR(args); }
static Val arg2(Val args) { return CADR(args); }
static Val arg3(Val args) { return CADDR(args); }

/* Require a numeric (INT, BIG, or FLOAT) value, error otherwise. */
static void require_number(const char *ctx, Val v)
{
    if (!IS_INT(v) && !IS_BIG(v) && !IS_FLOAT(v))
        pl_error(ctx, v);
}

/* Require a string value, error otherwise. */
static void require_string(const char *ctx, Val v)
{
    if (!IS_STR(v))
        pl_error(ctx, v);
}

/* =========================================================================
 * B.  List / Cell primitives
 * ===================================================================== */

static Val prim_cons(Val args, Val env)
{
    (void)env;
    expect_n("cons: expected 2 arguments", args, 2);
    return pl_cons(arg1(args), arg2(args));
}

static Val prim_car(Val args, Val env)
{
    (void)env;
    Val x = expect_1("car: expected 1 argument", args);
    if (IS_CONS(x)) return CAR(x);
    return NIL_VAL;
}

static Val prim_cdr(Val args, Val env)
{
    (void)env;
    Val x = expect_1("cdr: expected 1 argument", args);
    if (IS_CONS(x)) return CDR(x);
    return NIL_VAL;
}

static Val prim_cadr(Val args, Val env)
{
    (void)env;
    Val x = expect_1("cadr: expected 1 argument", args);
    if (!IS_CONS(x)) return NIL_VAL;
    Val d = CDR(x);
    if (!IS_CONS(d)) return NIL_VAL;
    return CAR(d);
}

static Val prim_cddr(Val args, Val env)
{
    (void)env;
    Val x = expect_1("cddr: expected 1 argument", args);
    if (!IS_CONS(x)) return NIL_VAL;
    Val d = CDR(x);
    if (!IS_CONS(d)) return NIL_VAL;
    return CDR(d);
}

static Val prim_caddr(Val args, Val env)
{
    (void)env;
    Val x = expect_1("caddr: expected 1 argument", args);
    if (!IS_CONS(x)) return NIL_VAL;
    Val d = CDR(x);
    if (!IS_CONS(d)) return NIL_VAL;
    Val dd = CDR(d);
    if (!IS_CONS(dd)) return NIL_VAL;
    return CAR(dd);
}

static Val prim_cdddr(Val args, Val env)
{
    (void)env;
    Val x = expect_1("cdddr: expected 1 argument", args);
    if (!IS_CONS(x)) return NIL_VAL;
    Val d = CDR(x);
    if (!IS_CONS(d)) return NIL_VAL;
    Val dd = CDR(d);
    if (!IS_CONS(dd)) return NIL_VAL;
    return CDR(dd);
}

static Val prim_set_car(Val args, Val env)
{
    (void)env;
    expect_n("set-car: expected 2 arguments", args, 2);
    Val x = arg1(args);
    Val v = arg2(args);
    if (!IS_CONS(x)) pl_error("set-car: not a cons", x);
    g_cells[CONS_IDX(x)].car = v;
    return v;
}

static Val prim_set_cdr(Val args, Val env)
{
    (void)env;
    expect_n("set-cdr: expected 2 arguments", args, 2);
    Val x = arg1(args);
    Val v = arg2(args);
    if (!IS_CONS(x)) pl_error("set-cdr: not a cons", x);
    g_cells[CONS_IDX(x)].cdr = v;
    return v;
}

static Val prim_list(Val args, Val env)
{
    (void)env;
    /* args is already the list we want -- return a fresh copy? No: the
     * evaluator has already built the evaluated arg list, so just return it. */
    return args;
}

static Val prim_list_star(Val args, Val env)
{
    (void)env;
    /* (list* a) -> a
     * (list* a b) -> (cons a b)
     * (list* a b c) -> (cons a (cons b c))
     * etc.                                                                 */
    if (IS_NIL(args)) return NIL_VAL;
    if (!IS_CONS(CDR(args))) return CAR(args);   /* single arg: return it   */

    /* Build from the back: find last element (tail), then cons backwards. */
    /* Collect into an array for easy back-traversal.                       */
    Val elems[1024];
    int n = 0;
    Val cur = args;
    while (IS_CONS(cur)) {
        if (n < 1024) elems[n++] = CAR(cur);
        cur = CDR(cur);
    }
    /* Last element becomes the tail. */
    Val result = elems[n - 1];
    for (int i = n - 2; i >= 0; i--)
        result = pl_cons(elems[i], result);
    return result;
}

static Val prim_append(Val args, Val env)
{
    (void)env;
    if (IS_NIL(args)) return NIL_VAL;
    if (!IS_CONS(CDR(args))) return CAR(args);   /* single list: return it  */

    Val lst1 = arg1(args);
    Val lst2 = arg2(args);

    if (IS_NIL(lst1)) return lst2;

    /* Copy lst1 and attach lst2 at the end. */
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(lst2); PUSH_ROOT(lst1);
    PUSH_ROOT(head); PUSH_ROOT(tail);
    Val cur = lst1;
    while (IS_CONS(cur)) {
        Val cell = pl_cons(CAR(cur), NIL_VAL);
        if (IS_NIL(g_gc_roots[g_gc_root_top - 2] /* head */)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            head = cell; tail = cell;
        } else {
            tail = g_gc_roots[g_gc_root_top - 1];
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            tail = cell;
        }
        cur = CDR(cur);
    }
    head = g_gc_roots[g_gc_root_top - 2];
    tail = g_gc_roots[g_gc_root_top - 1];
    lst2 = g_gc_roots[g_gc_root_top - 4]; /* lst2 is bottom of our 4 roots */
    POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT(); /* tail, head, lst1, lst2 */
    if (!IS_NIL(tail))
        g_cells[CONS_IDX(tail)].cdr = lst2;
    else
        head = lst2;
    return head;
}

static Val prim_nconc(Val args, Val env)
{
    (void)env;
    if (IS_NIL(args)) return NIL_VAL;
    Val lst1 = arg1(args);
    Val lst2 = arg2(args);
    if (IS_NIL(lst1)) return lst2;
    /* Find last cons of lst1. */
    Val cur = lst1;
    while (IS_CONS(CDR(cur))) cur = CDR(cur);
    g_cells[CONS_IDX(cur)].cdr = lst2;
    return lst1;
}

static Val prim_reverse(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("reverse: expected 1 argument", args);
    Val result = NIL_VAL;
    PUSH_ROOT(lst); PUSH_ROOT(result);
    while (IS_CONS(g_gc_roots[g_gc_root_top - 2] /* lst */)) {
        lst = g_gc_roots[g_gc_root_top - 2];
        result = pl_cons(CAR(lst), g_gc_roots[g_gc_root_top - 1]);
        g_gc_roots[g_gc_root_top - 1] = result;
        g_gc_roots[g_gc_root_top - 2] = CDR(lst);
    }
    result = g_gc_roots[g_gc_root_top - 1];
    POP_ROOT(); POP_ROOT();
    return result;
}

static Val prim_length(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("length: expected 1 argument", args);
    return MAKE_INT(val_list_len(lst));
}

static Val prim_last(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("last: expected 1 argument", args);
    if (!IS_CONS(lst)) return NIL_VAL;
    while (IS_CONS(CDR(lst))) lst = CDR(lst);
    return lst;
}

/* =========================================================================
 * wbt-from-list -- O(n) balanced WBT construction in C.
 * Replaces the L-level recursive wbt--build-n which is ~10x slower due
 * to per-call interpreter overhead (env alists, bind stack, etc.).
 * Tree node format: (wbt count left val right) -- 5-cell chain.
 * ===================================================================== */
static uint32_t g_sym_wbt_cached = 0;
static uint32_t sym_wbt(void) {
    if (g_sym_wbt_cached == 0) g_sym_wbt_cached = sym_intern("wbt", 3);
    return g_sym_wbt_cached;
}

static Val build_wbt_balanced(Val *items, int start, int end)
{
    if (start >= end) return NIL_VAL;
    int n   = end - start;
    int mid = start + (n - 1) / 2;
    Val val = items[mid];

    Val left = build_wbt_balanced(items, start, mid);
    PUSH_ROOT(left);
    Val right = build_wbt_balanced(items, mid + 1, end);
    /* left and right are both on the root stack while we build the node */
    PUSH_ROOT(right);

    /* Build (wbt count left val right) bottom-up so each intermediate is rooted. */
    Val v5 = pl_cons(g_gc_roots[g_gc_root_top - 1] /* right */, NIL_VAL);
    PUSH_ROOT(v5);
    Val v4 = pl_cons(val, g_gc_roots[g_gc_root_top - 1] /* v5 */);
    g_gc_roots[g_gc_root_top - 1] = v4;
    Val v3 = pl_cons(g_gc_roots[g_gc_root_top - 3] /* left */,
                     g_gc_roots[g_gc_root_top - 1] /* v4 */);
    g_gc_roots[g_gc_root_top - 1] = v3;
    Val v2 = pl_cons(MAKE_INT(n), g_gc_roots[g_gc_root_top - 1] /* v3 */);
    g_gc_roots[g_gc_root_top - 1] = v2;
    Val v1 = pl_cons(MAKE_SYM(sym_wbt()), g_gc_roots[g_gc_root_top - 1] /* v2 */);

    POP_ROOT(); /* v5/v4/v3/v2 slot */
    POP_ROOT(); /* right */
    POP_ROOT(); /* left */
    return v1;
}

static Val prim_wbt_from_list(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("wbt-from-list: expected 1 argument", args);
    if (!IS_CONS(lst)) return NIL_VAL;

    /* Count items */
    int n = 0;
    for (Val t = lst; IS_CONS(t); t = CDR(t)) n++;
    if (n == 0) return NIL_VAL;

    /* Protect lst across malloc (which can fail/longjmp through pl_error). */
    PUSH_ROOT(lst);
    Val *items = (Val *)malloc((size_t)n * sizeof(Val));
    if (!items) { POP_ROOT(); pl_error_str("wbt-from-list: out of memory"); }

    int i = 0;
    for (Val t = g_gc_roots[g_gc_root_top - 1]; IS_CONS(t); t = CDR(t))
        items[i++] = CAR(t);
    POP_ROOT();

    Val result = build_wbt_balanced(items, 0, n);
    free(items);
    return result;
}

static Val prim_nth(Val args, Val env)
{
    (void)env;
    /* (nth n lst) -> cdr n times, i.e., the nth tail */
    expect_n("nth: expected 2 arguments", args, 2);
    Val nval = arg1(args);
    Val lst  = arg2(args);
    if (!IS_INT(nval)) pl_error("nth: index must be integer", nval);
    int32_t n = INT_VAL(nval);
    while (n-- > 0 && IS_CONS(lst)) lst = CDR(lst);
    return lst;
}

/* Forward declaration so prim_member can call val_equal (defined later). */
static bool val_equal(Val a, Val b);

static Val prim_member(Val args, Val env)
{
    (void)env;
    expect_n("member: expected 2 arguments", args, 2);
    Val x   = arg1(args);
    Val lst = arg2(args);
    while (IS_CONS(lst)) {
        if (val_equal(x, CAR(lst))) return lst;
        lst = CDR(lst);
    }
    return NIL_VAL;
}

static Val prim_memq(Val args, Val env)
{
    (void)env;
    expect_n("memq: expected 2 arguments", args, 2);
    Val x   = arg1(args);
    Val lst = arg2(args);
    while (IS_CONS(lst)) {
        Val elem = CAR(lst);
        /* eq: same bit pattern (identity / value equality) */
        if (x == elem) return lst;
        /* For INT: same value counts as eq */
        if (IS_INT(x) && IS_INT(elem) && INT_VAL(x) == INT_VAL(elem))
            return lst;
        lst = CDR(lst);
    }
    return NIL_VAL;
}

static Val prim_assoc(Val args, Val env)
{
    (void)env;
    expect_n("assoc: expected 2 arguments", args, 2);
    Val key   = arg1(args);
    Val alist = arg2(args);
    while (IS_CONS(alist)) {
        Val pair = CAR(alist);
        if (IS_CONS(pair)) {
            Val k = CAR(pair);
            /* structural equal */
            bool eq = false;
            if (k == key) eq = true;
            else if (IS_INT(k) && IS_INT(key) && INT_VAL(k) == INT_VAL(key)) eq = true;
            else if (IS_STR(k) && IS_STR(key)) {
                const char *sk = str_ptr(STR_IDX(k));
                const char *sv = str_ptr(STR_IDX(key));
                if (sk && sv && strcmp(sk, sv) == 0) eq = true;
            } else if (IS_BIG(k) && IS_BIG(key)) {
                eq = (big_cmp(k, key) == 0);
            } else {
                char *sk2 = val_to_str(k);
                char *sv2 = val_to_str(key);
                eq = (sk2 && sv2 && strcmp(sk2, sv2) == 0);
                free(sk2); free(sv2);
            }
            if (eq) return pair;
        }
        alist = CDR(alist);
    }
    return NIL_VAL;
}

static Val prim_assq(Val args, Val env)
{
    (void)env;
    expect_n("assq: expected 2 arguments", args, 2);
    Val key   = arg1(args);
    Val alist = arg2(args);
    while (IS_CONS(alist)) {
        Val pair = CAR(alist);
        if (IS_CONS(pair)) {
            Val k = CAR(pair);
            bool eq = (k == key);
            if (!eq && IS_INT(k) && IS_INT(key)) eq = (INT_VAL(k) == INT_VAL(key));
            if (eq) return pair;
        }
        alist = CDR(alist);
    }
    return NIL_VAL;
}

static Val prim_filter(Val args, Val env)
{
    expect_n("filter: expected 2 arguments", args, 2);
    Val fn  = arg1(args);
    Val lst = arg2(args);
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(fn); PUSH_ROOT(lst);
    PUSH_ROOT(head); PUSH_ROOT(tail);
    while (IS_CONS(g_gc_roots[g_gc_root_top - 3] /* lst */)) {
        lst = g_gc_roots[g_gc_root_top - 3];
        fn  = g_gc_roots[g_gc_root_top - 4];
        Val elem = CAR(lst);
        PUSH_ROOT(elem);
        Val res  = pl_apply(fn, LIST1(elem), env);
        elem = g_gc_roots[g_gc_root_top - 1]; /* reload after pl_apply */
        POP_ROOT(); /* elem */
        if (IS_TRUE(res)) {
            Val cell = pl_cons(elem, NIL_VAL);
            if (IS_NIL(g_gc_roots[g_gc_root_top - 2] /* head */)) {
                g_gc_roots[g_gc_root_top - 2] = cell;
                g_gc_roots[g_gc_root_top - 1] = cell;
            } else {
                tail = g_gc_roots[g_gc_root_top - 1];
                g_cells[CONS_IDX(tail)].cdr = cell;
                g_gc_roots[g_gc_root_top - 1] = cell;
            }
        }
        g_gc_roots[g_gc_root_top - 3] = CDR(lst); /* advance lst */
    }
    head = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT();
    return head;
}

static Val prim_remove(Val args, Val env)
{
    expect_n("remove: expected 2 arguments", args, 2);
    Val fn  = arg1(args);
    Val lst = arg2(args);
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(fn); PUSH_ROOT(lst);
    PUSH_ROOT(head); PUSH_ROOT(tail);
    while (IS_CONS(g_gc_roots[g_gc_root_top - 3] /* lst */)) {
        lst = g_gc_roots[g_gc_root_top - 3];
        fn  = g_gc_roots[g_gc_root_top - 4];
        Val elem = CAR(lst);
        PUSH_ROOT(elem);
        Val res  = pl_apply(fn, LIST1(elem), env);
        elem = g_gc_roots[g_gc_root_top - 1];
        POP_ROOT();
        if (IS_FALSE(res)) {
            Val cell = pl_cons(elem, NIL_VAL);
            if (IS_NIL(g_gc_roots[g_gc_root_top - 2])) {
                g_gc_roots[g_gc_root_top - 2] = cell;
                g_gc_roots[g_gc_root_top - 1] = cell;
            } else {
                tail = g_gc_roots[g_gc_root_top - 1];
                g_cells[CONS_IDX(tail)].cdr = cell;
                g_gc_roots[g_gc_root_top - 1] = cell;
            }
        }
        g_gc_roots[g_gc_root_top - 3] = CDR(lst);
    }
    head = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT();
    return head;
}

static Val prim_mapcar(Val args, Val env)
{
    expect_n("mapcar: expected 2 arguments", args, 2);
    Val fn  = arg1(args);
    Val lst = arg2(args);
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(fn); PUSH_ROOT(lst);
    PUSH_ROOT(head); PUSH_ROOT(tail);
    while (IS_CONS(g_gc_roots[g_gc_root_top - 3] /* lst */)) {
        lst = g_gc_roots[g_gc_root_top - 3];
        fn  = g_gc_roots[g_gc_root_top - 4];
        Val mapped = pl_apply(fn, LIST1(CAR(lst)), env);
        Val cell   = pl_cons(mapped, NIL_VAL);
        if (IS_NIL(g_gc_roots[g_gc_root_top - 2] /* head */)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            head = cell; tail = cell;
        } else {
            tail = g_gc_roots[g_gc_root_top - 1];
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            tail = cell;
        }
        g_gc_roots[g_gc_root_top - 3] = CDR(lst); /* advance lst */
    }
    head = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT();
    return head;
}

static Val prim_mapc(Val args, Val env)
{
    expect_n("mapc: expected 2 arguments", args, 2);
    Val fn  = arg1(args);
    Val lst = arg2(args);
    PUSH_ROOT(fn); PUSH_ROOT(lst);
    while (IS_CONS(g_gc_roots[g_gc_root_top - 1])) {
        lst = g_gc_roots[g_gc_root_top - 1];
        fn  = g_gc_roots[g_gc_root_top - 2];
        pl_apply(fn, LIST1(CAR(lst)), env);
        g_gc_roots[g_gc_root_top - 1] = CDR(lst);
    }
    POP_ROOT(); POP_ROOT();
    return NIL_VAL;
}

static Val prim_apply(Val args, Val env)
{
    expect_n("apply: expected 2 arguments", args, 2);
    Val fn   = arg1(args);
    Val lst  = arg2(args);
    return pl_apply(fn, lst, env);
}

static Val prim_funcall(Val args, Val env)
{
    if (!IS_CONS(args)) pl_error_str("funcall: expected at least 1 argument");
    Val fn   = CAR(args);
    Val fargs = CDR(args);
    return pl_apply(fn, fargs, env);
}

/* =========================================================================
 * C.  Predicate primitives
 * ===================================================================== */

static Val prim_null(Val args, Val env)
{
    (void)env;
    Val x = expect_1("null: expected 1 argument", args);
    return IS_NIL(x) ? T_VAL : NIL_VAL;
}

static Val prim_atom(Val args, Val env)
{
    (void)env;
    Val x = expect_1("atom: expected 1 argument", args);
    return IS_ATOM(x) ? T_VAL : NIL_VAL;
}

static Val prim_pair(Val args, Val env)
{
    (void)env;
    Val x = expect_1("pair: expected 1 argument", args);
    return IS_CONS(x) ? T_VAL : NIL_VAL;
}

static Val prim_listp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("listp: expected 1 argument", args);
    return (IS_NIL(x) || IS_CONS(x)) ? T_VAL : NIL_VAL;
}

static Val prim_numberp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("numberp: expected 1 argument", args);
    return (IS_INT(x) || IS_BIG(x) || IS_FLOAT(x)) ? T_VAL : NIL_VAL;
}

static Val prim_stringp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("stringp: expected 1 argument", args);
    return IS_STR(x) ? T_VAL : NIL_VAL;
}

static Val prim_symbolp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("symbolp: expected 1 argument", args);
    return IS_SYM(x) ? T_VAL : NIL_VAL;
}

static Val prim_procedurep(Val args, Val env)
{
    (void)env;
    Val x = expect_1("procedure?: expected 1 argument", args);
    if (IS_PRIM(x) || IS_CB(x)) return T_VAL;
    /* lambda: a cons (lambda ...) */
    if (IS_CONS(x)) {
        Val head = CAR(x);
        if (IS_SYM(head) && SYM_IDX(head) == g_sym_lambda) return T_VAL;
    }
    return NIL_VAL;
}

static Val prim_eq(Val args, Val env)
{
    (void)env;
    expect_n("eq: expected 2 arguments", args, 2);
    Val a = arg1(args);
    Val b = arg2(args);
    bool eq = (a == b);
    if (!eq && IS_INT(a) && IS_INT(b)) eq = (INT_VAL(a) == INT_VAL(b));
    return eq ? T_VAL : NIL_VAL;
}

/* Structural equality helper -- uses val_to_str for deep comparison. */
static bool val_equal(Val a, Val b)
{
    if (a == b) return true;
    if (IS_NIL(a) && IS_NIL(b)) return true;
    if (IS_INT(a) && IS_INT(b)) return INT_VAL(a) == INT_VAL(b);
    if (IS_FLOAT(a) || IS_FLOAT(b)) {
        if (IS_INT(a)||IS_BIG(a)||IS_FLOAT(a)) if (IS_INT(b)||IS_BIG(b)||IS_FLOAT(b))
            return float_cmp(a, b) == 0;
    }
    if (IS_INT(a) && IS_BIG(b)) { Val ba = to_big(a); return big_cmp(ba, b) == 0; }
    if (IS_BIG(a) && IS_INT(b)) { Val bb = to_big(b); return big_cmp(a, bb) == 0; }
    if (IS_BIG(a) && IS_BIG(b)) return big_cmp(a, b) == 0;
    if (IS_STR(a) && IS_STR(b)) {
        if (STR_IDX(a) == STR_IDX(b)) return true;
        const char *sa = str_ptr(STR_IDX(a));
        const char *sb = str_ptr(STR_IDX(b));
        if (sa && sb) return strcmp(sa, sb) == 0;
        return false;
    }
    if (IS_SYM(a) && IS_SYM(b)) return SYM_IDX(a) == SYM_IDX(b);
    if (IS_CONS(a) && IS_CONS(b)) {
        /* Iterative comparison to avoid deep recursion on long lists. */
        while (IS_CONS(a) && IS_CONS(b)) {
            if (!val_equal(CAR(a), CAR(b))) return false;
            a = CDR(a);
            b = CDR(b);
        }
        return val_equal(a, b);
    }
    return false;
}

static Val prim_equal(Val args, Val env)
{
    (void)env;
    expect_n("equal: expected 2 arguments", args, 2);
    return val_equal(arg1(args), arg2(args)) ? T_VAL : NIL_VAL;
}

static Val prim_numeq(Val args, Val env)
{
    (void)env;
    expect_n("=: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("=", a); require_number("=", b);
    return (pl_cmp(a, b) == 0) ? T_VAL : NIL_VAL;
}

/* Comparison helper: works on numbers AND strings */
static int general_cmp(const char *ctx, Val a, Val b) {
    if (IS_STR(a) && IS_STR(b)) {
        return strcmp(str_ptr(STR_IDX(a)), str_ptr(STR_IDX(b)));
    }
    require_number(ctx, a); require_number(ctx, b);
    return pl_cmp(a, b);
}

static Val prim_numlt(Val args, Val env)
{
    (void)env;
    expect_n("<: expected 2 arguments", args, 2);
    return (general_cmp("<", arg1(args), arg2(args)) < 0) ? T_VAL : NIL_VAL;
}

static Val prim_numgt(Val args, Val env)
{
    (void)env;
    expect_n(">: expected 2 arguments", args, 2);
    return (general_cmp(">", arg1(args), arg2(args)) > 0) ? T_VAL : NIL_VAL;
}

static Val prim_numle(Val args, Val env)
{
    (void)env;
    expect_n("<=: expected 2 arguments", args, 2);
    return (general_cmp("<=", arg1(args), arg2(args)) <= 0) ? T_VAL : NIL_VAL;
}

static Val prim_numge(Val args, Val env)
{
    (void)env;
    expect_n(">=: expected 2 arguments", args, 2);
    return (general_cmp(">=", arg1(args), arg2(args)) >= 0) ? T_VAL : NIL_VAL;
}

static Val prim_not(Val args, Val env)
{
    (void)env;
    Val x = expect_1("not: expected 1 argument", args);
    return IS_NIL(x) ? T_VAL : NIL_VAL;
}

/* =========================================================================
 * D.  Arithmetic primitives
 * ===================================================================== */

static Val prim_add(Val args, Val env)
{
    (void)env;
    Val acc = MAKE_INT(0);
    while (IS_CONS(args)) {
        require_number("+", CAR(args));
        acc  = pl_add(acc, CAR(args));
        args = CDR(args);
    }
    return acc;
}

static Val prim_sub(Val args, Val env)
{
    (void)env;
    if (!IS_CONS(args)) pl_error_str("-: expected at least 1 argument");
    Val first = CAR(args);
    require_number("-", first);
    args = CDR(args);
    if (IS_NIL(args)) return pl_neg(first);
    Val acc = first;
    while (IS_CONS(args)) {
        require_number("-", CAR(args));
        acc  = pl_sub(acc, CAR(args));
        args = CDR(args);
    }
    return acc;
}

static Val prim_mul(Val args, Val env)
{
    (void)env;
    Val acc = MAKE_INT(1);
    while (IS_CONS(args)) {
        require_number("*", CAR(args));
        acc  = pl_mul(acc, CAR(args));
        args = CDR(args);
    }
    return acc;
}

static Val prim_div(Val args, Val env)
{
    (void)env;
    expect_n("/: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("/", a); require_number("/", b);
    return pl_div(a, b);
}

static Val prim_mod(Val args, Val env)
{
    (void)env;
    expect_n("mod: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("mod", a); require_number("mod", b);
    return pl_mod(a, b);
}

static Val prim_rem(Val args, Val env)
{
    (void)env;
    expect_n("rem: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("rem", a); require_number("rem", b);
    return pl_rem(a, b);
}

static Val prim_pow(Val args, Val env)
{
    (void)env;
    expect_n("**: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("**", a); require_number("**", b);
    if (IS_INT(a) && IS_INT(b)) {
        int32_t base = INT_VAL(a), exp = INT_VAL(b);
        if (exp < 0) return MAKE_INT(0);
        if (exp == 0) return MAKE_INT(1);
        /* Use bignum path if numbers are large */
        return big_normalize(big_pow(to_big(a), to_big(b)));
    }
    return big_normalize(big_pow(
        IS_BIG(a) ? a : to_big(a),
        IS_BIG(b) ? b : to_big(b)));
}

static Val prim_abs(Val args, Val env)
{
    (void)env;
    Val x = expect_1("abs: expected 1 argument", args);
    require_number("abs", x);
    return pl_abs(x);
}

static Val prim_max(Val args, Val env)
{
    (void)env;
    if (!IS_CONS(args)) pl_error_str("max: expected at least 1 argument");
    Val acc = CAR(args); args = CDR(args);
    require_number("max", acc);
    while (IS_CONS(args)) {
        require_number("max", CAR(args));
        acc  = pl_max(acc, CAR(args));
        args = CDR(args);
    }
    return acc;
}

static Val prim_min(Val args, Val env)
{
    (void)env;
    if (!IS_CONS(args)) pl_error_str("min: expected at least 1 argument");
    Val acc = CAR(args); args = CDR(args);
    require_number("min", acc);
    while (IS_CONS(args)) {
        require_number("min", CAR(args));
        acc  = pl_min(acc, CAR(args));
        args = CDR(args);
    }
    return acc;
}

static Val prim_inc1(Val args, Val env)
{
    (void)env;
    Val x = expect_1("1+: expected 1 argument", args);
    require_number("1+", x);
    return pl_add(x, MAKE_INT(1));
}

static Val prim_dec1(Val args, Val env)
{
    (void)env;
    Val x = expect_1("1-: expected 1 argument", args);
    require_number("1-", x);
    return pl_sub(x, MAKE_INT(1));
}

static Val prim_inc(Val args, Val env)
{
    (void)env;
    Val x = expect_1("inc: expected 1 argument", args);
    require_number("inc", x);
    return pl_add(x, MAKE_INT(1));
}

static Val prim_dec(Val args, Val env)
{
    (void)env;
    Val x = expect_1("dec: expected 1 argument", args);
    require_number("dec", x);
    return pl_sub(x, MAKE_INT(1));
}

static Val prim_gcd(Val args, Val env)
{
    (void)env;
    expect_n("gcd: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("gcd", a); require_number("gcd", b);
    Val ba = IS_BIG(a) ? a : to_big(a);
    Val bb = IS_BIG(b) ? b : to_big(b);
    return big_normalize(big_gcd(ba, bb));
}

static Val prim_lcm(Val args, Val env)
{
    (void)env;
    expect_n("lcm: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("lcm", a); require_number("lcm", b);
    Val ba = IS_BIG(a) ? a : to_big(a);
    Val bb = IS_BIG(b) ? b : to_big(b);
    return big_normalize(big_lcm(ba, bb));
}

static Val prim_bitand(Val args, Val env)
{
    (void)env;
    expect_n("bitand: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("bitand", a); require_number("bitand", b);
    if (IS_INT(a) && IS_INT(b))
        return MAKE_INT(INT_VAL(a) & INT_VAL(b));
    return big_normalize(big_bitand(
        IS_BIG(a) ? a : to_big(a),
        IS_BIG(b) ? b : to_big(b)));
}

static Val prim_bitor(Val args, Val env)
{
    (void)env;
    expect_n("bitor: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("bitor", a); require_number("bitor", b);
    if (IS_INT(a) && IS_INT(b))
        return MAKE_INT(INT_VAL(a) | INT_VAL(b));
    return big_normalize(big_bitor(
        IS_BIG(a) ? a : to_big(a),
        IS_BIG(b) ? b : to_big(b)));
}

static Val prim_bitxor(Val args, Val env)
{
    (void)env;
    expect_n("bitxor: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("bitxor", a); require_number("bitxor", b);
    if (IS_INT(a) && IS_INT(b))
        return MAKE_INT(INT_VAL(a) ^ INT_VAL(b));
    return big_normalize(big_bitxor(
        IS_BIG(a) ? a : to_big(a),
        IS_BIG(b) ? b : to_big(b)));
}

static Val prim_bitnot(Val args, Val env)
{
    (void)env;
    Val x = expect_1("bitnot: expected 1 argument", args);
    require_number("bitnot", x);
    if (IS_INT(x)) return MAKE_INT(~INT_VAL(x));
    return big_normalize(big_bitnot(x));
}

static Val prim_shr(Val args, Val env)
{
    (void)env;
    expect_n(">>: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number(">>", a); require_number(">>", b);
    int32_t n = IS_INT(b) ? INT_VAL(b) : 0;
    if (IS_INT(a)) return MAKE_INT(INT_VAL(a) >> n);
    return big_normalize(big_shr(a, n));
}

static Val prim_shl(Val args, Val env)
{
    (void)env;
    expect_n("<<: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("<<", a); require_number("<<", b);
    int32_t n = IS_INT(b) ? INT_VAL(b) : 0;
    if (IS_INT(a)) {
        if (n >= 32) return to_big(a);   /* promote; big_shl handles the rest */
        return MAKE_INT(INT_VAL(a) << n);
    }
    return big_normalize(big_shl(a, n));
}

static Val prim_bitp(Val args, Val env)
{
    (void)env;
    expect_n("bit?: expected 2 arguments", args, 2);
    Val a = arg1(args); Val b = arg2(args);
    require_number("bit?", a); require_number("bit?", b);
    /* (bit? a b) -> T if bit b is set in a */
    int32_t bit = IS_INT(b) ? INT_VAL(b) : 0;
    if (IS_INT(a)) {
        return ((INT_VAL(a) >> bit) & 1) ? T_VAL : NIL_VAL;
    }
    Val shifted = big_normalize(big_shr(a, bit));
    if (IS_INT(shifted)) return (INT_VAL(shifted) & 1) ? T_VAL : NIL_VAL;
    /* Check lowest bit of bignum */
    Val masked = big_normalize(big_bitand(shifted, to_big(MAKE_INT(1))));
    if (IS_INT(masked)) return INT_VAL(masked) ? T_VAL : NIL_VAL;
    return big_is_zero(masked) ? NIL_VAL : T_VAL;
}

static Val prim_bitmask(Val args, Val env)
{
    (void)env;
    Val x = expect_1("bitmask: expected 1 argument", args);
    require_number("bitmask", x);
    /* (bitmask n) -> (1 << n) - 1 */
    int32_t n = IS_INT(x) ? INT_VAL(x) : 0;
    if (n <= 0) return MAKE_INT(0);
    if (n < 31) return MAKE_INT((int32_t)((1u << n) - 1u));
    Val one = to_big(MAKE_INT(1));
    Val shifted = big_normalize(big_shl(one, n));
    return big_normalize(big_sub(shifted, to_big(MAKE_INT(1))));
}

/* =========================================================================
 * E.  Bignum helpers
 * ===================================================================== */

static Val prim_big(Val args, Val env)
{
    (void)env;
    Val x = expect_1("big: expected 1 argument", args);
    require_number("big", x);
    if (IS_BIG(x)) return x;
    return to_big(x);
}

static Val prim_bigp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("big?: expected 1 argument", args);
    return IS_BIG(x) ? T_VAL : NIL_VAL;
}

/* --- Float primitives --- */

static Val prim_floatp(Val args, Val env) {
    (void)env;
    return IS_FLOAT(arg1(args)) ? T_VAL : NIL_VAL;
}

static Val prim_to_float(Val args, Val env) {
    (void)env;
    Val x = arg1(args);
    if (IS_FLOAT(x)) return x;
    if (IS_INT(x) || IS_BIG(x)) return float_from_int(x);
    if (IS_STR(x)) return float_from_str(str_ptr(STR_IDX(x)));
    pl_error_str("float: cannot convert");
    return NIL_VAL;
}

#define MATH1_PRIM(name, op) \
static Val prim_##name(Val args, Val env) { \
    (void)env; Val x = arg1(args); \
    if (!IS_FLOAT(x)) x = float_from_int(x); \
    return float_math1(x, op); \
}

MATH1_PRIM(sqrt, 0)
MATH1_PRIM(sin, 1)
MATH1_PRIM(cos, 2)
MATH1_PRIM(tan, 3)
MATH1_PRIM(m_exp, 4)
MATH1_PRIM(m_log, 5)
MATH1_PRIM(m_floor, 6)
MATH1_PRIM(m_ceil, 7)
MATH1_PRIM(m_round, 8)
MATH1_PRIM(asin, 9)
MATH1_PRIM(acos, 10)
MATH1_PRIM(atan, 11)

static Val prim_m_pow(Val args, Val env) {
    (void)env;
    Val base = arg1(args), exp = arg2(args);
    if (!IS_FLOAT(base)) base = float_from_int(base);
    if (!IS_FLOAT(exp))  exp  = float_from_int(exp);
    return float_pow(base, exp);
}

static Val prim_pi(Val args, Val env) {
    (void)args; (void)env;
    return float_from_str("3.14159265358979323846264338327950288419716939937510");
}

/* =========================================================================
 * F.  String / Symbol primitives
 * ===================================================================== */

static Val prim_name(Val args, Val env)
{
    (void)env;
    Val x = expect_1("name: expected 1 argument", args);
    if (IS_SYM(x)) {
        const char *n = sym_name(SYM_IDX(x));
        if (!n) return NIL_VAL;
        return MAKE_STR(str_intern(n, strlen(n)));
    }
    if (IS_STR(x)) return x;
    pl_error("name: not a symbol or string", x);
    return NIL_VAL;
}

static Val prim_intern(Val args, Val env)
{
    (void)env;
    Val x = expect_1("intern: expected 1 argument", args);
    if (IS_SYM(x)) return x;
    require_string("intern", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return NIL_VAL;
    return MAKE_SYM(sym_intern(s, strlen(s)));
}

static Val prim_str(Val args, Val env)
{
    (void)env;
    Val x = expect_1("str: expected 1 argument", args);
    char *s = val_to_str(x);
    if (!s) return NIL_VAL;
    Val result = MAKE_STR(str_intern(s, strlen(s)));
    free(s);
    return result;
}

static Val prim_sym(Val args, Val env)
{
    /* alias for intern */
    return prim_intern(args, env);
}

static Val prim_pack(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("pack: expected 1 argument", args);
    /* Concatenate a list of strings/chars into one string. */
    /* First pass: compute total length. */
    size_t total = 0;
    Val cur = lst;
    while (IS_CONS(cur)) {
        Val elem = CAR(cur);
        if (IS_STR(elem)) {
            total += g_strs[STR_IDX(elem)].len;
        } else if (IS_INT(elem)) {
            /* Single codepoint as character */
            total += 1;
        } else if (IS_SYM(elem)) {
            const char *n = sym_name(SYM_IDX(elem));
            if (n) total += strlen(n);
        }
        cur = CDR(cur);
    }
    char *buf = (char *)malloc(total + 1);
    if (!buf) pl_error_str("pack: out of memory");
    size_t pos = 0;
    cur = lst;
    while (IS_CONS(cur)) {
        Val elem = CAR(cur);
        if (IS_STR(elem)) {
            const char *s = str_ptr(STR_IDX(elem));
            size_t len = g_strs[STR_IDX(elem)].len;
            memcpy(buf + pos, s, len);
            pos += len;
        } else if (IS_INT(elem)) {
            buf[pos++] = (char)(int8_t)INT_VAL(elem);
        } else if (IS_SYM(elem)) {
            const char *n = sym_name(SYM_IDX(elem));
            if (n) { size_t l = strlen(n); memcpy(buf + pos, n, l); pos += l; }
        }
        cur = CDR(cur);
    }
    buf[pos] = '\0';
    Val result = MAKE_STR(str_intern(buf, pos));
    free(buf);
    return result;
}

static Val prim_unpack(Val args, Val env)
{
    (void)env;
    Val x = expect_1("unpack: expected 1 argument", args);
    require_string("unpack", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return NIL_VAL;
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(head); PUSH_ROOT(tail);
    for (size_t i = 0; s[i]; i++) {
        char ch[2] = { s[i], '\0' };
        Val sv   = MAKE_STR(str_intern(ch, 1));
        Val cell = pl_cons(sv, NIL_VAL);
        if (IS_NIL(head)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            head = cell; tail = cell;
        } else {
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            tail = cell;
        }
    }
    POP_ROOT(); POP_ROOT();
    return head;
}

static Val prim_upcase(Val args, Val env)
{
    (void)env;
    Val x = expect_1("upcase: expected 1 argument", args);
    require_string("upcase", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return x;
    size_t len = strlen(s);
    char *buf  = (char *)malloc(len + 1);
    if (!buf) pl_error_str("upcase: out of memory");
    for (size_t i = 0; i <= len; i++) buf[i] = (char)toupper((unsigned char)s[i]);
    Val result = MAKE_STR(str_intern(buf, len));
    free(buf);
    return result;
}

static Val prim_downcase(Val args, Val env)
{
    (void)env;
    Val x = expect_1("downcase: expected 1 argument", args);
    require_string("downcase", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return x;
    size_t len = strlen(s);
    char *buf  = (char *)malloc(len + 1);
    if (!buf) pl_error_str("downcase: out of memory");
    for (size_t i = 0; i <= len; i++) buf[i] = (char)tolower((unsigned char)s[i]);
    Val result = MAKE_STR(str_intern(buf, len));
    free(buf);
    return result;
}

static Val prim_trim(Val args, Val env)
{
    (void)env;
    Val x = expect_1("trim: expected 1 argument", args);
    require_string("trim", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return x;
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    return MAKE_STR(str_intern(s, len));
}

static Val prim_ltrim(Val args, Val env)
{
    (void)env;
    Val x = expect_1("ltrim: expected 1 argument", args);
    require_string("ltrim", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return x;
    while (*s && isspace((unsigned char)*s)) s++;
    return MAKE_STR(str_intern(s, strlen(s)));
}

static Val prim_rtrim(Val args, Val env)
{
    (void)env;
    Val x = expect_1("rtrim: expected 1 argument", args);
    require_string("rtrim", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return x;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    return MAKE_STR(str_intern(s, len));
}

/* Note: "sub" is overloaded between arithmetic sub (prim_sub, already
 * defined) and string sub.  We register the string version as "sub"
 * only if there is no conflict; in practice the arithmetic "-" uses a
 * different symbol, so we name the string variant "sub" as requested. */
static Val prim_substr(Val args, Val env)
{
    (void)env;
    /* (sub str start len) */
    if (!IS_CONS(args) || !IS_CONS(CDR(args)))
        pl_error_str("sub: expected 2-3 arguments");
    Val strv  = arg1(args);
    Val startv = arg2(args);
    require_string("sub", strv);
    require_number("sub", startv);
    const char *s   = str_ptr(STR_IDX(strv));
    if (!s) return MAKE_STR(str_intern("", 0));
    size_t slen     = strlen(s);
    int32_t start   = INT_VAL(startv);
    if (start < 0) start = 0;
    size_t ustart   = (size_t)start;
    if (ustart >= slen) return MAKE_STR(str_intern("", 0));
    size_t sublen   = slen - ustart;
    if (IS_CONS(CDDR(args))) {
        Val lenv = CADDR(args);
        require_number("sub", lenv);
        int32_t l = INT_VAL(lenv);
        if (l < 0) l = 0;
        if ((size_t)l < sublen) sublen = (size_t)l;
    }
    return MAKE_STR(str_intern(s + ustart, sublen));
}

static Val prim_index(Val args, Val env)
{
    (void)env;
    /* (index char-or-str haystack) -> 1-indexed position or NIL */
    expect_n("index: expected 2 arguments", args, 2);
    Val needle = arg1(args);
    Val hay    = arg2(args);
    require_string("index", hay);
    const char *h = str_ptr(STR_IDX(hay));
    if (!h) return NIL_VAL;
    const char *n = NULL;
    size_t nlen = 0;
    char nbuf[2] = {0, 0};
    if (IS_STR(needle)) {
        n    = str_ptr(STR_IDX(needle));
        nlen = n ? strlen(n) : 0;
    } else if (IS_INT(needle)) {
        nbuf[0] = (char)INT_VAL(needle);
        n    = nbuf;
        nlen = 1;
    } else {
        pl_error("index: first argument must be string or int", needle);
    }
    if (!n || nlen == 0) return NIL_VAL;
    const char *found = strstr(h, n);
    if (!found) return NIL_VAL;
    return MAKE_INT((int32_t)(found - h) + 1);   /* 1-indexed */
}

static Val prim_replace(Val args, Val env)
{
    (void)env;
    /* (replace str from to) */
    expect_n("replace: expected 3 arguments", args, 3);
    Val strv = arg1(args);
    Val from = arg2(args);
    Val to   = arg3(args);
    require_string("replace", strv);
    require_string("replace", from);
    require_string("replace", to);
    const char *s    = str_ptr(STR_IDX(strv));
    const char *f    = str_ptr(STR_IDX(from));
    const char *t    = str_ptr(STR_IDX(to));
    if (!s || !f || !t) return strv;
    size_t flen = strlen(f);
    if (flen == 0) return strv;
    size_t tlen = strlen(t);
    /* Count occurrences to pre-size buffer. */
    size_t count = 0;
    const char *p = s;
    while ((p = strstr(p, f)) != NULL) { count++; p += flen; }
    size_t slen   = strlen(s);
    size_t newlen = slen + count * ((tlen > flen ? tlen - flen : 0))
                         - count * ((flen > tlen ? flen - tlen : 0));
    char *buf = (char *)malloc(newlen + 1);
    if (!buf) pl_error_str("replace: out of memory");
    char *out = buf;
    p = s;
    const char *next;
    while ((next = strstr(p, f)) != NULL) {
        size_t prefix = (size_t)(next - p);
        memcpy(out, p, prefix); out += prefix;
        memcpy(out, t, tlen);   out += tlen;
        p = next + flen;
    }
    size_t tail = strlen(p);
    memcpy(out, p, tail);
    out[tail] = '\0';
    Val result = MAKE_STR(str_intern(buf, (size_t)(out + tail - buf)));
    free(buf);
    return result;
}

static Val prim_format(Val args, Val env)
{
    (void)env;
    /* (format number [width [precision]]) -> formatted string */
    if (!IS_CONS(args)) pl_error_str("format: expected at least 1 argument");
    Val numv = arg1(args);
    require_number("format", numv);
    int32_t width     = 0;
    int32_t precision = 6;
    if (IS_CONS(CDR(args))) {
        Val wv = arg2(args);
        if (IS_INT(wv)) width = INT_VAL(wv);
        if (IS_CONS(CDDR(args))) {
            Val pv = CADDR(args);
            if (IS_INT(pv)) precision = INT_VAL(pv);
        }
    }
    char fmt_buf[64];
    char out_buf[128];
    if (IS_INT(numv)) {
        if (width > 0)
            snprintf(fmt_buf, sizeof(fmt_buf), "%%%dd", (int)width);
        else
            snprintf(fmt_buf, sizeof(fmt_buf), "%%d");
        snprintf(out_buf, sizeof(out_buf), fmt_buf, (int)INT_VAL(numv));
    } else {
        /* bignum: just stringify it */
        char *s = big_to_str(numv);
        if (!s) return NIL_VAL;
        Val result = MAKE_STR(str_intern(s, strlen(s)));
        free(s);
        return result;
    }
    (void)precision;
    return MAKE_STR(str_intern(out_buf, strlen(out_buf)));
}

/* Helper: write a Val as a prin-style (raw) string into a dynamically growing buffer. */
typedef struct { char *buf; size_t len; size_t cap; } DynBuf;

static void dynbuf_init(DynBuf *b)
{
    b->buf = (char *)malloc(64);
    b->len = 0;
    b->cap = b->buf ? 64 : 0;
    if (b->buf) b->buf[0] = '\0';
}

static void dynbuf_push(DynBuf *b, char c)
{
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->buf = (char *)realloc(b->buf, b->cap);
        if (!b->buf) { fprintf(stderr, "fmt: OOM\n"); exit(1); }
    }
    b->buf[b->len++] = c;
    b->buf[b->len]   = '\0';
}

static void dynbuf_push_str(DynBuf *b, const char *s)
{
    if (!s) return;
    while (*s) dynbuf_push(b, *s++);
}

static Val prim_fmt(Val args, Val env)
{
    /* (fmt template . args) -> string
     * ~A  prin format (raw)
     * ~S  print format (readable)
     * ~%  newline
     * ~~  literal ~                                                         */
    (void)env;
    if (!IS_CONS(args)) pl_error_str("fmt: expected template string");
    Val tmplv = CAR(args);
    require_string("fmt", tmplv);
    const char *tmpl  = str_ptr(STR_IDX(tmplv));
    Val fmt_args      = CDR(args);

    DynBuf buf;
    dynbuf_init(&buf);

    while (tmpl && *tmpl) {
        if (*tmpl == '~') {
            tmpl++;
            switch (*tmpl) {
            case 'A': case 'a': {
                /* ~A: prin (raw) */
                if (!IS_CONS(fmt_args)) pl_error_str("fmt: not enough arguments");
                char *s = val_to_str(CAR(fmt_args));
                /* val_to_str uses readable format; for ~A we want raw */
                /* Use pl_prin logic: for strings, no quotes */
                Val v = CAR(fmt_args);
                if (IS_STR(v)) {
                    dynbuf_push_str(&buf, str_ptr(STR_IDX(v)));
                } else if (IS_NIL(v)) {
                    dynbuf_push_str(&buf, "NIL");
                } else {
                    if (s) dynbuf_push_str(&buf, s);
                }
                if (s) free(s);
                fmt_args = CDR(fmt_args);
                break;
            }
            case 'S': case 's': {
                /* ~S: print (readable) */
                if (!IS_CONS(fmt_args)) pl_error_str("fmt: not enough arguments");
                char *s = val_to_str(CAR(fmt_args));
                if (s) dynbuf_push_str(&buf, s);
                free(s);
                fmt_args = CDR(fmt_args);
                break;
            }
            case '%':
                dynbuf_push(&buf, '\n');
                break;
            case '~':
                dynbuf_push(&buf, '~');
                break;
            case '\0':
                tmpl--;   /* will be incremented at end of loop */
                break;
            default:
                dynbuf_push(&buf, '~');
                dynbuf_push(&buf, *tmpl);
                break;
            }
        } else {
            dynbuf_push(&buf, *tmpl);
        }
        tmpl++;
    }

    Val result = MAKE_STR(str_intern(buf.buf ? buf.buf : "", buf.len));
    free(buf.buf);
    return result;
}

/* =========================================================================
 * G.  Character primitives
 * ===================================================================== */

static Val prim_char(Val args, Val env)
{
    (void)env;
    Val x = expect_1("char: expected 1 argument", args);
    if (IS_INT(x)) {
        /* (char n) -> 1-char string from codepoint */
        char buf[2] = { (char)(int8_t)INT_VAL(x), '\0' };
        return MAKE_STR(str_intern(buf, 1));
    }
    if (IS_STR(x)) {
        /* (char "x") -> codepoint as int */
        const char *s = str_ptr(STR_IDX(x));
        if (!s || !s[0]) return MAKE_INT(0);
        return MAKE_INT((int32_t)(unsigned char)s[0]);
    }
    pl_error("char: expected int or string", x);
    return NIL_VAL;
}

/* space, tab, newline are registered as global symbol values in prims_init */

/* =========================================================================
 * H.  I/O primitives
 * ===================================================================== */

static Val prim_prin(Val args, Val env)
{
    (void)env;
    Port *p = g_current_output ? g_current_output : &g_stdout_port;
    Val last = NIL_VAL;
    while (IS_CONS(args)) {
        last = CAR(args);
        pl_prin(last, p);
        args = CDR(args);
    }
    return last;
}

static Val prim_print(Val args, Val env)
{
    (void)env;
    Port *p = g_current_output ? g_current_output : &g_stdout_port;
    Val last = NIL_VAL;
    while (IS_CONS(args)) {
        last = CAR(args);
        pl_print(last, p);
        args = CDR(args);
    }
    return last;
}

static Val prim_prinl(Val args, Val env)
{
    (void)env;
    Port *p = g_current_output ? g_current_output : &g_stdout_port;
    Val last = NIL_VAL;
    while (IS_CONS(args)) {
        last = CAR(args);
        pl_prinl(last, p);
        args = CDR(args);
    }
    return last;
}

static Val prim_println(Val args, Val env)
{
    (void)env;
    Port *p = g_current_output ? g_current_output : &g_stdout_port;
    if (IS_NIL(args)) {
        /* (println) -- just a newline */
        p->putc_fn('\n', p->ctx);
        return NIL_VAL;
    }
    Val last = NIL_VAL;
    while (IS_CONS(args)) {
        last = CAR(args);
        pl_prin(last, p);
        if (IS_CONS(CDR(args))) p->putc_fn(' ', p->ctx);
        args = CDR(args);
    }
    p->putc_fn('\n', p->ctx);
    return last;
}

static Val prim_write(Val args, Val env)
{
    (void)env;
    Val x = expect_1("write: expected 1 argument", args);
    pl_println(x, g_current_output ? g_current_output : &g_stdout_port);
    return x;
}

static Val prim_read(Val args, Val env)
{
    (void)env;
    (void)args;
    if (!g_current_reader) return NIL_VAL;
    Val v = read_one(g_current_reader);
    if (v == EOF_VAL) return NIL_VAL;
    return v;
}

static Val prim_read_from_string(Val args, Val env)
{
    (void)env;
    Val x = expect_1("read-from-string: expected 1 argument", args);
    require_string("read-from-string", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return NIL_VAL;
    return read_from_string(s);
}

/* File handle table -- simple flat array mapping int -> FILE* */
#define MAX_HANDLES 64
static FILE *s_handles[MAX_HANDLES];
static int   s_handle_init = 0;

static void ensure_handles(void)
{
    if (!s_handle_init) {
        memset(s_handles, 0, sizeof(s_handles));
        s_handles[0] = stdin;
        s_handles[1] = stdout;
        s_handles[2] = stderr;
        s_handle_init = 1;
    }
}

static Val prim_open(Val args, Val env)
{
    (void)env;
    ensure_handles();
    expect_n("open: expected 2 arguments", args, 2);
    Val pathv = arg1(args);
    Val modev = arg2(args);
    require_string("open", pathv);
    require_string("open", modev);
    const char *path = str_ptr(STR_IDX(pathv));
    const char *mode = str_ptr(STR_IDX(modev));
    FILE *f = fopen(path, mode);
    if (!f) return NIL_VAL;
    /* Find free slot (start at 3 to skip stdin/out/err) */
    for (int i = 3; i < MAX_HANDLES; i++) {
        if (!s_handles[i]) {
            s_handles[i] = f;
            return MAKE_INT(i);
        }
    }
    fclose(f);
    pl_error_str("open: too many open files");
    return NIL_VAL;
}

static Val prim_close(Val args, Val env)
{
    (void)env;
    ensure_handles();
    Val x = expect_1("close: expected 1 argument", args);
    if (!IS_INT(x)) pl_error("close: expected file handle", x);
    int32_t h = INT_VAL(x);
    if (h >= 3 && h < MAX_HANDLES && s_handles[h]) {
        fclose(s_handles[h]);
        s_handles[h] = NULL;
    }
    return NIL_VAL;
}

/* ---- Raw terminal mode for non-blocking character input ---- */

#ifdef _WIN32
static HANDLE g_raw_hin = INVALID_HANDLE_VALUE;
static DWORD  g_raw_orig_mode = 0;
static int    g_raw_active = 0;

static void raw_mode_on(void) {
    if (g_raw_active) return;
    g_raw_hin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(g_raw_hin, &g_raw_orig_mode);
    SetConsoleMode(g_raw_hin, g_raw_orig_mode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT));
    g_raw_active = 1;
}

static void raw_mode_off(void) {
    if (!g_raw_active) return;
    SetConsoleMode(g_raw_hin, g_raw_orig_mode);
    g_raw_active = 0;
}

static int raw_read_byte_timeout(int ms) {
    if (g_raw_hin == INVALID_HANDLE_VALUE) return -1;
    if (WaitForSingleObject(g_raw_hin, (DWORD)ms) != WAIT_OBJECT_0) return -1;
    INPUT_RECORD ir; DWORD n;
    while (1) {
        if (!ReadConsoleInputA(g_raw_hin, &ir, 1, &n)) return -1;
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            char c = ir.Event.KeyEvent.uChar.AsciiChar;
            if (c) return (unsigned char)c;
        }
        /* Non-char event (mouse, etc) -- check if more events, else timeout */
        DWORD avail = 0;
        GetNumberOfConsoleInputEvents(g_raw_hin, &avail);
        if (avail == 0) return -1;
    }
}
#else
#include <termios.h>
static struct termios g_raw_orig;
static int g_raw_active = 0;

static void raw_mode_on(void) {
    if (g_raw_active) return;
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_raw_orig);
    raw = g_raw_orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_active = 1;
}

static void raw_mode_off(void) {
    if (!g_raw_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_raw_orig);
    g_raw_active = 0;
}

static int raw_read_byte_timeout(int ms) {
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    int r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return -1;
    unsigned char b;
    if (read(STDIN_FILENO, &b, 1) == 1) return (int)b;
    return -1;
}
#endif

/* Restore console mode on exit -- prevents stuck raw mode after Ctrl-C */
static void raw_mode_atexit(void) { raw_mode_off(); }
static int g_raw_atexit_registered = 0;
#ifdef _WIN32
static BOOL WINAPI raw_mode_ctrl_handler(DWORD type) {
    (void)type;
    raw_mode_off();
    return FALSE; /* let default handler run (exit) */
}
#endif

/* (raw-mode on?) -- enable/disable raw terminal mode */
static Val prim_raw_mode(Val args, Val env) {
    (void)env;
    Val on = arg1(args);
    if (IS_NIL(on)) {
        raw_mode_off();
    } else {
        raw_mode_on();
        if (!g_raw_atexit_registered) {
            atexit(raw_mode_atexit);
#ifdef _WIN32
            SetConsoleCtrlHandler(raw_mode_ctrl_handler, TRUE);
#endif
            g_raw_atexit_registered = 1;
        }
    }
    return NIL_VAL;
}

/* (read-byte-timeout ms) → integer byte or NIL if timeout */
static Val prim_read_byte_timeout(Val args, Val env) {
    (void)env;
    int ms = IS_INT(CAR(args)) ? INT_VAL(CAR(args)) : 0;
    int b = raw_read_byte_timeout(ms);
    return (b >= 0) ? MAKE_INT(b) : NIL_VAL;
}

/* (input-ready?) → T if stdin has data, NIL otherwise */
static Val prim_input_ready(Val args, Val env) {
    (void)args; (void)env;
    int b = raw_read_byte_timeout(0);
    /* Can't un-read, so this is imperfect. Better to use read-byte-timeout directly. */
    /* For simplicity, just check and return. */
    return (b >= 0) ? T_VAL : NIL_VAL;
    /* Note: this consumes the byte. The demo should use read-byte-timeout instead. */
}

static Val prim_read_line(Val args, Val env)
{
    (void)env;
    (void)args;
    FILE *f = stdin;
    if (g_current_reader && g_current_reader->ctx)
        f = (FILE *)g_current_reader->ctx;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) return NIL_VAL;
    size_t len = strlen(buf);
    /* Strip trailing newline */
    if (len > 0 && buf[len - 1] == '\n') { buf[--len] = '\0'; }
    if (len > 0 && buf[len - 1] == '\r') { buf[--len] = '\0'; }
    return MAKE_STR(str_intern(buf, len));
}

static Val prim_read_all_lines(Val args, Val env)
{
    (void)env;
    Val x = expect_1("read-all-lines: expected 1 argument", args);
    require_string("read-all-lines", x);
    const char *path = str_ptr(STR_IDX(x));
    if (!path) return NIL_VAL;
    FILE *f = fopen(path, "r");
    if (!f) return NIL_VAL;
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(head); PUSH_ROOT(tail);

    /* Use a growing dynamic buffer rather than fgets into a fixed array,
     * so file lines longer than the chunk size become a single L line
     * instead of being split into multiple L entries (with a spurious
     * empty entry whenever a line happens to exactly fill the chunk). */
    size_t cap  = 4096;
    char  *line = (char *)malloc(cap);
    if (!line) { fclose(f); POP_ROOT(); POP_ROOT();
                 pl_error_str("read-all-lines: out of memory"); }
    size_t len  = 0;

    int eof_reached = 0;
    while (!eof_reached) {
        int c = fgetc(f);
        if (c == EOF) {
            eof_reached = 1;
            if (len == 0) break;            /* trailing EOF, no partial line */
            /* fall through to emit the final partial line                   */
        } else if (c == '\n') {
            /* End of line: emit it (after stripping a trailing \r).         */
        } else {
            if (len + 1 >= cap) {
                cap *= 2;
                char *grown = (char *)realloc(line, cap);
                if (!grown) { free(line); fclose(f); POP_ROOT(); POP_ROOT();
                              pl_error_str("read-all-lines: out of memory"); }
                line = grown;
            }
            line[len++] = (char)c;
            continue;
        }
        if (len > 0 && line[len - 1] == '\r') len--;
        Val sv   = MAKE_STR(str_intern(line, len));
        len = 0;
        Val cell = pl_cons(sv, NIL_VAL);
        if (IS_NIL(g_gc_roots[g_gc_root_top - 2] /* head */)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
        } else {
            tail = g_gc_roots[g_gc_root_top - 1];
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
        }
    }
    free(line);
    fclose(f);
    head = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); POP_ROOT();
    return head;
}

static Val prim_write_lines(Val args, Val env)
{
    (void)env;
    expect_n("write-lines: expected 2 arguments", args, 2);
    Val pathv = arg1(args);
    Val lst   = arg2(args);
    require_string("write-lines", pathv);
    const char *path = str_ptr(STR_IDX(pathv));
    if (!path) return NIL_VAL;
    FILE *f = fopen(path, "w");
    if (!f) pl_error_str("write-lines: cannot open file for writing");
    while (IS_CONS(lst)) {
        Val line = CAR(lst);
        if (IS_STR(line)) {
            const char *s = str_ptr(STR_IDX(line));
            if (s) fputs(s, f);
        }
        fputc('\n', f);
        lst = CDR(lst);
    }
    fclose(f);
    return NIL_VAL;
}

static Val prim_read_char(Val args, Val env)
{
    (void)env;
    (void)args;
    FILE *f = stdin;
    if (g_current_reader && g_current_reader->ctx)
        f = (FILE *)g_current_reader->ctx;
    int c = fgetc(f);
    if (c == EOF) return NIL_VAL;
    char buf[2] = { (char)c, '\0' };
    return MAKE_STR(str_intern(buf, 1));
}

static Val prim_write_char(Val args, Val env)
{
    (void)env;
    Val x = expect_1("write-char: expected 1 argument", args);
    int c = 0;
    if (IS_INT(x)) c = INT_VAL(x);
    else if (IS_STR(x)) {
        const char *s = str_ptr(STR_IDX(x));
        if (s && s[0]) c = (unsigned char)s[0];
    }
    Port *out = g_current_output ? g_current_output : &g_stdout_port;
    out->putc_fn(c, out->ctx);
    return NIL_VAL;
}

static Val prim_eofp(Val args, Val env)
{
    (void)env;
    (void)args;
    FILE *f = stdin;
    if (g_current_reader && g_current_reader->ctx)
        f = (FILE *)g_current_reader->ctx;
    return feof(f) ? T_VAL : NIL_VAL;
}

static Val prim_flush(Val args, Val env)
{
    (void)env;
    (void)args;
    FILE *f = stdout;
    if (g_current_output && g_current_output->ctx)
        f = (FILE *)g_current_output->ctx;
    fflush(f);
    return NIL_VAL;
}

/* (in path . body) and (out path . body) are special forms in eval.c;
 * we provide stub primitives that handle the single-expression case. */
static Val prim_in(Val args, Val env)
{
    /* (in path expr) -- redirect current input to file, eval expr, restore */
    if (!IS_CONS(args)) pl_error_str("in: expected path argument");
    Val pathv = CAR(args);
    Val body  = CDR(args);
    require_string("in", pathv);
    const char *path = str_ptr(STR_IDX(pathv));
    FILE *f = fopen(path, "r");
    if (!f) pl_error_str("in: cannot open file");
    Reader new_reader;
    reader_init_file(&new_reader, f);
    Reader *saved     = g_current_reader;
    g_current_reader  = &new_reader;
    Val result = NIL_VAL;
    while (IS_CONS(body)) {
        result = eval(CAR(body), env);
        body   = CDR(body);
    }
    g_current_reader = saved;
    fclose(f);
    return result;
}

static Val prim_out(Val args, Val env)
{
    /* (out path expr) -- redirect current output to file, eval expr, restore */
    if (!IS_CONS(args)) pl_error_str("out: expected path argument");
    Val pathv = CAR(args);
    Val body  = CDR(args);
    require_string("out", pathv);
    const char *path = str_ptr(STR_IDX(pathv));
    FILE *f = fopen(path, "w");
    if (!f) pl_error_str("out: cannot open file");
    Port new_port = make_file_port(f);
    Port *saved   = g_current_output;
    g_current_output = &new_port;
    Val result = NIL_VAL;
    while (IS_CONS(body)) {
        result = eval(CAR(body), env);
        body   = CDR(body);
    }
    g_current_output = saved;
    fclose(f);
    return result;
}

/* =========================================================================
 * I.  JSON encode / decode
 * ===================================================================== */

/* Forward declaration for recursive encode. */
static void json_encode_val(Val v, DynBuf *buf);

static void json_encode_string(const char *s, DynBuf *buf)
{
    dynbuf_push(buf, '"');
    if (s) {
        while (*s) {
            unsigned char c = (unsigned char)*s++;
            switch (c) {
            case '"':  dynbuf_push(buf, '\\'); dynbuf_push(buf, '"');  break;
            case '\\': dynbuf_push(buf, '\\'); dynbuf_push(buf, '\\'); break;
            case '\n': dynbuf_push(buf, '\\'); dynbuf_push(buf, 'n');  break;
            case '\r': dynbuf_push(buf, '\\'); dynbuf_push(buf, 'r');  break;
            case '\t': dynbuf_push(buf, '\\'); dynbuf_push(buf, 't');  break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)c);
                    dynbuf_push_str(buf, esc);
                } else {
                    dynbuf_push(buf, (char)c);
                }
                break;
            }
        }
    }
    dynbuf_push(buf, '"');
}

static void json_encode_val(Val v, DynBuf *buf)
{
    if (IS_NIL(v)) {
        dynbuf_push_str(buf, "null");
    } else if (IS_INT(v)) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%d", (int)INT_VAL(v));
        dynbuf_push_str(buf, tmp);
    } else if (IS_BIG(v)) {
        char *s = big_to_str(v);
        if (s) { dynbuf_push_str(buf, s); free(s); }
        else dynbuf_push_str(buf, "0");
    } else if (IS_STR(v)) {
        json_encode_string(str_ptr(STR_IDX(v)), buf);
    } else if (IS_SYM(v)) {
        uint32_t idx = SYM_IDX(v);
        if (idx == g_sym_T) {
            dynbuf_push_str(buf, "true");
        } else {
            /* Encode symbol name as string */
            json_encode_string(sym_name(idx), buf);
        }
    } else if (IS_CONS(v)) {
        /* Heuristic: if car of first element is a cons (alist), encode as object.
         * Otherwise encode as array.                                        */
        Val first = CAR(v);
        bool is_obj = IS_CONS(first) && !IS_CONS(CAR(first));
        /* Actually: if it's a list of pairs where each pair's car is a string
         * or symbol, treat it as an object (alist).                         */
        bool as_obj = false;
        if (IS_CONS(first)) {
            Val k = CAR(first);
            if (IS_STR(k) || IS_SYM(k)) as_obj = true;
        }
        (void)is_obj;
        if (as_obj) {
            dynbuf_push(buf, '{');
            Val cur = v; bool first_elem = true;
            while (IS_CONS(cur)) {
                Val pair = CAR(cur);
                if (IS_CONS(pair)) {
                    if (!first_elem) dynbuf_push(buf, ',');
                    first_elem = false;
                    Val k = CAR(pair);
                    Val kval = CDR(pair);
                    /* Key */
                    if (IS_STR(k)) json_encode_string(str_ptr(STR_IDX(k)), buf);
                    else if (IS_SYM(k)) json_encode_string(sym_name(SYM_IDX(k)), buf);
                    else json_encode_val(k, buf);
                    dynbuf_push(buf, ':');
                    /* Value: if cdr is a cons cell, take its car (for dotted pair);
                     * if it's a proper list element use cadr. */
                    if (IS_CONS(kval)) json_encode_val(CAR(kval), buf);
                    else json_encode_val(kval, buf);
                }
                cur = CDR(cur);
            }
            dynbuf_push(buf, '}');
        } else {
            dynbuf_push(buf, '[');
            Val cur = v; bool first_elem = true;
            while (IS_CONS(cur)) {
                if (!first_elem) dynbuf_push(buf, ',');
                first_elem = false;
                json_encode_val(CAR(cur), buf);
                cur = CDR(cur);
            }
            dynbuf_push(buf, ']');
        }
    } else {
        dynbuf_push_str(buf, "null");
    }
}

static Val prim_json_encode(Val args, Val env)
{
    (void)env;
    Val x = expect_1("json-encode: expected 1 argument", args);
    DynBuf buf;
    dynbuf_init(&buf);
    json_encode_val(x, &buf);
    Val result = MAKE_STR(str_intern(buf.buf ? buf.buf : "", buf.len));
    free(buf.buf);
    return result;
}

/* Minimal JSON decoder */
typedef struct { const char *s; size_t pos; } JsonCtx;

static void json_skip_ws(JsonCtx *ctx)
{
    while (ctx->s[ctx->pos] && isspace((unsigned char)ctx->s[ctx->pos]))
        ctx->pos++;
}

static Val json_decode_val(JsonCtx *ctx);

static Val json_decode_string(JsonCtx *ctx)
{
    /* Assumes ctx->s[ctx->pos] == '"' */
    ctx->pos++; /* skip opening quote */
    DynBuf buf; dynbuf_init(&buf);
    while (ctx->s[ctx->pos] && ctx->s[ctx->pos] != '"') {
        char c = ctx->s[ctx->pos++];
        if (c == '\\') {
            char esc = ctx->s[ctx->pos++];
            switch (esc) {
            case '"':  dynbuf_push(&buf, '"');  break;
            case '\\': dynbuf_push(&buf, '\\'); break;
            case '/':  dynbuf_push(&buf, '/');  break;
            case 'n':  dynbuf_push(&buf, '\n'); break;
            case 'r':  dynbuf_push(&buf, '\r'); break;
            case 't':  dynbuf_push(&buf, '\t'); break;
            case 'b':  dynbuf_push(&buf, '\b'); break;
            case 'f':  dynbuf_push(&buf, '\f'); break;
            case 'u': {
                /* Parse 4 hex digits */
                unsigned int cp = 0;
                for (int i = 0; i < 4; i++) {
                    char hc = ctx->s[ctx->pos++];
                    int hv = 0;
                    if (hc >= '0' && hc <= '9') hv = hc - '0';
                    else if (hc >= 'a' && hc <= 'f') hv = hc - 'a' + 10;
                    else if (hc >= 'A' && hc <= 'F') hv = hc - 'A' + 10;
                    cp = (cp << 4) | (unsigned)hv;
                }
                /* Encode as UTF-8 (basic BMP only) */
                if (cp < 0x80) dynbuf_push(&buf, (char)cp);
                else if (cp < 0x800) {
                    dynbuf_push(&buf, (char)(0xC0 | (cp >> 6)));
                    dynbuf_push(&buf, (char)(0x80 | (cp & 0x3F)));
                } else {
                    dynbuf_push(&buf, (char)(0xE0 | (cp >> 12)));
                    dynbuf_push(&buf, (char)(0x80 | ((cp >> 6) & 0x3F)));
                    dynbuf_push(&buf, (char)(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: dynbuf_push(&buf, esc); break;
            }
        } else {
            dynbuf_push(&buf, c);
        }
    }
    if (ctx->s[ctx->pos] == '"') ctx->pos++;
    Val result = MAKE_STR(str_intern(buf.buf ? buf.buf : "", buf.len));
    free(buf.buf);
    return result;
}

static Val json_decode_array(JsonCtx *ctx)
{
    ctx->pos++; /* skip '[' */
    json_skip_ws(ctx);
    if (ctx->s[ctx->pos] == ']') { ctx->pos++; return NIL_VAL; }
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(head); PUSH_ROOT(tail);
    while (ctx->s[ctx->pos] && ctx->s[ctx->pos] != ']') {
        Val elem = json_decode_val(ctx);
        Val cell = pl_cons(elem, NIL_VAL);
        if (IS_NIL(head)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            head = cell; tail = cell;
        } else {
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            tail = cell;
        }
        json_skip_ws(ctx);
        if (ctx->s[ctx->pos] == ',') { ctx->pos++; json_skip_ws(ctx); }
    }
    if (ctx->s[ctx->pos] == ']') ctx->pos++;
    POP_ROOT(); POP_ROOT();
    return head;
}

static Val json_decode_object(JsonCtx *ctx)
{
    ctx->pos++; /* skip '{' */
    json_skip_ws(ctx);
    if (ctx->s[ctx->pos] == '}') { ctx->pos++; return NIL_VAL; }
    Val head = NIL_VAL, tail = NIL_VAL;
    PUSH_ROOT(head); PUSH_ROOT(tail);
    while (ctx->s[ctx->pos] && ctx->s[ctx->pos] != '}') {
        json_skip_ws(ctx);
        if (ctx->s[ctx->pos] != '"') break;
        Val key = json_decode_string(ctx);
        json_skip_ws(ctx);
        if (ctx->s[ctx->pos] == ':') ctx->pos++;
        json_skip_ws(ctx);
        Val val  = json_decode_val(ctx);
        Val pair = pl_cons(key, val);
        Val cell = pl_cons(pair, NIL_VAL);
        if (IS_NIL(head)) {
            g_gc_roots[g_gc_root_top - 2] = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            head = cell; tail = cell;
        } else {
            g_cells[CONS_IDX(tail)].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell;
            tail = cell;
        }
        json_skip_ws(ctx);
        if (ctx->s[ctx->pos] == ',') { ctx->pos++; json_skip_ws(ctx); }
    }
    if (ctx->s[ctx->pos] == '}') ctx->pos++;
    POP_ROOT(); POP_ROOT();
    return head;
}

static Val json_decode_val(JsonCtx *ctx)
{
    json_skip_ws(ctx);
    char c = ctx->s[ctx->pos];
    if (c == '"') return json_decode_string(ctx);
    if (c == '[') return json_decode_array(ctx);
    if (c == '{') return json_decode_object(ctx);
    if (c == 't' && strncmp(ctx->s + ctx->pos, "true", 4) == 0) {
        ctx->pos += 4; return T_VAL;
    }
    if (c == 'f' && strncmp(ctx->s + ctx->pos, "false", 5) == 0) {
        ctx->pos += 5; return NIL_VAL;
    }
    if (c == 'n' && strncmp(ctx->s + ctx->pos, "null", 4) == 0) {
        ctx->pos += 4; return NIL_VAL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        /* Parse integer (simplified: no float support) */
        int neg = 0;
        if (c == '-') { neg = 1; ctx->pos++; }
        int64_t n = 0;
        while (ctx->s[ctx->pos] >= '0' && ctx->s[ctx->pos] <= '9') {
            n = n * 10 + (ctx->s[ctx->pos++] - '0');
        }
        /* Skip fractional / exponent parts */
        if (ctx->s[ctx->pos] == '.') {
            ctx->pos++;
            while (ctx->s[ctx->pos] >= '0' && ctx->s[ctx->pos] <= '9') ctx->pos++;
        }
        if (ctx->s[ctx->pos] == 'e' || ctx->s[ctx->pos] == 'E') {
            ctx->pos++;
            if (ctx->s[ctx->pos] == '+' || ctx->s[ctx->pos] == '-') ctx->pos++;
            while (ctx->s[ctx->pos] >= '0' && ctx->s[ctx->pos] <= '9') ctx->pos++;
        }
        if (neg) n = -n;
        if (n >= INT32_MIN && n <= INT32_MAX) return MAKE_INT((int32_t)n);
        return big_from_int64(n);
    }
    /* Unknown token: skip and return NIL */
    while (ctx->s[ctx->pos] && !isspace((unsigned char)ctx->s[ctx->pos])
           && ctx->s[ctx->pos] != ',' && ctx->s[ctx->pos] != ']'
           && ctx->s[ctx->pos] != '}')
        ctx->pos++;
    return NIL_VAL;
}

static Val prim_json_decode(Val args, Val env)
{
    (void)env;
    Val x = expect_1("json-decode: expected 1 argument", args);
    require_string("json-decode", x);
    const char *s = str_ptr(STR_IDX(x));
    if (!s) return NIL_VAL;
    JsonCtx ctx;
    ctx.s   = s;
    ctx.pos = 0;
    return json_decode_val(&ctx);
}

/* =========================================================================
 * J.  Control primitives
 * ===================================================================== */

static Val prim_error(Val args, Val env)
{
    (void)env;
    if (!IS_CONS(args)) pl_error_str("error");
    Val msg = CAR(args);
    const char *m = "error";
    if (IS_STR(msg))      m = str_ptr(STR_IDX(msg));
    else if (IS_SYM(msg)) m = sym_name(SYM_IDX(msg));
    if (IS_CONS(CDR(args)))
        pl_error(m, CADR(args));
    else
        pl_error_str(m);
    return NIL_VAL;
}

static Val prim_quit(Val args, Val env)
{
    (void)env;
    int code = 0;
    if (IS_CONS(args) && IS_INT(CAR(args))) code = (int)INT_VAL(CAR(args));
    fflush(NULL); /* flush all stdio streams before bypassing atexit */
#ifdef _WIN32
    /* Use _exit on Windows to skip any atexit handlers (e.g. CoUninitialize)
     * that can stall.  _exit drains stdio; ExitProcess would terminate
     * immediately without flushing, so _exit is the right call here. */
    _exit(code);
#else
    exit(code);
#endif
    return NIL_VAL;
}

static Val prim_load(Val args, Val env)
{
    (void)env;
    Val x = expect_1("load: expected 1 argument", args);
    require_string("load", x);
    const char *path = str_ptr(STR_IDX(x));
    if (!path) return NIL_VAL;
    load_file(path);
    return NIL_VAL;
}

static Val prim_eval(Val args, Val env)
{
    Val x = expect_1("eval: expected 1 argument", args);
    return eval(x, env);
}

static Val prim_time_ms(Val args, Val env)
{
    (void)env;
    /* (time expr) -- but args are already evaluated; use as elapsed timer */
    /* We time the evaluation of an expression by receiving its already-
     * evaluated result.  For a proper timer, the special form would need
     * to intercept evaluation.  Here we just return 0 ms for the
     * evaluated-value form (the actual timing must be done at the call
     * site).  Return the value unchanged as documentation says "ms". */
    (void)args;
    /* Return current epoch ms as a compromise -- useful for benchmarking. */
#ifdef _WIN32
    /* On Windows: use time() in seconds, convert to ms, truncate to int32. */
    {
        int64_t ms = (int64_t)time(NULL) * 1000;
        return MAKE_INT((int32_t)(ms & (int64_t)0x7FFFFFFF));
    }
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (ms >= INT32_MIN && ms <= INT32_MAX) return MAKE_INT((int32_t)ms);
    return big_from_int64(ms);
#endif
}

/* =========================================================================
 * K.  Sort, maxi, mini
 * ===================================================================== */

/* Default less-than for sort: numbers (ascending), then strings (lex). */
static bool sort_val_lt(Val a, Val b)
{
    bool a_num = IS_INT(a) || IS_BIG(a);
    bool b_num = IS_INT(b) || IS_BIG(b);
    if (a_num && b_num) return pl_cmp(a, b) < 0;
    if (IS_STR(a) && IS_STR(b))
        return strcmp(str_ptr(STR_IDX(a)), str_ptr(STR_IDX(b))) < 0;
    if (a_num && IS_STR(b)) return true;   /* numbers before strings */
    if (IS_STR(a) && b_num) return false;
    pl_error_str("sort: cannot compare these types");
    return false;
}

/* Iterative merge of two sorted lists.  Relinks existing cons cells. */
static Val list_merge(Val a, Val b, Val fn, Val env)
{
    Val head = NIL_VAL, tail_cell = NIL_VAL;
    PUSH_ROOT(fn);
    PUSH_ROOT(head); PUSH_ROOT(tail_cell); PUSH_ROOT(a); PUSH_ROOT(b);

    while (IS_CONS(a) && IS_CONS(b)) {
        /* Stable sort: pick a when fn(b,a) is false (a <= b).
         * Calling fn(b,a) instead of fn(a,b) gives one call per comparison
         * and preserves original order for equal-keyed elements. */
        bool a_first;
        if (IS_NIL(fn)) {
            a_first = !sort_val_lt(CAR(b), CAR(a));
        } else {
            Val r = pl_apply(fn, LIST2(CAR(b), CAR(a)), env);
            a_first = IS_NIL(r);
        }
        Val chosen = a_first ? a : b;
        if (a_first) a = CDR(a); else b = CDR(b);
        g_gc_roots[g_gc_root_top - 2] = a;
        g_gc_roots[g_gc_root_top - 1] = b;

        g_cells[CONS_IDX(chosen)].cdr = NIL_VAL;
        if (IS_NIL(head)) {
            head = chosen; tail_cell = chosen;
            g_gc_roots[g_gc_root_top - 4] = head;
            g_gc_roots[g_gc_root_top - 3] = tail_cell;
        } else {
            g_cells[CONS_IDX(tail_cell)].cdr = chosen;
            tail_cell = chosen;
            g_gc_roots[g_gc_root_top - 3] = tail_cell;
        }
    }

    Val remainder = IS_CONS(a) ? a : b;
    if (IS_NIL(head)) head = remainder;
    else g_cells[CONS_IDX(tail_cell)].cdr = remainder;

    POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT(); /* +fn */
    return head;
}

/* Top-down merge sort.  Depth O(log n), GC-safe via root stack. */
static Val list_mergesort(Val lst, int len, Val fn, Val env)
{
    if (len <= 1) return lst;

    int half = len / 2;
    PUSH_ROOT(lst);
    /* Split: advance to cell at position (half-1), cut its cdr. */
    Val cur = lst;
    for (int i = 0; i < half - 1; i++) cur = CDR(cur);
    Val b = CDR(cur);
    g_cells[CONS_IDX(cur)].cdr = NIL_VAL;
    PUSH_ROOT(b);

    Val a_sorted = list_mergesort(lst, half, fn, env);
    g_gc_roots[g_gc_root_top - 2] = a_sorted;

    Val b_sorted = list_mergesort(b, len - half, fn, env);
    g_gc_roots[g_gc_root_top - 1] = b_sorted;

    Val result = list_merge(a_sorted, b_sorted, fn, env);
    POP_ROOT(); POP_ROOT();
    return result;
}

/* Shallow copy of a list -- allows non-destructive sort. */
static Val list_copy(Val lst)
{
    Val head = NIL_VAL, tail_cell = NIL_VAL;
    PUSH_ROOT(head); PUSH_ROOT(tail_cell);
    while (IS_CONS(lst)) {
        Val cell = pl_cons(CAR(lst), NIL_VAL);
        if (IS_NIL(head)) {
            head = cell; tail_cell = cell;
            g_gc_roots[g_gc_root_top - 2] = head;
            g_gc_roots[g_gc_root_top - 1] = tail_cell;
        } else {
            g_cells[CONS_IDX(tail_cell)].cdr = cell;
            tail_cell = cell;
            g_gc_roots[g_gc_root_top - 1] = tail_cell;
        }
        lst = CDR(lst);
    }
    POP_ROOT(); POP_ROOT();
    return head;
}

/* (sort lst) or (sort lst less-fn) -- non-destructive stable merge sort */
static Val prim_sort(Val args, Val env)
{
    int argc = val_list_len(args);
    if (argc < 1 || argc > 2) pl_error_str("sort: expected 1 or 2 arguments");
    Val lst = CAR(args);
    Val fn  = (argc == 2) ? CAR(CDR(args)) : NIL_VAL;
    if (!IS_CONS(lst) && !IS_NIL(lst)) pl_error_str("sort: expected a list");
    int len = val_list_len(lst);
    if (len == 0) return NIL_VAL;
    PUSH_ROOT(fn);
    Val copy = list_copy(lst);    /* sort on a copy -- original is unchanged */
    PUSH_ROOT(copy);
    if (len == 1) { POP_ROOT(); POP_ROOT(); return copy; }
    Val result = list_mergesort(copy, len, fn, env);
    POP_ROOT(); POP_ROOT();
    return result;
}

/* (maxi fun lst) -- element for which (fun elem) is greatest.
   (maxi lst)     -- greatest element (default comparison). */
static Val prim_maxi(Val args, Val env)
{
    int argc = val_list_len(args);
    if (argc < 1 || argc > 2) pl_error_str("maxi: expected 1 or 2 arguments");
    Val fn  = (argc == 2) ? CAR(args)      : NIL_VAL;
    Val lst = (argc == 2) ? CAR(CDR(args)) : CAR(args);
    if (!IS_CONS(lst)) pl_error_str("maxi: expected a non-empty list");
    Val best     = CAR(lst); lst = CDR(lst);
    Val best_key = IS_NIL(fn) ? best : pl_apply(fn, LIST1(best), env);
    PUSH_ROOT(best); PUSH_ROOT(best_key); PUSH_ROOT(lst);
    while (IS_CONS(lst)) {
        Val elem = CAR(lst);
        Val key  = IS_NIL(fn) ? elem : pl_apply(fn, LIST1(elem), env);
        if (sort_val_lt(best_key, key)) {   /* key > best_key */
            best = elem; best_key = key;
            g_gc_roots[g_gc_root_top - 3] = best;
            g_gc_roots[g_gc_root_top - 2] = best_key;
        }
        lst = CDR(lst);
        g_gc_roots[g_gc_root_top - 1] = lst;
    }
    POP_ROOT(); POP_ROOT(); POP_ROOT();
    return best;
}

/* (mini fun lst) / (mini lst) */
static Val prim_mini(Val args, Val env)
{
    int argc = val_list_len(args);
    if (argc < 1 || argc > 2) pl_error_str("mini: expected 1 or 2 arguments");
    Val fn  = (argc == 2) ? CAR(args)         : NIL_VAL;
    Val lst = (argc == 2) ? CAR(CDR(args))    : CAR(args);
    if (!IS_CONS(lst)) pl_error_str("mini: expected a non-empty list");
    Val best = CAR(lst); lst = CDR(lst);
    Val best_key = IS_NIL(fn) ? best : pl_apply(fn, LIST1(best), env);
    PUSH_ROOT(best); PUSH_ROOT(best_key); PUSH_ROOT(lst);
    while (IS_CONS(lst)) {
        Val elem = CAR(lst);
        Val key  = IS_NIL(fn) ? elem : pl_apply(fn, LIST1(elem), env);
        if (sort_val_lt(key, best_key)) {
            best = elem; best_key = key;
            g_gc_roots[g_gc_root_top - 3] = best;
            g_gc_roots[g_gc_root_top - 2] = best_key;
        }
        lst = CDR(lst);
        g_gc_roots[g_gc_root_top - 1] = lst;
    }
    POP_ROOT(); POP_ROOT(); POP_ROOT();
    return best;
}

/* =========================================================================
 * L.  Random numbers
 * ===================================================================== */

/* xorshift64 -- fast, decent quality, reproducible. */
static uint64_t rng_state = UINT64_C(0x9e3779b97f4a7c15);

static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* (rand)       → random non-negative int (full 63-bit range as bignum)
   (rand N)     → random int in [0, N-1]
   (rand N M)   → random int in [N, M] */
static Val prim_rand(Val args, Val env)
{
    (void)env;
    int argc = val_list_len(args);
    uint64_t r = rng_next();
    if (argc == 0) {
        return big_from_int64((int64_t)(r >> 1));   /* non-negative 63-bit */
    }
    Val nv = CAR(args);
    if (!IS_INT(nv) && !IS_BIG(nv)) pl_error_str("rand: expected integer");
    int64_t n = IS_INT(nv) ? (int64_t)INT_VAL(nv)
                           : (int64_t)strtoll(big_to_str(nv), NULL, 10);
    if (argc == 1) {
        if (n <= 0) pl_error_str("rand: N must be positive");
        int64_t v1 = (int64_t)(r % (uint64_t)n);
        return (v1 >= INT32_MIN && v1 <= INT32_MAX) ? MAKE_INT((int32_t)v1)
                                                     : big_from_int64(v1);
    }
    Val mv = CAR(CDR(args));
    if (!IS_INT(mv) && !IS_BIG(mv)) pl_error_str("rand: expected integer");
    int64_t m = IS_INT(mv) ? (int64_t)INT_VAL(mv)
                           : (int64_t)strtoll(big_to_str(mv), NULL, 10);
    if (m < n) pl_error_str("rand: M must be >= N");
    int64_t range = m - n + 1;
    int64_t v2 = n + (int64_t)(r % (uint64_t)range);
    return (v2 >= INT32_MIN && v2 <= INT32_MAX) ? MAKE_INT((int32_t)v2)
                                                 : big_from_int64(v2);
}

/* (seed N) → NIL -- seed the RNG */
static Val prim_seed(Val args, Val env)
{
    (void)env;
    Val nv = expect_1("seed: expected 1 argument", args);
    if (!IS_INT(nv) && !IS_BIG(nv)) pl_error_str("seed: expected integer");
    int64_t n = IS_INT(nv) ? (int64_t)INT_VAL(nv)
                           : (int64_t)strtoll(big_to_str(nv), NULL, 10);
    rng_state = (uint64_t)n ^ UINT64_C(0x9e3779b97f4a7c15);
    if (rng_state == 0) rng_state = UINT64_C(0x9e3779b97f4a7c15);
    return NIL_VAL;
}

/* =========================================================================
 * M.  Date and time
 * ===================================================================== */

#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#  include <sys/timeb.h>   /* _ftime64_s -- millisecond-precision epoch time */
#endif

/* (date)   → "YYYY-MM-DD"
   (date T) → (year month day) */
static Val prim_date(Val args, Val env)
{
    (void)env;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) pl_error_str("date: localtime failed");
    int argc = val_list_len(args);
    if (argc > 0 && IS_TRUE(CAR(args))) {
        /* list form */
        Val yr  = MAKE_INT(1900 + t->tm_year);
        Val mo  = MAKE_INT(1 + t->tm_mon);
        Val day = MAKE_INT(t->tm_mday);
        return LIST3(yr, mo, day);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             1900 + t->tm_year, 1 + t->tm_mon, t->tm_mday);
    return MAKE_STR(str_intern(buf, strlen(buf)));
}

/* (clock)   → "HH:MM:SS"
   (clock T) → (hour minute second) */
static Val prim_clock(Val args, Val env)
{
    (void)env;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) pl_error_str("clock: localtime failed");
    int argc = val_list_len(args);
    if (argc > 0 && IS_TRUE(CAR(args))) {
        Val h = MAKE_INT(t->tm_hour);
        Val m = MAKE_INT(t->tm_min);
        Val s = MAKE_INT(t->tm_sec);
        return LIST3(h, m, s);
    }
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             t->tm_hour, t->tm_min, t->tm_sec);
    return MAKE_STR(str_intern(buf, strlen(buf)));
}

/* (usec) → microseconds since Unix epoch (bignum) */
static Val prim_usec(Val args, Val env)
{
    (void)env; (void)args;
#ifdef _WIN32
    struct __timeb64 tb;
    _ftime64_s(&tb);
    int64_t us = (int64_t)tb.time * INT64_C(1000000) + (int64_t)tb.millitm * 1000LL;
    return big_from_int64(us);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t us = (int64_t)ts.tv_sec * INT64_C(1000000) + ts.tv_nsec / 1000;
    return big_from_int64(us);
#endif
}

/* =========================================================================
 * N.  Environment primitives
 * ===================================================================== */

static Val prim_val(Val args, Val env)
{
    (void)env;
    Val x = expect_1("val: expected 1 argument", args);
    if (!IS_SYM(x)) pl_error("val: not a symbol", x);
    return g_syms[SYM_IDX(x)].value;
}

static Val prim_set(Val args, Val env)
{
    (void)env;
    expect_n("set: expected 2 arguments", args, 2);
    Val sym = arg1(args);
    Val val = arg2(args);
    if (!IS_SYM(sym)) pl_error("set: not a symbol", sym);
    g_syms[SYM_IDX(sym)].value = val;
    return val;
}

static Val prim_boundp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("bound?: expected 1 argument", args);
    if (!IS_SYM(x)) return NIL_VAL;
    Val v = g_syms[SYM_IDX(x)].value;
    return IS_NIL(v) ? NIL_VAL : T_VAL;
}

static Val prim_identity(Val args, Val env)
{
    (void)env;
    return expect_1("identity: expected 1 argument", args);
}

/* (constantly x) -> a closure (lambda args x)
 * We build the closure as a proper Lisp lambda form stored in a cons cell. */
static Val prim_constantly(Val args, Val env)
{
    (void)env;
    Val x = expect_1("constantly: expected 1 argument", args);
    /* Build (lambda (args) x) where args is the rest parameter */
    uint32_t rest_sym = sym_intern("_args_", 6);
    Val params  = MAKE_SYM(rest_sym);   /* dotted rest param */
    Val body    = x;
    /* lambda form: (lambda params body) */
    Val lam = LIST3(MAKE_SYM(g_sym_lambda), params, body);
    return lam;
}

/* (compose f g) -> (lambda (x) (f (g x))) */
static Val prim_compose(Val args, Val env)
{
    (void)env;
    expect_n("compose: expected 2 arguments", args, 2);
    Val f = arg1(args);
    Val g_fn = arg2(args);
    uint32_t xsym = sym_intern("_x_", 3);
    Val xv = MAKE_SYM(xsym);
    /* (g x) */
    Val inner = LIST2(g_fn, xv);
    /* (f (g x)) */
    Val outer = LIST2(f, inner);
    /* (lambda (x) (f (g x))) */
    Val params = LIST1(xv);
    Val lam    = LIST3(MAKE_SYM(g_sym_lambda), params, outer);
    return lam;
}

/* gensym counter */
static uint32_t s_gensym_counter = 0;

static Val prim_gensym(Val args, Val env)
{
    (void)env;
    (void)args;
    char buf[32];
    snprintf(buf, sizeof(buf), "#g%u", ++s_gensym_counter);
    return MAKE_SYM(sym_intern(buf, strlen(buf)));
}

/* =========================================================================
 * L.  Mutable vector primitives
 * ===================================================================== */

static Val prim_vec(Val args, Val env)
{
    (void)env;
    /* (vec n init) */
    expect_n("vec: expected 2 arguments", args, 2);
    Val nv   = arg1(args);
    Val init = arg2(args);
    require_number("vec", nv);
    int32_t n = INT_VAL(nv);
    if (n < 0) n = 0;

    if (g_vec_count >= VEC_POOL_SIZE)
        pl_error_str("vec: vector pool exhausted");

    uint32_t idx = g_vec_count++;
    VecSlot *vs  = &g_vec_pool[idx];
    vs->len  = (uint32_t)n;
    vs->cap  = (uint32_t)(n > 0 ? n : 1);
    vs->mark = 0;
    vs->data = (Val *)malloc(vs->cap * sizeof(Val));
    if (!vs->data) pl_error_str("vec: out of memory");
    for (int32_t i = 0; i < n; i++) vs->data[i] = init;
    return MAKE_VEC(idx);
}

static Val prim_vec_get(Val args, Val env)
{
    (void)env;
    expect_n("vec-get: expected 2 arguments", args, 2);
    Val vv = arg1(args);
    Val iv = arg2(args);
    if (!IS_VEC(vv)) pl_error("vec-get: not a vector", vv);
    require_number("vec-get", iv);
    uint32_t idx = VEC_IDX(vv);
    if (idx >= g_vec_count) pl_error_str("vec-get: invalid vector");
    VecSlot *vs = &g_vec_pool[idx];
    int32_t i   = INT_VAL(iv);
    if (i < 0 || (uint32_t)i >= vs->len)
        pl_error_str("vec-get: index out of bounds");
    return vs->data[i];
}

static Val prim_vec_set(Val args, Val env)
{
    (void)env;
    expect_n("vec-set: expected 3 arguments", args, 3);
    Val vv = arg1(args);
    Val iv = arg2(args);
    Val xv = arg3(args);
    if (!IS_VEC(vv)) pl_error("vec-set: not a vector", vv);
    require_number("vec-set", iv);
    uint32_t idx = VEC_IDX(vv);
    if (idx >= g_vec_count) pl_error_str("vec-set: invalid vector");
    VecSlot *vs = &g_vec_pool[idx];
    int32_t i   = INT_VAL(iv);
    if (i < 0 || (uint32_t)i >= vs->len)
        pl_error_str("vec-set: index out of bounds");
    vs->data[i] = xv;
    return xv;
}

static Val prim_vec_length(Val args, Val env)
{
    (void)env;
    Val vv = expect_1("vec-length: expected 1 argument", args);
    if (!IS_VEC(vv)) pl_error("vec-length: not a vector", vv);
    uint32_t idx = VEC_IDX(vv);
    if (idx >= g_vec_count) return MAKE_INT(0);
    return MAKE_INT((int32_t)g_vec_pool[idx].len);
}

static Val prim_vec_push(Val args, Val env)
{
    (void)env;
    expect_n("vec-push: expected 2 arguments", args, 2);
    Val vv = arg1(args);
    Val xv = arg2(args);
    if (!IS_VEC(vv)) pl_error("vec-push: not a vector", vv);
    uint32_t idx = VEC_IDX(vv);
    if (idx >= g_vec_count) pl_error_str("vec-push: invalid vector");
    VecSlot *vs = &g_vec_pool[idx];
    if (vs->len >= vs->cap) {
        vs->cap = vs->cap ? vs->cap * 2 : 4;
        vs->data = (Val *)realloc(vs->data, vs->cap * sizeof(Val));
        if (!vs->data) pl_error_str("vec-push: out of memory");
    }
    vs->data[vs->len++] = xv;
    return vv;
}

static Val prim_vecp(Val args, Val env)
{
    (void)env;
    Val x = expect_1("vec?: expected 1 argument", args);
    return IS_VEC(x) ? T_VAL : NIL_VAL;
}

/* =========================================================================
 * M.  make / link / chain primitives
 * ===================================================================== */

static Val prim_link(Val args, Val env)
{
    (void)env;
    Val x = expect_1("link: expected 1 argument", args);
    Val cell = pl_cons(x, NIL_VAL);
    if (IS_NIL(g_make_head)) {
        g_make_head = cell;
        g_make_tail = cell;
    } else {
        g_cells[CONS_IDX(g_make_tail)].cdr = cell;
        g_make_tail = cell;
    }
    return x;
}

static Val prim_chain(Val args, Val env)
{
    (void)env;
    Val lst = expect_1("chain: expected 1 argument", args);
    /* Append every element of lst to the make accumulator. */
    while (IS_CONS(lst)) {
        Val cell = pl_cons(CAR(lst), NIL_VAL);
        if (IS_NIL(g_make_head)) {
            g_make_head = cell;
            g_make_tail = cell;
        } else {
            g_cells[CONS_IDX(g_make_tail)].cdr = cell;
            g_make_tail = cell;
        }
        lst = CDR(lst);
    }
    return g_make_head;
}

/* =========================================================================
 * N.  Parameter object primitives
 * ===================================================================== */

/* A parameter object is a closure (lambda) that:
 *   - called with no args: returns current value
 *   - called with one arg: sets and returns new value
 * We implement this as a function built from a mutable global sym binding. */

static Val prim_make_parameter(Val args, Val env)
{
    (void)env;
    Val init = expect_1("make-parameter: expected 1 argument", args);
    /* Allocate a fresh gensym to hold the dynamic value. */
    char buf[32];
    snprintf(buf, sizeof(buf), "#param%u", ++s_gensym_counter);
    uint32_t psym = sym_intern(buf, strlen(buf));
    g_syms[psym].value = init;

    /* Build a closure: (lambda _pa_args_
     *   (if (null? _pa_args_)
     *       (val 'psym)
     *       (set 'psym (car _pa_args_)))) */
    Val sym_val_name = MAKE_SYM(sym_intern("val",  3));
    Val sym_set_name = MAKE_SYM(sym_intern("set",  3));
    Val sym_null     = MAKE_SYM(sym_intern("null", 4));
    Val sym_car      = MAKE_SYM(sym_intern("car",  3));
    Val param_sym    = MAKE_SYM(psym);
    uint32_t pa_idx  = sym_intern("_pa_args_", 9);
    Val pa           = MAKE_SYM(pa_idx);

    Val quoted_psym  = LIST2(MAKE_SYM(g_sym_quote), param_sym);

    /* (val 'psym) */
    Val get_expr = LIST2(sym_val_name, quoted_psym);
    /* (car _pa_args_) */
    Val car_expr = LIST2(sym_car, pa);
    /* (set 'psym (car _pa_args_)) */
    Val set_expr = LIST3(sym_set_name, quoted_psym, car_expr);
    /* (null _pa_args_) */
    Val null_expr = LIST2(sym_null, pa);
    /* (if (null _pa_args_) (val 'psym) (set ...)) */
    Val if_expr = pl_cons(MAKE_SYM(g_sym_if),
                     pl_cons(null_expr,
                        pl_cons(get_expr,
                           pl_cons(set_expr, NIL_VAL))));

    /* (lambda _pa_args_ if_expr) */
    Val lam = LIST3(MAKE_SYM(g_sym_lambda), pa, if_expr);
    return lam;
}

/* =========================================================================
 * O.  Hash primitive (FNV-1a)
 * ===================================================================== */

static Val prim_hash(Val args, Val env)
{
    (void)env;
    /* (hash val nbits) -> FNV-1a hash of val's string repr, masked to nbits */
    expect_n("hash: expected 2 arguments", args, 2);
    Val v     = arg1(args);
    Val nbits = arg2(args);
    require_number("hash", nbits);
    int32_t bits = INT_VAL(nbits);
    if (bits <= 0 || bits > 32) bits = 32;

    /* Compute hash based on type */
    uint32_t h = 2166136261u;
    if (IS_INT(v)) {
        int32_t n = INT_VAL(v);
        uint8_t *p = (uint8_t *)&n;
        for (int i = 0; i < 4; i++) h = (h ^ p[i]) * 16777619u;
    } else if (IS_STR(v)) {
        const char *s = str_ptr(STR_IDX(v));
        if (s) {
            while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
        }
    } else if (IS_SYM(v)) {
        const char *s = sym_name(SYM_IDX(v));
        if (s) {
            while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
        }
    } else if (IS_BIG(v)) {
        char *s = big_to_str(v);
        if (s) {
            char *p = s;
            while (*p) h = (h ^ (uint8_t)*p++) * 16777619u;
            free(s);
        }
    } else {
        /* Use val_to_str for other types */
        char *s = val_to_str(v);
        if (s) {
            char *p = s;
            while (*p) h = (h ^ (uint8_t)*p++) * 16777619u;
            free(s);
        }
    }

    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return MAKE_INT((int32_t)(h & mask));
}

static Val prim_popcount(Val args, Val env)
{
    (void)env;
    Val v = arg1(args);
    require_number("popcount", v);
    uint32_t n = (uint32_t)INT_VAL(v);
#ifdef _MSC_VER
    n = __popcnt(n);
#else
    n = (uint32_t)__builtin_popcount(n);
#endif
    return MAKE_INT((int32_t)n);
}

/* (sha1 string) → 20-byte binary hash string (RFC 3174) */
static Val prim_sha1(Val args, Val env)
{
    (void)env;
    Val v = arg1(args);
    if (!IS_STR(v)) pl_error_str("sha1: expected string");
    StrSlot *ss = &g_strs[STR_IDX(v)];
    const uint8_t *msg = (const uint8_t*)ss->ptr;
    size_t len = ss->len;

    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
    /* Pre-processing: pad message */
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *buf = (uint8_t*)calloc(new_len, 1);
    if (!buf) return NIL_VAL;
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[new_len - 1 - i] = (uint8_t)(bits >> (i * 8));

    for (size_t offset = 0; offset < new_len; offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)buf[offset+i*4]<<24) | ((uint32_t)buf[offset+i*4+1]<<16) |
                   ((uint32_t)buf[offset+i*4+2]<<8) | buf[offset+i*4+3];
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i] = (t<<1)|(t>>31);
        }
        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if      (i < 20) { f = (b&c)|((~b)&d);     k = 0x5A827999; }
            else if (i < 40) { f = b^c^d;               k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b&c)|(b&d)|(c&d);   k = 0x8F1BBCDC; }
            else              { f = b^c^d;               k = 0xCA62C1D6; }
            uint32_t tmp = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e = d; d = c; c = (b<<30)|(b>>2); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    free(buf);
    uint8_t digest[20];
    for (int i = 0; i < 4; i++) {
        digest[i]    = (uint8_t)(h0 >> (24-i*8));
        digest[4+i]  = (uint8_t)(h1 >> (24-i*8));
        digest[8+i]  = (uint8_t)(h2 >> (24-i*8));
        digest[12+i] = (uint8_t)(h3 >> (24-i*8));
        digest[16+i] = (uint8_t)(h4 >> (24-i*8));
    }
    return MAKE_STR(str_intern((const char*)digest, 20));
}

/* (base64-encode string) → base64 ASCII string */
static Val prim_base64_encode(Val args, Val env)
{
    (void)env;
    Val v = arg1(args);
    if (!IS_STR(v)) pl_error_str("base64-encode: expected string");
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    StrSlot *ss = &g_strs[STR_IDX(v)];
    const uint8_t *src = (const uint8_t*)ss->ptr;
    size_t len = ss->len;
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char*)malloc(olen + 1);
    if (!out) return NIL_VAL;
    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        size_t start = i;
        uint32_t a = src[i++];
        uint32_t b = (i < len) ? src[i++] : 0;
        uint32_t c = (i < len) ? src[i++] : 0;
        size_t n = i - start; /* 1, 2, or 3 real bytes */
        uint32_t trip = (a << 16) | (b << 8) | c;
        out[j++] = t[(trip >> 18) & 0x3F];
        out[j++] = t[(trip >> 12) & 0x3F];
        out[j++] = (n >= 2) ? t[(trip >> 6) & 0x3F] : '=';
        out[j++] = (n >= 3) ? t[trip & 0x3F] : '=';
    }
    out[j] = '\0';
    uint32_t si = str_intern(out, j);
    free(out);
    return MAKE_STR(si);
}

/* =========================================================================
 * Q.  Property list primitives: put / get / prop / plist
 *
 * Every symbol has a plist stored as an alist: ((key . val) ...)
 * Keys are compared with val_equal (structural equality, same as equal?).
 *
 *   (put  sym key val)  -- set/update a property; returns val
 *   (get  sym key)      -- retrieve a property value; NIL if absent
 *   (prop sym key)      -- return the (key . val) cell itself (for mutation)
 *   (plist sym)         -- return the entire alist
 * ===================================================================== */

/* Walk sym's plist looking for key; return the (key . val) pair or NIL. */
static Val plist_find(uint32_t sidx, Val key)
{
    Val pl = g_syms[sidx].plist;
    while (IS_CONS(pl)) {
        Val pair = CAR(pl);
        if (IS_CONS(pair) && val_equal(CAR(pair), key))
            return pair;
        pl = CDR(pl);
    }
    return NIL_VAL;
}

/* (get sym key) → value or NIL */
static Val prim_get(Val args, Val env)
{
    (void)env;
    expect_n("get: expected 2 arguments", args, 2);
    Val sym = arg1(args);
    Val key = arg2(args);
    if (!IS_SYM(sym)) pl_error("get: not a symbol", sym);
    Val pair = plist_find(SYM_IDX(sym), key);
    return IS_CONS(pair) ? CDR(pair) : NIL_VAL;
}

/* (prop sym key) → the (key . val) cons cell, or NIL */
static Val prim_prop(Val args, Val env)
{
    (void)env;
    expect_n("prop: expected 2 arguments", args, 2);
    Val sym = arg1(args);
    Val key = arg2(args);
    if (!IS_SYM(sym)) pl_error("prop: not a symbol", sym);
    return plist_find(SYM_IDX(sym), key);
}

/* (put sym key val) → val
 * Updates the value in place if the key already exists; otherwise prepends
 * a new (key . val) pair to the front of the plist. */
static Val prim_put(Val args, Val env)
{
    (void)env;
    expect_n("put: expected 3 arguments", args, 3);
    Val sym = arg1(args);
    Val key = arg2(args);
    Val val = arg3(args);
    if (!IS_SYM(sym)) pl_error("put: not a symbol", sym);
    uint32_t sidx = SYM_IDX(sym);
    Val pair = plist_find(sidx, key);
    if (IS_CONS(pair)) {
        g_cells[CONS_IDX(pair)].cdr = val;
        return val;
    }
    /* Not found: cons (key . val) onto plist.
     * Root both live vals across the two allocations. */
    PUSH_ROOT(val);
    PUSH_ROOT(key);
    Val new_pair  = pl_cons(key, val);
    POP_ROOT();             /* key */
    PUSH_ROOT(new_pair);
    Val new_plist = pl_cons(new_pair, g_syms[sidx].plist);
    POP_ROOT(); POP_ROOT(); /* new_pair, val */
    g_syms[sidx].plist = new_plist;
    return val;
}

/* (plist sym) → the full property list alist */
static Val prim_plist(Val args, Val env)
{
    (void)env;
    Val sym = expect_1("plist: expected 1 argument", args);
    if (!IS_SYM(sym)) pl_error("plist: not a symbol", sym);
    return g_syms[SYM_IDX(sym)].plist;
}

/* =========================================================================
 * Memory access primitives -- for FFI pointer dereferencing in callbacks
 * ===================================================================== */

/* (mem-read-i32 ptr byte-offset) → integer
 * Reads a 32-bit signed int from (ptr + byte-offset). ptr is a raw pointer
 * value (integer or bignum). */
static Val prim_mem_read_i32(Val args, Val env)
{
    (void)env;
    Val ptr_val = CAR(args); args = CDR(args);
    Val off_val = CAR(args);

    int64_t ptr;
    if (IS_INT(ptr_val))
        ptr = (int64_t)INT_VAL(ptr_val);
    else if (IS_BIG(ptr_val))
        ptr = (int64_t)strtoll(big_to_str(ptr_val), NULL, 10);
    else
        pl_error_str("mem-read-i32: ptr must be an integer");
    if (!ptr)
        pl_error_str("mem-read-i32: null pointer");
    int32_t off = IS_INT(off_val) ? INT_VAL(off_val) : 0;

    int32_t result;
    memcpy(&result, (char *)(uintptr_t)(uint64_t)ptr + off, sizeof(result));
    return MAKE_INT(result);
}

/* (mem-write-i32 ptr byte-offset value) → NIL
 * Writes a 32-bit signed int to (ptr + byte-offset). */
static Val prim_mem_write_i32(Val args, Val env)
{
    (void)env;
    Val ptr_val = CAR(args); args = CDR(args);
    Val off_val = CAR(args); args = CDR(args);
    Val val_val = CAR(args);

    int64_t ptr;
    if (IS_INT(ptr_val))
        ptr = (int64_t)INT_VAL(ptr_val);
    else if (IS_BIG(ptr_val))
        ptr = (int64_t)strtoll(big_to_str(ptr_val), NULL, 10);
    else
        pl_error_str("mem-write-i32: ptr must be an integer");
    if (!ptr)
        pl_error_str("mem-write-i32: null pointer");
    int32_t off = IS_INT(off_val) ? INT_VAL(off_val) : 0;
    int32_t val = IS_INT(val_val) ? INT_VAL(val_val) : 0;

    memcpy((char *)(uintptr_t)(uint64_t)ptr + off, &val, sizeof(val));
    return NIL_VAL;
}

/* =========================================================================
 * P.  prims_init -- registration entry point
 * ===================================================================== */

void prims_init(void)
{
    /* --- List / Cell --- */
    prim_register("cons",       prim_cons,      2,  2);
    prim_register("car",        prim_car,       1,  1);
    prim_register("cdr",        prim_cdr,       1,  1);
    prim_register("cadr",       prim_cadr,      1,  1);
    prim_register("cddr",       prim_cddr,      1,  1);
    prim_register("caddr",      prim_caddr,     1,  1);
    prim_register("cdddr",      prim_cdddr,     1,  1);
    prim_register("set-car",    prim_set_car,   2,  2);
    prim_register("set-cdr",    prim_set_cdr,   2,  2);
    prim_register("list",       prim_list,      0, -1);
    prim_register("list*",      prim_list_star, 1, -1);
    prim_register("append",     prim_append,    0, -1);
    prim_register("nconc",      prim_nconc,     2,  2);
    prim_register("reverse",    prim_reverse,   1,  1);
    prim_register("length",     prim_length,    1,  1);
    prim_register("wbt-from-list", prim_wbt_from_list, 1, 1);
    prim_register("last",       prim_last,      1,  1);
    prim_register("nth",        prim_nth,       2,  2);
    prim_register("nthcdr",     prim_nth,       2,  2);
    prim_register("member",     prim_member,    2,  2);
    prim_register("memq",       prim_memq,      2,  2);
    prim_register("assoc",      prim_assoc,     2,  2);
    prim_register("assq",       prim_assq,      2,  2);
    prim_register("filter",     prim_filter,    2,  2);
    prim_register("remove",     prim_remove,    2,  2);
    prim_register("mapcar",     prim_mapcar,    2,  2);
    prim_register("mapc",       prim_mapc,      2,  2);
    prim_register("apply",      prim_apply,     2,  2);
    prim_register("funcall",    prim_funcall,   1, -1);

    /* --- Predicates --- */
    prim_register("null",       prim_null,      1,  1);
    prim_register("null?",      prim_null,      1,  1);
    prim_register("atom",       prim_atom,      1,  1);
    prim_register("atom?",      prim_atom,      1,  1);
    prim_register("pair",       prim_pair,      1,  1);
    prim_register("pair?",      prim_pair,      1,  1);
    prim_register("listp",      prim_listp,     1,  1);
    prim_register("list?",      prim_listp,     1,  1);
    prim_register("numberp",    prim_numberp,   1,  1);
    prim_register("number?",    prim_numberp,   1,  1);
    prim_register("stringp",    prim_stringp,   1,  1);
    prim_register("string?",    prim_stringp,   1,  1);
    prim_register("symbolp",    prim_symbolp,   1,  1);
    prim_register("symbol?",    prim_symbolp,   1,  1);
    prim_register("procedurep", prim_procedurep,1,  1);
    prim_register("procedure?", prim_procedurep,1,  1);
    prim_register("eq",         prim_eq,        2,  2);
    prim_register("eq?",        prim_eq,        2,  2);
    prim_register("equal",      prim_equal,     2,  2);
    prim_register("equal?",     prim_equal,     2,  2);
    prim_register("=",          prim_numeq,     2,  2);
    prim_register("<",          prim_numlt,     2,  2);
    prim_register(">",          prim_numgt,     2,  2);
    prim_register("<=",         prim_numle,     2,  2);
    prim_register(">=",         prim_numge,     2,  2);
    prim_register("not",        prim_not,       1,  1);

    /* --- Arithmetic --- */
    prim_register("+",          prim_add,       0, -1);
    prim_register("-",          prim_sub,       1, -1);
    prim_register("*",          prim_mul,       0, -1);
    prim_register("/",          prim_div,       2,  2);
    prim_register("mod",        prim_mod,       2,  2);
    prim_register("rem",        prim_rem,       2,  2);
    prim_register("**",         prim_pow,       2,  2);
    prim_register("expt",       prim_pow,       2,  2);
    prim_register("abs",        prim_abs,       1,  1);
    prim_register("max",        prim_max,       1, -1);
    prim_register("min",        prim_min,       1, -1);
    prim_register("1+",         prim_inc1,      1,  1);
    prim_register("1-",         prim_dec1,      1,  1);
    prim_register("inc",        prim_inc,       1,  1);
    prim_register("dec",        prim_dec,       1,  1);
    prim_register("gcd",        prim_gcd,       2,  2);
    prim_register("lcm",        prim_lcm,       2,  2);
    prim_register("bitand",     prim_bitand,    2,  2);
    prim_register("bitor",      prim_bitor,     2,  2);
    prim_register("bitxor",     prim_bitxor,    2,  2);
    prim_register("bitnot",     prim_bitnot,    1,  1);
    prim_register(">>",         prim_shr,       2,  2);
    prim_register("<<",         prim_shl,       2,  2);
    prim_register("bit?",       prim_bitp,      2,  2);
    prim_register("bitmask",    prim_bitmask,   1,  1);

    /* --- Bignum --- */
    prim_register("big",        prim_big,       1,  1);
    prim_register("big?",       prim_bigp,      1,  1);

    /* --- Float / Math --- */
    prim_register("float",      prim_to_float,  1,  1);
    prim_register("float?",     prim_floatp,    1,  1);
    prim_register("sqrt",       prim_sqrt,      1,  1);
    prim_register("sin",        prim_sin,       1,  1);
    prim_register("cos",        prim_cos,       1,  1);
    prim_register("tan",        prim_tan,       1,  1);
    prim_register("asin",       prim_asin,      1,  1);
    prim_register("acos",       prim_acos,      1,  1);
    prim_register("atan",       prim_atan,      1,  1);
    prim_register("exp",        prim_m_exp,     1,  1);
    prim_register("log",        prim_m_log,     1,  1);
    prim_register("pow",        prim_m_pow,     2,  2);
    prim_register("floor",      prim_m_floor,   1,  1);
    prim_register("ceil",       prim_m_ceil,    1,  1);
    prim_register("round",      prim_m_round,   1,  1);
    prim_register("pi",         prim_pi,        0,  0);

    /* --- String / Symbol --- */
    prim_register("name",       prim_name,      1,  1);
    prim_register("intern",     prim_intern,    1,  1);
    prim_register("str",        prim_str,       1,  1);
    prim_register("sym",        prim_sym,       1,  1);
    prim_register("pack",       prim_pack,      1,  1);
    prim_register("unpack",     prim_unpack,    1,  1);
    prim_register("upcase",     prim_upcase,    1,  1);
    prim_register("downcase",   prim_downcase,  1,  1);
    prim_register("trim",       prim_trim,      1,  1);
    prim_register("ltrim",      prim_ltrim,     1,  1);
    prim_register("rtrim",      prim_rtrim,     1,  1);
    prim_register("sub",        prim_substr,    2,  3);
    prim_register("index",      prim_index,     2,  2);
    prim_register("replace",    prim_replace,   3,  3);
    prim_register("format",     prim_format,    1,  3);
    prim_register("fmt",        prim_fmt,       1, -1);

    /* --- Character --- */
    prim_register("char",       prim_char,      1,  1);

    /* --- Property lists --- */
    prim_register("put",        prim_put,       3,  3);
    prim_register("get",        prim_get,       2,  2);
    prim_register("prop",       prim_prop,      2,  2);
    prim_register("plist",      prim_plist,     1,  1);

    /* --- I/O --- */
    prim_register("prin",       prim_prin,      0, -1);
    prim_register("print",      prim_print,     0, -1);
    prim_register("prinl",      prim_prinl,     0, -1);
    prim_register("println",    prim_println,   0, -1);
    prim_register("write",      prim_write,     1,  1);
    prim_register("read",       prim_read,      0,  0);
    prim_register("read-from-string", prim_read_from_string, 1, 1);
    prim_register("open",       prim_open,      2,  2);
    prim_register("close",      prim_close,     1,  1);
    prim_register("in",         prim_in,        1, -1);
    prim_register("out",        prim_out,       1, -1);
    prim_register("read-line",  prim_read_line, 0,  0);
    prim_register("input-ready?", prim_input_ready, 0, 0);
    prim_register("raw-mode",  prim_raw_mode,  1,  1);
    prim_register("read-byte-timeout", prim_read_byte_timeout, 1, 1);
    prim_register("read-all-lines", prim_read_all_lines, 1, 1);
    prim_register("write-lines",    prim_write_lines,    2, 2);
    prim_register("read-char",  prim_read_char, 0,  0);
    prim_register("write-char", prim_write_char, 1, 1);
    prim_register("eof?",       prim_eofp,      0,  0);
    prim_register("flush",      prim_flush,     0,  0);

    /* --- JSON --- */
    prim_register("json-encode", prim_json_encode, 1, 1);
    prim_register("json-decode", prim_json_decode, 1, 1);

    /* --- Control --- */
    prim_register("error",      prim_error,     1, -1);
    prim_register("quit",       prim_quit,      0,  1);
    prim_register("load",       prim_load,      1,  1);
    prim_register("eval",       prim_eval,      1,  1);
    prim_register("time",       prim_time_ms,   0,  0);
    prim_register("date",       prim_date,      0,  1);
    prim_register("clock",      prim_clock,     0,  1);
    prim_register("usec",       prim_usec,      0,  0);

    /* --- Sort / maxi / mini --- */
    prim_register("sort",       prim_sort,      1,  2);
    prim_register("maxi",       prim_maxi,      1,  2);
    prim_register("mini",       prim_mini,      1,  2);

    /* --- Random numbers --- */
    prim_register("rand",       prim_rand,      0,  2);
    prim_register("seed",       prim_seed,      1,  1);

    /* --- Environment --- */
    prim_register("val",        prim_val,       1,  1);
    prim_register("set",        prim_set,       2,  2);
    prim_register("bound?",     prim_boundp,    1,  1);
    prim_register("identity",   prim_identity,  1,  1);
    prim_register("constantly", prim_constantly,1,  1);
    prim_register("compose",    prim_compose,   2,  2);
    prim_register("gensym",     prim_gensym,    0,  0);

    /* --- Vectors --- */
    prim_register("vec",        prim_vec,       2,  2);
    prim_register("vec-get",    prim_vec_get,   2,  2);
    prim_register("vec-set",    prim_vec_set,   3,  3);
    prim_register("vec-length", prim_vec_length,1,  1);
    prim_register("vec-push",   prim_vec_push,  2,  2);
    prim_register("vec?",       prim_vecp,      1,  1);

    /* --- link / chain --- */
    prim_register("link",       prim_link,      1,  1);
    prim_register("chain",      prim_chain,     1,  1);

    /* --- Parameter objects --- */
    prim_register("make-parameter", prim_make_parameter, 1, 1);

    /* --- Hash / crypto --- */
    prim_register("hash",          prim_hash,          2,  2);
    prim_register("popcount",      prim_popcount,      1,  1);
    prim_register("sha1",          prim_sha1,          1,  1);
    prim_register("base64-encode", prim_base64_encode, 1,  1);

    /* --- Memory access (FFI pointer dereferencing) --- */
    prim_register("mem-read-i32",  prim_mem_read_i32,  2, 2);
    prim_register("mem-write-i32", prim_mem_write_i32, 3, 3);

    /* -----------------------------------------------------------------------
     * Register special constants as global symbol values.
     * --------------------------------------------------------------------- */
    uint32_t sidx;

    sidx = sym_intern("space",   5);
    g_syms[sidx].value = MAKE_STR(str_intern(" ",  1));

    sidx = sym_intern("tab",     3);
    g_syms[sidx].value = MAKE_STR(str_intern("\t", 1));

    sidx = sym_intern("newline", 7);
    g_syms[sidx].value = MAKE_STR(str_intern("\n", 1));

    /* *version* */
    sidx = sym_intern("*version*", 9);
    g_syms[sidx].value = MAKE_STR(str_intern("lispIsPerfect9-1.0", 18));

    /* *os* -- compile-time platform identifier: "windows" or "posix" */
#ifdef _WIN32
    sidx = sym_intern("*os*", 4);
    g_syms[sidx].value = MAKE_STR(str_intern("windows", 7));
#else
    sidx = sym_intern("*os*", 4);
    g_syms[sidx].value = MAKE_STR(str_intern("posix", 5));
#endif
}
