# rope.l -- Weight-balanced binary tree for O(log n) line sequences
#
# Node format: ('wbt count left val right)
#   count  = total items in this subtree (stored for O(1) access)
#   left   = left subtree  (items at indices < this node's position)
#   val    = item at this node
#   right  = right subtree (items at indices > this node's position)
# Empty tree = NIL
#
# Balance invariant (Adams BB-tree, delta=3, gamma=2):
#   size(left)  <= delta * size(right) + epsilon
#   size(right) <= delta * size(left)  + epsilon
#
# Complexity:
#   wbt-count    O(1)   -- stored in header
#   wbt-get      O(log n) -- navigate by subtree counts
#   wbt-set      O(log n) -- path-copy to updated leaf
#   wbt-insert   O(log n) -- insert with rebalancing
#   wbt-delete   O(log n) -- delete with rebalancing
#   wbt-from-list  O(n)   -- balanced construction
#   wbt-to-list    O(n)   -- in-order traversal via accumulator

# ---- Structural accessors ----------------------------------

(de wbt-count (t)
  (if (null t) 0 (cadr t)))

(de wbt--left  (t) (caddr t))
(de wbt--val   (t) (car (cdddr t)))
(de wbt--right (t) (cadr (cdddr t)))

# ---- Node constructor (no rebalancing) --------------------

(de wbt--node (left val right)
  (list 'wbt (+ 1 (wbt-count left) (wbt-count right)) left val right))

# ---- Rotations (called only when rebalancing is needed) ---

# Right rotation (left subtree is heavy).
# Uses single rotation unless the inner (lr) subtree is too big,
# in which case a double rotation is used.
# Condition uses <= so that equal sizes go to single rotation.
(de wbt--rot-right (left val right)
  (let ((ll (wbt--left  left))
        (lv (wbt--val   left))
        (lr (wbt--right left)))
    (if (<= (wbt-count lr) (* 2 (wbt-count ll)))
      # Single right rotation
      (wbt--node ll lv (wbt--node lr val right))
      # Double rotation: rotate left subtree left first
      (let ((lrl (wbt--left  lr))
            (lrv (wbt--val   lr))
            (lrr (wbt--right lr)))
        (wbt--node (wbt--node ll lv lrl)
                   lrv
                   (wbt--node lrr val right))))))

# Left rotation (right subtree is heavy).
(de wbt--rot-left (left val right)
  (let ((rl (wbt--left  right))
        (rv (wbt--val   right))
        (rr (wbt--right right)))
    (if (<= (wbt-count rl) (* 2 (wbt-count rr)))
      # Single left rotation
      (wbt--node (wbt--node left val rl) rv rr)
      # Double rotation: rotate right subtree right first
      (let ((rll (wbt--left  rl))
            (rlv (wbt--val   rl))
            (rlr (wbt--right rl)))
        (wbt--node (wbt--node left val rll)
                   rlv
                   (wbt--node rlr rv rr))))))

# ---- Balance -------------------------------------------

# Rebalance if one side is more than 3x the other; otherwise
# just build a new node (path copy only, no rotation).
(de wbt--balance (left val right)
  (let ((ls (wbt-count left))
        (rs (wbt-count right)))
    (cond
      ((> ls (* 3 rs)) (wbt--rot-right left val right))
      ((> rs (* 3 ls)) (wbt--rot-left  left val right))
      (T               (wbt--node left val right)))))

# ---- Public operations ------------------------------------

# O(log n): get element at 0-based index i
(de wbt-get (tree i)
  (if (null tree) NIL
    (let ((ls (wbt-count (wbt--left tree))))
      (cond
        ((< i ls) (wbt-get (wbt--left  tree) i))
        ((= i ls) (wbt--val tree))
        (T        (wbt-get (wbt--right tree) (- i ls 1)))))))

# O(log n): return new tree with element i replaced (path copy, no resize)
(de wbt-set (tree i val)
  (if (null tree) NIL
    (let ((ls (wbt-count (wbt--left tree))))
      (cond
        ((< i ls)
         (wbt--node (wbt-set (wbt--left tree) i val)
                    (wbt--val tree)
                    (wbt--right tree)))
        ((= i ls)
         (wbt--node (wbt--left tree) val (wbt--right tree)))
        (T
         (wbt--node (wbt--left tree)
                    (wbt--val tree)
                    (wbt-set (wbt--right tree) (- i ls 1) val)))))))

# O(log n): insert val at 0-based position i, shifting i..n-1 right
(de wbt-insert (tree i val)
  (if (null tree)
    (wbt--node NIL val NIL)
    (let ((ls (wbt-count (wbt--left tree))))
      (if (<= i ls)
        (wbt--balance (wbt-insert (wbt--left tree) i val)
                      (wbt--val tree)
                      (wbt--right tree))
        (wbt--balance (wbt--left tree)
                      (wbt--val tree)
                      (wbt-insert (wbt--right tree) (- i ls 1) val))))))

# Internal: minimum value in a non-empty tree
(de wbt--min-val (tree)
  (if (null (wbt--left tree))
    (wbt--val tree)
    (wbt--min-val (wbt--left tree))))

# Internal: delete the minimum element from a non-empty tree
(de wbt--del-min (tree)
  (if (null (wbt--left tree))
    (wbt--right tree)
    (wbt--balance (wbt--del-min (wbt--left tree))
                  (wbt--val tree)
                  (wbt--right tree))))

# Internal: join two already-balanced subtrees
(de wbt--join (left right)
  (cond
    ((null left)  right)
    ((null right) left)
    (T (wbt--balance left
                     (wbt--min-val right)
                     (wbt--del-min right)))))

# O(log n): delete element at 0-based index i
(de wbt-delete (tree i)
  (if (null tree) NIL
    (let ((ls (wbt-count (wbt--left tree))))
      (cond
        ((< i ls)
         (wbt--balance (wbt-delete (wbt--left tree) i)
                       (wbt--val tree)
                       (wbt--right tree)))
        ((= i ls)
         (wbt--join (wbt--left tree) (wbt--right tree)))
        (T
         (wbt--balance (wbt--left tree)
                       (wbt--val tree)
                       (wbt-delete (wbt--right tree) (- i ls 1))))))))

# ---- Conversion -------------------------------------------

# O(n): in-order traversal using accumulator (avoids quadratic append)
(de wbt--to-list-acc (tree acc)
  (if (null tree) acc
    (wbt--to-list-acc (wbt--left tree)
                      (cons (wbt--val tree)
                            (wbt--to-list-acc (wbt--right tree) acc)))))

(de wbt-to-list (tree)
  (wbt--to-list-acc tree NIL))

# wbt-from-list is implemented as a C primitive (prim_wbt_from_list)
# for ~10x speedup vs. an L-level recursive build (interpreter overhead
# dominates for large trees).
