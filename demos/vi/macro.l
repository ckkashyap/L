# demos/vi/macro.l -- macro recording (qa-qz) and playback (@a-@z)
# Requires: state.l

# Helper for play-macro: repeat keys list n times, accumulating into acc
(de pm--repeat (keys n acc)
  (if (= n 0) acc (pm--repeat keys (1- n) (append acc keys))))

(de start-macro-record (state key)
  (if (and (str key)
           (>= (char (car (unpack key))) 97) (<= (char (car (unpack key))) 122))
    (let* ((idx     (reg-idx key))
           (macros  (pvec-put (s-get state 'macros) idx NIL))
           (st2     (s-set state 'macros macros))
           (st3     (s-set st2 'macro-rec idx)))
      (s-set st3 'msg (pack (list "recording @" key))))
    state))

(de stop-macro-record (state)
  (s-set (s-set state 'macro-rec NIL) 'msg "Macro recorded"))

(de macro-add-key (state key)
  (let* ((idx    (s-get state 'macro-rec))
         (macros (s-get state 'macros))
         (cur    (or (pvec-get macros idx) NIL))
         (new    (append cur (list key))))
    (s-set state 'macros (pvec-put macros idx new))))

(de play-macro (state key count)
  (if (and (str key)
           (>= (char (car (unpack key))) 97) (<= (char (car (unpack key))) 122))
    (let* ((idx    (reg-idx key))
           (keys   (or (pvec-get (s-get state 'macros) idx) NIL))
           (inject (pm--repeat keys (max 1 count) NIL)))
      (s-set state 'macro-inject inject))
    state))

(de make-macro-filter (key-src state-cell)
  # state-cell is a 1-element list holding current state (mutable ref)
  # The filter checks (car state-cell) for macro-inject on each cycle.
  (co (lambda (_)
    (let ((buf NIL))
      (while T
        (let ((inject (s-get (car state-cell) 'macro-inject)))
          (when inject
            (setq buf (append buf inject))
            (set-car state-cell (s-set (car state-cell) 'macro-inject NIL))))
        (let ((key (if buf
                     (let ((k (car buf)))
                       (setq buf (cdr buf)) k)
                     (co-resume key-src NIL))))
          # record key if macro recording active
          (let ((idx (s-get (car state-cell) 'macro-rec)))
            (when (and idx (not (equal key "q")))
              (set-car state-cell (macro-add-key (car state-cell) key))))
          (yield key)))))))
