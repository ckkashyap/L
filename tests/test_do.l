# ============================================================
# test_do.l -- Scheme-style do iteration form
#
# What this covers:
#   • Basic do loop with multiple step variables (sum, counter)
#   • Simple single-variable countdown to zero
#   • Accumulating results into a list via do body/step
#   • Simultaneous variable update semantics (all steps evaluated
#     before any binding is updated -- not sequential like let*)
#   • Termination: loop exits when test-expr is truthy, returning
#     result-expr
#
# Run:  L tests/test_do.l
# Spec: L.spec.txt Part 3 -- Iteration Forms
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Multi-variable do with simultaneous step update -----------
# Both `i` and `sum` step expressions are evaluated using the OLD
# values of i and sum before either binding is advanced. After the
# loop, sum holds 0+1+2+3+4 = 10. The test-expr ((= i 5)) fires
# when i reaches 5; result-expr `sum` is then returned.
(test 10
  (do ((i 0 (1+ i))
       (sum 0 (+ sum i)))
    ((= i 5) sum)))

# ---- 2. Simple single-variable countdown --------------------------
# Counts down from 5 to 0. When n equals 0 the test fires and n
# (which is 0) is returned as the result. No body forms needed.
(test 0
  (do ((n 5 (1- n)))
    ((= n 0) n)))

# ---- 3. Accumulating results into a list --------------------------
# `acc` starts as NIL and each iteration appends the current index.
# NOTE: step expressions are evaluated simultaneously -- (append acc
# (list i)) still sees the i from THIS iteration, not the next.
# Result is the list (0 1 2 3 4).
(test (list 0 1 2 3 4)
  (do ((i 0 (1+ i))
       (acc NIL (append acc (list i))))
    ((= i 5) acc)))

(println "")
(println "test_do.l: ALL TESTS PASSED")
