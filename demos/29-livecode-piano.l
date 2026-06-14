# ============================================================
# 29-livecode-piano.l -- Live coding with real piano samples
#
# Uses Salamander Grand Piano samples (run setup.sh/setup.ps1
# to download). Falls back to synth piano if not available.
#
# Run: L demos/29-livecode-piano.l
#
# Try these at the > prompt:
#
#   # Simple arpeggios
#   (seq 'C4 'E4 'G4 'C5)
#
#   # Minor key
#   (seq 'A3 'C4 'E4 'A4 'E4 'C4)
#
#   # Add drums underneath
#   (every 1 'kick)
#   (every 4 'snare)
#   (every 2 'hihat)
#
#   # Change tempo
#   (bpm 90)
#
#   # Chord on every beat
#   (layer (lambda (t b)
#     (play-at t 'C4) (play-at t 'E4) (play-at t 'G4)))
#
#   # Rhythmic pattern
#   (pattern (lambda (t b)
#     (when (= (mod b 8) 0) (play-at t 'kick))
#     (when (= (mod b 8) 4) (play-at t 'snare))
#     (when (= (mod b 2) 0) (play-at t 'hihat))
#     (let ((melody (list 'C4 'D4 'E4 'G4 'A4 'G4 'E4 'D4)))
#       (play-at t (car (nth (mod b 8) melody))))))
#
#   (pause)    -- mute, keep pattern
#   (resume)   -- unmute
#   (stop)     -- clear everything
#   Ctrl-C     -- exit
# ============================================================

(load "lib/livecode.l")

(println "=== L Live Coding -- Piano ===")
(println "")

(if (not (audio-init))
  (prog (println "Audio init failed") (quit 1)))

(audio-volume 80)

# Load all available samples
(setq *n-drums* 0)
(setq *n-piano* 0)

# Synth samples
(let ((n (samples-load-dir "deps/samples/drums")))
  (setq *n-drums* (+ *n-drums* n)))
(let ((n (samples-load-dir "deps/samples/piano")))
  (setq *n-piano* (+ *n-piano* n)))

# 808 drums (override synth drums with punchier versions)
(let ((n (samples-load-dir "deps/samples/drums-808")))
  (setq *n-drums* (+ *n-drums* n)))

# Real piano (override synth piano -- loaded last so it wins)
(let ((n (samples-load-dir "deps/samples/piano-real")))
  (when (> n 0)
    (println "  Real piano samples loaded!" n "notes")
    (setq *n-piano* (+ *n-piano* n))))

(println "Loaded" *n-drums* "drum +" *n-piano* "piano samples")
(when (= *n-drums* 0)
  (println "No samples -- run setup.sh or setup.ps1")
  (quit 1))

(println "")
(println "Commands:")
(println "  (seq 'C4 'E4 'G4 'C5)              -- arpeggio")
(println "  (seq 'A3 'C4 'E4 'A4 'E4 'C4)      -- minor")
(println "  (every 1 'kick)                      -- drums")
(println "  (every 4 'snare)                     -- snare accent")
(println "  (bpm 90)                             -- slow down")
(println "  (pause) (resume) (stop)              -- control")
(println "  Ctrl-C                               -- exit")
(println "")

# Start with a gentle piano pattern
(bpm 100)
(start-loop)

(pattern (lambda (t b)
  # Drums: kick on 1&5, snare on 3&7, hihat on evens
  (when (= (mod b 8) 0) (play-at t 'kick))
  (when (= (mod b 8) 4) (play-at t 'kick))
  (when (= (mod b 8) 2) (play-at t 'snare))
  (when (= (mod b 8) 6) (play-at t 'snare))
  (when (= (mod b 2) 0) (play-at t 'hihat))
  # Piano: C major arpeggio
  (let ((notes (list 'C4 'E4 'G4 'C5 'G4 'E4)))
    (play-at t (car (nth (mod b (length notes)) notes))))))

(println "Playing drums + piano at 100 BPM. Type to change.")
(println "")

# ---- Raw-mode REPL (same as 28-livecode.l) ----
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
        ((or (= b 8) (= b 127))
         (when (> (length (unpack *line-buf*)) 0)
           (setq *line-buf* (sub *line-buf* 0 (- (length (unpack *line-buf*)) 1)))
           (prin (char 8) " " (char 8)) (flush)))
        ((or (= b 3) (= b 4))
         (livecode-cleanup)
         (quit 0))
        ((and (>= b 32) (<= b 126))
         (setq *line-buf* (pack (list *line-buf* (char b))))
         (prin (char b)) (flush))))))
