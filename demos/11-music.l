# ============================================================
# 11-music.l -- 16-step drum sequencer with synthesized audio
#
# What this covers:
#   1. Sequencer architecture (grid state, patterns)
#   2. BPM-based step timing
#   3. Grid rendering (active/inactive/cursor cells)
#   4. Audio synthesis via audio-drums and audio-tone
#   5. Keyboard controls for live performance
#
# Run:  L demos/11-music.l
# Spec: L.spec.txt Part XIV -- Graphics and Audio
#
# Controls:
#   Arrow keys   -- move cursor (track / step)
#   Space        -- toggle selected step on/off
#   [ / ]        -- BPM -5 / +5
#   P            -- pause / resume playback
#   Escape / Q   -- quit
# ============================================================

# ------------------------------------------------------------
# Architecture
#
# This is a 16-step drum sequencer: a grid of on/off buttons,
# one column per step, one row per instrument. State is a list
# of lists of integers (1 = hit, 0 = rest). There are four
# tracks: kick, snare, hi-hat, and bass. The bass track also
# carries a frequency table so each active step can play a
# pitched note rather than a fixed drum sound.
# ------------------------------------------------------------
(println "=== Live Music Coding Demo ===")

(if (not (bound? 'sdl-window))
  (prog (println "Graphics not available") (quit 0)))

(if (not (sdl-window "L -- Live Music Coder" 800 480))
  (prog (println "Window creation failed") (quit 0)))

(if (not (audio-init))
  (prog (println "Audio init failed") (quit 0)))

# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------
(de nth (lst n)
  (if (= n 0) (car lst) (nth (cdr lst) (1- n))))

(de set-nth (lst n v)
  (if (= n 0)
    (cons v (cdr lst))
    (cons (car lst) (set-nth (cdr lst) (1- n) v))))

(de toggle-nth (lst n)
  (set-nth lst n (if (= (nth lst n) 0) 1 0)))

(de clamp (x lo hi)
  (if (< x lo) lo (if (> x hi) hi x)))

# ---------------------------------------------------------------------------
# Global sequencer state  (de functions can access these as symbol values)
# ---------------------------------------------------------------------------
(setq g-steps        16)
(setq g-bpm          120)
(setq g-cur-step     0)
(setq g-cursor-track 0)
(setq g-cursor-step  0)
(setq g-paused       NIL)
(setq g-running      T)

# 16-step patterns: 1 = hit, 0 = rest
(setq g-kick  (list 1 0 0 0  1 0 0 0  1 0 0 0  1 0 0 0))
(setq g-snare (list 0 0 0 0  1 0 0 0  0 0 0 0  1 0 0 0))
(setq g-hat   (list 1 0 1 0  1 0 1 0  1 0 1 0  1 0 1 0))
(setq g-bass  (list 1 0 0 0  0 0 1 0  0 0 0 0  1 0 0 0))

# Bass note frequencies (Hz) -- A2 minor groove
(setq g-bass-freqs (list
  110 0 0 0   0 0 138 0   0 0 0 0   110 0 0 0))

# ---------------------------------------------------------------------------
# Layout constants (global)
# ---------------------------------------------------------------------------
(setq g-cell-w    42)
(setq g-cell-h    72)
(setq g-cell-gap   2)
(setq g-grid-x    80)   # left edge of step grid
(setq g-grid-y    90)   # top edge of step grid

(setq g-track-cols (list
  (list 220  60  60)    # kick  -- red
  (list  60 120 220)    # snare -- blue
  (list  60 200  90)    # hihat -- green
  (list 220 180  50)))  # bass  -- yellow

# ---------------------------------------------------------------------------
# Timing
#
# BPM (beats per minute) controls how fast the sequencer
# advances. `step-ms` converts BPM to milliseconds per step.
# The formula `(/ (* 60 1000) (* bpm 4))` treats each of the
# 16 steps as a sixteenth note: there are 4 sixteenth notes
# per beat, so one step lasts (60000 / (bpm * 4)) ms. At
# 120 BPM that is 125 ms per step (8 steps per second).
# The sequencer advances exactly one step per tick; the main
# loop checks `audio-pending` to stay ~2 steps ahead of the
# audio playback cursor to avoid glitches.
# ---------------------------------------------------------------------------
(de step-ms () (/ (* 60 1000) (* g-bpm 4)))

# ---------------------------------------------------------------------------
# Drawing helpers
#
# Grid rendering -- each step is a colored rectangle. Active
# steps are drawn at full RGB brightness; inactive steps use
# one-fifth of the channel values (dim). The currently playing
# step (g-cur-step) is highlighted with thin white bars at the
# top and bottom of every cell in that column. The cursor cell
# (g-cursor-track / g-cursor-step) gets a white outline drawn
# via `sdl-rect` (hollow rectangle) on top of the filled cell.
# ---------------------------------------------------------------------------
(de draw-cell (track step active? playing? cursor?)
  (let* (
    (x   (+ g-grid-x (* step  (+ g-cell-w g-cell-gap))))
    (y   (+ g-grid-y (* track (+ g-cell-h g-cell-gap))))
    (col (nth g-track-cols track))
    (r   (nth col 0))
    (g   (nth col 1))
    (b   (nth col 2))
    )
    (if active?
      (prog (sdl-color r g b 255) (sdl-fill x y g-cell-w g-cell-h))
      (prog (sdl-color (/ r 5) (/ g 5) (/ b 5) 255)
            (sdl-fill x y g-cell-w g-cell-h)))
    (if playing?
      (prog (sdl-color 255 255 255 200)
            (sdl-fill x y g-cell-w 4)
            (sdl-fill x (+ y g-cell-h -4) g-cell-w 4)))
    (if cursor?
      (prog (sdl-color 255 255 255 255)
            (sdl-rect x y g-cell-w g-cell-h)))))

(de draw-grid-steps (track pat s)
  (if (< s g-steps)
    (prog
      (draw-cell track s
        (= (nth pat s) 1)
        (= s g-cur-step)
        (and (= track g-cursor-track) (= s g-cursor-step)))
      (draw-grid-steps track pat (1+ s)))))

(de draw-grid-tracks (t pats)
  (if (< t 4)
    (prog
      (draw-grid-steps t (car pats) 0)
      (draw-grid-tracks (1+ t) (cdr pats)))))

(de draw-labels-loop (t cols)
  (if (< t 4)
    (let* (
      (col (car cols))
      (r   (nth col 0))
      (g   (nth col 1))
      (b   (nth col 2))
      (ty  (+ g-grid-y (* t (+ g-cell-h g-cell-gap))))
      )
      (sdl-color r g b 255)
      (sdl-fill 10 ty 62 g-cell-h)
      (if (= t g-cursor-track)
        (prog (sdl-color 255 255 255 255)
              (sdl-rect 10 ty 62 g-cell-h)))
      (draw-labels-loop (1+ t) (cdr cols)))))

(de draw-step-dots (s)
  (if (< s g-steps)
    (prog
      (if (= s g-cur-step)
        (sdl-color 255 255 255 255)
        (sdl-color 70 70 80 255))
      (sdl-fill (+ 200 (* s 24)) 55 18 18)
      (draw-step-dots (1+ s)))))

(de draw-bpm-bars (b limit)
  (if (< b limit)
    (prog
      (sdl-fill (+ 10 (* b 6)) 10 4 22)
      (draw-bpm-bars (1+ b) limit))))

(de draw-status ()
  (sdl-color 25 25 38 255)
  (sdl-fill 0 0 800 82)
  (draw-step-dots 0)
  (sdl-color 255 200 50 255)
  (draw-bpm-bars 0 (/ g-bpm 10))
  (if g-paused
    (prog (sdl-color 220 60 60 255)
          (sdl-fill 10 40 10 20)
          (sdl-fill 25 40 10 20))))

(de draw-hint ()
  # Simple coloured legend at the bottom
  (sdl-color 18 18 28 255)
  (sdl-fill 0 440 800 40)
  # Arrow hint
  (sdl-color 120 120 140 255)
  (sdl-fill  12 452 22 10)
  (sdl-fill  38 452 22 10)
  # Space hint
  (sdl-color 180 180 200 255)
  (sdl-fill  70 452 32 10)
  # BPM hints
  (sdl-color 200 160 40 255)
  (sdl-fill 112 452 14 10)
  (sdl-fill 130 452 14 10))

# ---------------------------------------------------------------------------
# Keyboard controls
#
# Arrow keys navigate the cursor through the grid (clamped to
# valid track 0–3 and step 0–15 ranges). Space toggles the
# step under the cursor on or off by mutating the appropriate
# pattern list via `toggle-nth`. [ and ] decrease/increase BPM
# by 5 (clamped to 40–300). P pauses or unpauses playback
# without stopping the render loop. Escape or Q quit cleanly.
# ---------------------------------------------------------------------------
(de handle-key (key)
  (if (equal key "Escape") (setq g-running NIL))
  (if (equal key "q")      (setq g-running NIL))
  (if (equal key "p")      (setq g-paused (not g-paused)))
  (if (equal key "Left")
    (setq g-cursor-step (clamp (1- g-cursor-step) 0 (1- g-steps))))
  (if (equal key "Right")
    (setq g-cursor-step (clamp (1+ g-cursor-step) 0 (1- g-steps))))
  (if (equal key "Up")
    (setq g-cursor-track (clamp (1- g-cursor-track) 0 3)))
  (if (equal key "Down")
    (setq g-cursor-track (clamp (1+ g-cursor-track) 0 3)))
  (if (equal key "[")
    (setq g-bpm (clamp (- g-bpm 5) 40 300)))
  (if (equal key "]")
    (setq g-bpm (clamp (+ g-bpm 5) 40 300)))
  (if (equal key " ")
    (cond
      ((= g-cursor-track 0) (setq g-kick  (toggle-nth g-kick  g-cursor-step)))
      ((= g-cursor-track 1) (setq g-snare (toggle-nth g-snare g-cursor-step)))
      ((= g-cursor-track 2) (setq g-hat   (toggle-nth g-hat   g-cursor-step)))
      (T                    (setq g-bass  (toggle-nth g-bass  g-cursor-step))))))

(de poll-all-events ()
  (let ((ev (sdl-poll)))
    (if ev
      (prog
        (let ((type (cdr (assoc "type" ev))))
          (if (equal type "quit") (setq g-running NIL))
          (if (equal type "keydown")
            (handle-key (cdr (assoc "key" ev)))))
        (poll-all-events)))))

# ---------------------------------------------------------------------------
# Audio synthesis
#
# Each instrument maps to a synthesis call. `audio-drums` takes
# a bitmask of active tracks and the step duration in ms; it
# synthesizes short percussive envelopes: kick is a pitched
# sine sweep, snare adds filtered noise, hi-hat is high-frequency
# noise. The bitmask uses bit 0 = kick, bit 1 = snare, bit 2 = hat.
#
# Bass steps call `audio-tone` with channel 2, the frequency
# from g-bass-freqs (Hz), duration, and amplitude (55). This
# plays a short sine-wave envelope at the given pitch, producing
# a plucked bass sound.
# ---------------------------------------------------------------------------
(de play-step (s dur)
  (let* (
    (k    (nth g-kick  s))
    (sn   (nth g-snare s))
    (h    (nth g-hat   s))
    (ba   (nth g-bass  s))
    (mask (+ (* k 1) (* sn 2) (* h 4)))
    )
    (if (> mask 0) (audio-drums mask dur))
    (if (= ba 1)
      (let ((freq (nth g-bass-freqs s)))
        (if (> freq 0)
          (audio-tone 2 freq dur 55))))))

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
(while g-running
  (poll-all-events)

  # Queue audio for upcoming steps (stay ~2 steps ahead)
  (if (not g-paused)
    (let ((dur (step-ms)))
      (if (< (audio-pending) (* dur 2))
        (prog
          (play-step g-cur-step dur)
          (setq g-cur-step (mod (1+ g-cur-step) g-steps))))))

  # Render frame
  (sdl-color 12 12 18 255)
  (sdl-clear)
  (draw-status)
  (draw-labels-loop 0 g-track-cols)
  (draw-grid-tracks 0 (list g-kick g-snare g-hat g-bass))
  (draw-hint)
  (sdl-present)
  (sdl-delay 16))

(sdl-quit)     # close window first (immediate)
(audio-quit)   # then stop audio
(println "Done.")
