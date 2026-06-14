#pragma once
#include "picolisp.h"

/* Include libbf header if available */
#ifdef HAVE_LIBBF
#include "libbf.h"
#endif

/* =========================================================================
 * Bignum pool
 *
 * BIG_POOL_SIZE slots are statically allocated.  Each slot holds either a
 * bf_t (when libbf is present) or a plain int64_t (fallback).  The mark
 * field is used by the garbage collector.
 * ===================================================================== */
#define BIG_POOL_SIZE (64 * 1024)

#ifdef HAVE_LIBBF

typedef struct {
    bf_t    val;
    uint8_t mark;
} BigSlot;

extern bf_context_t g_bf_ctx;

#else /* !HAVE_LIBBF -- fallback: int64_t arithmetic */

typedef struct {
    int64_t val;
    uint8_t mark;
} BigSlot;

#endif /* HAVE_LIBBF */

extern BigSlot   g_bignums[BIG_POOL_SIZE];
extern uint32_t  g_big_count;
extern uint32_t  g_big_free_head;

/* --- Float support (uses same pool as bignums) --- */
#define FLOAT_PREC 128  /* bits of precision for float operations */

Val  float_from_str(const char *s);
Val  float_from_int(Val v);
char *float_to_str(Val v);
Val  float_add(Val a, Val b);
Val  float_sub(Val a, Val b);
Val  float_mul(Val a, Val b);
Val  float_div(Val a, Val b);
int  float_cmp(Val a, Val b);
Val  float_math1(Val a, int op);  /* 0=sqrt,1=sin,2=cos,3=tan,4=exp,5=log,6=floor,7=ceil,8=round,9=asin,10=acos,11=atan */
Val  float_pow(Val base, Val exp);
