(load "tests/vi_harness.l")

# s = delete char + insert (substitute character)
(let ((st (vi-run "abc" '("s"))))
  (check "bc" (vi-text st) "s deletes the char under cursor")
  (check 'insert (vi-mode st) "s enters insert mode"))

# s with count: delete N chars then enter insert
(let ((st (vi-run "abcdef" '("3" "s"))))
  (check "def" (vi-text st) "3s deletes 3 chars")
  (check 'insert (vi-mode st) "3s enters insert mode"))

# S = delete entire line + insert (substitute line)
(let ((st (vi-run "abc\ndef\nghi" '("j" "S"))))
  (check "abc\n\nghi" (vi-text st) "S empties the current line")
  (check 'insert (vi-mode st) "S enters insert mode"))

(println "ALL TESTS PASSED")
