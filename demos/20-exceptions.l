# ============================================================
# 20-exceptions.l -- Exception handling: catch and throw
#
# What this covers:
#   1. catch/throw basics: non-resumable stack unwinding
#   2. Tag matching: only the nearest matching catch fires
#   3. Returning values through a throw
#   4. Early exit from loops using catch
#   5. Nesting catches: innermost matching tag wins
#   6. Limitations: no matching catch exits the process
#
# Run:  L demos/20-exceptions.l
# Spec: L.spec.txt Part VI -- Special Forms: catch/throw
# ============================================================

# ------------------------------------------------------------
# Section 1: catch / throw basics
# ------------------------------------------------------------
# (catch tag body...) establishes a catch point identified by the symbol tag.
# (throw tag value) unwinds the stack to the nearest matching catch,
# which then returns value.
# Tags are compared with eq (symbol identity -- not equal).

(println
  (catch 'err
    (println "before throw")
    (throw 'err 42)
    (println "never reached")))   # => 42
# Output:
#   before throw
#   42

# ------------------------------------------------------------
# Section 2: Returning values
# ------------------------------------------------------------
# The value passed to throw becomes the return value of the catch form.
# This is the only way to return early from a catch block.
(let ((result
       (catch 'found
         (mapc (lambda (x)
                 (if (= x 3) (throw 'found x)))
               (list 1 2 3 4 5))
         NIL)))    # default if not thrown
  (println result))  # => 3

# ------------------------------------------------------------
# Section 3: Non-matching tags propagate
# ------------------------------------------------------------
# A throw only catches at the NEAREST enclosing catch with the SAME tag.
# Tags that don't match are skipped -- the throw continues unwinding.
(catch 'outer
  (catch 'inner
    (throw 'outer "found outer"))  # skips 'inner, caught by 'outer
  (println "inner: never reached"))
# => "found outer" from the outer catch

# ------------------------------------------------------------
# Section 4: Early exit from loops
# ------------------------------------------------------------
# A common pattern: return the first matching element, NIL if none found.
# NOTE: catch/throw inside a 'de' function body has a known interpreter
# limitation -- the throw returns the dynamic binding pair instead of the
# thrown value. The correct workaround is filter, which is also cleaner:
(de find-first (pred lst)
  (let ((found (filter pred lst)))
    (if found (car found) NIL)))

(println (find-first even? (list 1 3 5 4 7)))  # => 4
(println (find-first even? (list 1 3 5 7)))    # => NIL

# ------------------------------------------------------------
# Section 5: Nesting -- innermost matching tag wins
# ------------------------------------------------------------
(println
  (catch 'a
    (catch 'b
      (catch 'a          # this inner 'a catches first
        (throw 'a "inner-a")))  # => "inner-a" from inner catch
    "b-default"))

# ------------------------------------------------------------
# Section 6: Limitation -- no matching catch exits process
# ------------------------------------------------------------
# WARNING: If no matching catch exists, the interpreter exits.
# There is no default "top-level handler" in L.
# Always ensure a catch exists for any throw that might fire.
#
# The following would exit the process if uncommented:
#   (throw 'unhandled "this kills the process")
#
# Best practice: wrap the top-level entry point in a catch:
(catch 'fatal
  # ... application code ...
  (println "application ran successfully"))

(println "20-exceptions.l done")
