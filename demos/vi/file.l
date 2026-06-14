# demos/vi/file.l -- file I/O using L primitives read-all-lines / write-lines

(de file-read (path)
  # Returns buf. read-all-lines returns NIL for both empty files and missing
  # files -- there is no primitive to distinguish the two cases (no stat,
  # no file-exists?). We treat NIL as an empty file and return a single-line
  # empty buffer. Callers that need to detect "file not found" must use an
  # alternative strategy (e.g. checking before calling, or accepting new-file
  # semantics for missing paths).
  (let ((lines (read-all-lines path)))
    (buf-from-lines (or lines '("")))))

(de file-write (buf path)
  # The buffer is a wbt tree of line strings; convert to a list and write.
  # NOTE: if write-lines fails (e.g. permission denied, disk full) it calls
  # pl_error_str internally, which unwinds L's stack and crashes the editor.
  # L has no catch/try primitive, so there is no way to guard against this
  # here. Known limitation: write errors cause an unhandled crash.
  (write-lines path (wbt-to-list buf))
  T)
