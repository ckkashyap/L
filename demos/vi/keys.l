# demos/vi/keys.l -- key decoding and key-source coroutine
# Requires: demos/vi/term.l (term-read-byte, term-read-byte-timeout)

# (decode-csi-seq bytes) -- pure: decode CSI from list of byte values (testable)
(de decode-csi-seq (bytes)
  (let ((params "") (b 0))
    (while (and bytes
                (let ((c (car bytes)))
                  (or (and (>= c 48) (<= c 57)) (= c 59))))
      (setq params (pack (list params (char (car bytes)))))
      (setq bytes (cdr bytes)))
    (setq b (if bytes (car bytes) 0))
    (cond
      ((= b 65) 'up)    ((= b 66) 'down)
      ((= b 67) 'right) ((= b 68) 'left)
      ((= b 72) 'home)  ((= b 70) 'end)
      ((and (= b 126) (equal params "5")) 'pgup)
      ((and (= b 126) (equal params "6")) 'pgdn)
      ((and (= b 126) (equal params "3")) 'del)
      ((and (= b 126) (equal params "2")) 'ins)
      (T 'unknown))))

(de decode-csi ()
  (let ((param-bytes NIL) (b (term-read-byte)))
    (while (or (and (>= b 48) (<= b 57)) (= b 59))
      (setq param-bytes (append param-bytes (list b)))
      (setq b (term-read-byte)))
    (decode-csi-seq (append param-bytes (list b)))))

(de decode-ss3 ()
  (let ((b (term-read-byte)))
    (cond ((= b 65) 'up) ((= b 66) 'down) ((= b 67) 'right) ((= b 68) 'left)
          (T 'unknown))))

(de decode-esc ()
  (let ((b (term-read-byte-timeout 50)))
    (cond ((= b -1) 'esc) ((= b 91) (decode-csi)) ((= b 79) (decode-ss3)) (T 'esc))))

(de read-next-key ()
  (let ((b (term-read-byte)))
    (cond
      ((= b  27) (decode-esc))
      ((= b  13) 'ret)
      ((= b 127) 'bs)
      ((= b   8) 'bs)
      ((= b   9) 'tab)
      ((= b  18) 'ctrl-r)
      ((= b  22) 'ctrl-v)
      ((= b  23) 'ctrl-w)
      ((= b  21) 'ctrl-u)
      ((= b   4) 'ctrl-d)
      ((= b   6) 'ctrl-f)
      ((= b   2) 'ctrl-b)
      ((< b   0) 'eof)       # NEW: EOF / error sentinel
      ((< b  32) NIL)
      (T (char b)))))

(de make-term-key-source ()
  (co (lambda (_)
    (let ((running T))
      (while running
        (let ((k (read-next-key)))
          (cond
            ((equal k 'eof) (setq running NIL))
            (k (yield k)))))))))
