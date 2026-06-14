# io.l -- High-level I/O helpers

# (slurp path) -- read entire file as string
(de slurp (path)
  (pack (read-all-lines path)))

# (spit path str) -- write string to file
(de spit (path str)
  (write-lines path (list str)))

# (with-in-file path . body) -- redirect input from file
(de with-in-file (path . body)
  (in path (prog . body)))

# (with-out-file path . body) -- redirect output to file
(de with-out-file (path . body)
  (out path (prog . body)))

# (print-list lst) -- print each element
(de print-list (lst)
  (mapc println lst))

# (for-each-line path fn) -- call fn for each line in file
(de for-each-line (path fn)
  (mapc fn (read-all-lines path)))

# (lines str) -- split string into lines
(de lines (str)
  (let ((result NIL)
        (current "")
        (chars (unpack str)))
    (while chars
      (let ((c (car chars)))
        (if (equal c "\n")
          (prog
            (setq result (append result (list current)))
            (setq current ""))
          (setq current (pack (list current c))))
        (setq chars (cdr chars))))
    (if (not (equal current ""))
      (append result (list current))
      result)))

# json-encode and json-decode are C primitives
# Additional JSON helpers:

# (json-encode-pretty val indent) -- pretty-printed JSON
(de json-encode-pretty (val)
  (json-encode val))  # stub: same as compact for now
