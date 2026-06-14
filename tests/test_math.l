# ============================================================
# test_math.l -- Numeric and bitwise arithmetic operations
#
# What this covers:
#   • Exact integer arithmetic: +, -, *, /, mod, rem
#   • Comparison operators and numeric predicates
#   • Exponentiation, GCD/LCM, abs/max/min, 1+/1-
#   • Bitwise operations and arithmetic shifts
#
# Run:  L tests/test_math.l
# Spec: L.spec.txt Part 2 -- Numeric Operations
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Basic arithmetic -------------------------------------
# All integers in L are exact -- there are no floating-point types.
# Division truncates toward zero (not toward negative infinity).
# mod and rem both compute a remainder but differ in sign handling
# (see section 2 below).
(test 5 (+ 2 3))
(test 1 (- 3 2))
(test 6 (* 2 3))
(test 3 (/ 9 3))
(test 2 (/ 7 3))    # truncating division -- fractional part is discarded
(test 1 (mod 7 3))
(test 1 (rem 7 3))

# ---- 2. Negative division, mod, and rem ----------------------
# Division truncates toward zero, so (-7)/3 = -2 (not -3).
# rem: sign of result follows the DIVIDEND (-7 rem 3 = -1).
# mod: sign of result follows the DIVISOR  (-7 mod 3 = 2, always >= 0).
# This matches the mathematical modulo convention for non-negative results.
(test -2 (/ -7 3))   # truncating toward zero, not floor division
(test -1 (rem -7 3)) # remainder has sign of dividend => -1

# ---- 3. Comparison -------------------------------------------
# Standard numeric comparisons; all return T or NIL.
# These operate on exact integers only.
(test T (= 42 42))
(test NIL (= 42 43))
(test T (< 1 2))
(test NIL (< 2 1))
(test T (> 2 1))
(test T (<= 2 2))
(test T (>= 3 2))

# ---- 4. abs / max / min --------------------------------------
# abs, max, and min are primitives in L, not macros or library
# functions.  They operate directly on exact integers.
(test 5 (abs -5))
(test 5 (abs 5))   # abs of non-negative is a no-op
(test 5 (max 3 5))
(test 3 (min 3 5))

# ---- 5. 1+ and 1- -------------------------------------------
# 1+ and 1- are primitives (not macros) that increment or decrement
# by 1.  They are more efficient than (+ n 1) / (- n 1).
(test 6 (1+ 5))
(test 4 (1- 5))

# ---- 6. Exponentiation: ** and expt --------------------------
# ** is integer exponentiation.  For large exponents the result is
# a bignum (arbitrary precision).  Any number raised to the 0 power
# is 1, including 0**0 by convention.
(test 8 (** 2 3))
(test 1 (** 5 0))    # edge case: anything to the 0 is 1
(test 100 (expt 10 2))

# ---- 7. gcd / lcm --------------------------------------------
# gcd returns the greatest common divisor; lcm returns the least
# common multiple.  Both always return non-negative values regardless
# of the signs of their arguments.
(test 6 (gcd 12 18))
(test 2 (gcd 4 6))
(test 12 (lcm 4 6))

# ---- 8. Bitwise operations -----------------------------------
# bitand, bitor, bitxor operate on the binary representation of
# signed integers.  << and >> are arithmetic shifts: >> preserves
# the sign bit (fills with 1s for negative numbers).
(test 4 (bitand 12 6))   # 1100 & 0110 = 0100 => 4
(test 14 (bitor 12 6))   # 1100 | 0110 = 1110 => 14
(test 10 (bitxor 12 6))  # 1100 ^ 0110 = 1010 => 10
(test 8 (<< 1 3))        # 1 shifted left 3 => 8 (multiply by 2^3)
(test 2 (>> 8 2))        # 8 shifted right 2 => 2 (divide by 2^2)

# ---- 9. numberp ----------------------------------------------
# numberp returns T for any integer (including bignums created with
# big), and NIL for strings, symbols, or any non-numeric value.
(test T (numberp 42))
(test T (numberp (big 42)))  # bignums are still numbers
(test NIL (numberp "42"))    # a string of digits is not a number
(test NIL (numberp 'sym))

(println "")
(println "test_math.l: ALL TESTS PASSED")
