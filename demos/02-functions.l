# ============================================================
# 02-functions.l -- de, lambda, closures, higher-order functions, TCO
#
# What this covers:
#   1. de -- named functions with dynamic scope
#   2. lambda -- anonymous functions with lexical scope
#   3. Closures -- capturing enclosing bindings
#   4. Higher-order functions (mapcar, filter, apply)
#   5. Tail-call optimization -- recursion in O(1) stack space
#
# Run:  L demos/02-functions.l
# Spec: L.spec.txt Part 2 -- Functions and Scope
# ============================================================

(println "=== Functions Demo ===")

# ---- 1. de -- named functions with dynamic scope -----------
# `de` defines a function and binds it to a global symbol.
# Functions defined with `de` use dynamic scoping: free variable
# references are resolved at call time, not at definition time.
# If you're coming from Python or JavaScript, this is unusual --
# those languages use lexical scoping everywhere. Dynamic scope
# means a function can see the local variables of its CALLER,
# which is powerful for configuration-style patterns but
# requires care. For most everyday functions that only use their
# own parameters, the distinction doesn't matter.

(de square (x) (* x x))
(de cube (x) (* x x x))

(println "square(7):" (square 7))   # => 49
(println "cube(4):" (cube 4))       # => 64

# ---- 2. lambda -- anonymous functions with lexical scope ---
# `lambda` creates a closure that captures the environment at
# the point of definition. This is lexical scoping. The two
# scoping modes coexist in L -- `de` uses dynamic, `lambda`
# uses lexical. If you've used JavaScript arrow functions or
# Python lambdas, this behaves the way you'd expect: the
# function "remembers" the variables that were in scope when
# it was created, regardless of where it is eventually called.
#
# `make-adder` returns a lambda that closes over `n`. Each
# call to `make-adder` produces a fresh closure with its own
# private copy of `n`.

# ---- 3. Closures -------------------------------------------
# A closure is a lambda that has 'closed over' variables from
# its enclosing scope. Each call to the outer function creates
# a fresh binding, so multiple closures can share a binding for
# communication -- or, as here, each closure gets its own
# independent binding.
#
# `make-counter` creates a local variable `n` and returns a
# lambda that increments and returns it. Because `n` is local
# to the `let` block, each call to `make-counter` produces a
# separate counter. (The counter itself is not demonstrated
# below, but `make-adder` illustrates the same capture pattern.)

# Note: in L with dynamic scoping, closures capture the env
(de make-counter ()
  (let ((n 0))
    (lambda ()
      (setq n (1+ n))
      n)))

(de make-adder (n)
  (lambda (x) (+ x n)))

(let ((add10 (make-adder 10)))
  (println "add10(5):" (add10 5))    # => 15
  (println "add10(32):" (add10 32))) # => 42

# ---- 4. Higher-order functions (mapcar, filter, apply) ----
# `mapcar` applies a function to every element of a list,
# returning a new list. `filter` retains elements for which
# the predicate is truthy. `apply` calls a function with the
# elements of a list as its arguments.
#
# These are the workhorses of functional programming. If you've
# used Python's `map()` and `filter()` builtins, or JavaScript's
# `Array.map` and `Array.filter`, you already know the concept.
# In L, the lambda syntax is slightly more verbose than Python's
# `lambda x: ...` but the semantics are identical.

(println "mapcar:")
(println (mapcar square (list 1 2 3 4 5)))
# => (1 4 9 16 25)

(println "filter even:")
(println (filter (lambda (x) (= (mod x 2) 0)) (list 1 2 3 4 5 6)))
# => (2 4 6)

# ---- 5. Tail-call optimization ----------------------------
# L guarantees that tail-recursive calls use O(1) stack space.
# A call is in tail position when its result is immediately
# returned -- no further computation wraps it. This makes
# recursion as efficient as a loop.
#
# `sum-to` accumulates the result in the `acc` parameter.
# Notice that the recursive call `(sum-to (1- n) (+ acc n))`
# is the LAST thing the function does -- nothing multiplies or
# adds to its result afterwards. That is the tail-call pattern.
# Contrast with `factorial` in 01-basics.l, which wraps the
# recursive call in `(* n ...)` -- that is NOT a tail call.
#
# Thanks to TCO, `(sum-to 50000 0)` runs without overflowing
# the call stack, even though it "loops" 50,000 times.

(de sum-to (n acc)
  (if (= n 0) acc (sum-to (1- n) (+ acc n))))

(println "sum 1..1000:" (sum-to 1000 0))    # => 500500
(println "sum 1..50000:" (sum-to 50000 0))  # => 1250025000

# Fibonacci with accumulator (TCO)
# The naive Fibonacci is doubly-recursive and exponential.
# By threading two accumulators `a` and `b` (representing
# fib(n) and fib(n+1)), we reduce it to a single tail-recursive
# loop. `fib-tc` does the work; `fib` is the public entry point
# that seeds the accumulators.

(de fib-tc (n a b)
  (if (= n 0) a (fib-tc (1- n) b (+ a b))))
(de fib (n) (fib-tc n 0 1))

(println "fib(20):" (fib 20))  # => 6765
(println "fib(40):" (fib 40))  # => 102334155

(println "Done.")
