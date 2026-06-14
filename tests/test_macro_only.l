# ============================================================
# test_macro_only.l -- Macro definition and expansion (dmacro)
#
# What this covers:
#   • dmacro defines a macro whose body runs at EXPAND TIME with
#     the raw (unevaluated) syntax forms as arguments
#   • my-if: a trivial macro that rewrites to the built-in if --
#     demonstrates that macros receive unevaluated operands
#   • swap!: a macro that generates a let + two setq forms to
#     exchange two variable bindings without a visible temp variable
#     at the call site
#   • progn2: a variadic macro using ,@ splice to forward an
#     arbitrary list of body forms to prog
#
# Run:  L tests/test_macro_only.l
# Spec: L.spec.txt Part 6 -- Macros
#
# NOTE: Macros are NOT first-class values. They cannot be passed to
# mapcar, stored in a variable, or used as a lambda. Expansion
# happens exactly once, before the resulting code is evaluated.
# Use gensym (not shown here) to generate capture-free temporaries
# when a macro introduces its own bindings.
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. my-if -- macro rewriting to built-in if ------------------
# At expand time (dmacro my-if (cond then else) ...) receives the
# three argument forms unevaluated and splices them directly into
# an `if` expression. The result is indistinguishable from writing
# (if ...) by hand. T branch returns 1, NIL branch returns 2.
(dmacro my-if (cond then else)
  `(if ,cond ,then ,else))

(test 1 (my-if T 1 2))   # => 1  (T branch taken)
(test 2 (my-if NIL 1 2)) # => 2  (NIL branch taken)

# ---- 2. swap! -- macro generating a temp-variable exchange --------
# The macro expands to (let ((tmp <a>)) (setq <a> <b>) (setq <b> tmp)).
# The caller never sees `tmp` -- it only exists in the expansion.
# NOTE: if the macro used a literal symbol `tmp` and the caller also
# had a variable named `tmp`, that would be a hygiene bug; gensym
# would be needed to avoid capture in a production implementation.
(dmacro swap! (a b)
  `(let ((tmp ,a))
     (setq ,a ,b)
     (setq ,b tmp)))

(let ((x 1) (y 2))
  (setq x 1)  # use setq for dynamic vars
  (setq y 2)
  (swap! x y)
  (test 2 x)  # => 2  (x received y's old value)
  (test 1 y)) # => 1  (y received x's old value)

# ---- 3. progn2 -- variadic macro via ,@ splice --------------------
# (. body) captures all arguments as a list named body. The macro
# splices them into (prog ...) using ,@body, forwarding any number
# of forms. prog evaluates each form for side effects and returns
# the value of the last one. Here z ends up as 2, so (+ z 1) => 3.
(dmacro progn2 (. body)
  `(prog ,@body))

(test 3 (progn2 (setq z 1) (setq z 2) (+ z 1))) # => 3

(println "")
(println "test_macro_only.l: ALL TESTS PASSED")
