(load "tests/vi_harness.l")

# Line-mode paste semantics: yy/dd of single line + p should paste BELOW,
# not splice into the current line.  Before the fix the put-after logic
# decided line vs char by whether the yanked register contained a \n --
# a single-line yy contained none, so p went char-wise.

# yy + p on a 3-line buffer: cursor stays line 1, line 1 is duplicated below
(let ((st (vi-run "aaa\nbbb\nccc" '("y" "y" "p"))))
  (check "aaa\naaa\nbbb\nccc" (vi-text st) "yy p inserts a new line below")
  (check '(2 . 0) (vi-cursor st) "yy p moves cursor to pasted line"))

# dd + p on a 3-line buffer: line 1 is removed and re-inserted after the
# (now-first) bbb
(let ((st (vi-run "aaa\nbbb\nccc" '("d" "d" "p"))))
  (check "bbb\naaa\nccc" (vi-text st) "dd p moves first line down by one"))

# Multi-line yank already worked but let's lock it in:
(let ((st (vi-run "aaa\nbbb\nccc" '("V" "j" "y" "G" "p"))))
  (check "aaa\nbbb\nccc\naaa\nbbb" (vi-text st) "V j y G p appends 2 lines"))

# Char-mode yank/paste unchanged:
(let ((st (vi-run "hello world" '("y" "w" "$" "p"))))
  # yw yanks "hello ", $ moves to col 10 (d of "world"), p inserts after d
  (check "hello worldhello " (vi-text st) "yw + p still char-wise"))

# P (put before) for line-mode: pastes ABOVE current line
(let ((st (vi-run "aaa\nbbb\nccc" '("j" "y" "y" "P"))))
  (check "aaa\nbbb\nbbb\nccc" (vi-text st) "yy P inserts above current line"))

# Count with line-mode paste: yy + 3p inserts the line 3 times below
(let ((st (vi-run "aaa\nbbb" '("y" "y" "3" "p"))))
  (check "aaa\naaa\naaa\naaa\nbbb" (vi-text st) "yy 3p inserts 3 copies below"))

# Count with line-mode P
(let ((st (vi-run "aaa\nbbb" '("j" "y" "y" "3" "P"))))
  (check "aaa\nbbb\nbbb\nbbb\nbbb" (vi-text st) "yy 3P inserts 3 copies above"))

# Count with multi-line yank
(let ((st (vi-run "a\nb\nc" '("V" "j" "y" "G" "2" "p"))))
  (check "a\nb\nc\na\nb\na\nb" (vi-text st) "V j y G 2p inserts 2 copies of 2-line yank"))

(println "ALL TESTS PASSED")
