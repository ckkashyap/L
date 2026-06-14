(load "tests/vi_harness.l")

# ── Undo (u) ─────────────────────────────────────────────────────────────
# undo-save is called on enter-insert, so u after insert-mode edits works.

# i X ESC u -- undo insert restores original
(let ((st (vi-run "hello" '("i" "X" esc "u"))))
  (check "hello" (vi-text st) "u undoes insert"))

# u at oldest change is a no-op (msg is set, buffer unchanged)
(let ((st (vi-run "hello" '("u"))))
  (check "hello" (vi-text st) "u with no undo history is no-op"))

# Multiple undos
(let ((st (vi-run "hello" '("i" "A" esc "i" "B" esc "u" "u"))))
  # i A esc -> "Ahello"; i B esc -> "BAhello"; u -> "Ahello"; u -> "hello"
  (check "hello" (vi-text st) "two undos restore original"))

# ── Redo (ctrl-r) ─────────────────────────────────────────────────────────

# i X ESC u ctrl-r -- undo then redo
(let ((st (vi-run "hello" '("i" "X" esc "u" ctrl-r))))
  (check "Xhello" (vi-text st) "ctrl-r redoes after undo"))

# ctrl-r with no redo history is a no-op
(let ((st (vi-run "hello" '(ctrl-r))))
  (check "hello" (vi-text st) "ctrl-r with no redo history is no-op"))

# ── Macros ────────────────────────────────────────────────────────────────
# The harness records keys (other than "q") into the macro register during
# recording, merges those recordings back after each co-resume, and drains
# macro-inject after @register calls so playback actually fires.

# Record "j" to register a, play it back.
# qa: start recording; j: recorded (macros[a]=["j"]) + dispatched (row 1->2);
# q: stop recording; @a: inject ["j"], dispatch "j" -> row 2->3.
# Expected cursor: (3 . 0)
(let ((st (vi-run "aaa\nbbb\nccc" '("q" "a" "j" "q" "@" "a"))))
  (check '(3 . 0) (vi-cursor st) "@a: j recorded and played back, cursor at row 3"))

# Record "x" to register a, move down, play back.
# qa: start recording; x: recorded (macros[a]=["x"]) + dispatched (deletes 'a' -> "aa");
# q: stop; j: move to row 2; @a: inject ["x"], dispatch "x" on "bbb" -> "bb".
# Expected text: "aa\nbb\nccc"
(let ((st (vi-run "aaa\nbbb\nccc" '("q" "a" "x" "q" "j" "@" "a"))))
  (check "aa\nbb\nccc" (vi-text st) "@a: x recorded, played back on row 2"))

# 2@a -- play macro twice with count.
# qa: record "j" (macros[a]=["j"]) + dispatched (row 1->2); q: stop;
# 2@a: inject ["j","j"]; first "j" row 2->3, second "j" row 3->4.
# Expected cursor: (4 . 0)
(let ((st (vi-run "aaa\nbbb\nccc\nddd" '("q" "a" "j" "q" "2" "@" "a"))))
  (check '(4 . 0) (vi-cursor st) "2@a: j played twice, cursor at row 4"))

# Multi-key macro: A ! ESC recorded to register z, then played back.
# qz: start recording; A: recorded+dispatched (insert at end of "hello");
# !: recorded+dispatched (insert "!" -> "hello!"); esc: recorded+dispatched (normal);
# q: stop (macros[z]=["A" "!" esc]);
# @z: inject ["A" "!" esc] -> A appends at end of "hello!" -> "!" inserted -> "hello!!".
# Expected text: "hello!!\nworld"
(let ((st (vi-run "hello\nworld" '("q" "z" "A" "!" esc "q" "@" "z"))))
  (check "hello!!\nworld" (vi-text st) "@z: A!esc recorded and played back -> hello!!"))

(println "ALL TESTS PASSED")
