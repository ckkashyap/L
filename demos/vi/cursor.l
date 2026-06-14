# demos/vi/cursor.l -- cursor clamping, movement, viewport
# Requires: buffer.l, state.l

(de cursor-clamp (buf cur)
  (let* ((row (max 1 (min (buf-nlines buf) (car cur))))
         (len (buf-line-len buf row))
         (col (max 0 (min (if (> len 0) (1- len) 0) (cdr cur)))))
    (cons row col)))

(de cursor-clamp-insert (buf cur)
  (let* ((row (max 1 (min (buf-nlines buf) (car cur))))
         (len (buf-line-len buf row))
         (col (max 0 (min len (cdr cur)))))
    (cons row col)))

(de cur-move-h (buf cur delta)
  (cursor-clamp buf (cons (car cur) (+ (cdr cur) delta))))

(de cur-move-v (buf cur delta)
  (cursor-clamp buf (cons (+ (car cur) delta) (cdr cur))))

(de cur-first-nonblank (buf row)
  (let ((chars (unpack (buf-line buf row))) (col 0))
    (while (and chars (or (equal (car chars) " ") (equal (car chars) "\t")))
      (setq col (1+ col)) (setq chars (cdr chars)))
    col))

(de viewport-ensure (buf cur scroll cols rows)
  # rows = text rows (screen height - 1 for status bar)
  (if *vi-wrap*
    (viewport-ensure-wrap buf cur scroll cols rows)
    (let* ((row   (car cur))   (col   (cdr cur))
           (top   (car scroll)) (left  (cdr scroll))
           (bot   (+ top rows -1))
           (top2  (cond ((< row top)      row)
                        ((> row bot)      (- row rows -1))
                        (T                top)))
           (left2 (cond ((< col left)             (max 0 (- col 4)))
                        ((>= col (+ left cols))   (- col cols -5))
                        (T                         left))))
      (cons top2 left2))))

# How many display rows does buffer line `lnum` consume in wrap mode?
# An empty line still uses 1 row (the cursor sits at col 0).
(de wrap--lines-of (buf lnum cols)
  (let ((len (buf-line-len buf lnum)))
    (if (= len 0) 1 (/ (+ len (1- cols)) cols))))

# Display-row index of the cursor (0-based) given `top` is the first
# visible buffer line.  Returns total display rows from line `top` up
# to buffer (crow . ccol).
(de wrap--display-row-of (buf top crow ccol cols)
  (let ((acc 0) (l top))
    (while (< l crow)
      (setq acc (+ acc (wrap--lines-of buf l cols)))
      (setq l (1+ l)))
    (+ acc (/ ccol cols))))

# Wrap-aware viewport scroll.  In wrap mode, top is still a buffer line
# number, but a single buffer line may consume multiple display rows.
# Bump top forward (one buffer line at a time) until the cursor's
# display row fits in the visible window; if cursor is above top,
# pull top up to the cursor row.  left is always 0 in wrap mode.
(de viewport-ensure-wrap (buf cur scroll cols rows)
  (let* ((row (car cur)) (col (cdr cur))
         (top (max 1 (min row (car scroll)))))
    (while (and (< top row)
                (>= (wrap--display-row-of buf top row col cols) rows))
      (setq top (1+ top)))
    (cons top 0)))
