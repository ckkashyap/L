# websocket.l -- WebSocket protocol (RFC 6455) implemented in L
#
# Provides: ws-handshake, ws-parse-frame, ws-build-frame
# Requires: sha1, base64-encode, tcp-write (C primitives)

(setq *ws-magic* "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")

# ---- Handshake ------------------------------------------------

(de ws-accept-key (client-key)
  (base64-encode (sha1 (pack (list client-key *ws-magic*)))))

(de ws-handshake (fd client-key)
  (tcp-write fd
    (pack (list "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " (ws-accept-key client-key) "\r\n"
                "\r\n"))))

# ---- Byte helpers ---------------------------------------------

(de ws--byte (s pos)
  (char (sub s pos 1)))

(de ws--bytes-len (s)
  (length (unpack s)))

# ---- Frame parsing (client→server, masked) --------------------

# Returns (opcode payload) or NIL if data is incomplete.
(de ws-parse-frame (data)
  (let ((dlen (ws--bytes-len data)))
    (if (< dlen 2) NIL
      (let* ((b0     (ws--byte data 0))
             (b1     (ws--byte data 1))
             (opcode (bitand b0 15))
             (masked (not (= 0 (bitand b1 128))))
             (plen   (bitand b1 127))
             (off    2))
        # Extended payload length
        (if (= plen 126)
          (if (< dlen 4) NIL
            (prog
              (setq plen (+ (* (ws--byte data 2) 256) (ws--byte data 3)))
              (setq off 4)))
          (if (= plen 127)
            (if (< dlen 10) NIL
              (prog
                (setq plen (+ (* (ws--byte data 6) 16777216)
                              (* (ws--byte data 7) 65536)
                              (* (ws--byte data 8) 256)
                              (ws--byte data 9)))
                (setq off 10)))))
        # Check we have enough data
        (let* ((hdr-len  (if masked (+ off 4) off))
               (need     (+ hdr-len plen)))
          (if (< dlen need) NIL
            (let ((payload
                    (if masked
                      (ws--unmask data off plen)
                      (sub data hdr-len plen))))
              (list opcode payload))))))))

# Unmask payload: XOR each byte with mask[i%4]
(de ws--unmask (data mask-off plen)
  (let* ((m0 (ws--byte data mask-off))
         (m1 (ws--byte data (+ mask-off 1)))
         (m2 (ws--byte data (+ mask-off 2)))
         (m3 (ws--byte data (+ mask-off 3)))
         (data-off (+ mask-off 4))
         (chars NIL)
         (i 0))
    (while (< i plen)
      (let* ((raw (ws--byte data (+ data-off i)))
             (mk  (cond ((= (mod i 4) 0) m0)
                        ((= (mod i 4) 1) m1)
                        ((= (mod i 4) 2) m2)
                        (T m3)))
             (dec (bitxor raw mk)))
        (setq chars (cons (char dec) chars)))
      (setq i (1+ i)))
    (pack (reverse chars))))

# ---- Frame building (server→client, unmasked) -----------------

(de ws-build-frame (payload)
  (let ((plen (ws--bytes-len payload)))
    (cond
      ((<= plen 125)
       (pack (list (char 129) (char plen) payload)))
      ((<= plen 65535)
       (pack (list (char 129) (char 126)
                   (char (/ plen 256)) (char (mod plen 256))
                   payload)))
      (T
       (pack (list (char 129) (char 127)
                   (char 0) (char 0) (char 0) (char 0)
                   (char (/ plen 16777216))
                   (char (mod (/ plen 65536) 256))
                   (char (mod (/ plen 256) 256))
                   (char (mod plen 256))
                   payload))))))

(de ws-build-close ()
  (pack (list (char 136) (char 0))))

(de ws-build-pong (payload)
  (let ((plen (ws--bytes-len payload)))
    (pack (list (char 138) (char plen) payload))))
