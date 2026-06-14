# ============================================================
# 23-callbacks.l -- C→Lisp callbacks via ffi-callback
#
# What this covers:
#   1. What is ffi-callback?
#   2. atexit: run Lisp code at process exit
#   3. qsort: sort a C int array using a Lisp comparator
#   4. Signal handler (POSIX only)
#
# Requires: build with HAVE_FFI=1 (libffi)
#   Linux:   sudo apt install libffi-dev && make clean && make
#   Windows: already bundled -- rebuild with: nmake -f Makefile.win
#
# Run:  L demos/23-callbacks.l
# ============================================================

(when (not (procedure? ffi-callback))
  (println "This demo requires libffi.")
  (println "  Linux:   sudo apt install libffi-dev && make clean && make")
  (println "  Windows: already bundled -- rebuild with: nmake -f Makefile.win")
  (quit))

(setq libc (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))

(println "=== FFI Callbacks Demo ===")
(println "")

# =========================================================================
# 1. What is ffi-callback?
#
# C libraries often accept function pointer callbacks: qsort needs a
# comparator, atexit needs a shutdown hook, GUI toolkits need event
# handlers. (ffi-callback ret-type arg-types fn) creates an executable C
# function pointer that, when called by C, invokes the Lisp function fn.
# The callback is GC-managed: alive as long as the Val is reachable.
#
# Type codes: "v" void, "i" int32, "l" int64, "p" pointer
# =========================================================================

# =========================================================================
# 2. atexit: register a Lisp shutdown hook
#
# C signature: int atexit(void (*function)(void))
# The registered function is called when the process exits normally.
# =========================================================================
(println "--- 2. atexit shutdown hook ---")

(setq exit-hook
  (ffi-callback "v" ""
    (lambda ()
      (println "  [atexit] Lisp shutdown hook fired"))))

(native libc "atexit" "i" "p" exit-hook)
(println "Registered atexit hook (fires when process exits)")

# =========================================================================
# 3. qsort with a Lisp comparator
#
# C signature: void qsort(void *base, size_t nmemb, size_t size,
#                         int (*compar)(const void*, const void*))
#
# The comparator receives two const void* pointers to adjacent array
# elements. mem-read-i32 dereferences them to get the integer values.
# mem-write-i32 fills the array before sorting.
# =========================================================================
(println "")
(println "--- 3. qsort with Lisp comparator ---")

(setq n 6)
(setq arr (native libc "malloc" "p" "i" (* n 4)))

# Fill array with: 5 1 4 2 6 3
(let ((vals (list 5 1 4 2 6 3)) (i 0))
  (while vals
    (mem-write-i32 arr (* i 4) (car vals))
    (setq vals (cdr vals))
    (setq i (1+ i))))

(println "Before sort:")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

# Ascending comparator: return (a - b)
(setq int-cmp
  (ffi-callback "i" "pp"
    (lambda (a b)
      (- (mem-read-i32 a 0) (mem-read-i32 b 0)))))

(native libc "qsort" "v" "p" arr "i" n "i" 4 "p" int-cmp)

(println "After sort (ascending):")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

# Descending comparator: return (b - a)
(setq int-cmp-desc
  (ffi-callback "i" "pp"
    (lambda (a b)
      (- (mem-read-i32 b 0) (mem-read-i32 a 0)))))

(native libc "qsort" "v" "p" arr "i" n "i" 4 "p" int-cmp-desc)

(println "After sort (descending):")
(let ((i 0))
  (while (< i n)
    (prin "  " (mem-read-i32 arr (* i 4)))
    (setq i (1+ i))))
(println "")

(native libc "free" "v" "p" arr)

# =========================================================================
# 4. Signal handler (POSIX only)
#
# C signature: void (*signal(int signum, void (*handler)(int)))(int)
# SIGUSR1 = 10 on Linux. We send ourselves the signal via raise(10).
# On Windows, signal handling is different -- skip with a note.
# =========================================================================
(println "")
(println "--- 4. Signal handler (POSIX only) ---")

(if (equal *os* "posix")
  (let ((sig-count 0))
    (setq sigusr1-handler
      (ffi-callback "v" "i"
        (lambda (sig)
          (setq sig-count (1+ sig-count)))))
    (native "libc.so.6" "signal" "p" "i" 10 "p" sigusr1-handler)
    (println "SIGUSR1 handler installed")
    (native "libc.so.6" "raise" "i" "i" 10)
    (native "libc.so.6" "raise" "i" "i" 10)
    (println "Sent SIGUSR1 twice, handler fired" sig-count "times"))
  (println "(Skipped on Windows -- use SetConsoleCtrlHandler for signal-like events)"))

(println "")
(println "=== Callbacks demo complete ===")
(println "(The atexit hook fires when the process exits)")
