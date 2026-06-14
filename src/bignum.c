/* bignum.c -- Arbitrary-precision integer support for the L interpreter.
 *
 * When HAVE_LIBBF is defined the implementation delegates to libbf.
 * Otherwise a self-contained int64_t fallback is used; it supports numbers
 * up to 64 bits and reports an error if an operation would overflow that
 * range (which is sufficient for most practical Lisp programs that do not
 * require true arbitrary precision without libbf).
 */

#include "picolisp.h"
#include "bignum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* =========================================================================
 * Global pool storage
 * ===================================================================== */
BigSlot  g_bignums[BIG_POOL_SIZE];
uint32_t g_big_count    = 0;
uint32_t g_big_free_head = UINT32_MAX;   /* UINT32_MAX = no free list entry */

#ifdef HAVE_LIBBF
bf_context_t g_bf_ctx;

static void *bf_realloc_fn(void *opaque, void *ptr, size_t size)
{
    (void)opaque;
    if (size == 0) { free(ptr); return NULL; }
    return ptr ? realloc(ptr, size) : malloc(size);
}
#endif /* HAVE_LIBBF */

/* =========================================================================
 * Initialisation
 * ===================================================================== */
void bignum_init(void)
{
#ifdef HAVE_LIBBF
    bf_context_init(&g_bf_ctx, bf_realloc_fn, NULL);
#endif
    memset(g_bignums, 0, sizeof(g_bignums));
    g_big_count    = 0;
    g_big_free_head = UINT32_MAX;
}

/* =========================================================================
 * Internal slot allocator
 *
 * Strategy:
 *   1. If the free-list has an entry, use it.
 *   2. Otherwise bump g_big_count (linear region at the top).
 *   3. If both fail, scan the pool for any un-marked slot (post-GC sweep).
 * ===================================================================== */
static uint32_t big_alloc_slot(void)
{
    /* 1. Free-list */
    if (g_big_free_head != UINT32_MAX) {
        uint32_t idx = g_big_free_head;
        /* The free-list chain is stored in the lower 32 bits of val.
         * For the fallback build val is int64_t; treat it as uint32_t. */
#ifdef HAVE_LIBBF
        /* We overload sign to store the next pointer; use mark==0 sentinel */
        g_big_free_head = UINT32_MAX; /* single-entry list simplification */
#else
        g_big_free_head = (uint32_t)(uint64_t)g_bignums[idx].val;
#endif
        return idx;
    }

    /* 2. Bump allocator */
    if (g_big_count < BIG_POOL_SIZE) {
        return g_big_count++;
    }

    /* 3. Pool full -- run GC to reclaim unreachable bignums.
     * gc_sweep_bignums() adds all freed slots to the free list, so we
     * simply retry path 1 after collection.  We MUST NOT scan by mark here:
     * gc_sweep resets all marks to 0, so every slot would appear free. */
    extern void gc_collect(void);
    gc_collect();


    /* Retry free list (populated by gc_sweep_bignums above). */
    if (g_big_free_head != UINT32_MAX) {
        uint32_t idx = g_big_free_head;
#ifdef HAVE_LIBBF
        g_big_free_head = UINT32_MAX;
#else
        g_big_free_head = (uint32_t)(uint64_t)g_bignums[idx].val;
#endif
        return idx;
    }

    pl_error_str("bignum pool exhausted");
    return 0; /* unreachable */
}

/* =========================================================================
 * Internal: release a slot back to the free list
 * ===================================================================== */
static void big_free_slot(uint32_t idx)
{
#ifdef HAVE_LIBBF
    bf_delete(&g_bignums[idx].val);
    memset(&g_bignums[idx].val, 0, sizeof(bf_t));
#else
    /* Store the old free-head in the slot's val for chaining */
    g_bignums[idx].val = (int64_t)(uint64_t)g_big_free_head;
#endif
    g_bignums[idx].mark = 0;
    g_big_free_head = idx;
}

/* =========================================================================
 * Constructors
 * ===================================================================== */

Val big_from_int64(int64_t n)
{
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;   /* keep alive until stored in a root */
#ifdef HAVE_LIBBF
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set_si(&g_bignums[idx].val, n);
#else
    g_bignums[idx].val = n;
#endif
    return MAKE_BIG(idx);
}

Val big_from_str(const char *s, int base)
{
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
#ifdef HAVE_LIBBF
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    if (bf_atof(&g_bignums[idx].val, s, NULL, base,
                 BF_PREC_INF, BF_RNDZ) != 0) {
        big_free_slot(idx);
        pl_error_str("bignum: invalid numeric string");
    }
#else
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, base);
    if (errno != 0 || (end && *end != '\0')) {
        big_free_slot(idx);
        pl_error_str("bignum: invalid numeric string");
    }
    g_bignums[idx].val = (int64_t)v;
#endif
    return MAKE_BIG(idx);
}

/* =========================================================================
 * to_big -- coerce an arbitrary Val to a bignum Val
 * ===================================================================== */
Val to_big(Val v)
{
    if (IS_BIG(v)) return v;
    if (IS_INT(v)) return big_from_int64((int64_t)INT_VAL(v));
    pl_error("not a number", v);
    return NIL_VAL;
}

/* =========================================================================
 * big_normalize -- demote a bignum to INT if the value fits in int32_t
 * ===================================================================== */
Val big_normalize(Val v)
{
    if (!IS_BIG(v)) return v;
    uint32_t idx = BIG_IDX(v);
#ifdef HAVE_LIBBF
    int64_t n = 0;
    if (bf_get_int64(&n, &g_bignums[idx].val, 0) == 0
        && n >= INT32_MIN && n <= INT32_MAX)
    {
        big_free_slot(idx);
        return MAKE_INT((int32_t)n);
    }
#else
    int64_t n = g_bignums[idx].val;
    if (n >= INT32_MIN && n <= INT32_MAX) {
        big_free_slot(idx);
        return MAKE_INT((int32_t)n);
    }
#endif
    return v;
}

/* =========================================================================
 * Helper: extract int64_t from a bignum slot
 * (fallback only -- with libbf we work directly on bf_t)
 * ===================================================================== */
#ifndef HAVE_LIBBF
static int64_t big_val(Val v)
{
    return g_bignums[BIG_IDX(v)].val;
}
#endif

/* =========================================================================
 * Core bignum arithmetic  (big_* always receive BIG-tagged Vals)
 * ===================================================================== */

Val big_add(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_add(&g_bignums[idx].val,
           &g_bignums[BIG_IDX(a)].val,
           &g_bignums[BIG_IDX(b)].val,
           BF_PREC_INF, BF_RNDZ);
    return MAKE_BIG(idx);
#else
    /* Detect signed overflow: if the signs match and the result flips, OOB.
     * For values up to 64-bit we use __int128 on GCC/Clang when available,
     * otherwise we rely on int64 wrapping and check post-hoc. */
    int64_t av = big_val(a), bv = big_val(b);
#if defined(__GNUC__) || defined(__clang__)
    __int128 r = (__int128)av + (__int128)bv;
    if (r > INT64_MAX || r < INT64_MIN)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64((int64_t)r);
#else
    /* Portable overflow check */
    if (bv > 0 && av > INT64_MAX - bv)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    if (bv < 0 && av < INT64_MIN - bv)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64(av + bv);
#endif
#endif /* HAVE_LIBBF */
}

Val big_sub(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_sub(&g_bignums[idx].val,
           &g_bignums[BIG_IDX(a)].val,
           &g_bignums[BIG_IDX(b)].val,
           BF_PREC_INF, BF_RNDZ);
    return MAKE_BIG(idx);
#else
    int64_t av = big_val(a), bv = big_val(b);
#if defined(__GNUC__) || defined(__clang__)
    __int128 r = (__int128)av - (__int128)bv;
    if (r > INT64_MAX || r < INT64_MIN)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64((int64_t)r);
#else
    if (bv < 0 && av > INT64_MAX + bv)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    if (bv > 0 && av < INT64_MIN + bv)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64(av - bv);
#endif
#endif
}

Val big_mul(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_mul(&g_bignums[idx].val,
           &g_bignums[BIG_IDX(a)].val,
           &g_bignums[BIG_IDX(b)].val,
           BF_PREC_INF, BF_RNDZ);
    return MAKE_BIG(idx);
#else
    int64_t av = big_val(a), bv = big_val(b);
#if defined(__GNUC__) || defined(__clang__)
    __int128 r = (__int128)av * (__int128)bv;
    if (r > INT64_MAX || r < INT64_MIN)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64((int64_t)r);
#else
    /* Portable overflow check for multiplication */
    if (av != 0 && bv != 0) {
        if ((av > 0 && bv > 0 && av > INT64_MAX / bv) ||
            (av < 0 && bv < 0 && av < INT64_MAX / bv) ||
            (av > 0 && bv < 0 && bv < INT64_MIN / av) ||
            (av < 0 && bv > 0 && av < INT64_MIN / bv))
            pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    }
    return big_from_int64(av * bv);
#endif
#endif
}

Val big_div(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    if (bf_is_zero(&g_bignums[BIG_IDX(b)].val))
        pl_error_str("division by zero");
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_t rem;
    bf_init(&g_bf_ctx, &rem);
    bf_divrem(&g_bignums[idx].val, &rem,
              &g_bignums[BIG_IDX(a)].val,
              &g_bignums[BIG_IDX(b)].val,
              BF_PREC_INF, BF_RNDZ, BF_RNDZ);
    bf_delete(&rem);
    return MAKE_BIG(idx);
#else
    int64_t av = big_val(a), bv = big_val(b);
    if (bv == 0) pl_error_str("division by zero");
    /* Handle INT64_MIN / -1 overflow */
    if (av == INT64_MIN && bv == -1)
        pl_error_str("bignum overflow (compile with libbf for arbitrary precision)");
    return big_from_int64(av / bv);
#endif
}

Val big_mod(Val a, Val b)
{
    /* Floored modulo: result has the sign of the divisor */
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    if (bf_is_zero(&g_bignums[BIG_IDX(b)].val))
        pl_error_str("modulo by zero");
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_t quot;
    bf_init(&g_bf_ctx, &quot);
    bf_divrem(&quot, &g_bignums[idx].val,
              &g_bignums[BIG_IDX(a)].val,
              &g_bignums[BIG_IDX(b)].val,
              BF_PREC_INF, BF_RNDZ, BF_RNDD);
    bf_delete(&quot);
    return MAKE_BIG(idx);
#else
    int64_t av = big_val(a), bv = big_val(b);
    if (bv == 0) pl_error_str("modulo by zero");
    int64_t r = av % bv;
    /* Adjust to floored (same sign as divisor) */
    if (r != 0 && ((r ^ bv) < 0)) r += bv;
    return big_from_int64(r);
#endif
}

Val big_rem(Val a, Val b)
{
    /* Truncating remainder: result has the sign of the dividend */
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    if (bf_is_zero(&g_bignums[BIG_IDX(b)].val))
        pl_error_str("remainder by zero");
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_t quot;
    bf_init(&g_bf_ctx, &quot);
    bf_divrem(&quot, &g_bignums[idx].val,
              &g_bignums[BIG_IDX(a)].val,
              &g_bignums[BIG_IDX(b)].val,
              BF_PREC_INF, BF_RNDZ, BF_RNDZ);
    bf_delete(&quot);
    return MAKE_BIG(idx);
#else
    int64_t av = big_val(a), bv = big_val(b);
    if (bv == 0) pl_error_str("remainder by zero");
    return big_from_int64(av % bv);
#endif
}

Val big_pow(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    /* libbf does not provide integer exponentiation directly.
     * Use repeated squaring on bf_t. */
    int64_t exp = 0;
    if (bf_get_int64(&exp, &g_bignums[BIG_IDX(b)].val, 0) != 0 || exp < 0)
        pl_error_str("big_pow: exponent must be a non-negative integer");
    uint32_t ridx = big_alloc_slot();
    g_bignums[ridx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[ridx].val);
    bf_set_si(&g_bignums[ridx].val, 1);

    uint32_t bidx = big_alloc_slot();
    g_bignums[bidx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[bidx].val);
    bf_set(&g_bignums[bidx].val, &g_bignums[BIG_IDX(a)].val);

    while (exp > 0) {
        if (exp & 1) {
            bf_t tmp;
            bf_init(&g_bf_ctx, &tmp);
            bf_mul(&tmp, &g_bignums[ridx].val, &g_bignums[bidx].val,
                   BF_PREC_INF, BF_RNDZ);
            bf_delete(&g_bignums[ridx].val);
            g_bignums[ridx].val = tmp;
        }
        bf_t tmp2;
        bf_init(&g_bf_ctx, &tmp2);
        bf_mul(&tmp2, &g_bignums[bidx].val, &g_bignums[bidx].val,
               BF_PREC_INF, BF_RNDZ);
        bf_delete(&g_bignums[bidx].val);
        g_bignums[bidx].val = tmp2;
        exp >>= 1;
    }
    big_free_slot(bidx);
    return MAKE_BIG(ridx);
#else
    int64_t base = big_val(a), exp = big_val(b);
    if (exp < 0) pl_error_str("big_pow: negative exponent");
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) {
#if defined(__GNUC__) || defined(__clang__)
            __int128 t = (__int128)result * base;
            if (t > INT64_MAX || t < INT64_MIN)
                pl_error_str("bignum overflow in pow (compile with libbf)");
            result = (int64_t)t;
#else
            result *= base;
#endif
        }
        if (exp > 1) {
#if defined(__GNUC__) || defined(__clang__)
            __int128 t = (__int128)base * base;
            if (t > INT64_MAX || t < INT64_MIN)
                pl_error_str("bignum overflow in pow (compile with libbf)");
            base = (int64_t)t;
#else
            base *= base;
#endif
        }
        exp >>= 1;
    }
    return big_from_int64(result);
#endif
}

Val big_gcd(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    /* Euclidean algorithm on bf_t */
    uint32_t xidx = big_alloc_slot();
    g_bignums[xidx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[xidx].val);
    bf_set(&g_bignums[xidx].val, &g_bignums[BIG_IDX(a)].val);
    /* abs(x) */
    g_bignums[xidx].val.sign = 0;

    uint32_t yidx = big_alloc_slot();
    g_bignums[yidx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[yidx].val);
    bf_set(&g_bignums[yidx].val, &g_bignums[BIG_IDX(b)].val);
    g_bignums[yidx].val.sign = 0;

    while (!bf_is_zero(&g_bignums[yidx].val)) {
        bf_t rem;
        bf_init(&g_bf_ctx, &rem);
        bf_t quot;
        bf_init(&g_bf_ctx, &quot);
        bf_divrem(&quot, &rem,
                  &g_bignums[xidx].val, &g_bignums[yidx].val,
                  BF_PREC_INF, BF_RNDZ, BF_RNDZ);
        bf_delete(&quot);
        bf_delete(&g_bignums[xidx].val);
        g_bignums[xidx].val = g_bignums[yidx].val;
        g_bignums[yidx].val = rem;
    }
    big_free_slot(yidx);
    return MAKE_BIG(xidx);
#else
    /* Simple Euclidean GCD */
    int64_t x = big_val(a);
    int64_t y = big_val(b);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y != 0) { int64_t t = y; y = x % y; x = t; }
    return big_from_int64(x);
#endif
}

Val big_lcm(Val a, Val b)
{
    PUSH_ROOT(a);
    PUSH_ROOT(b);
    Val g = big_gcd(a, b);
    PUSH_ROOT(g);

    /* lcm(a,b) = abs(a) / gcd(a,b) * abs(b) -- divide first to reduce size */
    Val aa = big_abs(a);
    PUSH_ROOT(aa);
    Val bb = big_abs(b);
    PUSH_ROOT(bb);

    Val q = big_div(aa, g);
    PUSH_ROOT(q);
    Val result = big_mul(q, bb);

    POP_ROOT(); /* q   */
    POP_ROOT(); /* bb  */
    POP_ROOT(); /* aa  */
    POP_ROOT(); /* g   */
    POP_ROOT(); /* b   */
    POP_ROOT(); /* a   */
    return result;
}

Val big_abs(Val a)
{
    a = to_big(a);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set(&g_bignums[idx].val, &g_bignums[BIG_IDX(a)].val);
    g_bignums[idx].val.sign = 0;
    return MAKE_BIG(idx);
#else
    int64_t v = big_val(a);
    if (v == INT64_MIN)
        pl_error_str("bignum: abs(INT64_MIN) overflows int64 (compile with libbf)");
    return big_from_int64(v < 0 ? -v : v);
#endif
}

Val big_neg(Val a)
{
    a = to_big(a);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set(&g_bignums[idx].val, &g_bignums[BIG_IDX(a)].val);
    bf_neg(&g_bignums[idx].val);
    return MAKE_BIG(idx);
#else
    int64_t v = big_val(a);
    if (v == INT64_MIN)
        pl_error_str("bignum: neg(INT64_MIN) overflows int64 (compile with libbf)");
    return big_from_int64(-v);
#endif
}

int big_cmp(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    int c = bf_cmp_full(&g_bignums[BIG_IDX(a)].val,
                        &g_bignums[BIG_IDX(b)].val);
    return (c > 0) ? 1 : (c < 0) ? -1 : 0;
#else
    int64_t av = big_val(a), bv = big_val(b);
    return (av > bv) ? 1 : (av < bv) ? -1 : 0;
#endif
}

/* =========================================================================
 * Predicate helpers
 * ===================================================================== */
bool big_is_zero(Val v)
{
    if (IS_INT(v)) return INT_VAL(v) == 0;
    v = to_big(v);
#ifdef HAVE_LIBBF
    return bf_is_zero(&g_bignums[BIG_IDX(v)].val);
#else
    return big_val(v) == 0;
#endif
}

bool big_is_neg(Val v)
{
    if (IS_INT(v)) return INT_VAL(v) < 0;
    v = to_big(v);
#ifdef HAVE_LIBBF
    return g_bignums[BIG_IDX(v)].val.sign != 0
        && !bf_is_zero(&g_bignums[BIG_IDX(v)].val);
#else
    return big_val(v) < 0;
#endif
}

/* =========================================================================
 * Bitwise operations
 * ===================================================================== */

Val big_bitand(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_logic_and(&g_bignums[idx].val,
                 &g_bignums[BIG_IDX(a)].val,
                 &g_bignums[BIG_IDX(b)].val);
    return MAKE_BIG(idx);
#else
    return big_from_int64(big_val(a) & big_val(b));
#endif
}

Val big_bitor(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_logic_or(&g_bignums[idx].val,
                &g_bignums[BIG_IDX(a)].val,
                &g_bignums[BIG_IDX(b)].val);
    return MAKE_BIG(idx);
#else
    return big_from_int64(big_val(a) | big_val(b));
#endif
}

Val big_bitxor(Val a, Val b)
{
    a = to_big(a);
    b = to_big(b);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_logic_xor(&g_bignums[idx].val,
                 &g_bignums[BIG_IDX(a)].val,
                 &g_bignums[BIG_IDX(b)].val);
    return MAKE_BIG(idx);
#else
    return big_from_int64(big_val(a) ^ big_val(b));
#endif
}

Val big_bitnot(Val a)
{
    a = to_big(a);
#ifdef HAVE_LIBBF
    /* ~x == -(x+1) */
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set(&g_bignums[idx].val, &g_bignums[BIG_IDX(a)].val);
    bf_add_si(&g_bignums[idx].val, &g_bignums[idx].val, 1, BF_PREC_INF, BF_RNDZ);
    bf_neg(&g_bignums[idx].val);
    return MAKE_BIG(idx);
#else
    return big_from_int64(~big_val(a));
#endif
}

Val big_shl(Val a, int n)
{
    a = to_big(a);
    if (n < 0) return big_shr(a, -n);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set(&g_bignums[idx].val, &g_bignums[BIG_IDX(a)].val);
    bf_mul_2exp(&g_bignums[idx].val, n, BF_PREC_INF, BF_RNDZ);
    return MAKE_BIG(idx);
#else
    int64_t v = big_val(a);
    if (n >= 64) {
        if (v == 0) return big_from_int64(0);
        pl_error_str("bignum shl overflow (compile with libbf)");
    }
    /* Check overflow */
    if (v != 0 && n > 0) {
        int64_t shifted = v << n;
        if ((shifted >> n) != v)
            pl_error_str("bignum shl overflow (compile with libbf)");
        return big_from_int64(shifted);
    }
    return big_from_int64(v << n);
#endif
}

Val big_shr(Val a, int n)
{
    a = to_big(a);
    if (n < 0) return big_shl(a, -n);
#ifdef HAVE_LIBBF
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    bf_set(&g_bignums[idx].val, &g_bignums[BIG_IDX(a)].val);
    bf_mul_2exp(&g_bignums[idx].val, -n, BF_PREC_INF, BF_RNDZ);
    /* Truncate to integer */
    bf_rint(&g_bignums[idx].val, BF_RNDZ);
    return MAKE_BIG(idx);
#else
    int64_t v = big_val(a);
    if (n >= 64) return big_from_int64(v < 0 ? -1 : 0);
    /* Arithmetic right shift */
    return big_from_int64(v >> n);
#endif
}

/* =========================================================================
 * big_to_str -- return a malloc'd decimal string
 * ===================================================================== */
char *big_to_str(Val v)
{
    if (IS_INT(v)) {
        char *buf = malloc(16);
        if (!buf) { pl_error_str("OOM in big_to_str"); return NULL; }
        snprintf(buf, 16, "%d", INT_VAL(v));
        return buf;
    }
#ifdef HAVE_LIBBF
    size_t len = 0;
    char *str = bf_ftoa(&len, &g_bignums[BIG_IDX(v)].val, 10,
                        0, BF_FTOA_FORMAT_FRAC | BF_RNDZ);
    if (!str) { pl_error_str("OOM in big_to_str (bf_ftoa)"); return NULL; }
    (void)len;
    return str;   /* caller must free() */
#else
    char *buf = malloc(32);
    if (!buf) { pl_error_str("OOM in big_to_str"); return NULL; }
    snprintf(buf, 32, "%lld", (long long)g_bignums[BIG_IDX(v)].val);
    return buf;
#endif
}

/* =========================================================================
 * Unified arithmetic helpers  (dispatch INT/BIG transparently)
 * ===================================================================== */

Val pl_add(Val a, Val b)
{
    if (IS_FLOAT(a) || IS_FLOAT(b)) return float_add(a, b);
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r = (int64_t)INT_VAL(a) + (int64_t)INT_VAL(b);
        if (r >= INT32_MIN && r <= INT32_MAX) return MAKE_INT((int32_t)r);
        return big_normalize(big_from_int64(r));
    }
    return big_normalize(big_add(to_big(a), to_big(b)));
}

Val pl_sub(Val a, Val b)
{
    if (IS_FLOAT(a) || IS_FLOAT(b)) return float_sub(a, b);
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r = (int64_t)INT_VAL(a) - (int64_t)INT_VAL(b);
        if (r >= INT32_MIN && r <= INT32_MAX) return MAKE_INT((int32_t)r);
        return big_normalize(big_from_int64(r));
    }
    return big_normalize(big_sub(to_big(a), to_big(b)));
}

Val pl_mul(Val a, Val b)
{
    if (IS_FLOAT(a) || IS_FLOAT(b)) return float_mul(a, b);
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r = (int64_t)INT_VAL(a) * (int64_t)INT_VAL(b);
        if (r >= INT32_MIN && r <= INT32_MAX) return MAKE_INT((int32_t)r);
        return big_normalize(big_from_int64(r));
    }
    return big_normalize(big_mul(to_big(a), to_big(b)));
}

Val pl_div(Val a, Val b)
{
    if (IS_FLOAT(a) || IS_FLOAT(b)) return float_div(a, b);
    if (IS_INT(a) && IS_INT(b)) {
        int32_t bv = INT_VAL(b);
        if (bv == 0) pl_error_str("division by zero");
        int32_t av = INT_VAL(a);
        if (av == INT32_MIN && bv == -1) return big_normalize(big_from_int64((int64_t)INT32_MIN * -1));
        return MAKE_INT(av / bv);
    }
    return big_normalize(big_div(to_big(a), to_big(b)));
}

Val pl_mod(Val a, Val b)
{
    if (IS_INT(a) && IS_INT(b)) {
        int32_t bv = INT_VAL(b);
        if (bv == 0) pl_error_str("modulo by zero");
        int32_t r = INT_VAL(a) % bv;
        if (r != 0 && ((r ^ bv) < 0)) r += bv;   /* floored */
        return MAKE_INT(r);
    }
    return big_normalize(big_mod(to_big(a), to_big(b)));
}

Val pl_rem(Val a, Val b)
{
    if (IS_INT(a) && IS_INT(b)) {
        int32_t bv = INT_VAL(b);
        if (bv == 0) pl_error_str("remainder by zero");
        return MAKE_INT(INT_VAL(a) % bv);
    }
    return big_normalize(big_rem(to_big(a), to_big(b)));
}

int pl_cmp(Val a, Val b)
{
    if (IS_FLOAT(a) || IS_FLOAT(b)) return float_cmp(a, b);
    if (IS_INT(a) && IS_INT(b)) {
        int32_t av = INT_VAL(a), bv = INT_VAL(b);
        return (av > bv) ? 1 : (av < bv) ? -1 : 0;
    }
    return big_cmp(to_big(a), to_big(b));
}

Val pl_abs(Val v)
{
    if (IS_INT(v)) {
        int32_t n = INT_VAL(v);
        if (n >= 0) return v;
        if (n == INT32_MIN) return big_normalize(big_from_int64(-(int64_t)INT32_MIN));
        return MAKE_INT(-n);
    }
    return big_normalize(big_abs(v));
}

Val pl_neg(Val v)
{
    if (IS_INT(v)) {
        int32_t n = INT_VAL(v);
        if (n == INT32_MIN) return big_normalize(big_from_int64(-(int64_t)INT32_MIN));
        return MAKE_INT(-n);
    }
    return big_normalize(big_neg(v));
}

Val pl_max(Val a, Val b) { return pl_cmp(a, b) >= 0 ? a : b; }
Val pl_min(Val a, Val b) { return pl_cmp(a, b) <= 0 ? a : b; }

/* =========================================================================
 * GC integration
 * ===================================================================== */

void gc_clear_bignum_marks(void)
{
    for (uint32_t i = 0; i < g_big_count; i++)
        g_bignums[i].mark = 0;
}

/* IS_BF: matches both BIG and FLOAT (they share the same pool) */
#define IS_BF(v) (IS_BIG(v) || IS_FLOAT(v))
#define BF_IDX(v) ((uint32_t)(VAL_PAYLOAD(v) & UINT64_C(0xFFFFFFFF)))
#define MARK_BF(v) do { if (IS_BF(v)) g_bignums[BF_IDX(v)].mark = 1; } while(0)

void gc_mark_bignums(void)
{
    /* Walk all marked cons cells */
    for (uint32_t i = 0; i < CELL_POOL_SIZE; i++) {
        if (g_cells[i].mark) {
            MARK_BF(g_cells[i].car);
            MARK_BF(g_cells[i].cdr);
        }
    }

    /* GC root stack */
    for (int i = 0; i < g_gc_root_top; i++)
        MARK_BF(g_gc_roots[i]);

    /* Dynamic bind stack */
    for (int i = 0; i < g_bind_top; i++)
        MARK_BF(g_bind_stack[i].saved);

    /* Symbol table values and plists */
    for (uint32_t i = 1; i <= g_sym_count; i++) {
        MARK_BF(g_syms[i].value);
        MARK_BF(g_syms[i].plist);
    }

    /* Special globals */
    MARK_BF(g_at);
    MARK_BF(g_make_head);
    MARK_BF(g_make_tail);

    /* Vector pool elements */
    for (uint32_t i = 0; i < g_vec_count; i++) {
        if (g_vec_pool[i].mark) {
            for (uint32_t j = 0; j < g_vec_pool[i].len; j++)
                MARK_BF(g_vec_pool[i].data[j]);
        }
    }
}

/* =========================================================================
 * Float support -- uses the same bignum pool with TAG_FLOAT
 * ===================================================================== */

#ifdef HAVE_LIBBF

/* Helper: allocate a slot, init bf_t, return FLOAT Val */
static Val float_alloc(void) {
    uint32_t idx = big_alloc_slot();
    g_bignums[idx].mark = 1;
    bf_init(&g_bf_ctx, &g_bignums[idx].val);
    return MAKE_FLOAT(idx);
}

/* Convert Val to bf_t pointer. Caller must NOT free.
 * For INT/BIG, stores into *tmp and returns tmp. For FLOAT, returns pool ptr. */
static bf_t *val_to_bf(Val v, bf_t *tmp) {
    if (IS_FLOAT(v)) return &g_bignums[FLOAT_IDX(v)].val;
    if (IS_BIG(v))   return &g_bignums[BIG_IDX(v)].val;
    /* INT */
    bf_init(&g_bf_ctx, tmp);
    bf_set_si(tmp, (int64_t)INT_VAL(v));
    return tmp;
}

Val float_from_str(const char *s) {
    Val r = float_alloc();
    bf_atof(&g_bignums[FLOAT_IDX(r)].val, s, NULL, 10, FLOAT_PREC, BF_RNDN);
    return r;
}

Val float_from_int(Val v) {
    Val r = float_alloc();
    if (IS_INT(v)) {
        bf_set_si(&g_bignums[FLOAT_IDX(r)].val, (int64_t)INT_VAL(v));
    } else if (IS_BIG(v)) {
        bf_set(&g_bignums[FLOAT_IDX(r)].val, &g_bignums[BIG_IDX(v)].val);
    }
    return r;
}

char *float_to_str(Val v) {
    size_t len = 0;
    char *s = bf_ftoa(&len, &g_bignums[FLOAT_IDX(v)].val, 10, 20,
                       BF_FTOA_FORMAT_FREE_MIN | BF_RNDN);
    if (!s) return NULL;
    /* Ensure there's a decimal point so it prints as a float */
    int has_dot = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == 'n' /* nan */ || s[i] == 'i' /* inf */) {
            has_dot = 1; break;
        }
    }
    if (!has_dot) {
        /* Append ".0" */
        char *s2 = realloc(s, len + 3);
        if (s2) { s = s2; s[len] = '.'; s[len+1] = '0'; s[len+2] = '\0'; }
    }
    return s;
}

#define FLOAT_BINOP(name, bf_fn) \
Val name(Val a, Val b) { \
    bf_t ta, tb; \
    bf_t *pa = val_to_bf(a, &ta); \
    bf_t *pb = val_to_bf(b, &tb); \
    Val r = float_alloc(); \
    bf_fn(&g_bignums[FLOAT_IDX(r)].val, pa, pb, FLOAT_PREC, BF_RNDN); \
    if (pa == &ta) bf_delete(&ta); \
    if (pb == &tb) bf_delete(&tb); \
    return r; \
}

FLOAT_BINOP(float_add, bf_add)
FLOAT_BINOP(float_sub, bf_sub)
FLOAT_BINOP(float_mul, bf_mul)
FLOAT_BINOP(float_div, bf_div)

int float_cmp(Val a, Val b) {
    bf_t ta, tb;
    bf_t *pa = val_to_bf(a, &ta);
    bf_t *pb = val_to_bf(b, &tb);
    int r = bf_cmp(pa, pb);
    if (pa == &ta) bf_delete(&ta);
    if (pb == &tb) bf_delete(&tb);
    return r;
}

Val float_math1(Val a, int op) {
    bf_t ta;
    bf_t *pa = val_to_bf(a, &ta);
    Val r = float_alloc();
    bf_t *pr = &g_bignums[FLOAT_IDX(r)].val;
    switch (op) {
        case 0:  bf_sqrt(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 1:  bf_sin(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 2:  bf_cos(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 3:  bf_tan(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 4:  bf_exp(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 5:  bf_log(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 6:  bf_set(pr, pa); bf_rint(pr, BF_RNDD); break; /* floor */
        case 7:  bf_set(pr, pa); bf_rint(pr, BF_RNDU); break; /* ceil */
        case 8:  bf_set(pr, pa); bf_rint(pr, BF_RNDN); break; /* round */
        case 9:  bf_asin(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 10: bf_acos(pr, pa, FLOAT_PREC, BF_RNDN); break;
        case 11: bf_atan(pr, pa, FLOAT_PREC, BF_RNDN); break;
        default: bf_set(pr, pa); break;
    }
    if (pa == &ta) bf_delete(&ta);
    return r;
}

Val float_pow(Val base, Val exp) {
    bf_t ta, tb;
    bf_t *pa = val_to_bf(base, &ta);
    bf_t *pb = val_to_bf(exp, &tb);
    Val r = float_alloc();
    bf_pow(&g_bignums[FLOAT_IDX(r)].val, pa, pb, FLOAT_PREC, BF_RNDN);
    if (pa == &ta) bf_delete(&ta);
    if (pb == &tb) bf_delete(&tb);
    return r;
}

#else /* !HAVE_LIBBF stubs */

Val float_from_str(const char *s) { (void)s; pl_error_str("floats require libbf"); return NIL_VAL; }
Val float_from_int(Val v) { (void)v; pl_error_str("floats require libbf"); return NIL_VAL; }
char *float_to_str(Val v) { (void)v; return NULL; }
Val float_add(Val a, Val b) { (void)a;(void)b; pl_error_str("floats require libbf"); return NIL_VAL; }
Val float_sub(Val a, Val b) { (void)a;(void)b; pl_error_str("floats require libbf"); return NIL_VAL; }
Val float_mul(Val a, Val b) { (void)a;(void)b; pl_error_str("floats require libbf"); return NIL_VAL; }
Val float_div(Val a, Val b) { (void)a;(void)b; pl_error_str("floats require libbf"); return NIL_VAL; }
int float_cmp(Val a, Val b) { (void)a;(void)b; return 0; }
Val float_math1(Val a, int op) { (void)a;(void)op; pl_error_str("floats require libbf"); return NIL_VAL; }
Val float_pow(Val a, Val b) { (void)a;(void)b; pl_error_str("floats require libbf"); return NIL_VAL; }

#endif /* HAVE_LIBBF */

/* =========================================================================
 * GC sweep
 * ===================================================================== */

void gc_sweep_bignums(void)
{
    for (uint32_t i = 0; i < g_big_count; i++) {
        if (!g_bignums[i].mark) {
#ifdef HAVE_LIBBF
            bf_delete(&g_bignums[i].val);
            memset(&g_bignums[i].val, 0, sizeof(bf_t));
#else
            /* Chain into free list */
            g_bignums[i].val = (int64_t)(uint64_t)g_big_free_head;
            g_big_free_head = i;
#endif
        }
        g_bignums[i].mark = 0;
    }
}
