# ============================================================
# 06-coroutines.l -- Primary coroutine tutorial: suspend, resume, compose
#
# What this covers:
#   1. What is a coroutine?
#   2. Infinite generator
#   3. Finite generator with co-alive?
#   4. The drain pattern (prime-and-pump)
#   5. Bidirectional communication
#   6. Pipeline (producer -> filter -> consumer)
#
# Run:  L demos/06-coroutines.l
# Spec: L.spec.txt Part X -- Coroutines
# ============================================================

# =========================================================================
# 1. What is a coroutine?
#
# A coroutine is a function that can be suspended and resumed. Unlike a
# regular function call which runs to completion, a coroutine can pause at
# any yield, returning a value to the caller. The caller can then resume
# it later -- at the exact point where it paused -- optionally passing a
# value back.
#
# co wraps a function into a coroutine object (initially suspended)
# co-resume starts/continues it; the arg becomes the result of yield inside
# yield suspends and returns a value to the co-resume caller
# co-alive? is T while the body has not yet returned
# =========================================================================

# =========================================================================
# 2. Infinite generator
#
# An infinite generator yields values forever -- the caller controls when
# to stop. Because the coroutine suspends at each yield, it never
# actually runs forever; it only advances when the caller asks for the
# next value.
# =========================================================================
(println "--- 2. Infinite generator ---")

(de integers-from (n)
  (co (lambda (_)
    (let ((i n))
      (while T
        (yield i)
        (setq i (1+ i)))))))

(let ((gen (integers-from 5)))
  (println (co-resume gen NIL))   # => 5
  (println (co-resume gen NIL))   # => 6
  (println (co-resume gen NIL)))  # => 7

# =========================================================================
# 3. Finite generator with co-alive?
#
# A finite generator yields a fixed number of values, then returns.
# After the body returns, co-alive? becomes NIL. This lets callers
# distinguish "got a value" from "the coroutine has finished".
#
# Note: co-alive? is T even before the first co-resume (the coroutine
# is suspended at its entry point, not yet started).
# =========================================================================
(println "")
(println "--- 3. Finite generator with co-alive? ---")

(de range-gen (lo hi)
  (co (lambda (_)
    (let ((i lo))
      (while (< i hi)
        (yield i)
        (setq i (1+ i)))))))

(let ((gen (range-gen 0 3)))
  (println (co-alive? gen))              # => T (not started yet)
  (println (co-resume gen NIL))          # => 0
  (println (co-alive? gen))             # => T
  (println (co-resume gen NIL))          # => 1
  (println (co-resume gen NIL))          # => 2
  (println (co-resume gen NIL))          # => 3  (body return value: last (setq i 3))
  (println (co-alive? gen)))             # => NIL

# =========================================================================
# 4. The drain pattern (critical)
#
# Collecting all values from a finite generator requires care. There are
# two common approaches, one of which is wrong.
#
# The WRONG pattern checks co-alive? BEFORE resuming, then resumes inside
# the loop body. When the body finally returns NIL, co-alive? was T going
# into the last iteration, so the NIL return value gets collected as a
# spurious last element:
#
#   (while (co-alive? gen)              # WRONG
#     (setq result (cons (co-resume gen NIL) result)))
#
# The CORRECT pattern is "prime and pump":
#   1. Prime: do the first resume OUTSIDE the loop to get the first value
#   2. Pump: check co-alive? AFTER collecting that value, resume at the
#      BOTTOM of the loop -- so the resume that triggers the body's return
#      is never collected
#
# This works because: after the body returns, co-alive? becomes NIL and
# the loop exits, leaving the unwanted NIL in v (not in result).
# =========================================================================
(println "")
(println "--- 4. The drain pattern ---")

(de drain (gen)
  # Prime: get the first value before entering the loop
  (let ((v (co-resume gen NIL)) (result NIL))
    (while (co-alive? gen)            # check alive AFTER getting v
      (setq result (cons v result))   # collect v (known-good value)
      (setq v (co-resume gen NIL)))   # resume to get next (or trigger body return)
    (reverse result)))                # result is in reverse order

(let ((gen (range-gen 1 5)))
  (println (drain gen)))   # => (1 2 3 4)  -- no spurious NIL at end

# =========================================================================
# 5. Bidirectional communication
#
# yield is not just for output -- it also receives a value. The argument
# passed to co-resume becomes the return value of yield inside the
# coroutine. This enables the caller to send data in and receive data out
# on every exchange.
#
# Exchange protocol:
#   caller: (co-resume coro VALUE-TO-SEND)  =>  VALUE-YIELDED
#   inside: (setq x (yield VALUE-TO-YIELD))  =>  VALUE-TO-SEND
# =========================================================================
(println "")
(println "--- 5. Bidirectional communication ---")

(de accumulator ()
  (co (lambda (_)
    (let ((sum 0))
      (while T
        (let ((n (yield sum)))
          (setq sum (+ sum n))))))))

(let ((acc (accumulator)))
  (println (co-resume acc NIL))  # => 0  (initial sum)
  (println (co-resume acc 10))   # => 10 (sum after adding 10)
  (println (co-resume acc 5))    # => 15 (sum after adding 5)
  (println (co-resume acc 3)))   # => 18

# =========================================================================
# 6. Pipeline: producer -> filter -> consumer
#
# Coroutines compose naturally into processing pipelines. Each stage is a
# coroutine that pulls from the previous stage using co-resume. The stages
# are lazy: a value only moves through the pipeline when the consumer asks
# for it. No intermediate lists are built.
#
# Here, integers-from (defined above) feeds into evens-of, which uses the
# prime-and-pump drain pattern internally to pull and filter.
# =========================================================================
(println "")
(println "--- 6. Coroutine pipeline: producer -> filter -> consumer ---")

(de evens-of (source)
  # filter: yields only even values from source
  (co (lambda (_)
    (let ((v (co-resume source NIL)))
      (while (co-alive? source)
        (if (= (mod v 2) 0) (yield v))
        (setq v (co-resume source NIL)))))))

(let* ((nums (integers-from 1))
       (evens (evens-of nums)))
  (println (co-resume evens NIL))  # => 2
  (println (co-resume evens NIL))  # => 4
  (println (co-resume evens NIL))) # => 6

(println "")
(println "06-coroutines.l done")
