# demos/vi/state.l -- editor state as a fixed-index pvec
#
# The state is a 25-element pvec (all in the tail, so O(1) access).
# s-set uses pvec-put which path-copies only the prefix up to the
# changed index, sharing the suffix -- 2-4x fewer allocations than
# the old full-alist-copy approach.
#
# Key index table (must match make-state order exactly):
#   0  buffer       8  search-dir   16 macro-rec    24 active-reg
#   1  cursor       9  search-mode  17 macro-inject 25 insert-replay
#   2  desired-col  10 undo         18 last-find
#   3  scroll       11 redo         19 last-cmd
#   4  mode         12 unnamed-reg  20 filename
#   5  visual       13 registers    21 dirty
#   6  cmd-buf      14 marks        22 msg
#   7  search       15 macros       23 running

(de state--key-idx (key)
  (cond
    ((eq key 'buffer)       0)
    ((eq key 'cursor)       1)
    ((eq key 'desired-col)  2)
    ((eq key 'scroll)       3)
    ((eq key 'mode)         4)
    ((eq key 'visual)       5)
    ((eq key 'cmd-buf)      6)
    ((eq key 'search)       7)
    ((eq key 'search-dir)   8)
    ((eq key 'search-mode)  9)
    ((eq key 'undo)        10)
    ((eq key 'redo)        11)
    ((eq key 'unnamed-reg) 12)
    ((eq key 'registers)   13)
    ((eq key 'marks)       14)
    ((eq key 'macros)      15)
    ((eq key 'macro-rec)   16)
    ((eq key 'macro-inject) 17)
    ((eq key 'last-find)   18)
    ((eq key 'last-cmd)    19)
    ((eq key 'filename)    20)
    ((eq key 'dirty)       21)
    ((eq key 'msg)         22)
    ((eq key 'running)     23)
    ((eq key 'active-reg)    24)
    ((eq key 'insert-replay) 25)))

(de s-get (state key)
  (pvec-get state (state--key-idx key)))

(de s-set (state key val)
  (pvec-put state (state--key-idx key) val))

# ---- Helpers for 26-slot pvecs (registers/marks/macros) --

(de state--p26s (v i) (if (= i 26) v (state--p26s (pvec-push v "") (1+ i))))
(de state--pvec26str () (state--p26s (pvec) 0))

(de state--p26n (v i) (if (= i 26) v (state--p26n (pvec-push v NIL) (1+ i))))
(de state--pvec26nil () (state--p26n (pvec) 0))

# ---- make-state: build the 24-element state pvec ----------
# Order MUST match the index table above.

(de make-state (buf)
  (pvec
    buf               # 0  buffer
    '(1 . 0)          # 1  cursor
    0                 # 2  desired-col
    '(1 . 0)          # 3  scroll
    'normal           # 4  mode
    NIL               # 5  visual
    ""                # 6  cmd-buf
    ""                # 7  search
    'forward          # 8  search-dir
    NIL               # 9  search-mode
    NIL               # 10 undo
    NIL               # 11 redo
    ""                # 12 unnamed-reg
    (state--pvec26str) # 13 registers
    (state--pvec26nil) # 14 marks
    (state--pvec26nil) # 15 macros
    NIL               # 16 macro-rec
    NIL               # 17 macro-inject
    NIL               # 18 last-find
    NIL               # 19 last-cmd
    NIL               # 20 filename
    NIL               # 21 dirty
    ""                # 22 msg
    T                 # 23 running
    NIL               # 24 active-reg (idx 0..25 = a-z, NIL = unnamed)
    NIL))             # 25 insert-replay (count . (row . col)) or NIL

# ---- Undo / redo helpers ----------------------------------
# These use s-get/s-set which now go through the indexed pvec.

(de undo-save (state)
  (s-set state 'undo
    (cons (cons (s-get state 'buffer) (s-get state 'cursor))
          (s-get state 'undo))))

(de undo-pop (state)
  (let ((stack (s-get state 'undo)))
    (if (null stack)
      (s-set state 'msg "Already at oldest change")
      (let* ((snap (car stack))
             (s2 (s-set state  'undo   (cdr stack)))
             (s3 (s-set s2     'buffer (car snap)))
             (s4 (s-set s3     'cursor (cdr snap))))
        (s-set s4 'redo
          (cons (cons (s-get state 'buffer) (s-get state 'cursor))
                (s-get state 'redo)))))))

(de redo-pop (state)
  (let ((stack (s-get state 'redo)))
    (if (null stack)
      (s-set state 'msg "Already at newest change")
      (let* ((snap (car stack))
             (s2 (s-set state  'redo   (cdr stack)))
             (s3 (s-set s2     'buffer (car snap)))
             (s4 (s-set s3     'cursor (cdr snap))))
        (s-set s4 'undo
          (cons (cons (s-get state 'buffer) (s-get state 'cursor))
                (s-get state 'undo)))))))

(de reg-idx (ch)
  (- (char (car (unpack ch))) 97))
