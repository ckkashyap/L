# stdlib.l -- Standard library umbrella loader
# Loaded automatically at startup if present

(load "lib/string.l")
(load "lib/math.l")
(load "lib/io.l")
(load "lib/ds.l")
(load "lib/rope.l")
(load "lib/http.l")
(load "lib/util.l")
# websocket.l loaded on demand by demos that need it
