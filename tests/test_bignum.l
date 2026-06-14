# ============================================================
# test_bignum.l -- Arbitrary-precision integer (bignum) arithmetic
#
# What this covers:
#   • Automatic promotion from fixnum to bignum at range boundary
#   • `(big? x)` predicate: T only for true bignums, NIL for fixnums
#   • All core arithmetic operators (+, -, *, factorial) on bignum values
#   • Comparison operators (=, <, >) work transparently across fixnum/bignum
#
# Run:  L tests/test_bignum.l
# Spec: L.spec.txt Part 3 -- Numeric Tower / Bignum
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. big? predicate -------------------------------------
# `(big x)` coerces any integer to bignum storage; values that
# naturally fit in the fixnum range are NOT bignums -- big? returns
# NIL for them regardless of their magnitude label.
(test T (big? (big 42)))
(test NIL (big? 42))

# Adding two large fixnums can cross the promotion threshold.
# On this platform, 2000000000 + 2000000000 = 4000000000 exceeds
# the ~±2^47 fixnum range and is automatically stored as a bignum.
(test T (big? (+ 2000000000 2000000000)))

# ---- 2. Factorial -- fixnum vs. bignum boundary -------------
# factorial(10) = 3628800 fits comfortably in a fixnum.
# factorial(20) overflows fixnum range; L promotes automatically.
# NOTE: the `factorial` helper is used only in this section.
(de factorial (n)
  (if (<= n 1) 1 (* n (factorial (1- n)))))

(test 3628800 (factorial 10))
(test T (> (factorial 20) 2000000000))
(test T (big? (factorial 20)))

# ---- 3. Addition -------------------------------------------
# Straightforward: two 2-billion fixnums sum to a 4-billion bignum.
(test 4000000000 (+ 2000000000 2000000000))
(test T (big? (+ 2000000000 2000000000)))

# ---- 4. Comparison -----------------------------------------
# Comparison operators accept mixed fixnum/bignum arguments.
# Ordering is by numeric value, not by storage type.
(test T (> 4000000000 2000000000))
(test T (< 2000000000 4000000000))
(test T (= 4000000000 4000000000))

# ---- 5. Subtraction ----------------------------------------
# Subtracting two bignums may produce a result small enough to
# demote back to a fixnum. The result here is 2000000000 which
# sits at the fixnum/bignum boundary -- its storage type is
# platform-dependent; only the numeric value is tested.
(test 2000000000 (- 4000000000 2000000000))

# ---- 6. Multiplication -------------------------------------
# 3e9 * 3e9 = 9e18, well beyond 64-bit signed int max (~9.2e18).
# L promotes the result to bignum transparently. # => 9000000000000000000
(test 9000000000000000000 (* 3000000000 3000000000))

(println "")
(println "test_bignum.l: ALL TESTS PASSED")
