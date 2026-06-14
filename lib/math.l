# math.l -- Extended math functions

# (range start end [step]) -- list of integers from start to end (exclusive)
# Build in reverse (O(n) cons cells), then reverse at end.
(de range (start end . rest)
  (let ((step (if rest (car rest) 1))
        (acc NIL))
    (if (> step 0)
      (let ((i (- end step)))
        (while (>= i start)
          (setq acc (cons i acc))
          (setq i (- i step))))
      (let ((i (- end step)))
        (while (<= i start)
          (setq acc (cons i acc))
          (setq i (- i step)))))
    acc))

# (iota n [start [step]]) -- list of n numbers
(de iota (n . rest)
  (let ((start (if rest (car rest) 0))
        (step (if (cdr rest) (cadr rest) 1)))
    (range start (+ start (* n step)) step)))

# Integer floor/ceil/round -- for integer division.
# Note: (floor), (ceil), (round) are now C primitives that work on floats.
(de ifloor (n d)
  (let ((q (/ n d)))
    (if (and (< (rem n d) 0) (not (= (rem n d) 0)))
      (1- q)
      q)))

(de iceil (n d)
  (let ((q (/ n d)))
    (if (and (> (rem n d) 0))
      (1+ q)
      q)))

# (truncate n d) -- truncate toward zero (same as / for integers)
(de truncate (n . rest)
  (if rest
    (/ n (car rest))
    n))

# (isqrt n) -- integer square root via Newton's method
# Note: (sqrt) is now a C primitive returning a float via libbf.
(de isqrt (n)
  (if (<= n 0) 0
    (let* ((x n)
           (y (/ (+ x 1) 2)))
      (while (< y x)
        (setq x y)
        (setq y (/ (+ y (/ n y)) 2)))
      x)))

# (expt base exp) -- integer exponentiation (alias for **)
(de expt (base exp)
  (** base exp))

# (factorial n)
(de factorial (n)
  (if (<= n 1)
    1
    (* n (factorial (1- n)))))

# (fib n) -- Fibonacci
(de fib (n)
  (if (< n 2)
    n
    (let ((a 0) (b 1) (i 2))
      (while (<= i n)
        (let ((c (+ a b)))
          (setq a b)
          (setq b c)
          (setq i (1+ i))))
      b)))

# (sum lst) -- sum of list
(de sum (lst)
  (let ((acc 0))
    (mapc (lambda (x) (setq acc (+ acc x))) lst)
    acc))

# (product lst) -- product of list
(de product (lst)
  (let ((acc 1))
    (mapc (lambda (x) (setq acc (* acc x))) lst)
    acc))

# (even? n)
(de even? (n) (= (rem n 2) 0))

# (odd? n)
(de odd? (n) (not (even? n)))

# (zero? n)
(de zero? (n) (= n 0))

# (positive? n)
(de positive? (n) (> n 0))

# (negative? n)
(de negative? (n) (< n 0))

# (clamp n lo hi) -- clamp n to [lo, hi]
(de clamp (n lo hi)
  (max lo (min n hi)))

# -------------------------------------------------------------------------
# Higher-order list functions
# -------------------------------------------------------------------------

# (reduce fn init lst) -- left fold: (fn (fn (fn init a) b) c)
(de reduce (fn init lst)
  (let ((acc init))
    (mapc (lambda (x) (setq acc (fn acc x))) lst)
    acc))

# (fold fn init lst) -- alias for reduce
(de fold (fn init lst)
  (reduce fn init lst))

# (foldr fn init lst) -- right fold: (fn a (fn b (fn c init)))
(de foldr (fn init lst)
  (if (null? lst)
    init
    (fn (car lst) (foldr fn init (cdr lst)))))

# (any fn lst) -- return first element where (fn elem) is non-NIL, else NIL
(de any (fn lst)
  (let ((result NIL))
    (while (and lst (not result))
      (let ((v (fn (car lst))))
        (if v (setq result v)))
      (setq lst (cdr lst)))
    result))

# (every fn lst) -- return T if (fn elem) is non-NIL for all elements, else NIL
(de every (fn lst)
  (let ((ok T))
    (while (and lst ok)
      (if (not (fn (car lst)))
        (setq ok NIL))
      (setq lst (cdr lst)))
    ok))

# (by key-fn lst) -- sort lst by applying key-fn to each element
(de by (key-fn lst)
  (sort lst (lambda (a b) (< (key-fn a) (key-fn b)))))

# (group-by key-fn lst) -- partition lst into alist keyed by (key-fn elem)
(de group-by (key-fn lst)
  (let ((result NIL))
    (mapc
      (lambda (x)
        (let* ((k (key-fn x))
               (bucket (assoc k result)))
          (if bucket
            (set-cdr bucket (cons x (cdr bucket)))
            (setq result (cons (cons k (list x)) result)))))
      lst)
    result))
