(load "tests/vi_harness.l")

# Ctrl-W in insert mode deletes the previous word
(let ((st (vi-run "hello world" '("A" ctrl-w))))
  (check "hello " (vi-text st) "Ctrl-W deletes 'world'"))

# Ctrl-W at start of line is a no-op (no word to delete)
(let ((st (vi-run "abc" '("i" ctrl-w))))
  (check "abc" (vi-text st) "Ctrl-W at BOL is no-op"))

# Ctrl-U deletes from cursor to start of line
(let ((st (vi-run "abc def" '("A" ctrl-u))))
  (check "" (vi-text st) "Ctrl-U deletes whole line content"))

(let ((st (vi-run "abc def" '("f" "d" "i" ctrl-u))))
  # Cursor at col 4 (the 'd' of def), Ctrl-U deletes cols 0..3
  (check "def" (vi-text st) "Ctrl-U deletes from cursor back to BOL"))

# Autoindent: Enter in insert mode copies leading whitespace of current line
(let ((st (vi-run "    hello" '("A" ret "x"))))
  (check "    hello\n    x" (vi-text st) "Enter autoindents"))

# Empty line: Enter just creates a new empty line
(let ((st (vi-run "" '("i" ret "y"))))
  (check "\ny" (vi-text st) "Enter on empty line: no indent"))

(println "ALL TESTS PASSED")
