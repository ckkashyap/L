(load "tests/vi_harness.l")
(load "demos/vi/render.l")

# After entering visual mode and selecting a region, render-frame-diff
# should emit ANSI reverse-video escapes around the selected cells.

# Setup: a 3-line buffer, enter visual-char, select cols 1..3 on line 1.
(setq *vi-cols* 20)
(setq *vi-rows* 5)

(let* ((buf (buf-from-string "hello"))
       (st  (make-state buf))
       (st  (s-set st 'mode 'visual-char))
       (st  (s-set st 'visual '(1 . 1)))
       (st  (s-set st 'cursor '(1 . 3)))
       # Capture the rendered rows (not the raw stdout)
       (collected (render--collect buf 1 0 20 4 0 NIL))
       (highlighted (render--apply-highlights collected st 1 0 20 NIL))
       (row1 (car highlighted)))
  # The row content should contain \e[7m and \e[0m around "ell" (cols 1..3).
  (println (list "highlighted row 0:" row1))
  (check T
    (and (>= (length (unpack row1)) (length (unpack "hello")))
         T)
    "highlighted row is at least as long as raw (markup added)")

  # Check the highlight wraps cols 1..3
  # Expect: "h" + ESC[7m + "ell" + ESC[0m + "o"
  (let ((expected (pack (list "h" (ansi-reverse) "ell" (ansi-reset) "o"))))
    (check expected row1 "row content has reverse video around cols 1..3")))

# visual-line: whole row should be wrapped
(let* ((buf (buf-from-string "aa\nbb\ncc"))
       (st  (make-state buf))
       (st  (s-set st 'mode 'visual-line))
       (st  (s-set st 'visual '(1 . 0)))
       (st  (s-set st 'cursor '(2 . 0)))
       (collected (render--collect buf 1 0 20 4 0 NIL))
       (hl (render--apply-highlights collected st 1 0 20 NIL)))
  (let ((expected-row1 (pack (list (ansi-reverse) "aa" (ansi-reset))))
        (expected-row2 (pack (list (ansi-reverse) "bb" (ansi-reset))))
        (expected-row3 "cc"))
    (check expected-row1 (car hl)       "line-mode: row 1 fully highlighted")
    (check expected-row2 (cadr hl)      "line-mode: row 2 fully highlighted")
    (check expected-row3 (caddr hl)     "line-mode: row 3 not in selection")))

# Normal mode: no markup added
(let* ((buf (buf-from-string "hello"))
       (st  (make-state buf))
       (collected (render--collect buf 1 0 20 4 0 NIL))
       (hl (render--apply-highlights collected st 1 0 20 NIL)))
  (check "hello" (car hl) "normal mode: no highlight markup"))

(println "ALL TESTS PASSED")
