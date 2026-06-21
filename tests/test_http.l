# ============================================================
# test_http.l -- HTTP server and client round-trip test
#
# Tests:
#   • http-serve starts a server that responds to requests
#   • http-get performs an async GET and receives the response
#   • http-post sends a request and receives a response
#   • event-loop drives async callbacks to completion
#
# Run:  build/l tests/test_http.l
# ============================================================

(load "lib/http.l")

(setq *pass 0)
(setq *fail 0)

(de test (name expected actual)
  (if (equal expected actual)
    (setq *pass (1+ *pass))
    (and
      (setq *fail (1+ *fail))
      (println (list "FAIL:" name "expected" expected "got" actual)))))

# ---- Start server on a high port to avoid conflicts --------

(setq *test-port 19876)

(de test-handler (method path body)
  (cond
    ((equal path "/hello")  (pack (list "Hello " method)))
    ((equal path "/status") (list 201 () "created"))
    (T "not found")))

(http-serve *test-port test-handler)

# ---- Schedule client requests after server is ready --------

(timer 50
  (lambda ()
    # Test 1: GET /hello
    (http-get (pack (list "http://localhost:" (str *test-port) "/hello"))
      (lambda (err status body)
        (test "GET no error" NIL err)
        (test "GET status" 200 status)
        (test "GET body" "Hello GET" body)

        # Test 2: POST /hello (verify routing and method detection)
        (timer 50
          (lambda ()
            (http-post (pack (list "http://localhost:" (str *test-port) "/hello"))
              ""
              (lambda (err2 status2 body2)
                (test "POST no error" NIL err2)
                (test "POST status" 200 status2)
                (test "POST body" "Hello POST" body2)

                # Done -- print results and exit
                (println "")
                (println (pack (list "test_http.l: " (str *pass) " passed, " (str *fail) " failed")))
                (if (= *fail 0)
                  (println "ALL TESTS PASSED")
                  (and (println "TESTS FAILED") (quit 1)))
                (quit 0)))
            NIL))
        NIL))
    NIL))

# ---- Timeout: fail if callbacks never fire -----------------
(timer 5000
  (lambda ()
    (println "TIMEOUT: event loop did not complete in 5 seconds")
    (quit 1)))

# ---- Run the event loop -----------------------------------
(event-loop)
