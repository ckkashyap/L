(load "lib/ds.l")
(load "demos/vi/buffer.l")

(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog (println "FAIL:" label "expected" expected "got" actual) (quit 1))))

(let ((b (buf-from-lines '("hello" "world" ""))))
  (check 3       (buf-nlines b)   "nlines")
  (check "hello" (buf-line b 1)   "line 1")
  (check "world" (buf-line b 2)   "line 2")
  (check ""      (buf-line b 3)   "line 3"))

(let* ((b  (buf-from-lines '("a" "b" "c")))
       (b2 (buf-set-line b 2 "B")))
  (check "b" (buf-line b  2) "original unchanged")
  (check "B" (buf-line b2 2) "set-line updated"))

(let* ((b  (buf-from-lines '("a" "c")))
       (b2 (buf-ins-line b 2 "b")))
  (check 3   (buf-nlines b2) "ins-line nlines")
  (check "b" (buf-line b2 2) "ins-line content")
  (check "c" (buf-line b2 3) "ins-line shifted"))

(let* ((b  (buf-from-lines '("a" "b" "c")))
       (b2 (buf-del-line b 2)))
  (check 2   (buf-nlines b2) "del-line nlines")
  (check "c" (buf-line b2 2) "del-line content"))

(let* ((b  (buf-from-lines '("hello")))
       (b2 (buf-ins-char b 1 2 (car (unpack "X"))))
       (b3 (buf-del-char b2 1 2)))
  (check "heXllo" (buf-line b2 1) "ins-char")
  (check "hello"  (buf-line b3 1) "del-char"))

(let* ((b  (buf-from-lines '("hello world")))
       (b2 (buf-split-line b 1 5))
       (b3 (buf-join-lines b2 1)))
  (check "hello"       (buf-line b2 1) "split-line 1")
  (check " world"      (buf-line b2 2) "split-line 2")
  (check "hello world" (buf-line b3 1) "join-lines"))

(check "foo\nbar" (buf-to-string (buf-from-lines '("foo" "bar"))) "to-string")
(check 2 (buf-nlines (buf-from-string "foo\nbar")) "from-string nlines")

# Test: buf-del-line on 1-line buffer (never-empty invariant)
(let* ((b  (buf-from-lines '("only")))
       (b2 (buf-del-line b 1)))
  (check 1  (buf-nlines b2) "del-line last nlines")
  (check "" (buf-line b2 1) "del-line last content"))

# Test: buf-from-string with trailing newline
(let ((b (buf-from-string "foo\n")))
  (check 2 (buf-nlines b) "from-string trailing newline nlines")
  (check "" (buf-line b 2) "from-string trailing newline last line"))

# Test: buf-join-lines nlines
(let* ((b  (buf-from-lines '("hello" "world")))
       (b2 (buf-join-lines b 1)))
  (check 1           (buf-nlines b2)   "join-lines nlines")
  (check "helloworld" (buf-line b2 1) "join-lines content"))

# Test: buf-join-lines on last line is no-op
(let* ((b  (buf-from-lines '("only")))
       (b2 (buf-join-lines b 1)))
  (check 1      (buf-nlines b2) "join-lines last no-op nlines")
  (check "only" (buf-line b2 1) "join-lines last no-op content"))

(println "ALL TESTS PASSED")
