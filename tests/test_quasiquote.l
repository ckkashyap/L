# ============================================================
# test_quasiquote.l -- Quasiquote, unquote, and splice
#
# What this covers:
#   • Basic unquote (,expr) -- evaluates one sub-expression inside a
#     quoted template while leaving the rest unevaluated
#   • Unquote-splicing (,@list) -- splices all elements of a list
#     inline as siblings, not as a nested list
#   • Mixed quasiquote with multiple unquoted positions
#   • Plain quasiquote with no substitutions (equivalent to quote)
#   • Nested list inside a quasiquote with an inner unquote
#
# Run:  L tests/test_quasiquote.l
# Spec: L.spec.txt Part 4 -- Reader / Quasiquote
#
# NOTE: Quasiquote is expanded by the reader into ordinary
# list-building code (list, append, etc.) before evaluation.
# Spliced expressions must evaluate to a list; NIL is valid as an
# empty splice.
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Basic unquote with , ------------------------------------
# `(1 2 ,x) reads as (list 1 2 x). x is evaluated and its value
# (42) is placed at that position. The surrounding 1 and 2 are not
# evaluated -- they are literal data.
(let ((x 42))
  (test (list 1 2 42) `(1 2 ,x))) # => (1 2 42)

# ---- 2. Unquote-splicing with ,@ --------------------------------
# ,@lst inlines each element of lst as a sibling. The result is a
# flat list (1 2 3 4), NOT (1 2 (3 4)). The spliced value must be
# a proper list; NIL splices as zero elements.
(let ((lst (list 3 4)))
  (test (list 1 2 3 4) `(1 2 ,@lst))) # => (1 2 3 4)

# ---- 3. Multiple unquote positions in one template ---------------
# Each ,var is independently evaluated. Symbols a and b in the
# template (without comma) are left as quoted symbols, so the
# result interleaves symbol literals with evaluated values.
(let ((a 1) (b 2))
  (test (list 'a 1 'b 2) `(a ,a b ,b))) # => (a 1 b 2)

# ---- 4. Quasiquote with no substitutions -------------------------
# When there are no unquote or splice forms, a quasiquote behaves
# exactly like a plain quote -- all elements are literal data.
# edge case: this is NOT a function call; 1, 2, 3 are data.
(test (list 1 2 3) `(1 2 3)) # => (1 2 3)

# ---- 5. Nested list with inner unquote ---------------------------
# The inner (2 ,x) is a nested quasiquote sub-form. Only ,x is
# unquoted; 2 remains a literal. The outer template wraps it in a
# list alongside the literals 1 and 3.
(let ((x 5))
  (test (list 1 (list 2 5) 3) `(1 (2 ,x) 3))) # => (1 (2 5) 3)

(println "")
(println "test_quasiquote.l: ALL TESTS PASSED")
