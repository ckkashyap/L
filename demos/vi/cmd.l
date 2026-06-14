# demos/vi/cmd.l -- : command line handling
# Requires: state.l, buffer.l, file.l, cursor.l

# Helper: check each char is an ASCII digit (48-57)
(de str-number?--check (chars)
  (if (null chars) T
    (let ((c (char (car chars))))
      (if (and (>= c 48) (<= c 57)) (str-number?--check (cdr chars)) NIL))))

(de str-number? (s)
  (let ((chars (unpack s)))
    (and chars (str-number?--check chars))))

# Helper: convert digit-string to integer
(de str->num--loop (chars acc)
  (if (null chars) acc
    (str->num--loop (cdr chars) (+ (* acc 10) (- (char (car chars)) 48)))))

(de str->num (s) (str->num--loop (unpack s) 0))

# Handle a keypress in command mode.
# (index " " cmd-str) returns 1-based position -- adjust with (1- sp) for verb end.
(de cmd-key (state key)
  (if (s-get state 'search-mode)
    # search mode -- delegate to search.l handler (stub until search.l loaded)
    (search-exec-key state key)
    # command mode: accumulate into cmd-buf, execute on RET
    (let ((buf (s-get state 'cmd-buf)))
      (cond
        ((equal key 'esc)
         (s-set (s-set state 'mode 'normal) 'cmd-buf ""))
        ((equal key 'ret)
         (let ((result (exec-cmd state buf)))
           (s-set (s-set result 'mode 'normal) 'cmd-buf "")))
        ((equal key 'bs)
         (let ((len (length (unpack buf))))
           (if (> len 0)
             (s-set state 'cmd-buf (sub buf 0 (1- len)))
             (s-set (s-set state 'mode 'normal) 'cmd-buf ""))))
        ((string? key)
         (s-set state 'cmd-buf (pack (list buf key))))
        (T state)))))

# Parse an optional leading range from cmd-str.
#
# Returns (range . rest) where range is one of:
#   NIL          -- no range present
#   'all         -- "%"
#   (n1 . n2)    -- explicit numeric range (n1 == n2 if a single line)
# and rest is the remainder of cmd-str after consuming the range.
(de parse-range (cmd-str)
  (let ((chars (unpack cmd-str)))
    (cond
      ((null chars) (cons NIL cmd-str))
      # % means whole buffer
      ((equal (car chars) "%")
       (cons 'all (pack (cdr chars))))
      # Leading digit: parse N, optionally followed by ",M"
      ((and (string? (car chars))
            (>= (char (car chars)) 48) (<= (char (car chars)) 57))
       (let ((n1 0) (cs chars))
         (while (and cs (string? (car cs))
                     (>= (char (car cs)) 48) (<= (char (car cs)) 57))
           (setq n1 (+ (* n1 10) (- (char (car cs)) 48)))
           (setq cs (cdr cs)))
         (if (and cs (equal (car cs) ","))
           (let ((n2 0) (ds (cdr cs)))
             (while (and ds (string? (car ds))
                         (>= (char (car ds)) 48) (<= (char (car ds)) 57))
               (setq n2 (+ (* n2 10) (- (char (car ds)) 48)))
               (setq ds (cdr ds)))
             (cons (cons n1 n2) (pack ds)))
           (cons (cons n1 n1) (pack cs)))))
      (T (cons NIL cmd-str)))))

# Execute a completed command string (already stripped of leading ':').
(de exec-cmd (state cmd-str)
  (let* ((rp   (parse-range cmd-str))
         (range (car rp))
         (rest  (cdr rp))
         (sp    (index " " rest))
         (verb  (if sp (sub rest 0 (1- sp)) rest))
         (arg   (if sp (sub rest sp) NIL)))
    (cond
      # Range + s/// -> apply substitute to each line in range
      ((and range (starts-with? verb "s"))
       (cmd-subst-range state range (if arg arg (sub verb 1))))
      # Lone range (e.g. ":42") -> jump to line
      ((and range (= (length (unpack verb)) 0))
       (let ((n (cond
                  ((equal range 'all)  (buf-nlines (s-get state 'buffer)))
                  (T (cdr range)))))
         (s-set state 'cursor
           (cursor-clamp (s-get state 'buffer)
                         (cons n (cdr (s-get state 'cursor)))))))
      ((equal verb "w")   (cmd-write state arg))
      ((equal verb "q")   (cmd-quit  state NIL))
      ((equal verb "q!")  (s-set state 'running NIL))
      ((equal verb "wq")  (cmd-write-quit state))
      ((equal verb "e")   (cmd-edit  state arg))
      ((equal verb "set") (cmd-set   state arg))
      ((starts-with? verb "s")
       (cmd-subst state (if arg arg (sub verb 1))))
      ((str-number? verb)
       (let ((n (str->num verb)))
         (s-set state 'cursor
           (cursor-clamp (s-get state 'buffer)
                         (cons n (cdr (s-get state 'cursor)))))))
      (T (s-set state 'msg
           (pack (list "E492: Not an editor command: " cmd-str)))))))

# :w [path] -- write buffer to file
(de cmd-write (state arg)
  (let* ((path (or arg (s-get state 'filename)))
         (buf  (s-get state 'buffer)))
    (if (null path)
      (s-set state 'msg "E32: No file name")
      (prog
        (file-write buf path)
        (s-set (s-set (s-set state 'filename path) 'dirty NIL)
               'msg (pack (list "\"" path "\" written")))))))

# :q [force] -- quit; refuse if dirty unless forced
(de cmd-quit (state force)
  (if (and (s-get state 'dirty) (not force))
    (s-set state 'msg "E37: No write since last change (add ! to override)")
    (s-set state 'running NIL)))

# :wq -- write then quit
(de cmd-write-quit (state)
  (let ((st2 (cmd-write state NIL)))
    (if (s-get st2 'dirty) st2 (s-set st2 'running NIL))))

# :set <option> -- toggle display options.  Currently:
#   :set wrap     long lines visually wrap onto subsequent screen rows
#   :set nowrap   long lines are truncated; horizontal scroll shows rest
(de cmd-set (state arg)
  (cond
    ((equal arg "wrap")
     (setq *vi-wrap* T)
     (ensure-cursor-visible (s-set state 'msg "  wrap")))
    ((equal arg "nowrap")
     (setq *vi-wrap* NIL)
     (ensure-cursor-visible (s-set state 'msg "nowrap")))
    ((or (equal arg "number") (equal arg "nu"))
     (setq *vi-number* T)
     (ensure-cursor-visible (s-set state 'msg "  number")))
    ((or (equal arg "nonumber") (equal arg "nonu"))
     (setq *vi-number* NIL)
     (ensure-cursor-visible (s-set state 'msg "nonumber")))
    ((null arg)
     (s-set state 'msg
       (pack (list "  wrap=" (if *vi-wrap* "on" "off")
                   "  number=" (if *vi-number* "on" "off")))))
    (T (s-set state 'msg
         (pack (list "E518: Unknown option: " arg))))))

# :e path -- open a different file.
# NOTE: file-read always returns a buf (empty buffer for missing or empty
# files -- L has no stat/file-exists? primitive to distinguish the two).
# Opening a non-existent path silently creates a new empty buffer, matching
# standard vi "new file" semantics.
(de cmd-edit (state arg)
  (if (null arg)
    (s-set state 'msg "E32: No file name")
    (let ((nb (file-read arg)))
      (s-set (s-set (s-set (s-set state 'buffer nb)
                           'filename arg)
                    'cursor '(1 . 0))
             'dirty NIL))))

