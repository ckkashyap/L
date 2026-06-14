# ============================================================
# 16-algo.l -- Algorithms: sort, group-by, reduce, any/every, rand, date
#
# What this covers:
#   1. sort / by / maxi / mini  -- key-based ordering and extrema
#   2. group-by                 -- partitioning into equivalence classes
#   3. reduce / fold / foldr    -- structural recursion over lists
#   4. any / every              -- short-circuit predicate quantifiers
#   5. rand / seed              -- deterministic random numbers and shuffling
#   6. date / clock / usec      -- wall-clock time and microsecond benchmarking
#
# Run:  L demos/16-algo.l
# Spec: L.spec.txt Part V -- Standard Library
# ============================================================

(println "=== Algorithms Demo ===")
(println "")

# =========================================================================
# 1. sort / by / maxi / mini
#
# Key-based ordering: `by` extracts a sort key from each element; elements
# with equal keys retain their original relative order (stable sort).
# `sort` without a comparator uses the natural order for the element type;
# with a comparator lambda it can sort in any order.
#
# `maxi` and `mini` return the element, not the key. For example,
# `(maxi abs (list -9 1 2))` returns -9 (the element with the largest
# absolute value), not 9. The key function is only used for comparison.
# =========================================================================
(println "--- 1. Sorting ---")

(setq nums (list 5 2 8 1 9 3 7 4 6))
(println "Original:   " nums)
(println "Ascending:  " (sort nums))
(println "Descending: " (sort nums (lambda (a b) (> a b))))
(println "Unchanged:  " nums)    # sort is non-destructive

# Sort strings alphabetically (default comparator handles strings)
(setq words (list "banana" "apple" "date" "cherry" "elderberry" "fig"))
(println "")
(println "Words alphabetical: " (sort words))
# `by` applies a key function and sorts by the key; equal keys preserve order
(println "Words by length:    " (by string-length words))

# Sort pairs (name score) by score descending
(setq scores (list
  (list "alice"   92)
  (list "bob"     87)
  (list "carol"   95)
  (list "dave"    88)
  (list "eve"     91)))

(setq ranked (sort scores (lambda (a b) (> (cadr a) (cadr b)))))
(println "")
(println "Leaderboard:")
(let ((rank 1))
  (mapc (lambda (entry)
    (println "  " rank ". " (car entry) " - " (cadr entry))
    (setq rank (1+ rank)))
  ranked))

# maxi/mini with key function -- returns the element, not the key value.
# `scores` is still in original order because sort was non-destructive.
(setq top    (maxi (lambda (e) (cadr e)) scores))
(setq bottom (mini (lambda (e) (cadr e)) scores))
(println "Top scorer:    " (car top)    " (" (cadr top)    ")")
(println "Bottom scorer: " (car bottom) " (" (cadr bottom) ")")

# =========================================================================
# 2. group-by
#
# Partition by equivalence class: elements with the same key are grouped
# together in an alist `(key . (members...))`. The result preserves the
# order in which each key is first encountered.
#
# Unlike `sort`, `group-by` does not require a total ordering -- it only
# needs an equality-comparable key.
# =========================================================================
(println "")
(println "--- 2. group-by ---")

# Group scores into grade bands
(de score-band (entry)
  (let ((s (cadr entry)))
    (if (>= s 90) "A"
      (if (>= s 80) "B"
        "C"))))

(setq by-grade (group-by score-band scores))
(println "Grade A: " (mapcar car (cdr (assoc "A" by-grade))))
(println "Grade B: " (mapcar car (cdr (assoc "B" by-grade))))

# Group words by first letter and show each bucket
(setq by-letter (group-by (lambda (w) (sub w 0 1)) words))
(println "Words by first letter:")
(mapc (lambda (bucket)
  (println "  " (car bucket) ": " (cdr bucket)))
  by-letter)

# =========================================================================
# 3. reduce / fold / foldr
#
# Structural recursion over a list. All three walk the list and combine
# elements with a binary function and an initial accumulator:
#
#   `reduce` is a left fold: (((init f e1) f e2) f e3 ...)
#     -- the accumulator grows on the left; good for sums, products, counts.
#   `fold`   is an alias for left fold with the same argument order.
#   `foldr`  is right-associative: (e1 f (e2 f (e3 f ... init)))
#     -- processes elements right-to-left; natural for building lists
#       (cons-ing onto an accumulator preserves order without reversing).
# =========================================================================
(println "")
(println "--- 3. reduce / fold / foldr ---")

(setq data (list 4 7 2 9 1 5 8 3 6 10))
(setq n    (length data))
(setq tot  (reduce + 0 data))
(setq mn   (/ tot n))
(println "Data:     " data)
(println "Sum:      " tot)
(println "Mean:     " mn)
(println "Max:      " (reduce (lambda (acc x) (if (> x acc) x acc)) (car data) (cdr data)))
(println "Min:      " (reduce (lambda (acc x) (if (< x acc) x acc)) (car data) (cdr data)))

# Variance: E[(x - mean)^2]
(setq variance
  (/ (reduce (lambda (acc x) (+ acc (* (- x mn) (- x mn)))) 0 data) n))
(println "Variance: " variance)

# Flatten nested list using foldr -- right-fold naturally appends sublists
# in order because it processes right-to-left and cons-es onto the result.
(de flatten (lst)
  (foldr
    (lambda (x acc)
      (if (listp x)
        (append (flatten x) acc)
        (cons x acc)))
    NIL lst))

(setq nested (list 1 (list 2 3) (list 4 (list 5 6)) 7))
(println "")
(println "Nested:    " nested)
(println "Flattened: " (flatten nested))

# Word frequency map using fold -- left-fold accumulates an alist where
# each entry maps a letter to its count.
(de frequencies (lst)
  (fold (lambda (acc x)
    (let ((entry (assoc x acc)))
      (if entry
        (prog (set-cdr entry (1+ (cdr entry))) acc)
        (cons (cons x 1) acc))))
  NIL lst))

(setq letters (list "a" "b" "a" "c" "b" "a" "d" "c" "a"))
(setq freqs   (sort (frequencies letters)
                    (lambda (a b) (> (cdr a) (cdr b)))))
(println "")
(println "Letter frequencies (desc):")
(mapc (lambda (e) (println "  " (car e) ": " (cdr e))) freqs)

# =========================================================================
# 4. any / every
#
# Short-circuit predicate quantifiers:
#   `any`   stops at the first truthy result and returns it (or NIL if none).
#   `every` stops at the first NIL result and returns NIL (or T if all pass).
#
# Because they short-circuit, they are efficient for large lists and safe
# for generators that should not be fully consumed. They also compose:
# `(not (any p lst))` is equivalent to `(every (complement p) lst)`.
# =========================================================================
(println "")
(println "--- 4. any / every ---")

# Simple primality test
(de prime? (x)
  (if (< x 2) NIL
    (let ((ok T) (d 2))
      (while (and ok (<= (* d d) x))
        (if (= 0 (mod x d)) (setq ok NIL))
        (setq d (1+ d)))
      ok)))

(setq mixed (list 2 7 4 11 6 13 8))
(println "List: " mixed)
(println "Any prime?    " (not (not (any prime? mixed))))
(println "All even?     " (every even? mixed))
(println "Any > 10?     " (not (not (any (lambda (x) (> x 10)) mixed))))
(println "All positive? " (every positive? mixed))

# Data validation: every short-circuits on the first invalid entry,
# avoiding unnecessary work on a large dataset.
(de valid-score? (entry)
  (and (listp entry)
       (stringp (car entry))
       (numberp (cadr entry))
       (>= (cadr entry) 0)
       (<= (cadr entry) 100)))

(setq bad-data (list (list "carol" 105) (list "dave" 80)))
(println "")
(println "Scores all valid?    " (every valid-score? scores))
(println "Bad data all valid?  " (every valid-score? bad-data))

# =========================================================================
# 5. rand / seed -- deterministic random numbers and shuffling
#
# `seed` initialises the PRNG to a known state; calling it with the same
# value produces identical sequences -- useful for reproducible tests and
# simulations. Without a seed call, the sequence is implementation-defined.
#
# Shuffling uses the Fisher-Yates algorithm: iterate from the last position
# backwards, swapping each element with a randomly chosen earlier one. This
# produces a uniformly random permutation in O(n) time. It requires O(1)
# indexed access, so the list is copied into a `vec` (mutable array) first.
# =========================================================================
(println "")
(println "--- 5. rand / seed ---")

# Fisher-Yates shuffle using vec for O(1) indexed access
(de shuffle (lst)
  (let ((n   (length lst))
        (arr (vec (length lst) 0))
        (i   0))
    (mapc (lambda (x) (vec-set arr i x) (setq i (1+ i))) lst)
    (setq i (1- n))
    (while (> i 0)
      (let ((j   (rand 0 i))
            (tmp (vec-get arr i)))
        (vec-set arr i (vec-get arr j))
        (vec-set arr j tmp))
      (setq i (1- i)))
    (setq i 0)
    (let ((result NIL))
      (while (< i n)
        (setq result (cons (vec-get arr i) result))
        (setq i (1+ i)))
      (reverse result))))

(seed 42)
(setq deck (list 1 2 3 4 5 6 7 8 9 10))
(println "Deck:       " deck)
(println "Shuffled:   " (shuffle deck))
(println "Shuffled:   " (shuffle deck))   # different each time

# Monte Carlo pi estimation (N=4000 points): scatter random points in the
# unit square [0,1]^2 and count those inside the quarter circle of radius 1.
# The fraction inside approximates pi/4, so pi ~ 4 * inside / total.
(seed 9999)
(let ((inside 0) (total 4000) (i 0))
  (while (< i total)
    (let ((x (rand 0 10000))
          (y (rand 0 10000)))
      (if (<= (+ (* x x) (* y y)) 100000000)
        (setq inside (1+ inside))))
    (setq i (1+ i)))
  (println "")
  # pi ~ 4 * inside / total; display as "3.NNN"
  (setq pi-num (* 4000 inside))
  (setq pi-int (/ pi-num total))
  (setq pi-frac (mod (/ pi-num (/ total 1000)) 1000))
  (println "Monte Carlo pi (N=4000): ~" pi-int "." pi-frac))

# Reproducibility: seeding with the same value replays the exact sequence.
(println "")
(println "Reproducibility:")
(seed 1)
(println "  seed(1): " (rand 1000) " " (rand 1000) " " (rand 1000))
(seed 1)
(println "  seed(1): " (rand 1000) " " (rand 1000) " " (rand 1000))   # identical

# =========================================================================
# 6. date / clock / usec
#
# Wall-clock time functions:
#   `date`  -- returns the current date as a string, or as a list (Y M D)
#              when called with T.
#   `clock` -- returns the current time as a string, or as a list (H M S)
#              when called with T.
#   `usec`  -- returns microseconds since an epoch (suitable for benchmarks).
#              Take the difference of two `usec` calls to measure elapsed time.
# =========================================================================
(println "")
(println "--- 6. date / clock / usec ---")

(println "Date:   " (date))
(println "Time:   " (clock))

(setq dl (date T))
(println "Year:   " (car dl) "  Month: " (cadr dl) "  Day: " (caddr dl))

(setq cl (clock T))
(println "Hour:   " (car cl) "  Min:   " (cadr cl) "  Sec: " (caddr cl))

# Benchmark: measure micros for a computation -- usec returns microseconds
# since epoch; subtracting start from end gives wall-clock elapsed time.
(setq t0 (usec))
(let ((i 0) (acc 0))
  (while (< i 200000)
    (setq acc (+ acc i))
    (setq i (1+ i))))
(setq t1 (usec))
(println "")
(println "Sum 0..199999: " (- t1 t0) " microseconds")

(println "")
(println "=== Algorithms demo complete ===")
