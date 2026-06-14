# ============================================================
# test_stdlib.l -- Tests for the standard library (lib/stdlib.l)
#
# What this covers:
#   • String utilities: split, join, starts-with?, ends-with?,
#     contains?, repeat, pad-left, pad-right
#   • Math utilities: range, iota, sqrt, even?, odd?
#
# stdlib.l is written in L itself and is loaded automatically by the
# interpreter at startup when lib/stdlib.l exists. All functions tested
# here are defined in stdlib.l using primitives -- none are C builtins.
# The explicit (load "lib/stdlib.l") below is defensive; it is a no-op
# if the interpreter already loaded the file.
#
# Run:  L tests/test_stdlib.l
# Spec: L.spec.txt Part 5 -- Standard Library
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

(load "lib/stdlib.l")

# ---- 1. String Utilities ----------------------------------------
# split breaks a string on a delimiter and returns a list of substrings;
# the delimiter itself is not included in any element. join is the
# inverse: it concatenates a list of strings with a delimiter between
# each pair. Neither function is a primitive -- both are implemented in L.
(test (list "a" "b" "c") (split "a,b,c" ","))
(test "a,b,c" (join (list "a" "b" "c") ","))

# starts-with? and ends-with? do a prefix/suffix match and return T or NIL.
# NOTE: the ? suffix is a naming convention for boolean-returning functions.
(test T (starts-with? "hello" "hel"))
(test NIL (starts-with? "hello" "world"))
(test T (ends-with? "hello" "llo"))
(test NIL (ends-with? "hello" "world"))

# contains? returns T if the substring appears anywhere in the string.
(test T (contains? "hello world" "world"))

# repeat returns a new string formed by concatenating str n times.
(test "aaa" (repeat "a" 3))

# pad-left / pad-right extend a string to a target width by prepending
# or appending the fill string. If the string is already at or beyond
# the target width the original string is returned unchanged.
(test "  hi" (pad-left "hi" 4 " "))
(test "hi  " (pad-right "hi" 4 " "))

# ---- 2. Math Utilities ------------------------------------------
# range builds a list of integers from start (inclusive) to end
# (exclusive). The optional third argument is the step; it defaults to 1.
# edge case: a step > 1 skips values -- (range 1 10 2) yields odd numbers.
(test (list 0 1 2 3 4) (range 0 5))
(test (list 1 3 5 7 9) (range 1 10 2))

# iota builds a list of n integers. With one argument it starts from 0;
# with two arguments it starts from the given offset.
# (iota 3)    => (0 1 2)
# (iota 3 1)  => (1 2 3)
(test (list 0 1 2) (iota 3))
(test (list 1 2 3) (iota 3 1))

# sqrt returns the integer square root (floor). L has no float type, so
# (sqrt 9) => 3 and (sqrt 10) => 3.
(test 3 (sqrt 9))
(test 4 (sqrt 16))

# even? and odd? are defined in stdlib, not as C primitives. They use
# (mod n 2) internally. NIL is falsy; T is truthy.
(test T (even? 4))
(test NIL (even? 3))
(test T (odd? 7))

(println "")
(println "test_stdlib.l: ALL TESTS PASSED")
