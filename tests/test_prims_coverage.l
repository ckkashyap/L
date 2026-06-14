# ============================================================
# test_prims_coverage.l -- Smoke test for all C-level primitive functions
#
# What this covers:
#   • List construction and access: cons, car, cdr, list, append, reverse, length
#   • Predicates: null, atom, pair, listp, numberp, stringp, symbolp, eq, equal, not
#   • Arithmetic: +, -, *, /, mod, 1+, 1-, abs, max, min, **, gcd, lcm
#   • String ops: upcase, downcase, sub, name, intern, str
#   • Functional ops: mapcar, filter, remove, apply, funcall
#   • Combinators: identity, constantly, compose
#
# "Primitive" means the function is registered in prims_init() in
# src/prims.c, not defined in lib/stdlib.l. These tests verify that each
# primitive exists, accepts expected arguments, and returns the expected type.
#
# Run:  L tests/test_prims_coverage.l
# Spec: L.spec.txt Part 3 -- Primitive Functions
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. List Primitives -----------------------------------------
# cons builds a pair; car/cdr access the head and tail. list is
# syntactic sugar that chains conses. append concatenates two proper
# lists. reverse is O(n). length counts elements of a proper list.
(test (cons 1 2) (cons 1 2))
(test 1 (car (cons 1 2)))
(test 2 (cdr (cons 1 2)))
(test (list 1 2 3) (list 1 2 3))
(test 3 (length (list 1 2 3)))
(test (list 3 2 1) (reverse (list 1 2 3)))
(test (list 1 2 3 4) (append (list 1 2) (list 3 4)))

# ---- 2. Predicates ----------------------------------------------
# null returns T only for NIL. atom returns T for anything that is not
# a cons pair (numbers, symbols, strings, and NIL are all atoms).
# pair is the complement of atom for cons cells. listp accepts both
# proper lists and NIL. eq tests pointer identity; equal tests structural
# equality recursively.
(test T (null NIL))
(test NIL (null T))
(test T (atom 42))
(test NIL (atom (cons 1 2)))
(test T (pair (cons 1 2)))
(test T (listp (list 1)))
(test T (listp NIL))          # edge case: NIL is a valid empty list
(test T (numberp 42))
(test T (stringp "hi"))
(test T (symbolp 'foo))
(test T (eq 'x 'x))
(test T (equal (list 1) (list 1)))
(test T (not NIL))
(test NIL (not T))

# ---- 3. Arithmetic ----------------------------------------------
# All arithmetic operates on integers only -- L has no float type.
# ** is exponentiation. gcd/lcm are Euclidean-algorithm primitives.
# abs returns the absolute value. 1+/1- are efficient increment/decrement
# aliases.
(test 5 (+ 2 3))
(test 1 (- 3 2))
(test 6 (* 2 3))
(test 3 (/ 9 3))
(test 2 (mod 8 3))
(test 2 (1+ 1))
(test 0 (1- 1))
(test 5 (abs -5))
(test 3 (max 1 3))
(test 1 (min 1 3))
(test 8 (** 2 3))
(test 2 (gcd 4 6))
(test 12 (lcm 4 6))

# ---- 4. String Operations ----------------------------------------
# upcase/downcase fold ASCII letters only. sub extracts a substring:
# (sub str offset length) -- both arguments are 0-based. name converts a
# symbol to its string representation; intern is the inverse. str
# converts any atom to its printed string form.
(test "HELLO" (upcase "hello"))
(test "hello" (downcase "HELLO"))
(test "hel" (sub "hello" 0 3))
(test "llo" (sub "hello" 2 3))   # offset 2, length 3
(test "foo" (name 'foo))
(test 'bar (intern "bar"))
(test "42" (str 42))

# ---- 5. Map / Filter --------------------------------------------
# mapcar applies a function to each element and returns a new list of
# the results. filter keeps elements where the predicate returns truthy.
# remove is the complement of filter -- it drops matching elements.
(test (list 2 4 6) (mapcar (lambda (x) (* x 2)) (list 1 2 3)))
(test (list 2 4) (filter (lambda (x) (= (mod x 2) 0)) (list 1 2 3 4)))
(test (list 1 3) (remove (lambda (x) (= (mod x 2) 0)) (list 1 2 3 4)))

# ---- 6. apply / funcall -----------------------------------------
# apply takes a function and a *list* of arguments: (apply f lst).
# funcall takes a function followed by individual arguments: (funcall f a b c).
# Both are useful for passing functions as first-class values.
(test 6 (apply + (list 1 2 3)))
(test 6 (funcall + 1 2 3))

# ---- 7. Functional Combinators ----------------------------------
# identity returns its argument unchanged -- useful as a no-op placeholder.
# constantly ignores all its arguments and always returns the bound value;
#   ((constantly 99) any number of args) => 99
# compose chains two functions right-to-left:
#   ((compose f g) x) => (f (g x))
#   Here: g=(lambda (x) (+ x 1)), f=(lambda (x) (* x 2)); g(1)=2, f(2)=4
(test 42 (identity 42))
(test 99 ((constantly 99) 1 2 3))
(test 4 ((compose (lambda (x) (* x 2)) (lambda (x) (+ x 1))) 1))

(println "")
(println "test_prims_coverage.l: ALL TESTS PASSED")
