(load "lib/ds.l")
(load "lib/string.l")
(load "demos/vi/buffer.l")
(load "demos/vi/cursor.l")
(load "demos/vi/state.l")
(load "demos/vi/ops.l")

(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog (println "FAIL:" label "expected" expected "got" actual) (quit 1))))

(let ((b (buf-from-lines '("hello world foo"))))
  (check '(1 . 4)  (word-end-forward   b '(1 . 0) 1) "word-end-fwd 1")
  (check '(1 . 10) (word-end-forward   b '(1 . 0) 2) "word-end-fwd 2")
  (check '(1 . 6)  (word-start-forward b '(1 . 0) 1) "word-start-fwd")
  (check '(1 . 6)  (word-start-backward b '(1 . 10) 1) "word-start-bk"))

# delete-region char (same line, cols 0..4 = "hello")
(let* ((b   (buf-from-lines '("hello world")))
       (st  (make-state b))
       (st2 (delete-region st (list 1 0 1 4 'char) T)))
  (check " world" (buf-line (s-get st2 'buffer) 1) "delete char region"))

# delete-region line
(let* ((b   (buf-from-lines '("a" "b" "c")))
       (st  (make-state b))
       (st2 (delete-region st (list 1 0 2 0 'line) T)))
  (check 1   (buf-nlines (s-get st2 'buffer)) "delete line nlines")
  (check "c" (buf-line (s-get st2 'buffer) 1)  "delete line content"))

# put-after char-wise
(let* ((b   (buf-from-lines '("hello")))
       (st  (s-set (make-state b) 'unnamed-reg "XY"))
       (st2 (put-after st 1)))
  (check "hXYello" (buf-line (s-get st2 'buffer) 1) "put-after char"))

# delete-char-at-cursor
(let* ((b  (buf-from-lines '("abcde")))
       (st (s-set (make-state b) 'cursor '(1 . 2)))
       (s2 (delete-char-at-cursor st 1)))
  (check "abde" (buf-line (s-get s2 'buffer) 1) "delete-char"))

(println "ALL TESTS PASSED")
