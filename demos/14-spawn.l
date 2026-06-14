# ============================================================
# 14-spawn.l -- Subprocess spawning and pipe I/O demo
#
# What this covers:
#   1. Subprocess model with pipe-open / spawn
#   2. stdin/stdout communication via pipe-write and pipe-read-line
#   3. Exit codes via pipe-wait / pipe-close
#   4. Platform differences (Windows vs POSIX commands)
#   5. Pipe lifecycle and resource cleanup
#
# Run:  L demos/14-spawn.l
# Spec: L.spec.txt Part 8 -- Subprocesses and Pipes
# ============================================================

# Subprocess primitives are built in -- no external dependencies.
#
# Platform: *os* is "windows" or "posix" -- used below to select the right
# shell and commands for each section.

(println "=== Spawn / Pipe Demo ===")
(println "")

# -------------------------------------------------------------------------
# Platform helper
#
# On Windows: run commands via cmd.exe /c "..."
# On POSIX:   run commands via sh -c "..."
# shell-spawn wraps a one-liner string and spawns it in the right shell.
# -------------------------------------------------------------------------
(de shell-spawn (cmdstr)
  (if (equal *os* "windows")
    (spawn "cmd.exe" "/c" cmdstr)
    (spawn "sh" "-c" cmdstr)))

# -------------------------------------------------------------------------
# 1. Basic spawn: capture stdout of a subprocess
# -------------------------------------------------------------------------
(println "--- 1. Capture output of a subprocess ---")

(setq p (shell-spawn "echo Hello from a child process!"))
(pipe-close-stdin p)
(setq out (pipe-read-all p))
(println "Output:" out)
(println "Exit code:" (pipe-close p))

# -------------------------------------------------------------------------
# 2. Feed data via stdin, read back stdout
#
# Windows: findstr searches for a literal string in each line.
# POSIX:   grep does the same.
# Both filter to lines containing the search term.
# -------------------------------------------------------------------------
(println "--- 2. Write to stdin, read from stdout ---")

(setq p (shell-spawn (if (equal *os* "windows") "findstr a" "grep a")))
(pipe-write p "banana\n")
(pipe-write p "apple\n")
(pipe-write p "cherry\n")
(pipe-close-stdin p)
(setq echoed (pipe-read-all p))
(pipe-close p)
(println "Lines containing 'a':" echoed)

# -------------------------------------------------------------------------
# 3. Sort lines fed through stdin
#
# Both Windows and POSIX provide a `sort` command that reads stdin and
# writes sorted lines to stdout.
# -------------------------------------------------------------------------
(println "--- 3. Sort lines via sort command ---")

(setq p (shell-spawn "sort"))
(pipe-write p "banana\n")
(pipe-write p "apple\n")
(pipe-write p "cherry\n")
(pipe-close-stdin p)
(setq sorted (pipe-read-all p))
(pipe-close p)
(println "Sorted:" sorted)

# -------------------------------------------------------------------------
# 4. Capture exit codes (success vs failure)
#
# `pipe-close` returns the subprocess exit code after waiting for it.
# Windows: cmd.exe /c "exit N" exits with code N.
# POSIX:   sh -c "exit N" exits with code N.
# Both use the same idiom through shell-spawn.
# -------------------------------------------------------------------------
(println "--- 4. Exit codes ---")

(setq p1 (shell-spawn "exit 0"))
(pipe-close-stdin p1)
(pipe-read-all p1)
(println "exit 0  ->" (pipe-close p1))   # => 0

(setq p2 (shell-spawn "exit 42"))
(pipe-close-stdin p2)
(pipe-read-all p2)
(println "exit 42 ->" (pipe-close p2))   # => 42

# -------------------------------------------------------------------------
# 5. Pipe lifecycle
#
# pipe-alive? returns T while the pipe handle is open; NIL after pipe-close.
# Always call pipe-close to release file descriptors and subprocess resources.
# -------------------------------------------------------------------------
(println "--- 5. Pipe lifecycle ---")

(setq p (shell-spawn "echo lifecycle test"))
(println "Alive after spawn:" (pipe-alive? p))   # => T
(pipe-close-stdin p)
(pipe-read-all p)
(pipe-close p)
(println "Alive after close:" (pipe-alive? p))   # => NIL

# -------------------------------------------------------------------------
# 6. Grep-style filtering: keep only lines matching a pattern
# -------------------------------------------------------------------------
(println "--- 6. Filter lines ---")

(setq p (shell-spawn (if (equal *os* "windows") "findstr apple" "grep apple")))
(pipe-write p "banana split\n")
(pipe-write p "apple pie\n")
(pipe-write p "cherry tart\n")
(pipe-write p "apple sauce\n")
(pipe-close-stdin p)
(setq filtered (pipe-read-all p))
(pipe-close p)
(println "Lines with 'apple':" filtered)

# -------------------------------------------------------------------------
# 7. Concurrent pipes: spawn multiple processes, collect results
# -------------------------------------------------------------------------
(println "--- 7. Multiple concurrent pipes ---")

(setq pa (shell-spawn "echo result-A"))
(setq pb (shell-spawn "echo result-B"))
(setq pc (shell-spawn "echo result-C"))

(pipe-close-stdin pa)
(pipe-close-stdin pb)
(pipe-close-stdin pc)

(setq oa (pipe-read-all pa))
(setq ob (pipe-read-all pb))
(setq oc (pipe-read-all pc))

(pipe-close pa)
(pipe-close pb)
(pipe-close pc)

(println "A:" oa)
(println "B:" ob)
(println "C:" oc)

(println "")
(println "=== Spawn demo complete ===")
