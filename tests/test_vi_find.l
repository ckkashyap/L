(load "tests/vi_harness.l")

# f{ch} jumps to the next occurrence of {ch} on the current line.
(let ((st (vi-run "hello world" '("f" "w"))))
  (check '(1 . 6) (vi-cursor st) "f w jumps to 'w' in 'world'"))

# F{ch} jumps backward to previous occurrence on the line.
(let ((st (vi-run "hello world" '("$" "F" "o"))))
  (check '(1 . 7) (vi-cursor st) "F o backward to 'o' in 'world'"))

# t{ch} jumps to the char BEFORE the next occurrence ('till).
(let ((st (vi-run "hello world" '("t" "w"))))
  (check '(1 . 5) (vi-cursor st) "t w stops at space before 'w'"))

# T{ch} jumps to the char AFTER the previous occurrence.
(let ((st (vi-run "hello world" '("$" "T" "o"))))
  (check '(1 . 8) (vi-cursor st) "T o stops at 'r' after the 'o' in 'world'"))

# ; repeats the last find in the same direction.
(let ((st (vi-run "a.b.c.d" '("f" "." ";"))))
  (check '(1 . 3) (vi-cursor st) "; repeats f."))

# , reverses the last find.
(let ((st (vi-run "a.b.c.d" '("f" "." ";" ","))))
  (check '(1 . 1) (vi-cursor st) ", goes backward to prior match"))

# f for nonexistent char leaves cursor put.
(let ((st (vi-run "hello" '("f" "Z"))))
  (check '(1 . 0) (vi-cursor st) "f Z (no match) is a no-op"))

# df{ch} -- delete up to and including the char.
(let ((st (vi-run "hello world" '("d" "f" "o"))))
  (check " world" (vi-text st) "df o deletes 'hello'"))

# dt{ch} -- delete up to but not including the char.
(let ((st (vi-run "hello world" '("d" "t" "o"))))
  (check "o world" (vi-text st) "dt o stops before 'o'"))

(println "ALL TESTS PASSED")
