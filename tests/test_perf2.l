# ============================================================
# test_perf2.l -- Performance baseline: doubly-recursive Fibonacci
#
# What this covers:
#   • Interpreter call throughput under a non-tail-recursive workload
#   • Absence of catastrophic slowdowns in the core eval loop
#
# fib(30) requires approximately 2.7 million recursive calls and produces
# the value 832040. This is intentionally NOT tail-recursive -- both
# branches of the recursion are live simultaneously -- so TCO does not
# apply. The test exercises raw call/return overhead and integer arithmetic.
#
# The correctness check (result = 832040) doubles as a timing gate: if the
# interpreter is too slow to finish in a reasonable time the test runner
# will time out in CI before reaching the println. The 5-second threshold
# is conservative enough to pass on slow CI hardware.
#
# If this test fails or times out, profile eval.c for call overhead or
# check for a regression in integer arithmetic.
#
# Run:  L tests/test_perf2.l
# Spec: L.spec.txt Part 6 -- Performance Requirements
# ============================================================

# NOTE: fib is doubly recursive -- TCO does NOT apply here.
# Call tree size: ~2.7 million calls for fib(30).
(de fib (n)
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

# => 832040  (fib 30)
(let ((result (fib 30)))
  (if (= result 832040)
    (println "test_perf2.l: ALL TESTS PASSED")
    (println (list "FAIL: fib(30) =" result "expected 832040"))))
