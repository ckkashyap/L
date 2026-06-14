(load "tests/vi_harness.l")
(load "demos/vi/render.l")

# view-gutter-width returns 0 when *vi-number* is off
(setq *vi-number* NIL)
(let ((buf (buf-from-lines (list "a" "b" "c"))))
  (check 0 (view-gutter-width buf 1 3) "gutter width 0 when nonumber"))

# When on, gutter is width = digits + 1 (trailing space)
(setq *vi-number* T)
(let ((buf (buf-from-lines (list "a" "b" "c"))))
  (check 2 (view-gutter-width buf 1 3) "gutter width = 2 for single-digit visible"))

# 12 visible lines need 3 chars (2 digits + space)
(let* ((lines (let ((acc NIL) (i 0))
                (while (< i 15) (setq acc (cons (str i) acc)) (setq i (1+ i)))
                acc))
       (buf (buf-from-lines lines)))
  (check 3 (view-gutter-width buf 1 12) "gutter width = 3 for two-digit visible"))

# view-add-gutter prepends the right-aligned numbers
(setq *vi-number* T)
(let ((buf (buf-from-lines (list "foo" "bar" "baz"))))
  (let ((rows (render--collect buf 1 0 80 3 0 NIL)))
    (let ((with-gutter (view-add-gutter rows NIL 1 2)))
      (check "1 foo" (car with-gutter)   "row 1: '1 foo'")
      (check "2 bar" (cadr with-gutter)  "row 2: '2 bar'")
      (check "3 baz" (caddr with-gutter) "row 3: '3 baz'"))))

# Modified marker [+] shows when state is dirty
(let* ((buf (buf-from-string "foo"))
       (st  (make-state buf))
       (st  (s-set st 'filename "test.txt"))
       (st  (s-set st 'dirty T)))
  (check "test.txt [+]" (car (view-status-left st)) "[+] modified marker"))

# Clean buffer has no marker
(let* ((buf (buf-from-string "foo"))
       (st  (make-state buf))
       (st  (s-set st 'filename "test.txt"))
       (st  (s-set st 'dirty NIL)))
  (check "test.txt" (car (view-status-left st)) "no marker when clean"))

(setq *vi-number* NIL)  # reset for other tests
(println "ALL TESTS PASSED")
