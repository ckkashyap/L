# ============================================================
# 13-tinygl.l -- Spinning cube via TinyGL software rasterizer
#
# What this covers:
#   1. TinyGL: software OpenGL without a GPU
#   2. Integer fixed-point coordinate convention
#   3. Matrix stack: projection and modelview
#   4. Per-frame rendering loop (clear → transform → draw → present)
#   5. Spinning cube animation
#
# Run:  L demos/13-tinygl.l
# Spec: L.spec.txt Part XIV -- Graphics and Audio
#
# Controls:  Q / Esc → quit
# ============================================================

# ------------------------------------------------------------
# TinyGL
#
# TinyGL is a software OpenGL 1.x rasterizer. It implements a
# subset of the classic OpenGL fixed-function pipeline in pure
# software -- no GPU required. The rasterizer runs entirely on
# the CPU and writes pixels into an internal framebuffer that
# is then blitted to the native window via `tgl-present`.
#
# Requires HAVE_TINYGL (enabled by default when deps/tinygl/
# is present). Build with: `make` (Linux) or `build.bat` (Windows).
# ------------------------------------------------------------
(println "=== TinyGL Spinning Cube ===")

(if (not (bound? 'sdl-window))
  (prog (println "Graphics not available") (quit 0)))

(if (not (bound? 'tgl-init))
  (prog (println "Rebuild with HAVE_TINYGL=1") (quit 0)))

(setq g-w 640)
(setq g-h 480)

(if (not (sdl-window "L -- TinyGL Cube" g-w g-h))
  (prog (println "Window creation failed") (quit 0)))

# Initialise TinyGL framebuffer at window resolution
(tgl-init g-w g-h)
(tgl-enable "depth-test")

# ------------------------------------------------------------
# Integer coordinates
#
# TinyGL in L uses integer fixed-point coordinates. All numeric
# arguments to `tgl-vertex`, `tgl-color`, `tgl-rotate`,
# `tgl-translate`, and `tgl-frustum` are divided by 100 inside
# the C bridge before being passed to TinyGL. This lets L code
# work with plain integers while preserving two decimal places
# of precision:
#
#   (tgl-vertex 100 200 300)        → glVertex3f(1.0, 2.0, 3.0)
#   (tgl-color  100  50   0)        → glColor3f(1.0, 0.5, 0.0)
#   (tgl-rotate 9000 0 100 0)       → glRotatef(90.0, 0.0, 1.0, 0.0)
#   (tgl-translate 0 0 -350)        → glTranslatef(0.0, 0.0, -3.5)
#
# The unit cube in this demo has vertices at ±100 centiunits,
# which equals ±1.0 in OpenGL space.
# ------------------------------------------------------------

# ------------------------------------------------------------
# Matrix stack
#
# `tgl-matrix-mode` switches the active matrix (either
# "projection" or "modelview"), matching glMatrixMode.
# `tgl-load-identity` resets it to the identity matrix.
# `tgl-frustum` sets up a perspective projection (equivalent
# to glFrustum). `tgl-rotate` and `tgl-translate` post-multiply
# the modelview matrix just like their classic OpenGL counterparts,
# making it straightforward to compose camera and object transforms.
#
# Projection: perspective frustum -- 4:3, ~53° vertical FOV
#   Args ×0.01: -133 133 -100 100 100 5000
#   → glFrustum(-1.33, 1.33, -1.0, 1.0, 1.0, 50.0)
# ------------------------------------------------------------
(tgl-matrix-mode "projection")
(tgl-load-identity)
(tgl-frustum -133 133 -100 100 100 5000)

(tgl-matrix-mode "modelview")

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------
(setq g-angle   0)     # Y-rotation in centidegrees (0–35999)
(setq g-running T)

# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------
# Draw one quad face with a flat colour.
# All coordinate args are in centiunits (÷100 in the bridge).
(de draw-face (r g b  x0 y0 z0  x1 y1 z1  x2 y2 z2  x3 y3 z3)
  (tgl-color r g b)
  (tgl-begin "quads")
  (tgl-vertex x0 y0 z0)
  (tgl-vertex x1 y1 z1)
  (tgl-vertex x2 y2 z2)
  (tgl-vertex x3 y3 z3)
  (tgl-end))

# Unit cube: vertices at ±100 (= ±1.0).  Six faces, six colours.
(de draw-cube ()
  # +Z face -- red
  (draw-face 100  35  35   -100 -100  100   100 -100  100   100  100  100  -100  100  100)
  # -Z face -- green
  (draw-face  35 100  35    100 -100 -100  -100 -100 -100  -100  100 -100   100  100 -100)
  # -X face -- blue
  (draw-face  35  35 100   -100 -100 -100  -100 -100  100  -100  100  100  -100  100 -100)
  # +X face -- yellow
  (draw-face 100 100  35    100 -100  100   100 -100 -100   100  100 -100   100  100  100)
  # +Y face -- cyan
  (draw-face  35 100 100   -100  100  100   100  100  100   100  100 -100  -100  100 -100)
  # -Y face -- magenta
  (draw-face 100  35 100   -100 -100 -100   100 -100 -100   100 -100  100  -100 -100  100))

# ---------------------------------------------------------------------------
# Event handling
# ---------------------------------------------------------------------------
(de poll-events ()
  (let ((ev (sdl-poll)))
    (if ev
      (prog
        (let ((ty (cdr (assoc "type" ev))))
          (if (equal ty "quit") (setq g-running NIL))
          (if (equal ty "keydown")
            (let ((k (cdr (assoc "key" ev))))
              (if (or (equal k "Escape") (equal k "q"))
                (setq g-running NIL)))))
        (poll-events)))))

# ---------------------------------------------------------------------------
# Rendering loop
#
# Each frame follows the classic OpenGL sequence:
#   1. Poll and handle SDL events (quit / keyboard).
#   2. Reset the modelview matrix with `tgl-load-identity`.
#   3. Apply camera transform: translate back 3.5 units on Z.
#   4. Apply object transforms: rotate on Y (primary spin) and
#      X (slow tumble) using the accumulated g-angle value.
#   5. `tgl-clear` clears the color and depth buffers.
#   6. `draw-cube` submits six quad faces as geometry.
#   7. `tgl-present` blits the TinyGL framebuffer to the native
#      window surface -- equivalent to SwapBuffers.
#
# Spinning cube
#
# The demo rotates a cube by incrementing g-angle by 100
# centidegrees (= 1°) each frame. At 60 fps that is one full
# Y-rotation every 6 seconds. The X tumble angle is g-angle/3,
# giving a slow secondary wobble that makes all six faces
# visible over time. This demonstrates the full pipeline:
#   clear → model transform → draw geometry → present.
# ---------------------------------------------------------------------------
(while g-running
  (poll-events)

  # Camera: pull back 3.5 units, spin on Y, slow tumble on X
  (tgl-matrix-mode "modelview")
  (tgl-load-identity)
  (tgl-translate 0 0 -350)                         # z = -3.5
  (tgl-rotate g-angle 0 100 0)                     # primary Y spin
  (tgl-rotate (/ g-angle 3) 100 0 0)               # slow X tumble

  # Render
  (tgl-clear)
  (draw-cube)
  (tgl-present)    # blit TinyGL framebuffer → native window

  # Advance: 1° per frame at 60 fps ≈ one rotation every 6 s
  (setq g-angle (mod (+ g-angle 100) 36000))

  (sdl-delay 16))

(sdl-quit)
(println "Done.")
