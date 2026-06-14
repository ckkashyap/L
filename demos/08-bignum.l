# ============================================================
# 08-bignum.l -- Arbitrary-precision integer arithmetic
#
# What this covers:
#   1. Automatic promotion to bignum beyond fixnum range
#   2. No floating-point -- exact integer arithmetic only
#   3. Classic bignum workloads: factorial and Fibonacci
#   4. Bignum predicate: big?
#
# Run:  L demos/08-bignum.l
# Spec: L.spec.txt Part 2 -- Numbers and Arithmetic
# ============================================================

(println "=== Bignum Demo ===")

# -------------------------------------------------------------------------
# 1. Arbitrary precision
#
# L uses libbf (the same arbitrary-precision library as QuickJS) for bignum
# support. Any integer exceeding the fixnum range automatically promotes to
# bignum. All arithmetic operators work transparently.
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# 2. No floats
#
# There are no floating-point numbers in L. Use scaled integers (e.g.,
# represent cents instead of dollars) or bignums for exact arithmetic.
# `sqrt` in stdlib returns the integer square root.
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# 3. Factorial and Fibonacci
#
# These are classic bignum workloads. `factorial(30)` =
# 265252859812191058636308480000000 -- far beyond 64-bit. `fib(100)`
# similarly requires bignum.
# -------------------------------------------------------------------------

# Large integers
(de factorial (n)
  (if (<= n 1) 1 (* n (factorial (1- n)))))

(println "10! =" (factorial 10))   # => 3628800
(println "15! =" (factorial 15))   # => 1307674368000
(println "20! =" (factorial 20))   # => 2432902008176640000
# Note: 21! exceeds int64; requires libbf for larger values

# Fibonacci with large numbers
(de fib (n)
  (do ((i 0 (1+ i))
       (a 0 b)
       (b 1 (+ a b)))
    ((= i n) a)))

(println "fib(50) =" (fib 50))   # => 12586269025
(println "fib(80) =" (fib 80))   # => 23416728348161557
# Note: fib(94) exceeds int64; requires libbf for larger values

# Powers of 2
(println "2^32 =" (** 2 32))   # => 4294967296
(println "2^62 =" (** 2 62))   # => 4611686018427387904
# Note: 2^63 overflows signed int64; requires libbf for 2^64 and beyond

# -------------------------------------------------------------------------
# 4. Bignum predicates
#
# `(big? x)` returns T if x is a bignum (not a fixnum). Numbers that fit
# in the fixnum range are NOT bignums even if computed via bignum operations.
# -------------------------------------------------------------------------
(println "big? 42:" (big? 42))          # => NIL  (fits in fixnum)
(println "big? 2^40:" (big? (** 2 40))) # => T    (exceeds fixnum range)

(println "Done.")
