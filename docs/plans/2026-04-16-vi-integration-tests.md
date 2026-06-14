# Vi Integration Test Suite Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a shared test harness and pre-written integration tests covering all major vi features by driving the mode-dispatch coroutine with synthetic key sequences.

**Architecture:** `tests/vi_harness.l` defines `vi-run`/`check` helpers; five `test_vi_*.l` files load the harness and test feature areas. No terminal or SDL required — the dispatch coroutine is pure Lisp.

**Tech Stack:** L (picolisp dialect), existing `lib/vi/*.l`, `run_tests.sh` test runner.

---

### Task 1: Write `tests/vi_harness.l`

**Files:**
- Create: `tests/vi_harness.l`

**Step 1: Write the file**

```lisp
# tests/vi_harness.l — shared helpers for vi integration tests.
# Load with (load "tests/vi_harness.l"). Not run directly.

(load "lib/ds.l")
(load "lib/string.l")
(load "lib/vi/buffer.l")
(load "lib/vi/state.l")
(load "lib/vi/cursor.l")
(load "lib/vi/ops.l")
(load "lib/vi/search.l")
(load "lib/vi/visual.l")
(load "lib/vi/macro.l")
(load "lib/vi/file.l")
(load "lib/vi/cmd.l")
(load "lib/vi/mode.l")

# (check expected actual label) — print OK/FAIL; quit 1 on failure.
(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog
      (println "FAIL:" label)
      (println "  expected:" expected)
      (println "  actual  :" actual)
      (quit 1))))

# Drain macro-inject: feed queued keys to disp until the queue is empty.
# Handles nested playback (macros that trigger macros).
(de vi-drain-inject (state disp)
  (let ((inject (s-get state 'macro-inject)))
    (if (null inject)
      state
      (prog
        (setq state (s-set state 'macro-inject NIL))
        (let ((ks inject))
          (while ks
            (setq state (co-resume disp (car ks)))
            (setq ks (cdr ks))))
        (vi-drain-inject state disp)))))

# (vi-run text keys) -> final state.
# Creates a fresh buffer+state, primes the dispatch coroutine, then feeds
# each key in order. Simulates the macro filter: keys are added to the
# recording when macro-rec is active, and "q" in normal mode stops recording.
(de vi-run (text keys)
  (let* ((buf   (buf-from-string text))
         (st0   (make-state buf))
         (disp  (make-mode-dispatch st0))
         (state (co-resume disp NIL))   # prime: runs to first (yield state)
         (ks    keys))
    (while ks
      (let ((k (car ks)))
        (setq ks (cdr ks))
        (if (and (equal k "q")
                 (s-get state 'macro-rec)
                 (equal (s-get state 'mode) 'normal))
          # Stop recording — do NOT send "q" to dispatch
          (setq state (stop-macro-record state))
          (prog
            # Add key to recording if active (simulates make-macro-filter)
            (when (s-get state 'macro-rec)
              (setq state (macro-add-key state k)))
            (setq state (co-resume disp k))
            # Drain any macro-inject produced by @register playback
            (setq state (vi-drain-inject state disp))))))
    state))

# Convenience accessors
(de vi-text   (state) (buf-to-string (s-get state 'buffer)))
(de vi-cursor (state) (s-get state 'cursor))
(de vi-mode   (state) (s-get state 'mode))
```

**Step 2: Verify it loads cleanly**

```bash
./build/picolisp -e '(load "tests/vi_harness.l") (println "harness OK")'
```

Expected output: `harness OK` (no errors).

**Step 3: Commit**

```bash
git add tests/vi_harness.l
git commit -m "test(vi): add vi_harness.l with vi-run/check helpers"
```

---

### Task 2: Write `tests/test_vi_normal.l` — motions, operators, search

**Files:**
- Create: `tests/test_vi_normal.l`

**Step 1: Write the file**

```lisp
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

# w — word forward (to start of next word)
(let ((st (vi-run "hello world" '("w"))))
  (check '(1 . 6) (vi-cursor st) "w -> start of next word"))

# b — word backward
(let ((st (vi-run "hello world" '("w" "b"))))
  (check '(1 . 0) (vi-cursor st) "w b -> back to word start"))

# e — word end forward
(let ((st (vi-run "hello world" '("e"))))
  (check '(1 . 4) (vi-cursor st) "e -> end of first word"))

# 0 — line start
(let ((st (vi-run "hello" '("l" "l" "l" "0"))))
  (check '(1 . 0) (vi-cursor st) "0 -> col 0"))

# $ — line end (last char)
(let ((st (vi-run "hello" '("$"))))
  (check '(1 . 4) (vi-cursor st) "$ -> col 4 (last char)"))

# G — last line
(let ((st (vi-run "a\nb\nc" '("G"))))
  (check '(3 . 0) (vi-cursor st) "G -> last row"))

# gg — first line
(let ((st (vi-run "a\nb\nc" '("G" "g" "g"))))
  (check '(1 . 0) (vi-cursor st) "G gg -> row 1"))

# ── Operators ─────────────────────────────────────────────────────────────

# x — delete char at cursor
(let ((st (vi-run "hello" '("x"))))
  (check "ello\n" (vi-text st) "x deletes char at cursor"))

# 2x — delete 2 chars
(let ((st (vi-run "hello" '("2" "x"))))
  (check "llo\n" (vi-text st) "2x deletes 2 chars"))

# x at end of line
(let ((st (vi-run "hello" '("$" "x"))))
  (check "hell\n" (vi-text st) "x at $ deletes last char"))

# dd — delete current line
(let ((st (vi-run "aaa\nbbb\nccc" '("d" "d"))))
  (check "bbb\nccc\n" (vi-text st) "dd deletes first line"))

# dd on last remaining line leaves empty buffer
(let ((st (vi-run "only" '("d" "d"))))
  (check "\n" (vi-text st) "dd on sole line leaves empty"))

# 2dd — delete 2 lines
(let ((st (vi-run "aaa\nbbb\nccc" '("2" "d" "d"))))
  (check "ccc\n" (vi-text st) "2dd deletes 2 lines"))

# dw — delete word
(let ((st (vi-run "hello world" '("d" "w"))))
  (check "world\n" (vi-text st) "dw deletes first word+space"))

# d$ — delete to end of line
(let ((st (vi-run "hello world" '("l" "l" "l" "d" "$"))))
  (check "hel\n" (vi-text st) "d$ deletes to end of line"))

# yy + p — yank line and paste below
(let ((st (vi-run "hello\nworld" '("y" "y" "p"))))
  (check "hello\nhello\nworld\n" (vi-text st) "yy p duplicates line"))

# yy + P — yank line and paste above
(let ((st (vi-run "hello\nworld" '("j" "y" "y" "P"))))
  (check "hello\nworld\nworld\n" (vi-text st) "yy P pastes line above"))

# J — join lines
(let ((st (vi-run "hello\nworld" '("J"))))
  (check "hello world\n" (vi-text st) "J joins lines with space"))

# ── Search ────────────────────────────────────────────────────────────────

# / — search forward, moves cursor to match
(let ((st (vi-run "bbb aaa bbb" '("/" "a" "a" "a" 'ret))))
  (check '(1 . 4) (vi-cursor st) "/aaa moves cursor to match"))

# / on unique string
(let ((st (vi-run "foo bar baz" '("/" "b" "a" "z" 'ret))))
  (check '(1 . 8) (vi-cursor st) "/baz moves to col 8"))

# n — repeat search forward
(let ((st (vi-run "aaa bbb aaa" '("/" "a" "a" "a" 'ret "n"))))
  (check '(1 . 8) (vi-cursor st) "n moves to second match"))

# ESC in search cancels (returns to normal)
(let ((st (vi-run "hello" '("/" "x" 'esc))))
  (check 'normal (vi-mode st) "ESC cancels search, returns to normal"))

(println "ALL TESTS PASSED")
```

**Step 2: Run it**

```bash
./build/picolisp tests/test_vi_normal.l
```

Expected: each line prints `OK: <label>`, final line `ALL TESTS PASSED`.

**Step 3: Commit**

```bash
git add tests/test_vi_normal.l
git commit -m "test(vi): add normal mode motion, operator and search tests"
```

---

### Task 3: Write `tests/test_vi_insert.l`

**Files:**
- Create: `tests/test_vi_insert.l`

**Step 1: Write the file**

```lisp
(load "tests/vi_harness.l")

# ── Mode entry ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '("i"))))
  (check 'insert (vi-mode st) "i enters insert mode"))

(let ((st (vi-run "hello" '("i" 'esc))))
  (check 'normal (vi-mode st) "ESC returns to normal"))

# ── i — insert before cursor ──────────────────────────────────────────────

# Type a char and ESC
(let ((st (vi-run "hello" '("i" "X" 'esc))))
  (check "Xhello\n" (vi-text st) "i X ESC inserts before cursor"))

# Cursor lands one before where we typed (ESC in insert retracts one)
(let ((st (vi-run "hello" '("l" "l" "i" "X" 'esc))))
  (check "heXllo\n" (vi-text st) "ll i X inserts at col 2"))

# ── a — append after cursor ───────────────────────────────────────────────

(let ((st (vi-run "hello" '("a" "X" 'esc))))
  (check "hXello\n" (vi-text st) "a X ESC appends after col 0"))

(let ((st (vi-run "hi" '("l" "a" "!" 'esc))))
  (check "hi!\n" (vi-text st) "l a ! appends after last char"))

# ── A — append at end of line ─────────────────────────────────────────────

(let ((st (vi-run "hello" '("A" "!" 'esc))))
  (check "hello!\n" (vi-text st) "A ! ESC appends at end of line"))

# ── o — open line below ───────────────────────────────────────────────────

(let ((st (vi-run "hello\nworld" '("o" "n" "e" "w" 'esc))))
  (check "hello\nnew\nworld\n" (vi-text st) "o opens line below, types text"))

# Cursor should be on the new line after ESC
(let ((st (vi-run "hello\nworld" '("o" "x" 'esc))))
  (check 2 (car (vi-cursor st)) "o cursor lands on new line 2"))

# ── O — open line above ───────────────────────────────────────────────────

(let ((st (vi-run "hello\nworld" '("j" "O" "n" "e" "w" 'esc))))
  (check "hello\nnew\nworld\n" (vi-text st) "O opens line above, types text"))

# ── Typing multiple chars ─────────────────────────────────────────────────

(let ((st (vi-run "" '("i" "h" "i" 'esc))))
  (check "hi\n" (vi-text st) "typing two chars in insert mode"))

# ── Backspace in insert mode ──────────────────────────────────────────────

# BS deletes previous char
(let ((st (vi-run "hello" '("l" "l" "i" 'bs 'esc))))
  (check "hllo\n" (vi-text st) "BS in insert deletes previous char"))

# BS at col 0 on row > 1 joins with previous line
(let ((st (vi-run "hello\nworld" '("j" "i" 'bs 'esc))))
  (check "helloworld\n" (vi-text st) "BS at start of line joins lines"))

# BS at col 0, row 1 is a no-op
(let ((st (vi-run "hello" '("i" 'bs 'esc))))
  (check "hello\n" (vi-text st) "BS at first position is no-op"))

# ── Enter in insert mode ──────────────────────────────────────────────────

(let ((st (vi-run "hello" '("l" "l" "i" 'ret 'esc))))
  (check "he\nllo\n" (vi-text st) "RET in insert splits line"))

(let ((st (vi-run "hello" '("l" "l" "i" 'ret 'esc))))
  (check '(2 . 0) (vi-cursor st) "RET cursor lands at start of new line"))

(println "ALL TESTS PASSED")
```

**Step 2: Run it**

```bash
./build/picolisp tests/test_vi_insert.l
```

**Step 3: Commit**

```bash
git add tests/test_vi_insert.l
git commit -m "test(vi): add insert mode tests"
```

---

### Task 4: Write `tests/test_vi_visual.l`

**Files:**
- Create: `tests/test_vi_visual.l`

**Step 1: Write the file**

```lisp
(load "tests/vi_harness.l")

# ── Mode entry ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '("v"))))
  (check 'visual-char (vi-mode st) "v enters visual-char"))

(let ((st (vi-run "hello" '("V"))))
  (check 'visual-line (vi-mode st) "V enters visual-line"))

(let ((st (vi-run "hello" '("v" 'esc))))
  (check 'normal (vi-mode st) "ESC exits visual to normal"))

(let ((st (vi-run "hello" '("V" 'esc))))
  (check 'normal (vi-mode st) "ESC exits visual-line to normal"))

# ── char visual: v + motion + d ───────────────────────────────────────────

# v l d — select two chars (cols 0-1) and delete
(let ((st (vi-run "hello" '("v" "l" "d"))))
  (check "llo\n" (vi-text st) "v l d deletes 2-char selection"))

# v $  d — select to end of line and delete
(let ((st (vi-run "hello world" '("v" "$" "d"))))
  (check "\n" (vi-text st) "v $ d deletes whole line content"))

# v + y — yank char selection, returns to normal
(let ((st (vi-run "hello" '("v" "l" "l" "y"))))
  (check 'normal (vi-mode st) "v y returns to normal"))

# Verify yanked text can be pasted
(let ((st (vi-run "hello world" '("v" "l" "l" "l" "l" "y" "$" "p"))))
  # v llll selects "hello" (cols 0-4), y yanks, $ goes to end, p pastes after
  (check 'normal (vi-mode st) "v y p works without crash"))

# ── line visual: V + d ────────────────────────────────────────────────────

# V d — delete current line (line visual)
(let ((st (vi-run "hello\nworld" '("V" "d"))))
  (check "world\n" (vi-text st) "V d deletes line"))

# V j d — extend selection down one line, delete both
(let ((st (vi-run "aaa\nbbb\nccc" '("V" "j" "d"))))
  (check "ccc\n" (vi-text st) "V j d deletes two lines"))

# V y + p — yank whole line and paste below
(let ((st (vi-run "aaa\nbbb" '("j" "V" "y" "k" "p"))))
  (check "aaa\nbbb\nbbb\n" (vi-text st) "V y p duplicates line via visual"))

# ── visual-char: v + c — change (delete + enter insert) ──────────────────

(let ((st (vi-run "hello" '("v" "l" "c"))))
  (check 'insert (vi-mode st) "v c enters insert mode"))

(let ((st (vi-run "hello" '("v" "l" "c" "X" 'esc))))
  (check "Xllo\n" (vi-text st) "v l c X replaces selection"))

(println "ALL TESTS PASSED")
```

**Step 2: Run it**

```bash
./build/picolisp tests/test_vi_visual.l
```

**Step 3: Commit**

```bash
git add tests/test_vi_visual.l
git commit -m "test(vi): add visual mode tests"
```

---

### Task 5: Write `tests/test_vi_cmd.l`

**Files:**
- Create: `tests/test_vi_cmd.l`

**Step 1: Write the file**

```lisp
(load "tests/vi_harness.l")

# ── : enters command mode ─────────────────────────────────────────────────

(let ((st (vi-run "hello" '(":"))))
  (check 'command (vi-mode st) ": enters command mode"))

# ESC in command mode returns to normal
(let ((st (vi-run "hello" '(":" 'esc))))
  (check 'normal (vi-mode st) "ESC from command -> normal"))

# ── :q — quit ────────────────────────────────────────────────────────────

(let ((st (vi-run "hello" '(":" "q" 'ret))))
  (check NIL (s-get st 'running) ":q sets running to NIL"))

# :q! — force quit regardless of dirty
(let ((st (vi-run "hello" '("i" "X" 'esc ":" "q" "!" 'ret))))
  (check NIL (s-get st 'running) ":q! force quits with unsaved changes"))

# ── :w — write ───────────────────────────────────────────────────────────

# :w /tmp/path sets filename
(let ((st (vi-run "hello" '(":" "w" " " "/" "t" "m" "p" "/" "v" "i" "t" "e" "s" "t" "1" "." "t" "x" "t" 'ret))))
  (check "/tmp/vitest1.txt" (s-get st 'filename) ":w sets filename"))

# :w clears dirty flag
(let ((st (vi-run "hello" '("i" "X" 'esc ":" "w" " " "/" "t" "m" "p" "/" "v" "i" "t" "e" "s" "t" "2" "." "t" "x" "t" 'ret))))
  (check NIL (s-get st 'dirty) ":w clears dirty flag"))

# ── :N — jump to line N ───────────────────────────────────────────────────

(let ((st (vi-run "aaa\nbbb\nccc\nddd" '(":" "3" 'ret))))
  (check '(3 . 0) (vi-cursor st) ":3 jumps to line 3"))

(let ((st (vi-run "aaa\nbbb\nccc\nddd" '(":" "1" 'ret))))
  (check '(1 . 0) (vi-cursor st) ":1 jumps to line 1"))

# ── :s — substitution ────────────────────────────────────────────────────

# :s/a/X/g — replace all 'a' with 'X' on current line
(let ((st (vi-run "banana" '(":" "s" "/" "a" "/" "X" "/" "g" 'ret))))
  (check "bXnXnX\n" (vi-text st) ":s/a/X/g replaces all on line"))

# :s with no match — line unchanged
(let ((st (vi-run "hello" '(":" "s" "/" "z" "/" "X" "/" "g" 'ret))))
  (check "hello\n" (vi-text st) ":s with no match leaves line unchanged"))

# :s on line 2 when cursor is on line 2
(let ((st (vi-run "aaa\nbbb\nccc" '("j" ":" "s" "/" "b" "/" "Z" "/" "g" 'ret))))
  (check "aaa\nZZZ\nccc\n" (vi-text st) ":s on line 2 only affects line 2"))

# ── cmd-buf accumulates correctly ────────────────────────────────────────

# Backspace in command mode removes last char
(let ((st (vi-run "hello" '(":" "q" 'bs 'esc))))
  (check 'normal (vi-mode st) "BS then ESC in cmd returns to normal"))

(println "ALL TESTS PASSED")
```

**Step 2: Run it**

```bash
./build/picolisp tests/test_vi_cmd.l
```

**Step 3: Commit**

```bash
git add tests/test_vi_cmd.l
git commit -m "test(vi): add ex command tests (:q :w :N :s)"
```

---

### Task 6: Write `tests/test_vi_undo_macro.l`

**Files:**
- Create: `tests/test_vi_undo_macro.l`

**Step 1: Write the file**

```lisp
(load "tests/vi_harness.l")

# ── Undo (u) ─────────────────────────────────────────────────────────────
# undo-save is called by enter-insert, so u after insert mode works.

# i X ESC u — undo insert restores original
(let ((st (vi-run "hello" '("i" "X" 'esc "u"))))
  (check "hello\n" (vi-text st) "u undoes insert"))

# u at oldest change is a no-op (msg is set, buffer unchanged)
(let ((st (vi-run "hello" '("u"))))
  (check "hello\n" (vi-text st) "u with no undo history is no-op"))

# Multiple undos
(let ((st (vi-run "hello" '("i" "A" 'esc "i" "B" 'esc "u" "u"))))
  # i A esc: "Ahello", i B esc: "BAhello", u: "Ahello", u: "hello"
  (check "hello\n" (vi-text st) "two undos restore original"))

# ── Redo (ctrl-r) ─────────────────────────────────────────────────────────

# i X ESC u ctrl-r — undo then redo
(let ((st (vi-run "hello" '("i" "X" 'esc "u" 'ctrl-r))))
  (check "Xhello\n" (vi-text st) "ctrl-r redoes after undo"))

# ctrl-r with no redo history is a no-op
(let ((st (vi-run "hello" '('ctrl-r))))
  (check "hello\n" (vi-text st) "ctrl-r with no redo history is no-op"))

# ── Macros ────────────────────────────────────────────────────────────────
# Macro recording: q<reg> ... q   Playback: @<reg>
# The harness simulates make-macro-filter: adds keys to macro-rec,
# and intercepts final "q" to call stop-macro-record.

# Record "j" to register a, play it back
(let ((st (vi-run "aaa\nbbb\nccc" '("q" "a" "j" "q" "@" "a"))))
  # qa: start recording to 'a'
  # j: move down (recorded AND executed) -> cursor row 2
  # q: stop recording (macro-rec=NIL, macros[0]=["j"])
  # @a: play-macro sets macro-inject=["j"]; drain feeds "j" to disp -> row 3
  (check '(3 . 0) (vi-cursor st) "@a plays back recorded j"))

# Record "x" (delete char) and play on next line
(let ((st (vi-run "aaa\nbbb\nccc" '("q" "a" "x" "q" "j" "@" "a"))))
  # qa: record to 'a'; x: delete 'a' from "aaa" -> "aa" (cursor row 1)
  # q: stop; j: go to row 2; @a: play 'x' on "bbb" -> "bb"
  (check "aa\nbb\nccc\n" (vi-text st) "@a plays deletion on new line"))

# 2@a — play macro twice with count
(let ((st (vi-run "aaa\nbbb\nccc\nddd" '("q" "a" "j" "q" "2" "@" "a"))))
  # qa: record 'j'; q: stop; 2@a: inject ["j","j"]; cursor moves 2 rows
  (check '(3 . 0) (vi-cursor st) "2@a plays macro twice"))

# @@ — repeat last macro (not yet tested, skip if not implemented)
# Register 'z' — record and play back a multi-key sequence
(let ((st (vi-run "hello\nworld" '("q" "z" "A" "!" 'esc "q" "@" "z"))))
  # qz: record to 'z'; A: append at end; !: type '!'; ESC: exit insert
  # The recorded keys are: A ! ESC
  # q: stop; @z: play -> A ! ESC on "world" -> "world!"
  (check "hello!\nworld!\n" (vi-text st) "@z plays multi-key sequence"))

(println "ALL TESTS PASSED")
```

**Step 2: Run it**

```bash
./build/picolisp tests/test_vi_undo_macro.l
```

**Step 3: Commit**

```bash
git add tests/test_vi_undo_macro.l
git commit -m "test(vi): add undo, redo and macro recording/playback tests"
```

---

### Task 7: Verify full test suite

**Step 1: Run all tests**

```bash
bash run_tests.sh
```

Expected output: all `test_vi_*.l` files report PASS, none report FAIL or SKIP. The `vi_harness.l` file is skipped (no `ALL TESTS PASSED` marker — correct).

**Step 2: If any test fails, use systematic-debugging skill to diagnose**

Failure pattern: `FAIL: <label>` followed by `expected:` / `actual:` lines. Read the failing test, trace which vi function produced the wrong value, and fix the test expectation or the bug.

**Step 3: Final commit (if any fixes were needed)**

```bash
git add -p
git commit -m "test(vi): fix test expectations after discovery"
```
