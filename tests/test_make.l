# ============================================================
# test_make.l -- make / link / chain list accumulator
#
# What this covers:
#   • make: sets up an implicit accumulator; its result is the
#     built list
#   • link: appends a single element at the tail (forward order,
#     unlike cons which adds at the head)
#   • chain: splices an entire list into the accumulator
#   • Scope: link/chain only work inside a make body
#
# Run:  L tests/test_make.l
# Spec: L.spec.txt Part 3 -- List Construction
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Basic make / link ----------------------------------
# make evaluates its body forms for side effects and returns
# the accumulated list. link appends one element at the tail
# on each call, so the output list preserves the call order
# without requiring an explicit reverse at the end.

# Basic make/link
(test (list 1 2 3)
  (make
    (link 1)
    (link 2)
    (link 3)))

# ---- 2. make with chain ------------------------------------
# chain splices a whole list into the accumulator, extending
# it rather than nesting it. Mixing link and chain in one
# make body is the idiomatic way to combine individual
# elements with sub-lists in forward order.

# make with chain
(test (list 1 2 3 4 5)
  (make
    (link 1)
    (chain (list 2 3 4))   # splices 2, 3, 4 -- not a nested list
    (link 5)))

# ---- 3. Empty make -----------------------------------------
# When the make body never calls link or chain the result is
# NIL, not an empty list -- there is no allocation.

# make returns NIL when nothing linked
(test NIL
  (make))

# ---- 4. Nested make ----------------------------------------
# Each make has its own private accumulator. The return value
# of an inner make is just a regular value in the outer make's
# body; unless the outer body explicitly links it, it is
# discarded.

# Nested make -- each has its own accumulator
(test (list 10 20)
  (make
    (link 10)
    (make (link 99))  # inner make's result is discarded
    (link 20)))

(println "")
(println "test_make.l: ALL TESTS PASSED")
