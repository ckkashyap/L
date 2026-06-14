(load "tests/vi_harness.l")

# ── Mode entry ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '("i"))))
  (check 'insert (vi-mode st) "i enters insert mode"))

(let ((st (vi-run "hello" '("i" esc))))
  (check 'normal (vi-mode st) "ESC returns to normal"))

# ── i -- insert before cursor ──────────────────────────────────────────────

(let ((st (vi-run "hello" '("i" "X" esc))))
  (check "Xhello" (vi-text st) "i X ESC inserts before cursor"))

(let ((st (vi-run "hello" '("l" "l" "i" "X" esc))))
  (check "heXllo" (vi-text st) "ll i X inserts at col 2"))

# ── a -- append after cursor ───────────────────────────────────────────────

(let ((st (vi-run "hello" '("a" "X" esc))))
  (check "hXello" (vi-text st) "a X ESC appends after col 0"))

(let ((st (vi-run "hi" '("l" "a" "!" esc))))
  (check "hi!" (vi-text st) "l a ! appends after last char"))

# ── A -- append at end of line ─────────────────────────────────────────────

(let ((st (vi-run "hello" '("A" "!" esc))))
  (check "hello!" (vi-text st) "A ! ESC appends at end of line"))

# ── o -- open line below ───────────────────────────────────────────────────

(let ((st (vi-run "hello\nworld" '("o" "n" "e" "w" esc))))
  (check "hello\nnew\nworld" (vi-text st) "o opens line below, types text"))

(let ((st (vi-run "hello\nworld" '("o" "x" esc))))
  (check 2 (car (vi-cursor st)) "o cursor lands on new line 2"))

# ── O -- open line above ───────────────────────────────────────────────────

(let ((st (vi-run "hello\nworld" '("j" "O" "n" "e" "w" esc))))
  (check "hello\nnew\nworld" (vi-text st) "O opens line above, types text"))

# ── Typing multiple chars ─────────────────────────────────────────────────

(let ((st (vi-run "" '("i" "h" "i" esc))))
  (check "hi" (vi-text st) "typing two chars in insert mode"))

# ── Backspace in insert mode ──────────────────────────────────────────────

(let ((st (vi-run "hello" '("l" "l" "i" bs esc))))
  (check "hllo" (vi-text st) "BS in insert deletes previous char"))

(let ((st (vi-run "hello\nworld" '("j" "i" bs esc))))
  (check "helloworld" (vi-text st) "BS at start of line joins lines"))

(let ((st (vi-run "hello" '("i" bs esc))))
  (check "hello" (vi-text st) "BS at first position is no-op"))

# ── Enter in insert mode ──────────────────────────────────────────────────

(let ((st (vi-run "hello" '("l" "l" "i" ret esc))))
  (check "he\nllo" (vi-text st) "RET in insert splits line"))

(let ((st (vi-run "hello" '("l" "l" "i" ret esc))))
  (check '(2 . 0) (vi-cursor st) "RET cursor lands at start of new line"))

(println "ALL TESTS PASSED")
