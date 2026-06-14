# livecode.l -- Sam Aaron temporal model for live coding music
#
# Core idea: schedule audio events ahead of time using absolute
# timestamps. The mixer thread plays them at the exact right
# moment regardless of interpreter jitter.
#
# Primitives used:
#   (audio-time)           → current mixer position in ms
#   (audio-at time-ms 'sample [speed]) → schedule at absolute time
#   (sample-play 'sample)  → play immediately
#   (audio-tone wave freq dur [vol]) → synthesize tone

# ---- Tempo ----

(setq *bpm* 120)
(setq *beat-num* 0)

(de bpm (n) (setq *bpm* n))
(de beat-ms () (/ 60000 *bpm*))

# ---- Scheduling: Sam Aaron ahead-of-time model ----
#
# (at-beat offset 'sample) schedules a sample at the given beat
# offset from the current beat time. The mixer plays it at the
# exact right moment.

(setq *beat-origin* 0)

(de beat-time (beat)
  (+ *beat-origin* (* beat (beat-ms))))

# Schedule a sample at a specific beat time (absolute ms)
(de play-at (time-ms name)
  (audio-at time-ms name))

# ---- Pattern helpers ----

(setq *pattern-fn* NIL)

# Define what happens each beat
(de pattern (fn)
  (setq *pattern-fn* fn))

# Convenience: play drum sounds
(de kick ()    (setq *pattern-fn* (lambda (t b) (play-at t 'kick))))
(de snare ()   (setq *pattern-fn* (lambda (t b) (play-at t 'snare))))
(de hat ()     (setq *pattern-fn* (lambda (t b) (play-at t 'hihat))))

# Play a note
(de note (name)
  (setq *pattern-fn* (lambda (t b) (play-at t name))))

# Sequence: cycle through a list of samples each beat (layers on top)
(de seq (. notes)
  (let ((ns notes)
        (len (length notes)))
    (layer (lambda (t b)
      (let ((n (car (nth (mod b len) ns))))
        (when n (play-at t n)))))))

# Every Nth beat, play the given sample
(de every (n name)
  (let ((prev *pattern-fn*))
    (setq *pattern-fn*
      (lambda (t b)
        (when prev (prev t b))
        (when (= (mod b n) 0)
          (play-at t name))))))

# Layer: add a pattern on top of existing
(de layer (fn)
  (let ((prev *pattern-fn*))
    (setq *pattern-fn*
      (lambda (t b)
        (when prev (prev t b))
        (fn t b)))))

# Pause: mute but keep pattern (resume to continue)
(de pause ()
  (setq *paused* T)
  (audio-stop-all))

# Resume after pause
(de resume ()
  (setq *paused* NIL)
  (setq *beat-origin* (- (audio-time) (* *beat-num* (beat-ms)))))

# Stop: clear pattern and silence
(de stop ()
  (setq *pattern-fn* NIL)
  (setq *paused* NIL)
  (audio-stop-all))

(setq *paused* NIL)

# ---- Beat loop (runs in background via timer) ----

(setq *loop-running* NIL)

(de start-loop ()
  (setq *beat-origin* (audio-time))
  (setq *beat-num* 0)
  (setq *loop-running* T)
  (println "Beat loop started at BPM" *bpm*))

(de stop-loop ()
  (setq *loop-running* NIL)
  (println "Beat loop stopped"))

# Called each iteration of the REPL to advance beats
(de tick ()
  (when (and *loop-running* (not *paused*))
    (let* ((now  (audio-time))
           (next (beat-time *beat-num*)))
      # Schedule ahead: if we're within 100ms of the next beat, fire it
      (when (> now (- next 100))
        (when *pattern-fn*
          (*pattern-fn* next *beat-num*))
        (setq *beat-num* (1+ *beat-num*))))))
