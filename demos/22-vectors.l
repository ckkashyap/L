# ============================================================
# 22-vectors.l -- Vectors: mutable vec vs persistent pvec
#
# What this covers:
#   1. vec -- mutable vector: create, get, set, push, length
#   2. pvec -- persistent vector: immutable updates return new vector
#   3. When to use which
#   4. Building a matrix as a vector of vectors
#   5. Converting between vec and list
#
# Run:  L demos/22-vectors.l
# Spec: L.spec.txt Part VII -- Vectors
# ============================================================

# ---- 1. vec -- mutable vector ------------------------------------
# (vec n fill) creates a mutable vector of n elements, all initialized to fill.
# vec-get and vec-set are 0-indexed.
# vec-set mutates in place -- returns NIL, not the vector.
# vec-push appends to the end, growing the vector.
# vec-length returns the current number of elements.

(let ((v (vec 3 NIL)))
  (println (vec-length v))    # => 3
  (println (vec-get v 0))     # => NIL  (initial value)
  (vec-set v 0 "hello")
  (vec-set v 1 42)
  (vec-set v 2 T)
  (println (vec-get v 0))     # => "hello"
  (println (vec-get v 1))     # => 42
  (vec-push v "appended")
  (println (vec-length v))    # => 4  (grew from 3)
  (println (vec-get v 3)))    # => "appended"

# ---- 2. pvec -- persistent vector --------------------------------
# pvec is immutable: pvec-put returns a NEW vector; the original is unchanged.
# pvec-push appends, also returning a new vector.
# Use pvec when you need immutable updates -- safe to share across coroutines
# or pass to functions without defensive copying.
#
# Constructor: (pvec elem0 elem1 ...) -- takes the elements directly.
# Length:      pvec-count (not pvec-length).

(let* ((v0 (pvec 0 0 0))         # 3-element pvec with all zeros
       (v1 (pvec-put v0 1 99))   # new vector with slot 1 changed
       (v2 (pvec-push v0 77)))   # new vector with 77 appended
  (println (pvec-get v0 1))   # => 0   (original unchanged)
  (println (pvec-get v1 1))   # => 99  (v1 has the update)
  (println (pvec-count v0))   # => 3   (original still length 3)
  (println (pvec-count v2)))  # => 4   (v2 has the appended element)

# ---- 3. When to use vec vs pvec ---------------------------------
# Use vec when:
#   - You need maximum performance (no allocation on update)
#   - The vector is local to one function or loop
#   - You are accumulating results imperatively (fill a buffer)
#
# Use pvec when:
#   - You need to share the vector safely (no mutation surprises)
#   - You need multiple versions (before/after a transformation)
#   - You are writing functional-style pipeline code

# Example: vec is ideal for building a result buffer in a while loop
(de squares-vec (n)
  (let ((v (vec n 0)) (i 0))
    (while (< i n)
      (vec-set v i (* i i))
      (setq i (1+ i)))
    v))

(let ((sq (squares-vec 5)))
  (println (vec-get sq 0))   # => 0
  (println (vec-get sq 4)))  # => 16

# ---- 4. Matrix as a vector of vectors ---------------------------
# A 2D matrix can be built as a vec of vecs.
# Row-major: matrix[row][col] = (vec-get (vec-get matrix row) col)

(de make-matrix (rows cols init)
  (let ((m (vec rows NIL)) (i 0))
    (while (< i rows)
      (vec-set m i (vec cols init))   # each row is its own vec, pre-filled with init
      (setq i (1+ i)))
    m))

(let ((mat (make-matrix 3 3 0)))
  (vec-set (vec-get mat 1) 1 99)   # set center cell
  (println (vec-get (vec-get mat 1) 1))  # => 99
  (println (vec-get (vec-get mat 0) 0))) # => 0  (other cells unchanged)

# ---- 5. Converting between vec and list -------------------------
# To convert a list to a vec: iterate the list with a while loop.
# To convert a vec to a list: iterate indices, cons, then reverse.

# list -> vec
(de list->vec (lst)
  (let ((v (vec (length lst) NIL)) (l lst) (i 0))
    (while l
      (vec-set v i (car l))
      (setq l (cdr l))
      (setq i (1+ i)))
    v))

# vec -> list
(de vec->list (v)
  (let ((n (vec-length v)) (i 0) (acc NIL))
    (while (< i n)
      (setq acc (cons (vec-get v i) acc))
      (setq i (1+ i)))
    (reverse acc)))

(let* ((lst (list 10 20 30 40))
       (v (list->vec lst))
       (back (vec->list v)))
  (println (vec-get v 2))   # => 30
  (println back))            # => (10 20 30 40)

(println "22-vectors.l done")
