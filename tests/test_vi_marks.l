(load "tests/vi_harness.l")

# ma sets mark 'a' at the cursor; 'a (or `a) jumps back to it.
# ' goes to the BOL of the marked line; ` goes to the exact (row, col).

# Build buf, mark line 2 col 3, move, jump back
(let ((st (vi-run "aaa\nbbbbb\nccc"
            '("j" "l" "l" "l" "m" "a" "G" "'" "a"))))
  (check '(2 . 0) (vi-cursor st) "'a -> BOL of marked line"))

(let ((st (vi-run "aaa\nbbbbb\nccc"
            '("j" "l" "l" "l" "m" "a" "G" "`" "a"))))
  (check '(2 . 3) (vi-cursor st) "`a -> exact marked column"))

# Re-marking 'a moves it.
(let ((st (vi-run "aaa\nbbb\nccc"
            '("m" "a" "j" "m" "a" "G" "'" "a"))))
  (check '(2 . 0) (vi-cursor st) "remarking moves the mark"))

# Unset mark is a no-op.
(let ((st (vi-run "aaa\nbbb" '("j" "'" "a"))))
  (check '(2 . 0) (vi-cursor st) "jumping to unset mark is a no-op"))

(println "ALL TESTS PASSED")
