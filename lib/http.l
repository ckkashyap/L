# http.l -- HTTP server implemented in L on top of tcp-listen
#
# (http-serve port handler)
#   Starts an HTTP server on port. For each request, handler is
#   called with (method path headers body) and should return
#   either a string (sent as 200 text/plain) or a list:
#     (status-code headers-alist body-string)
#   e.g. (200 (("Content-Type" . "text/html")) "<h1>Hi</h1>")
#
# The handler's return format:
#   "hello"                          → 200 OK, text/plain, "hello"
#   (200 () "hello")                 → 200 OK, no extra headers
#   (404 () "not found")             → 404 Not Found
#   (200 (("Content-Type" . "text/html")) "<h1>Hi</h1>")

# ---- HTTP request parsing -----------------------------------

# Parse "GET /path HTTP/1.1\r\n..." into (method path headers body)
(de http--parse-request (raw)
  (let* ((hdr-end  (index "\r\n\r\n" raw))
         (hdr-pos  (if hdr-end (- hdr-end 1) NIL))  # 0-based
         (hdr-part (if hdr-pos (sub raw 0 hdr-pos) raw))
         (body     (if hdr-pos (sub raw (+ hdr-pos 4)) ""))
         (lines    (split hdr-part "\r\n"))
         (req-line (car lines))
         (parts    (split req-line " "))
         (method   (car parts))
         (path     (if (cdr parts) (cadr parts) "/"))
         (hdrs     (http--parse-headers (cdr lines))))
    (list method path hdrs body)))

# Parse header lines into alist: (("Host" . "localhost") ...)
(de http--parse-headers (lines)
  (if (null lines) NIL
    (let* ((line (car lines))
           (colon (index ": " line)))
      (if colon
        (cons (cons (sub line 0 (- colon 1))
                    (sub line (+ (- colon 1) 2)))
              (http--parse-headers (cdr lines)))
        (http--parse-headers (cdr lines))))))

# ---- HTTP response formatting -------------------------------

# Standard status text lookup
(de http--status-text (code)
  (cond
    ((= code 200) "OK")
    ((= code 201) "Created")
    ((= code 204) "No Content")
    ((= code 301) "Moved Permanently")
    ((= code 302) "Found")
    ((= code 304) "Not Modified")
    ((= code 400) "Bad Request")
    ((= code 403) "Forbidden")
    ((= code 404) "Not Found")
    ((= code 405) "Method Not Allowed")
    ((= code 500) "Internal Server Error")
    (T "OK")))

# Format an HTTP response from handler return value
(de http--format-response (result)
  (if (string? result)
    # Simple string → 200 text/plain
    (let ((body result)
          (blen (str (length (unpack result)))))
      (pack (list "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain\r\n"
                  "Content-Length: " blen "\r\n"
                  "Connection: close\r\n"
                  "\r\n"
                  body)))
    # List: (status headers-alist body)
    (if (and (pair result) (number? (car result)))
      (let* ((status  (car result))
             (hdrs    (cadr result))
             (body    (caddr result))
             (body    (if body body ""))
             (blen    (length (unpack body)))
             (stxt    (http--status-text status)))
        (let ((hdr-str (http--format-headers hdrs blen)))
          (pack (list "HTTP/1.1 " (str status) " " stxt "\r\n"
                      hdr-str
                      "\r\n"
                      body))))
      # Fallback: convert to string
      (http--format-response (str result)))))

# Format headers alist + Content-Length + Connection: close
(de http--format-headers (hdrs blen)
  (let ((has-ct NIL)
        (has-cl NIL)
        (parts  NIL))
    (mapc (lambda ((k . v))
            (when (equal k "Content-Type") (setq has-ct T))
            (when (equal k "Content-Length") (setq has-cl T))
            (setq parts (cons (pack (list k ": " v "\r\n")) parts)))
          hdrs)
    (when (not has-ct)
      (setq parts (cons "Content-Type: text/plain\r\n" parts)))
    (when (not has-cl)
      (setq parts (cons (pack (list "Content-Length: " (str blen) "\r\n")) parts)))
    (setq parts (cons "Connection: close\r\n" parts))
    (pack (reverse parts))))

# ---- Server implementation ----------------------------------

# Per-connection state stored in a pmap: fd → buffer-string
(setq *http-bufs* (pmap-new))

(de http-serve (port handler)
  (setq *http-bufs* (pmap-new))
  (tcp-listen port
    (lambda (err fd . rest)
      (cond
        # Error
        (err NIL)

        # New connection accepted: fd is integer, no rest
        ((and (number? fd) (null rest))
         (setq *http-bufs* (pmap-put *http-bufs* fd "")))

        # Data received: (NIL fd data) -- rest = (data)
        ((and (number? fd) rest (car rest))
         (let* ((data (car rest))
                (prev (or (pmap-get *http-bufs* fd) ""))
                (buf  (pack (list prev data))))
           (setq *http-bufs* (pmap-put *http-bufs* fd buf))
           # Check if we have complete headers
           (when (index "\r\n\r\n" buf)
             (let* (((method path hdrs body) (http--parse-request buf))
                    (result   (handler method path body))
                    (response (http--format-response result)))
               (tcp-write fd response)
               (tcp-close fd)
               (setq *http-bufs* (pmap-del *http-bufs* fd))))))

        # Connection closed: (NIL fd NIL) -- rest = (NIL)
        ((and (number? fd) rest (null (car rest)))
         (setq *http-bufs* (pmap-del *http-bufs* fd)))))))
