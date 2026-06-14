# demos/vi/search.l -- pure L regex engine and vi search operations
# Supports: . * + ? ^ $ [...] [^...] \w \d \s
# regex-match returns (start . end) where end is exclusive, or NIL

# ---- Regex compiler ----
# Each compiled atom: (atom-spec quant)
# atom-spec: 'dot | 'bol | 'eol | (lit ch) | (cls chars) | (ncls chars)
#            | (cls-w) | (cls-d) | (cls-s)
# quant: NIL | '* | '+ | '?

# In L, "^" is an escape prefix in strings, so we use (char 94) for caret.
(de re--caret () (char 94))

# Helper: enumerate chars from integer lo to hi, appending to acc
(de re--range-chars (lo hi acc)
  (if (> lo hi) acc
    (re--range-chars (1+ lo) hi (cons (char lo) acc))))

(de re-compile (pat)
  (let ((chars (unpack pat)) (atoms NIL))
    (while chars
      (let* ((c (car chars))
             (atom
               (cond
                 ((equal c (re--caret))
                  (setq chars (cdr chars)) (quote bol))
                 ((equal c "$")
                  (setq chars (cdr chars)) (quote eol))
                 ((equal c ".")
                  (setq chars (cdr chars)) (quote dot))
                 ((equal c "\\")
                  (let ((nc (cadr chars)))
                    (setq chars (cdr (cdr chars)))
                    (cond
                      ((equal nc "w") (list (quote cls-w)))
                      ((equal nc "d") (list (quote cls-d)))
                      ((equal nc "s") (list (quote cls-s)))
                      (T              (list (quote lit) nc)))))
                 ((equal c "[")
                  (setq chars (cdr chars))
                  (let ((neg (equal (car chars) (re--caret))) (cset NIL))
                    (when neg (setq chars (cdr chars)))
                    (while (and chars (not (equal (car chars) "]")))
                      (if (and (cdr chars) (equal (cadr chars) "-")
                               (cdr (cdr chars)) (not (equal (caddr chars) "]")))
                        (prog
                          (setq cset
                            (re--range-chars
                              (char (car chars))
                              (char (caddr chars))
                              cset))
                          (setq chars (cdr (cdr (cdr chars)))))
                        (prog
                          (setq cset (cons (car chars) cset))
                          (setq chars (cdr chars)))))
                    (setq chars (cdr chars))   # consume ']'
                    (if neg (list (quote ncls) cset) (list (quote cls) cset))))
                 (T
                  (setq chars (cdr chars)) (list (quote lit) c))))
             (quant
               (cond
                 ((and chars (equal (car chars) "*")) (setq chars (cdr chars)) (quote *))
                 ((and chars (equal (car chars) "+")) (setq chars (cdr chars)) (quote +))
                 ((and chars (equal (car chars) "?")) (setq chars (cdr chars)) (quote ?))
                 (T NIL))))
        (when atom (setq atoms (append atoms (list (list atom quant)))))))
    atoms))

(de re-match-char (atom ch)
  (cond
    ((equal atom (quote dot))    (not (equal ch "\n")))
    ((equal atom (quote bol))    NIL)
    ((equal atom (quote eol))    NIL)
    ((and (pair atom) (equal (car atom) (quote lit)))   (equal ch (cadr atom)))
    ((and (pair atom) (equal (car atom) (quote cls)))   (member ch (cadr atom)))
    ((and (pair atom) (equal (car atom) (quote ncls)))  (not (member ch (cadr atom))))
    ((and (pair atom) (equal (car atom) (quote cls-w)))
     (let ((c (char ch)))
       (or (and (>= c 65) (<= c 90))
           (and (>= c 97) (<= c 122))
           (and (>= c 48) (<= c 57))
           (= c 95))))
    ((and (pair atom) (equal (car atom) (quote cls-d)))
     (let ((c (char ch))) (and (>= c 48) (<= c 57))))
    ((and (pair atom) (equal (car atom) (quote cls-s)))
     (or (equal ch " ") (equal ch "\t") (equal ch "\n")))
    (T NIL)))

(de re-nth (chars i)
  (if (or (not chars) (< i 0)) NIL
    (if (= i 0) (car chars) (re-nth (cdr chars) (1- i)))))

# Helper for * and + quantifiers: collect all positions where atom matches
(de re--collect-matches (atom chars pos acc)
  (let ((c (re-nth chars pos)))
    (if (and c (re-match-char atom c))
      (re--collect-matches atom chars (1+ pos) (cons (1+ pos) acc))
      acc)))

# Helper: try each position in ls with rest atoms
(de re--try-positions (rest chars start ls)
  (if (not ls) NIL
    (or (re-try rest chars start (car ls))
        (re--try-positions rest chars start (cdr ls)))))

(de re-try (atoms chars start pos)
  (cond
    ((not atoms) pos)
    (T
     (let* ((entry (car atoms))
            (atom  (car entry))
            (quant (cadr entry))
            (rest  (cdr atoms))
            (ch    (re-nth chars pos)))
       (cond
         ((equal atom (quote bol))
          (if (= pos start) (re-try rest chars start pos) NIL))
         ((equal atom (quote eol))
          (if (not ch) (re-try rest chars start pos) NIL))
         ((equal quant (quote *))
          (let ((ms (re--collect-matches atom chars pos (list pos))))
            (re--try-positions rest chars start ms)))
         ((equal quant (quote +))
          (if (and ch (re-match-char atom ch))
            (let ((ms (re--collect-matches atom chars (1+ pos) (list (1+ pos)))))
              (re--try-positions rest chars start ms))
            NIL))
         ((equal quant (quote ?))
          (or (and ch (re-match-char atom ch) (re-try rest chars start (1+ pos)))
              (re-try rest chars start pos)))
         (T
          (if (and ch (re-match-char atom ch))
            (re-try rest chars start (1+ pos))
            NIL)))))))

# Helper for regex-match: try each start position
(de re--search (atoms chars len i)
  (if (> i len) NIL
    (let ((end (re-try atoms chars i i)))
      (if end (cons i end) (re--search atoms chars len (1+ i))))))

(de regex-match (pattern str start)
  (let* ((atoms    (re-compile pattern))
         (chars    (unpack str))
         (len      (length chars))
         (anchored (and atoms (equal (car (car atoms)) (quote bol)))))
    (if anchored
      (let ((end (re-try atoms chars start start)))
        (if end (cons start end) NIL))
      (re--search atoms chars len start))))

# ---- Search operations ----

# Search forward: start at from-row/from-col, wrap around, give up after nlines rows checked
(de sf--loop (buf pat nlines from-row from-col r first remaining)
  (if (< remaining 0) NIL
    (let* ((line (buf-line buf r))
           (sc   (if (and first (= r from-row)) (1+ from-col) 0))
           (m    (regex-match pat line sc)))
      (if m (cons r (car m))
        (let ((nr (if (= r nlines) 1 (1+ r))))
          (sf--loop buf pat nlines from-row from-col nr NIL (1- remaining)))))))

(de search-forward (buf pat from-row from-col)
  (let ((nlines (buf-nlines buf)))
    (sf--loop buf pat nlines from-row from-col from-row T nlines)))

# Helper for search-backward: find last match on a line up to ec
(de sb--last-match (pat line i ec last)
  (if (> i ec) last
    (let ((mm (regex-match pat line i)))
      (if (and mm (<= (car mm) ec))
        (sb--last-match pat line (1+ (car mm)) ec mm)
        (sb--last-match pat line (1+ i) ec last)))))

# Search backward: start at from-row/from-col, wrap around, give up after nlines rows checked
(de sb--loop (buf pat nlines from-row from-col r first remaining)
  (if (< remaining 0) NIL
    (let* ((line (buf-line buf r))
           (ec   (if (and first (= r from-row)) from-col (length (unpack line))))
           (m2   (sb--last-match pat line 0 ec NIL)))
      (if m2 (cons r (car m2))
        (let ((nr (if (= r 1) nlines (1- r))))
          (sb--loop buf pat nlines from-row from-col nr NIL (1- remaining)))))))

(de search-backward (buf pat from-row from-col)
  (let ((nlines (buf-nlines buf)))
    (sb--loop buf pat nlines from-row from-col from-row T nlines)))

(de start-search (state dir)
  (s-set (s-set (s-set (s-set state 'mode 'command) 'cmd-buf "")
                      'search-dir dir)
        'search-mode T))

(de search-exec-key (state key)
  (let ((buf (s-get state 'cmd-buf)))
    (cond
      ((equal key 'esc)
       (s-set (s-set (s-set state 'mode 'normal) 'cmd-buf "") 'search-mode NIL))
      ((equal key 'ret)
       (let* ((pat (s-get state 'cmd-buf))
              (st2 (s-set (s-set (s-set state 'mode 'normal) 'search pat) 'search-mode NIL))
              (st3 (search-jump st2)))
         (s-set st3 'cmd-buf "")))
      ((equal key 'bs)
       (let ((len (length (unpack buf))))
         (if (> len 0)
           (s-set state 'cmd-buf (sub buf 0 (1- len)))
           (s-set (s-set (s-set state 'mode 'normal) 'cmd-buf "") 'search-mode NIL))))
      ((str key)
       (s-set state 'cmd-buf (pack (list buf key))))
      (T state))))

(de search-jump (state)
  (let* ((buf (s-get state 'buffer))
         (cur (s-get state 'cursor))
         (pat (s-get state 'search))
         (dir (s-get state 'search-dir))
         (row (car cur)) (col (cdr cur))
         (hit (if (equal dir 'forward)
                (search-forward  buf pat row col)
                (search-backward buf pat row col))))
    (if hit
      (s-set state 'cursor (cons (car hit) (cdr hit)))
      (s-set state 'msg (pack (list "Pattern not found: " pat))))))

# Helper for search-next: loop n times
(de sn--loop (st n)
  (if (= n 0) st (sn--loop (search-jump st) (1- n))))

(de search-next (state count)
  (sn--loop state (max 1 count)))

(de search-prev (state count)
  (let* ((dir (s-get state 'search-dir))
         (fl  (s-set state 'search-dir (if (equal dir 'forward) 'backward 'forward)))
         (res (search-next fl count)))
    (s-set res 'search-dir dir)))

# Find the word boundaries (s . e) at (row . col) -- contiguous run of
# non-whitespace covering col.  Returns NIL if cursor is on whitespace
# or past end of line.  e is exclusive.
(de word-bounds-at-cursor (buf row col)
  (let* ((line  (buf-line buf row))
         (chars (unpack line))
         (len   (length chars)))
    (if (or (>= col len)
            (equal (sub line col 1) " ")
            (equal (sub line col 1) "\t"))
      NIL
      (let ((s col) (e col))
        (while (and (> s 0)
                    (not (equal (sub line (1- s) 1) " "))
                    (not (equal (sub line (1- s) 1) "\t")))
          (setq s (1- s)))
        (while (and (< e len)
                    (not (equal (sub line e 1) " "))
                    (not (equal (sub line e 1) "\t")))
          (setq e (1+ e)))
        (cons s e)))))

(de word-at-cursor (buf row col)
  (let ((bd (word-bounds-at-cursor buf row col)))
    (if (null bd) NIL
      (sub (buf-line buf row) (car bd) (- (cdr bd) (car bd))))))

# * / # -- set search pattern to the word under the cursor and jump
# to the next / previous occurrence.  Adjusts cursor to a position
# that makes search-forward / search-backward skip the current word
# (otherwise the current match would be re-found at the same spot).
(de search-word-fwd (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (row  (car cur))
         (bd   (word-bounds-at-cursor buf row (cdr cur))))
    (if (null bd) state
      (let* ((word (sub (buf-line buf row) (car bd) (- (cdr bd) (car bd))))
             # Position cursor at word-start so search-forward (which uses
             # from-col+1) starts inside the word -- the pattern is at least
             # 1 char long so it won't match starting from col+1.
             (st1 (s-set state 'cursor (cons row (car bd))))
             (st2 (s-set (s-set st1 'search word) 'search-dir 'forward)))
        (sn--loop st2 (max 1 count))))))

(de search-word-bwd (state count)
  (let* ((buf  (s-get state 'buffer))
         (cur  (s-get state 'cursor))
         (row  (car cur))
         (bd   (word-bounds-at-cursor buf row (cdr cur))))
    (if (null bd) state
      (let* ((word (sub (buf-line buf row) (car bd) (- (cdr bd) (car bd))))
             (ws   (car bd))
             # Position cursor just before the current word so backward
             # search (whose ec = from-col) excludes the current match.
             (st1  (s-set state 'cursor
                     (if (> ws 0)
                       (cons row (1- ws))
                       (cons (max 1 (1- row))
                             (max 0 (1- (buf-line-len buf (max 1 (1- row)))))))))
             (st2  (s-set (s-set st1 'search word) 'search-dir 'backward)))
        (sn--loop st2 (max 1 count))))))

# ---- :s/// substitution ----

# Helper for global replace: split on match, recurse only on suffix to avoid re-matching replacement
(de cs--global (from to l)
  (let ((m (regex-match from l 0)))
    (if (not m) l
      (let* ((prefix (sub l 0 (car m)))
             (suffix (sub l (cdr m)))
             (rest   (cs--global from to suffix)))
        (pack (list prefix to rest))))))

# Apply from->to on one line, honouring the global flag.
(de cs--apply-line (from to global line)
  (if global
    (cs--global from to line)
    (let ((m (regex-match from line 0)))
      (if m (pack (list (sub line 0 (car m)) to (sub line (cdr m))))
        line))))

# Parse a substitute argument into (from to global?).  Accepts either
# "from/to/flags" or "/from/to/flags" (vim's standard form has a
# leading slash after :s; strip it if present).
(de cs--parse-arg (arg)
  (let* ((s     (if (and arg (> (length (unpack arg)) 0)
                         (equal (sub arg 0 1) "/"))
                  (sub arg 1)
                  (or arg "")))
         (parts (split s "/"))
         (from  (if parts (car parts) ""))
         (to    (if (cdr parts) (cadr parts) ""))
         (flags (if (cdr (cdr parts)) (caddr parts) "")))
    (list from to (member "g" (unpack flags)))))

(de cmd-subst (state arg)
  (let* ((p     (cs--parse-arg arg))
         (from  (car p))   (to (cadr p))  (global (caddr p))
         (buf   (s-get state 'buffer))
         (row   (car (s-get state 'cursor)))
         (line  (buf-line buf row))
         (nb    (buf-set-line buf row (cs--apply-line from to global line)))
         (st2   (s-set state 'buffer nb)))
    (s-set st2 'dirty T)))

# Substitute over a range.  `range` is 'all or (r1 . r2).
(de cmd-subst-range (state range arg)
  (let* ((p      (cs--parse-arg arg))
         (from   (car p))   (to (cadr p))  (global (caddr p))
         (buf    (s-get state 'buffer))
         (nlines (buf-nlines buf))
         (r1     (cond ((equal range 'all) 1) (T (max 1 (car range)))))
         (r2     (cond ((equal range 'all) nlines)
                       (T (min nlines (cdr range)))))
         (st1    (undo-save state)))
    (let ((r r1) (nb buf))
      (while (<= r r2)
        (setq nb (buf-set-line nb r
                   (cs--apply-line from to global (buf-line nb r))))
        (setq r (1+ r)))
      (s-set (s-set st1 'buffer nb) 'dirty T))))
