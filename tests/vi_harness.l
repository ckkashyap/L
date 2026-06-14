# tests/vi_harness.l -- shared helpers for vi integration tests.
# Load with (load "tests/vi_harness.l"). Not run directly.

(load "lib/ds.l")
(load "lib/string.l")
(load "demos/vi/buffer.l")
(load "demos/vi/state.l")
(load "demos/vi/cursor.l")
(load "demos/vi/ops.l")
(load "demos/vi/search.l")
(load "demos/vi/visual.l")
(load "demos/vi/macro.l")
(load "demos/vi/file.l")
(load "demos/vi/cmd.l")
(load "demos/vi/mode.l")

# (check expected actual label) -- print OK/FAIL; quit 1 on failure.
(de check (expected actual label)
  (if (equal expected actual)
    (println "OK:" label)
    (prog
      (println "FAIL:" label)
      (println "  expected:" expected)
      (println "  actual  :" actual)
      (quit 1))))

# Drain macro-inject: feed queued keys to disp until the queue is empty.
# Restores harness-authoritative macros and forces macro-rec=NIL after each resume.
(de vi-drain-inject (state disp saved-macros)
  (let ((inject (s-get state 'macro-inject)))
    (if (null inject)
      state
      (prog
        (setq state (s-set state 'macro-inject NIL))
        (let ((ks inject))
          (while ks
            (setq state (co-resume disp (car ks)))
            (setq state (s-set state 'macros   saved-macros))
            (setq state (s-set state 'macro-rec NIL))
            (setq ks (cdr ks))))
        (vi-drain-inject state disp saved-macros)))))

# Patch state with harness-authoritative macro fields.
# h-mac-rec is the harness intended macro-rec value:
#   - If h-mac-rec is explicitly provided (non-NIL sentinel handled by caller),
#     set macro-rec to it.
#   - macros always restored from saved-macros.
(de vi-patch-mac (state saved-macros h-mac-rec)
  (s-set (s-set state 'macros saved-macros) 'macro-rec h-mac-rec))

# (vi-run text keys) -> final state.
# Creates a fresh buffer+state, primes the dispatch coroutine, then feeds
# each key in order. The harness owns macro state authoritatively:
#
#   macro-rec lifecycle:
#     NIL  ->  (coroutine gets "q"+"a") -> idx set in coroutine, harness reads it back
#          ->  (coroutine gets recorded keys) -> harness restores idx each time
#          ->  (harness intercepts stop "q") -> h-mac-rec forced to NIL; coroutine
#              still holds stale idx but harness forces NIL after every co-resume
#
#   macros lifecycle:
#     harness calls macro-add-key when h-mac-rec is set; restores macros after co-resume
#
#   "@" + register lifecycle:
#     harness buffers "@", waits for register key, dispatches both to coroutine
#     (keeping its pfx in sync), then overrides macro-inject from h-macros
(de vi-run (text keys)
  (let* ((buf          (buf-from-string text))
         (st0          (make-state buf))
         (disp         (make-mode-dispatch st0))
         (state        (co-resume disp NIL))   # prime: runs to first (yield state)
         (ks           keys)
         (h-mac-rec    NIL)    # harness-authoritative macro-rec
         (h-macros     (s-get state 'macros))  # harness-authoritative macros
         (rec-active   NIL)    # T while recording (h-mac-rec is set)
         (rec-stopped  NIL)    # T after stop until coroutine starts new recording
         (q-pending    NIL)    # T after "q" in normal mode (awaiting register key)
         (at-pending   NIL)    # T after "@" in normal mode (awaiting register key)
         (harness-count 0)     # count prefix for @-with-count
         (pending-count 1))    # count captured when "@" was seen
    (while ks
      (let ((k (car ks)))
        (setq ks (cdr ks))
        (cond
          # ── Stop recording ──────────────────────────────────────────────────
          ((and (equal k "q")
                rec-active
                (equal (s-get state 'mode) 'normal))
           (setq state    (stop-macro-record state))
           (setq h-macros (s-get state 'macros))
           (setq h-mac-rec NIL)
           (setq rec-active  NIL)
           (setq rec-stopped T))

          # ── Register key after "@": inject from harness macros ──────────────
          (at-pending
           (let* ((idx    (reg-idx k))
                  (mkeys  (or (pvec-get h-macros idx) NIL))
                  (inject (pm--repeat mkeys (max 1 pending-count) NIL)))
             # Dispatch "@" to keep coroutine pfx in sync
             (setq state (co-resume disp "@"))
             (setq state (vi-patch-mac state h-macros h-mac-rec))
             # Dispatch register key (play-macro fires with stale macros -> inject=NIL)
             (setq state (co-resume disp k))
             (setq state (vi-patch-mac state h-macros h-mac-rec))
             # Override inject with harness-computed keys
             (setq state (s-set state 'macro-inject inject))
             # Drain injected keys
             (setq state (vi-drain-inject state disp h-macros))
             (setq at-pending    NIL)
             (setq pending-count 1)))

          # ── Register key after "q": start-macro-record will fire ─────────────
          (q-pending
           (let ((saved-h-macros h-macros))
             (setq state (co-resume disp k))
             # Coroutine ran start-macro-record: read back the new macro-rec idx
             (setq h-mac-rec  (s-get state 'macro-rec))
             (setq h-macros   saved-h-macros)
             # Restore harness macros (start-macro-record cleared the register)
             (setq state      (vi-patch-mac state h-macros h-mac-rec))
             (setq rec-active  T)
             (setq rec-stopped NIL)
             (setq q-pending   NIL)))

          # ── Normal key dispatch ──────────────────────────────────────────────
          (T
           (prog
             # Record key if macro-rec is active (q is never recorded)
             (when (and rec-active (not (equal k "q")))
               (setq state    (macro-add-key state k))
               (setq h-macros (s-get state 'macros)))
             # Detect "@" in normal mode (not recording): buffer it
             (cond
               ((and (equal k "@")
                     (not rec-active)
                     (equal (s-get state 'mode) 'normal))
                (setq pending-count (if (> harness-count 0) harness-count 1))
                (setq harness-count 0)
                (setq at-pending T))
               # Detect "q" in normal mode (not recording, not already stopped):
               # buffer -- await register key
               ((and (equal k "q")
                     (not rec-active)
                     (equal (s-get state 'mode) 'normal))
                (setq q-pending T)
                # Still dispatch "q" to coroutine so its pfx="q" is set
                (setq state (co-resume disp k))
                (setq state (vi-patch-mac state h-macros h-mac-rec)))
               (T
                # Track count digits for @-with-count
                (if (and (equal (s-get state 'mode) 'normal)
                         (not rec-active)
                         (string? k)
                         (>= (char (car (unpack k))) 49)
                         (<= (char (car (unpack k))) 57))
                  (setq harness-count
                    (+ (* harness-count 10) (- (char (car (unpack k))) 48)))
                  (setq harness-count 0))
                # Dispatch key; restore harness macro state after
                (setq state (co-resume disp k))
                (setq state (vi-patch-mac state h-macros h-mac-rec))
                # Drain any macro-inject
                (setq state (vi-drain-inject state disp h-macros)))))))))
    state))

# Convenience accessors
(de vi-text   (state) (buf-to-string (s-get state 'buffer)))
(de vi-cursor (state) (s-get state 'cursor))
(de vi-mode   (state) (s-get state 'mode))
