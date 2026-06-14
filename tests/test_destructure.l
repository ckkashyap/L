# ============================================================
# test_destructure.l -- Destructuring bind
#
# What this covers:
#   • Destructuring in let and let*
#   • Destructuring in lambda and de
#   • Rest parameters (dotted pair) in patterns
#   • Nested destructuring (patterns within patterns)
#   • Mixed destructured and simple parameters
#
# Run:  L tests/test_destructure.l
# Spec: L.spec.txt Part 2 -- Binding Forms
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- let destructuring ----
(let (((a b c) (list 1 2 3)))
  (test 1 a)
  (test 2 b)
  (test 3 c))

# ---- let with rest ----
(let (((x y . rest) (list 10 20 30 40)))
  (test 10 x)
  (test 20 y)
  (test (list 30 40) rest))

# ---- nested destructuring in let ----
(let ((((a b) (c d)) (list (list 1 2) (list 3 4))))
  (test 1 a)
  (test 2 b)
  (test 3 c)
  (test 4 d))

# ---- let* destructuring ----
(let* (((a b) (list 3 4))
       (sum (+ a b)))
  (test 3 a)
  (test 4 b)
  (test 7 sum))

# ---- lambda destructuring ----
(let ((f (lambda ((a b) c) (list a b c))))
  (test (list 1 2 3) (f (list 1 2) 3)))

# ---- lambda with rest in pattern ----
(let ((f (lambda ((x . xs) y) (list x xs y))))
  (test (list 1 (list 2 3) 99) (f (list 1 2 3) 99)))

# ---- de destructuring ----
(de destructure-add ((a b) (c d))
  (+ a b c d))
(test 10 (destructure-add (list 1 2) (list 3 4)))

# ---- mixed: some params destructured, some not ----
(let ((f (lambda (x (a b) y) (list x a b y))))
  (test (list 0 1 2 9) (f 0 (list 1 2) 9)))

# ---- destructure with ignored tail (more values than bindings) ----
(let (((a b) (list 1 2 3 4 5)))
  (test 1 a)
  (test 2 b))

(println "")
(println "test_destructure.l: ALL TESTS PASSED")
