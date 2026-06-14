# ============================================================
# 09-http.l -- Real HTTP client + server demo
#
# What this covers:
#   1. Event loop model (poll-based, native)
#   2. HTTP server with http-serve
#   3. HTTP client with http-get / http-post
#   4. Async callbacks and timers
#
# Run:  L demos/09-http.l
# Spec: L.spec.txt Part 7 -- Networking and Event Loop
# ============================================================

# -------------------------------------------------------------------------
# 1. Native I/O
#
# The HTTP primitives (http-serve, http-get, http-post, timer,
# event-loop) are built into the interpreter -- no external
# dependencies required. The event loop uses poll(2) on POSIX
# and WSAPoll on Windows. HTTP client requests run on worker
# threads so the server can handle them without deadlocking.
# -------------------------------------------------------------------------

# Run: ./build/l demos/09-http.l

(println "=== HTTP Demo ===")

# -------------------------------------------------------------------------
# 2. HTTP server
#
# `http-serve` binds a port and calls a handler function for each incoming
# request. The handler receives (method path body) and returns a response.
# -------------------------------------------------------------------------

# --- HTTP Server ---
# Start a local server on port 8080.
# The handler receives (method path body) and returns a response string.

(de handle-request (method path body)
  (pack (list "Hello from L!\nMethod: " method "\nPath: " path "\n")))

(println "Starting HTTP server on port 8080...")
(http-serve 8080 handle-request)

# -------------------------------------------------------------------------
# 3. HTTP client
#
# `http-get`/`http-post` perform asynchronous HTTP requests on a worker
# thread. The callback fires when the response arrives. Arguments to the
# callback are (err status body) -- err is NIL on success.
# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# 4. Async callbacks and timers
#
# `timer` schedules a callback after a delay (milliseconds). The event
# loop dispatches timers, server connections, and completed HTTP requests
# each iteration.
# -------------------------------------------------------------------------

# Give the server a moment to bind, then make a request to ourselves.
(timer 100
  (lambda ()
    (println "Making GET request to http://localhost:8080/hello ...")
    (http-get "http://localhost:8080/hello"
      (lambda (err status body)
        (if err
          (println "Error: " err)
          (prog
            (println "Status: " status)
            (println "Body:")
            (println body)))
        # Also demonstrate POST
        (println "Making POST request...")
        (http-post "http://localhost:8080/echo" "key=value&foo=bar"
          (lambda (err2 status2 body2)
            (if err2
              (println "POST error: " err2)
              (prog
                (println "POST Status: " status2)
                (println "POST Body: " body2)))
            (quit 0)))))
    NIL))

# -------------------------------------------------------------------------
# 5. Event loop
#
# `event-loop` runs the native poll-based event loop. It dispatches
# timer callbacks, accepts HTTP server connections, and checks for
# completed async HTTP requests each iteration. The loop exits when
# there are no more pending handles (timers, sockets, or requests).
# -------------------------------------------------------------------------
(event-loop)
(println "Done.")
