(load "tests/vi_harness.l")

# 10ahello<esc> -> "hello" appended 10 times
(let ((st (vi-run ""
            '("1" "0" "a" "h" "e" "l" "l" "o" esc))))
  (check (pack (let ((acc NIL) (i 0))
                 (while (< i 10) (setq acc (cons "hello" acc)) (setq i (1+ i)))
                 acc))
         (vi-text st)
         "10ahello<esc> -> 10 copies of hello"))

# 3iabc<esc> -> "abc" inserted 3 times at start of line
(let ((st (vi-run "xyz" '("3" "i" "a" "b" "c" esc))))
  (check "abcabcabcxyz" (vi-text st) "3iabc<esc> -> 3 copies of abc at start"))

# Count 1 (default) is a no-op repeat -- just insert once
(let ((st (vi-run "" '("i" "a" "b" esc))))
  (check "ab" (vi-text st) "plain i with no count -> single insert"))

# Count 0 (no count prefix) treated like count 1
(let ((st (vi-run "" '("a" "x" esc))))
  (check "x" (vi-text st) "a (no count) -> single insert"))

# 5Aend<esc> -> 5 copies of "end" appended
(let ((st (vi-run "foo" '("5" "A" "Z" esc))))
  (check "fooZZZZZ" (vi-text st) "5AZ<esc> -> 5 Zs at end"))

# 4ox<esc> -> 'o' opens a new line and types 'x'; the COMMAND is
# repeated 4 times total, so 4 new lines are appended each containing 'x'.
(let ((st (vi-run "top" '("4" "o" "x" esc))))
  (check "top\nx\nx\nx\nx" (vi-text st) "4ox<esc> -> four new lines, each 'x'"))

# 10ohello<esc> -> 10 new lines, each "hello"
(let ((st (vi-run "top" '("1" "0" "o" "h" "e" "l" "l" "o" esc))))
  (check "top\nhello\nhello\nhello\nhello\nhello\nhello\nhello\nhello\nhello\nhello"
         (vi-text st) "10ohello<esc> -> ten new lines of hello"))

# 3Ofoo<esc> -> 'O' opens a new line ABOVE, repeated 3 times
(let ((st (vi-run "bottom" '("3" "O" "f" "o" "o" esc))))
  (check "foo\nfoo\nfoo\nbottom" (vi-text st) "3Ofoo<esc> -> three new lines above"))

# 3rx replaces 3 characters with x
(let ((st (vi-run "abcdef" '("3" "r" "x"))))
  (check "xxxdef" (vi-text st) "3rx replaces 3 chars with x"))

# 5rZ on a 4-char line is a no-op (not enough chars)
(let ((st (vi-run "abcd" '("5" "r" "Z"))))
  (check "abcd" (vi-text st) "rZ with count > line length is a no-op"))

(println "ALL TESTS PASSED")
