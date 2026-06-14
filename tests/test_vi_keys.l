(load "demos/vi/keys.l")

(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog (println "FAIL:" label "expected" expected "got" actual) (quit 1))))

(check 'up    (decode-csi-seq '(65))      "up")
(check 'down  (decode-csi-seq '(66))      "down")
(check 'right (decode-csi-seq '(67))      "right")
(check 'left  (decode-csi-seq '(68))      "left")
(check 'home  (decode-csi-seq '(72))      "home")
(check 'end   (decode-csi-seq '(70))      "end")
(check 'pgup  (decode-csi-seq '(53 126))  "pgup 5~")
(check 'pgdn  (decode-csi-seq '(54 126))  "pgdn 6~")
(check 'del   (decode-csi-seq '(51 126))  "del 3~")
(check 'ins   (decode-csi-seq '(50 126))  "ins 2~")

(println "ALL TESTS PASSED")
