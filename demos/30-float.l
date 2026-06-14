# ============================================================
# 30-float.l -- Floating point numbers via libbf
#
# What this covers:
#   1. Float literals and automatic promotion
#   2. 128-bit precision arithmetic (beyond IEEE 754)
#   3. Trigonometry, exponentials, logarithms
#   4. Practical examples: distance, physics, finance
#
# Run:  L demos/30-float.l
# ============================================================

(println "=== Floating Point Numbers ===")
(println "")

# ---- 1. Float literals and promotion ----
(println "--- Literals & promotion ---")
(println "  3.14      =" 3.14)
(println "  -0.5      =" -0.5)
(println "  1 + 0.5   =" (+ 1 0.5))       # int + float → float
(println "  10 / 3.0  =" (/ 10 3.0))       # exact fractional division
(println "  0.1 * 0.2 =" (* 0.1 0.2))      # no IEEE 754 rounding error

# ---- 2. 128-bit precision ----
(println "")
(println "--- 128-bit precision ---")
(println "  pi        =" (pi))
(println "  sqrt(2)   =" (sqrt 2))
(println "  e         =" (exp 1))
(println "  1/3       =" (/ 1.0 3))

# ---- 3. Math functions ----
(println "")
(println "--- Trigonometry ---")
(println "  sin(0)    =" (sin 0))
(println "  cos(0)    =" (cos 0))
(println "  sin(pi/2) =" (sin (/ (pi) 2)))
(println "  cos(pi)   =" (cos (pi)))
(println "  tan(pi/4) =" (tan (/ (pi) 4)))
(println "  asin(1)   =" (asin 1))

(println "")
(println "--- Exponentials & logarithms ---")
(println "  exp(1)    =" (exp 1))
(println "  log(e)    =" (log (exp 1)))
(println "  2^10      =" (pow 2 10))
(println "  sqrt(144) =" (sqrt 144))

(println "")
(println "--- Rounding ---")
(println "  floor(3.7)  =" (floor 3.7))
(println "  ceil(3.2)   =" (ceil 3.2))
(println "  round(3.5)  =" (round 3.5))
(println "  floor(-1.3) =" (floor -1.3))

# ---- 4. Practical examples ----
(println "")
(println "--- Euclidean distance ---")
(de distance (x1 y1 x2 y2)
  (let* ((dx (- x2 x1))
         (dy (- y2 y1)))
    (sqrt (+ (* dx dx) (* dy dy)))))
(println "  (0,0) → (3,4) =" (distance 0 0 3 4))
(println "  (1,1) → (4,5) =" (distance 1 1 4 5))

(println "")
(println "--- Physics: projectile range ---")
# R = v² sin(2θ) / g
(de projectile-range (v angle-deg)
  (let* ((g 9.81)
         (theta (* angle-deg (/ (pi) 180)))
         (r (/ (* (* v v) (sin (* 2 theta))) g)))
    r))
(println "  v=20 m/s, 45° =" (projectile-range 20 45) "m")
(println "  v=20 m/s, 30° =" (projectile-range 20 30) "m")

(println "")
(println "--- Compound interest ---")
# A = P(1 + r/n)^(nt)
(de compound-interest (principal rate years n)
  (let ((base (+ 1 (/ rate n))))
    (* principal (pow base (* n years)))))
(println "  $1000 at 5% for 10 years =" (compound-interest 1000 0.05 10 12))

(println "")
(println "Done.")
