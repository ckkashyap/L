# ============================================================
# test_guard.l -- Non-local exit via catch / throw
#
# What this covers:
#   • catch establishes a named escape point identified by a symbol
#   • throw unwinds the stack to the nearest catch with a matching
#     tag and delivers a value; forms after throw are NOT evaluated
#   • Tag matching uses eq (symbol identity), not structural equal
#   • Nested catch forms: inner catch intercepts its own tag and
#     does not propagate to the outer catch
#   • When no throw occurs, catch evaluates its body normally and
#     returns the value of the last body form
#
# Run:  L tests/test_guard.l
# Spec: L.spec.txt Part 5 -- Control Flow / Non-local Exit
#
# NOTE: This is NOT a condition/restart system. There is no way to
# resume execution from the throw site. If no matching catch exists
# anywhere on the stack, the interpreter will exit the process.
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Basic throw -- early exit with a delivered value -----------
# throw immediately unwinds to the catch tagged 'my-tag and makes
# the catch expression return 42. The literal 99 that follows throw
# is dead code and is never evaluated.
(test 42
  (catch 'my-tag
    (throw 'my-tag 42)
    99))  # 99 never reached

# ---- 2. Nested catch -- inner tag matched first --------------------
# throw 'inner is caught by the inner catch; the outer catch never
# sees the throw. The strings "inner not reached" and "outer not
# reached" are both dead code in their respective contexts.
# The outer catch body evaluates to whatever (catch 'inner ...) returns,
# which is "done" -- so the whole expression is "done".
(test "done"
  (catch 'outer
    (catch 'inner
      (throw 'inner "done")
      "inner not reached")
    "outer not reached"))

# ---- 3. No throw -- catch returns body value normally --------------
# When the body completes without any throw, catch is transparent:
# it returns the value of the last body expression as if catch
# were not there. Here (* 10 10) => 100.
(test 100
  (catch 'never-thrown
    (* 10 10)))

(println "")
(println "test_guard.l: ALL TESTS PASSED")
