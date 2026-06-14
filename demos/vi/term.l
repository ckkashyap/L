# demos/vi/term.l -- FFI bindings for terminal control

(setq *vi-term-lib*
  (if (equal *os* "windows") "build/term_helper.dll" "build/term_helper.so"))

# NOTE: in L, the integer 0 is truthy (only NIL is false), so
# `(if on 1 0)` would map both 1 and 0 to 1.  Use `(if (= on 0) 0 1)`
# (or just pass `on` directly) so 0 actually means 0.
(de term-set-raw (on)
  (native *vi-term-lib* "term_set_raw" "i" "i" (if (= on 0) 0 1)))

# Enter (1) / leave (0) the alternate screen buffer so vi's content
# is isolated from the user's shell scrollback.  Mirrors what real vim
# does -- the original terminal contents are restored on exit.
(de term-alt-screen (on)
  (native *vi-term-lib* "term_alt_screen" "i" "i" (if (= on 0) 0 1)))

(de term-get-cols ()
  (native *vi-term-lib* "term_get_cols" "i"))

(de term-get-rows ()
  (native *vi-term-lib* "term_get_rows" "i"))

(de term-size ()
  (cons (term-get-cols) (term-get-rows)))

(de term-read-byte ()
  (native *vi-term-lib* "term_read_byte" "i"))

(de term-read-byte-timeout (ms)
  (native *vi-term-lib* "term_read_byte_timeout" "i" "i" ms))
