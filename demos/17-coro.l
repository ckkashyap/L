# ============================================================
# 17-coro.l -- Coroutine patterns: generators, bidirectional, scheduler, pipeline
#
# What this covers:
#   1. Infinite generator      -- lazy sequence with caller-controlled termination
#   2. Fibonacci generator     -- classic stateful infinite stream
#   3. Finite coroutine        -- body returns a final value; co-alive? tracks liveness
#   4. Bidirectional send/recv -- co-resume passes values in; co-yield passes them out
#   5. Round-robin scheduler   -- cooperative multitasking with multiple coroutines
#   6. Coroutine pipeline      -- composing stages; each stage pulls from the previous
#
# Run:  L demos/17-coro.l
# Spec: L.spec.txt Part X -- Coroutines
# ============================================================

(println "=== Coroutines Demo ===")
(println "")

# =========================================================================
# 1. Infinite generator -- lazy integer sequence
#
# The "infinite generator" pattern: a coroutine whose body loops forever,
# yielding one value per iteration. Because the coroutine suspends at each
# `yield`, it never actually runs forever -- it only advances when the caller
# invokes `co-resume`. The caller decides when to stop pulling values.
#
# Use this pattern when the number of values is unknown at construction time,
# or when the full sequence would be too large to materialise. In contrast,
# a finite generator (see section 3) is better when the count is known and
# the caller needs to detect the end naturally via co-alive?.
# =========================================================================
(println "--- 1. Infinite generators ---")

(de integers-from (n)
  (co (lambda (_)
    (let ((i n))
      (while T
        (yield i)
        (setq i (1+ i)))))))

# `take` pulls exactly n values from any generator, ignoring co-alive?.
# Safe for infinite generators; the coroutine is simply left suspended.
(de take (n coro)
  (let ((result NIL) (i 0))
    (while (< i n)
      (setq result (cons (co-resume coro NIL) result))
      (setq i (1+ i)))
    (reverse result)))

(println "First 10 naturals: " (take 10 (integers-from 0)))
(println "First 10 from 5:   " (take 10 (integers-from 5)))

# =========================================================================
# 2. Fibonacci -- classic generator example
#
# The Fibonacci generator holds two state variables (a, b) across yields.
# A regular function cannot do this without an explicit state object; a
# coroutine keeps its local variables alive between calls automatically.
# Each co-resume advances to the next yield and returns the next term.
# =========================================================================
(println "")
(println "--- 2. Fibonacci generator ---")

(setq fibs
  (co (lambda (_)
    (let ((a 0) (b 1))
      (while T
        (yield a)
        (setq tmp (+ a b))
        (setq a b)
        (setq b tmp))))))

(println "Fibonacci: " (take 10 fibs))

# =========================================================================
# 3. Finite coroutine with a return value
#
# A finite coroutine's body eventually returns rather than looping forever.
# When it does, co-alive? becomes NIL on the next check and the body's
# return value is what co-resume delivers on that final call.
#
# This example searches a list and yields matching elements. When the
# list is exhausted the body returns the string "done" -- the caller
# distinguishes in-progress yields from the final return by checking
# co-alive? immediately after each co-resume.
# =========================================================================
(println "")
(println "--- 3. Finite coroutine ---")

(setq searcher
  (co (lambda (_)
    (let ((data (list 3 1 4 1 5 9 2 6 5 3)))
      (mapc (lambda (x)
        (if (> x 4) (yield x)))
        data)
      "done"))))                        # final return value

(let ((alive T))
  (while alive
    (setq v (co-resume searcher NIL))
    (if (co-alive? searcher)
      (println "  found > 4: " v)
      (println "  finished with: " v))
    (setq alive (co-alive? searcher))))

# =========================================================================
# 4. Bidirectional -- accumulator (send values in, get running total out)
#
# The "bidirectional accumulator" pattern exploits the two-way nature of
# co-resume / yield: the value passed to co-resume becomes the return value
# of yield inside the coroutine. This creates a conversation:
#
#   caller: (co-resume acc VALUE)   => current total
#   inside: (setq n (yield total))  => VALUE is bound to n
#
# Each round-trip adds the caller's value to the running sum and returns
# the new total. The coroutine acts as a stateful computation object without
# any explicit state record or closure over mutable variables outside.
# =========================================================================
(println "")
(println "--- 4. Bidirectional send/receive ---")

(setq running-sum
  (co (lambda (init)
    (let ((total init))
      (while T
        (setq total (+ total (yield total))))))))

(setq t0 (co-resume running-sum 0))
(println "  start=0  total=" t0)
(setq t1 (co-resume running-sum 10))
(println "  + 10     total=" t1)
(setq t2 (co-resume running-sum 25))
(println "  + 25     total=" t2)
(setq t3 (co-resume running-sum 5))
(println "  + 5      total=" t3)

# =========================================================================
# 5. Multiple coroutines -- round-robin scheduler simulation
#
# The "round-robin scheduler" pattern demonstrates cooperative multitasking:
# multiple coroutines share the CPU by yielding control after each unit of
# work. A scheduler loop iterates over all live tasks and resumes each one
# in turn. A task signals it is done by returning from its body, after which
# co-alive? is NIL and the scheduler skips it.
#
# This is cooperative (not preemptive) -- a task runs until it explicitly
# yields. It simulates how event loops and async runtimes schedule work
# without OS threads.
# =========================================================================
(println "")
(println "--- 5. Round-robin scheduler ---")

(de make-task (name steps)
  (co (lambda (_)
    (let ((i 1))
      (while (<= i steps)
        (println "  [" name "] step " i "/" steps)
        (yield NIL)
        (setq i (1+ i)))
      (println "  [" name "] done")))))

(setq tasks
  (list
    (make-task "A" 3)
    (make-task "B" 2)
    (make-task "C" 4)))

# The scheduler runs until all tasks are dead. Each pass over the list
# resumes every live task once, then checks whether any are still alive.
(let ((running T))
  (while running
    (setq running NIL)
    (mapc (lambda (t)
      (if (co-alive? t)
        (prog
          (co-resume t NIL)
          (if (co-alive? t) (setq running T)))))
      tasks)))

# =========================================================================
# 6. Pipeline: producer -> transformer -> consumer
#
# The "coroutine pipeline" pattern composes stages where each stage is a
# coroutine that pulls from the previous stage via co-resume. Data flows
# lazily: the consumer drives execution -- it asks for one value, which causes
# the transformer to ask the producer for one value, and so on upstream.
#
# No intermediate lists are built. Adding a new stage (e.g., a second
# transformer) just wraps the existing pipeline in another coroutine.
# This mirrors Unix pipes: each stage is independent and recomposable.
# =========================================================================
(println "")
(println "--- 6. Coroutine pipeline ---")

# Producer: yields numbers 1..n
(de producer (n)
  (co (lambda (_)
    (let ((i 1))
      (while (<= i n)
        (yield i)
        (setq i (1+ i)))
      NIL))))

# Transformer: squares values from upstream (prime-and-pump drain pattern)
(de squarer (upstream)
  (co (lambda (_)
    (let ((v (co-resume upstream NIL)))
      (while (co-alive? upstream)
        (yield (* v v))
        (setq v (co-resume upstream NIL)))
      (if v (yield (* v v)))))))

# Consumer: collect results using prime-and-pump; excludes body return value
(de collect (gen)
  (let ((results NIL))
    (let ((v (co-resume gen NIL)))
      (while (co-alive? gen)
        (setq results (cons v results))
        (setq v (co-resume gen NIL)))
      (if v (setq results (cons v results))))
    (reverse results)))

(setq nums    (producer 6))
(setq squares (squarer nums))
(setq result  (collect squares))
(println "Squares of 1..6: " result)

(println "")
(println "=== Coroutines demo complete ===")
