(load "tests/vi_harness.l")

# H/M/L move to top/middle/bottom of the visible VIEWPORT, not the buffer.
# Build a 30-line buffer; viewport rows defaults to 24, text-rows = 23.
# At the start, top=1 so the viewport shows lines 1..23.
#   H -> line 1     (viewport top)
#   M -> line 12    (middle of [1..23])
#   L -> line 23    (viewport bottom = top + text-rows - 1)

(de gen-lines (n acc)
  (if (= n 0) acc
    (gen-lines (1- n) (cons (pack (list "line " (str n))) acc))))

(let* ((buf (buf-from-lines (gen-lines 30 NIL)))
       (st0 (make-state buf))
       (disp (make-mode-dispatch st0))
       (state (co-resume disp NIL)))
  # H from line 1 -- already on top
  (let ((s1 (co-resume disp "H")))
    (check '(1 . 0) (vi-cursor s1) "H from top -> line 1"))

  # M -- middle of visible viewport (top=1, text-rows=23 -> middle = 12)
  (let ((s2 (co-resume disp "M")))
    (check '(12 . 0) (vi-cursor s2) "M -> middle of viewport"))

  # L -- bottom of visible viewport (top=1, text-rows=23 -> bot = 23)
  (let ((s3 (co-resume disp "L")))
    (check '(23 . 0) (vi-cursor s3) "L -> bottom of viewport")))

(println "ALL TESTS PASSED")
