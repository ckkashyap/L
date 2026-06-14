/* native_io_posix.c -- poll(2)-based event loop; replaces libuv for POSIX. */
#define _POSIX_C_SOURCE 200809L
#include "picolisp.h"
#include "native_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <time.h>

/* =========================================================================
 * Timer table
 * ===================================================================== */

#define MAX_TIMERS 256

typedef struct {
    uint64_t expiry_ms;
    Val      callback;
    int      active;
} Timer;

static Timer g_timers[MAX_TIMERS];
static int   g_ntimers = 0;

/* =========================================================================
 * FD table
 * ===================================================================== */

#define MAX_FDS 256

typedef enum { FD_TCP_CLIENT, FD_TCP_SERVER, FD_HTTP_CLIENT } FdKind;

typedef struct {
    int     fd;
    FdKind  kind;
    Val     callback;
    int     active;
    char   *rbuf;
    size_t  rused, rcap;
    Val     handler;
} FdSlot;

static FdSlot g_fds[MAX_FDS];
static int    g_nfds = 0;

/* =========================================================================
 * Async HTTP request queue (threaded)
 * ===================================================================== */

#include <pthread.h>

#define MAX_ASYNC_HTTP 16

typedef struct {
    char    *method;
    char    *host;
    int      port;
    char    *path;
    char    *req_body;
    size_t   req_body_len;
    Val      callback;
    /* Results (written by worker thread) */
    int      status;
    size_t   body_len;
    char    *body;
    volatile int complete; /* 0 = pending, 1 = done */
} AsyncHttpReq;

static AsyncHttpReq *g_async_http[MAX_ASYNC_HTTP];
static int           g_nasync_http = 0;

/* =========================================================================
 * Monotonic clock
 * ===================================================================== */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* =========================================================================
 * HTTP helpers -- copied verbatim from io_uv.c (no libuv dependency)
 * ===================================================================== */

typedef int http_sock_t;
#define HTTP_SOCK_INVALID (-1)
#define http_sock_close(s) close(s)
static int http_sock_send(http_sock_t s, const void *b, int l)
    { return (int)send(s, b, (size_t)l, 0); }
static int http_sock_recv(http_sock_t s, void *b, int l)
    { return (int)recv(s, b, (size_t)l, 0); }

/* Returns 1 on success. Caller must free *host and *path. */
static int parse_http_url(const char *url, char **host, int *port, char **path)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else return 0; /* https not supported */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    int hlen;
    *port = 80;
    if (colon && (!slash || colon < slash)) {
        hlen  = (int)(colon - p);
        *port = atoi(colon + 1);
    } else {
        hlen = slash ? (int)(slash - p) : (int)strlen(p);
    }
    *host = malloc(hlen + 1);
    if (!*host) return 0;
    memcpy(*host, p, hlen); (*host)[hlen] = '\0';
    *path = strdup(slash ? slash : "/");
    if (!*path) { free(*host); return 0; }
    return 1;
}

/* Returns malloc'd body string (NUL-terminated) or NULL on error.
 * Sets *status to HTTP status code, *body_len to body byte count. */
static char *http_sync_request(const char *method, const char *host, int port,
                               const char *path, const char *req_body,
                               size_t req_body_len, int *status, size_t *body_len)
{
    *status = 0; *body_len = 0;
    char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        hints.ai_family = AF_UNSPEC;
        if (getaddrinfo(host, port_str, &hints, &res) != 0) return NULL;
    }
    http_sock_t s = HTTP_SOCK_INVALID;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == HTTP_SOCK_INVALID) continue;
        if (connect(s, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        http_sock_close(s); s = HTTP_SOCK_INVALID;
    }
    freeaddrinfo(res);
    if (s == HTTP_SOCK_INVALID) return NULL;
    { int flag = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag)); }

    char head[4096]; int hn = 0;
    hn += snprintf(head + hn, sizeof(head) - hn,
        "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
        "User-Agent: L/1.0\r\nAccept: */*\r\n",
        method, path, host);
    if (req_body && req_body_len > 0)
        hn += snprintf(head + hn, sizeof(head) - hn,
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %zu\r\n", req_body_len);
    hn += snprintf(head + hn, sizeof(head) - hn, "\r\n");
    http_sock_send(s, head, hn);
    if (req_body && req_body_len > 0)
        http_sock_send(s, req_body, (int)req_body_len);

    size_t cap = 65536, used = 0;
    char *buf = malloc(cap); if (!buf) { http_sock_close(s); return NULL; }
    for (;;) {
        if (used + 4096 > cap) {
            cap *= 2; char *nb = realloc(buf, cap);
            if (!nb) { free(buf); http_sock_close(s); return NULL; }
            buf = nb;
        }
        int n = http_sock_recv(s, buf + used, (int)(cap - used - 1));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    http_sock_close(s);
    buf[used] = '\0';

    if (strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) *status = atoi(sp + 1);
    }
    const char *bs = strstr(buf, "\r\n\r\n");
    if (!bs) { free(buf); return NULL; }
    bs += 4;
    *body_len = used - (size_t)(bs - buf);
    char *body = malloc(*body_len + 1);
    if (!body) { free(buf); return NULL; }
    memcpy(body, bs, *body_len); body[*body_len] = '\0';
    free(buf);
    return body;
}

/* Thread function for async HTTP requests */
static void *http_thread_fn(void *arg)
{
    AsyncHttpReq *req = (AsyncHttpReq *)arg;
    req->body = http_sync_request(req->method, req->host, req->port,
                                   req->path, req->req_body, req->req_body_len,
                                   &req->status, &req->body_len);
    __sync_synchronize(); /* memory barrier */
    req->complete = 1;
    return NULL;
}

/* Check for completed async HTTP requests and invoke callbacks */
static void fire_async_http(void)
{
    for (int i = 0; i < g_nasync_http; ) {
        AsyncHttpReq *req = g_async_http[i];
        if (!req->complete) { i++; continue; }

        Val cb = req->callback;
        if (req->body) {
            Val bval = MAKE_STR(str_intern(req->body, req->body_len));
            free(req->body);
            Val sval = MAKE_INT(req->status);
            PUSH_ROOT(bval);
            Val a  = pl_cons(sval, pl_cons(bval, NIL_VAL));
            Val ca = pl_cons(NIL_VAL, a); POP_ROOT();
            pl_apply(cb, ca, NIL_VAL);
        } else {
            Val err = MAKE_STR(str_intern("connection-failed", 17));
            PUSH_ROOT(err);
            Val a  = pl_cons(NIL_VAL, pl_cons(NIL_VAL, NIL_VAL));
            Val ca = pl_cons(err, a); POP_ROOT();
            pl_apply(cb, ca, NIL_VAL);
        }
        free(req->method); free(req->host); free(req->path); free(req->req_body);
        free(req);
        g_async_http[i] = g_async_http[--g_nasync_http];
    }
}

/* Helper: enqueue an async HTTP request on a worker thread */
static Val enqueue_async_http(const char *method, char *host, int port,
                               char *path, char *req_body, size_t req_body_len,
                               Val cb)
{
    if (g_nasync_http >= MAX_ASYNC_HTTP)
        pl_error_str("http: too many concurrent requests");
    AsyncHttpReq *req = (AsyncHttpReq *)calloc(1, sizeof(AsyncHttpReq));
    if (!req) pl_error_str("http: out of memory");
    req->method       = strdup(method);
    req->host         = host;
    req->port         = port;
    req->path         = path;
    req->req_body     = req_body;
    req->req_body_len = req_body_len;
    req->callback     = cb;
    g_async_http[g_nasync_http++] = req;
    pthread_t th;
    pthread_create(&th, NULL, http_thread_fn, req);
    pthread_detach(th);
    return NIL_VAL;
}

/* =========================================================================
 * Timer primitive
 * ===================================================================== */

/* (timer ms callback) */
static Val prim_timer(Val args, Val env)
{
    (void)env;
    Val ms_val = CAR(args);
    Val cb     = CADR(args);
    if (!IS_INT(ms_val)) pl_error_str("timer: first argument must be integer ms");
    uint64_t ms = (uint64_t)(unsigned)INT_VAL(ms_val);

    if (g_ntimers >= MAX_TIMERS) pl_error_str("timer: too many active timers");
    /* find a free slot */
    int slot = -1;
    for (int i = 0; i < g_ntimers; i++) {
        if (!g_timers[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = g_ntimers++;
    g_timers[slot].expiry_ms = now_ms() + ms;
    g_timers[slot].callback  = cb;
    g_timers[slot].active    = 1;
    return NIL_VAL;
}

/* =========================================================================
 * Event loop helpers
 * ===================================================================== */

static void fire_expired_timers(void)
{
    uint64_t now = now_ms();
    for (int i = 0; i < g_ntimers; i++) {
        if (!g_timers[i].active) continue;
        if (now >= g_timers[i].expiry_ms) {
            Val cb = g_timers[i].callback;
            g_timers[i].active = 0;
            pl_apply(cb, NIL_VAL, NIL_VAL);
        }
    }
}

static int compute_poll_timeout(void)
{
    int has = 0;
    uint64_t now = now_ms();
    int timeout  = 1000; /* 1 s default */
    for (int i = 0; i < g_ntimers; i++) {
        if (!g_timers[i].active) continue;
        has = 1;
        int64_t diff = (int64_t)(g_timers[i].expiry_ms - now);
        if (diff <= 0) return 0;
        if ((int)diff < timeout) timeout = (int)diff;
    }
    return has ? timeout : 50;
}

static int has_active_handles(void)
{
    if (g_nasync_http > 0) return 1;
    for (int i = 0; i < g_ntimers; i++)
        if (g_timers[i].active) return 1;
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].active) return 1;
    return 0;
}

/* =========================================================================
 * TCP helpers -- non-blocking connect and set
 * ===================================================================== */

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int fd_alloc_slot(void)
{
    for (int i = 0; i < g_nfds; i++)
        if (!g_fds[i].active) return i;
    if (g_nfds >= MAX_FDS) pl_error_str("native_io: too many open fds");
    return g_nfds++;
}

/* =========================================================================
 * TCP connect
 * ===================================================================== */

/* (tcp-connect host port callback) */
static Val prim_tcp_connect(Val args, Val env)
{
    (void)env;
    Val host_val = CAR(args);
    Val port_val = CADR(args);
    Val cb       = CADDR(args);

    if (!IS_STR(host_val) || !IS_INT(port_val))
        pl_error_str("tcp-connect: expected (string int callback)");

    const char *host = str_ptr(STR_IDX(host_val));
    int         port = INT_VAL(port_val);

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        Val err = MAKE_STR(str_intern("connection-failed", 17));
        PUSH_ROOT(err);
        Val a = pl_cons(err, NIL_VAL);
        POP_ROOT();
        pl_apply(cb, a, NIL_VAL);
        return NIL_VAL;
    }

    /* Check fd table capacity BEFORE opening any socket (C3/I2 guard) */
    {
        int has_slot = 0;
        for (int i = 0; i < g_nfds; i++)
            if (!g_fds[i].active) { has_slot = 1; break; }
        if (!has_slot && g_nfds >= MAX_FDS) {
            freeaddrinfo(res);
            return NIL_VAL;
        }
    }

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype | SOCK_CLOEXEC, rp->ai_protocol);
        if (fd < 0) continue;
        set_nonblocking(fd);
        int r = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (r == 0 || errno == EINPROGRESS) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        Val err = MAKE_STR(str_intern("connection-failed", 17));
        PUSH_ROOT(err);
        Val a = pl_cons(err, NIL_VAL);
        POP_ROOT();
        pl_apply(cb, a, NIL_VAL);
        return NIL_VAL;
    }

    /* Wait for connect completion synchronously via poll (C1+C2) */
    struct pollfd pf = { .fd = fd, .events = POLLOUT };
    int r = poll(&pf, 1, 5000);
    if (r <= 0) { close(fd); return NIL_VAL; }
    if (pf.revents & POLLERR) { close(fd); return NIL_VAL; }
    int so_err = 0; socklen_t slen = sizeof(so_err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen);
    if (so_err != 0) { close(fd); return NIL_VAL; }

    int slot = fd_alloc_slot();
    g_fds[slot].fd       = fd;
    g_fds[slot].kind     = FD_TCP_CLIENT;
    g_fds[slot].callback = cb;
    g_fds[slot].active   = 1;
    g_fds[slot].rbuf     = NULL;
    g_fds[slot].rused    = 0;
    g_fds[slot].rcap     = 0;

    /* Fire callback with fd index as the handle */
    Val fd_val = MAKE_INT(slot);
    PUSH_ROOT(fd_val);
    Val a = pl_cons(NIL_VAL, pl_cons(fd_val, NIL_VAL));
    POP_ROOT();
    pl_apply(cb, a, NIL_VAL);
    return NIL_VAL;
}

/* =========================================================================
 * TCP listen
 * ===================================================================== */

/* (tcp-listen port callback) */
static Val prim_tcp_listen(Val args, Val env)
{
    (void)env;
    Val port_val = CAR(args);
    Val cb       = CADR(args);
    if (!IS_INT(port_val)) pl_error_str("tcp-listen: port must be an integer");
    int port = INT_VAL(port_val);

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) pl_error_str("tcp-listen: socket() failed");
    { int opt = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }
    set_nonblocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); pl_error_str("tcp-listen: bind() failed");
    }
    if (listen(fd, 128) < 0) {
        close(fd); pl_error_str("tcp-listen: listen() failed");
    }

    int slot = fd_alloc_slot();
    g_fds[slot].fd       = fd;
    g_fds[slot].kind     = FD_TCP_SERVER;
    g_fds[slot].callback = cb;
    g_fds[slot].active   = 1;
    g_fds[slot].rbuf     = NULL;
    g_fds[slot].rused    = 0;
    g_fds[slot].rcap     = 0;
    return NIL_VAL;
}

/* =========================================================================
 * TCP write / close
 * ===================================================================== */

/* (tcp-write fd-val data) */
static Val prim_tcp_write(Val args, Val env)
{
    (void)env;
    Val fd_val   = CAR(args);
    Val data_val = CADR(args);
    if (!IS_INT(fd_val))  pl_error_str("tcp-write: expected int fd");
    if (!IS_STR(data_val)) pl_error_str("tcp-write: expected string data");

    int slot = INT_VAL(fd_val);
    if (slot < 0 || slot >= g_nfds || !g_fds[slot].active)
        pl_error_str("tcp-write: invalid fd");

    StrSlot *ss = &g_strs[STR_IDX(data_val)];
    size_t  off = 0;
    while (off < ss->len) {
        ssize_t n = send(g_fds[slot].fd, ss->ptr + off, ss->len - off, 0);
        if (n < 0) break;
        off += (size_t)n;
    }
    return NIL_VAL;
}

/* (tcp-close fd-val) */
static Val prim_tcp_close(Val args, Val env)
{
    (void)env;
    Val fd_val = CAR(args);
    if (!IS_INT(fd_val)) pl_error_str("tcp-close: expected int fd");
    int slot = INT_VAL(fd_val);
    if (slot >= 0 && slot < g_nfds && g_fds[slot].active) {
        close(g_fds[slot].fd);
        g_fds[slot].active = 0;
        free(g_fds[slot].rbuf);
        g_fds[slot].rbuf  = NULL;
        g_fds[slot].rused = 0;
        g_fds[slot].rcap  = 0;
    }
    return NIL_VAL;
}

/* =========================================================================
 * HTTP server helpers
 * ===================================================================== */

static void http_serve_client(int client_fd, Val handler)
{
    /* Read until we have full headers */
    size_t cap = 8192, used = 0;
    char *rbuf = malloc(cap);
    if (!rbuf) { close(client_fd); return; }
    rbuf[0] = '\0';

    for (;;) {
        if (used + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(rbuf, cap);
            if (!nb) { free(rbuf); close(client_fd); return; }
            rbuf = nb;
        }
        ssize_t n = recv(client_fd, rbuf + used, cap - used - 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
        rbuf[used] = '\0';
        if (strstr(rbuf, "\r\n\r\n")) break;
    }

    char method[16] = "GET", path[1024] = "/";
    sscanf(rbuf, "%15s %1023s", method, path);
    const char *body_start = strstr(rbuf, "\r\n\r\n");
    const char *req_body   = body_start ? body_start + 4 : "";

    Val mval = MAKE_STR(str_intern(method,   strlen(method)));
    Val pval = MAKE_STR(str_intern(path,     strlen(path)));
    Val bval = MAKE_STR(str_intern(req_body, strlen(req_body)));
    free(rbuf);

    PUSH_ROOT(mval); PUSH_ROOT(pval); PUSH_ROOT(bval);
    Val hargs  = pl_cons(mval, pl_cons(pval, pl_cons(bval, NIL_VAL)));
    POP_ROOT(); POP_ROOT(); POP_ROOT();
    Val result = pl_apply(handler, hargs, NIL_VAL);

    const char *resp_body = "";
    size_t      resp_len  = 0;
    if (IS_STR(result)) {
        resp_body = str_ptr(STR_IDX(result));
        resp_len  = g_strs[STR_IDX(result)].len;
    }

    char hdr[512];
    int  hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n", resp_len);
    char *wbuf = malloc((size_t)hdr_len + resp_len);
    if (wbuf) {
        memcpy(wbuf,            hdr,       (size_t)hdr_len);
        memcpy(wbuf + hdr_len, resp_body,  resp_len);
        size_t tot = (size_t)hdr_len + resp_len, off = 0;
        while (off < tot) {
            ssize_t n = send(client_fd, wbuf + off, tot - off, 0);
            if (n < 0) break;
            off += (size_t)n;
        }
        free(wbuf);
    }
    close(client_fd);
}

/* =========================================================================
 * HTTP serve / get / post primitives
 * ===================================================================== */

/* (http-serve port handler) */
static Val prim_http_serve(Val args, Val env)
{
    (void)env;
    Val port_val = CAR(args);
    Val handler  = CADR(args);
    if (!IS_INT(port_val)) pl_error_str("http-serve: port must be an integer");
    int port = INT_VAL(port_val);

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) pl_error_str("http-serve: socket() failed");
    { int opt = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }
    set_nonblocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); pl_error_str("http-serve: bind() failed");
    }
    if (listen(fd, 128) < 0) {
        close(fd); pl_error_str("http-serve: listen() failed");
    }

    int slot = fd_alloc_slot();
    g_fds[slot].fd      = fd;
    g_fds[slot].kind    = FD_HTTP_CLIENT;
    g_fds[slot].active  = 1;
    g_fds[slot].handler = handler;
    g_fds[slot].rbuf    = NULL;
    g_fds[slot].rused   = 0;
    g_fds[slot].rcap    = 0;
    return NIL_VAL;
}

/* (http-get url callback) */
static Val prim_http_get(Val args, Val env)
{
    (void)env;
    Val url_val = CAR(args);
    Val cb      = CADR(args);
    if (!IS_STR(url_val)) pl_error_str("http-get: url must be a string");
    const char *url = str_ptr(STR_IDX(url_val));
    char *host = NULL, *path = NULL; int port = 80;
    if (!parse_http_url(url, &host, &port, &path)) {
        Val err = MAKE_STR(str_intern("unsupported-url", 15));
        PUSH_ROOT(err);
        Val a  = pl_cons(NIL_VAL, pl_cons(NIL_VAL, NIL_VAL));
        Val ca = pl_cons(err, a); POP_ROOT();
        pl_apply(cb, ca, NIL_VAL); return NIL_VAL;
    }
    return enqueue_async_http("GET", host, port, path, NULL, 0, cb);
}

/* (http-post url body callback) */
static Val prim_http_post(Val args, Val env)
{
    (void)env;
    Val url_val  = CAR(args);
    Val body_val = CADR(args);
    Val cb       = CADDR(args);
    if (!IS_STR(url_val)) pl_error_str("http-post: url must be a string");
    const char *url = str_ptr(STR_IDX(url_val));
    char *req_body_copy = NULL; size_t req_blen = 0;
    if (IS_STR(body_val)) {
        const char *rb = str_ptr(STR_IDX(body_val));
        req_blen = g_strs[STR_IDX(body_val)].len;
        req_body_copy = malloc(req_blen + 1);
        if (req_body_copy) { memcpy(req_body_copy, rb, req_blen); req_body_copy[req_blen] = '\0'; }
    }
    char *host = NULL, *path = NULL; int port = 80;
    if (!parse_http_url(url, &host, &port, &path)) {
        free(req_body_copy);
        Val err = MAKE_STR(str_intern("unsupported-url", 15));
        PUSH_ROOT(err);
        Val a  = pl_cons(NIL_VAL, pl_cons(NIL_VAL, NIL_VAL));
        Val ca = pl_cons(err, a); POP_ROOT();
        pl_apply(cb, ca, NIL_VAL); return NIL_VAL;
    }
    return enqueue_async_http("POST", host, port, path,
                               req_body_copy, req_blen, cb);
}

/* =========================================================================
 * File I/O -- copied verbatim from io_uv.c (no libuv dependency)
 * ===================================================================== */

/* (file-read path callback) */
static Val prim_file_read(Val args, Val env)
{
    (void)env;
    Val path_val = CAR(args);
    Val cb       = CADR(args);
    if (!IS_STR(path_val)) pl_error_str("file-read: path must be a string");

    const char *path = str_ptr(STR_IDX(path_val));
    FILE *f = fopen(path, "rb");
    if (!f) {
        Val err = MAKE_STR(str_intern("error", 5));
        PUSH_ROOT(err);
        Val cb_args = pl_cons(err, NIL_VAL);
        POP_ROOT();
        pl_apply(cb, cb_args, NIL_VAL);
        return NIL_VAL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); pl_error_str("OOM in file-read"); }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    (void)nread;
    fclose(f);
    buf[sz] = '\0';

    uint32_t si  = str_intern(buf, (size_t)sz);
    free(buf);
    Val data = MAKE_STR(si);
    PUSH_ROOT(data);
    Val cb_args = pl_cons(NIL_VAL, pl_cons(data, NIL_VAL));
    POP_ROOT();
    pl_apply(cb, cb_args, NIL_VAL);
    return NIL_VAL;
}

/* (file-write path data callback) */
static Val prim_file_write(Val args, Val env)
{
    (void)env;
    Val path_val = CAR(args);
    Val data_val = CADR(args);
    Val cb       = CADDR(args);
    if (!IS_STR(path_val)) pl_error_str("file-write: path must be a string");
    if (!IS_STR(data_val)) pl_error_str("file-write: data must be a string");

    const char *path = str_ptr(STR_IDX(path_val));
    StrSlot    *ss   = &g_strs[STR_IDX(data_val)];
    FILE *f = fopen(path, "wb");
    if (!f) {
        Val err = MAKE_STR(str_intern("error", 5));
        PUSH_ROOT(err);
        Val cb_args = pl_cons(err, NIL_VAL);
        POP_ROOT();
        pl_apply(cb, cb_args, NIL_VAL);
        return NIL_VAL;
    }
    fwrite(ss->ptr, 1, ss->len, f);
    fclose(f);
    pl_apply(cb, pl_cons(NIL_VAL, NIL_VAL), NIL_VAL);
    return NIL_VAL;
}

/* =========================================================================
 * Event loop
 * ===================================================================== */

/* (event-loop) / (uv-run) */
static Val prim_event_loop(Val args, Val env)
{
    (void)args; (void)env;

    while (has_active_handles()) {
        fire_expired_timers();
        fire_async_http();

        /* Build pollfd array for active fds */
        struct pollfd pfds[MAX_FDS];
        int           pmap[MAX_FDS]; /* pfds[i] -> g_fds[pmap[i]] */
        int           nfds = 0;

        for (int i = 0; i < g_nfds; i++) {
            if (!g_fds[i].active) continue;
            pfds[nfds].fd      = g_fds[i].fd;
            pfds[nfds].events  = POLLIN;
            pfds[nfds].revents = 0;
            pmap[nfds]         = i;
            nfds++;
        }

        int timeout = compute_poll_timeout();
        if (nfds == 0) {
            /* Only timers -- just sleep for the timeout */
            struct timespec ts = {
                .tv_sec  = timeout / 1000,
                .tv_nsec = (timeout % 1000) * 1000000L
            };
            nanosleep(&ts, NULL);
            continue;
        }

        int ready = poll(pfds, (nfds_t)nfds, timeout);
        if (ready <= 0) continue;

        for (int i = 0; i < nfds; i++) {
            if (!(pfds[i].revents & POLLIN)) continue;
            int    slot = pmap[i];
            FdSlot *fs  = &g_fds[slot];

            if (fs->kind == FD_TCP_SERVER) {
                /* Accept new connection, fire callback */
                int client_fd = accept(fs->fd, NULL, NULL);
                if (client_fd < 0) continue;
                int cslot = fd_alloc_slot();
                g_fds[cslot].fd       = client_fd;
                g_fds[cslot].kind     = FD_TCP_CLIENT;
                g_fds[cslot].callback = fs->callback;
                g_fds[cslot].active   = 1;
                g_fds[cslot].rbuf     = NULL;
                g_fds[cslot].rused    = 0;
                g_fds[cslot].rcap     = 0;
                Val fd_val = MAKE_INT(cslot);
                PUSH_ROOT(fd_val);
                Val a = pl_cons(NIL_VAL, pl_cons(fd_val, NIL_VAL));
                POP_ROOT();
                pl_apply(fs->callback, a, NIL_VAL);
            } else if (fs->kind == FD_HTTP_CLIENT) {
                /* Accept and dispatch HTTP request inline */
                int client_fd = accept(fs->fd, NULL, NULL);
                if (client_fd < 0) continue;
                http_serve_client(client_fd, fs->handler);
            } else {
                /* TCP client data available -- fire callback (NIL fd data) */
                char tmp[4096];
                ssize_t n = recv(fs->fd, tmp, sizeof(tmp), 0);
                if (n <= 0) {
                    /* Connection closed -- fire callback (NIL fd NIL) */
                    Val fd_v = MAKE_INT(slot);
                    PUSH_ROOT(fd_v);
                    Val a = pl_cons(NIL_VAL, pl_cons(fd_v, pl_cons(NIL_VAL, NIL_VAL)));
                    POP_ROOT();
                    pl_apply(fs->callback, a, NIL_VAL);
                    close(fs->fd);
                    fs->active = 0;
                    free(fs->rbuf); fs->rbuf = NULL;
                } else {
                    Val data = MAKE_STR(str_intern(tmp, (size_t)n));
                    Val fd_v = MAKE_INT(slot);
                    PUSH_ROOT(data); PUSH_ROOT(fd_v);
                    Val a = pl_cons(NIL_VAL, pl_cons(fd_v, pl_cons(data, NIL_VAL)));
                    POP_ROOT(); POP_ROOT();
                    pl_apply(fs->callback, a, NIL_VAL);
                }
            }
        }
    }
    return NIL_VAL;
}

/* =========================================================================
 * Init and registration
 * ===================================================================== */

void io_init(void)
{
    memset(g_timers, 0, sizeof(g_timers));
    g_ntimers = 0;
    memset(g_fds, 0, sizeof(g_fds));
    g_nfds = 0;
}

void io_prims_register(void)
{
    prim_register("tcp-connect",  prim_tcp_connect,  3,  3);
    prim_register("tcp-listen",   prim_tcp_listen,   2,  2);
    prim_register("tcp-write",    prim_tcp_write,    2,  2);
    prim_register("tcp-close",    prim_tcp_close,    1,  1);
    prim_register("timer",        prim_timer,        2,  2);
    prim_register("file-read",    prim_file_read,    2,  2);
    prim_register("file-write",   prim_file_write,   3,  3);
    prim_register("event-loop",   prim_event_loop,   0,  0);
    prim_register("uv-run",       prim_event_loop,   0,  0);
    prim_register("http-get",     prim_http_get,     2,  2);
    prim_register("http-post",    prim_http_post,    3,  3);
    /* http-serve is now implemented in lib/http.l using tcp-listen */
}
