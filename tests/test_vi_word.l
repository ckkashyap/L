(load "tests/vi_harness.l")

# w/b/e treat punctuation as a separate word; W/B/E treat anything
# non-whitespace as part of the same WORD.  E.g.:
#   "foo.bar baz"
#       ^ col 0
#   w  -> col 3 (the '.')
#   W  -> col 8 (start of baz)

(let ((st (vi-run "foo.bar baz" '("W"))))
  (check '(1 . 8) (vi-cursor st) "W skips over punctuation to next WORD"))

(let ((st (vi-run "foo.bar baz" '("W" "B"))))
  (check '(1 . 0) (vi-cursor st) "B from baz returns to foo"))

(let ((st (vi-run "foo.bar baz" '("E"))))
  (check '(1 . 6) (vi-cursor st) "E goes to end of foo.bar"))

# Multi-line WORD motions
(let ((st (vi-run "one two\nthree four" '("W" "W"))))
  (check '(2 . 0) (vi-cursor st) "W crosses newline to next line"))

# WORD with counts
(let ((st (vi-run "a.b c.d e.f" '("2" "W"))))
  (check '(1 . 8) (vi-cursor st) "2W -> 3rd WORD start"))

(println "ALL TESTS PASSED")
