(load "tests/vi_harness.l")

# ── Motions ───────────────────────────────────────────────────────────────

# l moves right
(let ((st (vi-run "hello" '("l" "l"))))
  (check '(1 . 2) (vi-cursor st) "l l -> col 2"))

# h moves left
(let ((st (vi-run "hello" '("l" "l" "h"))))
  (check '(1 . 1) (vi-cursor st) "l l h -> col 1"))

# h at col 0 is a no-op
(let ((st (vi-run "hello" '("h"))))
  (check '(1 . 0) (vi-cursor st) "h at col 0 stays"))

# j moves down
(let ((st (vi-run "aaa\nbbb\nccc" '("j"))))
  (check '(2 . 0) (vi-cursor st) "j -> row 2"))

# k moves up
(let ((st (vi-run "aaa\nbbb\nccc" '("j" "k"))))
  (check '(1 . 0) (vi-cursor st) "j k -> row 1"))

# k at row 1 is a no-op
(let ((st (vi-run "aaa\nbbb" '("k"))))
  (check '(1 . 0) (vi-cursor st) "k at row 1 stays"))

# count prefix: 3j
(let ((st (vi-run "a\nb\nc\nd\ne" '("3" "j"))))
  (check '(4 . 0) (vi-cursor st) "3j -> row 4"))

# count prefix: 2l
(let ((st (vi-run "hello" '("2" "l"))))
  (check '(1 . 2) (vi-cursor st) "2l -> col 2"))

# w -- word forward (to start of next word)
(let ((st (vi-run "hello world" '("w"))))
  (check '(1 . 6) (vi-cursor st) "w -> start of next word"))

# b -- word backward
(let ((st (vi-run "hello world" '("w" "b"))))
  (check '(1 . 0) (vi-cursor st) "w b -> back to word start"))

# e -- word end forward
(let ((st (vi-run "hello world" '("e"))))
  (check '(1 . 4) (vi-cursor st) "e -> end of first word"))

# 0 -- line start
(let ((st (vi-run "hello" '("l" "l" "l" "0"))))
  (check '(1 . 0) (vi-cursor st) "0 -> col 0"))

# $ -- line end (last char)
(let ((st (vi-run "hello" '("$"))))
  (check '(1 . 4) (vi-cursor st) "$ -> col 4 (last char)"))

# G -- last line
(let ((st (vi-run "a\nb\nc" '("G"))))
  (check '(3 . 0) (vi-cursor st) "G -> last row"))

# gg -- first line
(let ((st (vi-run "a\nb\nc" '("G" "g" "g"))))
  (check '(1 . 0) (vi-cursor st) "G gg -> row 1"))

# ── Operators ─────────────────────────────────────────────────────────────

# x -- delete char at cursor
(let ((st (vi-run "hello" '("x"))))
  (check "ello" (vi-text st) "x deletes char at cursor"))

# 2x -- delete 2 chars
(let ((st (vi-run "hello" '("2" "x"))))
  (check "llo" (vi-text st) "2x deletes 2 chars"))

# x at end of line
(let ((st (vi-run "hello" '("$" "x"))))
  (check "hell" (vi-text st) "x at $ deletes last char"))

# dd -- delete current line
(let ((st (vi-run "aaa\nbbb\nccc" '("d" "d"))))
  (check "bbb\nccc" (vi-text st) "dd deletes first line"))

# dd on last remaining line leaves empty buffer
(let ((st (vi-run "only" '("d" "d"))))
  (check "" (vi-text st) "dd on sole line leaves empty"))

# 2dd -- delete 2 lines
(let ((st (vi-run "aaa\nbbb\nccc" '("2" "d" "d"))))
  (check "ccc" (vi-text st) "2dd deletes 2 lines"))

# dw -- delete word
(let ((st (vi-run "hello world" '("d" "w"))))
  (check "world" (vi-text st) "dw deletes first word+space"))

# d$ -- delete to end of line
(let ((st (vi-run "hello world" '("l" "l" "l" "d" "$"))))
  (check "hel" (vi-text st) "d$ deletes to end of line"))

# yy + p -- yank line, then paste char-wise after cursor
# yy + p -- yank line, then paste BELOW (vim line-mode semantics)
(let ((st (vi-run "hello\nworld" '("y" "y" "p"))))
  (check "hello\nhello\nworld" (vi-text st) "yy p: line-mode paste below"))

# yy + P -- yank line, then paste ABOVE
(let ((st (vi-run "hello\nworld" '("j" "y" "y" "P"))))
  (check "hello\nworld\nworld" (vi-text st) "yy P: line-mode paste above"))

# J -- join lines (no space added, just concatenate)
(let ((st (vi-run "hello\nworld" '("J"))))
  (check "helloworld" (vi-text st) "J joins lines without space"))

# ── Search ────────────────────────────────────────────────────────────────

# / -- search forward, moves cursor to match
(let ((st (vi-run "bbb aaa bbb" '("/" "a" "a" "a" ret))))
  (check '(1 . 4) (vi-cursor st) "/aaa moves cursor to match"))

# / on unique string
(let ((st (vi-run "foo bar baz" '("/" "b" "a" "z" ret))))
  (check '(1 . 8) (vi-cursor st) "/baz moves to col 8"))

# n -- repeat search forward; /aaa from col 0 lands on col 8 (skips col 0),
# then n from col 8 wraps around to col 0
(let ((st (vi-run "aaa bbb aaa" '("/" "a" "a" "a" ret "n"))))
  (check '(1 . 0) (vi-cursor st) "n wraps search to first match"))

# ESC in search cancels (returns to normal)
(let ((st (vi-run "hello" '("/" "x" esc))))
  (check 'normal (vi-mode st) "ESC cancels search, returns to normal"))

(println "ALL TESTS PASSED")
