# ============================================================
# 28-livecode.l -- Live Coding Music (Sonic Pi / Extempore style)
#
# What this covers:
#   1. Sam Aaron temporal model: ahead-of-time scheduling
#   2. Background mixer thread with timestamped voices
#   3. Interactive REPL: change patterns while music plays
#   4. Beat-synced sample playback with zero drift
#
# Run:  L demos/28-livecode.l
#
# Then type expressions at the > prompt:
#   (kick)                    -- kick on every beat
#   (every 2 'snare)          -- snare on beats 0, 2, 4...
#   (seq 'C4 'E4 'G4 'C5)    -- cycle through notes
#   (bpm 140)                 -- change tempo (takes effect next beat)
#   (stop)                    -- silence
#   (quit 0)                  -- exit
#
# Requires: run setup.ps1 first to generate audio samples
# ============================================================

(load "lib/livecode.l")

(println "=== L Live Coding ===")
(println "")

# ---- Load samples ----
(if (not (audio-init))
  (prog (println "Audio init failed") (quit 1)))

(audio-volume 80)

# Load all available sample packs
(setq *total-drums* 0)
(setq *total-piano* 0)

# Synth samples (from setup)
(let ((nd (samples-load-dir "deps/samples/drums"))
      (np (samples-load-dir "deps/samples/piano")))
  (setq *total-drums* (+ *total-drums* nd))
  (setq *total-piano* (+ *total-piano* np)))

# 808 drum kit (from setup)
(let ((n (samples-load-dir "deps/samples/drums-808")))
  (setq *total-drums* (+ *total-drums* n)))

# Salamander Grand Piano (from setup)
(let ((n (samples-load-dir "deps/samples/piano-real")))
  (setq *total-piano* (+ *total-piano* n)))

(println "Loaded" *total-drums* "drum samples," *total-piano* "piano samples")
(when (= *total-drums* 0)
  (println "  No samples found -- run setup.sh or setup.ps1 first")
  (quit 1))

(println "")
(println "Commands:")
(println "  (kick)                 -- kick on every beat")
(println "  (snare)                -- snare on every beat")
(println "  (hat)                  -- hihat on every beat")
(println "  (note 'C4)             -- play a note each beat")
(println "  (seq 'C4 'E4 'G4 'C5) -- cycle through notes")
(println "  (every N 'sample)      -- play sample every Nth beat")
(println "  (layer (lambda (t b) (when (= (mod b 4) 0) (play-at t 'kick))))")
(println "  (bpm 140)              -- change tempo")
(println "  (pause)                -- mute (keep pattern)")
(println "  (resume)               -- resume after pause")
(println "  (stop)                 -- clear pattern + silence")
(println "  (quit 0)               -- exit")
(println "")

# ---- Start the beat loop ----
(bpm 120)
(start-loop)

# ---- Default pattern: simple 4/4 beat ----
(pattern
  (lambda (t b)
    (play-at t 'kick)
    (when (= (mod b 2) 0) (play-at t 'hihat))
    (when (= (mod b 4) 2) (play-at t 'snare))))

(println "")
(println "Playing default 4/4 pattern. Type expressions to change it.")
(println "")

# ---- Interactive REPL with raw mode + non-blocking reads ----
# Terminal is in raw mode: each keystroke arrives immediately.
# read-byte-timeout returns NIL after 20ms if no input, letting
# tick() run continuously between keystrokes.

(raw-mode T)

(setq *line-buf* "")
(setq *prompt-shown* NIL)

(de livecode-cleanup ()
  (raw-mode NIL)
  (audio-stop-all)
  (println ""))

(while T
  (tick)

  (when (not *prompt-shown*)
    (prin "> ") (flush)
    (setq *prompt-shown* T))

  (let ((b (read-byte-timeout 20)))
    (when b
      (cond
        # Enter (13 or 10): eval the line
        ((or (= b 13) (= b 10))
         (println "")
         (let ((line (trim *line-buf*)))
           (setq *line-buf* "")
           (setq *prompt-shown* NIL)
           (when (> (length (unpack line)) 0)
             (catch 'repl-error
               (let ((expr (read-from-string line)))
                 (let ((result (eval expr)))
                   (when result (println result))))))))
        # Backspace (8 or 127)
        ((or (= b 8) (= b 127))
         (when (> (length (unpack *line-buf*)) 0)
           (setq *line-buf* (sub *line-buf* 0 (- (length (unpack *line-buf*)) 1)))
           (prin (char 8) " " (char 8)) (flush)))
        # Ctrl-C (3) or Ctrl-D (4): quit
        ((or (= b 3) (= b 4))
         (livecode-cleanup)
         (quit 0))
        # Regular printable character
        ((and (>= b 32) (<= b 126))
         (setq *line-buf* (pack (list *line-buf* (char b))))
         (prin (char b)) (flush))))))
