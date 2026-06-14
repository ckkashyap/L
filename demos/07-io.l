# ============================================================
# 07-io.l -- File I/O, formatted output, and JSON encoding
#
# What this covers:
#   1. File I/O with write-lines / read-all-lines
#   2. Formatted console output with fmt
#   3. JSON encoding of L values
#   4. Async file I/O (callback-based)
#
# Run:  L demos/07-io.l
# Spec: L.spec.txt Part 4 -- I/O and Ports
# ============================================================

(println "=== I/O Demo ===")

# -------------------------------------------------------------------------
# 1. File I/O model
#
# L's file I/O is synchronous by default. `write-lines` takes a path and a
# list; each element is written as a line with a trailing newline.
# `read-all-lines` reads the file and returns a list of strings, one per
# line, with newlines stripped.
# -------------------------------------------------------------------------
(let ((tmpfile "build/demo_io.txt"))
  (write-lines tmpfile (list "Line 1" "Line 2" "Line 3"))
  (println "Wrote to" tmpfile)

  (let ((lines (read-all-lines tmpfile)))
    (println "Read" (length lines) "lines:")  # => 3
    (mapc println lines)))                    # => Line 1 / Line 2 / Line 3

# -------------------------------------------------------------------------
# 2. fmt output
#
# `fmt` writes formatted output using `~A` (value placeholder) and `~n`
# (newline). It is the equivalent of printf/format for console output.
# -------------------------------------------------------------------------
(println (fmt "Pi is approximately ~A/~A" 355 113))  # => Pi is approximately 355/113
(println (fmt "Items: ~A" (list 1 2 3)))             # => Items: (1 2 3)

# -------------------------------------------------------------------------
# 3. JSON encoding
#
# `json-encode` converts L values to JSON strings: numbers → JSON numbers,
# strings → JSON strings, lists → JSON arrays, symbols → JSON strings.
# NIL → null.
# -------------------------------------------------------------------------
(println "JSON:")
(println (json-encode (list 1 2 3)))  # => [1,2,3]
(println (json-encode "hello"))       # => "hello"
(println (json-encode 42))            # => 42

# -------------------------------------------------------------------------
# 4. Ports
#
# `file-read` and `file-write` are async (callback-based) I/O primitives.
# `write-lines` and `read-all-lines` work on file paths.
# -------------------------------------------------------------------------

(println "Done.")
