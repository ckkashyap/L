(load "tests/vi_harness.l")

# * jumps to the next occurrence of the word under the cursor.
(let ((st (vi-run "foo bar foo bar" '("*"))))
  (check '(1 . 8) (vi-cursor st) "* finds next 'foo'"))

# # jumps to the previous occurrence.
(let ((st (vi-run "foo bar foo bar" '("w" "w" "#"))))
  (check '(1 . 0) (vi-cursor st) "# finds previous 'foo'"))

# After *, n / N work as repeat / reverse.
# Cursor: x abc abc abc
#         0 2   6  10
# w -> col 2 (first abc); * -> col 6 (next abc); n -> col 10 (next abc)
(let ((st (vi-run "x abc abc abc" '("w" "*" "n"))))
  (check '(1 . 10) (vi-cursor st) "* then n moves forward through matches"))

# Cursor on whitespace: * is a no-op.
(let ((st (vi-run "  hello" '("*"))))
  (check '(1 . 0) (vi-cursor st) "* on whitespace is a no-op"))

# Multi-line: * wraps to next line.
(let ((st (vi-run "foo\nbar\nfoo" '("*"))))
  (check '(3 . 0) (vi-cursor st) "* across lines"))

(println "ALL TESTS PASSED")
