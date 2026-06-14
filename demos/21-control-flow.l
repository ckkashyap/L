# ============================================================
# 21-control-flow.l -- Control flow: while, do, letrec, case, cond
#
# What this covers:
#   1. while -- imperative loop with mutable state
#   2. do -- Scheme-style loop with simultaneous variable steps
#   3. letrec -- mutual recursion (even?/odd? style)
#   4. letrec* -- sequential recursive bindings
#   5. Named-let pattern -- loop via tail-recursive local function
#   6. for / mapc -- iterating over lists
#   7. case -- value dispatch
#   8. cond -- multi-branch conditional
#
# Run:  L demos/21-control-flow.l
# Spec: L.spec.txt Part VI -- Special Forms
# ============================================================

# ---- 1. while -- imperative loop --------------------------------
# while evaluates its body repeatedly as long as the condition is truthy.
# Returns NIL. Body is run for side effects only.
# Use setq to maintain state across iterations.

(let ((i 0) (sum 0))
  (while (< i 5)
    (setq sum (+ sum i))
    (setq i (1+ i)))
  (println sum))   # => 10  (0+1+2+3+4)

# ---- 2. do -- Scheme-style loop with simultaneous variable steps --
# Syntax: (do ((var init step) ...) (test result) body...)
# - Variables are initialized to init, then stepped simultaneously each iteration
# - Simultaneously means ALL step expressions see the OLD values
# - Loop exits when test is truthy; result is then evaluated and returned
# - Body forms run for side effects; their values are discarded

(println
  (do ((i 0 (1+ i))
       (sum 0 (+ sum i)))
      ((= i 5) sum)))   # => 10  (sum 0..4)

# Simultaneous stepping: swap two variables without a temp variable
(println
  (do ((a 1 b)           # a gets OLD b
       (b 2 a))          # b gets OLD a
      ((= a 2) (list a b))))   # => (2 1)  after one step

# ---- 3. letrec -- mutually recursive bindings --------------------
# All bindings in letrec are in scope for ALL right-hand-side expressions.
# This enables even?/odd? style mutual recursion where each function
# refers to the other before either is fully defined.
# Contrast with let: in let, no binding is in scope for any RHS.

(letrec ((my-even? (lambda (n)
                     (if (= n 0) T (my-odd? (1- n)))))
          (my-odd?  (lambda (n)
                     (if (= n 0) NIL (my-even? (1- n))))))
  (println (my-even? 4))   # => T
  (println (my-odd? 7))    # => T
  (println (my-even? 3)))  # => NIL

# ---- 4. letrec* -- sequential recursive bindings ----------------
# Like letrec, but each binding is in scope for SUBSEQUENT bindings (like let*).
# A binding can reference previous bindings AND itself (self-recursion).
# Use letrec* when bindings depend on prior ones and need self-recursion.

(letrec* ((base 10)
           (count-down (lambda (n)
                         (if (= n 0)
                           base                     # sees base from prior binding
                           (count-down (1- n))))))  # self-recursive
  (println (count-down 5)))   # => 10

# ---- 5. Named-let -- tail-recursive loop via local function ------
# Scheme's named-let (let name ((var init) ...) body) is not supported in L.
# The idiomatic equivalent is letrec + lambda: bind a local function that
# can call itself by name, then immediately invoke it with the initial values.
# The self-call (loop ...) is a tail call -- TCO applies.

(println
  (letrec ((iter (lambda (i acc)
                   (if (= i 5)
                     (reverse acc)
                     (iter (1+ i) (cons (* i i) acc))))))
    (iter 0 (list))))
# => (0 1 4 9 16)  -- squares of 0..4

# ---- 6. for / mapc -- iterating over lists -----------------------
# mapc applies a function to each element of a list for side effects.
# Returns NIL. Use mapcar when you need the results.

(mapc println (list 1 2 3))   # prints 1, 2, 3 on separate lines

(mapc (lambda (x) (println (* x x))) (list 1 2 3 4 5))
# prints 1 4 9 16 25

# ---- 7. case -- dispatch on value --------------------------------
# (case val (pattern body...) ... (T default...))
# Tests val against each pattern using equal. First match wins.
# T as a pattern is the else/default clause.
# NOTE: patterns are literal values, not variables -- they are NOT evaluated.

(de day-name (n)
  (case n
    (1 "Monday")
    (2 "Tuesday")
    (3 "Wednesday")
    (4 "Thursday")
    (5 "Friday")
    (6 "Saturday")
    (7 "Sunday")
    (T "Unknown")))

(println (day-name 3))   # => "Wednesday"
(println (day-name 9))   # => "Unknown"

# ---- 8. cond -- multi-branch conditional -------------------------
# (cond (test1 result1) (test2 result2) ... (T default))
# Tests each condition in order; returns the result of the first truthy one.
# T as the final test is the conventional else clause.
# Like nested if/else if -- but more readable for many branches.

(de classify (n)
  (cond
    ((< n 0) "negative")
    ((= n 0) "zero")
    ((< n 10) "small")
    ((< n 100) "medium")
    (T "large")))

(println (classify -5))   # => "negative"
(println (classify 0))    # => "zero"
(println (classify 7))    # => "small"
(println (classify 42))   # => "medium"
(println (classify 999))  # => "large"

(println "21-control-flow.l done")
