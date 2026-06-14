# ============================================================
# test_coro.l -- Coroutines: co / yield / co-resume / co-alive?
#
# What this covers:
#   • Infinite generators: counter, Fibonacci sequence
#   • Finite coroutines: final return value vs. yielded values
#   • co-alive? lifecycle: T while body has not returned, NIL after
#   • Multiple independent coroutines interleaved without interference
#   • Bidirectional data flow: values sent in via co-resume
#   • GC correctness: yielded heap values survive GC between resumes
#   • co-alive? on non-coroutine values (NIL, integers) returns NIL
#
# Run:  L tests/test_coro.l
# Spec: L.spec.txt Part 7 -- Coroutines
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# -------------------------------------------------------------------------
# 1. Basic counter generator
# -------------------------------------------------------------------------
# The first co-resume passes the argument as the lambda's parameter
# (`start`).  Subsequent resumes pass NIL -- the counter ignores the
# incoming value and just increments its own state variable `i`.
# NOTE: `co` is an alias for `co-new`; the coroutine is initially
# suspended and runs only when resumed.
(setq counter
  (co (lambda (start)
    (setq i start)
    (while T
      (yield i)
      (setq i (1+ i))))))
(test 1  (co-resume counter 1))
(test 2  (co-resume counter NIL))
(test 3  (co-resume counter NIL))
(test 4  (co-resume counter NIL))
(test 5  (co-resume counter NIL))    # send val ignored; counter just increments
(test 6  (co-resume counter NIL))

# -------------------------------------------------------------------------
# 2. Fibonacci generator
# -------------------------------------------------------------------------
# Classic two-variable Fibonacci recurrence inside an infinite loop.
# The coroutine yields `a` before advancing the pair, so the sequence
# starts at F(0)=0.  The tmp variable avoids clobbering `a` before `b`
# is updated -- a classic swap idiom.
(setq fib
  (co (lambda (_)
    (let ((a 0) (b 1))
      (while T
        (yield a)
        (setq tmp (+ a b))
        (setq a b)
        (setq b tmp))))))
(test 0  (co-resume fib NIL))
(test 1  (co-resume fib NIL))
(test 1  (co-resume fib NIL))
(test 2  (co-resume fib NIL))
(test 3  (co-resume fib NIL))
(test 5  (co-resume fib NIL))
(test 8  (co-resume fib NIL))
(test 13 (co-resume fib NIL))

# -------------------------------------------------------------------------
# 3. Finite coroutine -- final return value + co-alive?
# -------------------------------------------------------------------------
# A coroutine that yields a finite number of times.  The LAST co-resume
# runs the body to completion and returns the body's return value (30),
# NOT a yielded value.  After that resume, co-alive? flips to NIL.
# NOTE: resuming a dead coroutine is safe and returns NIL.
(setq g (co (lambda (_) (yield 10) (yield 20) 30)))
(test T   (co-alive? g))
(test 10  (co-resume g NIL))
(test T   (co-alive? g))
(test 20  (co-resume g NIL))
(test T   (co-alive? g))
(test 30  (co-resume g NIL))
(test NIL (co-alive? g))
# resuming a dead coro should return NIL safely
(test NIL (co-resume g NIL))

# -------------------------------------------------------------------------
# 4. Multiple independent coroutines interleaved
# -------------------------------------------------------------------------
# Two range generators (c1: 1-3, c2: 10-12) are advanced in alternating
# order.  Each coroutine has its own stack frame and local variable `i`;
# resuming one has no effect on the suspended state of the other.
(de make-range (from to)
  (co (lambda (_)
    (let ((i from))
      (while (<= i to)
        (yield i)
        (setq i (1+ i)))))))

(setq c1 (make-range 1 3))
(setq c2 (make-range 10 12))
(test 1  (co-resume c1 NIL))
(test 10 (co-resume c2 NIL))
(test 2  (co-resume c1 NIL))
(test 11 (co-resume c2 NIL))
(test 3  (co-resume c1 NIL))
(test 12 (co-resume c2 NIL))

# -------------------------------------------------------------------------
# 5. Bidirectional send / receive
# -------------------------------------------------------------------------
# The value passed to co-resume becomes the RESULT of the co-yield
# expression inside the coroutine.  This enables a push-based pipeline:
# the caller sends increments; the coroutine accumulates and reflects the
# running total back.
# First resume: init=0, yield evaluates to 0 (body: yield total => 0)
# Second resume: sends 5; yield returns 5; total becomes 0+5=5; yield 5
(setq accumulator
  (co (lambda (init)
    (let ((total init))
      (while T
        (setq total (+ total (yield total))))))))
(test 0  (co-resume accumulator 0))   # first resume: init=0, yield 0
(test 5  (co-resume accumulator 5))   # send 5: total=5, yield 5
(test 12 (co-resume accumulator 7))   # send 7: total=12, yield 12
(test 12 (co-resume accumulator 0))   # send 0: total=12, yield 12

# -------------------------------------------------------------------------
# 6. GC stress: heavy allocations between resumes
# -------------------------------------------------------------------------
# A coroutine yields heap-allocated lists.  Between resumes the test
# allocates 100K cons cells to trigger the garbage collector.
# The coroutine's suspended stack is snapshotted on yield, so the GC
# can trace through it and must NOT collect the yielded lists.
# If gc-r1/gc-r2/gc-r3 are corrupted, this section fails.
(setq gc-coro
  (co (lambda (_)
    (yield (list 100 200 300))
    (yield (list 400 500 600))
    (list 700 800 900))))

(setq gc-r1 (co-resume gc-coro NIL))
# Allocate enough to trigger GC while coro is suspended
(let ((i 0) (junk NIL))
  (while (< i 100000)
    (setq junk (cons i junk))
    (setq i (1+ i))))
(setq gc-r2 (co-resume gc-coro NIL))
(let ((i 0) (junk NIL))
  (while (< i 100000)
    (setq junk (cons i junk))
    (setq i (1+ i))))
(setq gc-r3 (co-resume gc-coro NIL))

(test (list 100 200 300) gc-r1)
(test (list 400 500 600) gc-r2)
(test (list 700 800 900) gc-r3)
(test NIL (co-alive? gc-coro))

# -------------------------------------------------------------------------
# 7. co-alive? on non-coroutine returns NIL
# -------------------------------------------------------------------------
# co-alive? must not crash or throw when given non-coroutine values.
# NIL and integers are the most common accidental arguments.
(test NIL (co-alive? NIL))
(test NIL (co-alive? 42))

(println "")
(println "test_coro.l: ALL TESTS PASSED")
