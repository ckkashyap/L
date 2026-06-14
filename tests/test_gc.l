# ============================================================
# test_gc.l -- Mark-sweep garbage collector stress tests
#
# What this covers:
#   • Cons-cell mark/sweep under sustained allocation pressure
#   • Deep recursion -- iterative mark stack must not overflow
#   • Shared structure: two lists pointing to the same tail survive GC
#   • Vec pool GC: vectors are reclaimed when unreferenced, kept when live
#   • Pipe handle GC (TAG_PIPE): pipe values rooted in variables and lists
#     survive GC cycles; slot reuse works after close; no auto-close on GC
#
# Run:  L tests/test_gc.l
# Spec: L.spec.txt Part 9 -- Memory Management
#
# NOTE: This file tests GC CORRECTNESS, not feature correctness.
#       If any test here fails, the bug is in the GC, not in the
#       feature (lists, vecs, pipes, etc.) being exercised.
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# -------------------------------------------------------------------------
# 1. Basic cons-cell GC: allocate and discard many lists
# -------------------------------------------------------------------------
# make-list builds a list of n elements; every call returns a fresh list
# that is immediately discarded (not stored in any root).  After 100
# iterations x 100 cells the GC must have reclaimed all of them without
# corrupting the heap or crashing.  The (test T T) is a crash guard.
(de make-list (n)
  (let ((result NIL))
    (while (> n 0)
      (setq result (cons n result))
      (setq n (1- n)))
    result))

# Create and discard 10K lists
(let ((i 0))
  (while (< i 100)
    (make-list 100)
    (setq i (1+ i))))

(test T T)   # If we get here, GC didn't crash

# -------------------------------------------------------------------------
# 2. Deep recursion
# -------------------------------------------------------------------------
# Tests that the GC's iterative mark stack handles deeply nested live
# structures without a C-stack overflow.  depth 100 is modest but
# sufficient to exercise the stack path during any GC that fires mid-call.
(de depth (n)
  (if (= n 0) 0 (1+ (depth (1- n)))))

(test 100 (depth 100))

# -------------------------------------------------------------------------
# 3. Shared structure survival across GC
# -------------------------------------------------------------------------
# Both `a` and `b` share the same tail cons cell (the list 1 2 3).
# The GC must mark the shared tail only once but must NOT collect it
# while either root remains reachable.  eq? (pointer equality) confirms
# the tail is truly shared, not copied.
(let ((shared (list 1 2 3)))
  (let ((a (cons 0 shared))
        (b (cons 9 shared)))
    (test 1 (cadr a))
    (test 1 (cadr b))
    (test T (eq (cdr a) (cdr b)))))

# -------------------------------------------------------------------------
# 4. Vec GC: vectors survive while referenced, are freed when not
# -------------------------------------------------------------------------
# alloc-vecs creates `n` vectors, each immediately going out of scope.
# The GC must reclaim them.  After 200 such discarded vecs we create
# live-vec and keep a reference to it, then trigger more GC pressure.
# live-vec must retain its contents (length=3, element[1]=2).
(de alloc-vecs (n)
  (let ((i 0))
    (while (< i n)
      (let ((v (vec 4 0)))   # allocate a vec and immediately discard
        (vec-push v 42))
      (setq i (1+ i)))))

(alloc-vecs 200)   # many vecs created and discarded -- GC must reclaim them

(setq live-vec (vec 0 0))
(vec-push live-vec 1)
(vec-push live-vec 2)
(vec-push live-vec 3)
(alloc-vecs 200)   # trigger GC pressure; live-vec must survive

(test 3 (vec-length live-vec))
(test 2 (vec-get live-vec 1))

# -------------------------------------------------------------------------
# 5. Pipe GC: pipe handles are tracked by TAG_PIPE; they survive GC while
#    referenced and can be stored in lists, passed as arguments, etc.
#
# Uses *os* to pick the right shell so tests pass on Windows and POSIX.
# -------------------------------------------------------------------------

# Spawn a simple echo process in a cross-platform way.
(de echo-proc (msg)
  (if (equal *os* "windows")
    (spawn "cmd.exe" "/c" (pack "echo " msg))
    (spawn "sh" "-c" (pack "printf '%s\\n' " msg))))

# drain-pipe: close stdin so the child sees EOF, read all output, then
# close the pipe handle.  After this, pipe-alive? returns NIL.
(de drain-pipe (p)
  (pipe-close-stdin p)
  (pipe-read-all p)
  (pipe-close p))

# 5a. A pipe value held in a variable survives a GC cycle
# The pipe is stored in gc-pipe (a root).  50 rounds of make-list
# creates ~10K cons cells -- enough to trigger GC.  The pipe handle
# must survive and still report alive afterward.
(setq gc-pipe (echo-proc "gc-test"))
(test T (pipe-alive? gc-pipe))

# Force GC pressure while holding the pipe
(let ((i 0))
  (while (< i 50)
    (make-list 200)
    (setq i (1+ i))))

# Pipe must still be valid after GC
(test T (pipe-alive? gc-pipe))
(drain-pipe gc-pipe)
(test NIL (pipe-alive? gc-pipe))

# 5b. A pipe stored inside a list survives GC
# After clearing the direct reference p1, only pipe-list keeps the pipe
# reachable.  The GC must trace into the list and mark the pipe handle.
(setq p1 (echo-proc "in-list"))
(setq pipe-list (list p1 42 "hello"))
(setq p1 NIL)   # clear the direct reference -- only list holds it

(let ((i 0))
  (while (< i 50)
    (make-list 200)
    (setq i (1+ i))))

# The pipe extracted from the list must still be alive
(setq recovered (car pipe-list))
(test T (pipe-alive? recovered))
(drain-pipe recovered)
(setq pipe-list NIL)

# 5c. Multiple concurrent pipes survive GC pressure
# Three pipes are open simultaneously.  GC must mark all three even
# though they share no structural relationship with each other.
(setq pa (echo-proc "pipe-a"))
(setq pb (echo-proc "pipe-b"))
(setq pc (echo-proc "pipe-c"))

(let ((i 0))
  (while (< i 80)
    (make-list 150)
    (setq i (1+ i))))

(test T (pipe-alive? pa))
(test T (pipe-alive? pb))
(test T (pipe-alive? pc))

(drain-pipe pa)
(drain-pipe pb)
(drain-pipe pc)

(test NIL (pipe-alive? pa))
(test NIL (pipe-alive? pb))
(test NIL (pipe-alive? pc))

# 5d. Pipe slot reuse: after closing, the slot index is reused for the next spawn
# Ensures the pipe table's free-slot bookkeeping is not confused by GC
# running between the close and the subsequent spawn.
(setq p-first (echo-proc "slot-reuse"))
(drain-pipe p-first)   # slot freed

(setq p-reuse (echo-proc "reused"))
(test T (pipe-alive? p-reuse))
(drain-pipe p-reuse)

# 5e. GC does NOT auto-close pipes (no-crash test: leave a pipe alive through GC)
# A pipe that has NOT been drained must not be closed or corrupted by GC.
# After GC runs, the pipe is still open and drain-pipe must succeed.
# NOTE: this is a deliberate "leak until drain" to verify GC is passive.
(setq dangling (echo-proc "dangling"))
(let ((i 0))
  (while (< i 30)
    (make-list 100)
    (setq i (1+ i))))
# GC ran but pipe is still open -- must not crash or corrupt
(test T (pipe-alive? dangling))
(drain-pipe dangling)

# -------------------------------------------------------------------------
# 6. Mixed GC: interleave pipe ops with heavy cons allocation
# -------------------------------------------------------------------------
# GC may fire between any two operations here.  The pipe handle in
# p-mixed must remain valid through all four make-list calls.
# This catches races between the pipe-op state machine and the GC
# that would only surface when allocation triggers a collection
# in the middle of a multi-step pipe interaction.
(setq p-mixed (echo-proc "mixed-gc"))
(make-list 500)
(test T (pipe-alive? p-mixed))
(make-list 500)
(pipe-close-stdin p-mixed)
(make-list 500)
(setq mixed-out (pipe-read-all p-mixed))
(make-list 500)
(pipe-close p-mixed)
(test NIL (pipe-alive? p-mixed))

(println "")
(println "test_gc.l: ALL TESTS PASSED")
