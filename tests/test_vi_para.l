(load "tests/vi_harness.l")

# In vim, paragraphs are separated by blank lines.  { goes to the
# previous blank line (or buffer start); } goes to the next blank
# line (or buffer end).  We implement that minimal definition.

# Simple two-paragraph buffer:
#   line 1: aaa
#   line 2: bbb
#   line 3: (blank)
#   line 4: ccc
#   line 5: ddd
(let ((text "aaa\nbbb\n\nccc\nddd"))
  # } from line 1 -> jump to the blank line 3
  (let ((st (vi-run text '("}"))))
    (check '(3 . 0) (vi-cursor st) "} from para 1 -> blank line"))

  # }} -> past the second paragraph (end of buffer)
  (let ((st (vi-run text '("}" "}"))))
    (check '(5 . 0) (vi-cursor st) "}} -> end of buffer"))

  # { from line 5 -> back to the blank line 3
  (let ((st (vi-run text '("G" "{"))))
    (check '(3 . 0) (vi-cursor st) "{ from end -> blank line"))

  # {{ -> start of buffer
  (let ((st (vi-run text '("G" "{" "{"))))
    (check '(1 . 0) (vi-cursor st) "{{ from end -> buffer start")))

# % matches paired brackets on the same line.
(let ((st (vi-run "if (a + b) {}" '("f" "(" "%"))))
  (check '(1 . 9) (vi-cursor st) "% from '(' jumps to matching ')'"))

(let ((st (vi-run "if (a + b) {}" '("f" "(" "%" "%"))))
  (check '(1 . 3) (vi-cursor st) "% then % returns to '('"))

(let ((st (vi-run "if (a + b) {}" '("f" "{" "%"))))
  (check '(1 . 12) (vi-cursor st) "% from '{' jumps to '}'"))

# % on a non-bracket char is a no-op
(let ((st (vi-run "hello" '("%"))))
  (check '(1 . 0) (vi-cursor st) "% on non-bracket is no-op"))

(println "ALL TESTS PASSED")
