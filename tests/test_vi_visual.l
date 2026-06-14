(load "tests/vi_harness.l")

# ── Mode entry ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '("v"))))
  (check 'visual-char (vi-mode st) "v enters visual-char"))

(let ((st (vi-run "hello" '("V"))))
  (check 'visual-line (vi-mode st) "V enters visual-line"))

(let ((st (vi-run "hello" '("v" esc))))
  (check 'normal (vi-mode st) "ESC exits visual to normal"))

(let ((st (vi-run "hello" '("V" esc))))
  (check 'normal (vi-mode st) "ESC exits visual-line to normal"))

# ── char visual: v + motion + d ───────────────────────────────────────────

# v l d -- select two chars (cols 0-1) and delete
(let ((st (vi-run "hello" '("v" "l" "d"))))
  (check "llo" (vi-text st) "v l d deletes 2-char selection"))

# v $ d -- select to end of line and delete
(let ((st (vi-run "hello world" '("v" "$" "d"))))
  (check "" (vi-text st) "v $ d deletes whole line content"))

# v + y -- yank char selection, returns to normal
(let ((st (vi-run "hello" '("v" "l" "l" "y"))))
  (check 'normal (vi-mode st) "v y returns to normal"))

# v y p -- yank and paste
(let ((st (vi-run "hello world" '("v" "l" "l" "l" "l" "y" "$" "p"))))
  (check 'normal (vi-mode st) "v y p works without crash"))

# ── line visual: V + d ────────────────────────────────────────────────────

# V d -- delete current line (line visual)
(let ((st (vi-run "hello\nworld" '("V" "d"))))
  (check "world" (vi-text st) "V d deletes line"))

# V j d -- extend selection down one line, delete both
(let ((st (vi-run "aaa\nbbb\nccc" '("V" "j" "d"))))
  (check "ccc" (vi-text st) "V j d deletes two lines"))

# V y + p -- yank whole line and paste below
(let ((st (vi-run "aaa\nbbb" '("j" "V" "y" "k" "p"))))
  (check 'normal (vi-mode st) "V y p returns to normal"))

# ── visual-char: v + c -- change (delete + enter insert) ──────────────────

(let ((st (vi-run "hello" '("v" "l" "c"))))
  (check 'insert (vi-mode st) "v c enters insert mode"))

(let ((st (vi-run "hello" '("v" "l" "c" "X" esc))))
  (check "Xllo" (vi-text st) "v l c X replaces selection"))

(println "ALL TESTS PASSED")
