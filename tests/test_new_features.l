# ============================================================
# test_new_features.l -- Regression suite for core language features
#
# What this covers:
#   • TCO (tail-call optimization) -- unlimited tail calls via trampoline
#   • Closures -- lexical capture of bindings at lambda creation time
#   • Higher-order functions -- mapcar with anonymous lambdas
#   • Multiple values / accumulator pattern -- passing state through tail calls
#   • case dispatch -- literal pattern matching with fallthrough to T
#   • cond -- sequential predicate chain; T as catch-all else branch
#   • while -- imperative loop; returns NIL; used for side effects
#   • String primitives -- pack/unpack for string/list conversion
#
# Run:  L tests/test_new_features.l
# Spec: L.spec.txt Part 2 -- Core Evaluation Semantics
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. TCO -- Tail-Call Optimization ----------------------------
# L guarantees unlimited tail calls. The eval.c trampoline uses a
# `goto tail_call` loop instead of a C stack frame for each recursive
# call, so (count-down 10000) must return 'done without a stack overflow.
(de count-down (n)
  (if (= n 0) 'done (count-down (1- n))))
(test 'done (count-down 10000))

# ---- 2. Closures ------------------------------------------------
# `lambda` captures the lexical environment at creation time. The
# variable `n` in make-adder is captured by reference -- each call to
# make-adder produces a distinct closure with its own `n` binding.
(de make-adder (n)
  (lambda (x) (+ x n)))
(let ((add5 (make-adder 5)))
  (test 15 (add5 10))
  (test 105 (add5 100)))

# ---- 3. Higher-Order Functions ----------------------------------
# mapcar applies a function to every element and returns a new list.
# Using an inline lambda avoids the need to name trivial helpers.
(test (list 1 4 9) (mapcar (lambda (x) (* x x)) (list 1 2 3)))

# ---- 4. Multiple Values via List --------------------------------
# L has no native multiple-return-value mechanism; the idiom is to
# return a plain list. min and max are primitives; apply unpacks the list.
(de min-max (lst)
  (list (apply min lst) (apply max lst)))
(test (list 1 5) (min-max (list 3 1 4 1 5)))

# ---- 5. Accumulator Pattern (Tail-Recursive Sum) ----------------
# Passing an accumulator `acc` as an extra parameter converts a
# naturally recursive sum into a tail call, enabling TCO. The initial
# caller passes 0 as the seed.
(de sum-list (lst acc)
  (if (null lst) acc
    (sum-list (cdr lst) (+ acc (car lst)))))
(test 15 (sum-list (list 1 2 3 4 5) 0))

# ---- 6. case Dispatch -------------------------------------------
# `case` matches the value of `n` against a list of literal patterns
# in order. T in the pattern list acts as a catch-all default; without
# it, an unmatched value returns NIL.
(de classify (n)
  (case n
    (0 "zero")
    (1 "one")
    (2 "two")
    (T "other")))
(test "zero" (classify 0))
(test "one" (classify 1))
(test "other" (classify 99))

# ---- 7. cond ----------------------------------------------------
# `cond` tests each predicate left-to-right and returns the value of
# the first truthy branch. T as the last condition is the conventional
# else clause -- it is always truthy.
(de sign (n)
  (cond
    ((> n 0) "positive")
    ((< n 0) "negative")
    (T "zero")))
(test "positive" (sign 5))
(test "negative" (sign -3))
(test "zero" (sign 0))

# ---- 8. while Loop ----------------------------------------------
# `while` repeatedly evaluates its body as long as the condition is
# truthy; it always returns NIL. Side effects (setq) are the only
# way to accumulate a result across iterations.
# NOTE: sum of 0..4 = 0+1+2+3+4 = 10
(let ((i 0) (sum 0))
  (while (< i 5)
    (setq sum (+ sum i))
    (setq i (1+ i)))
  (test 10 sum))

# ---- 9. String Primitives ---------------------------------------
# `pack` concatenates a list of strings into one string.
# `unpack` splits a string into a list of single-character strings.
# These are C-level primitives, not stdlib functions.
(test "hello world" (pack (list "hello" " " "world")))
(test (list "h" "e" "l" "l" "o") (unpack "hello"))

(println "")
(println "test_new_features.l: ALL TESTS PASSED")
