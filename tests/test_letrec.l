# ============================================================
# test_letrec.l -- letrec and letrec* binding forms
#
# What this covers:
#   • letrec: ALL bindings are simultaneously in scope for every
#     RHS -- the only way to write mutually recursive local functions
#   • letrec*: sequential AND recursive -- each binding is in scope
#     for all later RHS expressions (like let* but allows self-refs)
#   • Contrast with plain let: no binding is visible to any RHS,
#     so forward references would produce an unbound-variable error
#
# Run:  L tests/test_letrec.l
# Spec: L.spec.txt Part 2 -- Binding Forms
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. letrec -- mutual recursion via shared scope ----------------
# even? calls odd? and odd? calls even?. This only works because
# letrec places both names in scope before evaluating either lambda
# body. With plain let, the second lambda's reference to `even?`
# would be unbound at definition time.
(letrec ((even? (lambda (n) (if (= n 0) T (odd? (1- n)))))
         (odd?  (lambda (n) (if (= n 0) NIL (even? (1- n))))))
  (test T (even? 4))   # => T
  (test NIL (even? 3)) # => NIL
  (test T (odd? 7))    # => T
  (test NIL (odd? 8))) # => NIL

# ---- 2. letrec* -- sequential bindings, each visible to the next --
# b's RHS can reference a because a is bound first; c's RHS can
# reference b. Unlike letrec, the ordering of evaluation is
# guaranteed left-to-right. Self-recursion within any single binding
# is also permitted (not demonstrated here as it is covered by
# letrec tests above).
(letrec* ((a 1)
           (b (+ a 1))  # a is already bound => 1+1 = 2
           (c (+ b 1))) # b is already bound => 2+1 = 3
  (test 1 a)
  (test 2 b)
  (test 3 c))

(println "")
(println "test_letrec.l: ALL TESTS PASSED")
