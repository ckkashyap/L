(load "tests/vi_harness.l")
(load "demos/vi/render.l")   # for view-visual-range

# view-visual-range tests: given a state in visual mode, ask what columns
# of a given buffer row are highlighted.

# Setup helper: build state in visual-char mode with given anchor + cursor.
(de mk-vstate (text mode anchor cursor)
  (let* ((buf (buf-from-string text))
         (st  (make-state buf))
         (st  (s-set st 'mode mode))
         (st  (s-set st 'visual anchor))
         (st  (s-set st 'cursor cursor)))
    st))

# Single-line selection: anchor col 2 -> cursor col 5
(let ((st (mk-vstate "hello world" 'visual-char '(1 . 2) '(1 . 5))))
  (check '(2 . 5) (view-visual-range st 1) "char: same row, asc range"))

# Single-line, cursor BEFORE anchor: still normalised
(let ((st (mk-vstate "hello world" 'visual-char '(1 . 5) '(1 . 2))))
  (check '(2 . 5) (view-visual-range st 1) "char: same row, reversed -> normalised"))

# Two-row char selection: first row from c1 to EOL, last row from 0 to c2
(let ((st (mk-vstate "aaaaa\nbbbbb\nccccc" 'visual-char '(1 . 2) '(3 . 1))))
  (check '(2 . -1) (view-visual-range st 1) "char: first row -> col 2 to EOL")
  (check '(0 . -1) (view-visual-range st 2) "char: middle row -> whole line")
  (check '(0 . 1)  (view-visual-range st 3) "char: last row -> 0..1"))

# Line-mode selection: rows fully covered
(let ((st (mk-vstate "aa\nbb\ncc\ndd" 'visual-line '(2 . 0) '(3 . 0))))
  (check '(0 . -1) (view-visual-range st 2) "line: row 2 fully")
  (check '(0 . -1) (view-visual-range st 3) "line: row 3 fully")
  (check NIL       (view-visual-range st 1) "line: row 1 unselected")
  (check NIL       (view-visual-range st 4) "line: row 4 unselected"))

# Not in visual mode -> always NIL
(let ((st (mk-vstate "hello" 'normal '(1 . 0) '(1 . 3))))
  (check NIL (view-visual-range st 1) "normal mode -> no highlight"))

(println "ALL TESTS PASSED")
