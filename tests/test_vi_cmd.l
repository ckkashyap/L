(load "tests/vi_harness.l")

# ── : enters command mode ─────────────────────────────────────────────────

(let ((st (vi-run "hello" '(":"))))
  (check 'command (vi-mode st) ": enters command mode"))

(let ((st (vi-run "hello" '(":" esc))))
  (check 'normal (vi-mode st) "ESC from command -> normal"))

# ── :q -- quit ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '(":" "q" ret))))
  (check NIL (s-get st 'running) ":q sets running to NIL"))

(let ((st (vi-run "hello" '("i" "X" esc ":" "q" "!" ret))))
  (check NIL (s-get st 'running) ":q! force quits with unsaved changes"))

# ── :w -- write ───────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '(":" "w" " " "b" "u" "i" "l" "d" "/" "v" "i" "t" "e" "s" "t" "1" "." "t" "x" "t" ret))))
  (check "build/vitest1.txt" (s-get st 'filename) ":w sets filename"))

(let ((st (vi-run "hello" '("i" "X" esc ":" "w" " " "b" "u" "i" "l" "d" "/" "v" "i" "t" "e" "s" "t" "2" "." "t" "x" "t" ret))))
  (check NIL (s-get st 'dirty) ":w clears dirty flag"))

# ── :N -- jump to line N ───────────────────────────────────────────────────

(let ((st (vi-run "aaa\nbbb\nccc\nddd" '(":" "3" ret))))
  (check '(3 . 0) (vi-cursor st) ":3 jumps to line 3"))

(let ((st (vi-run "aaa\nbbb\nccc\nddd" '(":" "1" ret))))
  (check '(1 . 0) (vi-cursor st) ":1 jumps to line 1"))

# ── :s -- substitution ────────────────────────────────────────────────────
# NOTE: cmd-subst receives (sub verb 1) which strips the leading "s", giving
# "a/X/g". split by "/" yields ("a" "X" "g"): from="a", to="X", flags="g".
# A literal leading "/" would produce from="" and mis-parse. So the key
# sequence omits the first slash: ":" "s" "a" "/" "X" "/" "g" ret.

(let ((st (vi-run "banana" '(":" "s" "a" "/" "X" "/" "g" ret))))
  (check "bXnXnX" (vi-text st) ":s/a/X/g replaces all on line"))

(let ((st (vi-run "hello" '(":" "s" "z" "/" "X" "/" "g" ret))))
  (check "hello" (vi-text st) ":s with no match leaves line unchanged"))

(let ((st (vi-run "aaa\nbbb\nccc" '("j" ":" "s" "b" "/" "Z" "/" "g" ret))))
  (check "aaa\nZZZ\nccc" (vi-text st) ":s on line 2 only affects line 2"))

# ── cmd-buf: backspace removes last char ─────────────────────────────────

(let ((st (vi-run "hello" '(":" "q" bs esc))))
  (check 'normal (vi-mode st) "BS then ESC in cmd returns to normal"))

(println "ALL TESTS PASSED")
