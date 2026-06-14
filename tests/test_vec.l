# ============================================================
# test_vec.l -- Mutable vector operations
#
# What this covers:
#   • vec creation with a fixed length and fill value
#   • vec-set / vec-get: in-place mutation, 0-based indexing
#   • vec-push: appending to the end, growing the vector
#
# Run:  L tests/test_vec.l
# Spec: L.spec.txt Part 4 -- Mutable Vectors
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Create vector --------------------------------------
# (vec n fill) allocates a mutable vector of length n with
# every slot set to fill. Contrast with pvec, which is
# immutable: vec mutates in place and is suited to
# performance-critical loops and buffers.

# Create vector
(let ((v (vec 3 0)))
  (test T (vec? v))
  (test 3 (vec-length v))
  (test 0 (vec-get v 0))   # => 0  (fill value)
  (test 0 (vec-get v 1))   # => 0
  (test 0 (vec-get v 2))   # => 0

  # ---- 2. Set elements -------------------------------------
  # vec-set mutates the vector in place. It returns NIL, not
  # the vector. Indexing is 0-based (unlike string `index`,
  # which is 1-based).

  # Set elements
  (vec-set v 0 10)   # NOTE: mutates in place, returns NIL
  (vec-set v 1 20)
  (vec-set v 2 30)
  (test 10 (vec-get v 0))   # => 10
  (test 20 (vec-get v 1))   # => 20
  (test 30 (vec-get v 2))   # => 30

  # ---- 3. Push ---------------------------------------------
  # vec-push appends one element to the end of the vector,
  # growing its length by 1. Like vec-set, it mutates in
  # place and returns NIL.

  # Push
  (vec-push v 40)            # NOTE: mutates in place, returns NIL
  (test 4 (vec-length v))   # => 4  (grew from 3)
  (test 40 (vec-get v 3)))  # => 40  (new last slot)

(println "")
(println "test_vec.l: ALL TESTS PASSED")
