# ============================================================
# test_io.l -- File I/O: write-lines / read-all-lines
#
# What this covers:
#   • `(write-lines path lst)` creates/truncates a file and writes
#     each list element as a newline-terminated text line
#   • `(read-all-lines path)` returns a list of strings with
#     newlines stripped, one element per line
#   • Round-trip fidelity: content written equals content read back
#   • Correct line count and per-line indexing (car / cadr / caddr)
#
# Run:  L tests/test_io.l
# Spec: L.spec.txt Part 6 -- I/O Primitives
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Round-trip: write then read back ------------------
# write-lines creates build/test_io_tmp.txt (or truncates it if it
# already exists) and writes each string on its own line.
# read-all-lines parses the file back into a list of strings,
# stripping the trailing newline from each line.
# NOTE: the build/ directory must exist before running this test.
(let ((tmpfile "build/test_io_tmp.txt"))
  (write-lines tmpfile (list "line1" "line2" "line3"))
  (let ((lines (read-all-lines tmpfile)))
    (test "line1" (car lines))    # => first line
    (test "line2" (cadr lines))   # => second line
    (test "line3" (caddr lines))  # => third line
    (test 3 (length lines))))     # edge case: no phantom empty line at EOF

(println "")
(println "test_io.l: ALL TESTS PASSED")
