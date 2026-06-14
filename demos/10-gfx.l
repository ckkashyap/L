# ============================================================
# 10-gfx.l -- Native graphics demo: window, renderer, shapes
#
# What this covers:
#   1. Native window creation (Win32 GDI / X11)
#   2. Event loop (keyboard / quit)
#   3. Drawing primitives: fill, circle, line
#   4. Frame presentation at ~60 fps
#
# Run:  L demos/10-gfx.l
# Spec: L.spec.txt Part XIV -- Graphics and Audio
# ============================================================

# ------------------------------------------------------------
# Graphics backend
#
# Graphics primitives (sdl-window, sdl-clear, sdl-present, etc.)
# are built in -- no external dependencies. They use Win32 GDI
# on Windows and Xlib on Linux. The API names start with "sdl-"
# for historical reasons but do not require SDL.
# ------------------------------------------------------------
(println "=== Graphics Demo ===")

(if (not (bound? 'sdl-window))
  (prog
    (println "Graphics not available")
    (quit 0)))

# ------------------------------------------------------------
# Window creation
#
# `sdl-window` creates a native window in one call:
# title, width (px), height (px). It returns T on
# success or NIL on failure. The window stays open until
# `sdl-quit` is called or the process exits.
# ------------------------------------------------------------
(if (not (sdl-window "L Graphics Demo" 800 600))
  (prog (println "Window init failed") (quit 0)))

# ------------------------------------------------------------
# Renderer and event loop
#
# `sdl-poll` returns the next pending event as an association
# list, or NIL when the queue is empty. The "type" key holds
# the event kind: "quit" (window X button) or "keydown". For
# keydown events, "key" holds the key name string.
#
# The main loop runs until `running` becomes NIL, polling
# for events then redrawing every ~16 ms (~60 fps).
# ------------------------------------------------------------
(let ((running T)
      (angle 0))
  (while running
    # Handle events
    (let ((ev (sdl-poll)))
      (if ev
        (let ((type (cdr (assoc "type" ev))))
          (if (equal type "quit") (setq running NIL))
          (if (equal type "keydown")
            (if (equal (cdr (assoc "key" ev)) "Escape")
              (setq running NIL))))))

    # ----------------------------------------------------------
    # Drawing primitives
    #
    # `sdl-color` sets the active RGBA draw color (0–255 each).
    # `sdl-clear` fills the entire window with that color.
    # `sdl-fill`  draws a filled rectangle: x y w h (pixels).
    # `sdl-circle` draws a hollow circle: cx cy radius.
    # `sdl-fill-circle` draws a filled circle: cx cy radius.
    # `sdl-line`  draws a line: x1 y1 x2 y2.
    # `sdl-rect`  draws a hollow rectangle: x y w h.
    # All coordinates are integers in pixels.
    # ----------------------------------------------------------

    # Background: dark grey
    (sdl-color 30 30 30 255)
    (sdl-clear)

    # Filled red rectangle -- top-left area
    (sdl-color 255 100 100 255)
    (sdl-fill 100 100 200 150)

    # Hollow green circle -- centre of window
    (sdl-color 100 255 100 255)
    (sdl-circle 400 300 80)

    # Filled blue circle -- upper-right area
    (sdl-color 100 100 255 255)
    (sdl-fill-circle 600 200 50)

    # Yellow diagonal lines -- full-window X pattern
    (sdl-color 255 255 0 255)
    (sdl-line 0 0 800 600)
    (sdl-line 800 0 0 600)

    # ----------------------------------------------------------
    # Present
    #
    # `sdl-present` flips the back buffer to the screen. All
    # drawing above goes to an off-screen buffer; `sdl-present`
    # makes it visible atomically, preventing tearing. Call it
    # exactly once per frame, after all drawing is complete.
    # `sdl-delay` sleeps for N milliseconds; 16 ms ≈ 60 fps.
    # ----------------------------------------------------------
    (sdl-present)
    (sdl-delay 16)  # ~60fps

    (setq angle (1+ angle))))

(sdl-quit)
(println "Done.")
