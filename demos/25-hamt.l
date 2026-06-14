# ============================================================
# 25-hamt.l -- Persistent Hash Maps (HAMT)
#
# What this covers:
#   1. What is a persistent map?
#   2. Creating and querying maps
#   3. Structural sharing -- immutability without copying
#   4. Building up large maps efficiently
#   5. Real-world pattern: word frequency counter
#   6. Choosing between pmap, alist, and pvec
#
# Run:  L demos/25-hamt.l
# ============================================================

(println "=== Persistent Hash Maps (HAMT) ===")
(println "")

# ---- 1. What is a persistent map? --------------------------
#
# A persistent map is an immutable key-value store. Every
# "update" returns a BRAND NEW map -- the original is never
# modified. This makes it safe to share maps across functions,
# coroutines, and undo histories without defensive copying.
#
# Under the hood, pmap uses a Hash Array Mapped Trie (HAMT):
# a 32-way tree indexed by 5-bit chunks of each key's hash.
# Lookups and updates are O(log₃₂ n) -- at most 7 steps for
# billions of entries. In practice, it feels like O(1).

# ---- 2. Creating and querying maps -------------------------

(println "--- Basic operations ---")

(setq m (pmap-new))
(println "Empty map count: " (pmap-count m))      # => 0

# pmap-put returns a new map with the key added
(setq m (pmap-put m "name"  "Alice"))
(setq m (pmap-put m "age"   30))
(setq m (pmap-put m "lang"  "Lisp"))

(println "name: " (pmap-get m "name"))             # => Alice
(println "age:  " (pmap-get m "age"))              # => 30
(println "lang: " (pmap-get m "lang"))             # => Lisp
(println "miss: " (pmap-get m "email"))            # => NIL
(println "count:" (pmap-count m))                  # => 3

# ---- 3. Structural sharing ---------------------------------
#
# When you "update" a key, only the path from root to that
# leaf is copied -- all other branches are shared with the
# original map. This means creating a new map from an old
# one is cheap: O(log₃₂ n) allocations, not O(n).

(println "")
(println "--- Immutability ---")

(setq original (pmap-put (pmap-put (pmap-new) "x" 1) "y" 2))
(setq updated  (pmap-put original "x" 99))

(println "original x: " (pmap-get original "x"))   # => 1  (unchanged!)
(println "updated  x: " (pmap-get updated "x"))    # => 99

# Delete also returns a new map
(setq smaller (pmap-del updated "y"))
(println "updated  y: " (pmap-get updated "y"))     # => 2  (still there)
(println "smaller  y: " (pmap-get smaller "y"))     # => NIL (gone)
(println "smaller  x: " (pmap-get smaller "x"))     # => 99 (kept)

# ---- 4. Building up large maps -----------------------------

(println "")
(println "--- Scaling up ---")

(setq big (pmap-new))
(setq i 0)
(while (< i 1000)
  (setq big (pmap-put big (str i) (* i i)))
  (setq i (1+ i)))

(println "1000-entry map count: " (pmap-count big))
(println "  get 0:   " (pmap-get big "0"))          # => 0
(println "  get 500: " (pmap-get big "500"))        # => 250000
(println "  get 999: " (pmap-get big "999"))        # => 998001

# ---- 5. Word frequency counter -----------------------------
#
# A classic use case: count how many times each word appears.
# pmap-put to increment, pmap-get to check current count.

(println "")
(println "--- Word frequency ---")

(setq words (list "the" "cat" "sat" "on" "the" "mat"
                  "the" "cat" "on" "the" "hat"))

(setq freq (pmap-new))
(mapc (lambda (w)
        (let ((count (pmap-get freq w)))
          (setq freq (pmap-put freq w (if count (1+ count) 1)))))
      words)

(println "  the: " (pmap-get freq "the"))           # => 4
(println "  cat: " (pmap-get freq "cat"))            # => 2
(println "  sat: " (pmap-get freq "sat"))            # => 1

# ---- 6. Merge and list operations --------------------------

(println "")
(println "--- Merge ---")

(setq defaults (pmap-put (pmap-put (pmap-new)
                 "color" "blue") "size" 10))
(setq overrides (pmap-put (pmap-new) "color" "red"))
(setq config (pmap-merge defaults overrides))

(println "  color: " (pmap-get config "color"))     # => red (override wins)
(println "  size:  " (pmap-get config "size"))       # => 10  (from defaults)

(println "  keys: " (pmap-keys config))
(println "  vals: " (pmap-vals config))

# ---- 7. Choosing the right data structure -------------------
#
#   Need                          → Use
#   -------------------------------------------
#   Key-value lookup by any key   → pmap
#   Ordered pairs, small (< 20)   → alist + assoc
#   Indexed by integer position   → pvec
#   Mutable, append-heavy         → vec
#   FIFO queue / stack            → list (cons/car/cdr)

(println "")
(println "Done.")
