/* src/term_helper.c -- terminal FFI helper for vi editor */
#ifndef _WIN32
/* Enable POSIX (sigaction, struct sigaction) and BSD (cfmakeraw) APIs
 * even when the compiler is invoked in a strict mode like -std=c17.   */
#  define _DEFAULT_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define EXPORT __declspec(dllexport)
#else
#  include <termios.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#  include <errno.h>
#  include <signal.h>
#  define EXPORT
#endif
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
static HANDLE g_hin = INVALID_HANDLE_VALUE;
static DWORD  g_orig_mode = 0;
static int    g_raw_active = 0;
#else
static struct termios g_orig;
static int            g_raw_active = 0;
#endif
static int g_atexit_installed = 0;
static int g_alt_screen_active = 0;

/* Restore terminal to cooked mode + visible cursor + default attrs +
 * leave alternate screen buffer.  Called explicitly by term-set-raw(0)
 * and via atexit() / signal handlers so any exit path -- normal,
 * signal, crash -- leaves the terminal usable without `reset`.       */
static void term_restore(void) {
    /* Leave alt screen buffer first so the user's shell content
     * (preserved by the terminal) becomes visible again.              */
    if (g_alt_screen_active) {
        fputs("\033[?1049l", stdout);
        g_alt_screen_active = 0;
    }
    /* Cursor visible + attributes reset.  Always emit -- a renderer
     * may have hidden the cursor or left bold/reverse video on.       */
    fputs("\033[?25h\033[0m", stdout);
    fflush(stdout);

#ifdef _WIN32
    if (g_raw_active) {
        SetConsoleMode(g_hin, g_orig_mode);
        g_raw_active = 0;
    }
#else
    if (g_raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
        g_raw_active = 0;
    }
#endif
}

#ifndef _WIN32
/* Signal handler: restore terminal then re-raise the signal with the
 * default action so the process exits with the right status code.    */
static void term_signal_handler(int signo) {
    term_restore();
    signal(signo, SIG_DFL);
    raise(signo);
}
#endif

static void term_atexit_install(void) {
    if (g_atexit_installed) return;
    g_atexit_installed = 1;
    atexit(term_restore);
#ifndef _WIN32
    /* Install handlers for fatal signals so the terminal is restored
     * even if the program is killed (Ctrl-C in cooked mode, terminal
     * hangup, segfault, etc.).                                        */
    struct sigaction sa;
    sa.sa_handler = term_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#endif
}

/* Enter alternate screen buffer (\e[?1049h).  Real vim does this so
 * the editor's content lives in a separate screen and the user's
 * shell scrollback is restored exactly on exit.                      */
EXPORT int term_alt_screen(int on) {
    term_atexit_install();
    if (on) {
        if (!g_alt_screen_active) {
            fputs("\033[?1049h", stdout);
            fflush(stdout);
            g_alt_screen_active = 1;
        }
    } else {
        if (g_alt_screen_active) {
            fputs("\033[?1049l", stdout);
            fflush(stdout);
            g_alt_screen_active = 0;
        }
    }
    return 0;
}

EXPORT int term_set_raw(int on) {
    term_atexit_install();
#ifdef _WIN32
    if (g_hin == INVALID_HANDLE_VALUE)
        g_hin = GetStdHandle(STD_INPUT_HANDLE);
    if (on) {
        GetConsoleMode(g_hin, &g_orig_mode);
        DWORD m = g_orig_mode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT);
        int r = SetConsoleMode(g_hin, m) ? 0 : -1;
        if (r == 0) g_raw_active = 1;
        return r;
    }
    if (!g_raw_active) return 0;
    g_raw_active = 0;
    return SetConsoleMode(g_hin, g_orig_mode) ? 0 : -1;
#else
    if (on) {
        struct termios raw;
        if (tcgetattr(STDIN_FILENO, &g_orig) < 0) return -1;
        raw = g_orig;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        int r = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        if (r == 0) g_raw_active = 1;
        return r;
    }
    if (!g_raw_active) return 0;
    g_raw_active = 0;
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
#endif
}

EXPORT int term_get_cols(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return 80;
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) return 80;
    return ws.ws_col;
#endif
}

EXPORT int term_get_rows(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return 24;
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_row == 0) return 24;
    return ws.ws_row;
#endif
}

EXPORT int term_read_byte(void) {
#ifdef _WIN32
    if (g_hin == INVALID_HANDLE_VALUE)
        g_hin = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD ir; DWORD n;
    while (1) {
        if (!ReadConsoleInputA(g_hin, &ir, 1, &n)) return -1;
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            char c = ir.Event.KeyEvent.uChar.AsciiChar;
            if (c) return (unsigned char)c;
        }
    }
#else
    unsigned char b;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &b, 1);
    } while (n == -1 && errno == EINTR);
    return (n == 1) ? (int)b : -1;
#endif
}

EXPORT int term_read_byte_timeout(int ms) {
#ifdef _WIN32
    if (g_hin == INVALID_HANDLE_VALUE)
        g_hin = GetStdHandle(STD_INPUT_HANDLE);
    if (WaitForSingleObject(g_hin, (DWORD)ms) != WAIT_OBJECT_0) return -1;
    return term_read_byte();
#else
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { ms/1000, (ms%1000)*1000 };
    int r;
    do {
        r = select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    } while (r == -1 && errno == EINTR);
    if (r <= 0) return -1;
    unsigned char b;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &b, 1);
    } while (n == -1 && errno == EINTR);
    return (n == 1) ? (int)b : -1;
#endif
}
