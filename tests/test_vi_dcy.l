(load "tests/vi_harness.l")

# D = d$ -- delete from cursor to end of line
(let ((st (vi-run "hello world" '("l" "l" "D"))))
  (check "he" (vi-text st) "D deletes from cursor to EOL")
  (check '(1 . 1) (vi-cursor st) "D leaves cursor at last char"))

# C = c$ -- change from cursor to end of line (enter insert)
(let ((st (vi-run "hello world" '("l" "l" "C"))))
  (check "he" (vi-text st) "C deletes from cursor to EOL")
  (check 'insert (vi-mode st) "C enters insert mode"))

# Y = yy -- yank current line (in vim Y is line-mode like yy, not y$)
(let ((st (vi-run "aaa\nbbb" '("Y" "p"))))
  (check "aaa\naaa\nbbb" (vi-text st) "Y p duplicates the line below"))

# D on last line still deletes to EOL
(let ((st (vi-run "abcd" '("D"))))
  (check "" (vi-text st) "D on single-line buffer empties it"))

(println "ALL TESTS PASSED")
