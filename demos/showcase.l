# ============================================================
# showcase.l -- Guided tour of all major L subsystems
#
# What this covers:
#   1. Core Lisp        -- atoms, lists, cons, car, cdr, arithmetic
#   2. Closures and TCO -- lambda with captured state, tail recursion
#   3. Macros           -- dmacro, quasiquote, short-circuit via macro
#   4. Bignum           -- factorial(30), fibonacci(50), arbitrary precision
#   5. Strings          -- fmt, sub, split/join, pack/unpack
#   6. Persistent data  -- pmap and pvec with structural sharing
#   7. Coroutines       -- infinite generator, drain pattern
#   8. Property lists   -- put/get/with/:/=:
#
# Run:  L demos/showcase.l
# Spec: L.spec.txt (all parts)
# ============================================================

(println "=== L Feature Showcase ===")
(println "")

# ============================================================
# 1. Core Lisp
#
# L is a Lisp-1: symbols, cons cells, and lists are the core
# data types. Everything is either an atom (number, string,
# symbol, NIL) or a cons cell (pair of two values).
#
# `cons` builds a pair; `car` gets the first element; `cdr`
# gets the rest. A proper list is a chain of cons cells ending
# in NIL:  (cons 1 (cons 2 (cons 3 NIL)))  =  (1 2 3).
#
# Arithmetic operators work on integers. L has no floats;
# use scaled integers for fractional quantities.
# ============================================================
(println "--- 1. Core Lisp ---")

(let ((p (cons 10 20)))
  (println "cons:" p)         # => (10 . 20)
  (println "car:" (car p))    # => 10
  (println "cdr:" (cdr p)))   # => 20

(let ((lst (list 1 2 3 4 5)))
  (println "list:" lst)                    # => (1 2 3 4 5)
  (println "car:" (car lst))              # => 1
  (println "cdr:" (cdr lst))              # => (2 3 4 5)
  (println "length:" (length lst)))       # => 5

(println "arithmetic:" (+ 10 (* 3 (- 7 2))))  # => 25
(println "mod:" (mod 17 5))                    # => 2
(println "")

# ============================================================
# 2. Closures and TCO
#
# `lambda` captures its lexical environment. A closure is a
# function that remembers the bindings from the scope in which
# it was created. `make-adder` returns a lambda that captures
# `n`; each resulting function carries its own copy of `n`.
#
# Tail-Call Optimization (TCO): L eliminates tail calls, so
# a tail-recursive function can loop over arbitrarily large
# inputs without growing the stack. `sum-to` accumulates a
# result in an explicit `acc` parameter. Called with 100000
# it completes without a stack overflow.
#
# The accumulator style (extra parameter for the running
# result) is the standard pattern for TCO in Lisp.
# ============================================================
(println "--- 2. Closures and TCO ---")

(de make-adder (n) (lambda (x) (+ x n)))
(let ((add5  (make-adder 5))
      (add10 (make-adder 10)))
  (println "add5(3):"  (add5  3))   # => 8
  (println "add10(3):" (add10 3)))  # => 13

# make-mult is another closure example: captures multiplier n
(de make-mult (n) (lambda (x) (* x n)))
(let ((triple (make-mult 3)))
  (println "triple(7):" (triple 7)))  # => 21

# sum-to uses tail recursion with an accumulator
# The recursive call is the LAST thing done -- no work is left
# pending after the call, so no stack frame is needed.
(de sum-to (n acc)
  (if (= n 0) acc (sum-to (1- n) (+ acc n))))

(println "sum(100000):" (sum-to 100000 0))  # => 5000050000
(println "")

# ============================================================
# 3. Macros
#
# Macros transform syntax at evaluation time. `dmacro` defines
# a macro; its body receives unevaluated argument forms and
# returns a new form that is then evaluated in place of the
# original call.
#
# Quasiquote (`) builds a list template. Unquote (`,`) splices
# a computed value into the template. This makes macro bodies
# look like the code they generate, with only the dynamic
# parts marked.
#
# `my-and` demonstrates the key power of macros: short-circuit
# evaluation. A function evaluates all its arguments before
# the call; a macro can choose NOT to evaluate an argument.
# When `a` is NIL the expansion `(if a b NIL)` never touches
# `b` at all. This is impossible to achieve with a function.
#
# `my-when` shows a multi-expression body using `. body` rest
# syntax and `(prog ,@body)` to sequence the forms.
# ============================================================
(println "--- 3. Macros ---")

(dmacro my-and (a b)
  `(if ,a ,b NIL))

(println "my-and T T:"   (my-and T T))    # => T
(println "my-and NIL T:" (my-and NIL T))  # => NIL  (b never evaluated)

(dmacro my-when (cond . body)
  `(if ,cond (prog ,@body) NIL))

(my-when T
  (println "my-when: body runs when cond is truthy"))  # => runs

# assert macro: wraps a test, prints OK or FAILED with the expression
(dmacro assert (expr)
  `(if (not ,expr)
     (println (list "ASSERTION FAILED:" ',expr))
     (println (list "OK:" ',expr))))

(assert (= (+ 2 3) 5))
(assert (equal (reverse (list 1 2 3)) (list 3 2 1)))
(println "")

# ============================================================
# 4. Bignum
#
# L uses libbf for arbitrary-precision integers. Numbers
# promote to bignum automatically when they exceed the fixnum
# range -- no special syntax or types needed. All arithmetic
# operators (+, *, **, etc.) work transparently on bignums.
#
# factorial(30) produces a 33-digit number, far beyond 64-bit.
# fib(50) is 12-digit but shown here to contrast with the
# naive recursive version which would be exponential. The
# iterative `fib` below uses the `do` loop form.
#
# There are no floats in L. For exact large-number arithmetic,
# bignums are the only mechanism -- and they are always exact.
# ============================================================
(println "--- 4. Bignum ---")

(de factorial (n)
  (if (<= n 1) 1 (* n (factorial (1- n)))))

(println "10! =" (factorial 10))   # => 3628800
(println "20! =" (factorial 20))   # => 2432902008176640000
(println "30! =" (factorial 30))   # => 265252859812191058636308480000000

(de fib (n)
  (do ((i 0 (1+ i))
       (a 0 b)
       (b 1 (+ a b)))
    ((= i n) a)))

(println "fib(50) =" (fib 50))   # => 12586269025
(println "2^62 =" (** 2 62))     # => 4611686018427387904
(println "")

# ============================================================
# 5. Strings
#
# L strings are double-quoted, immutable, and UTF-8 internally.
# Every operation returns a new string.
#
# `fmt` formats a string using ~A (print next arg) and ~%
# (newline). It is L's equivalent of sprintf / str.format.
#
# `sub` extracts a substring: start index (0-based), length.
#   (sub "hello world" 6 5)  →  "world"
#
# `pack` builds a string from a list of integer codepoints
# (or concatenates a list of strings). `unpack` does the
# reverse, converting a string to a list of codepoints.
# This lets you process strings as character sequences using
# ordinary list operations.
#
# `split` breaks a string at a delimiter. `join` reassembles
# a list of strings with a delimiter between them.
# ============================================================
(println "--- 5. Strings ---")

# These load stdlib extensions -- trim/split/join require lib/string.l; pmap-*/pvec-* require lib/ds.l
# In production, stdlib.l is auto-loaded at startup; explicit loads here ensure the demo is self-contained
(load "lib/string.l")

(println (fmt "Hello, ~A! ~A items." "World" 42))  # => Hello, World! 42 items.
(println (fmt "~A + ~A = ~A" 10 20 (+ 10 20)))     # => 10 + 20 = 30

(println (sub "hello world" 6 5))  # => world
(println (upcase "lisp"))          # => LISP
(println (trim "  hello  "))       # => hello

(let ((chars (unpack "ABC")))
  (println "unpack:" chars)            # => (65 66 67)
  (println "pack:" (pack chars)))      # => ABC

(let ((parts (split "a,b,c,d" ",")))
  (println "split:" parts)              # => (a b c d)
  (println "join:" (join parts "-")))   # => a-b-c-d

(println "")

# ============================================================
# 6. Persistent data structures
#
# `pmap` is an immutable hash map. Every mutating operation
# (`pmap-put`, `pmap-del`) returns a NEW map; the original
# is unchanged. Multiple versions coexist through structural
# sharing -- only the changed nodes are copied.
#
# `pvec` is an immutable indexed vector. `pvec-put` updates
# a slot and returns a new vector; `pvec-push` appends.
# Indices are 0-based. `pvec-seq` converts to a plain list
# for printing.
#
# Use pmap/pvec when you need:
#   - Immutable updates (functional style)
#   - Multiple versions of a data structure to coexist
#   - Safe sharing across coroutines without locking
# ============================================================
(println "--- 6. Persistent Data Structures ---")

(load "lib/ds.l")

# pmap: each put returns a new map, original unchanged
(let ((m (pmap-put (pmap-put (pmap-new) "x" 1) "y" 2)))
  (println "map x=" (pmap-get m "x") "y=" (pmap-get m "y"))  # => 1 2
  (println "count:" (pmap-count m))                           # => 2
  (let ((m2 (pmap-put m "z" 3)))
    (println "m2 count:" (pmap-count m2))  # => 3
    (println "m count unchanged:" (pmap-count m))))  # => 2

# pvec: persistent indexed vector
(let ((v (pvec 10 20 30 40 50)))
  (println "v:" (pvec-seq v))         # => (10 20 30 40 50)
  (println "v[2]:" (pvec-get v 2))   # => 30
  (let ((v2 (pvec-put v 2 99)))
    (println "v2[2]:" (pvec-get v2 2))  # => 99
    (println "v[2] unchanged:" (pvec-get v 2))))  # => 30

(println "")

# ============================================================
# 7. Coroutines
#
# A coroutine is a function that can be suspended and resumed.
# `co-new` wraps a function into a suspended coroutine object.
# `co-resume` starts or continues it; `co-yield` pauses it
# and returns a value to the caller. `co-alive?` is T while
# the coroutine body has not yet returned.
#
# Infinite generator: the coroutine body loops forever but
# yields control at each step. The caller decides when to stop.
# Because the coroutine is suspended at each co-yield, it
# never actually runs without the caller's permission.
#
# Drain pattern: collecting all values from a finite generator
# requires care. The correct pattern is prime-and-pump:
#   1. Prime -- do the first resume OUTSIDE the loop.
#   2. Pump  -- check co-alive? AFTER collecting, resume at
#              the BOTTOM of the loop body.
# This ensures the final NIL return value is never collected.
# ============================================================
(println "--- 7. Coroutines ---")

# Infinite integer generator starting at n
(de integers-from (n)
  (co-new (lambda (_)
    (let ((i n))
      (while T
        (co-yield i)
        (setq i (1+ i)))))))

(let ((gen (integers-from 1)))
  (println (co-resume gen NIL))   # => 1
  (println (co-resume gen NIL))   # => 2
  (println (co-resume gen NIL)))  # => 3

# Finite range generator: yields lo, lo+1, ..., hi-1
(de range-gen (lo hi)
  (co-new (lambda (_)
    (let ((i lo))
      (while (< i hi)
        (co-yield i)
        (setq i (1+ i)))))))

# Drain: collect all values from a finite generator into a list
(de drain (gen)
  (let ((v (co-resume gen NIL)) (result NIL))
    (while (co-alive? gen)
      (setq result (cons v result))
      (setq v (co-resume gen NIL)))
    (reverse result)))

(println (drain (range-gen 1 6)))  # => (1 2 3 4 5)
(println "")

# ============================================================
# 8. Property lists
#
# Every symbol in L has a built-in property list (plist): an
# association list of (key . value) pairs stored on the symbol
# itself. This is the basis for OOP-style programming in L.
#
# `put sym key val` -- attach a property to a symbol.
# `get sym key`     -- retrieve a property; NIL if absent.
# `plist sym`       -- return the full alist of all properties.
#
# `(with sym body...)` -- binds `This` to sym for the scope of
# body. Inside a `with` block, three special forms are active:
#   (: key)      -- get property key from This (key unevaluated)
#   (=: key val) -- put property key on This
#   (:: key)     -- return the live (key . val) cons cell
#
# Note: `:` and `::` are SPECIAL FORMS. The key is not
# evaluated -- `(: x)` reads the property named `x`, not the
# value of a variable called `x`.
# ============================================================
(println "--- 8. Property Lists ---")

(put 'person 'name "Alice")
(put 'person 'age  30)

(println "name:" (get 'person 'name))  # => Alice
(println "age:"  (get 'person 'age))   # => 30
(println "plist:" (plist 'person))     # => ((age . 30) (name . "Alice"))

(with 'person
  (println "via :name:" (: name))   # => Alice
  (=: age 31)                        # update age
  (println "updated age:" (: age)))  # => 31

# Changes made inside `with` persist on the symbol
(println "age after with:" (get 'person 'age))  # => 31

# Objects as symbols: lightweight OOP without a class system
(de make-counter (name start)
  (put name 'n start)
  name)

(de counter-inc (name)
  (put name 'n (1+ (get name 'n))))

(de counter-val (name)
  (get name 'n))

(make-counter 'c1 0)
(counter-inc 'c1)
(counter-inc 'c1)
(println "c1 after 2 increments:" (counter-val 'c1))  # => 2

(println "")
(println "Showcase complete!")
