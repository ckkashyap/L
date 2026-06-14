(load "tests/vi_harness.l")

# :%s/from/to/ -- replace first occurrence on every line
(let ((st (vi-run "abc\nabc\nxyz"
            '(":" "%" "s" "/" "a" "/" "X" "/" ret))))
  (check "Xbc\nXbc\nxyz" (vi-text st) ":%s replaces on every line"))

# :%s/from/to/g -- replace all occurrences on every line
(let ((st (vi-run "aa aa\naa aa"
            '(":" "%" "s" "/" "a" "/" "X" "/" "g" ret))))
  (check "XX XX\nXX XX" (vi-text st) ":%s///g replaces all on every line"))

# :2s/from/to/ -- only on line 2
(let ((st (vi-run "abc\nabc\nabc"
            '(":" "2" "s" "/" "a" "/" "X" "/" ret))))
  (check "abc\nXbc\nabc" (vi-text st) ":2s targets a single line"))

# :1,2s/from/to/ -- lines 1..2 inclusive
(let ((st (vi-run "abc\nabc\nabc"
            '(":" "1" "," "2" "s" "/" "a" "/" "X" "/" ret))))
  (check "Xbc\nXbc\nabc" (vi-text st) ":1,2s targets a range"))

# :s without range still works on current line (existing behavior)
(let ((st (vi-run "abc\nabc"
            '("j" ":" "s" "/" "a" "/" "X" "/" ret))))
  (check "abc\nXbc" (vi-text st) ":s works on current line"))

(println "ALL TESTS PASSED")
