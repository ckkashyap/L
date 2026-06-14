# ============================================================
# test_gc_env.l -- GC safety of lexical environments
#
# What this covers:
#   Verifies that lexical env cons cells survive GC during
#   function body evaluation. The bug: cur_env was only in
#   C locals (invisible to GC), so under heap pressure GC
#   could sweep env cells, making symbol lookups return NIL.
#
# Strategy: allocate enough cons cells to force GC, then
#   verify that let/let*/lambda bindings are still intact.
#
# Run:  L tests/test_gc_env.l
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- Helper: burn N cons cells to force GC ----
# Uses a loop to avoid stack overflow from deep recursion.
(de burn-cells (n)
  (let ((i 0) (junk NIL))
    (while (< i n)
      (setq junk (cons i junk))
      (setq i (1+ i)))
    junk))

# ---- Test 1: let bindings survive GC ----
(let ((a 42) (b 99))
  (burn-cells 300000)
  (test 42 a)
  (test 99 b)
  (test 141 (+ a b)))

# ---- Test 2: let* bindings survive GC ----
(let* ((x 10) (y (+ x 5)))
  (burn-cells 300000)
  (test 10 x)
  (test 15 y)
  (test 150 (* x y)))

# ---- Test 3: lambda closure env survives GC ----
(let ((make-adder (lambda (n)
                    (lambda (x) (+ n x)))))
  (let ((add5 (make-adder 5))
        (add100 (make-adder 100)))
    (burn-cells 300000)
    (test 15 (add5 10))
    (test 105 (add100 5))))

# ---- Test 4: de function lexical env survives GC ----
(de gc-test-fn (a b)
  (burn-cells 300000)
  (list a b (- a b)))

(test (list 50 20 30) (gc-test-fn 50 20))

# ---- Test 5: nested let + lambda under GC pressure ----
(let ((vals (let ((a 1) (b 2) (c 3))
              (burn-cells 300000)
              (let ((sum (+ a b c)))
                (burn-cells 300000)
                (list a b c sum)))))
  (test (list 1 2 3 6) vals))

# ---- Test 6: coroutine closure env survives GC ----
(de make-counter (start)
  (co (lambda (_)
    (let ((n start))
      (while T
        (yield n)
        (setq n (1+ n)))))))

(let ((c (make-counter 100)))
  (burn-cells 300000)
  (test 100 (co-resume c NIL))
  (burn-cells 300000)
  (test 101 (co-resume c NIL))
  (burn-cells 300000)
  (test 102 (co-resume c NIL)))

# ---- Test 7: letrec env survives GC ----
(letrec ((even? (lambda (n) (if (= n 0) T (odd? (1- n)))))
         (odd?  (lambda (n) (if (= n 0) NIL (even? (1- n))))))
  (burn-cells 300000)
  (test T   (even? 10))
  (test NIL (even? 7))
  (test T   (odd? 3)))

# ---- Test 8: deeply nested closures under GC pressure ----
(let ((f (lambda (a)
           (lambda (b)
             (lambda (c)
               (burn-cells 300000)
               (+ a b c))))))
  (test 60 (((f 10) 20) 30)))

(println "")
(println "test_gc_env.l: ALL TESTS PASSED")
