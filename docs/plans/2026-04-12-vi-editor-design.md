# Vi Editor Implementation Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a complete vi editor (normal/insert/visual modes, macros, :commands, search) in pure L with minimal FFI, running in both terminal and graphical (SDL) modes.

**Architecture:** Coroutine pipeline — `key-source → macro-filter → mode-dispatch → renderer`. The two platform seams (key-source and renderer) are the only parts that differ between terminal and graphical mode. All editor logic is shared pure L.

**Tech Stack:** L interpreter (pvec, coroutines, strings, property lists), libffi for terminal raw mode and SDL event bridging, ANSI escape codes for terminal rendering, SDL for graphical mode.

---

## Section 1 — Architecture

### File layout (`lib/vi/`)

```
vi.l          — entry point, startup/shutdown, main loop
buffer.l      — pvec-of-strings buffer, edit primitives
cursor.l      — cursor movement, viewport scroll, line clamping
mode.l        — mode dispatch coroutine (normal/insert/visual/command)
ops.l         — operators (d/c/y/</>), motions (w/b/e/0/$^GgG), text objects
visual.l      — visual selection logic (char/line/block) + operators on selection
search.l      — / ? n N search, basic regex engine, :s/// substitution
cmd.l         — : command line parsing and execution (:w :q :e :set :s :N)
undo.l        — undo/redo stack (pvec snapshots, structural sharing)
register.l    — named registers "a–"z, unnamed ", clipboard "+
macro.l       — macro recording/playback key queue (qa–qz, @a–@z)
mark.l        — marks ma–mz, '' (last jump position)
term.l        — FFI: raw mode, terminal size, ANSI renderer
gfx.l         — FFI: SDL window, events, bitmap font, SDL renderer
```

### Coroutine pipeline

```
key-source ──► macro-filter ──► mode-dispatch ──► renderer
   │               │                 │               │
stdin/SDL     record/inject      state machine   ANSI/SDL
(platform)    (pure L)           (pure L)        (platform)
```

The macro-filter owns its own key buffer. It returns a buffered key if one exists; otherwise it resumes key-source for the next raw key. The main loop is simply:

```lisp
(while (s-get state 'running)
  (let* ((key   (co-resume mac-flt NIL))
         (state (co-resume mode-dsp key)))
    (co-resume renderer state)))
```

### Editor state (property list)

```lisp
; Core
buffer    pvec-of-strings     ; lines, no trailing newlines
cursor    (row . col)         ; 1-based row, 0-based col
scroll    (top . left)        ; viewport top-line, left-col
mode      'normal|'insert|'visual-char|'visual-line|'visual-block|'command

; Features
visual    NIL | (anchor-row . anchor-col)
cmd-buf   ""                  ; : command buffer being typed
search    ""                  ; last / or ? pattern
search-dir 'forward           ; 'forward or 'backward (for n/N)
registers  (pvec 26 NIL)      ; "a – "z
marks      (pvec 26 NIL)      ; ma – mz, stored as (row . col)
macros     (pvec 26 NIL)      ; qa – qz, each a list of key values
macro-rec  NIL | 0–25         ; index of register being recorded

; Lifecycle
undo      NIL | list          ; stack of (buffer . cursor) snapshots
redo      NIL | list
filename  NIL | string
dirty     NIL | T
msg       ""                  ; status bar message (cleared on next key)
running   T                   ; set to NIL to exit loop
```

State accessors:

```lisp
(de s-get (state key)      (cadr (member key state)))
(de s-set (state key val)
  (if (member key state)
    (let ((s (copy state)))
      (setcar (cdr (member key s)) val) s)
    (append state (list key val))))
```

---

## Section 2 — Buffer Model

### Representation

A buffer is a pvec of strings — one string per line, no trailing newlines. Empty file = `(pvec "")`.

pvec gives O(log n) line lookup and update. Structural sharing means undo snapshots are cheap.

### Core primitives (all pure, return new buffer)

```lisp
(de buf-line     (buf row)      (pvec-get buf (1- row)))
(de buf-nlines   (buf)          (pvec-count buf))
(de buf-set-line (buf row str)  (pvec-put buf (1- row) str))
(de buf-ins-line (buf row str)  ; insert before row
  ...)  ; rebuild pvec with new line inserted
(de buf-del-line (buf row)      ; remove row
  ...)  ; rebuild pvec with row removed
```

### Character-level ops (pure string arithmetic)

```lisp
; Insert char ch (code point) at (row, col)
(de buf-ins-char (buf row col ch)
  (let ((line (buf-line buf row)))
    (buf-set-line buf row
      (pack (list (sub line 0 col) (char ch) (sub line col))))))

; Delete char at (row, col)
(de buf-del-char (buf row col)
  (let ((line (buf-line buf row)))
    (buf-set-line buf row
      (pack (list (sub line 0 col) (sub line (1+ col)))))))

; Split line at col — Enter in insert mode
(de buf-split-line (buf row col)
  (let ((line (buf-line buf row)))
    (buf-ins-line
      (buf-set-line buf row (sub line 0 col))
      (1+ row)
      (sub line col))))

; Join line row with row+1 — J, backspace at start of line
(de buf-join-lines (buf row)
  (let ((a (buf-line buf row))
        (b (buf-line buf (1+ row))))
    (buf-del-line (buf-set-line buf row (pack (list a b))) (1+ row))))
```

### Undo/redo

Each mutating operation first pushes a snapshot:

```lisp
(de undo-save (state)
  (s-set state 'undo
    (cons (cons (s-get state 'buffer) (s-get state 'cursor))
          (s-get state 'undo))))

(de undo-pop (state)
  (let ((stack (s-get state 'undo)))
    (when stack
      (let ((snap (car stack)))
        (s-set (s-set (s-set state 'undo (cdr stack))
                      'buffer (car snap))
               'cursor (cdr snap))))))
```

---

## Section 3 — Mode State Machine

### Mode dispatch coroutine

```lisp
(de make-mode-dispatch (init-state)
  (co (lambda (_)
    (let ((state init-state)
          (count 0)
          (op    NIL)
          (pfx   NIL))
      (while T
        (let ((key (yield state)))
          (cond
            ; prefix pending (g, Z, r, f, F, t, T, ', m)
            (pfx
             (setq state (exec-prefix state pfx key (max 1 count)))
             (setq pfx NIL) (setq count 0) (setq op NIL))

            ; visual modes
            ((member (s-get state 'mode)
                     '(visual-char visual-line visual-block))
             (setq state (visual-key state key count))
             (setq count 0))

            ; insert mode
            ((equal (s-get state 'mode) 'insert)
             (setq state (insert-key state key)))

            ; command mode
            ((equal (s-get state 'mode) 'command)
             (setq state (cmd-key state key count)))

            ; normal mode
            (T
             (cond
               ; non-zero digits (or any digit when count already > 0)
               ((and (string? key)
                     (member key '("1" "2" "3" "4" "5" "6" "7" "8" "9")))
                (setq count (+ (* count 10) (- (car (unpack key)) 48))))
               ((and (equal key "0") (> count 0))
                (setq count (* count 10)))

               ; two-key prefixes
               ((member key '("g" "Z" "r" "f" "F" "t" "T" "'" "m" "q" "@"))
                (setq pfx key))

               ; operators (d c y < >)
               ((member key '("d" "c" "y" "<" ">"))
                (if (equal op key)
                  (progn
                    (setq state (exec-linewise-op state key (max 1 count)))
                    (setq op NIL) (setq count 0))
                  (setq op key)))

               ; motions — move or apply op
               ((motion-key? key)
                (setq state (exec-motion state key (max 1 count) op))
                (setq op NIL) (setq count 0))

               ; mode switches
               ((member key '("i" "I" "a" "A" "o" "O"))
                (setq state (enter-insert state key))
                (setq op NIL) (setq count 0))
               ((equal key "v")
                (setq state (enter-visual state 'visual-char))
                (setq op NIL) (setq count 0))
               ((equal key "V")
                (setq state (enter-visual state 'visual-line))
                (setq op NIL) (setq count 0))
               ((equal key ":")
                (setq state (s-set (s-set state 'mode 'command) 'cmd-buf ""))
                (setq op NIL) (setq count 0))
               ((equal key "/")
                (setq state (start-search state 'forward))
                (setq op NIL) (setq count 0))
               ((equal key "?")
                (setq state (start-search state 'backward)))

               ; single normal commands
               ((equal key "u")   (setq state (undo-pop state)))
               ((equal key "p")   (setq state (put-after  state (max 1 count))))
               ((equal key "P")   (setq state (put-before state (max 1 count))))
               ((equal key "x")   (setq state (delete-char-at-cursor state (max 1 count))))
               ((equal key "J")   (setq state (join-lines state (max 1 count))))
               ((equal key "~")   (setq state (toggle-case state)))
               ((equal key ".")   (setq state (repeat-last state)))
               ((equal key "n")   (setq state (search-next state (max 1 count))))
               ((equal key "N")   (setq state (search-prev state (max 1 count))))
               (T NIL)   ; unknown key — ignore
               ))))
        ))))))
```

### Prefix key dispatch

```lisp
(de exec-prefix (state pfx key count)
  (cond
    ((equal pfx "g")
     (cond
       ((equal key "g") (goto-line state 1))
       ((equal key "e") (move-back-word-end state count))
       ((equal key "E") (move-back-WORD-end state count))
       (T state)))
    ((equal pfx "Z")
     (cond
       ((equal key "Z") (cmd-write-quit state))
       ((equal key "Q") (s-set state 'running NIL))
       (T state)))
    ((equal pfx "r")    (replace-char state key))
    ((equal pfx "m")    (set-mark state key))
    ((equal pfx "'")    (jump-mark state key))
    ((equal pfx "`")    (jump-mark-exact state key))
    ((member pfx '("f" "F" "t" "T"))
     (find-char state pfx key count))
    ((equal pfx "q")    (start-macro-record state key))
    ((equal pfx "@")    (play-macro state key count))
    (T state)))
```

---

## Section 4 — Key Pipeline

### Key representation

```lisp
"a" "Z" ":" "/"        ; printable → single-char string
'esc 'ret 'bs 'tab     ; control → symbol
'up 'down 'left 'right ; arrows → symbol
'home 'end 'ins 'del   ; navigation → symbol
'pgup 'pgdn            ; page → symbol
'ctrl-r 'ctrl-v 'ctrl-w 'ctrl-u  ; Ctrl combos → symbol
```

### Terminal key-source

```lisp
(de make-term-key-source ()
  (co (lambda (_)
    (while T (yield (read-next-key))))))

(de read-next-key ()
  (let ((b (raw-read-byte)))
    (cond
      ((= b 27)  (decode-esc))
      ((= b 13)  'ret)
      ((= b 127) 'bs)
      ((= b 9)   'tab)
      ((= b 18)  'ctrl-r)    ; Ctrl+R
      ((= b 22)  'ctrl-v)    ; Ctrl+V (visual block)
      ((= b 23)  'ctrl-w)    ; Ctrl+W
      ((= b 21)  'ctrl-u)    ; Ctrl+U (half-page up)
      ((= b 4)   'ctrl-d)    ; Ctrl+D (half-page down)
      ((< b 32)  NIL)        ; other control — ignore
      (T (char b)))))

(de decode-esc ()
  (let ((b (raw-read-byte-timeout 50)))   ; 50ms ESC disambiguation
    (cond
      ((= b -1)  'esc)                    ; lone ESC
      ((= b 91)  (decode-csi))            ; ESC [
      ((= b 79)  (decode-ss3))            ; ESC O
      (T         'esc))))

(de decode-csi ()
  (let ((params "") (b (raw-read-byte)))
    (while (or (and (>= b 48) (<= b 57)) (= b 59))  ; 0-9 and ;
      (setq params (pack (list params (char b))))
      (setq b (raw-read-byte)))
    (cond
      ((= b 65) 'up)    ((= b 66) 'down)
      ((= b 67) 'right) ((= b 68) 'left)
      ((= b 72) 'home)  ((= b 70) 'end)
      ((and (= b 126) (equal params "2")) 'ins)
      ((and (= b 126) (equal params "3")) 'del)
      ((and (= b 126) (equal params "5")) 'pgup)
      ((and (= b 126) (equal params "6")) 'pgdn)
      (T 'unknown))))
```

### Macro-filter coroutine

```lisp
(de make-macro-filter (key-src macros)
  (co (lambda (_)
    (let ((buf NIL) (recording NIL) (rec-reg NIL) (rec-keys NIL))
      (while T
        (let ((key (if buf
                     (let ((k (car buf)))
                       (setq buf (cdr buf)) k)
                     (co-resume key-src NIL))))
          ; record key if recording
          (when (and recording (not (equal key (list 'q rec-reg))))
            (setq rec-keys (append rec-keys (list key))))
          (yield key)))))))
```

Macro injection — when mode-dispatch executes `@a`, it updates state with a request to inject; the filter drains the macro into its buffer before resuming key-src.

### SDL key-source

```lisp
(de make-sdl-key-source ()
  (co (lambda (_)
    (while T
      (let ((ev (sdl-wait-event)))
        (when (equal (ev 'type) 'keydown)
          (yield (sdl-key->vi-key (ev 'sym) (ev 'mod)))))))))
```

### FFI seam (term.l) — C helper: ~50 lines

```c
// term_helper.c
int  set_raw_mode(int on);       // tcsetattr / SetConsoleMode
void get_term_size(int *w, int *h); // TIOCGWINSZ / GetConsoleScreenBufferInfo
int  read_byte_raw(void);        // read(0,&b,1)
int  read_byte_timeout(int ms);  // select()+read() / WaitForSingleObject
```

---

## Section 5 — Display

### ANSI terminal renderer

Pure L — no FFI. All drawing is ANSI escape sequences sent to stdout.

```lisp
(de make-ansi-renderer (cols rows)
  (co (lambda (_)
    (while T
      (let ((state (yield NIL)))
        (render-frame state cols rows))))))

(de render-frame (state cols rows)
  ; 1. Hide cursor
  (prin "\x1b[?25l")
  ; 2. Draw text lines (viewport rows-1 lines)
  (let ((top  (car (s-get state 'scroll)))
        (left (cdr (s-get state 'scroll)))
        (buf  (s-get state 'buffer))
        (row  (car (s-get state 'cursor)))
        (col  (cdr (s-get state 'cursor)))
        (mode (s-get state 'mode)))
    (let ((i 0))
      (while (< i (1- rows))
        (let ((lnum (+ top i 1)))
          (prin (pack (list "\x1b[" (1+ i) ";1H")))   ; position cursor
          (if (> lnum (buf-nlines buf))
            (prin "\x1b[K~")                           ; ~ for lines past EOF
            (render-line (buf-line buf lnum) left cols state lnum)))
        (setq i (1+ i))))
    ; 3. Status line (last row)
    (render-status state cols rows)
    ; 4. Position cursor
    (let ((scr-row (- row (car (s-get state 'scroll))))
          (scr-col (- col (cdr (s-get state 'scroll)))))
      (prin (pack (list "\x1b[" scr-row ";" (1+ scr-col) "H"))))
    ; 5. Show cursor
    (prin "\x1b[?25h")
    (flush)))

(de render-status (state cols rows)
  (prin (pack (list "\x1b[" rows ";1H\x1b[K")))   ; clear status line
  (let ((mode (s-get state 'mode))
        (file (or (s-get state 'filename) "[No Name]"))
        (dirty (if (s-get state 'dirty) "[+]" ""))
        (msg  (s-get state 'msg))
        (row  (car (s-get state 'cursor)))
        (col  (cdr (s-get state 'cursor))))
    (if (equal mode 'command)
      (prin (pack (list ":" (s-get state 'cmd-buf))))
      (progn
        (when (equal mode 'insert)
          (prin "\x1b[1m-- INSERT --\x1b[0m "))
        (when (member mode '(visual-char visual-line visual-block))
          (prin "\x1b[1m-- VISUAL --\x1b[0m "))
        (when msg (prin msg))
        ; right-align row:col
        (let ((pos (pack (list row ":" (1+ col)))))
          (prin (pack (list "\x1b[" rows ";" (- cols (length pos)) "H" pos))))))))
```

Visual selection highlighting uses ANSI reverse video (`\x1b[7m`) on selected characters within `render-line`.

### SDL graphical renderer

```lisp
(de make-sdl-renderer (win font cell-w cell-h)
  (co (lambda (_)
    (while T
      (let ((state (yield NIL)))
        (sdl-render-clear win 0 0 0)
        (render-sdl-lines state win font cell-w cell-h)
        (render-sdl-status state win font cell-w cell-h)
        (sdl-render-present win))))))
```

Each character is drawn with `(sdl-draw-char win font col row ch fg-color bg-color)`. The SDL renderer uses the same viewport/scroll math as the ANSI renderer — only the output primitive differs.

---

## Section 6 — Operations and Motions

### Motion → region

Every motion returns a region `(r1 c1 r2 c2 type)` where type is `'char`, `'line`, or `'block`. Operators apply uniformly to regions.

```lisp
(de motion->region (state key count)
  (let ((row (car (s-get state 'cursor)))
        (col (cdr (s-get state 'cursor))))
    (cond
      ((equal key "w")  (char-region state row col (word-end-forward state count)))
      ((equal key "b")  (char-region (word-start-backward state count) row col))
      ((equal key "$")  (line-region-to-eol state row))
      ((equal key "0")  (char-region state row 0 row col))
      ((equal key "G")  (line-region state row (buf-nlines (s-get state 'buffer))))
      ; ...
      )))
```

### Operator × region

```lisp
(de apply-op (state op region)
  (cond
    ((equal op "d") (delete-region state region T))   ; delete, yank to unnamed
    ((equal op "c") (let ((s (delete-region state region T)))
                      (s-set s 'mode 'insert)))        ; delete + insert
    ((equal op "y") (yank-region state region))        ; yank only
    ((equal op "<") (indent-region state region -1))
    ((equal op ">") (indent-region state region  1))))
```

### Word motion (w/b/e/W/B/E)

Small-word boundary: non-alnum/underscore transition.
WORD boundary: any whitespace transition.

### Complete motion set

| Key | Motion |
|-----|--------|
| h/l | char left/right |
| j/k | line down/up |
| w/W | word/WORD forward |
| b/B | word/WORD backward |
| e/E | word/WORD end forward |
| ge/gE | word/WORD end backward |
| 0/^ | col 0 / first non-blank |
| $ | end of line |
| gg/G | first/last line (G with count = goto line N) |
| H/M/L | screen top/middle/bottom |
| f/F | find char forward/backward on line |
| t/T | till char forward/backward on line |
| ; /, | repeat last f/F/t/T |
| ( ) | sentence backward/forward |
| { } | paragraph backward/forward |
| % | matching bracket |

### Text objects (used after d/c/y in op-pending)

`diw` delete inner word, `da"` delete around quotes, etc.

Text objects are recognised by the motion decoder when `op` is set:
- `iw`/`aw` — inner/around word
- `i"`/`a"`, `i'`/`a'`, `` i` ``/`` a` `` — quotes
- `i(`/`a(`, `i[`/`a[`, `i{`/`a{` — brackets

---

## Section 7 — Search and Command Line

### Search (`/` `?` `n` `N`)

Basic regex engine implemented in pure L — supports: `.` `*` `+` `?` `^` `$` `[...]` `\w` `\d` `\s`. No backreferences needed for vi-level usage.

```lisp
; Returns (match-start match-end) or NIL
(de regex-match (pattern str pos) ...)

; Search forward from (row, col+1), wrap at EOF
(de search-next (state count)
  (let ((pat (s-get state 'search))
        (buf (s-get state 'buffer)))
    ; scan lines starting from cursor+1, wrap
    ...))
```

### Command line (`:` commands)

```lisp
(de exec-cmd (state cmd-str)
  (let ((parts (split-cmd cmd-str)))
    (cond
      ((equal (car parts) "w")  (cmd-write  state parts))
      ((equal (car parts) "q")  (cmd-quit   state parts))
      ((equal (car parts) "wq") (cmd-write-quit state parts))
      ((equal (car parts) "q!") (s-set state 'running NIL))
      ((equal (car parts) "e")  (cmd-edit   state parts))
      ((equal (car parts) "s")  (cmd-subst  state parts))
      ((equal (car parts) "set") (cmd-set   state parts))
      ((number? (car parts))   (goto-line state (car parts)))
      (T (s-set state 'msg (pack (list "Unknown command: " cmd-str)))))))
```

`:s/from/to/[g][i]` — substitution using the regex engine.

---

## Section 8 — FFI Requirements (Minimal)

### term_helper.c (~50 lines, cross-platform)

```c
int  set_raw_mode(int on);       // POSIX: tcsetattr; Windows: SetConsoleMode
void get_term_size(int *w, int *h); // POSIX: TIOCGWINSZ; Windows: GetConsoleScreenBufferInfo
int  read_byte_raw(void);        // read(0, &b, 1)
int  read_byte_timeout(int ms);  // POSIX: select()+read(); Windows: WaitForSingleObject+ReadFile
```

### SDL in gfx.l (uses existing sdl_gfx.c bindings)

New SDL primitives needed (if not already exposed):
- `sdl-wait-event` — blocking event poll
- `sdl-draw-text` — render a string at (x, y) with fg/bg color
- `sdl-load-font` — load a monospaced bitmap or TTF font

### L language gaps this exposes

1. `raw-read-byte` — synchronous byte read from stdin (may already work via read-char)
2. `raw-read-byte-timeout` — timed read, needed for ESC disambiguation
3. `term-size` — terminal dimensions query
4. No built-in regex — implement in pure L (~200 lines)
5. `sub str n` — substring from n to end (need to verify this works without length arg)

---

## Implementation Order

1. `term_helper.c` + `term.l` — raw mode, size, byte read
2. `buffer.l` — pvec buffer with all edit primitives
3. `cursor.l` — movement, clamping, viewport scroll
4. `keys.l` — escape sequence decoder, key-source coroutine
5. `term.l` renderer — ANSI escape output, status line
6. `mode.l` — dispatch coroutine (normal + insert modes only first)
7. `ops.l` — motions and operators (d/c/y/p/x, w/b/e/0/$)
8. `file.l` + `cmd.l` — :w :q :e (usable editor milestone)
9. `search.l` — regex engine + /? search + :s///
10. `undo.l` — u and Ctrl-R
11. `register.l` + `mark.l` — "a–"z registers, ma–mz marks
12. `visual.l` — v/V/Ctrl-V selection + operators on selection
13. `macro.l` — qa/qz record, @a/@z play back
14. `gfx.l` — SDL renderer (shares all logic, swap key-source + renderer)
15. Tests + demo
