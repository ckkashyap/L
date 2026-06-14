/* pipe_win32.c -- subprocess I/O via CreateProcess */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "picolisp.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * PipeSlot pool
 * ===================================================================== */

#define PIPE_POOL_SIZE 1024

typedef struct {
    HANDLE  proc;        /* process handle */
    HANDLE  stdin_wr;    /* parent writes here */
    HANDLE  stdout_rd;   /* parent reads here */
    int     alive;
    DWORD   exit_code;
    uint8_t mark;
    char   *rbuf;
    size_t  rlen;
    size_t  rcap;
} PipeSlot;

static PipeSlot g_pipes[PIPE_POOL_SIZE];
static int      g_pipe_count = 0;

/* =========================================================================
 * GC hooks -- called from heap.c
 * ===================================================================== */

void mark_pipe(uint32_t idx)
{
    if ((int)idx < g_pipe_count) g_pipes[idx].mark = 1;
}

void gc_sweep_pipes(void)
{
    for (int i = 0; i < g_pipe_count; i++) {
        if (!g_pipes[i].alive) continue;
        if (!g_pipes[i].mark) {
            /* Unmarked live pipe: close it to avoid handle leak */
            if (g_pipes[i].stdin_wr)  CloseHandle(g_pipes[i].stdin_wr);
            if (g_pipes[i].stdout_rd) CloseHandle(g_pipes[i].stdout_rd);
            TerminateProcess(g_pipes[i].proc, 0);
            CloseHandle(g_pipes[i].proc);
            free(g_pipes[i].rbuf);
            g_pipes[i].rbuf     = NULL;
            g_pipes[i].stdin_wr  = NULL;
            g_pipes[i].stdout_rd = NULL;
            g_pipes[i].proc      = NULL;
            g_pipes[i].alive     = 0;
        }
        g_pipes[i].mark = 0;
    }
}

/* =========================================================================
 * Slot allocation
 * ===================================================================== */

static int pipe_alloc_slot(void)
{
    for (int i = 0; i < g_pipe_count; i++)
        if (!g_pipes[i].alive) return i;
    if (g_pipe_count >= PIPE_POOL_SIZE)
        pl_error_str("spawn: too many open pipes (limit 1024)");
    return g_pipe_count++;
}

/* =========================================================================
 * Primitives
 * ===================================================================== */

/* (spawn cmd arg...) -> pipe */
static Val prim_spawn(Val args, Val env)
{
    (void)env;
    Val cmd_val = CAR(args);
    if (!IS_STR(cmd_val)) pl_error_str("spawn: first arg must be a string (command)");
    const char *cmd = str_ptr(STR_IDX(cmd_val));

    Val  rest  = CDR(args);
    int  nargs = val_list_len(rest);

    /* Build cmdline: "cmd arg1 arg2 ..." (space-separated) */
    size_t cmdline_len = strlen(cmd) + 1;
    {
        Val cur = rest;
        while (IS_CONS(cur)) {
            Val a = CAR(cur);
            if (!IS_STR(a)) pl_error_str("spawn: args must be strings");
            cmdline_len += 1 + g_strs[STR_IDX(a)].len; /* space + arg */
            cur = CDR(cur);
        }
    }
    char *cmdline = (char *)malloc(cmdline_len + 1);
    if (!cmdline) pl_error_str("spawn: out of memory");
    {
        size_t pos = 0;
        size_t clen = strlen(cmd);
        memcpy(cmdline + pos, cmd, clen); pos += clen;
        Val cur = rest;
        while (IS_CONS(cur)) {
            Val a = CAR(cur);
            cmdline[pos++] = ' ';
            size_t alen = g_strs[STR_IDX(a)].len;
            memcpy(cmdline + pos, str_ptr(STR_IDX(a)), alen);
            pos += alen;
            cur = CDR(cur);
        }
        cmdline[pos] = '\0';
    }
    (void)nargs;

    /* Create stdin pipe: child reads from stdin_rd, parent writes to stdin_wr */
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE; /* inheritable by default */

    HANDLE stdin_rd = NULL, stdin_wr = NULL;
    HANDLE stdout_rd = NULL, stdout_wr = NULL;

    if (!CreatePipe(&stdin_rd, &stdin_wr, &sa, 0)) {
        free(cmdline); pl_error_str("spawn: CreatePipe for stdin failed");
    }
    /* Make parent-side write handle non-inheritable */
    SetHandleInformation(stdin_wr, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stdout_rd, &stdout_wr, &sa, 0)) {
        CloseHandle(stdin_rd); CloseHandle(stdin_wr);
        free(cmdline); pl_error_str("spawn: CreatePipe for stdout failed");
    }
    /* Make parent-side read handle non-inheritable */
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb          = sizeof(si);
    si.hStdInput   = stdin_rd;
    si.hStdOutput  = stdout_wr;
    si.hStdError   = stdout_wr; /* merge stderr into stdout */
    si.dwFlags     = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL,       /* application name (use cmdline) */
        cmdline,    /* command line */
        NULL,       /* process security attributes */
        NULL,       /* thread security attributes */
        TRUE,       /* inherit handles */
        0,          /* creation flags */
        NULL,       /* environment */
        NULL,       /* current directory */
        &si,
        &pi
    );
    free(cmdline);

    /* Close child-side handles in parent */
    CloseHandle(stdin_rd);
    CloseHandle(stdout_wr);

    if (!ok) {
        CloseHandle(stdin_wr);
        CloseHandle(stdout_rd);
        pl_error_str("spawn: CreateProcess failed");
    }
    CloseHandle(pi.hThread); /* we don't need the thread handle */

    int slot      = pipe_alloc_slot();
    PipeSlot *ps  = &g_pipes[slot];
    memset(ps, 0, sizeof(*ps));
    ps->proc      = pi.hProcess;
    ps->stdin_wr  = stdin_wr;
    ps->stdout_rd = stdout_rd;
    ps->alive     = 1;
    ps->exit_code = (DWORD)-1;

    return MAKE_PIPE((uint32_t)slot);
}

/* (pipe-read-all pipe) -> string
 *
 * Reads all stdout bytes until EOF, returns as L string. */
static Val prim_pipe_read_all(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) pl_error_str("pipe-read-all: expected pipe");
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) pl_error_str("pipe-read-all: invalid pipe");
    PipeSlot *ps  = &g_pipes[idx];
    if (!ps->alive || !ps->stdout_rd)
        pl_error_str("pipe-read-all: pipe is closed");

    ps->rlen = 0;
    if (!ps->rbuf) {
        ps->rcap = 4096;
        ps->rbuf = (char *)malloc(ps->rcap);
        if (!ps->rbuf) pl_error_str("pipe-read-all: out of memory");
    }
    ps->rbuf[0] = '\0';

    for (;;) {
        if (ps->rlen + 4096 > ps->rcap) {
            size_t new_cap = ps->rcap * 2 + 4096;
            char *nb = (char *)realloc(ps->rbuf, new_cap);
            if (!nb) pl_error_str("pipe-read-all: out of memory");
            ps->rbuf = nb;
            ps->rcap = new_cap;
        }
        DWORD nread = 0;
        BOOL  ok    = ReadFile(ps->stdout_rd,
                               ps->rbuf + ps->rlen,
                               (DWORD)(ps->rcap - ps->rlen - 1),
                               &nread, NULL);
        if (!ok || nread == 0) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF || nread == 0)
                break; /* EOF */
            break;
        }
        ps->rlen += (size_t)nread;
        ps->rbuf[ps->rlen] = '\0';
    }

    uint32_t si = str_intern(ps->rbuf ? ps->rbuf : "", ps->rlen);
    return MAKE_STR(si);
}

/* (pipe-write pipe data) -> NIL */
static Val prim_pipe_write(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    Val sv = CADR(args);
    if (!IS_PIPE(pv)) pl_error_str("pipe-write: expected pipe");
    if (!IS_STR(sv))  pl_error_str("pipe-write: expected string");
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) pl_error_str("pipe-write: invalid pipe");
    PipeSlot *ps  = &g_pipes[idx];
    if (!ps->alive || !ps->stdin_wr)
        pl_error_str("pipe-write: stdin is already closed");

    const char *data = str_ptr(STR_IDX(sv));
    size_t      len  = g_strs[STR_IDX(sv)].len;
    size_t      off  = 0;
    while (off < len) {
        DWORD nwritten = 0;
        BOOL  ok = WriteFile(ps->stdin_wr, data + off, (DWORD)(len - off), &nwritten, NULL);
        if (!ok || nwritten == 0) break;
        off += (size_t)nwritten;
    }
    return NIL_VAL;
}

/* (pipe-close-stdin pipe) -> NIL */
static Val prim_pipe_close_stdin(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) pl_error_str("pipe-close-stdin: expected pipe");
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) pl_error_str("pipe-close-stdin: invalid pipe");
    PipeSlot *ps  = &g_pipes[idx];
    if (!ps->alive) pl_error_str("pipe-close-stdin: pipe is closed");

    if (ps->stdin_wr) {
        CloseHandle(ps->stdin_wr);
        ps->stdin_wr = NULL;
    }
    return NIL_VAL;
}

/* (pipe-close pipe) -> exit-code */
static Val prim_pipe_close(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) pl_error_str("pipe-close: expected pipe");
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) pl_error_str("pipe-close: invalid pipe");
    PipeSlot *ps  = &g_pipes[idx];
    if (!ps->alive) return MAKE_INT((int)ps->exit_code);

    if (ps->stdin_wr)  { CloseHandle(ps->stdin_wr);  ps->stdin_wr  = NULL; }
    if (ps->stdout_rd) { CloseHandle(ps->stdout_rd); ps->stdout_rd = NULL; }

    WaitForSingleObject(ps->proc, 5000);
    DWORD code = 0;
    GetExitCodeProcess(ps->proc, &code);
    CloseHandle(ps->proc);
    ps->proc = NULL;

    ps->alive     = 0;
    ps->exit_code = code;
    free(ps->rbuf);
    ps->rbuf = NULL;

    return MAKE_INT((int)code);
}

/* (pipe-alive? pipe) -> T or NIL
 *
 * Returns T if the pipe handle is still open (pipe-close has not been called).
 * The child process may have already exited but the pipe is still usable for
 * reading remaining buffered output until pipe-close is called. */
static Val prim_pipe_alive(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) return NIL_VAL;
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) return NIL_VAL;
    PipeSlot *ps = &g_pipes[idx];
    return ps->alive ? T_VAL : NIL_VAL;
}

/* =========================================================================
 * Registration
 * ===================================================================== */

void pipe_prims_register(void)
{
    prim_register("spawn",            prim_spawn,            1, -1);
    prim_register("pipe-read-all",    prim_pipe_read_all,    1,  1);
    prim_register("pipe-write",       prim_pipe_write,       2,  2);
    prim_register("pipe-close-stdin", prim_pipe_close_stdin, 1,  1);
    prim_register("pipe-close",       prim_pipe_close,       1,  1);
    prim_register("pipe-alive?",      prim_pipe_alive,       1,  1);
}
