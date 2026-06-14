# demos/vi/visual.l -- visual selection (v/V) and operators on selection
# Requires: state.l, buffer.l, cursor.l, ops.l

(de enter-visual (state mode)
  (s-set (s-set state 'mode mode) 'visual (s-get state 'cursor)))

(de visual-region (state)
  (let* ((anchor (s-get state 'visual))
         (cur    (s-get state 'cursor))
         (mode   (s-get state 'mode))
         (ar (car anchor)) (ac (cdr anchor))
         (cr (car cur))    (cc (cdr cur))
         (r1 (min ar cr))  (r2 (max ar cr))
         (c1 (cond ((< ar cr) ac) ((> ar cr) cc) (T (min ac cc))))
         (c2 (cond ((< ar cr) cc) ((> ar cr) ac) (T (max ac cc)))))
    (if (equal mode 'visual-line)
      (make-line-region r1 r2)
      (make-char-region r1 c1 r2 c2))))

(de visual-key (state key count)
  (cond
    ((equal key 'esc)
     (s-set (s-set state 'mode 'normal) 'visual NIL))
    ((member key '("d" "x"))
     (s-set (s-set (delete-region state (visual-region state) T) 'mode 'normal) 'visual NIL))
    ((equal key "y")
     (s-set (s-set (yank-region state (visual-region state)) 'mode 'normal) 'visual NIL))
    ((equal key "c")
     (s-set (s-set (delete-region state (visual-region state) T) 'mode 'insert) 'visual NIL))
    ((equal key ">")
     (s-set (s-set (indent-region state (visual-region state) 1) 'mode 'normal) 'visual NIL))
    ((equal key "<")
     (s-set (s-set (indent-region state (visual-region state) -1) 'mode 'normal) 'visual NIL))
    # motion keys extend selection
    ((motion-key? key)
     (exec-motion state key (max 1 count) NIL))
    (T state)))
