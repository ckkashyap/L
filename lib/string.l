# String utilities for L interpreter

# (split str sep) -- split string by separator, return list of strings
# Handles multi-character separators (e.g. "\r\n")
(de split (str sep)
  (let ((result NIL)
        (s str)
        (slen (length (unpack sep)))
        (done NIL))
    (while (not done)
      (let ((pos (index sep s)))
        (if pos
          (prog
            (setq result (append result (list (sub s 0 (- pos 1)))))
            (setq s (sub s (+ (- pos 1) slen))))
          (prog
            (setq result (append result (list s)))
            (setq done T)))))
    result))

# (join lst sep) -- join list of strings with separator
(de join (lst sep)
  (if (null lst)
    ""
    (prog1
      (let ((result (car lst))
            (rest (cdr lst)))
        (while rest
          (setq result (pack (list result sep (car rest))))
          (setq rest (cdr rest)))
        result))))

# (starts-with? str prefix)
(de starts-with? (str prefix)
  (let ((slen (length (unpack str)))
        (plen (length (unpack prefix))))
    (and (>= slen plen)
         (equal (sub str 0 plen) prefix))))

# (ends-with? str suffix)
(de ends-with? (str suffix)
  (let ((slen (length (unpack str)))
        (suflen (length (unpack suffix))))
    (and (>= slen suflen)
         (equal (sub str (- slen suflen) suflen) suffix))))

# (contains? str sub)
(de contains? (str sub)
  (if (index sub str) T NIL))

# (repeat str n) -- repeat str n times
(de repeat (str n)
  (let ((result ""))
    (while (> n 0)
      (setq result (pack (list result str)))
      (setq n (1- n)))
    result))

# (pad-left str width char)
(de pad-left (str width ch)
  (let ((len (length (unpack str))))
    (if (>= len width)
      str
      (pack (list (repeat ch (- width len)) str)))))

# (pad-right str width char)
(de pad-right (str width ch)
  (let ((len (length (unpack str))))
    (if (>= len width)
      str
      (pack (list str (repeat ch (- width len)))))))

# (string-length str) -- character count
(de string-length (str)
  (length (unpack str)))
