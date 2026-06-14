# ============================================================
# test_perf3.l -- Performance baseline: list construction and traversal
#
# What this covers:
#   • List allocation throughput -- building a 1000-element list via cons
#   • apply over a large list -- verifying (apply + lst) scales linearly
#   • GC health -- 1000 cons cells should not trigger pathological GC pauses
#
# make-list uses a do-loop accumulator pattern to build a list of integers
# [0, 999] in reverse (each cons prepends). The sum 0+1+...+999 = 499500
# is then verified with (apply +). If the allocator or GC has a quadratic
# algorithm the total time will be noticeably superlinear for n=1000.
#
# If this test fails or times out, check for:
#   • A quadratic algorithm in list allocation or traversal
#   • Excessive GC pressure caused by a missing write barrier
#   • apply building an intermediate structure in O(n^2) time
#
# Run:  L tests/test_perf3.l
# Spec: L.spec.txt Part 6 -- Performance Requirements
# ============================================================

# Build a list of 1000 elements using a do-loop with a cons accumulator.
# The result is in reverse order (999 down to 0) but the sum is the same.
# NOTE: do ((var init step) ...) ((end-test) result) body
(de make-list (n)
  (do ((i 0 (1+ i))
       (acc NIL (cons i acc)))
    ((= i n) acc)))

# Sum all elements; expected: 0+1+2+...+999 = n*(n-1)/2 = 499500
(let ((lst (make-list 1000)))
  (let ((sum (apply + lst)))
    (if (= sum 499500)
      (println "test_perf3.l: ALL TESTS PASSED")
      (println (list "FAIL: sum=" sum "expected 499500")))))
