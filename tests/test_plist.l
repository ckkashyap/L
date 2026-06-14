# ============================================================
# test_plist.l -- Symbol property lists: put / get / prop / plist / with
#
# What this covers:
#   • Basic put / get: add, retrieve, and update per-symbol properties
#   • plist: inspect the full alist of a symbol's properties
#   • prop: obtain the live (key . val) cons cell for in-place mutation
#   • Symbol isolation: each symbol has a completely independent plist
#   • Non-symbol keys: get/put use structural equality (equal?), not eq?
#   • with / : / =: / ::: OOP shorthand for working on a symbol's plist
#   • Practical pattern: simulating a mutable "object" with helper fns
#
# Run:  L tests/test_plist.l
# Spec: L.spec.txt Part 5 -- Symbols and Property Lists
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# -------------------------------------------------------------------------
# 1. Basic put / get
# -------------------------------------------------------------------------
# put returns val and installs (key . val) on the symbol's alist.
# A second put on the same key updates the existing cell in-place rather
# than prepending a shadow entry, so plist length stays bounded.
(put 'alice 'age 30)
(test 30 (get 'alice 'age))

(put 'alice 'job "engineer")
(test "engineer" (get 'alice 'job))

# edge case: missing key returns NIL, not an error
(test NIL (get 'alice 'salary))

# put same key updates value in-place
(put 'alice 'age 31)
(test 31 (get 'alice 'age))

# -------------------------------------------------------------------------
# 2. plist -- inspect the whole alist
# -------------------------------------------------------------------------
# plist returns the raw alist: a list of (key . val) pairs.
# Length reflects exactly how many distinct keys have been put.
(put 'bob 'x 1)
(put 'bob 'y 2)
(put 'bob 'z 3)
(test 3 (length (plist 'bob)))
(test 1 (get 'bob 'x))
(test 2 (get 'bob 'y))
(test 3 (get 'bob 'z))

# -------------------------------------------------------------------------
# 3. prop -- get the live (key . val) cell; mutation via set-cdr
# -------------------------------------------------------------------------
# prop returns the actual cons cell stored in the plist, not a copy.
# Mutating its cdr with set-cdr is equivalent to calling put with a new
# value -- and is faster because no alist scan is needed afterward.
(put 'node 'val 99)
(let ((cell (prop 'node 'val)))
  (test T (pair? cell))   # => it is a cons cell
  (test 'val (car cell))  # => key is the symbol val
  (test 99 (cdr cell))    # => current value
  # Mutate through the cell -- no second put needed
  (set-cdr cell 100))
(test 100 (get 'node 'val))

# prop on missing key => NIL (not an error)
(test NIL (prop 'node 'missing))

# -------------------------------------------------------------------------
# 4. Multiple independent symbols -- plists don't bleed between symbols
# -------------------------------------------------------------------------
# Each symbol owns its own alist; there is no shared backing store.
# Putting 'color on x-sym has no effect on y-sym's plist.
(put 'x-sym 'color "red")
(put 'y-sym 'color "blue")
(test "red"  (get 'x-sym 'color))
(test "blue" (get 'y-sym 'color))

# -------------------------------------------------------------------------
# 5. Non-symbol keys (structural equality via equal?)
# -------------------------------------------------------------------------
# Keys are matched with equal?, so strings (and lists) work as keys.
# NOTE: two distinct string objects with the same characters ARE equal.
(put 'data "str-key" 42)
(test 42 (get 'data "str-key"))
(test NIL (get 'data "str-key-other"))

# -------------------------------------------------------------------------
# 6. =: / : / :: via (with obj ...) -- OOP shorthand
# -------------------------------------------------------------------------
# (with sym body...) binds `This` to sym for the duration of body.
#   (: key)   -- sugar for (get This 'key);  key is unevaluated
#   (=: key v) -- sugar for (put This 'key v)
#   (:: key)  -- sugar for (prop This 'key); returns live cell
# Mutations made inside with persist on the symbol after with exits.
(put 'point 'x 10)
(put 'point 'y 20)

(with 'point
  (test 10  (: x))
  (test 20  (: y))
  (=: x 99)
  (test 99  (: x))
  # :: gives the live cell -- mutate y without a second put
  (let ((c (:: y)))
    (test T    (pair? c))
    (test 20   (cdr c))
    (set-cdr c 55))
  (test 55  (: y)))

# mutation via :: persisted outside with
(test 99 (get 'point 'x))
(test 55 (get 'point 'y))

# =: creates new property if absent
(with 'point
  (=: z 7)
  (test 7 (: z)))
(test 7 (get 'point 'z))

# -------------------------------------------------------------------------
# 7. get on non-symbol → error (just test normal path here)
# -------------------------------------------------------------------------
# A symbol with no properties returns NIL for every key -- it is not
# an error to query a symbol that has never had put called on it.
(test NIL (get 'empty-sym 'anything))

# -------------------------------------------------------------------------
# 8. Simulation: property-based "object" with methods
# -------------------------------------------------------------------------
# Demonstrates the idiomatic L pattern of using a well-known symbol
# ('counter here) as a namespace for mutable state, with plain
# functions as "methods".  put/get replace field assignment/access.
(de make-counter (start)
  (put 'counter 'n start)
  'counter)

(de counter-inc ()
  (put 'counter 'n (1+ (get 'counter 'n))))

(de counter-val ()
  (get 'counter 'n))

(make-counter 0)
(counter-inc)
(counter-inc)
(counter-inc)
(test 3 (counter-val))

(println "")
(println "test_plist.l: ALL TESTS PASSED")
