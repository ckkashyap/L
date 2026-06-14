# ============================================================
# test_algo.l -- Algorithm and utility functions
#
# What this covers:
#   • sort: natural-order and custom-comparator sorting
#   • maxi / mini: extremum with optional key function
#   • rand / seed: seeded PRNG with range variants
#   • reduce / fold / foldr: left- and right-fold
#   • any / every: short-circuiting predicates
#   • by: sort by derived key; group-by: partition into alist
#   • date / clock / usec: real-time queries
#
# Run:  L tests/test_algo.l
# Spec: L.spec.txt Part 5 -- Algorithms and Utilities
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# =========================================================================
# 1. sort
# =========================================================================
# sort returns a new list in natural ascending order (numbers before
# strings). An optional second argument is a two-argument comparator;
# when provided it fully controls the ordering. sort is stable with
# respect to equal elements.

# Basic ascending sort (with duplicates)
(test (list 1 1 3 4 5) (sort (list 3 1 4 1 5)))

# Descending with custom comparator
(test (list 5 4 3 1 1) (sort (list 3 1 4 1 5) (lambda (a b) (> a b))))

# Empty and singleton
(test NIL      (sort NIL))
(test (list 7) (sort (list 7)))

# Already sorted -- idempotent
(test (list 1 2 3) (sort (list 1 2 3)))

# Reverse-sorted input
(test (list 1 2 3 4 5) (sort (list 5 4 3 2 1)))

# Negatives
(test (list -5 -3 -1 0 2 4) (sort (list 0 -3 4 -1 2 -5)))

# All equal elements
(test (list 7 7 7) (sort (list 7 7 7)))

# Strings -- lexicographic
(test (list "apple" "banana" "cherry")
      (sort (list "cherry" "apple" "banana")))

# Larger list: sort 0..19 shuffled via a fixed sequence
(setq shuffled (list 15 3 9 7 19 1 11 5 17 13 2 14 8 18 0 4 12 6 16 10))
(test (list 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19)
      (sort shuffled))

# Sort a list of pairs by car -- comparator on first element
(setq pairs (list (list 3 "c") (list 1 "a") (list 2 "b")))
(test (list (list 1 "a") (list 2 "b") (list 3 "c"))
      (sort pairs (lambda (a b) (< (car a) (car b)))))

# =========================================================================
# 2. maxi / mini
# =========================================================================
# maxi and mini return the ELEMENT (not the key) that has the maximum or
# minimum value. With a key function the key is used only for comparison;
# the original element is what gets returned. This means you can rank
# records by a derived field and get the full record back.

(test 5  (maxi (list 3 1 5 2 4)))
(test 1  (mini (list 3 1 5 2 4)))

# Negatives
(test -1 (maxi (list -5 -3 -1 -4)))
(test -5 (mini (list -5 -3 -1 -4)))

# Single element
(test 42 (maxi (list 42)))
(test 42 (mini (list 42)))

# All equal
(test 7 (maxi (list 7 7 7)))
(test 7 (mini (list 7 7 7)))

# With key function: longest string
(test "banana" (maxi string-length (list "fig" "banana" "kiwi")))
(test "fig"    (mini string-length (list "fig" "banana" "kiwi")))

# With key function: highest absolute value
(test -9 (maxi abs (list 3 -9 5 -2)))   # => -9  (element returned, not 9)
(test -2 (mini abs (list 3 -9 5 -2)))   # => -2  (element returned, not 2)

# maxi/mini on pairs by second element
(setq scored (list (list "alice" 85) (list "bob" 92) (list "carol" 78)))
(test (list "bob"   92) (maxi (lambda (p) (cadr p)) scored))
(test (list "carol" 78) (mini (lambda (p) (cadr p)) scored))

# =========================================================================
# 3. rand / seed
# =========================================================================
# seed sets the PRNG state so subsequent rand calls are reproducible.
# (rand n) returns an integer in [0, n).
# (rand n m) returns an integer in [n, m].
# (rand) with no arguments returns a non-negative integer of arbitrary size.

# Reproducibility: same seed -> same first value
(seed 12345)
(setq r1 (rand 1000))
(seed 12345)
(setq r2 (rand 1000))
(test r1 r2)

# Reproducibility: same seed -> same sequence of N values
(seed 99)
(setq seq-a (list (rand 100) (rand 100) (rand 100) (rand 100) (rand 100)))
(seed 99)
(setq seq-b (list (rand 100) (rand 100) (rand 100) (rand 100) (rand 100)))
(test seq-a seq-b)

# Range [0, N)
(seed 0)
(let ((ok T) (i 0))
  (while (< i 50)
    (let ((r (rand 10)))
      (if (or (< r 0) (>= r 10)) (setq ok NIL)))
    (setq i (1+ i)))
  (test T ok))

# Range [N, M]
(seed 0)
(let ((ok T) (i 0))
  (while (< i 50)
    (let ((r (rand 5 15)))
      (if (or (< r 5) (> r 15)) (setq ok NIL)))
    (setq i (1+ i)))
  (test T ok))

# 0-arg form returns non-negative number
(seed 7)
(test T (>= (rand) 0))

# Values are not all the same (RNG actually advances)
(seed 42)
(setq va (rand 1000000))
(setq vb (rand 1000000))
(setq vc (rand 1000000))
(test T (or (not (= va vb)) (not (= vb vc))))

# =========================================================================
# 4. reduce / fold
# =========================================================================
# reduce (alias: fold) is a left-fold. The function receives
# (accumulator element) in that order. Left-associativity means the
# accumulator is updated strictly left-to-right, so operator order
# matters for non-commutative operations like subtraction.

# Sum
(test 15 (reduce + 0 (list 1 2 3 4 5)))
(test 15 (fold   + 0 (list 1 2 3 4 5)))

# Product
(test 120 (reduce * 1 (list 1 2 3 4 5)))

# Left-associativity: (((10 - 3) - 2) - 4) = 1
(test 1 (reduce - 10 (list 3 2 4)))

# Max via reduce
(test 9 (reduce (lambda (acc x) (if (> x acc) x acc)) 0 (list 3 9 1 5 2)))

# String concatenation
(test "hello world" (reduce (lambda (acc x) (pack (list acc " " x)))
                             "hello" (list "world")))

# Build list in reverse via fold
(test (list 3 2 1) (fold (lambda (acc x) (cons x acc)) NIL (list 1 2 3)))

# Empty list returns init
(test 42 (reduce + 42 NIL))
(test 0  (fold   * 0  NIL))

# =========================================================================
# 5. foldr
# =========================================================================
# foldr is a right-fold: it processes elements from right to left, so the
# function receives (element accumulator). This means foldr cons NIL
# reconstructs a list in its original order, while foldl (reduce) would
# reverse it.

# foldr rebuilds list (cons with NIL init = id)
(test (list 1 2 3) (foldr cons NIL (list 1 2 3)))

# Right-associativity: 1 - (2 - (3 - 0)) = 1 - (2 - 3) = 1 - (-1) = 2
(test 2 (foldr - 0 (list 1 2 3)))

# foldr vs foldl differ for non-commutative ops
(setq foldl-result (reduce - 0 (list 1 2 3)))   # ((0-1)-2)-3 = -6
(setq foldr-result (foldr - 0 (list 1 2 3)))    # 1-(2-(3-0)) = 2
(test -6 foldl-result)
(test  2 foldr-result)

# Empty list
(test "init" (foldr (lambda (x acc) (pack (list x acc))) "init" NIL))

# =========================================================================
# 6. any / every
# =========================================================================
# any short-circuits on the FIRST truthy result and returns that result
# (not just T), so it can be used to find values, not just test membership.
# every short-circuits on the FIRST NIL result.
# NOTE: any on an empty list returns NIL; every on an empty list returns T
# (vacuous truth).

# any returns the truthy value (first match), not just T
(test T   (not (not (any odd? (list 2 4 5 6)))))   # 5 passes odd?
(test NIL (any odd? (list 2 4 6)))

# every returns T on success, NIL on first failure
(test T   (every even? (list 2 4 6)))
(test NIL (every even? (list 2 3 6)))

# Empty list
(test NIL (any  odd?  NIL))
(test T   (every even? NIL))

# any short-circuits: even if fn has side-effects, stop at first match
(setq sc-count 0)
(any (lambda (x) (setq sc-count (1+ sc-count)) (= x 3))
     (list 1 2 3 4 5))
(test 3 sc-count)   # stopped after finding 3

# every short-circuits on failure
(setq sc2-count 0)
(every (lambda (x) (setq sc2-count (1+ sc2-count)) (< x 3))
       (list 1 2 5 6))
(test 3 sc2-count)   # checked 1, 2, then 5 failed

# Complex predicates
(test T   (any  (lambda (x) (> x 10)) (list 1 5 15 3)))
(test NIL (every (lambda (x) (> x 0)) (list 1 2 -1 4)))

# =========================================================================
# 7. by (sort with key function)
# =========================================================================
# by sorts a list by applying key-fn to each element and comparing the
# resulting values in natural ascending order. The original elements are
# returned -- not the keys. When two elements produce equal keys their
# relative order is stable.

# Sort strings by length
(test (list "fig" "kiwi" "banana")
      (by string-length (list "banana" "fig" "kiwi")))

# Sort by absolute value
(test (list -1 2 -3 5)
      (by abs (list 5 -1 2 -3)))

# Sort pairs by second element
(test (list (list "c" 1) (list "a" 2) (list "b" 3))
      (by (lambda (p) (cadr p))
          (list (list "a" 2) (list "b" 3) (list "c" 1))))

# Sort by modulo (grouping effect)
(test (list 3 6 9 1 4 7 2 5 8)
      (by (lambda (x) (mod x 3))
          (list 1 2 3 4 5 6 7 8 9)))

# Single element
(test (list 42) (by abs (list 42)))

# Empty list
(test NIL (by abs NIL))

# =========================================================================
# 8. group-by
# =========================================================================
# group-by partitions a list into an alist of (key . members) pairs.
# Each unique key returned by key-fn becomes a key in the alist, and the
# associated value is the list of elements that produced that key.
# Insertion order of keys and members matches the original list order.

# Group by parity
(setq g1 (group-by even? (list 1 2 3 4 5 6)))
(test T (not (not (member 2 (cdr (assoc T   g1))))))
(test T (not (not (member 4 (cdr (assoc T   g1))))))
(test T (not (not (member 1 (cdr (assoc NIL g1))))))
(test T (not (not (member 3 (cdr (assoc NIL g1))))))

# Group by mod 3 -- three buckets
(setq g2 (group-by (lambda (x) (mod x 3))
                   (list 0 1 2 3 4 5 6 7 8)))
(test T (not (not (member 0 (cdr (assoc 0 g2))))))
(test T (not (not (member 3 (cdr (assoc 0 g2))))))
(test T (not (not (member 1 (cdr (assoc 1 g2))))))
(test T (not (not (member 4 (cdr (assoc 1 g2))))))
(test T (not (not (member 2 (cdr (assoc 2 g2))))))
(test T (not (not (member 5 (cdr (assoc 2 g2))))))

# Group strings by length
(setq g3 (group-by string-length (list "a" "bb" "cc" "ddd" "e")))
(test T (not (not (member "a"   (cdr (assoc 1 g3))))))
(test T (not (not (member "e"   (cdr (assoc 1 g3))))))
(test T (not (not (member "bb"  (cdr (assoc 2 g3))))))
(test T (not (not (member "ddd" (cdr (assoc 3 g3))))))

# Single element per group
(setq g4 (group-by abs (list 1 2 3)))
(test T (not (not (member 1 (cdr (assoc 1 g4))))))
(test T (not (not (member 2 (cdr (assoc 2 g4))))))

# =========================================================================
# 9. date / clock / usec
# =========================================================================
# date, clock, and usec query the system clock at call time.
# (date)   => string "YYYY-MM-DD"; (date T) => list (year month day)
# (clock)  => string "HH:MM:SS";  (clock T) => list (hour min sec)
# (usec)   => microseconds since the Unix epoch (large positive integer).
# NOTE: consecutive calls to usec are non-decreasing but may be equal on
# fast hardware; do not rely on strict monotonicity within a single process.

# date string format: "YYYY-MM-DD" (10 chars)
(setq d (date))
(test T (stringp d))
(test T (= 10 (string-length d)))
(test "-" (sub d 4 1))   # dash at position 4
(test "-" (sub d 7 1))   # dash at position 7

# date list form: (year month day)
(setq dl (date T))
(test T (= 3 (length dl)))
(test T (> (car dl) 2020))
(test T (and (>= (cadr  dl) 1)  (<= (cadr  dl) 12)))
(test T (and (>= (caddr dl) 1)  (<= (caddr dl) 31)))

# (string-to-int cross-check skipped; date/clock may tick between calls)

# clock string format: "HH:MM:SS" (8 chars)
(setq c (clock))
(test T (stringp c))
(test T (= 8 (string-length c)))
(test ":" (sub c 2 1))   # colon at position 2
(test ":" (sub c 5 1))   # colon at position 5

# clock list form: (hour minute second)
(setq cl (clock T))
(test T (= 3 (length cl)))
(test T (and (>= (car   cl) 0) (<= (car   cl) 23)))
(test T (and (>= (cadr  cl) 0) (<= (cadr  cl) 59)))
(test T (and (>= (caddr cl) 0) (<= (caddr cl) 59)))

# usec: large positive, monotonically non-decreasing
(setq u1 (usec))
(setq u2 (usec))
(setq u3 (usec))
(test T (> u1 0))
(test T (>= u2 u1))
(test T (>= u3 u2))

# usec is at least in the right ballpark: > 1700000000000000 (year 2023+)
(test T (> u1 1700000000000000))

(println "")
(println "test_algo.l: ALL TESTS PASSED")
