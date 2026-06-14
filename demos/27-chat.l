# ============================================================
# 27-chat.l -- WebSocket chat server
#
# What this covers:
#   1. HTTP serving (static HTML page)
#   2. WebSocket upgrade handshake
#   3. WebSocket frame parsing and building
#   4. Broadcasting messages to all connected clients
#
# Run:  L demos/27-chat.l
# Open: http://localhost:8080 in multiple browser tabs
#
# Controls: Type a message, press Enter or click Send.
#           Messages appear in all connected tabs in real time.
# ============================================================

(load "lib/websocket.l")
(println "=== WebSocket Chat Demo ===")

# ---- HTML page served to browser ----------------------------

(setq *chat-html* (pack (list
"<!DOCTYPE html><html><head><meta charset='utf-8'><title>L Chat</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;
     display:flex;flex-direction:column;height:100vh}
#header{background:#16213e;padding:12px 20px;font-size:18px;font-weight:bold;
        border-bottom:1px solid #0f3460}
#messages{flex:1;overflow-y:auto;padding:16px 20px}
.msg{margin-bottom:8px;padding:8px 12px;background:#16213e;border-radius:6px;
     max-width:80%;word-wrap:break-word}
.msg .user{color:#e94560;font-weight:bold;margin-right:8px}
.msg .time{color:#666;font-size:11px;float:right}
.msg.system{color:#888;font-style:italic;background:none;padding:4px 12px}
#input-bar{display:flex;padding:12px;background:#16213e;border-top:1px solid #0f3460}
#msg-input{flex:1;padding:10px 14px;border:1px solid #0f3460;border-radius:6px;
           background:#0a0a1a;color:#e0e0e0;font-size:14px;outline:none}
#msg-input:focus{border-color:#e94560}
#send-btn{margin-left:8px;padding:10px 20px;background:#e94560;color:white;
          border:none;border-radius:6px;cursor:pointer;font-size:14px}
#send-btn:hover{background:#c73e54}
#status{padding:4px 20px;font-size:11px;color:#666;background:#111}
</style></head><body>
<div id='header'>L Chat</div>
<div id='status'>Connecting...</div>
<div id='messages'></div>
<div id='input-bar'>
  <input id='msg-input' placeholder='Type a message...' autocomplete='off'>
  <button id='send-btn'>Send</button>
</div>
<script>
const user = 'User-' + Math.floor(Math.random()*9000+1000);
const msgs = document.getElementById('messages');
const inp = document.getElementById('msg-input');
const stat = document.getElementById('status');
let ws;
function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => { stat.textContent = 'Connected as ' + user; };
  ws.onclose = (e) => { stat.textContent = 'Disconnected (code=' + e.code + '). Reconnecting...'; setTimeout(connect, 2000); };
  ws.onerror = (e) => { console.log('ws error', e); };
  ws.onmessage = (e) => {
    try {
      const m = JSON.parse(e.data);
      const d = document.createElement('div');
      d.className = 'msg' + (m.system ? ' system' : '');
      if (m.system) { d.textContent = m.text; }
      else {
        const t = new Date().toLocaleTimeString();
        d.innerHTML = '<span class=\"user\">' + esc(m.user) + '</span>' +
                      esc(m.text) + '<span class=\"time\">' + t + '</span>';
      }
      msgs.appendChild(d);
      msgs.scrollTop = msgs.scrollHeight;
    } catch(ex) {}
  };
}
function esc(s) { const d=document.createElement('div'); d.textContent=s; return d.innerHTML; }
function send() {
  const t = inp.value.trim();
  if (t && ws && ws.readyState === 1) {
    ws.send(JSON.stringify({user: user, text: t}));
    inp.value = '';
  }
}
inp.addEventListener('keydown', (e) => { if(e.key==='Enter') { e.preventDefault(); send(); } });
document.getElementById('send-btn').addEventListener('click', send);
connect();
</script></body></html>"
)))

# ---- Server state -------------------------------------------

# Per-fd connection state:
#   "http"  -- waiting for HTTP request
#   "ws"    -- upgraded to WebSocket
(setq *conns* (pmap-new))      # fd → "http" | "ws"
(setq *bufs*  (pmap-new))      # fd → accumulated data string
(setq *ws-fds* NIL)            # list of WebSocket fd ints

# ---- Helpers ------------------------------------------------

(de chat--find-header (raw name)
  (let* ((lines (split raw "\r\n"))
         (found NIL))
    (mapc (lambda (line)
            (when (starts-with? line (pack (list name ": ")))
              (setq found (trim (sub line (+ (length (unpack name)) 2))))))
          lines)
    found))

(de chat--broadcast (msg)
  (let ((frame (ws-build-frame msg))
        (fds *ws-fds*))
    (while fds
      (tcp-write (car fds) frame)
      (setq fds (cdr fds)))))

# ---- Connection handler -------------------------------------

(de chat--on-connect (fd)
  (setq *conns* (pmap-put *conns* fd "http"))
  (setq *bufs*  (pmap-put *bufs*  fd "")))

(de chat--on-close (fd)
  (setq *conns* (pmap-del *conns* fd))
  (setq *bufs*  (pmap-del *bufs*  fd))
  (setq *ws-fds* (filter (lambda (f) (not (= f fd))) *ws-fds*))
  (chat--broadcast
    (pack (list "{\"system\":true,\"text\":\"A user disconnected ("
                (str (length *ws-fds*)) " online)\"}"))))

(de chat--on-data (fd data)
  (let* ((prev (or (pmap-get *bufs* fd) ""))
         (buf  (pack (list prev data)))
         (type (or (pmap-get *conns* fd) "http")))
    (setq *bufs* (pmap-put *bufs* fd buf))
    (cond
      # --- HTTP request (waiting for headers) ---
      ((equal type "http")
       (when (index "\r\n\r\n" buf)
         (let* ((req-line (car (split buf "\r\n")))
                (parts    (split req-line " "))
                (path     (if (cdr parts) (cadr parts) "/")))
           (cond
             # WebSocket upgrade request
             ((and (equal path "/ws") (chat--find-header buf "Sec-WebSocket-Key"))
              (let ((key (chat--find-header buf "Sec-WebSocket-Key")))
                (ws-handshake fd key)
                (setq *conns* (pmap-put *conns* fd "ws"))
                (setq *bufs*  (pmap-put *bufs*  fd ""))
                (setq *ws-fds* (cons fd *ws-fds*))
                (chat--broadcast
                  (pack (list "{\"system\":true,\"text\":\"A user connected ("
                              (str (length *ws-fds*)) " online)\"}")))))
             # Serve HTML page
             (T
              (let* ((body *chat-html*)
                     (blen (str (length (unpack body))))
                     (resp (pack (list "HTTP/1.1 200 OK\r\n"
                                       "Content-Type: text/html; charset=utf-8\r\n"
                                       "Content-Length: " blen "\r\n"
                                       "Connection: close\r\n"
                                       "\r\n" body))))
                (tcp-write fd resp)
                (tcp-close fd)
                (setq *conns* (pmap-del *conns* fd))
                (setq *bufs*  (pmap-del *bufs*  fd))))))))

      # --- WebSocket frame ---
      ((equal type "ws")
       (let ((frame (ws-parse-frame buf)))
         (when frame
           (let (((opcode payload) frame))
             (setq *bufs* (pmap-put *bufs* fd ""))
             (cond
               # Text message -- broadcast to all
               ((= opcode 1) (chat--broadcast payload))
               # Close frame
               ((= opcode 8)
                (tcp-write fd (ws-build-close))
                (tcp-close fd)
                (chat--on-close fd))
               # Ping → pong
               ((= opcode 9)
                (tcp-write fd (ws-build-pong payload))))))))
      )))

# ---- Start server -------------------------------------------

(tcp-listen 8080
  (lambda (err fd . rest)
    (cond
      (err NIL)
      ((and (number? fd) (null rest))
       (chat--on-connect fd))
      ((and (number? fd) rest (car rest))
       (chat--on-data fd (car rest)))
      ((and (number? fd) rest (null (car rest)))
       (chat--on-close fd)))))

(println "Chat server running on http://localhost:8080")
(println "Open in multiple browser tabs to test.")
(println "Press Ctrl+C to stop.")
(event-loop)
