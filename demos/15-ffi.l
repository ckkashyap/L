# ============================================================
# 15-ffi.l -- C Foreign Function Interface demo
#
# What this covers:
#   1. FFI basics: calling C functions via ffi-call / native
#   2. Type codes for arguments and return values
#   3. C standard library functions (strlen, abs, puts, malloc, free)
#   4. Windows API calls via system DLLs (kernel32.dll)
#   5. Synchronous-only execution model
#
# Run:  L demos/15-ffi.l
# Spec: L.spec.txt Part 9 -- Foreign Function Interface
# ============================================================

# Run: build/l demos/15-ffi.l    (Linux/macOS, requires libffi-dev)
#      build\l demos\15-ffi.l    (Windows, built-in bare x64 FFI)
#
# Build with libffi for full type support (including double):
#   Linux:   sudo apt install libffi-dev && make
#   Windows: works out of the box (bundled libffi)

(println "=== FFI (Foreign Function Interface) Demo ===")
(println "")

# -------------------------------------------------------------------------
# 1. FFI basics
#
# `ffi-call` calls a C function from a shared library (DLL on Windows, .so
# on POSIX). The function is looked up by name in the library.
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# 2. Type codes
#
# Argument and return types are specified with keyword symbols:
#   :int    (32-bit integer)
#   :long   (64-bit integer)
#   :ptr    (pointer)
#   :str    (C string)
#   :void   (no return value)
#   :double (64-bit float -- Windows only limitation)
#
# In the `native` form the type is a single-character string:
#   "i" = int, "l" = long/int64, "p" = pointer, "s" = string, "v" = void
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# Probe: verify the FFI is available at all
# -------------------------------------------------------------------------
(setq libc
  (if (equal *os* "windows") "msvcrt.dll" "libc.so.6"))

(setq ffi-ok
  (guard (e (T NIL))
    (native libc "strlen" "l" "s" "probe")
    T))

(if (not ffi-ok)
  (prog
    (println "FFI not available.")
    (println "On Linux:   sudo apt install libffi-dev && make clean && make")
    (println "On Windows: build.bat (libffi is bundled)")
    (quit 0)))

(println "Using C library:" libc)
(println "")

# =========================================================================
# Part A -- C runtime library (msvcrt.dll / libc.so.6)
# =========================================================================

# -------------------------------------------------------------------------
# 3. C standard library
#
# Functions like `strlen`, `abs`, `puts`, `malloc`, `free` are available
# via the C runtime library loaded automatically.
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# 1. String functions: strlen
# -------------------------------------------------------------------------
(println "--- 1. strlen ---")

(setq len (native libc "strlen" "l" "s" "hello, world"))
(println "strlen(\"hello, world\") =" len)    # => 12

(setq len2 (native libc "strlen" "l" "s" ""))
(println "strlen(\"\") =" len2)               # => 0

# -------------------------------------------------------------------------
# 2. Integer math: abs
# -------------------------------------------------------------------------
(println "--- 2. abs ---")

(setq a (native libc "abs" "i" "i" -42))
(println "abs(-42) =" a)                      # => 42

(setq b (native libc "abs" "i" "i" 17))
(println "abs(17)  =" b)                      # => 17

# -------------------------------------------------------------------------
# 3. Side-effecting call: puts (writes to stdout)
# -------------------------------------------------------------------------
(println "--- 3. puts (side effect) ---")

(setq r (native libc "puts" "i" "s" ">>> This line came from C puts() <<<"))
(println "puts() return value (>=0 on success):" r)

# -------------------------------------------------------------------------
# 4. Pointer round-trip: malloc + free
# -------------------------------------------------------------------------
(println "--- 4. malloc / free ---")

(setq ptr (native libc "malloc" "p" "l" 256))
(println "malloc(256) returned non-zero:" (not (= ptr 0)))
(native libc "free" "v" "p" ptr)
(println "free() completed without crash")

# -------------------------------------------------------------------------
# 5. ffi-open: pre-load a library
# -------------------------------------------------------------------------
(println "--- 5. ffi-open ---")

(ffi-open libc)
(println "ffi-open succeeded for:" libc)

# -------------------------------------------------------------------------
# 6. C string return: strerror
# -------------------------------------------------------------------------
(println "--- 6. strerror ---")

(setq msg (native libc "strerror" "s" "i" 0))
(println "strerror(0) =" msg)

# =========================================================================
# Part B -- Windows-specific Win32 API calls (kernel32.dll)
# =========================================================================

# -------------------------------------------------------------------------
# 4. Windows API
#
# On Windows, system DLLs (kernel32.dll, user32.dll) can be called
# directly. This enables accessing Windows-specific functionality.
#
# 5. Synchronous only
#
# FFI calls are synchronous -- they block the interpreter until the C
# function returns. There is no async FFI. Long-running C calls will
# block coroutines.
# -------------------------------------------------------------------------

(if (equal *os* "windows")
  (prog
    (println "")
    (println "--- 7. Win32 API: GetCurrentProcessId (kernel32.dll) ---")
    (setq pid (native "kernel32.dll" "GetCurrentProcessId" "i"))
    (println "GetCurrentProcessId() =" pid)

    (println "--- 8. Win32 API: GetTickCount (kernel32.dll) ---")
    (setq t1 (native "kernel32.dll" "GetTickCount" "i"))
    (setq t2 (native "kernel32.dll" "GetTickCount" "i"))
    (println "GetTickCount() first call  =" t1)
    (println "GetTickCount() second call =" t2)
    (println "Elapsed ms between calls   =" (- t2 t1))

    (println "--- 9. Win32 API: GetCurrentThreadId (kernel32.dll) ---")
    (setq tid (native "kernel32.dll" "GetCurrentThreadId" "i"))
    (println "GetCurrentThreadId() =" tid)

    (println "--- 10. Win32 API: GetLastError (kernel32.dll) ---")
    (setq err (native "kernel32.dll" "GetLastError" "i"))
    (println "GetLastError() =" err)  # => 0 (no prior error)
  )
  (prog
    (println "")
    (println "--- 7. atol (int64 from string) ---")
    (guard (e (T (println "(atol not tested on this platform)")))
      (setq n64 (native libc "atol" "l" "s" "9876543210"))
      (println "atol(\"9876543210\") =" n64))  # => 9876543210
  ))

(println "")
(println "=== FFI demo complete ===")
