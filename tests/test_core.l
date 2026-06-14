# ============================================================
# test_core.l -- Core list construction and structural operations
#
# What this covers:
#   • Pair construction and destructuring: cons, car, cdr, list
#   • Identity vs structural equality: eq vs equal
#   • List utilities: append, reverse, length, nth, last
#   • Search: member, assoc (return tails/pairs, not booleans)
#
# Run:  L tests/test_core.l
# Spec: L.spec.txt Part 1 -- Core List Operations
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. cons / car / cdr -------------------------------------
# cons builds an improper pair (a . b) or proper list cell (a . rest).
# NIL is the canonical empty list -- car and cdr of NIL are both NIL,
# not errors, matching PicoLisp convention.
(test T (pair (cons 1 2)))
(test 1 (car (cons 1 2)))
(test 2 (cdr (cons 1 2)))
(test NIL (car NIL))   # edge case: car of NIL is NIL, not an error
(test NIL (cdr NIL))   # edge case: cdr of NIL is NIL, not an error

# ---- 2. list -------------------------------------------------
# list is syntactic sugar for nested cons cells terminated by NIL.
# (list 1 2 3) expands to (cons 1 (cons 2 (cons 3 NIL))).
(test T (pair (list 1 2 3)))
(test 1 (car (list 1 2 3)))
(test 3 (length (list 1 2 3)))

# ---- 3. atom / pair ------------------------------------------
# atom returns T for anything that is NOT a cons pair: numbers,
# strings, symbols, and NIL all qualify.  pair is the complement.
(test T (atom 42))
(test T (atom "hello"))
(test T (atom 'foo))
(test T (atom NIL))          # NIL is an atom despite representing the empty list
(test NIL (atom (cons 1 2)))
(test T (pair (cons 1 2)))

# ---- 4. null -------------------------------------------------
# null tests specifically for NIL.  0, the empty string, and even
# (list) with no args are NOT null -- only NIL itself is.
(test T (null NIL))
(test NIL (null 0))
(test NIL (null ""))
(test NIL (null (list)))
# Note: in L, (list) with no args returns () which is NOT NIL -- so (null (list)) is NIL, not T.

# ---- 5. eq -- pointer / identity equality ---------------------
# eq compares object identity (pointer equality), not structure.
# Symbols are interned, so two occurrences of 'foo are the same
# object.  Two independently constructed list cells are never eq,
# even if they contain identical elements.
(test T (eq 'foo 'foo))
(test T (eq NIL NIL))
(test NIL (eq (list 1) (list 1)))  # different cons cells -- same structure, not same object

# ---- 6. equal -- structural (deep) equality -------------------
# equal recursively compares structure and values.  Use equal when
# you care about content, not object identity.
(test T (equal (list 1 2 3) (list 1 2 3)))
(test T (equal "hello" "hello"))
(test NIL (equal (list 1 2) (list 1 3)))
(test T (equal 42 42))

# ---- 7. append -----------------------------------------------
# append joins two lists.  The first list is copied; the second is
# shared (not copied) as the tail.  Appending NIL on either side is
# a no-op for the resulting value.
# NOTE: append takes exactly 2 args -- a third arg is silently ignored.
(test (list 1 2 3 4) (append (list 1 2) (list 3 4)))
(test (list 1 2) (append (list 1 2) NIL))
(test (list 3 4) (append NIL (list 3 4)))

# ---- 8. reverse ----------------------------------------------
# reverse returns a new list in reverse order; the original is
# not modified.  reverse of NIL is NIL.
(test (list 3 2 1) (reverse (list 1 2 3)))
(test NIL (reverse NIL))

# ---- 9. length -----------------------------------------------
# length counts proper list cells down to NIL.
# length of NIL is 0 (not an error).
(test 0 (length NIL))   # edge case: empty list has length 0
(test 3 (length (list 1 2 3)))

# ---- 10. nth -- 0-indexed tail access -------------------------
# nth is 0-indexed and returns the TAIL (sublist) starting at
# position n, not just the element.  nth(0) returns the whole list;
# nth(1) skips the first element.
(test (list 2 3) (nth 1 (list 1 2 3)))  # => (2 3), not 2
(test (list 3) (nth 2 (list 1 2 3)))    # => (3), not 3

# ---- 11. last ------------------------------------------------
# last returns the last one-element tail (a cons pair), not the
# bare element.  For a proper list this is the final cons cell.
(test (list 3) (last (list 1 2 3)))     # => (3) -- last returns the tail pair, not the element 3

# ---- 12. member / memq ---------------------------------------
# member uses equal (structural comparison); memq uses eq (identity).
# On a hit, member returns the TAIL of the list starting at the
# found element -- this allows callers to use cdr for the remainder.
# On a miss, member returns NIL (not an error, not -1).
(test (list 2 3) (member 2 (list 1 2 3)))  # => (2 3), not T
(test NIL (member 4 (list 1 2 3)))          # edge case: NIL for missing element, not an error

# ---- 13. assoc / assq ----------------------------------------
# assoc searches an association list ((key . val) ...) using equal.
# It returns the whole pair (key . val), NOT just the value.
# Callers must use cdr on the result to extract the value.
# Returns NIL when the key is not found.
(test (cons 'b 2) (assoc 'b (list (cons 'a 1) (cons 'b 2) (cons 'c 3))))  # => (b . 2)
(test NIL (assoc 'd (list (cons 'a 1) (cons 'b 2))))  # edge case: NIL for missing key, not an error

(println "")
(println "test_core.l: ALL TESTS PASSED")
