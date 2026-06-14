/* eval.c -- TCO trampoline evaluator for the L (lispIsPerfect9) interpreter.
 *
 * Implements:
 *   - TCO trampoline (tail calls via goto tail_call)
 *   - All ~30 special forms
 *   - Dynamic binding stack (de-style functions)
 *   - catch/throw via setjmp/longjmp
 *   - Macro expansion (dmacro / macro)
 *   - Quasiquote expansion at eval time
 *   - Parameter objects (make-parameter / parameterize)
 *   - recur for named self-tail-calls
 *   - PicoLisp-style make / link / chain list accumulation
 *   - Scheme-style do loop
 *   - guard (simplified SRFI-34)
 *   - with (PicoLisp object context)
 *   - letrec / letrec*
 */

#include "picolisp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* =========================================================================
 * Catch / throw stack
 * ===================================================================== */
#define CATCH_STACK_SIZE 256

typedef struct {
    jmp_buf jb;
    Val     tag;
    Val     thrown_val;
    int     bind_mark;
    int     gc_root_mark;
} CatchFrame;

static CatchFrame g_catch_stack[CATCH_STACK_SIZE];
static int        g_catch_top = 0;

/* =========================================================================
 * recur state -- set by (recur arg...) form, detected after eval returns
 * ===================================================================== */
static Val g_recur_args = NIL_VAL;  /* evaluated argument list for recur */
static int g_in_recur   = 0;        /* non-zero when recur was triggered  */

/* =========================================================================
 * make-parameter symbol index (interned lazily)
 * ===================================================================== */
static uint32_t g_sym_parameter     = 0;
static uint32_t g_sym_parameterize  = 0;
static uint32_t g_sym_make_param    = 0;
static uint32_t g_sym_this          = 0;  /* 'This' for (with obj ...) */
static uint32_t g_sym_colon         = 0;  /* ':'  -- get This property  */
static uint32_t g_sym_coloncolon    = 0;  /* '::' -- prop This cell     */
static uint32_t g_sym_setcolon      = 0;  /* '=:' -- put This property  */

/* Lazy intern helpers. */
static uint32_t sym_parameter(void) {
    if (!g_sym_parameter)
        g_sym_parameter = sym_intern("parameter", 9);
    return g_sym_parameter;
}

/* =========================================================================
 * Dynamic binding
 * ===================================================================== */

int dynamic_bind(uint32_t sym_idx, Val new_val) {
    int mark = g_bind_top;
    if (g_bind_top >= BIND_STACK_SIZE)
        pl_error_str("dynamic bind stack overflow");
    g_bind_stack[g_bind_top].sym_idx = sym_idx;
    g_bind_stack[g_bind_top].saved   = g_syms[sym_idx].value;
    g_bind_top++;
    g_syms[sym_idx].value = new_val;
    return mark;
}

void dynamic_unbind_to(int mark) {
    while (g_bind_top > mark) {
        g_bind_top--;
        uint32_t idx = g_bind_stack[g_bind_top].sym_idx;
        g_syms[idx].value = g_bind_stack[g_bind_top].saved;
    }
}

/* =========================================================================
 * Environment lookup
 *
 * L uses dynamic scoping as the primary model (like PicoLisp).  The env
 * alist is used only for lexical closures created by lambda.  Symbol lookup
 * checks the alist first, then falls through to g_syms[idx].value.
 * ===================================================================== */
static Val env_lookup(Val sym, Val env) {
    uint32_t sidx = SYM_IDX(sym);
    Val e = env;
    while (IS_CONS(e)) {
        Val binding = CAR(e);
        if (IS_CONS(binding) && IS_SYM(CAR(binding)) &&
            SYM_IDX(CAR(binding)) == sidx)
            return CDR(binding);
        e = CDR(e);
    }
    return g_syms[sidx].value;
}

/* =========================================================================
 * Parameter-list binding helpers
 * ===================================================================== */

/*
 * bind_params_env -- build a new lexical env alist for lambda calls.
 *
 * params may be:
 *   NIL          - no parameters
 *   (a b c)      - positional
 *   (a b . rest) - positional + rest
 *   sym          - all args as a single list (variadic)
 */
static Val bind_params_env(Val params, Val args, Val base_env) {
    /* Push three roots we will update in-place:
     *   [top-3] = env   (accumulating result)
     *   [top-2] = params (cursor)
     *   [top-1] = args   (cursor)
     */
    Val env = base_env;
    PUSH_ROOT(env);    /* slot A: env result */
    PUSH_ROOT(params); /* slot B: params cursor */
    PUSH_ROOT(args);   /* slot C: args cursor */

    /* Variadic shorthand: params is a bare symbol (not NIL). */
    if (IS_SYM(g_gc_roots[g_gc_root_top - 2]) &&
        !IS_NIL(g_gc_roots[g_gc_root_top - 2])) {
        Val binding = pl_cons(g_gc_roots[g_gc_root_top - 2],
                              g_gc_roots[g_gc_root_top - 1]);
        PUSH_ROOT(binding);
        Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 4]); /* env */
        POP_ROOT(); /* binding */
        g_gc_roots[g_gc_root_top - 3] = ne; /* update env slot */
        goto done_bpe;
    }

    /* Positional parameters -- with destructuring support.
     * If a parameter position is a CONS (nested pattern), recursively
     * destructure the corresponding argument. */
    while (IS_CONS(g_gc_roots[g_gc_root_top - 2]) &&
           IS_CONS(g_gc_roots[g_gc_root_top - 1])) {
        Val p = CAR(g_gc_roots[g_gc_root_top - 2]);
        Val a = CAR(g_gc_roots[g_gc_root_top - 1]);
        if (IS_CONS(p)) {
            /* Destructure: p is a nested pattern like (a b . rest) */
            Val new_env = bind_params_env(p, a,
                              g_gc_roots[g_gc_root_top - 3]);
            g_gc_roots[g_gc_root_top - 3] = new_env;
        } else {
            Val binding = pl_cons(p, a);
            PUSH_ROOT(binding);
            Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 4]);
            POP_ROOT();
            g_gc_roots[g_gc_root_top - 3] = ne;
        }
        g_gc_roots[g_gc_root_top - 2] = CDR(g_gc_roots[g_gc_root_top - 2]);
        g_gc_roots[g_gc_root_top - 1] = CDR(g_gc_roots[g_gc_root_top - 1]);
    }
    /* Dotted rest. */
    if (IS_SYM(g_gc_roots[g_gc_root_top - 2]) &&
        !IS_NIL(g_gc_roots[g_gc_root_top - 2])) {
        Val binding = pl_cons(g_gc_roots[g_gc_root_top - 2],
                              g_gc_roots[g_gc_root_top - 1]);
        PUSH_ROOT(binding);
        Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 4]);
        POP_ROOT();
        g_gc_roots[g_gc_root_top - 3] = ne;
    }

done_bpe:
    {
        Val result = g_gc_roots[g_gc_root_top - 3]; /* env */
        POP_ROOT(); /* args */
        POP_ROOT(); /* params */
        POP_ROOT(); /* env */
        return result;
    }
}

/*
 * bind_params_dynamic -- push symbol/value pairs onto the dynamic bind stack.
 * Returns the mark for later dynamic_unbind_to().
 */
static int bind_params_dynamic(Val params, Val args) {
    int mark = g_bind_top;
    while (IS_CONS(params) && IS_CONS(args)) {
        Val p = CAR(params);
        Val a = CAR(args);
        if (IS_CONS(p))
            bind_params_dynamic(p, a); /* destructure nested pattern */
        else if (IS_SYM(p))
            dynamic_bind(SYM_IDX(p), a);
        params = CDR(params);
        args   = CDR(args);
    }
    if (IS_SYM(params) && !IS_NIL(params))
        dynamic_bind(SYM_IDX(params), args);
    return mark;
}

/* =========================================================================
 * Forward declarations
 * ===================================================================== */
Val  eval(Val expr, Val env);

static Val eval_body_non_tco(Val body, Val env);
static Val expand_quasiquote(Val form, Val env, int depth);

/* =========================================================================
 * make_prog_expr -- wrap a body list as a single evaluable expression.
 * ===================================================================== */
static Val make_prog_expr(Val body) {
    if (IS_CONS(body) && IS_NIL(CDR(body)))
        return CAR(body);
    PUSH_ROOT(body);
    Val result = pl_cons(MAKE_SYM(g_sym_prog), g_gc_roots[g_gc_root_top - 1]);
    POP_ROOT();
    return result;
}

/* =========================================================================
 * eval_body_non_tco -- evaluate body, return last; no TCO.
 * ===================================================================== */
static Val eval_body_non_tco(Val body, Val env) {
    Val result = NIL_VAL;
    while (IS_CONS(body)) {
        result = eval(CAR(body), env);
        body   = CDR(body);
    }
    return result;
}

/* =========================================================================
 * eval_list -- evaluate every element of a list; return a new list.
 *
 * GC safety: indices are stable (no moving GC), so cur = CDR(cur) is safe
 * across allocations.  We still push roots to prevent collection.
 * ===================================================================== */
Val eval_list(Val lst, Val env) {
    if (IS_NIL(lst)) return NIL_VAL;

    PUSH_ROOT(lst);    /* [top-4] */
    PUSH_ROOT(env);    /* [top-3] */
    Val result = NIL_VAL;
    PUSH_ROOT(result); /* [top-2] */
    Val tail   = NIL_VAL;
    PUSH_ROOT(tail);   /* [top-1] */

    Val cur = lst; /* stable index */
    while (IS_CONS(cur)) {
        Val elem = eval(CAR(cur), g_gc_roots[g_gc_root_top - 3]); /* env */
        PUSH_ROOT(elem);
        Val cell = pl_cons(elem, NIL_VAL);
        POP_ROOT(); /* elem */

        if (IS_NIL(g_gc_roots[g_gc_root_top - 2])) { /* result == NIL */
            g_gc_roots[g_gc_root_top - 2] = cell; /* result */
            g_gc_roots[g_gc_root_top - 1] = cell; /* tail */
        } else {
            g_cells[CONS_IDX(g_gc_roots[g_gc_root_top - 1])].cdr = cell;
            g_gc_roots[g_gc_root_top - 1] = cell; /* advance tail */
        }
        cur = CDR(cur);
    }

    Val res = g_gc_roots[g_gc_root_top - 2];
    POP_ROOT(); /* tail */
    POP_ROOT(); /* result */
    POP_ROOT(); /* env */
    POP_ROOT(); /* lst */
    return res;
}

/* =========================================================================
 * eval_body -- evaluate body with TCO on the last form (public API).
 * ===================================================================== */
Val eval_body(Val body, Val env) {
    if (IS_NIL(body)) return NIL_VAL;
    while (IS_CONS(CDR(body))) {
        eval(CAR(body), env);
        body = CDR(body);
    }
    return eval(CAR(body), env);
}

/* =========================================================================
 * pl_equal -- structural equality.
 * ===================================================================== */
int pl_equal(Val a, Val b) {
    if (a == b) return 1;
    if (VAL_TAG(a) != VAL_TAG(b)) return 0;
    if (IS_INT(a)) return INT_VAL(a) == INT_VAL(b);
    if (IS_STR(a)) {
        StrSlot *sa = &g_strs[STR_IDX(a)];
        StrSlot *sb = &g_strs[STR_IDX(b)];
        return sa->len == sb->len && memcmp(sa->ptr, sb->ptr, sa->len) == 0;
    }
    if (IS_CONS(a))
        return pl_equal(CAR(a), CAR(b)) && pl_equal(CDR(a), CDR(b));
    if (IS_BIG(a))
        return big_cmp(a, b) == 0;
    return 0;
}

/* =========================================================================
 * pl_apply -- apply an already-evaluated function to an already-evaluated
 * argument list.  Used by the 'apply' primitive.
 * ===================================================================== */
Val pl_apply(Val fn, Val args, Val env) {
    PUSH_ROOT(fn);
    PUSH_ROOT(args);

    if (IS_PRIM(fn)) {
        POP_ROOT(); POP_ROOT();
        return g_prims[PRIM_IDX(fn)].fn(args, env);
    }

    if (IS_CONS(fn)) {
        Val fn_tag = CAR(fn);
        if (IS_SYM(fn_tag)) {
            uint32_t ftag = SYM_IDX(fn_tag);

            if (ftag == g_sym_lambda) {
                Val cap_env = CADR(fn);
                Val params  = CADDR(fn);
                Val body    = CDDDR(fn);
                Val new_env = bind_params_env(params, args, cap_env);
                POP_ROOT(); POP_ROOT();
                PUSH_ROOT(new_env);
                Val result = eval_body(body, new_env);
                POP_ROOT();
                return result;
            }

            if (ftag == g_sym_de || ftag == g_sym_dm) {
                Val params = CADR(fn);
                Val body   = CDDR(fn);
                int mark   = bind_params_dynamic(params, args);
                Val de_env = bind_params_env(params, args, NIL_VAL);
                POP_ROOT(); POP_ROOT();
                PUSH_ROOT(de_env);
                Val result = eval_body_non_tco(body, de_env);
                POP_ROOT();
                dynamic_unbind_to(mark);
                return result;
            }

            if (ftag == sym_parameter()) {
                POP_ROOT(); POP_ROOT();
                if (IS_NIL(args)) return CDR(fn);
                g_cells[CONS_IDX(fn)].cdr = CAR(args);
                return CAR(args);
            }
        }
    }

    POP_ROOT(); POP_ROOT();
    pl_error("not a function", fn);
    return NIL_VAL;
}

/* =========================================================================
 * Quasiquote expansion (eval-time, depth-tracked)
 *
 * depth 0: unquote → eval; unquote-splicing → splice into list.
 * depth > 0: decrement depth for unquote/splicing, increment for quasiquote.
 * ===================================================================== */
static Val expand_quasiquote(Val form, Val env, int depth) {
    if (!IS_CONS(form))
        return form; /* atoms are self-quoting in quasi context */

    Val head = CAR(form);

    /* (unquote x) */
    if (IS_SYM(head) && SYM_IDX(head) == g_sym_unquote) {
        Val x = IS_CONS(CDR(form)) ? CAR(CDR(form)) : NIL_VAL;
        if (depth == 0)
            return eval(x, env);
        PUSH_ROOT(env);
        Val inner = expand_quasiquote(x, env, depth - 1);
        PUSH_ROOT(inner);
        Val pair = pl_cons(inner, NIL_VAL);
        POP_ROOT(); /* inner */
        PUSH_ROOT(pair);
        Val result = pl_cons(MAKE_SYM(g_sym_unquote), pair);
        POP_ROOT(); POP_ROOT(); /* pair, env */
        return result;
    }

    /* (quasiquote x) -- nested, depth + 1 */
    if (IS_SYM(head) && SYM_IDX(head) == g_sym_quasiquote) {
        Val x = IS_CONS(CDR(form)) ? CAR(CDR(form)) : NIL_VAL;
        PUSH_ROOT(env);
        Val inner = expand_quasiquote(x, env, depth + 1);
        PUSH_ROOT(inner);
        Val pair = pl_cons(inner, NIL_VAL);
        POP_ROOT();
        PUSH_ROOT(pair);
        Val result = pl_cons(MAKE_SYM(g_sym_quasiquote), pair);
        POP_ROOT(); POP_ROOT();
        return result;
    }

    /* List: walk elements, handling unquote-splicing by splicing. */
    PUSH_ROOT(env);
    PUSH_ROOT(form);
    Val result = NIL_VAL;
    PUSH_ROOT(result); /* [top-3] result head */
    Val tail   = NIL_VAL;
    PUSH_ROOT(tail);   /* [top-4] result tail; NOTE: below env/form on stack */
    /* Stack layout (top = highest index):
     *   [top-4] = env
     *   [top-3] = form
     *   [top-2] = result
     *   [top-1] = tail
     */

    Val cur = form; /* stable index */
    while (IS_CONS(cur)) {
        Val elem  = CAR(cur);
        Val e_env = g_gc_roots[g_gc_root_top - 4]; /* env */

        if (IS_CONS(elem) && IS_SYM(CAR(elem)) &&
            SYM_IDX(CAR(elem)) == g_sym_unquote_splicing) {
            Val splice_expr = IS_CONS(CDR(elem)) ? CAR(CDR(elem)) : NIL_VAL;
            Val spliced;
            if (depth == 0) {
                spliced = eval(splice_expr, e_env);
            } else {
                PUSH_ROOT(splice_expr);
                spliced = expand_quasiquote(splice_expr, e_env, depth - 1);
                POP_ROOT();
            }
            PUSH_ROOT(spliced);
            Val sp = spliced;
            while (IS_CONS(sp)) {
                PUSH_ROOT(sp);
                Val cell = pl_cons(CAR(sp), NIL_VAL);
                POP_ROOT(); /* sp */
                PUSH_ROOT(cell);
                if (IS_NIL(g_gc_roots[g_gc_root_top - 4])) { /* result */
                    g_gc_roots[g_gc_root_top - 4] = cell;
                    g_gc_roots[g_gc_root_top - 3] = cell; /* tail */
                } else {
                    g_cells[CONS_IDX(g_gc_roots[g_gc_root_top - 3])].cdr = cell;
                    g_gc_roots[g_gc_root_top - 3] = cell;
                }
                POP_ROOT(); /* cell */
                sp = CDR(sp);
            }
            POP_ROOT(); /* spliced */
        } else {
            Val expanded = expand_quasiquote(elem, e_env, depth);
            PUSH_ROOT(expanded);
            Val cell = pl_cons(expanded, NIL_VAL);
            POP_ROOT();
            PUSH_ROOT(cell);
            if (IS_NIL(g_gc_roots[g_gc_root_top - 3])) { /* result */
                g_gc_roots[g_gc_root_top - 3] = cell;
                g_gc_roots[g_gc_root_top - 2] = cell; /* tail */
            } else {
                g_cells[CONS_IDX(g_gc_roots[g_gc_root_top - 2])].cdr = cell;
                g_gc_roots[g_gc_root_top - 2] = cell;
            }
            POP_ROOT(); /* cell */
        }
        cur = CDR(cur);
    }

    /* Handle improper list tail. */
    if (!IS_NIL(cur)) {
        Val e_env  = g_gc_roots[g_gc_root_top - 4];
        Val et     = expand_quasiquote(cur, e_env, depth);
        if (!IS_NIL(g_gc_roots[g_gc_root_top - 2])) /* tail */
            g_cells[CONS_IDX(g_gc_roots[g_gc_root_top - 2])].cdr = et;
        else
            g_gc_roots[g_gc_root_top - 3] = et; /* result */
    }

    Val res = g_gc_roots[g_gc_root_top - 2]; /* result */
    POP_ROOT(); /* tail   [top-1] */
    POP_ROOT(); /* result [top-2] */
    POP_ROOT(); /* form   [top-3] */
    POP_ROOT(); /* env    [top-4] */
    return res;
}

/* =========================================================================
 * parameterize implementation
 * (parameterize ((p1 v1) ...) . body)
 *
 * Each pi must be a parameter object: (parameter . current-val).
 * Saves old values, sets new ones, evaluates body, restores.
 * ===================================================================== */
#define MAX_PARAM_BINDINGS 64

static Val eval_parameterize(Val bindings, Val body, Val env) {
    Val  saved[MAX_PARAM_BINDINGS];
    Val  objs [MAX_PARAM_BINDINGS];
    int  n = 0;

    PUSH_ROOT(body);
    PUSH_ROOT(env);

    Val b = bindings;
    while (IS_CONS(b) && n < MAX_PARAM_BINDINGS) {
        Val pair = CAR(b);
        Val po   = eval(IS_CONS(pair) ? CAR(pair) : NIL_VAL,
                        g_gc_roots[g_gc_root_top - 1]); /* env */
        Val nv   = eval(IS_CONS(CDR(pair)) ? CADR(pair) : NIL_VAL,
                        g_gc_roots[g_gc_root_top - 1]);
        if (IS_CONS(po) && IS_SYM(CAR(po)) &&
            SYM_IDX(CAR(po)) == sym_parameter()) {
            saved[n] = CDR(po);
            objs [n] = po;
            g_cells[CONS_IDX(po)].cdr = nv;
            n++;
        }
        b = CDR(b);
    }

    Val result = eval_body_non_tco(g_gc_roots[g_gc_root_top - 2], /* body */
                                   g_gc_roots[g_gc_root_top - 1]  /* env  */);
    /* Restore in reverse. */
    for (int i = n - 1; i >= 0; i--)
        g_cells[CONS_IDX(objs[i])].cdr = saved[i];

    POP_ROOT(); /* env */
    POP_ROOT(); /* body */
    return result;
}

/* =========================================================================
 * make-parameter primitive (registered during eval_init)
 * ===================================================================== */
static Val prim_make_parameter(Val args, Val env) {
    (void)env;
    Val init = IS_CONS(args) ? CAR(args) : NIL_VAL;
    PUSH_ROOT(init);
    Val po = pl_cons(MAKE_SYM(sym_parameter()), init);
    POP_ROOT();
    return po;
}

/* =========================================================================
 * Main evaluator -- TCO trampoline
 * ===================================================================== */
/* Macro for returning from eval: must pop the env root pushed at function entry. */
#define EVAL_RETURN(v) do { Val _eval_ret_ = (v); POP_ROOT(); return _eval_ret_; } while(0)

Val eval(Val expr, Val env) {
    Val cur_expr = expr;
    Val cur_env  = env;
    PUSH_ROOT(cur_env);
    int env_slot = g_gc_root_top - 1;

tail_call:
    g_gc_roots[env_slot] = cur_env;

    /* Self-evaluating types */
    if (IS_NIL(cur_expr)  || IS_INT(cur_expr)  || IS_STR(cur_expr) ||
        IS_BIG(cur_expr)  || IS_FLOAT(cur_expr) || IS_VEC(cur_expr) || IS_PRIM(cur_expr))
        EVAL_RETURN(cur_expr);

    /* Symbol lookup */
    if (IS_SYM(cur_expr))
        EVAL_RETURN(env_lookup(cur_expr, cur_env));

    if (!IS_CONS(cur_expr)) EVAL_RETURN(cur_expr);

    Val head = CAR(cur_expr);
    Val rest = CDR(cur_expr);

    /* ==================================================================
     * Special form dispatch
     * ================================================================== */
    if (IS_SYM(head)) {
        uint32_t sidx = SYM_IDX(head);

        /* ----------------------------------------------------------------
         * quote
         * -------------------------------------------------------------- */
        if (sidx == g_sym_quote)
            EVAL_RETURN(IS_CONS(rest) ? CAR(rest) : NIL_VAL);

        /* ----------------------------------------------------------------
         * if
         * -------------------------------------------------------------- */
        if (sidx == g_sym_if) {
            Val cond_val = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            g_at = cond_val;
            if (IS_NIL(cond_val))
                cur_expr = IS_CONS(CDDR(rest)) ? CAR(CDDR(rest)) : NIL_VAL;
            else
                cur_expr = IS_CONS(CDR(rest))  ? CAR(CDR(rest))  : NIL_VAL;
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * when
         * -------------------------------------------------------------- */
        if (sidx == g_sym_when) {
            Val cv = eval(CAR(rest), cur_env);
            g_at = cv;
            if (IS_NIL(cv)) EVAL_RETURN(NIL_VAL);
            Val body = CDR(rest);
            if (IS_NIL(body)) EVAL_RETURN(cv);
            cur_expr = make_prog_expr(body);
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * unless
         * -------------------------------------------------------------- */
        if (sidx == g_sym_unless) {
            Val cv = eval(CAR(rest), cur_env);
            g_at = cv;
            if (!IS_NIL(cv)) EVAL_RETURN(NIL_VAL);
            Val body = CDR(rest);
            if (IS_NIL(body)) EVAL_RETURN(NIL_VAL);
            cur_expr = make_prog_expr(body);
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * cond
         * -------------------------------------------------------------- */
        if (sidx == g_sym_cond) {
            Val clauses = rest;
            while (IS_CONS(clauses)) {
                Val clause = CAR(clauses);
                if (!IS_CONS(clause)) { clauses = CDR(clauses); continue; }
                Val test = CAR(clause);
                Val body = CDR(clause);
                Val cv;
                if (IS_SYM(test) && SYM_IDX(test) == g_sym_T)
                    cv = T_VAL;
                else
                    cv = eval(test, cur_env);
                g_at = cv;
                if (!IS_NIL(cv)) {
                    if (IS_NIL(body)) EVAL_RETURN(cv);
                    cur_expr = make_prog_expr(body);
                    goto tail_call;
                }
                clauses = CDR(clauses);
            }
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * and
         * -------------------------------------------------------------- */
        if (sidx == g_sym_and) {
            if (IS_NIL(rest)) EVAL_RETURN(T_VAL);
            Val exprs = rest;
            while (IS_CONS(exprs)) {
                if (IS_NIL(CDR(exprs))) {
                    cur_expr = CAR(exprs);
                    goto tail_call;
                }
                Val v = eval(CAR(exprs), cur_env);
                if (IS_NIL(v)) EVAL_RETURN(NIL_VAL);
                exprs = CDR(exprs);
            }
            EVAL_RETURN(T_VAL);
        }

        /* ----------------------------------------------------------------
         * or
         * -------------------------------------------------------------- */
        if (sidx == g_sym_or) {
            if (IS_NIL(rest)) EVAL_RETURN(NIL_VAL);
            Val exprs = rest;
            while (IS_CONS(exprs)) {
                if (IS_NIL(CDR(exprs))) {
                    cur_expr = CAR(exprs);
                    goto tail_call;
                }
                Val v = eval(CAR(exprs), cur_env);
                if (!IS_NIL(v)) EVAL_RETURN(v);
                exprs = CDR(exprs);
            }
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * prog -- sequence, return last (TCO)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_prog) {
            if (IS_NIL(rest)) EVAL_RETURN(NIL_VAL);
            Val cur = rest;
            while (IS_CONS(CDR(cur))) {
                eval(CAR(cur), cur_env);
                cur = CDR(cur);
            }
            cur_expr = CAR(cur);
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * prog1 -- evaluate all, return first
         * -------------------------------------------------------------- */
        if (sidx == g_sym_prog1) {
            Val result = eval(CAR(rest), cur_env);
            PUSH_ROOT(result);
            Val cur = CDR(rest);
            while (IS_CONS(cur)) { eval(CAR(cur), cur_env); cur = CDR(cur); }
            POP_ROOT();
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * prog2 -- evaluate all, return second
         * -------------------------------------------------------------- */
        if (sidx == g_sym_prog2) {
            eval(CAR(rest), cur_env);
            Val result = eval(CADR(rest), cur_env);
            PUSH_ROOT(result);
            Val cur = CDDR(rest);
            while (IS_CONS(cur)) { eval(CAR(cur), cur_env); cur = CDR(cur); }
            POP_ROOT();
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * while
         * -------------------------------------------------------------- */
        if (sidx == g_sym_while) {
            Val test_expr = CAR(rest);
            Val body      = CDR(rest);
            Val result    = NIL_VAL;
            while (1) {
                Val cv = eval(test_expr, cur_env);
                if (IS_NIL(cv)) break;
                g_at   = cv;
                result = eval_body_non_tco(body, cur_env);
            }
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * loop -- infinite loop; use (throw tag val) to break out
         * -------------------------------------------------------------- */
        if (sidx == g_sym_loop) {
            for (;;)
                eval_body_non_tco(rest, cur_env);
            /* unreachable */
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * for  (for (var init step) test . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_for) {
            Val spec      = CAR(rest);
            Val var_sym   = IS_CONS(spec)        ? CAR(spec)   : NIL_VAL;
            Val init_expr = IS_CONS(CDR(spec))   ? CADR(spec)  : NIL_VAL;
            Val step_expr = IS_CONS(CDDR(spec))  ? CADDR(spec) : NIL_VAL;
            Val test_expr = IS_CONS(CDR(rest))   ? CADR(rest)  : NIL_VAL;
            Val body      = IS_CONS(CDR(rest))   ? CDDR(rest)  : NIL_VAL;

            if (!IS_SYM(var_sym))
                pl_error("for: loop variable must be a symbol", var_sym);

            Val init_val = eval(init_expr, cur_env);
            int mark     = dynamic_bind(SYM_IDX(var_sym), init_val);
            Val result   = NIL_VAL;

            while (1) {
                Val cv = eval(test_expr, cur_env);
                if (IS_NIL(cv)) break;
                g_at   = cv;
                result = eval_body_non_tco(body, cur_env);
                Val step_val = eval(step_expr, cur_env);
                g_syms[SYM_IDX(var_sym)].value = step_val;
            }
            dynamic_unbind_to(mark);
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * do  (do ((var init step) ...) (test . result) . body)
         *
         * Scheme-style do: init all vars, then loop until test passes,
         * stepping all vars simultaneously each iteration.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_do) {
            Val var_specs  = IS_CONS(rest)        ? CAR(rest)   : NIL_VAL;
            Val term_spec  = IS_CONS(CDR(rest))   ? CADR(rest)  : NIL_VAL;
            Val loop_body  = IS_CONS(CDR(rest))   ? CDDR(rest)  : NIL_VAL;
            Val test_expr  = IS_CONS(term_spec)   ? CAR(term_spec)  : NIL_VAL;
            Val result_seq = IS_CONS(term_spec)   ? CDR(term_spec)  : NIL_VAL;

            PUSH_ROOT(cur_env);
            PUSH_ROOT(var_specs);
            PUSH_ROOT(loop_body);
            PUSH_ROOT(result_seq);
            PUSH_ROOT(test_expr);

            /* Initialize all vars simultaneously in cur_env. */
            int mark = g_bind_top;
            {
                Val vs = var_specs;
                while (IS_CONS(vs)) {
                    Val spec = CAR(vs);
                    Val var  = IS_CONS(spec)      ? CAR(spec)  : NIL_VAL;
                    Val ie   = IS_CONS(CDR(spec)) ? CADR(spec) : NIL_VAL;
                    Val iv   = eval(ie, cur_env);
                    if (IS_SYM(var)) dynamic_bind(SYM_IDX(var), iv);
                    vs = CDR(vs);
                }
            }

            Val result = NIL_VAL;
            for (;;) {
                Val tv = eval(g_gc_roots[g_gc_root_top - 1] /* test_expr */, cur_env);
                g_at = tv;
                if (!IS_NIL(tv)) {
                    result = eval_body_non_tco(g_gc_roots[g_gc_root_top - 2] /* result_seq */,
                                               cur_env);
                    break;
                }
                eval_body_non_tco(g_gc_roots[g_gc_root_top - 3] /* loop_body */, cur_env);

                /* Count vars to allocate step-value buffer on GC root stack. */
                int nvars = 0;
                { Val vs = g_gc_roots[g_gc_root_top - 4]; /* var_specs */
                  while (IS_CONS(vs)) { nvars++; vs = CDR(vs); } }

                /* Evaluate all step exprs in current env (simultaneous). */
                {
                    Val vs = g_gc_roots[g_gc_root_top - 4];
                    for (int vi = 0; vi < nvars; vi++) {
                        Val spec = CAR(vs);
                        /* If no step expr, use the var itself (identity step). */
                        Val se   = IS_CONS(CDDR(spec)) ? CADDR(spec) : CAR(spec);
                        Val sv   = eval(se, cur_env);
                        PUSH_ROOT(sv);
                        vs = CDR(vs);
                    }
                }
                /* Assign new values. */
                {
                    int base = g_gc_root_top - nvars;
                    Val vs   = g_gc_roots[g_gc_root_top - 4 - nvars]; /* var_specs */
                    for (int vi = 0; vi < nvars; vi++) {
                        Val spec = CAR(vs);
                        Val var  = IS_CONS(spec) ? CAR(spec) : NIL_VAL;
                        if (IS_SYM(var))
                            g_syms[SYM_IDX(var)].value = g_gc_roots[base + vi];
                        vs = CDR(vs);
                    }
                    g_gc_root_top -= nvars;
                }
            }

            dynamic_unbind_to(mark);
            POP_ROOT(); /* test_expr */
            POP_ROOT(); /* result_seq */
            POP_ROOT(); /* loop_body */
            POP_ROOT(); /* var_specs */
            POP_ROOT(); /* cur_env */
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * let  (let ((var val) ...) . body)  -- parallel binding, lexical
         * -------------------------------------------------------------- */
        if (sidx == g_sym_let) {
            Val bindings  = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body      = IS_CONS(rest) ? CDR(rest) : NIL_VAL;

            Val new_env   = cur_env;
            Val orig_env  = cur_env;
            PUSH_ROOT(new_env);   /* [top-2] -- being built */
            PUSH_ROOT(orig_env);  /* [top-1] -- stays fixed for RHS eval */

            Val b = bindings;
            while (IS_CONS(b)) {
                Val pair  = CAR(b);
                Val sym_v = IS_CONS(pair)      ? CAR(pair)  : NIL_VAL;
                Val rhs_e = IS_CONS(CDR(pair)) ? CADR(pair) : NIL_VAL;
                Val rhs   = eval(rhs_e, g_gc_roots[g_gc_root_top - 1]); /* orig_env */
                if (IS_CONS(sym_v)) {
                    /* Destructuring bind: sym_v is a pattern */
                    Val ne = bind_params_env(sym_v, rhs,
                                 g_gc_roots[g_gc_root_top - 2]);
                    g_gc_roots[g_gc_root_top - 2] = ne;
                } else {
                    PUSH_ROOT(rhs);
                    Val binding = pl_cons(sym_v, rhs);
                    POP_ROOT(); /* rhs */
                    PUSH_ROOT(binding);
                    Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 3]);
                    POP_ROOT(); /* binding */
                    g_gc_roots[g_gc_root_top - 2] = ne;
                }
                b = CDR(b);
            }

            cur_env = g_gc_roots[g_gc_root_top - 2];
            POP_ROOT(); /* orig_env */
            /* keep new_env on root stack to protect cur_env during body eval */

            if (IS_NIL(body)) { POP_ROOT(); EVAL_RETURN(NIL_VAL); }
            while (IS_CONS(CDR(body))) { eval(CAR(body), cur_env); body = CDR(body); }
            cur_expr = CAR(body);
            POP_ROOT(); /* new_env */
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * let*  (let* ((var val) ...) . body)  -- sequential, lexical
         * -------------------------------------------------------------- */
        if (sidx == g_sym_letstar) {
            Val bindings = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body     = IS_CONS(rest) ? CDR(rest) : NIL_VAL;

            Val new_env  = cur_env;
            PUSH_ROOT(new_env);

            Val b = bindings;
            while (IS_CONS(b)) {
                Val pair  = CAR(b);
                Val sym_v = IS_CONS(pair)      ? CAR(pair)  : NIL_VAL;
                Val rhs_e = IS_CONS(CDR(pair)) ? CADR(pair) : NIL_VAL;
                Val rhs   = eval(rhs_e, g_gc_roots[g_gc_root_top - 1]); /* new_env */
                if (IS_CONS(sym_v)) {
                    Val ne = bind_params_env(sym_v, rhs,
                                 g_gc_roots[g_gc_root_top - 1]);
                    g_gc_roots[g_gc_root_top - 1] = ne;
                } else {
                    PUSH_ROOT(rhs);
                    Val binding = pl_cons(sym_v, rhs);
                    POP_ROOT();
                    PUSH_ROOT(binding);
                    Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 2]);
                    POP_ROOT();
                    g_gc_roots[g_gc_root_top - 1] = ne;
                }
                b = CDR(b);
            }

            cur_env = g_gc_roots[g_gc_root_top - 1];
            /* keep new_env on root stack to protect cur_env during body eval */

            if (IS_NIL(body)) { POP_ROOT(); EVAL_RETURN(NIL_VAL); }
            while (IS_CONS(CDR(body))) { eval(CAR(body), cur_env); body = CDR(body); }
            cur_expr = CAR(body);
            POP_ROOT(); /* new_env */
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * letrec  (letrec ((var val) ...) . body)
         *
         * All variables are in scope for all initializers; bound to NIL
         * first (parallel semantics), then each is mutated to its value.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_letrec) {
            Val bindings = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body     = IS_CONS(rest) ? CDR(rest) : NIL_VAL;

            /* Step 1: extend env with all vars → NIL. */
            Val lr_env = cur_env;
            PUSH_ROOT(lr_env);
            PUSH_ROOT(bindings); /* keep stable ref */

            {
                Val b = bindings;
                while (IS_CONS(b)) {
                    Val pair  = CAR(b);
                    Val sym_v = IS_CONS(pair) ? CAR(pair) : NIL_VAL;
                    Val binding = pl_cons(sym_v, NIL_VAL);
                    PUSH_ROOT(binding);
                    Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 3]); /* lr_env */
                    POP_ROOT();
                    g_gc_roots[g_gc_root_top - 2] = ne; /* update lr_env */
                    b = CDR(b);
                }
            }

            Val full_env = g_gc_roots[g_gc_root_top - 2];

            /* Step 2: evaluate each RHS in full_env and mutate binding CDRs. */
            {
                Val b = g_gc_roots[g_gc_root_top - 1]; /* bindings */
                while (IS_CONS(b)) {
                    Val pair  = CAR(b);
                    Val sym_v = IS_CONS(pair)      ? CAR(pair)  : NIL_VAL;
                    Val rhs_e = IS_CONS(CDR(pair)) ? CADR(pair) : NIL_VAL;
                    Val rhs   = eval(rhs_e, full_env);
                    /* Find sym_v's binding in full_env and set its CDR. */
                    if (IS_SYM(sym_v)) {
                        uint32_t target = SYM_IDX(sym_v);
                        Val e = full_env;
                        while (IS_CONS(e)) {
                            Val bnd = CAR(e);
                            if (IS_CONS(bnd) && IS_SYM(CAR(bnd)) &&
                                SYM_IDX(CAR(bnd)) == target) {
                                g_cells[CONS_IDX(bnd)].cdr = rhs;
                                break;
                            }
                            e = CDR(e);
                        }
                    }
                    b = CDR(b);
                }
            }

            cur_env = full_env;
            POP_ROOT(); /* bindings */
            /* keep lr_env root to protect cur_env during body eval */

            if (IS_NIL(body)) { POP_ROOT(); EVAL_RETURN(NIL_VAL); }
            while (IS_CONS(CDR(body))) { eval(CAR(body), cur_env); body = CDR(body); }
            cur_expr = CAR(body);
            POP_ROOT(); /* lr_env */
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * letrec*  (letrec* ((var val) ...) . body)
         *
         * Sequential: each RHS evaluated after previous bindings are set.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_letrecstar) {
            Val bindings = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body     = IS_CONS(rest) ? CDR(rest) : NIL_VAL;

            Val lrs_env = cur_env;
            PUSH_ROOT(lrs_env);

            Val b = bindings;
            while (IS_CONS(b)) {
                Val pair  = CAR(b);
                Val sym_v = IS_CONS(pair)      ? CAR(pair)  : NIL_VAL;
                Val rhs_e = IS_CONS(CDR(pair)) ? CADR(pair) : NIL_VAL;
                /* Add binding with NIL placeholder first. */
                Val binding = pl_cons(sym_v, NIL_VAL);
                PUSH_ROOT(binding);
                Val ne = pl_cons(binding, g_gc_roots[g_gc_root_top - 2]); /* lrs_env */
                POP_ROOT(); /* binding */
                g_gc_roots[g_gc_root_top - 1] = ne; /* update lrs_env */
                /* Now evaluate RHS in the updated env (sequential visibility). */
                Val rhs = eval(rhs_e, ne);
                /* Mutate the binding we just pushed (it's CAR(ne)). */
                g_cells[CONS_IDX(CAR(ne))].cdr = rhs;
                b = CDR(b);
            }

            cur_env = g_gc_roots[g_gc_root_top - 1];
            /* keep lrs_env root to protect cur_env during body eval */

            if (IS_NIL(body)) { POP_ROOT(); EVAL_RETURN(NIL_VAL); }
            while (IS_CONS(CDR(body))) { eval(CAR(body), cur_env); body = CDR(body); }
            cur_expr = CAR(body);
            POP_ROOT(); /* lrs_env */
            goto tail_call;
        }

        /* ----------------------------------------------------------------
         * setq  (setq sym val)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_setq) {
            Val sym  = IS_CONS(rest)      ? CAR(rest)  : NIL_VAL;
            Val vexpr = IS_CONS(CDR(rest)) ? CADR(rest) : NIL_VAL;
            Val val  = eval(vexpr, cur_env);
            if (IS_SYM(sym)) {
                uint32_t sym_idx = SYM_IDX(sym);
                /* Update lexical env binding if present (so setq works in let). */
                Val e = cur_env;
                bool found_lexical = false;
                while (IS_CONS(e)) {
                    Val binding = CAR(e);
                    if (IS_CONS(binding) && IS_SYM(CAR(binding)) &&
                        SYM_IDX(CAR(binding)) == sym_idx) {
                        g_cells[CONS_IDX(binding)].cdr = val;
                        found_lexical = true;
                        break;
                    }
                    e = CDR(e);
                }
                /* Also update global dynamic binding. */
                if (!found_lexical)
                    g_syms[sym_idx].value = val;
            }
            EVAL_RETURN(val);
        }

        /* ----------------------------------------------------------------
         * de / dm  (de name (params...) . body)
         *
         * Define a dynamic-scoped (PicoLisp-style) function.
         * Closure format: (de params . body)  or  (dm params . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_de || sidx == g_sym_dm) {
            Val name   = IS_CONS(rest)       ? CAR(rest)  : NIL_VAL;
            Val params = IS_CONS(CDR(rest))  ? CADR(rest) : NIL_VAL;
            Val body   = IS_CONS(CDR(rest))  ? CDDR(rest) : NIL_VAL;
            PUSH_ROOT(params);
            PUSH_ROOT(body);
            Val pb      = pl_cons(params, body);
            POP_ROOT(); POP_ROOT();
            PUSH_ROOT(pb);
            Val closure = pl_cons(head, pb);
            POP_ROOT();
            PUSH_ROOT(closure);
            if (IS_SYM(name))
                g_syms[SYM_IDX(name)].value = closure;
            POP_ROOT();
            EVAL_RETURN(closure);
        }

        /* ----------------------------------------------------------------
         * dmacro  (dmacro name (params...) . body)
         * Macro format: (macro NIL params . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_dmacro) {
            Val name   = IS_CONS(rest)      ? CAR(rest)  : NIL_VAL;
            Val params = IS_CONS(CDR(rest)) ? CADR(rest) : NIL_VAL;
            Val body   = IS_CONS(CDR(rest)) ? CDDR(rest) : NIL_VAL;
            PUSH_ROOT(params);
            PUSH_ROOT(body);
            Val pb = pl_cons(params, body);
            POP_ROOT(); POP_ROOT();
            PUSH_ROOT(pb);
            Val nil_pb = pl_cons(NIL_VAL, pb);
            POP_ROOT();
            PUSH_ROOT(nil_pb);
            Val mac = pl_cons(MAKE_SYM(g_sym_macro), nil_pb);
            POP_ROOT();
            PUSH_ROOT(mac);
            if (IS_SYM(name))
                g_syms[SYM_IDX(name)].value = mac;
            POP_ROOT();
            EVAL_RETURN(mac);
        }

        /* ----------------------------------------------------------------
         * lambda  (lambda params . body)
         * Closure format: (lambda captured-env params . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_lambda) {
            Val params = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body   = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            PUSH_ROOT(cur_env);
            PUSH_ROOT(params);
            PUSH_ROOT(body);
            Val pb  = pl_cons(params, body);
            POP_ROOT(); POP_ROOT(); /* body, params */
            PUSH_ROOT(pb);
            Val epb = pl_cons(g_gc_roots[g_gc_root_top - 2] /* cur_env */, pb);
            POP_ROOT(); /* pb */
            PUSH_ROOT(epb);
            Val closure = pl_cons(MAKE_SYM(g_sym_lambda), epb);
            POP_ROOT(); /* epb */
            POP_ROOT(); /* cur_env */
            EVAL_RETURN(closure);
        }

        /* ----------------------------------------------------------------
         * macro  (macro params . body)
         * Anonymous macro; format: (macro NIL params . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_macro) {
            Val params = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body   = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            PUSH_ROOT(params);
            PUSH_ROOT(body);
            Val pb     = pl_cons(params, body);
            POP_ROOT(); POP_ROOT();
            PUSH_ROOT(pb);
            Val nil_pb = pl_cons(NIL_VAL, pb);
            POP_ROOT();
            PUSH_ROOT(nil_pb);
            Val mac    = pl_cons(MAKE_SYM(g_sym_macro), nil_pb);
            POP_ROOT();
            EVAL_RETURN(mac);
        }

        /* ----------------------------------------------------------------
         * case  (case key-expr clause ...)
         * clause: (val . body) or ((v1 v2 ...) . body) or (T . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_case) {
            Val key = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            PUSH_ROOT(key);
            Val clauses = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            while (IS_CONS(clauses)) {
                Val clause     = CAR(clauses);
                Val clause_key = IS_CONS(clause) ? CAR(clause) : NIL_VAL;
                Val clause_bod = IS_CONS(clause) ? CDR(clause) : NIL_VAL;
                int matches    = 0;
                if (IS_SYM(clause_key) && SYM_IDX(clause_key) == g_sym_T) {
                    matches = 1;
                } else if (IS_CONS(clause_key)) {
                    /* list of alternatives */
                    Val alts = clause_key;
                    Val k    = g_gc_roots[g_gc_root_top - 1]; /* key */
                    while (IS_CONS(alts)) {
                        if (pl_equal(k, CAR(alts))) { matches = 1; break; }
                        alts = CDR(alts);
                    }
                } else {
                    matches = pl_equal(g_gc_roots[g_gc_root_top - 1], clause_key);
                }
                if (matches) {
                    POP_ROOT(); /* key */
                    if (IS_NIL(clause_bod)) EVAL_RETURN(NIL_VAL);
                    cur_expr = make_prog_expr(clause_bod);
                    goto tail_call;
                }
                clauses = CDR(clauses);
            }
            POP_ROOT(); /* key */
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * catch  (catch tag-expr . body)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_catch) {
            if (g_catch_top >= CATCH_STACK_SIZE)
                pl_error_str("catch stack overflow");

            Val tag = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            PUSH_ROOT(tag);

            int idx = g_catch_top++;
            g_catch_stack[idx].tag          = tag;
            g_catch_stack[idx].bind_mark    = g_bind_top;
            g_catch_stack[idx].gc_root_mark = g_gc_root_top;

            if (setjmp(g_catch_stack[idx].jb) == 0) {
                Val result = eval_body_non_tco(IS_CONS(rest) ? CDR(rest) : NIL_VAL,
                                               cur_env);
                g_catch_top--;
                POP_ROOT(); /* tag */
                EVAL_RETURN(result);
            } else {
                /* longjmp landed here; bind/gc_root stacks were restored. */
                g_catch_top = idx;
                return g_catch_stack[idx].thrown_val;
            }
        }

        /* ----------------------------------------------------------------
         * throw  (throw tag value)
         * -------------------------------------------------------------- */
        if (sidx == g_sym_throw) {
            Val tag = eval(IS_CONS(rest)      ? CAR(rest)  : NIL_VAL, cur_env);
            Val val = eval(IS_CONS(CDR(rest)) ? CADR(rest) : NIL_VAL, cur_env);
            for (int i = g_catch_top - 1; i >= 0; i--) {
                if (pl_equal(g_catch_stack[i].tag, tag)) {
                    g_catch_stack[i].thrown_val = val;
                    g_bind_top    = g_catch_stack[i].bind_mark;
                    g_gc_root_top = g_catch_stack[i].gc_root_mark;
                    g_catch_top   = i + 1;
                    longjmp(g_catch_stack[i].jb, 1);
                }
            }
            pl_error("uncaught throw", tag);
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * guard  (guard (var clause ...) . body)
         *
         * Simplified SRFI-34: catches any exception thrown within body,
         * binds it to var, evaluates handler clauses (cond-style);
         * re-raises if none match.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_guard) {
            if (g_catch_top >= CATCH_STACK_SIZE)
                pl_error_str("catch stack overflow");

            Val spec     = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val guard_body = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            Val guard_var  = IS_CONS(spec) ? CAR(spec)  : NIL_VAL;
            Val guard_cls  = IS_CONS(spec) ? CDR(spec)  : NIL_VAL;

            /* Use the spec cell as a unique catch tag. */
            Val guard_tag = spec;
            PUSH_ROOT(guard_tag);
            PUSH_ROOT(guard_cls);
            PUSH_ROOT(guard_var);
            PUSH_ROOT(guard_body);
            PUSH_ROOT(cur_env);

            int idx = g_catch_top++;
            g_catch_stack[idx].tag          = guard_tag;
            g_catch_stack[idx].bind_mark    = g_bind_top;
            g_catch_stack[idx].gc_root_mark = g_gc_root_top;

            if (setjmp(g_catch_stack[idx].jb) == 0) {
                Val result = eval_body_non_tco(guard_body, cur_env);
                g_catch_top--;
                POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT();
                EVAL_RETURN(result);
            } else {
                g_catch_top = idx;
                Val exc     = g_catch_stack[idx].thrown_val;
                Val g_env   = g_gc_roots[g_gc_root_top - 1]; /* cur_env */
                Val g_var   = g_gc_roots[g_gc_root_top - 3]; /* guard_var */
                Val g_cls   = g_gc_roots[g_gc_root_top - 4]; /* guard_cls */
                PUSH_ROOT(exc);
                Val binding    = pl_cons(g_var, exc);
                PUSH_ROOT(binding);
                Val clause_env = pl_cons(binding, g_env);
                POP_ROOT(); /* binding */
                PUSH_ROOT(clause_env);

                Val cls = g_cls;
                while (IS_CONS(cls)) {
                    Val clause   = CAR(cls);
                    Val test_e   = IS_CONS(clause) ? CAR(clause) : NIL_VAL;
                    Val cl_body  = IS_CONS(clause) ? CDR(clause) : NIL_VAL;
                    Val tv;
                    if (IS_SYM(test_e) && SYM_IDX(test_e) == g_sym_T)
                        tv = T_VAL;
                    else
                        tv = eval(test_e, clause_env);
                    if (!IS_NIL(tv)) {
                        Val result = IS_NIL(cl_body) ? tv
                                     : eval_body_non_tco(cl_body, clause_env);
                        POP_ROOT(); /* clause_env */
                        POP_ROOT(); /* exc */
                        POP_ROOT(); POP_ROOT(); POP_ROOT();
                        POP_ROOT(); POP_ROOT(); /* guard_body, cur_env */
                        EVAL_RETURN(result);
                    }
                    cls = CDR(cls);
                }
                /* No clause matched -- re-raise. */
                Val rethrow = exc;
                POP_ROOT(); /* clause_env */
                POP_ROOT(); /* exc */
                POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT(); POP_ROOT();
                pl_error("unhandled exception in guard", rethrow);
                EVAL_RETURN(NIL_VAL);
            }
        }

        /* ----------------------------------------------------------------
         * make  (make . body)
         * PicoLisp list accumulation: saves/restores g_make_head/tail.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_make) {
            Val sh = g_make_head;
            Val st = g_make_tail;
            PUSH_ROOT(sh);
            PUSH_ROOT(st);
            g_make_head = NIL_VAL;
            g_make_tail = NIL_VAL;
            eval_body_non_tco(rest, cur_env);
            Val result  = g_make_head;
            g_make_head = g_gc_roots[g_gc_root_top - 2]; /* sh */
            g_make_tail = g_gc_roots[g_gc_root_top - 1]; /* st */
            POP_ROOT(); POP_ROOT();
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * link  (link val)
         * Append a single value to the current make list.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_link) {
            Val val = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            PUSH_ROOT(val);
            Val cell = pl_cons(val, NIL_VAL);
            POP_ROOT();
            if (IS_NIL(g_make_head)) {
                g_make_head = cell;
                g_make_tail = cell;
            } else {
                g_cells[CONS_IDX(g_make_tail)].cdr = cell;
                g_make_tail = cell;
            }
            EVAL_RETURN(val);
        }

        /* ----------------------------------------------------------------
         * chain  (chain list-expr)
         * Splice an entire list into the current make list.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_chain) {
            Val lv = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            PUSH_ROOT(lv);
            Val cur2 = lv;
            while (IS_CONS(cur2)) {
                PUSH_ROOT(cur2);
                Val cell = pl_cons(CAR(cur2), NIL_VAL);
                POP_ROOT(); /* cur2 */
                PUSH_ROOT(cell);
                if (IS_NIL(g_make_head)) {
                    g_make_head = cell;
                    g_make_tail = cell;
                } else {
                    g_cells[CONS_IDX(g_make_tail)].cdr = cell;
                    g_make_tail = cell;
                }
                POP_ROOT(); /* cell */
                cur2 = CDR(cur2);
            }
            POP_ROOT(); /* lv */
            EVAL_RETURN(lv);
        }

        /* ----------------------------------------------------------------
         * with  (with obj . body)
         * PicoLisp object context: binds 'This' dynamically to obj.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_with) {
            if (!g_sym_this)
                g_sym_this = sym_intern("This", 4);
            Val obj    = eval(IS_CONS(rest) ? CAR(rest) : NIL_VAL, cur_env);
            Val body   = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            int mark   = dynamic_bind(g_sym_this, obj);
            Val result = eval_body_non_tco(body, cur_env);
            dynamic_unbind_to(mark);
            EVAL_RETURN(result);
        }

        /* ----------------------------------------------------------------
         * quasiquote
         * -------------------------------------------------------------- */
        if (sidx == g_sym_quasiquote) {
            Val tmpl = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            EVAL_RETURN(expand_quasiquote(tmpl, cur_env, 0));
        }

        /* ----------------------------------------------------------------
         * recur  (recur arg ...)
         *
         * Signal a tail-recursive self-call.  The enclosing function-call
         * dispatch detects g_in_recur after the last body form returns.
         * -------------------------------------------------------------- */
        if (sidx == g_sym_recur) {
            g_recur_args = eval_list(rest, cur_env);
            g_in_recur   = 1;
            EVAL_RETURN(NIL_VAL); /* ignored by recur-aware loop above */
        }

        /* ----------------------------------------------------------------
         * parameterize  (parameterize ((p v) ...) . body)
         * -------------------------------------------------------------- */
        if (g_sym_parameterize && sidx == g_sym_parameterize) {
            Val bindings = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val body     = IS_CONS(rest) ? CDR(rest) : NIL_VAL;
            EVAL_RETURN(eval_parameterize(bindings, body, cur_env));
        }

        /* ----------------------------------------------------------------
         * (: key)   →  get property 'key' from This  (key unevaluated)
         * ---------------------------------------------------------------- */
        if (g_sym_colon && sidx == g_sym_colon) {
            Val key      = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val this_val = g_syms[g_sym_this].value;
            if (!IS_SYM(this_val)) EVAL_RETURN(NIL_VAL);
            Val pl = g_syms[SYM_IDX(this_val)].plist;
            while (IS_CONS(pl)) {
                Val pair = CAR(pl);
                if (IS_CONS(pair) && pl_equal(CAR(pair), key))
                    EVAL_RETURN(CDR(pair));
                pl = CDR(pl);
            }
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * (:: key)  →  prop cell for 'key' in This   (key unevaluated)
         * ---------------------------------------------------------------- */
        if (g_sym_coloncolon && sidx == g_sym_coloncolon) {
            Val key      = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val this_val = g_syms[g_sym_this].value;
            if (!IS_SYM(this_val)) EVAL_RETURN(NIL_VAL);
            Val pl = g_syms[SYM_IDX(this_val)].plist;
            while (IS_CONS(pl)) {
                Val pair = CAR(pl);
                if (IS_CONS(pair) && pl_equal(CAR(pair), key))
                    EVAL_RETURN(pair);
                pl = CDR(pl);
            }
            EVAL_RETURN(NIL_VAL);
        }

        /* ----------------------------------------------------------------
         * (=: key val)  →  put property 'key' on This  (key unevaluated)
         * ---------------------------------------------------------------- */
        if (g_sym_setcolon && sidx == g_sym_setcolon) {
            Val key_sym  = IS_CONS(rest) ? CAR(rest) : NIL_VAL;
            Val val_expr = IS_CONS(CDR(rest)) ? CADR(rest) : NIL_VAL;
            Val val      = eval(val_expr, cur_env);
            Val this_val = g_syms[g_sym_this].value;
            if (!IS_SYM(this_val)) EVAL_RETURN(val);
            uint32_t tsidx = SYM_IDX(this_val);
            /* Update existing key if found */
            Val pl = g_syms[tsidx].plist;
            while (IS_CONS(pl)) {
                Val pair = CAR(pl);
                if (IS_CONS(pair) && pl_equal(CAR(pair), key_sym)) {
                    g_cells[CONS_IDX(pair)].cdr = val;
                    EVAL_RETURN(val);
                }
                pl = CDR(pl);
            }
            /* Not found: prepend new (key . val) pair */
            PUSH_ROOT(val);
            PUSH_ROOT(key_sym);
            Val new_pair  = pl_cons(key_sym, val);
            POP_ROOT();              /* key_sym */
            PUSH_ROOT(new_pair);
            Val new_plist = pl_cons(new_pair, g_syms[tsidx].plist);
            POP_ROOT(); POP_ROOT();  /* new_pair, val */
            g_syms[tsidx].plist = new_plist;
            EVAL_RETURN(val);
        }

    } /* end IS_SYM(head) dispatch */

    /* ==================================================================
     * General application: evaluate function, check for macro, eval args.
     * ================================================================== */

    Val fn = eval(head, cur_env);
    PUSH_ROOT(fn);

    /* Macro application -- args are NOT evaluated. */
    if (IS_CONS(fn) && IS_SYM(CAR(fn)) && SYM_IDX(CAR(fn)) == g_sym_macro) {
        /* Macro format: (macro NIL params . body) */
        Val params   = CADDR(fn);
        Val mac_body = CDDDR(fn);
        Val mac_env  = bind_params_env(params, rest, NIL_VAL);
        PUSH_ROOT(mac_env);
        Val expansion = eval_body_non_tco(mac_body, mac_env);
        POP_ROOT(); /* mac_env */
        POP_ROOT(); /* fn */
        cur_expr = expansion;
        goto tail_call;
    }

    /* Evaluate arguments. */
    Val args = eval_list(rest, cur_env);
    PUSH_ROOT(args); /* fn is already at [top-2]; args at [top-1] */

    /* Primitive. */
    if (IS_PRIM(fn)) {
        Val result = g_prims[PRIM_IDX(fn)].fn(args, cur_env);
        POP_ROOT(); /* args */
        POP_ROOT(); /* fn */
        EVAL_RETURN(result);
    }

    /* Cons-tagged closures. */
    if (IS_CONS(fn)) {
        Val fn_tag = CAR(fn);
        if (IS_SYM(fn_tag)) {
            uint32_t ftag = SYM_IDX(fn_tag);

            /* lambda -- lexical TCO via goto tail_call on last body form. */
            if (ftag == g_sym_lambda) {
                Val cap_env  = CADR(fn);
                Val params   = CADDR(fn);
                Val lam_body = CDDDR(fn);
                Val call_args = args;
                POP_ROOT(); /* args */
                POP_ROOT(); /* fn */

                if (IS_NIL(lam_body)) EVAL_RETURN(NIL_VAL);

                cur_env = bind_params_env(params, call_args, cap_env);
                PUSH_ROOT(cur_env);

                Val b = lam_body;
                while (IS_CONS(CDR(b))) {
                    eval(CAR(b), cur_env);
                    b = CDR(b);
                }
                cur_expr = CAR(b);
                POP_ROOT(); /* cur_env */
                goto tail_call;
            }

            /* de / dm -- dynamic scoping with true TCO.
             * All body expressions except the last are evaluated normally.
             * The last body expression uses goto tail_call so no C stack
             * frame is consumed for tail calls (including self-recursion). */
            if (ftag == g_sym_de || ftag == g_sym_dm) {
                Val de_params = CADR(fn);
                Val de_body   = CDDR(fn);
                Val call_args = args;
                POP_ROOT(); /* args */
                POP_ROOT(); /* fn */

                if (IS_NIL(de_body)) EVAL_RETURN(NIL_VAL);

                /* Dynamic binding (PicoLisp compat: val/set work on params). */
                int mark = bind_params_dynamic(de_params, call_args);

                /* Lexical env so inner lambdas can capture params.
                 * Use NIL_VAL (not cur_env) as base: de functions are
                 * dynamically scoped and must not inherit the caller's
                 * lexical env (which would let free vars in de bodies
                 * accidentally shadow same-named primitives or globals). */
                Val de_env = bind_params_env(de_params, call_args, NIL_VAL);
                PUSH_ROOT(de_env);

                {
                    Val b = de_body;
                    while (IS_CONS(CDR(b))) {
                        eval(CAR(b), de_env);
                        b = CDR(b);
                    }
                    /* True TCO: clean up and jump to tail_call for last expr. */
                    cur_expr = CAR(b);
                    cur_env  = de_env;
                    POP_ROOT(); /* de_env */
                    dynamic_unbind_to(mark);
                    goto tail_call;
                }
            }

            /* Parameter object: callable as (po) → get, (po new) → set. */
            if (ftag == sym_parameter()) {
                Val po = fn;
                POP_ROOT(); /* args */
                POP_ROOT(); /* fn */
                if (IS_NIL(args)) EVAL_RETURN(CDR(po));
                g_cells[CONS_IDX(po)].cdr = CAR(args);
                EVAL_RETURN(CAR(args));
            }
        }
    }

    POP_ROOT(); /* args */
    POP_ROOT(); /* fn */
    pl_error("not a function", fn);
    EVAL_RETURN(NIL_VAL);
}
#undef EVAL_RETURN

/* =========================================================================
 * eval_toplevel -- evaluate an expression with an empty lexical env.
 * ===================================================================== */
Val eval_toplevel(Val expr) {
    return eval(expr, NIL_VAL);
}

/* =========================================================================
 * eval_init -- one-time setup called from main() after sym_init().
 *
 * Interns the symbols needed by the evaluator that are not in the standard
 * set (parameterize, make-parameter) and registers make-parameter as a
 * primitive so user code can call it.
 * ===================================================================== */
void eval_init(void) {
    g_sym_parameterize = sym_intern("parameterize",   11);
    g_sym_make_param   = sym_intern("make-parameter", 14);
    g_sym_this         = sym_intern("This",            4);
    g_sym_colon        = sym_intern(":",               1);
    g_sym_coloncolon   = sym_intern("::",              2);
    g_sym_setcolon     = sym_intern("=:",              2);
    /* ensure parameter tag is interned */
    (void)sym_parameter();

    /* Register make-parameter as a primitive function. */
    prim_register("make-parameter", prim_make_parameter, 1, 1);
}
