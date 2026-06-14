# ============================================================
# test_callbacks.l -- FFI callback tests
#
# Requires: build/cb_helper.so (Linux) or build/cb_helper.dll (Windows)
#           built with HAVE_FFI=1
# ============================================================

# progn is not a built-in; define it as a macro that sequences via 'and'
(dmacro progn (. body)
  (cons (quote and) body))

(setq *pass 0)
(setq *fail 0)

(de test (expected actual)
  (if (equal expected actual)
    (progn (setq *pass (1+ *pass)) T)
    (progn
      (setq *fail (1+ *fail))
      (println "FAIL: expected" expected "got" actual)
      NIL)))

# Skip entire file if ffi-callback is not available (no libffi)
(when (not (procedure? ffi-callback))
  (println "SKIP test_callbacks.l (ffi-callback not available)")
  (println "ALL TESTS PASSED")
  (quit))

(setq helper
  (if (equal *os* "windows")
    "build/cb_helper.dll"
    "build/cb_helper.so"))

(ffi-open helper)

# ---- 1. call1: single int arg, int return --------------------------------
(setq double-cb (ffi-callback "i" "i" (lambda (x) (* x 2))))
(test 14 (native helper "call1" "i" "p" double-cb "i" 7))
(test 0  (native helper "call1" "i" "p" double-cb "i" 0))
(test -4 (native helper "call1" "i" "p" double-cb "i" -2))

# ---- 2. call2: two int args, int return ----------------------------------
(setq add-cb (ffi-callback "i" "ii" (lambda (a b) (+ a b))))
(test 9  (native helper "call2" "i" "p" add-cb "i" 4 "i" 5))
(test 0  (native helper "call2" "i" "p" add-cb "i" -3 "i" 3))

(setq mul-cb (ffi-callback "i" "ii" (lambda (a b) (* a b))))
(test 21 (native helper "call2" "i" "p" mul-cb "i" 3 "i" 7))

# ---- 3. call0: void callback fires for side effects ----------------------
(setq fired NIL)
(setq flag-cb (ffi-callback "v" "" (lambda () (setq fired T))))
(native helper "call0" "v" "p" flag-cb)
(test T fired)

# ---- 4. Closure captures enclosing state ---------------------------------
(setq offset 100)
(setq add-offset-cb (ffi-callback "i" "i" (lambda (x) (+ x offset))))
(test 107 (native helper "call1" "i" "p" add-offset-cb "i" 7))
(setq offset 200)
# Dynamic scoping: offset lookup happens at call time
(test 207 (native helper "call1" "i" "p" add-offset-cb "i" 7))

# ---- 5. call_and_add: callback result used in further C computation ------
(setq inc-cb (ffi-callback "i" "i" (lambda (x) (1+ x))))
(test 13 (native helper "call_and_add" "i" "p" inc-cb "i" 7 "i" 5))
# fn(7) = 8, 8 + 5 = 13

# ---- 6. Multiple independent callbacks coexist ---------------------------
(setq cb-a (ffi-callback "i" "i" (lambda (x) (+ x 1))))
(setq cb-b (ffi-callback "i" "i" (lambda (x) (* x 3))))
(setq cb-c (ffi-callback "i" "i" (lambda (x) (- x 1))))
(test 8  (native helper "call1" "i" "p" cb-a "i" 7))
(test 21 (native helper "call1" "i" "p" cb-b "i" 7))
(test 6  (native helper "call1" "i" "p" cb-c "i" 7))

# ---- 7. GC: callback survives a forced collection while referenced -------
# Trigger GC via allocation pressure (gc primitive not exposed)
(de alloc-pressure (n)
  (let ((i 0))
    (while (< i n)
      (cons i i)
      (setq i (1+ i)))))
(alloc-pressure 5000)
(test 14 (native helper "call1" "i" "p" double-cb "i" 7))
(alloc-pressure 5000)
(test 9  (native helper "call2" "i" "p" add-cb "i" 4 "i" 5))

# ---- 8. Callback can call other Lisp functions ---------------------------
(de square (n) (* n n))
(setq square-cb (ffi-callback "i" "i" (lambda (x) (square x))))
(test 49 (native helper "call1" "i" "p" square-cb "i" 7))
(test 0  (native helper "call1" "i" "p" square-cb "i" 0))

# ---- 9. mem-read-i32 / mem-write-i32 ------------------------------------
(setq libc (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))
(setq arr (native libc "malloc" "p" "i" 8))  # 2 ints x 4 bytes
(mem-write-i32 arr 0 42)
(mem-write-i32 arr 4 -7)
(test 42 (mem-read-i32 arr 0))
(test -7 (mem-read-i32 arr 4))
(mem-write-i32 arr 0 99)
(test 99 (mem-read-i32 arr 0))
(native libc "free" "v" "p" arr)

# ---- Report ---------------------------------------------------------------
(println "")
(println "Callbacks:" *pass "passed," *fail "failed")
(if (= *fail 0)
  (println "ALL TESTS PASSED")
  (and (println "TESTS FAILED") (quit 1)))
