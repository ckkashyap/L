/* pipe_posix.c -- subprocess I/O via POSIX fork/exec */
#define _GNU_SOURCE
#include "picolisp.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

/* =========================================================================
 * PipeSlot pool
 * ===================================================================== */

#define PIPE_POOL_SIZE 1024

typedef struct {
    pid_t   pid;
    int     stdin_fd;    /* write end of stdin pipe (parent side) */
    int     stdout_fd;   /* read end of stdout pipe (parent side) */
    int     alive;
    int     exit_code;
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
            /* Unmarked live pipe: close it to avoid fd leak */
            if (g_pipes[i].stdin_fd  >= 0) close(g_pipes[i].stdin_fd);
            if (g_pipes[i].stdout_fd >= 0) close(g_pipes[i].stdout_fd);
            waitpid(g_pipes[i].pid, NULL, WNOHANG);
            free(g_pipes[i].rbuf);
            g_pipes[i].rbuf  = NULL;
            g_pipes[i].alive = 0;
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

    /* Build argv: [cmd, arg1, ..., argN, NULL] */
    char **argv = (char **)malloc((size_t)(nargs + 2) * sizeof(char *));
    if (!argv) pl_error_str("spawn: out of memory");
    argv[0] = (char *)cmd;
    {
        int ai  = 1;
        Val cur = rest;
        while (IS_CONS(cur)) {
            Val a = CAR(cur);
            if (!IS_STR(a)) { free(argv); pl_error_str("spawn: args must be strings"); }
            argv[ai++] = (char *)str_ptr(STR_IDX(a));
            cur = CDR(cur);
        }
        argv[ai] = NULL;
    }

    /* Create stdin and stdout pipes with O_CLOEXEC (I3) */
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe2(stdin_pipe, O_CLOEXEC) < 0) {
        free(argv); pl_error_str("spawn: pipe() for stdin failed");
    }
    if (pipe2(stdout_pipe, O_CLOEXEC) < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        free(argv); pl_error_str("spawn: pipe() for stdout failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        free(argv); pl_error_str("spawn: fork() failed");
    }

    if (pid == 0) {
        /* Child process */
        dup2(stdin_pipe[0],  STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        /* Close all pipe ends in child */
        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        execvp(cmd, argv);
        /* execvp failed */
        _exit(127);
    }

    /* Parent: close child-side ends */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    free(argv);

    int slot    = pipe_alloc_slot();
    PipeSlot *ps = &g_pipes[slot];
    memset(ps, 0, sizeof(*ps));
    ps->pid       = pid;
    ps->stdin_fd  = stdin_pipe[1];
    ps->stdout_fd = stdout_pipe[0];
    ps->alive     = 1;
    ps->exit_code = -1;

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
    if (ps->stdout_fd < 0)
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
        ssize_t n = read(ps->stdout_fd, ps->rbuf + ps->rlen, ps->rcap - ps->rlen - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break; /* EOF */
        ps->rlen += (size_t)n;
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
    if (!ps->alive || ps->stdin_fd < 0)
        pl_error_str("pipe-write: stdin is already closed");

    const char *data = str_ptr(STR_IDX(sv));
    size_t      len  = g_strs[STR_IDX(sv)].len;
    size_t      off  = 0;
    while (off < len) {
        ssize_t n = write(ps->stdin_fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)n;
    }
    return NIL_VAL;
}

/* (pipe-close-stdin pipe) -> NIL
 *
 * Closes the write end of the pipe (sends EOF to child's stdin).
 * Idempotent: safe to call even after the child has exited or stdin was
 * already closed -- subsequent calls are silent no-ops.
 */
static Val prim_pipe_close_stdin(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) pl_error_str("pipe-close-stdin: expected pipe");
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) pl_error_str("pipe-close-stdin: invalid pipe");
    PipeSlot *ps  = &g_pipes[idx];
    /* Allow close-stdin even if the child has already exited (alive may be
       0 if pipe-close was called, but alive==0 with stdin_fd>=0 is unusual;
       guard only against a fully-freed slot). */
    if (!ps->alive && ps->stdin_fd < 0) return NIL_VAL;

    if (ps->stdin_fd >= 0) {
        close(ps->stdin_fd);
        ps->stdin_fd = -1;
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
    if (!ps->alive) return MAKE_INT(ps->exit_code);

    if (ps->stdin_fd >= 0)  { close(ps->stdin_fd);  ps->stdin_fd  = -1; }
    if (ps->stdout_fd >= 0) { close(ps->stdout_fd); ps->stdout_fd = -1; }

    int wstatus = 0;
    waitpid(ps->pid, &wstatus, 0);
    int code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;

    ps->alive     = 0;
    ps->exit_code = code;
    free(ps->rbuf);
    ps->rbuf = NULL;

    return MAKE_INT(code);
}

/* (pipe-alive? pipe) -> T or NIL
 *
 * Returns T as long as the pipe slot has not been explicitly closed via
 * pipe-close.  We deliberately do NOT call waitpid here so that a process
 * that exits quickly (e.g. a simple echo) does not prematurely flip the
 * alive flag -- the test suite relies on the pipe remaining usable for
 * pipe-read-all / pipe-close-stdin even after the child has exited.
 */
static Val prim_pipe_alive(Val args, Val env)
{
    (void)env;
    Val pv = CAR(args);
    if (!IS_PIPE(pv)) return NIL_VAL;
    uint32_t  idx = PIPE_IDX(pv);
    if ((int)idx >= g_pipe_count) return NIL_VAL;
    return g_pipes[idx].alive ? T_VAL : NIL_VAL;
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
