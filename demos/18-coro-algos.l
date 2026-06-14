# ============================================================
# 18-coro-algos.l -- Classic algorithms made elegant with coroutines
#
# What this covers:
#   1. Sieve of Eratosthenes  -- infinite pipeline of filter coroutines
#   2. Binary tree traversal  -- yield works inside ordinary recursion
#   3. Permutations           -- lazy search cursor with early termination
#   4. Lazy flatten           -- recursive walk that yields atoms directly
#
# Run:  L demos/18-coro-algos.l
# Spec: L.spec.txt Part X -- Coroutines
# ============================================================

(println "=== Coroutine Algorithm Demos ===")

# --- Shared helpers -------------------------------------------------------

# `take`: pull exactly n values from any generator into a list.
# Safe for infinite generators -- the coroutine is left suspended, not killed.
(de take (n gen)
  (let ((result NIL) (i 0))
    (while (< i n)
      (setq result (cons (co-resume gen NIL) result))
      (setq i (1+ i)))
    (reverse result)))

# `drain`: collect all remaining values from a finite generator.
# Uses the prime-and-pump pattern so the body's NIL return is NOT included:
#   1. Prime -- resume once outside the loop to get the first real value.
#   2. Pump  -- check co-alive? before collecting; resume at bottom of loop.
# When the body returns, co-alive? becomes NIL, the loop exits, and the
# unwanted return value is left in v (not appended to result).
(de drain (gen)
  (let ((v (co-resume gen NIL)) (result NIL))
    (while (co-alive? gen)
      (setq result (cons v result))
      (setq v (co-resume gen NIL)))
    (reverse result)))

# =========================================================================
# 1. Sieve of Eratosthenes
#
# The classic eager sieve needs an upper bound N in advance so it can
# allocate a bit array of size N. It is fast but requires knowing how far
# you want to go before starting.
#
# The coroutine sieve is an infinite pipeline of filters. Each prime p
# discovered creates a new coroutine stage that strips multiples of p from
# an upstream source. Adding a new prime extends the pipeline dynamically
# -- no bound is needed and the pipeline grows on demand:
#
#   nats(2) → sift(2) → sift(3) → sift(5) → sift(7) → sift(11) → ...
#
# The caller decides how many primes to consume; `take N` stops the pipeline
# at any point without wasting work on values that will never be requested.
# =========================================================================
(println "")
(println "--- 1. Sieve of Eratosthenes ---")
(println "")

# Eager -- classic bit-sieve; requires an upper bound
(de primes-up-to (limit)
  (let ((sieve (vec (1+ limit) T)) (result NIL))
    (vec-set sieve 0 NIL)
    (vec-set sieve 1 NIL)
    (let ((i 2))
      (while (<= i limit)
        (if (vec-get sieve i)
          (let ((j (* i i)))
            (while (<= j limit)
              (vec-set sieve j NIL)
              (setq j (+ j i)))))
        (setq i (1+ i))))
    (let ((i 2))
      (while (<= i limit)
        (if (vec-get sieve i) (setq result (cons i result)))
        (setq i (1+ i))))
    (reverse result)))

# nats-from: infinite source of integers starting at n
(de nats-from (n)
  (co (lambda (_)
    (let ((i n))
      (while T (yield i) (setq i (1+ i)))))))

# sift: a single filter stage -- passes through values from src not
# divisible by p. Each call to `sift` creates one coroutine in the chain.
(de sift (src p)
  (co (lambda (_)
    (while T
      (let ((v (co-resume src NIL)))
        (if (not (= 0 (mod v p))) (yield v)))))))

# primes-coro: the sieve itself. Each value pulled from src is a prime
# (it has survived all prior filter stages). Yielding it and inserting a
# new sift stage ensures subsequent values are filtered by this prime too.
(de primes-coro ()
  (co (lambda (_)
    (let ((src (nats-from 2)))
      (while T
        (let ((p (co-resume src NIL)))
          (yield p)
          (setq src (sift src p))))))))

(println "Eager (<=50):      " (primes-up-to 50))
(println "Lazy  (first 15): " (take 15 (primes-coro)))
(println "")
(println "The pipeline after generating the first 5 primes looks like:")
(println "  nats(2) -> sift(2) -> sift(3) -> sift(5) -> sift(7) -> sift(11) -> ...")
(println "Each stage is a live coroutine. (take N) stops it at any point.")

# =========================================================================
# 2. Binary tree in-order traversal
#
# The eager version accumulates the entire sorted list before returning --
# the caller cannot see any node until the traversal is complete.
#
# The coroutine version uses the identical recursive structure but replaces
# the list accumulation with `yield`. Key insight: `yield` works from any
# depth of recursion inside a coroutine body. The recursive helper calls
# itself normally; when it hits a leaf it yields the node value and the
# entire call stack is suspended at that exact point.
#
# This makes tree traversal lazy: the caller can pull nodes one at a time,
# stop early after finding what it needs, or pipeline the output into another
# stage -- all without restructuring the recursive algorithm or materialising
# the full sorted list. With a balanced BST of N nodes, early exit after k
# results visits only O(k + log N) nodes instead of all N.
# =========================================================================
(println "")
(println "--- 2. Binary tree in-order traversal ---")
(println "")

# Tree node constructor: (value left-child right-child); NIL = empty leaf
(de node (v l r) (list v l r))

# Eager -- builds the entire sorted list before the caller sees anything
(de inorder-eager (tree)
  (if (null tree) NIL
    (append
      (append
        (inorder-eager (cadr tree))
        (list (car tree)))
      (inorder-eager (caddr tree)))))

# Coroutine -- the recursive helper calls (yield) at each node.
# No list is ever built; the caller receives nodes one at a time.
(de inorder-walk (n)
  (if n
    (prog
      (inorder-walk (cadr n))
      (yield (car n))
      (inorder-walk (caddr n)))))

(de inorder-gen (tree)
  (co (lambda (_) (inorder-walk tree))))

#           10
#          /  \
#         5    15
#        / \  /  \
#       2   7 12  20
#          /
#         6
(setq bst
  (node 10
    (node 5
      (node 2 NIL NIL)
      (node 7 (node 6 NIL NIL) NIL))
    (node 15
      (node 12 NIL NIL)
      (node 20 NIL NIL))))

(setq eager-result (inorder-eager bst))
(println "Eager inorder:  " eager-result)
(println "Lazy  inorder:  " (drain (inorder-gen bst)))

# Early exit: find the two smallest values > 6 without full traversal.
# The right subtree (12, 15, 20) is never visited.
(let ((gen (inorder-gen bst)) (found NIL))
  (while (and (co-alive? gen) (< (length found) 2))
    (let ((v (co-resume gen NIL)))
      (if (> v 6) (setq found (append found (list v))))))
  (println "Two smallest > 6: " found " (right subtree never visited)"))

# =========================================================================
# 3. Permutations -- lazy search cursor with early termination
#
# The eager version generates all n! permutations and returns a list.
# For n=10 that is 3,628,800 permutations -- even if you only need the first.
#
# The coroutine version uses the identical recursive logic but replaces the
# accumulation step with `yield`. The coroutine acts as a search cursor: it
# explores the permutation search space lazily, yielding each permutation as
# it is found, without computing the ones that follow.
#
# This enables early termination: as soon as the caller finds what it needs,
# it simply stops calling co-resume. The remaining subtree of the search
# space is never explored. For large n or complex predicates this can be
# many orders of magnitude faster than the eager approach.
# =========================================================================
(println "")
(println "--- 3. Permutations ---")
(println "")

(de remove-first (x lst)
  (if (null lst) NIL
    (if (equal x (car lst)) (cdr lst)
      (cons (car lst) (remove-first x (cdr lst))))))

# Eager -- returns all n! permutations as a list of lists
(de permutations-eager (prefix remaining)
  (if (null remaining)
    (list prefix)
    (apply append
      (mapcar (lambda (x)
        (permutations-eager
          (append prefix (list x))
          (remove-first x remaining)))
        remaining))))

# Coroutine -- same recursion structure; (yield) replaces accumulation.
# perm-walk is defined outside co-new so it can call itself recursively;
# it must only be invoked from within a coroutine body.
(de perm-walk (prefix remaining)
  (if (null remaining)
    (yield prefix)
    (mapc (lambda (x)
      (perm-walk
        (append prefix (list x))
        (remove-first x remaining)))
      remaining)))

(de permutations-gen (lst)
  (co (lambda (_) (perm-walk NIL lst))))

(println "All permutations of (1 2 3):")
(mapc (lambda (p) (println "  " p))
      (drain (permutations-gen (list 1 2 3))))

# Search: find first permutation of (1 2 3 4) where sum of first two > 6.
# The eager approach must generate all 24 permutations first.
# The coroutine stops the moment it finds a match -- often far fewer checks.
(println "")
(let ((gen (permutations-gen (list 1 2 3 4)))
      (found NIL)
      (checked 0))
  (while (and (co-alive? gen) (not found))
    (let ((p (co-resume gen NIL)))
      (setq checked (1+ checked))
      (if (> (+ (car p) (cadr p)) 6)
        (setq found p))))
  (println "First perm of (1 2 3 4) with first-two sum > 6:")
  (println "  " found)
  (println "  Checked " checked " of 24 permutations (stopped early)."))

# =========================================================================
# 4. Lazy flatten of nested lists
#
# The eager version uses depth-first recursion to build the entire flat list
# before returning -- all structure must be traversed even if only the first
# atom is needed.
#
# The coroutine version uses an identical recursive walk but calls `yield`
# instead of consing atoms onto a result list. Each atom is only processed
# when the consumer asks for the next value. This makes the flatten lazy:
# the caller can stop mid-structure without visiting the remaining nodes.
#
# Because `yield` works at any recursion depth, the recursive walk looks
# exactly like the eager version -- no continuation-passing or trampolining
# required. The coroutine runtime handles the stack suspension automatically.
# =========================================================================
(println "")
(println "--- 4. Lazy flatten ---")
(println "")

# Eager -- fully traverses the structure before returning
(de flatten-eager (x)
  (cond
    ((null x) NIL)
    ((pair x) (append (flatten-eager (car x)) (flatten-eager (cdr x))))
    (T (list x))))

# flatten-walk and flatten-gen are defined at top level so they can
# recurse naturally. They call (yield), so they must only be invoked
# from inside a coroutine body.
(de flatten-walk (x)
  (cond
    ((null x))
    ((pair x) (mapc flatten-walk x))
    (T (yield x))))

(de flatten-gen (lst)
  (co (lambda (_) (mapc flatten-walk lst))))

(setq deep (list 1 (list 2 (list 3 (list 4 (list 5 (list 6 7 8))) 9) 10) 11))
(println "Nested:   " deep)
(println "Eager:    " (flatten-eager deep))
(println "Lazy:     " (drain (flatten-gen deep)))

# Pipeline: flatten then keep only even numbers.
# The evens filter wraps the flatten generator -- it pulls atoms one at a
# time and yields only those satisfying the predicate. Two coroutines;
# no intermediate list.
(de evens-from-nested (lst)
  (co (lambda (_)
    (let ((src (flatten-gen lst)))
      (let ((v (co-resume src NIL)))
        (while (co-alive? src)
          (if (= 0 (mod v 2)) (yield v))
          (setq v (co-resume src NIL)))
        (if (and v (= 0 (mod v 2))) (yield v)))))))

(println "Even atoms from nested list: " (drain (evens-from-nested deep)))

# Early exit: find the first atom > 5, deep inside the structure.
# Traversal stops the moment the atom is found -- remaining structure is
# never visited.
(let ((gen (flatten-gen deep)) (found NIL))
  (while (and (co-alive? gen) (not found))
    (let ((v (co-resume gen NIL)))
      (if (> v 5) (setq found v))))
  (println "First atom > 5: " found " (traversal stopped at that point)"))

(println "")
(println "=== Done ===")
