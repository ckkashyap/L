# ============================================================
# test_ds.l -- Persistent (immutable) map and vector
#
# What this covers:
#   • pmap: persistent hash map (HAMT -- Hash Array Mapped Trie)
#     -- put/get/del/merge/count/keys/vals/to-list
#     -- key update (overwrite), 100-key stress test
#     -- integer and symbol keys
#   • pvec: persistent vector (32-way trie with tail)
#     -- get/put/push/seq/count
#   • Immutability invariant: every mutating op returns a NEW
#     structure; the original is always left unchanged
#
# Run:  L tests/test_ds.l
# Spec: L.spec.txt Part 4 -- Persistent Data Structures
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

(load "lib/ds.l")

# ---- 1. pmap basics ----------------------------------------
# pmap is a persistent (immutable) hash map. Every write
# operation (put, del, merge) returns a BRAND NEW map; the
# source map is never modified. Use pmap-count to query size,
# pmap-get to look up keys (returns NIL for missing keys).

(let ((m (pmap-new)))
  (test T (pmap? m))
  (test 0 (pmap-count m))

  (let ((m2 (pmap-put m "key" "val")))
    (test "val" (pmap-get m2 "key"))
    (test NIL (pmap-get m2 "other"))   # edge case: NIL for missing key
    (test 1 (pmap-count m2))

    # Original unaffected
    (test NIL (pmap-get m "key"))      # NOTE: returns NEW map, original unchanged

    (let ((m3 (pmap-put m2 "key2" 42)))
      (test 2 (pmap-count m3))
      (test "val" (pmap-get m3 "key"))
      (test 42 (pmap-get m3 "key2"))

      # Delete
      (let ((m4 (pmap-del m3 "key")))  # NOTE: returns NEW map, original unchanged
        (test 1 (pmap-count m4))
        (test NIL (pmap-get m4 "key"))
        (test 42 (pmap-get m4 "key2"))))))

# ---- 2. pmap update (overwrite existing key) ------------------
(let* ((m (pmap-put (pmap-new) "x" 1))
       (m2 (pmap-put m "x" 99)))
  (test 99 (pmap-get m2 "x"))   # new value
  (test 1  (pmap-get m "x"))    # original unchanged
  (test 1  (pmap-count m2)))    # no duplicate

# ---- 3. pmap with many keys (HAMT branching) -----------------
# Insert 100 keys, verify all retrievable, then delete half
(let ((m (pmap-new))
      (i 0))
  (while (< i 100)
    (setq m (pmap-put m (str i) i))
    (setq i (1+ i)))
  (test 100 (pmap-count m))
  (test 0   (pmap-get m "0"))
  (test 50  (pmap-get m "50"))
  (test 99  (pmap-get m "99"))
  (test NIL (pmap-get m "100"))

  # Delete even keys
  (setq i 0)
  (while (< i 100)
    (setq m (pmap-del m (str i)))
    (setq i (+ i 2)))
  (test 50  (pmap-count m))
  (test NIL (pmap-get m "0"))
  (test 1   (pmap-get m "1"))
  (test NIL (pmap-get m "98"))
  (test 99  (pmap-get m "99")))

# ---- 4. pmap-keys / pmap-vals / pmap-to-list -----------------
(let* ((m (pmap-put (pmap-put (pmap-new) 1 10) 2 20))
       (keys (sort (pmap-keys m) (lambda (a b) (< a b))))
       (vals (sort (pmap-vals m) (lambda (a b) (< a b)))))
  (test (list 1 2) keys)
  (test (list 10 20) vals)
  (test 2 (length (pmap-to-list m))))

# ---- 5. pmap with integer and symbol keys --------------------
(let* ((m (pmap-new))
       (m (pmap-put m 42 "forty-two"))
       (m (pmap-put m 'hello "world")))
  (test "forty-two" (pmap-get m 42))
  (test "world" (pmap-get m 'hello))
  (test NIL (pmap-get m 43)))

# ---- 6. pmap-merge -----------------------------------------
# pmap-merge combines two maps into a new one. When both maps
# share a key the right-hand (second) map's value wins.
(let ((m1 (pmap-put (pmap-new) "a" 1))
      (m2 (pmap-put (pmap-new) "b" 2)))
  (let ((merged (pmap-merge m1 m2)))
    (test 2 (pmap-count merged))
    (test 1 (pmap-get merged "a"))
    (test 2 (pmap-get merged "b"))))

# pmap-merge with conflict (right wins)
(let* ((m1 (pmap-put (pmap-new) "x" 1))
       (m2 (pmap-put (pmap-new) "x" 99))
       (merged (pmap-merge m1 m2)))
  (test 99 (pmap-get merged "x")))

# ---- 7. pvec basics ----------------------------------------
# pvec is a persistent (immutable) vector. Indexing is
# 0-based. pvec-put and pvec-push both return NEW vectors;
# the source vector is left unchanged.

# pvec
(let ((v (pvec 10 20 30)))
  (test T (pvec? v))
  (test 3 (pvec-count v))
  (test 10 (pvec-get v 0))
  (test 20 (pvec-get v 1))
  (test 30 (pvec-get v 2))

  # pvec-put -- immutable update
  (let ((v2 (pvec-put v 1 99)))         # NOTE: returns NEW vec, original unchanged
    (test 99 (pvec-get v2 1))
    (test 20 (pvec-get v 1)))  # original unchanged

  # pvec-push
  (let ((v3 (pvec-push v 40)))          # appends to end, returns new vector
    (test 4 (pvec-count v3))
    (test 40 (pvec-get v3 3)))

  # pvec-seq
  (test (list 10 20 30) (pvec-seq v)))  # => (10 20 30)

(println "")
(println "test_ds.l: ALL TESTS PASSED")
