# ============================================================
# 01-basics.l -- Atoms, lists, arithmetic, conditionals, recursion
#
# What this covers:
#   1. Atoms -- the primitive values of L
#   2. Lists and S-expressions -- code as data
#   3. Arithmetic -- prefix notation, exact integers
#   4. Conditionals (if, cond) -- NIL is the only false value
#   5. Recursion -- how L replaces loops
#
# Run:  L demos/01-basics.l
# Spec: L.spec.txt Part 1 -- Core Language
# ============================================================

(println "=== L Basics Demo ===")

# ---- 1. Atoms ---------------------------------------------
# In L, every value is an atom or a list. Atoms include:
# integers, strings, symbols, NIL (false/empty list), and T
# (true). Everything evaluates to itself except symbols, which
# look up their value. If you're used to Python or JavaScript,
# think of atoms as primitives -- but symbols are special because
# they ARE their name; quoting one with ' prevents evaluation
# and gives you the symbol itself rather than its binding.

(println "Atoms:")
(println 42)              # => 42  (integer, self-evaluating)
(println "hello world")   # => hello world  (string, self-evaluating)
(println 'foo)            # => foo  (quoted symbol -- the name, not a value)
(println T)               # => T   (canonical true)
(println NIL)             # => NIL (false AND the empty list -- same thing)

# ---- 2. Lists and S-expressions ---------------------------
# L code IS a list. `(+ 1 2)` is the list containing the symbol
# `+` and two integers. This homoiconicity -- code and data having
# the same structure -- is what makes macros possible. There is no
# separate "AST" type: the source you write is the tree the
# evaluator walks. Coming from Python, think of every expression
# as already being its own parse tree, written in parentheses.
#
# `list` constructs a list; `car` returns the first element;
# `cdr` returns everything after the first element (the tail);
# `length` and `reverse` are self-explanatory.

(println "Lists:")
(let ((lst (list 1 2 3 4 5)))
  (println lst)            # => (1 2 3 4 5)
  (println (car lst))      # => 1
  (println (cdr lst))      # => (2 3 4 5)
  (println (length lst))   # => 5
  (println (reverse lst))) # => (5 4 3 2 1)

# ---- 3. Arithmetic ----------------------------------------
# There is no operator precedence. All operators are functions
# called in prefix notation. `(+ 1 (* 2 3))` is 1 + (2 * 3).
# All integers are exact; there are no floating-point numbers
# in L. Division truncates toward zero (integer division).
# `mod` gives the remainder. Most arithmetic operators accept
# more than two arguments: `(+ 1 2 3)` is perfectly valid.

(println "Arithmetic:")
(println (+ 1 2 3))           # => 6   (variadic +)
(println (* 6 7))             # => 42
(println (/ 100 7))           # => 14  (integer division, truncates)
(println (mod 100 7))         # => 2

# ---- 4. Conditionals (if, cond) ---------------------------
# NIL is the only false value. 0 is truthy. T is the canonical
# true value but any non-NIL value is truthy. `if` is a special
# form: only one branch is evaluated -- the evaluator never even
# looks at the other branch. This matters when branches have
# side effects, and it is also what makes short-circuit macros
# possible (see 03-macros.l).
#
# `cond` works like a chain of if/else-if. Each clause is
# (test body...). The first test that is truthy wins; `T` as
# the last test acts as an else fallthrough.

(println "Conditionals:")
(if (> 5 3)
  (println "5 > 3")
  (println "5 <= 3"))

(cond
  ((> 10 20) (println "10 > 20"))
  ((= 10 10) (println "10 = 10"))  # => this branch wins
  (T         (println "else")))

# ---- 5. Recursion -----------------------------------------
# L has no built-in for/while loop at the language level
# (stdlib adds while/for). Iteration is expressed as recursion.
# The interpreter optimizes tail calls to run in O(1) stack
# space, so a tail-recursive function is as cheap as a loop.
#
# `factorial` below is NOT tail-recursive: after the recursive
# call returns, we still need to multiply by n. Each call
# pushes a frame. This is fine for small n, but would overflow
# the stack for large n. See 02-functions.l for a tail-recursive
# version that can handle arbitrarily large n.

(de factorial (n)
  (if (<= n 1) 1 (* n (factorial (1- n)))))

(println "Factorials:")
(println (factorial 10))  # => 3628800
(println (factorial 15))  # => 1307674368000

(println "Done.")
