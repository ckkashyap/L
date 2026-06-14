# ============================================================
# 31-util.l -- Threading macros, partial, sets, convenience forms
#
# What this covers:
#   1. -> and ->> threading macros (Clojure-style)
#   2. Partial application
#   3. Persistent sets
#   4. when-let / if-let
#   5. String comparison
#
# Run:  L demos/31-util.l
# ============================================================

(println "=== Utility Functions ===")
(println "")

# ---- 1. Threading macros ----
#
# -> threads a value through forms as the FIRST argument.
# ->> threads as the LAST argument.
# These transform deeply nested calls into readable pipelines.

(println "--- Threading macros ---")

# Without threading:
(println "  nested:    " (* (+ 1 2) 3))
# With ->:
(println "  (-> 1 ...): " (-> 1 (+ 2) (* 3)))

# ->> is great for list pipelines:
(println "  squares>2: "
  (->> (list 1 2 3 4 5)
       (filter (lambda (x) (> x 2)))
       (mapcar (lambda (x) (* x x)))))

# Build a map with ->:
(println "  map build: "
  (pmap-count
    (-> (pmap-new)
        (pmap-put "a" 1)
        (pmap-put "b" 2)
        (pmap-put "c" 3))))

# ---- 2. Partial application ----
#
# (partial f arg ...) returns a new function with some arguments
# pre-filled. The remaining arguments are appended when called.

(println "")
(println "--- Partial application ---")

(println "  add10:     " (mapcar (partial + 10) (list 1 2 3)))
(println "  double:    " (mapcar (partial * 2) (list 4 5 6)))

# Composing with threading:
(println "  pipeline:  "
  (->> (list 1 2 3 4 5)
       (filter (partial < 2))
       (mapcar (partial * 10))))

# ---- 3. Persistent sets ----
#
# Sets are built on top of HAMT maps (pmap). A set is a map
# where every value is T. O(log₃₂ n) membership, add, delete.

(println "")
(println "--- Persistent sets ---")

(setq fruits (pset "apple" "banana" "cherry"))
(println "  fruits:     " (pset-to-list fruits))
(println "  has apple?  " (pset-member? fruits "apple"))
(println "  has grape?  " (pset-member? fruits "grape"))

(setq more (pset "cherry" "date" "elderberry"))
(println "  union:      " (pset-count (pset-union fruits more)))
(println "  intersect:  " (pset-count (pset-intersection fruits more)))
(println "  difference: " (pset-count (pset-difference fruits more)))

# ---- 4. when-let / if-let ----
#
# Bind a variable and conditionally use it. Avoids the
# common pattern: (let ((x (find ...))) (when x (use x)))

(println "")
(println "--- when-let / if-let ---")

(de find-user (name)
  (if (equal name "Alice") (list "Alice" 30 "engineer") NIL))

(when-let ((u (find-user "Alice")))
  (println "  found: " u))

(when-let ((u (find-user "Nobody")))
  (println "  should not print"))

(println "  if-let: " (if-let ((u (find-user "Alice"))) "found" "missing"))
(println "  if-let: " (if-let ((u (find-user "Nobody"))) "found" "missing"))

# ---- 5. String comparison ----
#
# <, >, <=, >= now work on strings (lexicographic via strcmp).
# Useful for sorting, searching, and validation.

(println "")
(println "--- String comparison ---")

(println "  apple < banana: " (< "apple" "banana"))
(println "  zoo > abc:      " (> "zoo" "abc"))

# Sort a list of strings
(println "  sorted: "
  (sort (list "cherry" "apple" "banana" "date")
        (lambda (a b) (< a b))))

(println "")
(println "Done.")
