# 12-samples.l -- Live Coding Music: Sampler Demo
# Drum sequencer + melodic piano sampler using WAV sample playback.
# Run setup.ps1 first to generate deps/samples/.
#
# Controls:
#   Arrows     -- navigate grid  (track / step)
#   Space      -- toggle step    [melody: C4 if empty, clear if set]
#   ,  /  .    -- melody track: shift selected note down / up 1 semitone
#   [  /  ]    -- BPM -5 / +5
#   P          -- pause / resume
#   Q / Esc    -- quit

(println "=== Live Coding: Sampler ===")

(if (not (bound? 'sdl-window))
  (prog (println "Graphics not available") (quit 0)))

(if (not (sdl-window "L -- Live Coding Music" 800 500))
  (prog (println "Window creation failed") (quit 0)))

(if (not (audio-init))
  (prog (println "Audio init failed") (quit 0)))

(audio-volume 85)

(let ((nd (samples-load-dir "deps/samples/drums"))
      (np (samples-load-dir "deps/samples/piano")))
  (println (list "Drums loaded:" nd "  Piano loaded:" np))
  (if (= nd 0)
    (println "  NOTE: no drum samples found -- run setup.ps1 first")))

# -----------------------------------------------------------------------
# Utility
# -----------------------------------------------------------------------
(de nth (lst n)
  (if (= n 0) (car lst) (nth (cdr lst) (1- n))))

(de set-nth (lst n v)
  (if (= n 0)
    (cons v (cdr lst))
    (cons (car lst) (set-nth (cdr lst) (1- n) v))))

(de clamp (x lo hi)
  (if (< x lo) lo (if (> x hi) hi x)))

# -----------------------------------------------------------------------
# Note table: chromatic C3 ... C5 (25 notes, semitone indices 0..24)
# -----------------------------------------------------------------------
(setq g-notes (list
  'C3 'Cs3 'D3 'Ds3 'E3 'F3 'Fs3 'G3 'Gs3 'A3 'As3 'B3
  'C4 'Cs4 'D4 'Ds4 'E4 'F4 'Fs4 'G4 'Gs4 'A4 'As4 'B4 'C5))

(setq g-note-st (list
  (cons 'C3 0)  (cons 'Cs3 1)  (cons 'D3 2)  (cons 'Ds3 3)
  (cons 'E3 4)  (cons 'F3 5)   (cons 'Fs3 6)  (cons 'G3 7)
  (cons 'Gs3 8) (cons 'A3 9)   (cons 'As3 10) (cons 'B3 11)
  (cons 'C4 12) (cons 'Cs4 13) (cons 'D4 14)  (cons 'Ds4 15)
  (cons 'E4 16) (cons 'F4 17)  (cons 'Fs4 18) (cons 'G4 19)
  (cons 'Gs4 20)(cons 'A4 21)  (cons 'As4 22) (cons 'B4 23)
  (cons 'C5 24)))

(de note-semitone (n)
  (let ((p (assoc n g-note-st)))
    (if p (cdr p) 12)))

(de note-find-loop (lst i target)
  (if (not lst) 12
    (if (= (car lst) target) i
      (note-find-loop (cdr lst) (1+ i) target))))

(de note-find (n) (note-find-loop g-notes 0 n))

# -----------------------------------------------------------------------
# Sequencer state
# -----------------------------------------------------------------------
(setq g-steps        16)
(setq g-bpm          128)
(setq g-cur-step     0)
(setq g-cursor-track 0)
(setq g-cursor-step  0)
(setq g-paused       NIL)
(setq g-running      T)

# Drum patterns (1 = hit, 0 = rest)
(setq g-kick  (list 1 0 0 0  1 0 0 0  1 0 0 0  1 0 1 0))
(setq g-snare (list 0 0 0 0  1 0 0 0  0 0 0 0  1 0 0 0))
(setq g-hihat (list 1 0 1 0  1 0 1 0  1 0 1 0  1 0 1 1))
(setq g-ohat  (list 0 0 0 0  0 0 0 0  0 0 0 0  0 0 0 0))

# Melody pattern: NIL = rest, else a note symbol such as 'C4
(setq g-melody (list
  'C4  NIL  NIL  NIL
  'E4  NIL  'G4  NIL
  'A4  NIL  NIL  NIL
  'C5  NIL  'G4  NIL))

(de get-track (t)
  (cond
    ((= t 0) g-kick)
    ((= t 1) g-snare)
    ((= t 2) g-hihat)
    ((= t 3) g-ohat)
    (T       g-melody)))

(de set-track (t pat)
  (cond
    ((= t 0) (setq g-kick  pat))
    ((= t 1) (setq g-snare pat))
    ((= t 2) (setq g-hihat pat))
    ((= t 3) (setq g-ohat  pat))
    (T       (setq g-melody pat))))

# -----------------------------------------------------------------------
# Layout
# -----------------------------------------------------------------------
(setq g-cell-w    42)
(setq g-cell-h    60)
(setq g-cell-gap   2)
(setq g-grid-x    80)
(setq g-grid-y    85)
(setq g-n-tracks   5)

# Per-track (r g b) accent colors
(setq g-cols (list
  (list 220  55  55)    # kick   -- red
  (list  55 115 220)    # snare  -- blue
  (list  55 200  85)    # hihat  -- green
  (list  55 200 200)    # ohat   -- cyan
  (list 230 155  40)))  # melody -- amber

# -----------------------------------------------------------------------
# Timing: sdl-ticks based (sample-play bypasses g_audio_stream, so
# audio-pending stays at 0; we time steps ourselves instead)
# -----------------------------------------------------------------------
(setq g-next-ms (sdl-ticks))
(de step-ms () (/ (* 60 1000) (* g-bpm 4)))

# -----------------------------------------------------------------------
# Drawing
# -----------------------------------------------------------------------
(de draw-cell (track step active? playing? cursor?)
  (let* (
    (x  (+ g-grid-x (* step  (+ g-cell-w g-cell-gap))))
    (y  (+ g-grid-y (* track (+ g-cell-h g-cell-gap))))
    (c  (nth g-cols track))
    (r  (nth c 0))
    (g  (nth c 1))
    (b  (nth c 2))
    )
    (if active?
      (prog (sdl-color r g b 255) (sdl-fill x y g-cell-w g-cell-h))
      (prog (sdl-color (/ r 6) (/ g 6) (/ b 6) 255)
            (sdl-fill x y g-cell-w g-cell-h)))
    # white edge bars flash on the currently-playing step
    (if playing?
      (prog (sdl-color 255 255 255 160)
            (sdl-fill x y g-cell-w 4)
            (sdl-fill x (+ y g-cell-h -4) g-cell-w 4)))
    (if cursor?
      (prog (sdl-color 255 255 255 255)
            (sdl-rect x y g-cell-w g-cell-h)))))

(de draw-pitch-bar (step note)
  # White stripe inside melody cell: low note = bottom, high = top.
  (let* (
    (x     (+ g-grid-x (* step (+ g-cell-w g-cell-gap))))
    (y     (+ g-grid-y (* 4   (+ g-cell-h g-cell-gap))))
    (st    (note-semitone note))
    (bar-y (+ y (- (- g-cell-h 4) (/ (* st (- g-cell-h 8)) 24))))
    )
    (sdl-color 255 255 255 220)
    (sdl-fill x bar-y g-cell-w 4)))

(de draw-melody-steps (s)
  (if (< s g-steps)
    (prog
      (let ((note (nth g-melody s)))
        (draw-cell 4 s
          note  # truthy when not NIL
          (= s g-cur-step)
          (and (= g-cursor-track 4) (= s g-cursor-step)))
        (if note (draw-pitch-bar s note)))
      (draw-melody-steps (1+ s)))))

(de draw-drum-steps (track pat s)
  (if (< s g-steps)
    (prog
      (draw-cell track s
        (= (nth pat s) 1)
        (= s g-cur-step)
        (and (= track g-cursor-track) (= s g-cursor-step)))
      (draw-drum-steps track pat (1+ s)))))

(de draw-tracks (t)
  (if (< t g-n-tracks)
    (prog
      (if (= t 4)
        (draw-melody-steps 0)
        (draw-drum-steps t (get-track t) 0))
      (draw-tracks (1+ t)))))

(de draw-labels (t)
  (if (< t g-n-tracks)
    (let* (
      (c  (nth g-cols t))
      (r  (nth c 0))
      (g  (nth c 1))
      (b  (nth c 2))
      (ty (+ g-grid-y (* t (+ g-cell-h g-cell-gap))))
      )
      (sdl-color (/ r 2) (/ g 2) (/ b 2) 255)
      (sdl-fill 4 ty 68 g-cell-h)
      (if (= t g-cursor-track)
        (prog (sdl-color 255 255 255 255)
              (sdl-rect 4 ty 68 g-cell-h)))
      (draw-labels (1+ t)))))

(de draw-step-dots (s)
  (if (< s g-steps)
    (prog
      (if (= s g-cur-step)
        (sdl-color 255 255 255 255)
        (sdl-color 50 50 68 255))
      (sdl-fill (+ 178 (* s 22)) 50 18 14)
      (draw-step-dots (1+ s)))))

(de draw-bpm-bars (b lim)
  (if (< b lim)
    (prog
      (sdl-fill (+ 8 (* b 6)) 8 4 20)
      (draw-bpm-bars (1+ b) lim))))

(de draw-status ()
  (sdl-color 18 18 30 255)
  (sdl-fill 0 0 800 78)
  (draw-step-dots 0)
  (sdl-color 230 155 40 255)
  (draw-bpm-bars 0 (/ g-bpm 10))
  (if g-paused
    (prog (sdl-color 200 50 50 255)
          (sdl-fill 8 36 10 18)
          (sdl-fill 22 36 10 18))))

(de draw-hint ()
  # Bottom bar: track legend + BPM progress strip
  (sdl-color 14 14 22 255)
  (sdl-fill 0 456 800 44)
  # Track color swatches
  (sdl-color 220  55  55 255) (sdl-fill   8 468 130 12)  # kick
  (sdl-color  55 115 220 255) (sdl-fill 142 468 130 12)  # snare
  (sdl-color  55 200  85 255) (sdl-fill 276 468 130 12)  # hihat
  (sdl-color  55 200 200 255) (sdl-fill 410 468 100 12)  # ohat
  (sdl-color 230 155  40 255) (sdl-fill 514 468 100 12)  # melody
  # BPM fill bar (40..300 range → 180px)
  (sdl-color 35 35 55 255)
  (sdl-fill 620 468 172 12)
  (sdl-color 180 120 30 255)
  (sdl-fill 620 468 (clamp (/ (* (- g-bpm 40) 172) 260) 0 172) 12))

# -----------------------------------------------------------------------
# Event handling
# -----------------------------------------------------------------------
(de melody-note-shift (step delta)
  (let ((n (nth g-melody step)))
    (let ((idx (if n (note-find n) (- 12 delta))))
      (setq g-melody (set-nth g-melody step
        (nth g-notes (clamp (+ idx delta) 0 24)))))))

(de handle-key (key)
  (if (or (equal key "Escape") (equal key "q"))
    (setq g-running NIL))
  (if (equal key "p")
    (prog
      (setq g-paused (not g-paused))
      (if (not g-paused) (setq g-next-ms (sdl-ticks)))))
  (if (equal key "Left")
    (setq g-cursor-step (clamp (1- g-cursor-step) 0 (1- g-steps))))
  (if (equal key "Right")
    (setq g-cursor-step (clamp (1+ g-cursor-step) 0 (1- g-steps))))
  (if (equal key "Up")
    (setq g-cursor-track (clamp (1- g-cursor-track) 0 (1- g-n-tracks))))
  (if (equal key "Down")
    (setq g-cursor-track (clamp (1+ g-cursor-track) 0 (1- g-n-tracks))))
  (if (equal key ",")
    (if (= g-cursor-track 4) (melody-note-shift g-cursor-step -1)))
  (if (equal key ".")
    (if (= g-cursor-track 4) (melody-note-shift g-cursor-step  1)))
  (if (equal key "[")
    (setq g-bpm (clamp (- g-bpm 5) 40 300)))
  (if (equal key "]")
    (setq g-bpm (clamp (+ g-bpm 5) 40 300)))
  (if (equal key " ")
    (if (= g-cursor-track 4)
      # Melody: toggle note on/off (default to C4 when enabling)
      (setq g-melody (set-nth g-melody g-cursor-step
        (if (nth g-melody g-cursor-step) NIL 'C4)))
      # Drum: toggle 0/1
      (let ((pat (get-track g-cursor-track)))
        (set-track g-cursor-track
          (set-nth pat g-cursor-step
            (if (= (nth pat g-cursor-step) 1) 0 1)))))))

(de poll-all ()
  (let ((ev (sdl-poll)))
    (if ev
      (prog
        (let ((ev-type (cdr (assoc "type" ev))))
          (if (equal ev-type "quit") (setq g-running NIL))
          (if (equal ev-type "keydown")
            (handle-key (cdr (assoc "key" ev)))))
        (poll-all)))))

# -----------------------------------------------------------------------
# Audio: play one step
# -----------------------------------------------------------------------
(de play-step (s)
  (if (= (nth g-kick  s) 1) (sample-play 'kick))
  (if (= (nth g-snare s) 1) (sample-play 'snare))
  (if (= (nth g-hihat s) 1) (sample-play 'hihat))
  (if (= (nth g-ohat  s) 1) (sample-play 'ohat))
  (let ((note (nth g-melody s)))
    (if note (sample-play note)))
  (sample-flush))

# -----------------------------------------------------------------------
# Main loop
# -----------------------------------------------------------------------
(while g-running
  (poll-all)

  (if (not g-paused)
    (let ((now (sdl-ticks)))
      # Resync if we stalled (e.g. window dragged, debug breakpoint)
      (if (> (- now g-next-ms) 500) (setq g-next-ms now))
      (if (>= now g-next-ms)
        (prog
          (play-step g-cur-step)
          (setq g-cur-step (mod (1+ g-cur-step) g-steps))
          (setq g-next-ms (+ g-next-ms (step-ms)))))))

  (sdl-color 10 10 18 255)
  (sdl-clear)
  (draw-status)
  (draw-labels 0)
  (draw-tracks 0)
  (draw-hint)
  (sdl-present)
  (sdl-delay 16))

(audio-stop-all)
(sdl-quit)     # close window first (immediate)
(audio-quit)   # then stop audio
(println "Done.")
