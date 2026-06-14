/* main.c -- Entry point for the L (lispIsPerfect9) interpreter.
 *
 * Responsibilities:
 *   - Initialise all subsystems (heap, symbols, bignums, primitives, I/O).
 *   - Optionally load lib/stdlib.l.
 *   - Parse command-line flags (-e expr, file args).
 *   - Run the REPL when no file or -e flag is given.
 */

#include "picolisp.h"
#include "coro.h"
#include "native_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

/* =========================================================================
 * Error reporting
 * ===================================================================== */

void pl_error(const char *msg, Val irritant)
{
    fprintf(stderr, "ERROR: %s: ", msg);
    Port p = make_file_port(stderr);
    pl_print(irritant, &p);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(1);
}

void pl_error_str(const char *msg)
{
    fprintf(stderr, "ERROR: %s\n", msg);
    fflush(stderr);
    exit(1);
}

/* =========================================================================
 * File loader
 * ===================================================================== */

void load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open file: %s\n", path);
        exit(1);
    }
    Reader r;
    reader_init_file(&r, f);
    for (;;) {
        Val expr = read_one(&r);
        if (expr == EOF_VAL) break;
        PUSH_ROOT(expr);
        eval(expr, NIL_VAL);
        POP_ROOT();
    }
    fclose(f);
}

/* =========================================================================
 * REPL
 * ===================================================================== */

static void repl(void)
{
    Reader r;
    reader_init_file(&r, stdin);
    for (;;) {
        printf("> ");
        fflush(stdout);
        Val expr = read_one(&r);
        if (expr == EOF_VAL) {
            printf("\n");
            break;
        }
        PUSH_ROOT(expr);
        Val result = eval(expr, NIL_VAL);
        POP_ROOT();
        pl_println(result, &g_stdout_port);
    }
}

/* =========================================================================
 * Build the *args* list from argv
 * ===================================================================== */

static Val build_args(int argc, char **argv)
{
    Val args = NIL_VAL;
    PUSH_ROOT(args);
    /* Iterate in reverse so that consing builds the list in forward order */
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t si = str_intern(argv[i], strlen(argv[i]));
        args = pl_cons(MAKE_STR(si), args);
        /* Update the root so GC can see the growing list */
        g_gc_roots[g_gc_root_top - 1] = args;
    }
    POP_ROOT();
    return args;
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(int argc, char **argv)
{
    /* ----- Initialise subsystems ---------------------------------------- */
    heap_init();
    sym_init();
    bignum_init();
    prims_init();
    extern void eval_init(void);
    eval_init();
    extern void pipe_prims_register(void);
    extern void ffi_prims_register(void);
    extern void gfx_prims_register(void);
#ifdef HAVE_TINYGL
    extern void tinygl_prims_register(void);
#endif

    coro_init();
    coro_prims_register();
    io_init();
    io_prims_register();
    pipe_prims_register();
    ffi_prims_register();
    gfx_prims_register();
#ifdef HAVE_TINYGL
    tinygl_prims_register();
#endif

    /* ----- Optionally load the standard library ------------------------- */
    {
        FILE *f = fopen("lib/stdlib.l", "r");
        if (f) {
            fclose(f);
            load_file("lib/stdlib.l");
        }
    }

    /* ----- Bind *args* -------------------------------------------------- */
    Val args_val = build_args(argc, argv);
    PUSH_ROOT(args_val);
    g_syms[g_sym_args].value = args_val;
    POP_ROOT();

    /* ----- Parse command line ------------------------------------------- */
    bool ran_something = false;
    int i = 1;

    while (i < argc) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            /* Evaluate an expression supplied on the command line */
            i++;
            Val expr = read_from_string(argv[i]);
            PUSH_ROOT(expr);
            Val result = eval(expr, NIL_VAL);
            POP_ROOT();
            pl_println(result, &g_stdout_port);
            ran_something = true;
            i++;

        } else if (strcmp(argv[i], "--") == 0) {
            /* End of interpreter flags; remaining args belong to the script */
            i++;
            break;

        } else if (argv[i][0] != '-') {
            /* Bare argument: treat as a file to load; remaining args
               belong to the script (accessible via *args*). */
            load_file(argv[i]);
            ran_something = true;
            break;

        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            i++;
        }
    }

    /* ----- Fall back to REPL if nothing was executed -------------------- */
    if (!ran_something) {
        repl();
    }

    return 0;
}
