# demos/vi/vi.l -- vi editor entry point

(load "lib/ds.l")
(load "lib/string.l")
(load "demos/vi/buffer.l")
(load "demos/vi/state.l")
(load "demos/vi/cursor.l")
(load "demos/vi/term.l")
(load "demos/vi/keys.l")
(load "demos/vi/ops.l")
(load "demos/vi/file.l")
(load "demos/vi/search.l")
(load "demos/vi/visual.l")
(load "demos/vi/macro.l")
(load "demos/vi/cmd.l")
(load "demos/vi/render.l")
(load "demos/vi/mode.l")
(load "demos/vi/gfx.l")

# Viewport dimensions for exec-motion (set by terminal/gfx startup)
(setq *vi-cols* 80)
(setq *vi-rows* 24)

# Display option: when T, long lines visually wrap onto subsequent
# screen rows; when NIL, they're truncated and horizontal scroll
# (left) shows the rest.  Toggled with ":set wrap" / ":set nowrap".
(setq *vi-wrap* NIL)

# Display option: when T, show line numbers in a left-side gutter.
# Toggled with ":set number" / ":set nonumber".
(setq *vi-number* NIL)

# Parse *args* for --gfx flag and a filename.
# *args* is the full argv list; skip entries that are the interpreter itself
# or script files (.l extension) or flags we don't recognise.
(setq *vi-gfx* NIL)
(setq *vi-file* NIL)

(let ((args (cdr *args*)))   # skip argv[0] (interpreter path)
  (while args
    (let ((a (car args)))
      (cond
        ((equal a "--gfx")
         (setq *vi-gfx* T))
        ((equal a "--")
         NIL)   # end-of-flags sentinel; remaining handled below
        ((and (str a)
              (not (starts-with? a "-"))
              (not (ends-with? a ".l")))   # skip loaded script names
         (when (not *vi-file*)
           (setq *vi-file* a))))
      (setq args (cdr args)))))

# Build the initial buffer from the file (or empty).
(setq *vi-buf*
  (if *vi-file*
    (or (file-read *vi-file*) (buf-empty))
    (buf-empty)))

# ── Main editor loop ──────────────────────────────────────────────────────────

(if *vi-gfx*
  (vi-gfx-start *vi-file* *vi-buf*)
  (let* ((st0  (make-state *vi-buf*))
         (st0  (if *vi-file* (s-set st0 'filename *vi-file*) st0))
         (sz   (term-size))
         (cols (car sz))
         (rows (cdr sz)))
    (setq *vi-cols* cols)
    (setq *vi-rows* rows)
    (term-set-raw 1)
    (term-alt-screen 1)
    (prin (ansi-clear-screen))
    (let* ((state-cell (list st0))
           (ksrc  (make-term-key-source))
           (mac   (make-macro-filter ksrc state-cell))
           (rend  (make-ansi-renderer cols rows))
           (disp  (make-mode-dispatch st0))
           (state (co-resume disp NIL)))   # kickstart: runs to first (yield state)
      (set-car state-cell state)
      (co-resume rend state)               # render initial frame
      (while (s-get state 'running)
        (let* ((key (co-resume mac NIL))
               (key (if (and (equal key "q") (s-get state 'macro-rec) (equal (s-get state 'mode) 'normal))
                      (prog (setq state (stop-macro-record state))
                            (set-car state-cell state)
                            NIL)
                      key))
               (st2 (if key (co-resume disp key) state)))
          (setq state st2)
          (set-car state-cell state)
          (co-resume rend state)))
      # Restore terminal: show cursor, reset attributes, leave alt
      # screen (which restores the user's shell content), leave cooked
      # mode.  term_helper also installs atexit + signal handlers so
      # abnormal exits get the same cleanup.
      (prin (ansi-cursor-show))
      (prin (ansi-reset))
      (flush)
      (term-alt-screen 0)
      (term-set-raw 0)
      (println "Goodbye."))))
