# ============================================================
# 26-destructuring.l -- Destructuring Bind
#
# What this covers:
#   1. What is destructuring?
#   2. Destructuring in let / let*
#   3. Destructuring in lambda
#   4. Destructuring in de (named functions)
#   5. Rest parameters in destructuring patterns
#   6. Nested destructuring
#   7. Real-world patterns
#
# Run:  L demos/26-destructuring.l
# ============================================================

(println "=== Destructuring Bind ===")
(println "")

# ---- 1. What is destructuring? -----------------------------
#
# Destructuring lets you bind multiple variables at once by
# matching a pattern against a data structure. Instead of
# extracting elements one by one with car/cdr/nth, you
# declare the shape you expect and the interpreter pulls
# it apart for you.
#
# If you come from Clojure: this is like [a b c] binding
# but with list syntax (a b c).
# If you come from Python: this is like a, b, c = my_list.
# If you come from JavaScript: this is like const [a, b] = arr.

# ---- 2. Destructuring in let / let* ------------------------

(println "--- let / let* ---")

# Basic: bind three variables from a list
(let (((a b c) (list 10 20 30)))
  (println "  a=" a " b=" b " c=" c))     # => a=10 b=20 c=30

# let*: earlier bindings visible to later ones
(let* (((x y) (list 3 4))
       (sum (+ x y))
       (product (* x y)))
  (println "  x=" x " y=" y " sum=" sum " product=" product))

# Mix destructured and simple bindings
(let ((name "Alice")
      ((x y) (list 1 2)))
  (println "  name=" name " x=" x " y=" y))

# ---- 3. Destructuring in lambda ----------------------------

(println "")
(println "--- lambda ---")

# A function that takes a point (pair) and a scalar
(setq scale-point
  (lambda ((x y) factor)
    (list (* x factor) (* y factor))))

(println "  scale (3 4) by 2: " (scale-point (list 3 4) 2))

# Swap function using destructuring
(setq swap (lambda ((a b)) (list b a)))
(println "  swap (1 2): " (swap (list 1 2)))

# ---- 4. Destructuring in de --------------------------------

(println "")
(println "--- de (named functions) ---")

# Named function with destructured first argument
(de distance ((x1 y1) (x2 y2))
  (let* ((dx (- x2 x1))
         (dy (- y2 y1)))
    (+ (* dx dx) (* dy dy))))

(println "  distance² (0,0)→(3,4): " (distance (list 0 0) (list 3 4)))

# Process a key-value pair
(de describe-pair ((k . v))
  (pack (list k " => " v)))

(println "  pair: " (describe-pair (cons "name" "Bob")))

# ---- 5. Rest parameters ------------------------------------

(println "")
(println "--- rest parameters ---")

# Take the first element and collect the rest
(let (((head . tail) (list 1 2 3 4 5)))
  (println "  head=" head " tail=" tail))

# First two, rest collected
(let (((a b . rest) (list 10 20 30 40 50)))
  (println "  a=" a " b=" b " rest=" rest))

# In a function: split a list into head and tail
(de head-tail ((h . t)) (list h t))
(println "  head-tail: " (head-tail (list 'a 'b 'c)))

# ---- 6. Nested destructuring -------------------------------

(println "")
(println "--- nested patterns ---")

# Destructure a list of pairs
(let ((((a b) (c d)) (list (list 1 2) (list 3 4))))
  (println "  a=" a " b=" b " c=" c " d=" d))

# Deeply nested
(let (((x (y (z))) (list 1 (list 2 (list 3)))))
  (println "  x=" x " y=" y " z=" z))

# ---- 7. Real-world patterns --------------------------------

(println "")
(println "--- real-world patterns ---")

# Pattern: processing a list of records
(setq people (list
  (list "Alice" 30 "engineer")
  (list "Bob"   25 "designer")
  (list "Carol" 35 "manager")))

(println "  Team:")
(mapc (lambda ((name age role))
        (println "    " name " (" age "), " role))
      people)

# Pattern: returning multiple values
(de divide-and-remainder (a b)
  (list (/ a b) (mod a b)))

(let (((quotient remainder) (divide-and-remainder 17 5)))
  (println "  17/5 = " quotient " remainder " remainder))

# Pattern: working with alist entries
(setq config (list (cons "host" "localhost")
                   (cons "port" 8080)
                   (cons "debug" T)))

(println "  Config:")
(mapc (lambda ((key . val))
        (println "    " key " = " val))
      config)

(println "")
(println "Done.")
