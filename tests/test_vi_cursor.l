(load "lib/ds.l")
(load "demos/vi/buffer.l")
(load "demos/vi/state.l")
(load "demos/vi/cursor.l")

(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog (println "FAIL:" label "expected" expected "got" actual) (quit 1))))

(let ((b (buf-from-lines '("hello" "hi" ""))))
  (check '(1 . 4) (cursor-clamp b '(1 . 10)) "clamp past eol")
  (check '(1 . 0) (cursor-clamp b '(0 . 0))  "clamp row below 1")
  (check '(3 . 0) (cursor-clamp b '(9 . 0))  "clamp row past eof"))

(let ((b (buf-from-lines '("hello"))))
  (check '(1 . 1) (cur-move-h b '(1 . 0) 1)   "move right 1")
  (check '(1 . 4) (cur-move-h b '(1 . 0) 99)  "move right clamp")
  (check '(1 . 0) (cur-move-h b '(1 . 2) -99) "move left clamp"))

(let ((b (buf-from-lines '("abc" "de" "fghi"))))
  (check '(2 . 1) (cur-move-v b '(1 . 1) 1)  "move down")
  (check '(2 . 1) (cur-move-v b '(3 . 3) -1) "move up clamp col"))

(let ((b (buf-from-lines '("  hello"))))
  (check 2 (cur-first-nonblank b 1) "first-nonblank"))

(let* ((b  (buf-from-lines '("a" "b" "c" "d" "e")))
       (s2 (viewport-ensure b '(5 . 0) '(1 . 0) 80 3)))
  (check 3 (car s2) "scroll down to expose row 5"))

# Test cursor-clamp-insert (insert mode allows col = line length)
(let ((b (buf-from-lines '("hello"))))
  (check '(1 . 5) (cursor-clamp-insert b '(1 . 5)) "clamp-insert at eol")
  (check '(1 . 5) (cursor-clamp-insert b '(1 . 99)) "clamp-insert past eol"))

# Test reg-idx boundaries
(check 0  (reg-idx "a") "reg-idx a=0")
(check 25 (reg-idx "z") "reg-idx z=25")

(println "ALL TESTS PASSED")
