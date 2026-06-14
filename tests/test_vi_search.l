(load "demos/vi/search.l")

(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog (println "FAIL:" label "expected" expected "got" actual) (quit 1))))

# Build patterns that contain ^ (char 94) since "^" is escape prefix in L strings
(setq *pat-caret-hel* (pack (list (char 94) "hel")))
(setq *pat-lo-dollar* "lo$")
(setq *lbl-anchor-bol* (pack (list "anchor " (char 94))))
(setq *lbl-wplus* (pack (list "\\" "w+")))

(check '(0 . 5)  (regex-match "hello" "hello world" 0)  "literal")
(check '(6 . 11) (regex-match "world" "hello world" 0)  "literal offset")
(check NIL        (regex-match "xyz"   "hello world" 0)  "no match")
(check '(0 . 5)  (regex-match "h.llo" "hello world" 0)  "dot")
(check '(0 . 5)  (regex-match "hel*o" "hello world" 0)  "star-l")
(check '(0 . 3)  (regex-match *pat-caret-hel* "hello" 0) *lbl-anchor-bol*)
(check '(3 . 5)  (regex-match *pat-lo-dollar* "hello" 0) "anchor $")
(check '(0 . 3)  (regex-match "[hH]el" "hello" 0)       "char class")
(check '(0 . 5)  (regex-match "\\w+"  "hello world" 0)  *lbl-wplus*)

(println "ALL TESTS PASSED")
