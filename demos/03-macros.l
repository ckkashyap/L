# ============================================================
# 03-macros.l -- dmacro, quasiquote, macro expansion, hygiene
#
# What this covers:
#   1. What is a macro?
#   2. my-and / my-unless -- when functions are not enough
#   3. breakable-while -- early exit via catch/throw
#   4. swap! with quasiquote -- readable code templates
#   5. gensym for hygiene -- avoiding variable capture
#
# Run:  L demos/03-macros.l
# Spec: L.spec.txt Part 3 -- Macros and Quasiquote
# ============================================================

(println "=== Macros Demo ===")

# ---- 1. What is a macro? ----------------------------------
# A macro is a compile-time code transformer. When the evaluator
# sees `(my-and a b)`, it calls the `my-and` macro transformer
# with the unevaluated forms `a` and `b`, gets back a new form,
# and evaluates that. Macros receive syntax trees; functions
# receive values.
#
# This is fundamentally different from functions. A function like
# `(square x)` sees the VALUE of x. A macro like `(my-and a b)`
# sees the EXPRESSION `a` -- the source code -- and decides for
# itself whether and how to evaluate it. In Python or JavaScript,
# there is no equivalent mechanism at the language level; you
# would reach for eval() or Babel transforms. In L, macros are
# a first-class, ordinary part of the language.
#
# `dmacro` is L's macro-definition form (analogous to `de` for
# functions). The body must return a list (a form) that the
# evaluator will then evaluate in place of the original call.

# ---- 2. my-and / my-unless --------------------------------
# Short-circuit operators cannot be functions: if `and` were a
# function, both arguments would be evaluated before the call.
# Only a macro can conditionally evaluate arguments. `my-unless`
# is `if` with the branches swapped -- trivial as a macro.
#
# `my-and` expands `(my-and a b)` into `(if a b NIL)`. Because
# `if` is a special form that only evaluates one branch, `b` is
# never touched when `a` is NIL. This short-circuit behaviour is
# impossible to achieve with a plain function.

(dmacro my-and (a b)
  `(if ,a ,b NIL))

(println "my-and T T:" (my-and T T))      # => T
(println "my-and T NIL:" (my-and T NIL))  # => NIL
(println "my-and NIL T:" (my-and NIL T))  # => NIL  (b never evaluated)

# `my-unless` runs its body when the condition is NIL (falsy).
# The `. body` rest-parameter collects all remaining forms so
# the macro works with multi-expression bodies. `(prog ...)` is
# L's sequencing form -- it evaluates each sub-form in order and
# returns the value of the last one.

(dmacro my-unless (cond . body)
  `(if (not ,cond) (prog ,@body) NIL))

(my-unless NIL (println "NIL is falsy, body runs"))   # => runs
(my-unless T   (println "T is truthy, body skipped")) # => skipped

# ---- 3. breakable-while -----------------------------------
# A macro that creates a while loop with a named exit.
# `(throw 'break NIL)` inside the loop exits early by unwinding
# to the `(catch 'break ...)` wrapped around the loop body. This
# pattern is idiomatic for early exit in L -- think of it as a
# structured goto that can only jump outward.
#
# Two macros cooperate here:
#   `break` expands to the throw that signals exit.
#   `breakable-while` wraps the loop in a catch that intercepts it.
# Neither `break` nor `breakable-while` could be a function,
# because functions cannot introduce new control-flow structure.

(dmacro break ()
  `(throw 'break NIL))

(dmacro breakable-while (cond . body)
  `(catch 'break
     (while ,cond ,@body)))

(let ((i 0))
  (breakable-while T
    (if (>= i 5) (break))   # exits the loop when i reaches 5
    (prin (list i " "))
    (setq i (1+ i))))       # prints: (0 " ") (1 " ") (2 " ") (3 " ") (4 " ")
(println "")

# ---- 4. swap! with quasiquote -----------------------------
# Quasiquote (`) lets you write a template with holes.
# Unquote (`,`) fills a hole with a computed value. This makes
# macro bodies readable: the template looks almost like the code
# it generates, with only the variable parts marked.
#
# `(swap! x y)` must expand to something that uses a temporary
# variable. The naive template `(let ((tmp ,a)) ...)` would
# introduce a literal symbol `tmp` into the user's code -- which
# causes a hygiene bug (see section 5). Here we use gensym to
# create a unique name, store it in `tmp` at macro-expansion
# time, then splice the generated symbol into the template with
# `,tmp`. The `,@` (unquote-splicing) form is not needed here,
# but note it is available to splice an entire list of forms.

(dmacro swap! (a b)
  (let ((tmp (gensym)))
    `(let ((,tmp ,a))
       (setq ,a ,b)
       (setq ,b ,tmp))))

(setq x 10)
(setq y 20)
(swap! x y)
(println "After swap: x=" x "y=" y)  # => x=20 y=10

# ---- 5. gensym for hygiene --------------------------------
# If a macro introduces a variable name like `tmp`, it risks
# clashing with a user variable of the same name. Consider:
#
#   (let ((tmp 999))
#     (swap! tmp y))
#
# Without gensym, `swap!` would expand to `(let ((tmp tmp)) ...)`
# which binds `tmp` to itself -- the wrong value. `gensym`
# generates a unique symbol that cannot clash with anything the
# user has written -- this is called a hygienic macro.
#
# The `swap!` above already uses gensym, so it is safe in all
# contexts. This is the idiomatic way to introduce temporaries
# inside any macro body.

(println "Done.")
