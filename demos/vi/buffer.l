# demos/vi/buffer.l -- weight-balanced tree line buffer
# Requires lib/ds.l and lib/rope.l (wbt-* functions)
#
# The buffer is a wbt (weight-balanced tree) of strings,
# one string per line.  All operations that were O(n) before
# (buf-ins-line, buf-del-line) are now O(log n).
#
# Row numbers throughout this file are 1-based (vi convention).
# WBT indices are 0-based; we subtract 1 at every boundary.

(load "lib/rope.l")

# ---- Buffer construction ----------------------------------

(de buf-from-lines (lst)
  (wbt-from-list lst))

(de buf-empty ()
  (wbt-from-list '("")))

# ---- Core accessors ---------------------------------------

(de buf-nlines (buf)
  (wbt-count buf))

# row is 1-based
(de buf-line (buf row)
  (wbt-get buf (1- row)))

# ---- Line-level operations (all O(log n)) -----------------

# Return new buf with row replaced by str
(de buf-set-line (buf row str)
  (wbt-set buf (1- row) str))

# Insert str BEFORE row (row and everything after shift down by 1)
(de buf-ins-line (buf row str)
  (wbt-insert buf (1- row) str))

# Delete line at row; if buf had only 1 line, replace with empty line
(de buf-del-line (buf row)
  (if (= (buf-nlines buf) 1)
    (wbt-from-list '(""))
    (wbt-delete buf (1- row))))

# ---- Character-level operations (O(log n) via buf-set-line)

(de buf-line-len (buf row)
  (length (unpack (buf-line buf row))))

(de buf-ins-char (buf row col cp)
  (let ((line (buf-line buf row)))
    (buf-set-line buf row
      (pack (list (sub line 0 col) cp (sub line col))))))

(de buf-del-char (buf row col)
  (let ((line (buf-line buf row)))
    (buf-set-line buf row
      (pack (list (sub line 0 col) (sub line (1+ col)))))))

# ---- Structural operations --------------------------------

# Split line at col: line becomes sub(0,col), new line inserted after
(de buf-split-line (buf row col)
  (let ((line (buf-line buf row)))
    (buf-ins-line
      (buf-set-line buf row (sub line 0 col))
      (1+ row)
      (sub line col))))

# Join line row with line row+1 (no-op if row is last)
(de buf-join-lines (buf row)
  (if (>= row (buf-nlines buf))
    buf
    (let ((a (buf-line buf row))
          (b (buf-line buf (1+ row))))
      (buf-del-line (buf-set-line buf row (pack (list a b)))
                    (1+ row)))))

# ---- Whole-buffer conversion ------------------------------

# Internal: join list of strings with "\n" between them
(de buf--join-lines-str (lst)
  (if (null (cdr lst))
    (car lst)
    (pack (list (car lst) "\n" (buf--join-lines-str (cdr lst))))))

(de buf-to-string (buf)
  (let ((lines (wbt-to-list buf)))
    (if lines (buf--join-lines-str lines) "")))

# Internal: split string by newlines into list, accumulating in reverse
(de buf--split-newlines (chars cur lines)
  (if (null chars)
    (cons cur lines)
    (if (equal (car chars) "\n")
      (buf--split-newlines (cdr chars) "" (cons cur lines))
      (buf--split-newlines (cdr chars) (pack (list cur (car chars))) lines))))

(de buf-from-string (str)
  (buf-from-lines
    (reverse (buf--split-newlines (unpack str) "" NIL))))
