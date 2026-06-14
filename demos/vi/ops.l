# demos/vi/ops.l -- word motions, regions, text operators
# Requires: lib/ds.l, lib/string.l, lib/vi/buffer.l, lib/vi/cursor.l, lib/vi/state.l

# ---------------------------------------------------------------------------
# Character classification helpers
# ---------------------------------------------------------------------------

(de space? (ch)
  (or (equal ch " ") (equal ch "\t")))

(de word-char? (ch)
  (not (or (space? ch) (equal ch NIL))))

# ---------------------------------------------------------------------------
# Region constructors and accessors
# ---------------------------------------------------------------------------

(de make-char-region (r1 c1 r2 c2) (list r1 c1 r2 c2 'char))
(de make-line-region (r1 r2)       (list r1 0 r2 0 'line))
(de reg-r1   (reg) (car reg))
(de reg-c1   (reg) (cadr reg))
(de reg-r2   (reg) (caddr reg))
(de reg-c2   (reg) (cadddr reg))
(de reg-type (reg) (car (cddddr reg)))

# ---------------------------------------------------------------------------
# word-end-forward helpers
# word-end-forward: move to the end of the Nth word forward (vi 'e' motion)
# From current pos, skip any trailing non-space chars to find end of word,
# or skip spaces first to land on next word, then advance to its end.
# Result: col of last char of word (0-indexed).
# ---------------------------------------------------------------------------

# Advance col forward while char at col is a space (skip whitespace)
(de wef--skip-space (line len col)
  (if (or (>= col len) (not (space? (sub line col 1))))
    col
    (wef--skip-space line len (1+ col))))

# Advance col forward while char at col is a non-space (find end of word)
(de wef--skip-word (line len col)
  # Returns col of last non-space char (stop before next space or end)
  (if (or (>= (1+ col) len) (space? (sub line (1+ col) 1)))
    col
    (wef--skip-word line len (1+ col))))

# Main loop for word-end-forward: r=row, c=col, n=remaining count
(de wef--loop (buf r c n)
  (if (= n 0)
    (cons r c)
    (let* ((line (buf-line buf r))
           (len  (buf-line-len buf r)))
      (if (>= c len)
        # Past end of line -- advance to next line if possible
        (if (< r (buf-nlines buf))
          (wef--loop buf (1+ r) 0 n)
          (cons r (max 0 (1- len))))
        (let* (# If currently on a non-space and next is non-space, we're mid-word;
               # try to advance to end of current word first (if c < len-1).
               # Otherwise skip space then find word end.
               (c1   (if (and (< c len)
                              (< (1+ c) len)
                              (not (space? (sub line c 1)))
                              (not (space? (sub line (1+ c) 1))))
                       # mid-word: advance to end of current word
                       (wef--skip-word line len c)
                       # else: skip space then find word end
                       (let ((c2 (wef--skip-space line len (1+ c))))
                         (wef--skip-word line len c2)))))
          (wef--loop buf r c1 (1- n)))))))

(de word-end-forward (buf cur count)
  (let* ((r    (car cur))
         (c    (cdr cur))
         (line (buf-line buf r))
         (len  (buf-line-len buf r)))
    # Special case: if at end of line, try next line
    (if (>= c (1- len))
      (if (>= r (buf-nlines buf))
        cur
        (wef--loop buf (1+ r) 0 count))
      # Skip space if currently on space, then advance to word end
      (let* ((c1 (if (space? (sub line c 1))
                   (wef--skip-space line len c)
                   c))
             (c2 (wef--skip-word line len c1)))
        (wef--loop buf r c2 (1- count))))))

# ---------------------------------------------------------------------------
# word-start-forward helpers (vi 'w' motion)
# Move to start of Nth next word.
# ---------------------------------------------------------------------------

# Skip non-space chars forward
(de wsf--skip-word (line len col)
  (if (or (>= col len) (space? (sub line col 1)))
    col
    (wsf--skip-word line len (1+ col))))

# Skip space chars forward
(de wsf--skip-space (line len col)
  (if (or (>= col len) (not (space? (sub line col 1))))
    col
    (wsf--skip-space line len (1+ col))))

(de wsf--loop (buf r c n)
  (if (= n 0)
    (cons r c)
    (let* ((line (buf-line buf r))
           (len  (buf-line-len buf r))
           (c1   (wsf--skip-word line len c))
           (c2   (wsf--skip-space line len c1)))
      (if (< c2 len)
        (wsf--loop buf r c2 (1- n))
        # Wrapped past end of line
        (if (>= r (buf-nlines buf))
          (cons r (max 0 (1- len)))
          (wsf--loop buf (1+ r) 0 (1- n)))))))

(de word-start-forward (buf cur count)
  (wsf--loop buf (car cur) (cdr cur) count))

# ---------------------------------------------------------------------------
# word-start-backward helpers (vi 'b' motion)
# Move to start of Nth previous word.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Paragraph motion: } forward to next blank line, { backward to prev blank.
# Vim's full definition allows configurable paragraph separators; we use
# the simple "blank line == paragraph separator" subset which covers the
# common case (text, code with blank separators).
# ---------------------------------------------------------------------------

(de para--blank-line? (buf row)
  (or (< row 1) (> row (buf-nlines buf))
      (= 0 (buf-line-len buf row))))

(de para-forward (buf row count)
  # Move down until we hit a blank line.  Count repeats the search;
  # each repeat starts from the line AFTER the previous landing.
  # If no blank line found, land on the last line.
  (let ((r (1+ row)) (left (max 1 count)) (last-hit row) (nlines (buf-nlines buf)))
    (while (and (<= r nlines) (> left 0))
      (when (para--blank-line? buf r)
        (setq last-hit r)
        (setq left (1- left)))
      (setq r (1+ r)))
    (if (= left 0) last-hit nlines)))

(de para-backward (buf row count)
  (let ((r (1- row)) (left (max 1 count)) (last-hit row))
    (while (and (>= r 1) (> left 0))
      (when (para--blank-line? buf r)
        (setq last-hit r)
        (setq left (1- left)))
      (setq r (1- r)))
    (if (= left 0) last-hit 1)))

# ---------------------------------------------------------------------------
# %: jump to matching bracket.  Looks at the character under the cursor;
# if it's one of ()[]{}, find its pair (nested, possibly multi-line).
# ---------------------------------------------------------------------------

(de match--open?  (ch) (or (equal ch "(") (equal ch "[") (equal ch "{")))
(de match--close? (ch) (or (equal ch ")") (equal ch "]") (equal ch "}")))
(de match--pair (ch)
  (cond
    ((equal ch "(") ")")  ((equal ch ")") "(")
    ((equal ch "[") "]")  ((equal ch "]") "[")
    ((equal ch "{") "}")  ((equal ch "}") "{")
    (T NIL)))

(de match--scan-fwd (buf row col target-open target-close depth)
  # Walk forward from (row, col+1), tracking nesting; return (r . c) on match.
  (let ((r row) (c (1+ col)) (nlines (buf-nlines buf)) (result NIL))
    (while (and (<= r nlines) (null result))
      (let ((line (buf-line buf r))
            (len  (buf-line-len buf r)))
        (while (and (< c len) (null result))
          (let ((ch (sub line c 1)))
            (cond
              ((equal ch target-open)  (setq depth (1+ depth)))
              ((equal ch target-close)
               (if (= depth 0) (setq result (cons r c))
                 (setq depth (1- depth))))))
          (setq c (1+ c)))
        (setq r (1+ r))
        (setq c 0)))
    result))

(de match--scan-bwd (buf row col target-close target-open depth)
  # Walk backward from (row, col-1).  target-close is the char we'd find
  # nested inside; target-open is the one we're searching for.
  (let ((r row) (c (1- col)) (result NIL))
    (while (and (>= r 1) (null result))
      (let ((line (buf-line buf r))
            (len  (buf-line-len buf r)))
        (when (< c 0) (setq c (1- len)))
        (while (and (>= c 0) (null result))
          (let ((ch (if (< c len) (sub line c 1) "")))
            (cond
              ((equal ch target-close) (setq depth (1+ depth)))
              ((equal ch target-open)
               (if (= depth 0) (setq result (cons r c))
                 (setq depth (1- depth))))))
          (setq c (1- c)))
        (setq r (1- r))))
    result))

(de match-bracket (buf row col)
  (let* ((line (buf-line buf row))
         (len  (buf-line-len buf row))
         (ch   (if (and (>= col 0) (< col len)) (sub line col 1) NIL)))
    (cond
      ((null ch) NIL)
      ((match--open? ch)
       (match--scan-fwd buf row col ch (match--pair ch) 0))
      ((match--close? ch)
       (match--scan-bwd buf row col ch (match--pair ch) 0))
      (T NIL))))

# Skip non-space chars backward (from col, going left)
(de wsb--skip-word-back (line col)
  (if (or (< col 0) (space? (sub line col 1)))
    col
    (wsb--skip-word-back line (1- col))))

# Skip space chars backward
(de wsb--skip-space-back (line col)
  (if (or (< col 0) (not (space? (sub line col 1))))
    col
    (wsb--skip-space-back line (1- col))))

(de wsb--loop (buf r c n)
  (if (= n 0)
    (cons r c)
    (let* ((line (buf-line buf r))
           (c1   (1- c))
           # skip any space to find end of previous word
           (c2   (wsb--skip-space-back line c1))
           # skip word chars back to find start of word
           (c3   (wsb--skip-word-back line c2))
           (c4   (1+ c3)))  # c3 is first space or -1, so word starts at c3+1
      (if (>= c4 0)
        (wsb--loop buf r c4 (1- n))
        # Moved to previous line
        (if (<= r 1)
          (cons r 0)
          (let* ((pr   (1- r))
                 (plen (buf-line-len buf pr)))
            (wsb--loop buf pr (max 0 (1- plen)) (1- n))))))))

(de word-start-backward (buf cur count)
  (wsb--loop buf (car cur) (cdr cur) count))

# ---------------------------------------------------------------------------
# yank helpers for delete-region / yank-region (line mode)
# ---------------------------------------------------------------------------

# Collect lines r1..r2 into a list
(de ops--collect-lines (buf r1 r2 acc)
  (if (> r1 r2)
    (reverse acc)
    (ops--collect-lines buf (1+ r1) r2 (cons (buf-line buf r1) acc))))

# Delete lines r1..r2 from buf (r1 <= r2)
(de ops--delete-lines (buf r1 r2)
  (if (> r1 r2)
    buf
    (ops--delete-lines (buf-del-line buf r1) r1 (1- r2))))

# ---------------------------------------------------------------------------
# delete-region
# region = (list r1 c1 r2 c2 kind)  where kind is 'char or 'line
# yank? = T means store in unnamed-reg
# Returns new state.
# ---------------------------------------------------------------------------

(de delete-region (state region yank?)
  (let* ((r1   (car (nth 0 region)))
         (c1   (car (nth 1 region)))
         (r2   (car (nth 2 region)))
         (c2   (car (nth 3 region)))
         (kind (car (nth 4 region)))
         (buf  (s-get state 'buffer))
         (st1  (undo-save state)))
    (s-set
      (if (equal kind 'line)
        # Line-wise deletion: delete rows r1..r2.  Append a trailing \n so
        # put-after/put-before can recognise line-mode even for a single line.
        (let* ((yanked (if yank? (ops--collect-lines buf r1 r2 NIL) NIL))
               (ystr   (if yanked (pack (list (join yanked "\n") "\n")) ""))
               (nbuf   (ops--delete-lines buf r1 r2))
               (nlines (buf-nlines nbuf))
               (newr   (min r1 nlines))
               (newc   0)
               (st2    (s-set st1 'buffer nbuf))
               (st3    (s-set st2 'cursor (cons newr newc))))
          (if yank? (reg--write st3 ystr NIL) st3))
        # Char-wise deletion
        (let* ((line  (buf-line buf r1))
               (len   (buf-line-len buf r1))
               # c2 is inclusive end col; clamp to len-1
               (ec    (min c2 (1- len)))
               (yanked (if yank? (sub line c1 (- (1+ ec) c1)) ""))
               # Delete chars c1..ec inclusive: keep prefix + suffix
               (newline (pack (list (sub line 0 c1) (sub line (1+ ec)))))
               (nbuf   (buf-set-line buf r1 newline))
               (newlen (length (unpack newline)))
               (newc   (min c1 (max 0 (1- newlen))))
               (st2    (s-set st1 'buffer nbuf))
               (st3    (s-set st2 'cursor (cons r1 newc))))
          (if yank? (reg--write st3 yanked NIL) st3)))
      'active-reg NIL)))

# ---------------------------------------------------------------------------
# yank-region -- like delete-region but no modification
# ---------------------------------------------------------------------------

# Write `text` to whichever register the user has selected: the named
# register at state.active-reg (a-z) if non-NIL, otherwise the
# unnamed register.  Always also writes the unnamed register, matching
# vim's behaviour of mirroring named-yanks into "".
# For named-register DELETES vim DOESN'T mirror to "" -- callers
# can pass mirror?=NIL to skip that.
(de reg--write (state text mirror?)
  (let ((ar (s-get state 'active-reg)))
    (cond
      ((and ar (>= ar 0))
       (let* ((regs  (s-get state 'registers))
              (regs2 (pvec-put regs ar text))
              (st1   (s-set state 'registers regs2)))
         (if mirror? (s-set st1 'unnamed-reg text) st1)))
      (T (s-set state 'unnamed-reg text)))))

# Read the active register, falling back to the unnamed one.
(de reg--read (state)
  (let ((ar (s-get state 'active-reg)))
    (if (and ar (>= ar 0))
      (pvec-get (s-get state 'registers) ar)
      (s-get state 'unnamed-reg))))

(de yank-region (state region)
  (let* ((r1   (car (nth 0 region)))
         (c1   (car (nth 1 region)))
         (r2   (car (nth 2 region)))
         (c2   (car (nth 3 region)))
         (kind (car (nth 4 region)))
         (buf  (s-get state 'buffer)))
    (s-set
      (if (equal kind 'line)
        # Line-mode yank: tag with trailing \n so put can recognise it
        # even for a single yanked line (where join produces no separator).
        (let* ((lines (ops--collect-lines buf r1 r2 NIL))
               (ystr  (pack (list (join lines "\n") "\n"))))
          (reg--write state ystr T))
        (let* ((line   (buf-line buf r1))
               (len    (buf-line-len buf r1))
               (ec     (min c2 (1- len)))
               (yanked (sub line c1 (- (1+ ec) c1))))
          (reg--write state yanked T)))
      'active-reg NIL)))

# ---------------------------------------------------------------------------
# put-after (vi 'p')
# count = number of times to put
# Char-wise: insert reg text after cursor col
# Line-wise: insert reg text as new lines after cursor row
# ---------------------------------------------------------------------------

# Insert text str into buf at row r after col c (char-wise)
(de ops--put-char-after (buf r c text)
  (let* ((line (buf-line buf r))
         (pre  (sub line 0 (1+ c)))
         (post (sub line (1+ c)))
         (newline (pack (list pre text post))))
    (buf-set-line buf r newline)))

# Insert lines from a list after row r (line-wise put-after)
# Fix: thread the insertion row so multi-line paste preserves order
(de ops--insert-lines-after (buf r lines)
  (if (null lines)
    buf
    (let ((nb (buf-ins-line buf (1+ r) (car lines))))
      (ops--insert-lines-after nb (1+ r) (cdr lines)))))

(de put-after--char-loop (st1 buf r c reg count i)
  (if (= i count)
    (let* ((newlen (buf-line-len buf r))
           (newc   (if (> newlen 0) (min (+ c (length (unpack reg))) (1- newlen)) 0))
           (st2    (s-set st1 'buffer buf)))
      (s-set st2 'cursor (cons r newc)))
    (put-after--char-loop st1 (ops--put-char-after buf r c reg) r c reg count (1+ i))))

(de reg--line-mode? (reg)
  # Line-mode is encoded by a trailing newline (single-line yy still
  # contains \n at the end; a char-mode yank never does).
  (let ((len (length (unpack reg))))
    (and (> len 0) (equal (sub reg (1- len)) "\n"))))

(de reg--line-content (reg)
  # Strip the line-mode trailing \n marker and split into a list of lines.
  (let ((len (length (unpack reg))))
    (split (sub reg 0 (1- len)) "\n")))

(de put-after (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (r    (car cur))
         (c    (cdr cur))
         (reg  (reg--read state))
         (st1  (s-set (undo-save state) 'active-reg NIL))
         (n    (max 1 count)))
    (cond
      ((or (null reg) (= 0 (length (unpack reg)))) st1)
      ((reg--line-mode? reg)
       # Line-wise: insert the line(s) as new rows BELOW the cursor row,
       # count times.  Cursor lands at the first line of the first copy.
       (let* ((lines (reg--line-content reg))
              (nb    (let ((b buf) (i 0))
                       (while (< i n)
                         (setq b (ops--insert-lines-after b
                                   (+ r (* i (length lines))) lines))
                         (setq i (1+ i)))
                       b))
              (st2   (s-set st1 'buffer nb)))
         (s-set st2 'cursor (cons (1+ r) 0))))
      (T
       # Char-wise: insert reg after col c, count times
       (put-after--char-loop st1 buf r c reg n 0)))))

# ---------------------------------------------------------------------------
# put-before (vi 'P')
# ---------------------------------------------------------------------------

(de ops--insert-lines-before (buf r lines)
  (if (null lines)
    buf
    (let ((nb (buf-ins-line buf r (car lines))))
      (ops--insert-lines-before nb (1+ r) (cdr lines)))))

(de put-before (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (r    (car cur))
         (c    (cdr cur))
         (reg  (reg--read state))
         (st1  (s-set (undo-save state) 'active-reg NIL))
         (n    (max 1 count)))
    (cond
      ((or (null reg) (= 0 (length (unpack reg)))) st1)
      ((reg--line-mode? reg)
       (let* ((lines (reg--line-content reg))
              (nb    (let ((b buf) (i 0))
                       (while (< i n)
                         (setq b (ops--insert-lines-before b r lines))
                         (setq i (1+ i)))
                       b))
              (st2   (s-set st1 'buffer nb)))
         (s-set st2 'cursor (cons r 0))))
      (T
       # Char-wise count: insert reg N times before the cursor on the row.
       (let* ((rep (let ((acc NIL) (i 0))
                     (while (< i n) (setq acc (cons reg acc)) (setq i (1+ i)))
                     (pack acc)))
              (line    (buf-line buf r))
              (newline (pack (list (sub line 0 c) rep (sub line c))))
              (nb      (buf-set-line buf r newline))
              (newc    (+ c (length (unpack rep))))
              (st2     (s-set st1 'buffer nb)))
         (s-set st2 'cursor (cons r (max 0 (1- newc)))))))))

# ---------------------------------------------------------------------------
# delete-char-at-cursor (vi 'x')
# ---------------------------------------------------------------------------

(de dcc--loop (buf r c n)
  (if (= n 0)
    buf
    (let* ((len (buf-line-len buf r)))
      (if (< c len)
        (dcc--loop (buf-del-char buf r c) r c (1- n))
        buf))))

(de delete-char-at-cursor (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (r    (car cur))
         (c    (cdr cur))
         (st1  (undo-save state))
         (nb   (dcc--loop buf r c count))
         (nlen (buf-line-len nb r))
         (newc (if (> nlen 0) (min c (1- nlen)) 0))
         (st2  (s-set st1 'buffer nb)))
    (s-set st2 'cursor (cons r newc))))

# ---------------------------------------------------------------------------
# join-lines-cmd (vi 'J')
# ---------------------------------------------------------------------------

(de jl--loop (buf r n)
  (if (= n 0)
    buf
    (jl--loop (buf-join-lines buf r) r (1- n))))

(de join-lines-cmd (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (r    (car cur))
         (n    (max 1 (1- count)))
         (st1  (undo-save state))
         (nb   (jl--loop buf r n))
         (nlen (buf-line-len nb r))
         (newc (max 0 (1- nlen)))
         (st2  (s-set st1 'buffer nb)))
    (s-set st2 'cursor (cons r newc))))

# ---------------------------------------------------------------------------
# toggle-case (vi '~')
# ---------------------------------------------------------------------------

(de ops--toggle-char-case (ch)
  # ch is a 1-char string
  (let ((code (char ch)))
    (cond
      ((and (>= code 65) (<= code 90))  (char (+ code 32)))  # A-Z -> a-z
      ((and (>= code 97) (<= code 122)) (char (- code 32)))  # a-z -> A-Z
      (T ch))))

(de ops--toggle-case-at (buf r c)
  (let* ((line (buf-line buf r))
         (len  (buf-line-len buf r)))
    (if (< c len)
      (let* ((ch      (sub line c 1))
             (newch   (ops--toggle-char-case ch))
             (newline (pack (list (sub line 0 c) newch (sub line (1+ c))))))
        (buf-set-line buf r newline))
      buf)))

# Fix: use cc (accumulated column) for final cursor position, not (1+ c0)
(de tc--loop (st1 r c0 buf cc n)
  (if (= n 0)
    (let* ((st2 (s-set st1 'buffer buf))
           (nlen (buf-line-len buf r))
           (newc (min cc (max 0 (1- nlen)))))
      (s-set st2 'cursor (cons r newc)))
    (tc--loop st1 r c0 (ops--toggle-case-at buf r cc) (1+ cc) (1- n))))

(de toggle-case (state count)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (r   (car cur))
         (c   (cdr cur))
         (st1 (undo-save state)))
    (tc--loop st1 r c buf c count)))

# ---------------------------------------------------------------------------
# indent-region (>> / <<)
# reg is a region object; delta is +1 (indent) or -1 (dedent)
# Uses 4-space indentation for indent, removes "    " or "\t" for dedent
# ---------------------------------------------------------------------------

(de indent--loop (buf r r2 delta)
  (if (> r r2) buf
    (let ((line (buf-line buf r)))
      (indent--loop
        (buf-set-line buf r
          (if (> delta 0)
            (pack (list "    " line))
            (cond
              ((starts-with? line "    ") (sub line 4))
              ((starts-with? line "\t")   (sub line 1))
              (T line))))
        (1+ r) r2 delta))))

(de indent-region (state reg delta)
  (let* ((buf (s-get state 'buffer))
         (r1  (reg-r1 reg)) (r2 (reg-r2 reg))
         (st2 (undo-save state))
         (nb  (indent--loop buf r1 r2 delta)))
    (s-set (s-set st2 'buffer nb) 'cursor (cursor-clamp nb (s-get st2 'cursor)))))

# ---------------------------------------------------------------------------
# Motion dispatch helpers
# ---------------------------------------------------------------------------

(de repeat-last (state) state)   # placeholder

# Key string for vi '^' motion (caret = ASCII 94).
# Cannot use the literal "^" in source because PicoLisp treats ^ as
# an escape prefix inside string literals.
(de vi-caret-key () (char 94))

(de motion-key? (key)
  (or (member key '("h" "l" "j" "k" "w" "W" "b" "B" "e" "E"
                    "0" "$" "G" "H" "M" "L" "%" "(" ")" "{" "}"))
      (equal key (vi-caret-key))))

(de motion->cur (buf cur key count)
  (cond
    ((equal key "h")  (cur-move-h buf cur (- count)))
    ((equal key "l")  (cur-move-h buf cur count))
    ((equal key "w")  (word-start-forward  buf cur count))
    ((equal key "b")  (word-start-backward buf cur count))
    ((equal key "e")  (word-end-forward    buf cur count))
    # W/B/E use whitespace-only boundaries -- which is what this codebase's
    # w/b/e already do, so they share the same impl.  (Real vim distinguishes
    # word=alnum-aware and WORD=whitespace-only; that finer split is left
    # for a future improvement.)
    ((equal key "W")  (word-start-forward  buf cur count))
    ((equal key "B")  (word-start-backward buf cur count))
    ((equal key "E")  (word-end-forward    buf cur count))
    ((equal key "j")  (cursor-clamp buf (cons (+ (car cur) count) (cdr cur))))
    ((equal key "k")  (cursor-clamp buf (cons (- (car cur) count) (cdr cur))))
    ((equal key "$")  (cons (car cur) (max 0 (1- (buf-line-len buf (car cur))))))
    ((equal key "0")  (cons (car cur) 0))
    ((equal key (vi-caret-key)) (cons (car cur) (cur-first-nonblank buf (car cur))))
    ((equal key "G")  (cursor-clamp buf (cons (buf-nlines buf) (cdr cur))))
    # H/M/L: viewport top/middle/bottom -- overridden by exec-motion which
    # has access to the scroll state.  The simple buffer-line fallbacks
    # here are used only if a caller bypasses exec-motion.
    ((equal key "H")  (cursor-clamp buf (cons 1 0)))
    ((equal key "M")  (cursor-clamp buf (cons (/ (buf-nlines buf) 2) 0)))
    ((equal key "L")  (cursor-clamp buf (cons (buf-nlines buf) 0)))
    # Paragraph motions: blank-line as separator.
    ((equal key "}")  (cons (para-forward  buf (car cur) count) 0))
    ((equal key "{")  (cons (para-backward buf (car cur) count) 0))
    # %: matching bracket on a paren/bracket/brace character.
    ((equal key "%")
     (let ((m (match-bracket buf (car cur) (cdr cur))))
       (if m m cur)))
    (T cur)))

(de motion->region (state key count)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur)))
    (cond
      ((or (equal key "w") (equal key "W"))
       (let ((e (word-start-forward buf cur count)))
         (make-char-region row col (car e) (max 0 (1- (cdr e))))))
      ((or (equal key "b") (equal key "B"))
       (let ((e (word-start-backward buf cur count)))
         (make-char-region (car e) (cdr e) row (max 0 (1- col)))))
      ((or (equal key "e") (equal key "E"))
       (let ((e (word-end-forward buf cur count)))
         (make-char-region row col (car e) (cdr e))))
      ((equal key "$")
       (make-char-region row col row (max 0 (1- (buf-line-len buf row)))))
      ((member key '("j" "k"))
       (let ((t2 (if (equal key "j") (+ row count) (- row count))))
         (make-line-region (min row t2) (min (max row t2) (buf-nlines buf)))))
      ((equal key "G")
       (make-line-region row (buf-nlines buf)))
      (T (make-char-region row col row col)))))

(de apply-op (state op reg)
  (cond
    ((equal op "d") (delete-region state reg T))
    ((equal op "c") (s-set (delete-region state reg T) 'mode 'insert))
    ((equal op "y") (yank-region state reg))
    ((equal op "<") (indent-region state reg -1))
    ((equal op ">") (indent-region state reg  1))
    (T state)))

# ───────────────────────────────────────────────────────────────────────────
# Text objects -- diw / ci" / da{ / etc.  Each returns a region or NIL.
# ───────────────────────────────────────────────────────────────────────────

# Word: contiguous non-whitespace centered on cursor.
# Inner: just the word; Around: include trailing whitespace if present,
# else leading whitespace.
(de tobj-word (buf row col kind)
  (let ((bd (word-bounds-at-cursor buf row col)))
    (if (null bd) NIL
      (let* ((s (car bd)) (e (cdr bd))   # word at [s, e)
             (line (buf-line buf row))
             (len  (buf-line-len buf row))
             (s2 s) (e2 e))
        (when (equal kind "a")
          # Try extending right over trailing whitespace
          (let ((k e))
            (while (and (< k len)
                        (or (equal (sub line k 1) " ")
                            (equal (sub line k 1) "\t")))
              (setq k (1+ k)))
            (if (> k e)
              (setq e2 k)
              # No trailing ws: include leading instead
              (let ((k2 s))
                (while (and (> k2 0)
                            (or (equal (sub line (1- k2) 1) " ")
                                (equal (sub line (1- k2) 1) "\t")))
                  (setq k2 (1- k2)))
                (setq s2 k2)))))
        (make-char-region row s2 row (max s2 (1- e2)))))))

# Paired delimiter (single-line): find the nearest enclosing `open`..`close`
# pair such that cursor is between them (or on them).  Returns NIL if no pair.
(de tobj--find-pair (line col open close)
  # Scan left from col for `open`, then right of cursor for the matching close.
  # This is the simple "single-line, no nesting" case; sufficient for quotes
  # and for paren/bracket/brace pairs on one line.
  (let ((s -1) (e -1) (len (length (unpack line))))
    # If cursor is ON an open, take it as start; if on close, scan left.
    (when (and (>= col 0) (< col len))
      (let ((ch (sub line col 1)))
        (cond
          ((equal ch open)  (setq s col))
          ((equal ch close) (setq e col)))))
    # Scan left for open if not yet found
    (when (= s -1)
      (let ((k (if (= e -1) (1- col) (1- e))))
        (while (and (>= k 0) (= s -1))
          (when (equal (sub line k 1) open) (setq s k))
          (setq k (1- k)))))
    # Scan right for close if not yet found
    (when (and (>= s 0) (= e -1))
      (let ((k (1+ s)))
        (while (and (< k len) (= e -1))
          (when (equal (sub line k 1) close) (setq e k))
          (setq k (1+ k)))))
    (if (and (>= s 0) (>= e 0)) (cons s e) NIL)))

# Quote object: same delim for open and close.  Picks the first complete
# pair on the line whose interior contains the cursor (or whose delim
# the cursor sits on).
(de tobj--find-quote (line col q)
  (let ((len (length (unpack line))) (pairs NIL))
    # Collect pair indices [a, b]
    (let ((k 0) (open NIL))
      (while (< k len)
        (when (equal (sub line k 1) q)
          (if (null open)
            (setq open k)
            (prog (setq pairs (cons (cons open k) pairs))
                  (setq open NIL))))
        (setq k (1+ k))))
    # Find the pair whose [a, b] contains col (inclusive)
    (let ((found NIL) (ps (reverse pairs)))
      (while (and ps (null found))
        (let ((p (car ps)))
          (when (and (<= (car p) col) (<= col (cdr p)))
            (setq found p)))
        (setq ps (cdr ps)))
      found)))

(de tobj-paired (buf row col kind open close)
  (let* ((line (buf-line buf row))
         (p (tobj--find-pair line col open close)))
    (if (null p) NIL
      (let ((s (car p)) (e (cdr p)))
        (cond
          ((equal kind "i")
           (if (< (1+ s) e)
             (make-char-region row (1+ s) row (1- e))
             NIL))
          (T   # "a"
           (make-char-region row s row e)))))))

(de tobj-quote (buf row col kind q)
  (let* ((line (buf-line buf row))
         (p (tobj--find-quote line col q)))
    (if (null p) NIL
      (let ((s (car p)) (e (cdr p)))
        (cond
          ((equal kind "i")
           (if (< (1+ s) e)
             (make-char-region row (1+ s) row (1- e))
             NIL))
          (T
           (make-char-region row s row e)))))))

# Paragraph: rows of contiguous non-blank, optionally with one trailing
# blank for the "a" variant.
(de tobj-paragraph (buf row kind)
  (if (para--blank-line? buf row)
    NIL
    (let ((r1 row) (r2 row) (nlines (buf-nlines buf)))
      (while (and (> r1 1) (not (para--blank-line? buf (1- r1))))
        (setq r1 (1- r1)))
      (while (and (< r2 nlines) (not (para--blank-line? buf (1+ r2))))
        (setq r2 (1+ r2)))
      (when (equal kind "a")
        (when (and (< r2 nlines) (para--blank-line? buf (1+ r2)))
          (setq r2 (1+ r2))))
      (make-line-region r1 r2))))

# Dispatch text-object: build a region, then apply the operator.
# `kind` is "i" or "a"; `key` is the object key (w, ", ', (, ), [, ],
# {, }, p, etc.).  No-op if the object isn't found.
(de text-object (state kind key count op)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur)) (col (cdr cur))
         (reg
           (cond
             ((equal key "w") (tobj-word      buf row col kind))
             ((equal key "W") (tobj-word      buf row col kind))   # same impl
             ((or (equal key "(") (equal key ")") (equal key "b"))
              (tobj-paired buf row col kind "(" ")"))
             ((or (equal key "[") (equal key "]"))
              (tobj-paired buf row col kind "[" "]"))
             ((or (equal key "{") (equal key "}") (equal key "B"))
              (tobj-paired buf row col kind "{" "}"))
             ((equal key "\"") (tobj-quote buf row col kind "\""))
             ((equal key "'")  (tobj-quote buf row col kind "'"))
             ((equal key "p")  (tobj-paragraph buf row kind))
             (T NIL))))
    (if (null reg) state
      (apply-op (undo-save state) op reg))))

(de exec-linewise-op (state op count)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (row (car cur))
         (reg (make-line-region row (min (+ row count -1) (buf-nlines buf)))))
    (apply-op (undo-save state) op reg)))

# Update scroll so the current cursor is inside the visible viewport.
# Call after any operation that may have moved the cursor without
# updating scroll (insert, undo/redo, search, : line jump, etc.).
(de ensure-cursor-visible (state)
  (let* ((buf   (s-get state 'buffer))
         (cur   (s-get state 'cursor))
         (sz    (s-get state 'scroll))
         (vcols (or *vi-cols* 80))
         (vrows (1- (or *vi-rows* 24))))
    (s-set state 'scroll (viewport-ensure buf cur sz vcols vrows))))

# For H / M / L, compute the target row from the visible viewport
# (scroll top + text-rows), clamped to the buffer.
(de motion--viewport-target (buf top text-rows key)
  (let* ((nlines (buf-nlines buf))
         (bot    (min nlines (+ top text-rows -1))))
    (cond
      ((equal key "H") top)
      ((equal key "L") bot)
      ((equal key "M") (/ (+ top bot) 2))
      (T top))))

(de exec-motion (state key count op)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (sz  (s-get state 'scroll))
         (vcols (or *vi-cols* 80))
         # viewport-ensure wants text-row count (screen height minus status bar)
         (vrows (1- (or *vi-rows* 24))))
    (if op
      (let* ((reg (motion->region state key count))
             (st2 (apply-op (undo-save state) op reg)))
        (s-set st2 'scroll (viewport-ensure buf (s-get st2 'cursor) sz vcols vrows)))
      (let* ((nc (if (member key '("H" "M" "L"))
                   (cursor-clamp buf
                     (cons (motion--viewport-target buf (car sz) vrows key)
                           (cdr cur)))
                   (motion->cur buf cur key count)))
             (sz2 (viewport-ensure buf nc sz vcols vrows)))
        (s-set (s-set state 'cursor nc) 'scroll sz2)))))
