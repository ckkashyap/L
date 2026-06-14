#pragma once

/* msvc_compat.h -- Compatibility shims for MSVC (cl.exe)
 * Included before any system headers in files that need it, but picolisp.h
 * pulls this in automatically when _MSC_VER is defined.
 */

#ifdef _MSC_VER

/* -----------------------------------------------------------------------
 * Suppress MSVC CRT security warnings for standard C string functions.
 * Must be defined before any CRT header is included.
 * --------------------------------------------------------------------- */
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS 1
#endif

/* -----------------------------------------------------------------------
 * <unistd.h> does not exist on Windows / MSVC.  Provide the tiny subset
 * that our code actually uses.
 * --------------------------------------------------------------------- */
/* Guard against accidental direct inclusion of <unistd.h> */
#define _UNISTD_H   /* common guard used by MinGW */
#define _UNISTD_H_  /* alternate guard */

#include <io.h>       /* _read, _write, _close, _isatty ... */
#include <process.h>  /* _getpid */

/* POSIX names that map to underscore-prefixed CRT equivalents */
#ifndef STDIN_FILENO
#  define STDIN_FILENO  0
#endif
#ifndef STDOUT_FILENO
#  define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#  define STDERR_FILENO 2
#endif

/* -----------------------------------------------------------------------
 * ssize_t
 * --------------------------------------------------------------------- */
#ifndef _SSIZE_T_DEFINED
#  define _SSIZE_T_DEFINED
#  include <stddef.h>
   typedef ptrdiff_t ssize_t;
#endif

/* -----------------------------------------------------------------------
 * inline -- older MSVC (pre-C99 mode) does not recognise the bare keyword.
 * --------------------------------------------------------------------- */
#if defined(_MSC_VER) && (_MSC_VER < 1900) && !defined(__cplusplus)
#  define inline __inline
#endif

/* -----------------------------------------------------------------------
 * GCC / Clang attributes that MSVC does not understand.
 * --------------------------------------------------------------------- */

/* __attribute__((unused)) -- silence unused-variable warnings.
 * MSVC uses #pragma warning or (void)x; just swallow the attribute. */
#ifndef __attribute__
#  define __attribute__(x)  /* nothing */
#endif

/* __attribute__((noreturn)) analogue */
#ifndef __noreturn
#  define __noreturn __declspec(noreturn)
#endif

/* -----------------------------------------------------------------------
 * __builtin_* GCC intrinsics
 * --------------------------------------------------------------------- */

/* Branch-prediction hint */
#ifndef __builtin_expect
#  define __builtin_expect(x, v)  (x)
#endif

/* Unreachable code marker -- MSVC uses __assume(0) for the same effect */
#ifndef __builtin_unreachable
#  define __builtin_unreachable()  __assume(0)
#endif

/* -----------------------------------------------------------------------
 * alloca
 * --------------------------------------------------------------------- */
#include <malloc.h>  /* provides _alloca on MSVC */
#ifndef alloca
#  define alloca _alloca
#endif

/* -----------------------------------------------------------------------
 * strdup / strndup
 * --------------------------------------------------------------------- */
#ifndef strdup
#  define strdup _strdup
#endif

#ifndef strndup
#  include <stddef.h>  /* size_t */
#  include <stdlib.h>  /* malloc */
#  include <string.h>  /* memcpy, strnlen */
static inline char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char  *p   = (char *)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}
#endif

/* -----------------------------------------------------------------------
 * static_assert
 * C11 _Static_assert is available in VS 2015+ (cl 19+).
 * For older compilers provide a fallback.
 * --------------------------------------------------------------------- */
#if defined(_MSC_VER) && (_MSC_VER < 1900)
#  ifndef static_assert
#    define static_assert(cond, msg)  \
         typedef char _static_assert_##__LINE__[(cond) ? 1 : -1]
#  endif
#endif

/* -----------------------------------------------------------------------
 * MSVC does not define __func__ in C mode before VS 2015.
 * --------------------------------------------------------------------- */
#if defined(_MSC_VER) && (_MSC_VER < 1900) && !defined(__func__)
#  define __func__  __FUNCTION__
#endif

/* -----------------------------------------------------------------------
 * Disable a few noisy MSVC warnings that fire on valid C code.
 *
 *   C4200 -- zero-sized array in struct/union
 *   C4201 -- nameless struct/union
 *   C4204 -- non-constant aggregate initialiser
 *   C4214 -- bit-field type other than int
 * --------------------------------------------------------------------- */
#pragma warning(disable: 4200 4201 4204 4214)

#endif /* _MSC_VER */
