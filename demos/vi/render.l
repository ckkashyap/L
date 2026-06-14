# demos/vi/render.l -- ANSI terminal renderer (pure L, no FFI)
# Requires: buffer.l, cursor.l, state.l

(de ansi-goto (row col)   (pack (list (char 27) "[" (str row) ";" (str col) "H")))
(de ansi-clear-line ()    (pack (list (char 27) "[K")))
(de ansi-cursor-hide ()   (pack (list (char 27) "[?25l")))
(de ansi-cursor-show ()   (pack (list (char 27) "[?25h")))
(de ansi-clear-screen ()  (pack (list (char 27) "[2J" (char 27) "[H")))
(de ansi-bold ()          (pack (list (char 27) "[1m")))
(de ansi-reset ()         (pack (list (char 27) "[0m")))
(de ansi-reverse ()       (pack (list (char 27) "[7m")))

(de render--visible-chars (chars i start end acc)
  # Collect chars in range [start, end) into acc (reverse order), then pack.
  # Replaces the named-let loop from the original plan.
  (if (or (null chars) (>= i end))
    (if acc (pack (reverse acc)) "")
    (render--visible-chars (cdr chars) (1+ i) start end
      (if (>= i start) (cons (car chars) acc) acc))))

(de render-line (line left cols)
  # Return substring of line visible in viewport [left, left+cols)
  (let* ((chars (unpack line))
         (len   (length chars))
         (start (min left len))
         (end   (min (+ left cols) len)))
    (render--visible-chars chars 0 start end NIL)))

# Shared: compute the status-bar content as a (left-text . bold?) cons,
# leaving the trailing position string up to the backend (each one knows
# its own coordinate system for right-aligning).  Returns ("" . NIL) if
# there is nothing to show on the left side.
#
# Normal mode falls back to the filename + a [+] modified marker when
# the buffer is dirty, matching vim's default statusline.
(de view-status-left (state)
  (let ((mode (s-get state 'mode))
        (msg  (s-get state 'msg)))
    (cond
      ((equal mode 'command)
       (cons (pack (list ":" (or (s-get state 'cmd-buf) ""))) NIL))
      ((equal mode 'insert)
       (cons "-- INSERT --" T))
      ((member mode '(visual-char visual-line visual-block))
       (cons "-- VISUAL --" T))
      ((and msg (not (equal msg "")))
       (cons msg NIL))
      (T
       (let ((file  (or (s-get state 'filename) ""))
             (dirty (s-get state 'dirty)))
         (cons (if dirty (pack (list file " [+]")) file)
               NIL))))))

# Shared: row:col indicator for the right end of the status bar.
(de view-status-right (state)
  (let ((row  (car (s-get state 'cursor)))
        (col  (cdr (s-get state 'cursor))))
    (pack (list (str row) ":" (str (1+ col))))))

(de render-status (state cols rows)
  (prin (ansi-goto rows 1))
  (prin (ansi-clear-line))
  (let ((left (view-status-left state)))
    (if (cdr left)   # bold?
      (prin (pack (list (ansi-bold) (car left) (ansi-reset))))
      (prin (car left))))
  (let ((pos (view-status-right state)))
    (prin (pack (list (ansi-goto rows (max 1 (- cols (length (unpack pos))))) pos)))))

# Compute the string that should appear on screen row i (0-based) given
# scroll top/left and viewport cols.  Returns "~" beyond buffer end,
# matching vi convention.
(de render--row-text (buf top left cols i)
  (let ((lnum (+ top i)))
    (if (or (< lnum 1) (> lnum (buf-nlines buf)))
      "~"
      (render-line (buf-line buf lnum) left cols))))

# Build a list of strings: the visible content for rows 0..text-rows-1.
# Used in nowrap mode -- one buffer line per screen row.
(de render--collect (buf top left cols text-rows i acc)
  (if (>= i text-rows) (reverse acc)
    (render--collect buf top left cols text-rows (1+ i)
      (cons (render--row-text buf top left cols i) acc))))

# Wrap mode: each buffer line that exceeds cols is broken into multiple
# screen rows.  Returns (rows . line-map) where rows is a list of
# text-rows strings and line-map is a parallel list of (lnum . sub-row),
# used by the cursor positioner so it can find the on-screen position
# of (crow . ccol).  sub-row is 0 for the first chunk of a line, 1 for
# the second, etc.  Pads with ("~" . NIL) once the buffer is exhausted.
(de render--collect-wrap (buf top cols text-rows)
  (let ((rows NIL) (lmap NIL) (count 0)
        (lnum top) (nlines (buf-nlines buf)))
    (while (< count text-rows)
      (cond
        ((or (< lnum 1) (> lnum nlines))
         (setq rows (cons "~" rows))
         (setq lmap (cons NIL lmap))
         (setq count (1+ count))
         (setq lnum (1+ lnum)))
        (T
         (let* ((line  (buf-line buf lnum))
                (chars (unpack line))
                (len   (length chars)))
           (if (= len 0)
             (prog
               (setq rows (cons "" rows))
               (setq lmap (cons (cons lnum 0) lmap))
               (setq count (1+ count)))
             (let ((offset 0) (sub 0))
               (while (and (< offset len) (< count text-rows))
                 (let* ((end   (min len (+ offset cols)))
                        (chunk (render--visible-chars chars 0 offset end NIL)))
                   (setq rows (cons chunk rows))
                   (setq lmap (cons (cons lnum sub) lmap))
                   (setq count (1+ count))
                   (setq offset end)
                   (setq sub (1+ sub))))))
           (setq lnum (1+ lnum))))))
    (cons (reverse rows) (reverse lmap))))

# Find the screen row (0-based) and column (0-based) for cursor at
# buffer (crow . ccol) given the line-map produced by render--collect-wrap.
# Returns (scr-row . scr-col) or NIL if cursor is off-screen.
(de wrap--cursor-pos (lmap crow ccol cols)
  (let ((i 0) (m lmap) (target-sub (/ ccol cols)) (col-in-sub (- ccol (* (/ ccol cols) cols)))
        (result NIL))
    (while (and m (null result))
      (let ((entry (car m)))
        (when (and entry
                   (= (car entry) crow)
                   (= (cdr entry) target-sub))
          (setq result (cons i col-in-sub)))
        (setq i (1+ i))
        (setq m (cdr m))))
    result))

# Width of the line-number gutter (right-aligned digits + trailing space)
# for the visible range, or 0 when *vi-number* is off.
(de view-gutter-width (buf top text-rows)
  (if (not *vi-number*) 0
    (let ((maxn (min (buf-nlines buf) (max 1 (+ top text-rows -1)))))
      (+ 1 (length (unpack (str maxn)))))))

# Right-pad/right-align a string to width chars.
(de view--rpad-num (n width)
  (let* ((s   (str n))
         (slen (length (unpack s)))
         (need (max 0 (- width 1 slen))))
    (pack (list (pack (need-spaces need)) s " "))))

(de need-spaces (n)
  (if (= n 0) NIL (cons " " (need-spaces (1- n)))))

# Prepend a line-number gutter to each row.  rows is the list of row
# content strings; lmap is the (lnum . sub-row) list (NIL in nowrap
# mode, in which case the buffer row is top + screen-row).  Continuation
# rows (sub-row > 0) get a blank gutter so wrapped chunks stay aligned.
# Returns the new row list; never affects the input.
(de view-add-gutter (rows lmap top width)
  (if (= width 0) rows
    (let ((acc NIL) (i 0) (rs rows) (lm lmap))
      (while rs
        (let* ((entry (if lm (car lm) NIL))
               (lnum  (cond
                        (lm (if entry (car entry) NIL))
                        (T  (+ top i))))
               (sub   (if lm (if entry (cdr entry) 0) 0))
               (pad   (pack (need-spaces width)))
               (gut   (cond
                        ((null lnum) pad)               ; "~" row
                        ((> sub 0)   pad)               ; wrap continuation
                        (T (view--rpad-num lnum width)))))
          (setq acc (cons (pack (list gut (car rs))) acc)))
        (setq i  (1+ i))
        (setq rs (cdr rs))
        (when lm (setq lm (cdr lm))))
      (reverse acc))))

# Apply visual highlight to a full frame's rows.  Returns a new list.
# - raw-rows: list of row content strings (no markup yet)
# - state: editor state (for view-visual-range)
# - top: scroll top buffer row (so we can map screen row -> buf row)
# - left: scroll left (for nowrap; wrap mode passes 0)
# - cols: viewport width
# - line-map: NIL in nowrap, or list of (lnum . sub-row) in wrap mode
(de render--apply-highlights (raw-rows state top left cols line-map)
  (let ((acc NIL) (i 0) (rs raw-rows) (lm line-map))
    (while rs
      (let* ((content (car rs))
             (entry   (if lm (car lm) NIL))
             (lnum    (if lm
                        (if entry (car entry) NIL)
                        (+ top i)))
             (sub-row (if lm (if entry (cdr entry) 0) 0))
             (range   (if lnum (view-visual-range state lnum) NIL))
             # In wrap mode the chunk shown on this screen row starts at
             # buffer col sub-row*cols within the buffer line, so shift
             # the (start . end) range "left" by that offset before
             # applying.
             (chunk-left (+ left (* sub-row cols))))
        (setq acc (cons (render--apply-highlight content range chunk-left cols)
                        acc)))
      (setq i (1+ i))
      (setq rs (cdr rs))
      (when lm (setq lm (cdr lm))))
    (reverse acc)))

# Apply visual highlight markup to a rendered row string.
# `content` is the visible portion of the line (already left-trimmed by `left`),
# `range` is a (buf-start . buf-end) pair or NIL (no highlight on this row),
# `left` is the buffer column corresponding to screen col 0, `cols` is the
# screen width.  Returns the row with ANSI reverse video around the
# highlighted segment.  end = -1 in range means "to end of line".
(de render--apply-highlight (content range left cols)
  (if (null range) content
    (let* ((bs   (car range))                              ; buffer col start
           (be   (cdr range))                              ; buffer col end (or -1)
           (clen (length (unpack content)))
           ; screen-col start: max(0, bs - left)
           (ss   (max 0 (- bs left)))
           ; screen-col end (exclusive).  -1 means to end of visible.
           (se   (if (= be -1) clen (min clen (+ 1 (- be left))))))
      (if (or (>= ss clen) (<= se ss)) content
        (pack (list (sub content 0 ss)
                    (ansi-reverse)
                    (sub content ss (- se ss))
                    (ansi-reset)
                    (sub content se)))))))

# Return (start-col . end-col) of the highlighted columns on buffer
# row `row`, or NIL if the row isn't part of the current visual
# selection (or the editor isn't in visual mode).  end-col of -1
# means "highlight to end of line".  Inclusive endpoints.
(de view-visual-range (state row)
  (let ((mode (s-get state 'mode)))
    (if (not (member mode '(visual-char visual-line)))
      NIL
      (let* ((anchor (s-get state 'visual))
             (cur    (s-get state 'cursor))
             (ar (car anchor)) (ac (cdr anchor))
             (cr (car cur))    (cc (cdr cur))
             (r1 (min ar cr))  (r2 (max ar cr)))
        (cond
          ((or (< row r1) (> row r2)) NIL)
          ((equal mode 'visual-line) (cons 0 -1))
          # visual-char: normalise the (ac, cc) pair given which row
          # is higher in the buffer.
          (T
           (let ((c1 (cond ((< ar cr) ac) ((> ar cr) cc) (T (min ac cc))))
                 (c2 (cond ((< ar cr) cc) ((> ar cr) ac) (T (max ac cc)))))
             (cond
               ((= row r1) (if (= row r2) (cons c1 c2) (cons c1 -1)))
               ((= row r2) (cons 0 c2))
               (T          (cons 0 -1))))))))))

# Compute the cursor's 0-based screen position (scr-row . scr-col), or
# NIL if it's off-screen.  Backend-agnostic -- each backend converts the
# result to its own coordinate system (terminal: +1 each for ANSI;
# gfx: multiply by cell size).  In wrap mode lmap is the line-map
# returned by render--collect-wrap; in nowrap mode it's NIL.
(de view-cursor-pos (lmap top left crow ccol cols text-rows wrap?)
  (if wrap?
    (wrap--cursor-pos lmap crow ccol cols)
    (let ((scr-row (- crow top))
          (scr-col (- ccol left)))
      (if (and (>= scr-row 0) (< scr-row text-rows)
               (>= scr-col 0) (< scr-col cols))
        (cons scr-row scr-col)
        NIL))))

# Diff-based renderer.  prev-rows is the list of strings drawn last frame.
# Returns the new list so the caller can pass it back next time.
(de render-frame-diff (state cols rows prev-rows)
  (let* ((buf       (s-get state 'buffer))
         (cursor    (s-get state 'cursor))
         (scroll    (s-get state 'scroll))
         (top       (car scroll))
         (left      (cdr scroll))
         (crow      (car cursor))
         (ccol      (cdr cursor))
         (text-rows (1- rows))
         (wrap?     *vi-wrap*)
         (gutter-w  (view-gutter-width buf top text-rows))
         (eff-cols  (max 1 (- cols gutter-w)))
         (collected (if wrap?
                      (render--collect-wrap buf top eff-cols text-rows)
                      NIL))
         (raw-rows  (if wrap?
                      (car collected)
                      (render--collect buf top left eff-cols text-rows 0 NIL)))
         (line-map  (if wrap? (cdr collected) NIL))
         # Walk visible rows and apply visual-mode highlights.  In
         # wrap mode the line-map tells us which buffer line + sub-row
         # each screen row corresponds to; the highlight range is
         # measured in BUFFER cols, so for wrapped chunks we shift by
         # sub-row * eff-cols before passing to render--apply-highlight.
         (hl-rows   (render--apply-highlights raw-rows state top
                                              left eff-cols line-map))
         # Prepend line-number gutter (after highlight, before output)
         (new-rows  (view-add-gutter hl-rows line-map top gutter-w)))
    (prin (ansi-cursor-hide))
    # Walk new-rows and prev-rows in parallel; redraw only where they differ.
    # Order: goto, clear-to-eol, then write content.  Clearing BEFORE the
    # content avoids the auto-wrap trap: if the new content is exactly
    # `cols` wide, writing it leaves the cursor at the right edge (or
    # wrapped to the next row in eager-wrap terminals).  Doing the clear
    # AFTER would erase the next row in that case -- which is what
    # produced "unexpected blank lines" after G on files with long lines.
    (let ((i 0) (new new-rows) (prev prev-rows))
      (while (< i text-rows)
        (let ((nl (if new (car new) ""))
              (pl (if prev (car prev) NIL)))
          (when (not (equal nl pl))
            (prin (ansi-goto (1+ i) 1))
            (prin (ansi-clear-line))
            (prin nl)))
        (setq i (1+ i))
        (setq new (cdr new))
        (when prev (setq prev (cdr prev)))))
    # Status bar is too dynamic (mode/msg/cursor pos) to diff cheaply.
    (render-status state cols rows)
    # Position the cursor using the shared 0-based helper, then
    # translate to 1-based ANSI coordinates.  Add gutter-w to the
    # column so the cursor lands past the line-number gutter.
    (let ((pos (view-cursor-pos line-map top left crow ccol eff-cols text-rows wrap?)))
      (when pos
        (prin (ansi-goto (1+ (car pos)) (+ 1 gutter-w (cdr pos))))))
    (prin (ansi-cursor-show))
    (flush)
    new-rows))

(de make-ansi-renderer (cols rows)
  # Returns a coroutine; caller sends state via (co-resume renderer state).
  # The coroutine remembers the previously-drawn rows and only redraws
  # rows whose content actually changed.
  (co (lambda (state)
    (let ((prev-rows NIL))
      (while T
        (setq prev-rows (render-frame-diff state cols rows prev-rows))
        (setq state (yield NIL)))))))
