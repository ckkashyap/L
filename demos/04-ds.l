# ============================================================
# 04-ds.l -- Persistent hash maps and persistent vectors
#
# What this covers:
#   1. pmap -- persistent hash map
#   2. pvec -- persistent vector
#   3. Structural sharing -- independence of derived structures
#   4. Choosing the right structure -- pmap vs alist vs pvec vs vec
#
# Run:  L demos/04-ds.l
# Spec: L.spec.txt Part 4 -- Persistent Data Structures
# ============================================================

(load "lib/ds.l")

(println "=== Persistent Data Structures Demo ===")

# ---- 1. pmap -- persistent hash map ------------------------
# `pmap` is an immutable hash map. Every operation (`pmap-put`,
# `pmap-del`, `pmap-merge`) returns a NEW map; the original is
# unchanged. This is not copying -- structural sharing makes it
# efficient. Use pmap for functional-style code where you need
# to track history or share state safely.
#
# If you come from Python, think of pmap as dict -- except that
# `m["key"] = value` is illegal; instead you call `pmap-put`
# and receive a fresh map. In JavaScript terms, pmap behaves
# like Object.freeze() plus spread: `{ ...old, key: value }`.
#
# The chain of `let` bindings below threads each updated map
# through to the next step. `m` is the empty base; `m1`, `m2`,
# `m3` are successive versions. All four coexist simultaneously
# in memory, sharing the parts they have in common.

(println "Persistent Maps:")
(let ((m (pmap-new)))
  (let ((m1 (pmap-put m "name" "Alice")))
    (let ((m2 (pmap-put m1 "age" 30)))
      (let ((m3 (pmap-put m2 "city" "NYC")))
        (println "Name:" (pmap-get m3 "name"))   # => Alice
        (println "Age:" (pmap-get m3 "age"))     # => 30
        (println "City:" (pmap-get m3 "city"))   # => NYC
        (println "Count:" (pmap-count m3))       # => 3

        # ---- 3. Structural sharing -------------------------
        # The key property: updating m3 to produce m4 does NOT
        # affect m3 or any of its ancestors. m1 still has only
        # one entry; m has none. This is the guarantee that makes
        # persistent structures safe to share across coroutines
        # or pass to functions without defensive copying.

        # Original maps unchanged
        (println "m1 count:" (pmap-count m1))       # => 1
        (println "m has name?:" (pmap-get m "name")) # => NIL

        # Delete returns a new map with the key removed.
        # m3 is unaffected -- it still carries "age".
        (let ((m4 (pmap-del m3 "age")))
          (println "After del age, count:" (pmap-count m4))  # => 2
          (println "age still in m3:" (pmap-get m3 "age")))  # => 30
        ))))

# ---- 2. pvec -- persistent vector --------------------------
# `pvec` is an immutable vector. `pvec-put` at index i returns
# a new vector with that slot changed. `pvec-push` appends.
# The original is unchanged. Like pmap, structural sharing
# keeps this efficient for most update patterns.
#
# `pvec` takes its initial elements as arguments (variadic).
# `pvec-seq` converts a pvec to an ordinary L list for printing.
# Indices are 0-based. Accessing an out-of-range index returns NIL.
#
# When to prefer pvec over a plain list:
#   - You need O(1) indexed access (lists are O(n) to index).
#   - You're building a functional pipeline that transforms
#     vectors step by step and needs the old versions around.

(println "Persistent Vectors:")
(let ((v (pvec 10 20 30 40 50)))
  (println "v:" (pvec-seq v))          # => (10 20 30 40 50)
  (println "v[2]:" (pvec-get v 2))     # => 30

  # pvec-put returns a new vector; v is unchanged.
  (let ((v2 (pvec-put v 2 99)))
    (println "v2 after put [2]=99:" (pvec-seq v2))   # => (10 20 99 40 50)
    (println "v[2] unchanged:" (pvec-get v 2)))       # => 30

  # pvec-push appends an element, returning a new vector.
  (let ((v3 (pvec-push v 60)))
    (println "v3 after push 60:" (pvec-seq v3))))     # => (10 20 30 40 50 60)

# ---- 4. Choosing the right structure ----------------------
# L offers several associative and sequential structures.
# Here is a quick decision guide:
#
#   pmap  -- persistent, functional, equality-based key lookup.
#           Use when you need immutability, history, or safe
#           sharing. O(log n) operations via hash-array mapped
#           trie internally.
#
#   alist -- association list: ((key . val) ...). Small maps only.
#           O(n) lookup, but zero overhead for tiny tables and
#           trivially inspectable in the REPL.
#
#   plist -- property list: (key val key val ...) stored on a
#           symbol. The OOP-style store used by `de`, `dmacro`,
#           and the object system. Fine for per-symbol metadata.
#
#   vec   -- mutable indexed array. Use for performance-critical
#           inner loops where you need O(1) set-by-index with no
#           allocation. Not safe to share across coroutines
#           without explicit locking.
#
#   pvec  -- persistent indexed vector. Use for functional
#           pipelines that transform sequences step by step and
#           need old snapshots. O(log n) updates.

(println "Done.")
