# demos/vi/mode.l -- mode dispatch coroutine
# Requires: state.l, cursor.l, buffer.l, ops.l

(de enter-insert (state key count)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur))
         (len (buf-line-len buf row))
         (st1 (s-set (undo-save state) 'mode 'insert))
         (st2 (cond
                ((equal key "i") st1)
                ((equal key "I") (s-set st1 'cursor (cons row (cur-first-nonblank buf row))))
                ((equal key "a") (s-set st1 'cursor (cons row (min (1+ col) len))))
                ((equal key "A") (s-set st1 'cursor (cons row len)))
                ((equal key "o")
                 (let* ((nb (buf-ins-line buf (1+ row) ""))
                        (s3 (s-set st1 'buffer nb)))
                   (s-set s3 'cursor (cons (1+ row) 0))))
                ((equal key "O")
                 (let* ((nb (buf-ins-line buf row ""))
                        (s3 (s-set st1 'buffer nb)))
                   (s-set s3 'cursor (cons row 0))))
                (T st1)))
         # If count > 1, remember the anchor + the entry-key so ESC can
         # replay the full command (including any new-line opened by
         # o/O) count-1 more times.  Stored as a 4-list:
         #   (count entry-key anchor-row anchor-col)
         (cnt (max 1 count))
         (anc (s-get st2 'cursor)))
    (if (> cnt 1)
      (s-set st2 'insert-replay (list cnt key (car anc) (cdr anc)))
      st2)))

# Leading whitespace of a line (returns string, possibly empty).
(de insert--leading-ws (line)
  (let ((chars (unpack line)) (acc NIL))
    (while (and chars
                (or (equal (car chars) " ") (equal (car chars) "\t")))
      (setq acc (cons (car chars) acc))
      (setq chars (cdr chars)))
    (if acc (pack (reverse acc)) "")))

# Ctrl-W: delete from cursor back to the start of the previous word.
(de insert--ctrl-w (state)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur))
         (line (buf-line buf row)))
    (if (= col 0) state
      (let ((k (1- col)))
        # Skip trailing whitespace just before cursor
        (while (and (>= k 0)
                    (or (equal (sub line k 1) " ")
                        (equal (sub line k 1) "\t")))
          (setq k (1- k)))
        # Skip the word
        (while (and (>= k 0)
                    (not (equal (sub line k 1) " "))
                    (not (equal (sub line k 1) "\t")))
          (setq k (1- k)))
        (let* ((new-col (1+ k))
               (nb (buf-set-line buf row
                     (pack (list (sub line 0 new-col)
                                 (sub line col))))))
          (s-set (s-set state 'buffer nb)
                 'cursor (cons row new-col)))))))

# Ctrl-U: delete from cursor back to the start of the line.
(de insert--ctrl-u (state)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur))
         (line (buf-line buf row)))
    (if (= col 0) state
      (let ((nb (buf-set-line buf row (sub line col))))
        (s-set (s-set state 'buffer nb) 'cursor (cons row 0))))))

# Insert `text` at the cursor (char-wise, on the current row).
(de insert--paste-text (state text)
  (let* ((cur  (s-get state 'cursor))
         (r    (car cur))
         (c    (cdr cur))
         (b    (s-get state 'buffer))
         (line (buf-line b r))
         (nl   (pack (list (sub line 0 c) text (sub line c))))
         (nb   (buf-set-line b r nl))
         (nc   (+ c (length (unpack text)))))
    (s-set (s-set state 'buffer nb) 'cursor (cons r nc))))

# Open a new line below (key="o") or above (key="O") the current row
# and move the cursor to col 0 of that new line.
(de insert--open-line (state key)
  (let* ((cur (s-get state 'cursor))
         (r   (car cur))
         (b   (s-get state 'buffer))
         (target (if (equal key "o") (1+ r) r))
         (nb  (buf-ins-line b target "")))
    (s-set (s-set state 'buffer nb) 'cursor (cons target 0))))

# Replay-on-ESC: if there's an insert-replay record AND the user's
# edits stayed on the same line, re-execute the open-line action
# (for o/O) and insert the captured text (count-1) more times.
# Multi-line inserts (Enter was pressed) skip the replay -- matches
# vim's behaviour for multi-line counts.
(de insert--maybe-replay (state)
  (let ((rep (s-get state 'insert-replay)))
    (if (null rep)
      state
      (let* ((cnt    (car rep))
             (ekey   (cadr rep))
             (ar     (caddr rep))
             (ac     (car (cdddr rep)))
             (cur    (s-get state 'cursor))
             (cr     (car cur))
             (cc     (cdr cur))
             (st1    (s-set state 'insert-replay NIL))
             (opens? (or (equal ekey "o") (equal ekey "O"))))
        (cond
          ((<= cnt 1) st1)
          # Multi-line typed insert (Enter mid-text) -- skip the replay.
          ((not (= cr ar)) st1)
          (T
           (let* ((line (buf-line (s-get st1 'buffer) cr))
                  (text (if (> cc ac) (sub line ac (- cc ac)) ""))
                  (st   st1)
                  (i    1))
             (while (< i cnt)
               # For o/O each repeat opens a fresh line first.
               (when opens?
                 (setq st (insert--open-line st ekey)))
               (when (> (length (unpack text)) 0)
                 (setq st (insert--paste-text st text)))
               (setq i (1+ i)))
             st)))))))

(de insert-key (state key)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur)))
    (cond
      ((equal key 'esc)
       (let* ((st1 (insert--maybe-replay state))
              (st2 (s-set st1 'mode 'normal))
              (buf2 (s-get st2 'buffer))
              (cur2 (s-get st2 'cursor))
              (r2   (car cur2)) (c2 (cdr cur2))
              (len  (buf-line-len buf2 r2))
              (nc   (max 0 (min (1- (max 1 len)) c2))))
         (s-set st2 'cursor (cons r2 nc))))
      ((equal key 'ret)
       # Split, then autoindent: copy the leading whitespace of the
       # original line onto the new one.
       (let* ((line  (buf-line buf row))
              (indent (insert--leading-ws line))
              (nb1   (buf-split-line buf row col))
              (nb2   (if (= (length (unpack indent)) 0)
                       nb1
                       (buf-set-line nb1 (1+ row)
                         (pack (list indent (buf-line nb1 (1+ row)))))))
              (st2   (s-set state 'buffer nb2)))
         (s-set st2 'cursor (cons (1+ row) (length (unpack indent))))))
      ((equal key 'bs)
       (if (> col 0)
         (let* ((nb  (buf-del-char buf row (1- col)))
                (st2 (s-set state 'buffer nb)))
           (s-set st2 'cursor (cons row (1- col))))
         (if (> row 1)
           (let* ((pl  (buf-line-len buf (1- row)))
                  (nb  (buf-join-lines buf (1- row)))
                  (st2 (s-set state 'buffer nb)))
             (s-set st2 'cursor (cons (1- row) pl)))
           state)))
      ((equal key 'del)
       (let ((len (buf-line-len buf row)))
         (if (< col len) (s-set state 'buffer (buf-del-char buf row col)) state)))
      ((equal key 'ctrl-w) (insert--ctrl-w state))
      ((equal key 'ctrl-u) (insert--ctrl-u state))
      ((string? key)
       (let* ((cp  (car (unpack key)))
              (nb  (buf-ins-char buf row col cp))
              (st2 (s-set state 'buffer nb)))
         (s-set st2 'cursor (cons row (1+ col)))))
      (T state))))

(de exec-prefix (state pfx key count op)
  (cond
    ((equal pfx "g")
     (cond
       ((equal key "g")
        (s-set state 'cursor
          (cursor-clamp (s-get state 'buffer) (cons 1 (cdr (s-get state 'cursor))))))
       ((equal key "e")
        (s-set state 'cursor
          (word-end-forward (s-get state 'buffer) (s-get state 'cursor) count)))
       (T state)))
    ((equal pfx "Z")
     (cond
       ((equal key "Z") (cmd-write-quit state))
       ((equal key "Q") (s-set state 'running NIL))
       (T state)))
    ((equal pfx "r")   (replace-char state key count))
    ((equal pfx "m")   (set-mark state key))
    ((equal pfx "'")   (jump-mark state key 'line))
    ((equal pfx "`")   (jump-mark state key 'exact))
    ((or (equal pfx "f") (equal pfx "F") (equal pfx "t") (equal pfx "T"))
     (find-char state pfx key count op))
    ((or (equal pfx "i") (equal pfx "a"))
     (text-object state pfx key count op))
    ((equal pfx "\"")
     # "{a-z} -- select a named register for the NEXT op (yank/del/put).
     # Stored as 0..25 in state.active-reg; cleared by the op handlers.
     (if (and (string? key)
              (let ((c (char (car (unpack key)))))
                (and (>= c 97) (<= c 122))))
       (s-set state 'active-reg (reg-idx key))
       state))
    ((equal pfx "q")   (start-macro-record state key))
    ((equal pfx "@")   (play-macro state key count))
    (T state)))

# Normal mode dispatch coroutine.
# Yields state after each key; receives next key from caller.
(de make-mode-dispatch (init-state)
  (co (lambda (_)
    (let ((state init-state) (count 0) (op NIL) (pfx NIL))
      (while T
        (let ((key (yield state)))
          (when (and (s-get state 'msg) (not (equal (s-get state 'msg) "")))
            (setq state (s-set state 'msg "")))
          (cond
            (pfx
             (setq state (ensure-cursor-visible
                          (exec-prefix state pfx key (max 1 count) op)))
             (setq pfx NIL) (setq count 0) (setq op NIL))

            ((equal (s-get state 'mode) 'insert)
             (setq state (ensure-cursor-visible (insert-key state key))))

            ((equal (s-get state 'mode) 'command)
             (setq state (cmd-key state key)))

            ((or (equal (s-get state 'mode) 'visual-char)
                 (equal (s-get state 'mode) 'visual-line)
                 (equal (s-get state 'mode) 'visual-block))
             (setq state (visual-key state key count))
             (setq count 0))

            (T  # normal mode
             (cond
               ((and (string? key)
                     (or (equal key "1") (equal key "2") (equal key "3")
                         (equal key "4") (equal key "5") (equal key "6")
                         (equal key "7") (equal key "8") (equal key "9")))
                (setq count (+ (* count 10) (- (char (car (unpack key))) 48))))
               ((and (equal key "0") (> count 0))
                (setq count (* count 10)))
               ((or (equal key "g") (equal key "Z") (equal key "r")
                    (equal key "f") (equal key "F") (equal key "t") (equal key "T")
                    (equal key "'") (equal key "`") (equal key "m")
                    (equal key "q") (equal key "@") (equal key "\""))
                (setq pfx key))
               ((or (equal key "d") (equal key "c") (equal key "y")
                    (equal key "<") (equal key ">"))
                (if (equal op key)
                  (prog (setq state (exec-linewise-op state key (max 1 count)))
                        (setq op NIL) (setq count 0))
                  (setq op key)))
               ((or (motion-key? key)
                    (equal key (vi-caret-key))
                    (equal key 'bs))
                (setq state (exec-motion state (if (equal key 'bs) "h" key) (max 1 count) op))
                (setq op NIL) (setq count 0))
               # When an operator is pending, i / a become text-object prefixes
               # (e.g. diw, ci", da(); otherwise they enter insert mode.
               ((and op (or (equal key "i") (equal key "a")))
                (setq pfx key))
               ((or (equal key "i") (equal key "I") (equal key "a")
                    (equal key "A") (equal key "o") (equal key "O"))
                (setq state (enter-insert state key (max 1 count)))
                (setq op NIL) (setq count 0))
               ((equal key "v")
                (setq state (enter-visual state 'visual-char))
                (setq op NIL) (setq count 0))
               ((equal key "V")
                (setq state (enter-visual state 'visual-line))
                (setq op NIL) (setq count 0))
               ((equal key ":")
                (setq state (s-set (s-set state 'mode 'command) 'cmd-buf ""))
                (setq op NIL) (setq count 0))
               ((equal key "/")
                (setq state (start-search state 'forward))
                (setq op NIL) (setq count 0))
               ((equal key "?")
                (setq state (start-search state 'backward))
                (setq op NIL) (setq count 0))
               ((equal key "u")  (setq state (undo-pop state)))
               ((equal key 'ctrl-r)
                (setq state (redo-pop state)))
               ((equal key "x")
                (setq state (delete-char-at-cursor state (max 1 count)))
                (setq count 0))
               # D / C / Y are sugar for d$ / c$ / yy.
               ((equal key "D")
                (setq state (exec-motion state "$" (max 1 count) "d"))
                (setq op NIL) (setq count 0))
               ((equal key "C")
                (setq state (exec-motion state "$" (max 1 count) "c"))
                (setq op NIL) (setq count 0))
               ((equal key "Y")
                (setq state (exec-linewise-op state "y" (max 1 count)))
                (setq op NIL) (setq count 0))
               # s = delete N chars + enter insert (substitute char)
               ((equal key "s")
                (setq state (delete-char-at-cursor state (max 1 count)))
                (setq state (s-set state 'mode 'insert))
                (setq op NIL) (setq count 0))
               # S = delete current line content + enter insert (substitute line)
               ((equal key "S")
                (let* ((buf (s-get state 'buffer))
                       (row (car (s-get state 'cursor)))
                       (nb  (buf-set-line buf row "")))
                  (setq state (s-set (s-set (s-set (undo-save state)
                                                   'buffer nb)
                                            'cursor (cons row 0))
                                     'mode 'insert)))
                (setq op NIL) (setq count 0))
               ((equal key "p")
                (setq state (put-after  state (max 1 count)))
                (setq count 0))
               ((equal key "P")
                (setq state (put-before state (max 1 count)))
                (setq count 0))
               ((equal key "J")
                (setq state (join-lines-cmd state (max 1 count)))
                (setq count 0))
               ((equal key "n")
                (setq state (search-next state (max 1 count)))
                (setq count 0))
               ((equal key "N")
                (setq state (search-prev state (max 1 count)))
                (setq count 0))
               ((equal key "~")
                (setq state (toggle-case state (max 1 count)))
                (setq count 0))
               ((equal key ";")
                (setq state (find-repeat state (max 1 count) NIL))
                (setq count 0))
               ((equal key ",")
                (setq state (find-repeat state (max 1 count) T))
                (setq count 0))
               ((equal key "*")
                (setq state (search-word-fwd state (max 1 count)))
                (setq count 0))
               ((equal key "#")
                (setq state (search-word-bwd state (max 1 count)))
                (setq count 0))
               ((equal key ".")  (setq state (repeat-last state)))
               (T NIL))
             # Recompute scroll so any operation that moved the cursor
             # (insert open-line, undo/redo, search, : line jump, etc.)
             # doesn't leave the cursor outside the visible viewport.
             (setq state (ensure-cursor-visible state))))))))))

# ── Stubs (replaced when later tasks are loaded) ──────────────────────────────

(de replace-char (state key count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (row  (car cur)) (col (cdr cur))
         (line (buf-line buf row))
         (len  (length (unpack line)))
         (n    (max 1 count)))
    (if (and (string? key) (< col len) (<= (+ col n) len))
      # Replace n chars starting at col with n copies of key.
      (let* ((rep (pack (let ((acc NIL) (i 0))
                          (while (< i n) (setq acc (cons key acc)) (setq i (1+ i)))
                          acc)))
             (nl  (pack (list (sub line 0 col) rep (sub line (+ col n))))))
        (s-set (s-set state 'buffer (buf-set-line buf row nl))
               'cursor (cons row (+ col (1- n)))))
      state)))

# Local marks: m{a-z} stores the current cursor at marks[idx], where
# idx is the lowercase letter index 0..25.  '{a-z} jumps to the start
# of the marked line; `{a-z} jumps to the exact (row . col).
(de mark--valid-key? (key)
  (and (string? key)
       (let ((c (char (car (unpack key)))))
         (and (>= c 97) (<= c 122)))))

(de set-mark (state key)
  (if (not (mark--valid-key? key))
    state
    (let* ((idx    (reg-idx key))
           (marks  (s-get state 'marks))
           (marks2 (pvec-put marks idx (s-get state 'cursor))))
      (s-set state 'marks marks2))))

(de jump-mark (state key kind)
  (if (not (mark--valid-key? key))
    state
    (let* ((idx (reg-idx key))
           (m   (pvec-get (s-get state 'marks) idx)))
      (if (null m)
        state
        (let ((buf (s-get state 'buffer)))
          (s-set state 'cursor
            (cursor-clamp buf
              (if (equal kind 'line)
                (cons (car m) 0)
                m))))))))

# Forward search for `ch` on `line` starting AFTER col, returning the
# column of the n-th occurrence (1-based count), or NIL if not found.
(de find--forward (line col len ch n)
  (let ((i (1+ col)) (left n) (result NIL))
    (while (and (< i len) (null result))
      (when (equal (sub line i 1) ch)
        (setq left (1- left))
        (when (= left 0) (setq result i)))
      (setq i (1+ i)))
    result))

# Backward search for `ch` on `line` starting BEFORE col.
(de find--backward (line col ch n)
  (let ((i (1- col)) (left n) (result NIL))
    (while (and (>= i 0) (null result))
      (when (equal (sub line i 1) ch)
        (setq left (1- left))
        (when (= left 0) (setq result i)))
      (setq i (1- i)))
    result))

# Inline find: f/F = land on the char; t/T = land beside it.
# Records (pfx . ch) in 'last-find so ; / , can repeat / reverse.
# If op is non-NIL (d/c/y/</>), operate on the [cursor..target] range
# instead of just moving the cursor.
(de find-char (state pfx key count op)
  (if (not (string? key))
    state
    (let* ((buf  (s-get state 'buffer))
           (cur  (s-get state 'cursor))
           (row  (car cur)) (col (cdr cur))
           (line (buf-line buf row))
           (len  (length (unpack line)))
           (n    (max 1 count))
           (forward? (or (equal pfx "f") (equal pfx "t")))
           (till?    (or (equal pfx "t") (equal pfx "T")))
           (hit  (if forward?
                   (find--forward  line col len key n)
                   (find--backward line col     key n))))
      (if (null hit)
        state
        (let ((nc (cond
                    ((and forward? till?)       (max 0 (1- hit)))
                    ((and (not forward?) till?) (min (1- len) (1+ hit)))
                    (T hit)))
              (st1 (s-set state 'last-find (cons pfx key))))
          (if op
            # Operator-pending: apply op to [cursor..target] inclusive on
            # the f/F (land-on) side; t/T excludes the target char itself.
            (let* ((from (if forward? col nc))
                   (to   (if forward? nc col))
                   (reg  (make-char-region row from row to)))
              (apply-op st1 op reg))
            (s-set st1 'cursor (cons row nc))))))))

# ; / , -- repeat / reverse the last f/F/t/T.
(de find-repeat (state count reverse?)
  (let ((lf (s-get state 'last-find)))
    (if (null lf) state
      (let* ((pfx (car lf))
             (ch  (cdr lf))
             (rpx (if reverse?
                    (cond ((equal pfx "f") "F") ((equal pfx "F") "f")
                          ((equal pfx "t") "T") ((equal pfx "T") "t")
                          (T pfx))
                    pfx))
             (st2 (find-char state rpx ch count NIL)))
        # find-char overwrites last-find; restore the original so
        # subsequent ; / , still repeat the user's original choice.
        (s-set st2 'last-find lf)))))
