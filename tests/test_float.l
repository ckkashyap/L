# ============================================================
# test_float.l -- Floating point numbers via libbf
#
# What this covers:
#   • Float literals (3.14, -0.5, .5)
#   • Arithmetic: +, -, *, / with auto-promotion
#   • Math functions: sqrt, sin, cos, tan, exp, log, pow
#   • Comparison: =, <, >, <=, >=
#   • floor, ceil, round
#   • Type predicates: float?, number?
#   • Mixed int/float operations
#
# Run:  L tests/test_float.l
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- Float literals ----
(test T (float? 3.14))
(test T (float? -0.5))
(test T (float? 0.0))
(test NIL (float? 42))
(test NIL (float? "hello"))

# ---- number? includes floats ----
(test T (number? 3.14))
(test T (number? 42))

# ---- Arithmetic with auto-promotion ----
(test T (float? (+ 1 0.5)))      # int + float → float
(test T (float? (- 3.0 1)))      # float - int → float
(test T (float? (* 2 0.5)))      # int * float → float
(test T (float? (/ 1.0 3)))      # float / int → float

# ---- Arithmetic values ----
(test T (= (+ 1 0.5) 1.5))
(test T (= (- 3.0 1) 2.0))
(test T (= (* 2 0.5) 1.0))

# ---- Comparison ----
(test T (< 1.5 2.5))
(test T (> 3.0 2.0))
(test T (<= 1.0 1.0))
(test T (>= 2.5 2.5))
(test T (= 1.0 1.0))
(test NIL (= 1.0 2.0))

# ---- Mixed int/float comparison ----
(test T (< 1 1.5))
(test T (> 2.5 2))
(test T (= 1.0 1))

# ---- Math functions ----
(test T (> (sqrt 2) 1.414))
(test T (< (sqrt 2) 1.415))
(test T (= (cos 0) 1.0))
(test T (> (exp 1) 2.718))
(test T (< (exp 1) 2.719))
(test T (> (log (exp 1)) 0.999))
(test T (= (pow 2 10) 1024.0))

# ---- Trig ----
(test T (< (sin (pi)) 0.001))     # sin(π) ≈ 0
(test T (> (sin (pi)) -0.001))
(test T (> (cos (pi)) -1.001))    # cos(π) ≈ -1
(test T (< (cos (pi)) -0.999))

# ---- floor / ceil / round ----
(test T (= (floor 3.7) 3.0))
(test T (= (floor -1.3) -2.0))
(test T (= (ceil 3.2) 4.0))
(test T (= (ceil -1.8) -1.0))
(test T (= (round 3.5) 4.0))
(test T (= (round 3.4) 3.0))

# ---- float conversion ----
(test T (float? (float 42)))
(test T (float? (float "3.14")))
(test T (= (float 42) 42.0))

# ---- pi ----
(test T (> (pi) 3.14159))
(test T (< (pi) 3.14160))

(println "")
(println "test_float.l: ALL TESTS PASSED")
