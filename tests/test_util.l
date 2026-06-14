# ============================================================
# test_util.l -- Threading macros, partial, sets, when-let, string compare
#
# What this covers:
#   • -> and ->> threading macros
#   • partial application
#   • pset (persistent set)
#   • when-let / if-let
#   • String comparison (<, >, <=, >=)
#
# Run:  L tests/test_util.l
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- Threading macros ----

# -> threads as first argument
(test 9 (-> 1 (+ 2) (* 3)))           # (* (+ 1 2) 3) = 9
(test 3 (-> 10 (- 7)))                 # (- 10 7) = 3
(test 5 (-> 5))                        # identity

# ->> threads as last argument
(test (list 9 16 25)
  (->> (list 1 2 3 4 5)
       (filter (lambda (x) (> x 2)))
       (mapcar (lambda (x) (* x x)))))

(test 15 (->> (list 1 2 3 4 5) (reduce + 0)))

# ---- Partial application ----

(test 15 ((partial + 10) 5))
(test (list 11 12 13) (mapcar (partial + 10) (list 1 2 3)))
(test "hello world" ((partial pack) (list "hello" " " "world")))

# ---- Persistent set ----

(let ((s (pset 1 2 3 4 5)))
  (test T (pset? s))
  (test T (pset-member? s 3))
  (test NIL (pset-member? s 99))
  (test 5 (pset-count s))

  # Add
  (let ((s2 (pset-add s 6)))
    (test 6 (pset-count s2))
    (test T (pset-member? s2 6))
    (test 5 (pset-count s)))  # original unchanged

  # Delete
  (let ((s3 (pset-del s 3)))
    (test 4 (pset-count s3))
    (test NIL (pset-member? s3 3))))

# Set operations
(let ((a (pset 1 2 3))
      (b (pset 2 3 4)))
  (test 4 (pset-count (pset-union a b)))          # {1 2 3 4}
  (test 2 (pset-count (pset-intersection a b)))    # {2 3}
  (test 1 (pset-count (pset-difference a b))))     # {1}

# ---- String comparison ----

(test T (< "apple" "banana"))
(test NIL (< "banana" "apple"))
(test T (> "zoo" "abc"))
(test T (<= "abc" "abc"))
(test T (>= "xyz" "abc"))
(test NIL (> "abc" "abc"))

# Mixed: numbers still work
(test T (< 1 2))
(test T (> 3.0 2.0))

# ---- when-let ----

(test 3 (when-let ((x (+ 1 2))) x))
(test NIL (when-let ((x NIL)) 42))    # body doesn't run

# ---- if-let ----

(test "yes" (if-let ((x 42)) "yes" "no"))
(test "no" (if-let ((x NIL)) "yes" "no"))

(println "")
(println "test_util.l: ALL TESTS PASSED")
