(load "tests/vi_harness.l")

# ─ Word objects ─────────────────────────────────────────────────────────────
# diw deletes the word under the cursor (no surrounding space)
(let ((st (vi-run "hello world" '("d" "i" "w"))))
  (check " world" (vi-text st) "diw on first word"))

# Cursor in the middle of a word still picks up the whole word
(let ((st (vi-run "hello world" '("l" "l" "d" "i" "w"))))
  (check " world" (vi-text st) "diw mid-word"))

# daw deletes the word AND a trailing space
(let ((st (vi-run "hello world" '("d" "a" "w"))))
  (check "world" (vi-text st) "daw on first word eats trailing space"))

# ciw replaces the word
(let ((st (vi-run "hello world" '("c" "i" "w"))))
  (check " world" (vi-text st) "ciw deletes the word")
  (check 'insert (vi-mode st) "ciw enters insert mode"))

# yiw yanks the word; p re-inserts it (char-wise after cursor)
(let ((st (vi-run "abc def" '("y" "i" "w" "$" "p"))))
  (check "abc defabc" (vi-text st) "yiw + $p appends word at line end"))

# ─ Quote objects ────────────────────────────────────────────────────────────
# di" deletes the contents between the surrounding quotes
(let ((st (vi-run "x = \"hello\" y" '("f" "h" "d" "i" "\""))))
  (check "x = \"\" y" (vi-text st) "di\" empties quoted string"))

# da" deletes the quoted string including the quotes
(let ((st (vi-run "x = \"hello\" y" '("f" "h" "d" "a" "\""))))
  (check "x =  y" (vi-text st) "da\" deletes the quotes too"))

# ─ Bracket objects ─────────────────────────────────────────────────────────
# di( -- delete inside parens
(let ((st (vi-run "f(a + b)" '("f" "a" "d" "i" "("))))
  (check "f()" (vi-text st) "di( empties parens"))

# da( -- including parens
(let ((st (vi-run "f(a + b)" '("f" "a" "d" "a" "("))))
  (check "f" (vi-text st) "da( eats the parens too"))

# Same with i) and a) -- vim treats both the open and close as triggers
(let ((st (vi-run "f(x)" '("f" "x" "d" "i" ")"))))
  (check "f()" (vi-text st) "di) same as di("))

# ─ Brace objects ────────────────────────────────────────────────────────────
(let ((st (vi-run "{abc}" '("f" "b" "d" "i" "{"))))
  (check "{}" (vi-text st) "di{ empties braces"))

(println "ALL TESTS PASSED")
