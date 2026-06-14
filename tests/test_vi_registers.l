(load "tests/vi_harness.l")

# Yank a line into register a, move, paste from a
(let ((st (vi-run "aaa\nbbb\nccc"
            '("\"" "a" "y" "y" "j" "\"" "a" "p"))))
  (check "aaa\nbbb\naaa\nccc" (vi-text st)
    "\"ayy + j + \"ap pastes from register a"))

# Two registers in flight: a holds line 1, b holds line 3
# After "ayy: regs[a] = "L1\n"
# G -> cursor on L3 (row 3)
# After "byy: regs[b] = "L3\n"
# k -> back to L2 (row 2)
# "bp -> paste b below row 2 -> [L1, L2, L3-pasted, L3-orig].  Cursor row 3.
# "ap -> paste a below row 3 -> [L1, L2, L3, L1, L3]
(let ((st (vi-run "L1\nL2\nL3"
            '("\"" "a" "y" "y"
              "G" "\"" "b" "y" "y"
              "k" "\"" "b" "p"
              "\"" "a" "p"))))
  (check "L1\nL2\nL3\nL1\nL3" (vi-text st)
    "two named registers preserved"))

# Word yank into named register
(let ((st (vi-run "hello world"
            '("\"" "x" "y" "i" "w"
              "$" "\"" "x" "p"))))
  (check "hello worldhello" (vi-text st)
    "\"xyiw yanks word into x, \"xp pastes after cursor"))

# Delete into a named register; default register remains empty for unrelated p
(let ((st (vi-run "aaa\nbbb"
            '("\"" "c" "d" "d"
              "p"))))
  # dd into "c -- "" (unnamed) was NOT updated when a named register is targeted
  # so the bare p uses whatever "" had previously (nothing).  In vim "" also
  # mirrors named-register deletes; we only check that the buffer is now
  # "bbb" (line 1 went into c, not "") and a bare p was a no-op.
  (check "bbb" (vi-text st)
    "\"cdd deletes into c (unnamed register untouched)"))

# Active register clears after one paste -- next p uses the unnamed register
(let ((st (vi-run "alpha\nbeta"
            '("y" "y"               ; "" = "alpha\n"
              "\"" "a" "y" "y"      ; "a" = "alpha\n" (same line)
              "j" "p"))))            ; bare p uses "", pasting alpha below beta
  (check "alpha\nbeta\nalpha" (vi-text st)
    "bare p after a named yank uses the unnamed register"))

(println "ALL TESTS PASSED")
