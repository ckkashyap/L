/* tests/helpers/cb_helper.c
 * Minimal C shared library used by tests/test_callbacks.l.
 * Exports simple functions that accept C function pointers, allowing
 * the test to verify that Lisp closures are called correctly from C. */

#ifdef _WIN32
#  define EXPORT __declspec(dllexport)
#else
#  define EXPORT
#endif

/* call1: call fn(x) and return the result */
EXPORT int call1(int (*fn)(int), int x)
{
    return fn(x);
}

/* call2: call fn(a, b) and return the result */
EXPORT int call2(int (*fn)(int, int), int a, int b)
{
    return fn(a, b);
}

/* call0: call fn() for side effects (void return) */
EXPORT void call0(void (*fn)(void))
{
    fn();
}

/* call_and_add: call fn(x), add extra to the result */
EXPORT int call_and_add(int (*fn)(int), int x, int extra)
{
    return fn(x) + extra;
}
