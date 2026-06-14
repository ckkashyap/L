# ds.l -- Persistent data structures
# HAMT maps and persistent vectors implemented in pure Lisp
# using the (hash key nbits) C primitive

# ============================================================
# Persistent Map -- HAMT (Hash Array Mapped Trie)
# ============================================================
#
# O(log₃₂ n) get/put/del via 32-way trie keyed by hash bits.
# Structural sharing: only the path from root to the affected
# leaf is copied on update; everything else is shared.
#
# Node types:
#   NIL                              -- empty
#   (leaf hash key val)              -- single entry
#   (collision hash (k . v) ...)     -- same hash, different keys
#   (branch bitmap child ...)        -- sparse 32-way; bitmap
#                                      indicates populated slots;
#                                      children stored compactly
#
# Uses C primitives: (hash key 32), (popcount n),
#   (bitand), (bitor), (<<), (>>), (bit?)

# ---- Internal node constructors / accessors -----------------

(de hamt--leaf (h k v) (list 'leaf h k v))
(de hamt--leaf? (n) (and (pair n) (eq (car n) 'leaf)))
(de hamt--leaf-hash (n) (cadr n))
(de hamt--leaf-key  (n) (caddr n))
(de hamt--leaf-val  (n) (car (cdddr n)))

(de hamt--coll (h . pairs) (list* 'collision h pairs))
(de hamt--coll? (n) (and (pair n) (eq (car n) 'collision)))
(de hamt--coll-hash  (n) (cadr n))
(de hamt--coll-pairs (n) (cddr n))

(de hamt--branch (bm . children) (list* 'branch bm children))
(de hamt--branch? (n) (and (pair n) (eq (car n) 'branch)))
(de hamt--branch-bm (n) (cadr n))
(de hamt--branch-children (n) (cddr n))

# Index of child in compact array: popcount of bits below idx
# Use (bitnot (<< -1 idx)) instead of (- (<< 1 idx) 1) to avoid
# signed overflow when idx=31: (<< 1 31) = -2147483648 in 32-bit.
(de hamt--child-idx (bm idx)
  (popcount (bitand bm (bitnot (<< -1 idx)))))

# Get child at logical slot idx (0-31) from branch, or NIL
(de hamt--child-at (node idx)
  (let ((bm (hamt--branch-bm node)))
    (if (bit? bm idx)
      (car (nth (hamt--child-idx bm idx) (hamt--branch-children node)))
      NIL)))

# Return new branch with child at logical slot idx replaced/inserted
(de hamt--branch-set (node idx child)
  (let* ((bm   (hamt--branch-bm node))
         (kids (hamt--branch-children node))
         (ci   (hamt--child-idx bm idx)))
    (if (bit? bm idx)
      # Replace existing child
      (list* 'branch bm (hamt--list-set kids ci child))
      # Insert new child
      (list* 'branch (bitor bm (<< 1 idx))
             (hamt--list-insert kids ci child)))))

# Return new branch with child at logical slot idx removed
(de hamt--branch-del (node idx)
  (let* ((bm   (hamt--branch-bm node))
         (kids (hamt--branch-children node))
         (ci   (hamt--child-idx bm idx)))
    (list* 'branch (bitand bm (bitnot (<< 1 idx)))
           (hamt--list-remove kids ci))))

# ---- List helpers for compact child arrays ------------------

(de hamt--list-set (lst i val)
  (if (= i 0) (cons val (cdr lst))
    (cons (car lst) (hamt--list-set (cdr lst) (1- i) val))))

(de hamt--list-insert (lst i val)
  (if (= i 0) (cons val lst)
    (cons (car lst) (hamt--list-insert (cdr lst) (1- i) val))))

(de hamt--list-remove (lst i)
  (if (= i 0) (cdr lst)
    (cons (car lst) (hamt--list-remove (cdr lst) (1- i)))))

# ---- Core HAMT operations ----------------------------------

# Extract 5-bit chunk from hash at given depth (0, 5, 10, ...)
(de hamt--idx (h depth) (bitand (>> h depth) 31))

# Get: walk trie using 5-bit hash chunks
(de hamt--get (node key h depth)
  (cond
    ((null node) NIL)
    ((hamt--leaf? node)
     (if (equal key (hamt--leaf-key node))
       (hamt--leaf-val node)
       NIL))
    ((hamt--coll? node)
     (let ((pair (assoc key (hamt--coll-pairs node))))
       (if pair (cdr pair) NIL)))
    ((hamt--branch? node)
     (let ((idx (hamt--idx h depth)))
       (hamt--get (hamt--child-at node idx) key h (+ depth 5))))
    (T NIL)))

# Put: returns new trie with key→val inserted/updated
(de hamt--put (node key val h depth)
  (cond
    ((null node)
     (hamt--leaf h key val))
    ((hamt--leaf? node)
     (let ((eh (hamt--leaf-hash node))
           (ek (hamt--leaf-key node)))
       (cond
         ((equal key ek)
          # Replace value
          (hamt--leaf h key val))
         ((= h eh)
          # Hash collision: promote to collision node
          (list 'collision h (cons key val) (cons ek (hamt--leaf-val node))))
         (T
          # Different hashes: create branch with both leaves
          (let* ((idx1 (hamt--idx eh depth))
                 (idx2 (hamt--idx h depth)))
            (if (= idx1 idx2)
              # Same slot at this level: recurse deeper
              (let ((sub (hamt--put node key val h (+ depth 5))))
                (hamt--branch-set (list 'branch 0) idx1 sub))
              # Different slots: branch with two children
              (let* ((b (list 'branch 0))
                     (b (hamt--branch-set b idx1 node))
                     (b (hamt--branch-set b idx2 (hamt--leaf h key val))))
                b)))))))
    ((hamt--coll? node)
     (let ((ch (hamt--coll-hash node)))
       (if (= h ch)
         # Add/update in collision list
         (let ((pairs (hamt--coll-pairs node))
               (new-pairs (cons (cons key val)
                           (filter (lambda (p) (not (equal (car p) key))) pairs))))
           (list* 'collision ch new-pairs))
         # Different hash: wrap collision in branch and insert
         (let* ((idx1 (hamt--idx ch depth))
                (idx2 (hamt--idx h depth))
                (b (hamt--branch-set (list 'branch 0) idx1 node)))
           (if (= idx1 idx2)
             (hamt--branch-set (list 'branch 0) idx1
               (hamt--put node key val h (+ depth 5)))
             (hamt--branch-set b idx2 (hamt--leaf h key val)))))))
    ((hamt--branch? node)
     (let* ((idx   (hamt--idx h depth))
            (child (hamt--child-at node idx))
            (new-c (hamt--put child key val h (+ depth 5))))
       (hamt--branch-set node idx new-c)))
    (T (hamt--leaf h key val))))

# Del: returns new trie with key removed
(de hamt--del (node key h depth)
  (cond
    ((null node) NIL)
    ((hamt--leaf? node)
     (if (equal key (hamt--leaf-key node)) NIL node))
    ((hamt--coll? node)
     (let ((pairs (filter (lambda (p) (not (equal (car p) key)))
                          (hamt--coll-pairs node))))
       (cond
         ((null pairs) NIL)
         ((null (cdr pairs)) (hamt--leaf (hamt--coll-hash node) (car (car pairs)) (cdr (car pairs))))
         (T (list* 'collision (hamt--coll-hash node) pairs)))))
    ((hamt--branch? node)
     (let* ((idx   (hamt--idx h depth))
            (bm    (hamt--branch-bm node)))
       (if (not (bit? bm idx))
         node
         (let* ((child (hamt--child-at node idx))
                (new-c (hamt--del child key h (+ depth 5))))
           (if (null new-c)
             # Child gone: remove slot; collapse single-child branches
             (let ((new-bm (bitand bm (bitnot (<< 1 idx)))))
               (if (= new-bm 0) NIL
                 (let ((b (hamt--branch-del node idx)))
                   (if (and (= (popcount new-bm) 1)
                            (hamt--leaf? (car (hamt--branch-children b))))
                     (car (hamt--branch-children b))
                     b))))
             (hamt--branch-set node idx new-c))))))
    (T node)))

# Collect all key-value pairs from a trie
(de hamt--entries (node)
  (cond
    ((null node) NIL)
    ((hamt--leaf? node) (list (cons (hamt--leaf-key node) (hamt--leaf-val node))))
    ((hamt--coll? node) (hamt--coll-pairs node))
    ((hamt--branch? node)
     (let ((result NIL)
           (kids (hamt--branch-children node)))
       (while kids
         (setq result (append (hamt--entries (car kids)) result))
         (setq kids (cdr kids)))
       result))
    (T NIL)))

# Count entries in a trie
(de hamt--count (node)
  (cond
    ((null node) 0)
    ((hamt--leaf? node) 1)
    ((hamt--coll? node) (length (hamt--coll-pairs node)))
    ((hamt--branch? node)
     (let ((sum 0) (kids (hamt--branch-children node)))
       (while kids
         (setq sum (+ sum (hamt--count (car kids))))
         (setq kids (cdr kids)))
       sum))
    (T 0)))

# ---- Public API (same signatures as before) -----------------

(de pmap-new () (cons 'pmap NIL))

(de pmap? (m) (and (pair m) (eq (car m) 'pmap)))

(de pmap-get (m key)
  (if (pmap? m)
    (hamt--get (cdr m) key (hash key 32) 0)
    NIL))

(de pmap-put (m key val)
  (if (pmap? m)
    (cons 'pmap (hamt--put (cdr m) key val (hash key 32) 0))
    (cons 'pmap (hamt--leaf (hash key 32) key val))))

(de pmap-del (m key)
  (if (pmap? m)
    (cons 'pmap (hamt--del (cdr m) key (hash key 32) 0))
    (pmap-new)))

(de pmap-keys (m)
  (if (pmap? m) (mapcar car (hamt--entries (cdr m))) NIL))

(de pmap-vals (m)
  (if (pmap? m) (mapcar cdr (hamt--entries (cdr m))) NIL))

(de pmap-count (m)
  (if (pmap? m) (hamt--count (cdr m)) 0))

(de pmap-merge (m1 m2)
  (if (pmap? m2)
    (let ((result m1)
          (pairs (hamt--entries (cdr m2))))
      (while pairs
        (setq result (pmap-put result (car (car pairs)) (cdr (car pairs))))
        (setq pairs (cdr pairs)))
      result)
    m1))

(de pmap-to-list (m)
  (if (pmap? m) (hamt--entries (cdr m)) NIL))

# ============================================================
# Persistent Vector -- Clojure-style 32-way branching trie
# ============================================================
#
# Internal representation: (pvec count shift root tail)
#   count -- total element count                (O(1) access)
#   shift -- tree height x 5: 5 for 1-level,   10 for 2-level, etc.
#   root  -- trie root node (list of <= 32 children), or NIL
#   tail  -- list of <= 32 elements (the "tail" optimisation)
#
# A "node" is a plain list of up to 32 elements (child nodes or values).
# Leaves contain actual values.  Internal nodes contain child nodes.
# Maximum tree depth for 2^31 elements: ceil(31/5) = 7 levels.
#
# Complexity (n = pvec-count):
#   pvec-count  O(1)              stored in header
#   pvec-get    O(log_32 n) ~ O(1)  at most 7 node lookups
#   pvec-put    O(log_32 n) ~ O(1)  path-copy, only changed nodes
#   pvec-push   O(1) amortised      tail has room 97% of the time
#   pvec-seq    O(n)              DFS of tree then append tail

# ---- Internal structural accessors -------------------------

(de pv-cnt   (v) (cadr v))
(de pv-shift (v) (caddr v))
(de pv-root  (v) (car (cdddr v)))
(de pv-tail  (v) (cadr (cdddr v)))

(de make-pvec (cnt shift root tail)
  (list 'pvec cnt shift root tail))

(de make-empty-pvec ()
  (list 'pvec 0 5 NIL NIL))

# ---- Tail offset -------------------------------------------
# tailoff(cnt) = lowest multiple of 32 that is < cnt
# = floor((cnt-1)/32)*32
# Elements [0..tailoff-1] live in the tree; [tailoff..cnt-1] in tail.
# Special case: cnt < 32 -> tailoff=0 (all elements are in the tail).

(de pv-tailoff (cnt)
  (if (< cnt 32) 0 (<< (>> (- cnt 1) 5) 5)))

# ---- Node helpers (a node is a list of up to 32 elements) -

# Get element i from a node (0-based)
(de node-nth (node i)
  (car (nth i node)))

# Return a new node with element i replaced by val.
# Also handles i == (length node), i.e. appending a new element.
(de node-assoc (node i val)
  (if (= i 0)
    (if (pair node)
      (cons val (cdr node))    # replace existing first element
      (list val))              # node was NIL/empty -> create singleton
    (cons (car node)
          (node-assoc (cdr node) (1- i) val))))

# ---- Path-copy update (pvec-put) ---------------------------

# Returns a new subtree rooted at 'node' with element i
# replaced by val.  Only nodes on the path from root to the
# affected leaf are copied; all other subtrees are shared.
(de pvec--do-assoc (level node i val)
  (if (= level 0)
    (node-assoc node (bitand i 31) val)
    (let ((idx (bitand (>> i level) 31)))
      (node-assoc node idx
        (pvec--do-assoc (- level 5) (node-nth node idx) i val)))))

# ---- Push a full tail into the trie (pvec-push) -----------

# Create a single right-spine of wrapper nodes leading to 'node'.
# pvec-new-path 5  leaf => (leaf)            1-level wrapper
# pvec-new-path 10 leaf => ((leaf))          2-level wrapper
(de pvec--new-path (level node)
  (if (= level 0)
    node
    (list (pvec--new-path (- level 5) node))))

# Recursively insert tail-node into the tree at the correct position.
# cnt is the count BEFORE adding the new element (tail was full).
(de pvec--insert-tail (cnt level parent tail-node)
  (let ((subidx (bitand (>> (- cnt 1) level) 31)))
    (if (= level 5)
      # Leaf-parent level: add tail-node as the next child
      (node-assoc parent subidx tail-node)
      # Internal level: descend into existing child or create new path
      (let ((child (if (< subidx (length parent))
                     (node-nth parent subidx)
                     NIL)))
        (node-assoc parent subidx
          (if child
            (pvec--insert-tail cnt (- level 5) child tail-node)
            (pvec--new-path (- level 5) tail-node)))))))

# Push full tail into tree.  Returns (new-shift new-root).
# Overflow occurs when the root is completely full; in that case
# the tree gains one extra level and shift increases by 5.
(de pvec--push-tree (cnt shift root tail)
  (if (> (>> cnt 5) (<< 1 shift))
    # Tree full: grow one level
    (list (+ shift 5)
          (list root (pvec--new-path shift tail)))
    # Room in tree: insert without growing
    (list shift
          (pvec--insert-tail cnt shift root tail))))

# ---- pvec-seq helpers: O(n) DFS ---------------------------

# Collect all elements from a subtree into a flat list.
(de pvec--collect-tree (node level)
  (if (null node) NIL
    (if (= level 0)
      node                            # leaf: elements are the list itself
      (pvec--concat-children node (- level 5)))))

# Concatenate elements from all children at 'child-level'.
# Right-recursive append is O(n) total: each element copied once.
(de pvec--concat-children (children child-level)
  (if (null children) NIL
    (append (pvec--collect-tree (car children) child-level)
            (pvec--concat-children (cdr children) child-level))))

# ---- Public API -------------------------------------------

(de pvec? (v)
  (and (pair v) (eq (car v) 'pvec)))

(de pvec-count (v)
  (if (pvec? v) (pv-cnt v) 0))

# O(log_32 n): extract 5-bit chunks of index to walk down the trie,
# then index into the leaf (or tail directly).
(de pvec-get (v i)
  (if (pvec? v)
    (let* ((cnt  (pv-cnt v))
           (toff (pv-tailoff cnt)))
      (if (>= i toff)
        # Element lives in the tail
        (node-nth (pv-tail v) (- i toff))
        # Element lives in the trie: walk from root to leaf
        (let ((node  (pv-root v))
              (level (pv-shift v)))
          (while (> level 0)
            (setq node  (node-nth node (bitand (>> i level) 31)))
            (setq level (- level 5)))
          (node-nth node (bitand i 31)))))
    NIL))

# O(log_32 n): path-copy from root to the updated leaf.
(de pvec-put (v i val)
  (if (pvec? v)
    (let* ((cnt   (pv-cnt v))
           (shift (pv-shift v))
           (root  (pv-root v))
           (tail  (pv-tail v))
           (toff  (pv-tailoff cnt)))
      (if (>= i toff)
        # Update in tail (copy tail, share tree)
        (make-pvec cnt shift root
                   (node-assoc tail (- i toff) val))
        # Update in trie (structural sharing: only path is copied)
        (make-pvec cnt shift
                   (pvec--do-assoc shift root i val)
                   tail)))
    NIL))

# O(1) amortised: 97% of calls simply append to the tail in O(1).
# When the tail fills up (every 32nd push), it is flushed into the
# trie in O(log_32 n) and a new single-element tail is started.
(de pvec-push (v x)
  (if (pvec? v)
    (let* ((cnt  (pv-cnt v))
           (tail (pv-tail v))
           (tlen (length tail)))
      (if (< tlen 32)
        # Fast path: room in tail
        (make-pvec (+ cnt 1)
                   (pv-shift v)
                   (pv-root v)
                   (node-assoc tail tlen x))
        # Slow path (1-in-32): flush tail into trie, start new tail
        (let* ((result    (pvec--push-tree cnt (pv-shift v) (pv-root v) tail))
               (new-shift (car result))
               (new-root  (cadr result)))
          (make-pvec (+ cnt 1) new-shift new-root (list x)))))
    # Fallback: treat non-pvec as empty
    (make-pvec 1 5 NIL (list x))))

# O(n) via rebuild -- pvec-pop is not used in any hot path.
(de pvec-pop (v)
  (if (and (pvec? v) (> (pv-cnt v) 0))
    (pvec-from-list (reverse (cdr (reverse (pvec-seq v)))))
    v))

# O(n): DFS of trie to collect all tree elements, then append tail.
(de pvec-seq (v)
  (if (pvec? v)
    (append (pvec--collect-tree (pv-root v) (pv-shift v))
            (pv-tail v))
    NIL))

# ---- Constructor ------------------------------------------

# Build a pvec from a Lisp list via repeated pvec-push.
(de pvec-from-list (lst)
  (let ((v    (make-empty-pvec))
        (rest lst))
    (while rest
      (setq v    (pvec-push v (car rest)))
      (setq rest (cdr rest)))
    v))

# Variadic constructor: (pvec) -> empty, (pvec 1 2 3) -> 3-element pvec
(de pvec (. elems)
  (pvec-from-list elems))
