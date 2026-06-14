/* native_gfx_x11.c -- Xlib + ALSA graphics backend, replaces sdl_gfx.c on Linux */
#define _POSIX_C_SOURCE 200809L
#include "picolisp.h"
#include "native_gfx.h"
#include "ttf.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <alsa/asoundlib.h>
#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <dirent.h>

/* =========================================================================
 * Global X11 state
 * ===================================================================== */
static Display  *g_dpy    = NULL;
static Window    g_win    = 0;
static Pixmap    g_back   = 0;   /* offscreen backing pixmap */
static GC        g_gc     = 0;
static int       g_w      = 0, g_h = 0;
static uint32_t  g_color  = 0;  /* current draw color (XRGB) */
static uint32_t *g_pixels = NULL;  /* for tgl-present; NULL when not needed */
static Atom      g_wm_delete_window = 0;  /* WM_DELETE_WINDOW atom for close-button */

/* =========================================================================
 * Global font state
 * ===================================================================== */
#define MAX_FONTS 32
static TtfFont *g_fonts[MAX_FONTS];
static int      g_font_sizes[MAX_FONTS];
static int      g_font_count = 0;

/* =========================================================================
 * Audio constants & ALSA state
 * ===================================================================== */
#define AUDIO_SR 44100
#define AUDIO_CH 2

static snd_pcm_t *g_pcm        = NULL;
static float      g_volume     = 1.0f;

/* =========================================================================
 * Sample slots
 * ===================================================================== */
#define MAX_SAMPLES 64

typedef struct {
    char   name[64];
    float *pcm;        /* malloc'd, F32 stereo interleaved at AUDIO_SR */
    int    frames;
} SampleSlot;

static SampleSlot g_samples[MAX_SAMPLES];
static int        g_sample_count = 0;

/* =========================================================================
 * Helpers: write F32 interleaved PCM to ALSA
 * ===================================================================== */
static void alsa_write_f32(const float *buf, int frames) {
    if (!g_pcm || !buf || frames <= 0) return;
    /* Convert F32 to S16_LE */
    int total = frames * AUDIO_CH;
    int16_t *s16 = (int16_t *)malloc((size_t)total * sizeof(int16_t));
    if (!s16) return;
    for (int i = 0; i < total; i++) {
        float s = buf[i] * g_volume;
        if      (s >  1.0f) s =  1.0f;
        else if (s < -1.0f) s = -1.0f;
        s16[i] = (int16_t)(s * 32767.0f);
    }
    snd_pcm_sframes_t written = 0;
    int remaining = frames;
    const int16_t *ptr = s16;
    while (remaining > 0) {
        written = snd_pcm_writei(g_pcm, ptr, (snd_pcm_uframes_t)remaining);
        if (written == -EAGAIN) continue;
        if (written < 0) {
            snd_pcm_recover(g_pcm, (int)written, 0);
            break;
        }
        ptr       += written * AUDIO_CH;
        remaining -= (int)written;
    }
    free(s16);
}

/* =========================================================================
 * Window management -- sdl-window
 * ===================================================================== */
static Val prim_sdl_window(Val args, Val env) {
    (void)env;
    const char *title = str_ptr(STR_IDX(CAR(args)));
    int w = INT_VAL(CADR(args)), h = INT_VAL(CADDR(args));
    if (!g_dpy) { g_dpy = XOpenDisplay(NULL); }
    if (!g_dpy) return NIL_VAL;
    int scr = DefaultScreen(g_dpy);

    if (g_win) {
        /* Window already exists -- resize instead of creating a new one.
         * vi-gfx-start calls sdl-window twice: once at a default size to
         * measure font metrics, then again at the real calculated size. */
        XResizeWindow(g_dpy, g_win, (unsigned)w, (unsigned)h);
        XStoreName(g_dpy, g_win, title);
        if (g_back) XFreePixmap(g_dpy, g_back);
        g_back = XCreatePixmap(g_dpy, g_win, (unsigned)w, (unsigned)h,
                               (unsigned)DefaultDepth(g_dpy, scr));
        if (g_gc) XFreeGC(g_dpy, g_gc);
        g_gc = XCreateGC(g_dpy, g_back, 0, NULL);
        g_w = w; g_h = h;
        free(g_pixels); g_pixels = NULL;
        XFlush(g_dpy);
        return T_VAL;
    }

    g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, scr),
                                0, 0, (unsigned)w, (unsigned)h, 0,
                                BlackPixel(g_dpy, scr),
                                BlackPixel(g_dpy, scr));
    XStoreName(g_dpy, g_win, title);
    XSelectInput(g_dpy, g_win,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);
    g_back = XCreatePixmap(g_dpy, g_win, (unsigned)w, (unsigned)h,
                           (unsigned)DefaultDepth(g_dpy, scr));
    g_gc   = XCreateGC(g_dpy, g_back, 0, NULL);
    g_w    = w; g_h = h;
    XMapWindow(g_dpy, g_win);
    /* Register WM_DELETE_WINDOW so the close button sends a ClientMessage
     * instead of the server destroying the window under us. */
    g_wm_delete_window = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &g_wm_delete_window, 1);
    XFlush(g_dpy);
    return T_VAL;
}

/* =========================================================================
 * 2D drawing primitives
 * ===================================================================== */

/* (sdl-color r g b) */
static Val prim_sdl_color(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    int r = INT_VAL(CAR(args));
    int g = INT_VAL(CADR(args));
    int b = INT_VAL(CADDR(args));
    g_color = (uint32_t)(((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b);
    XSetForeground(g_dpy, g_gc, (unsigned long)g_color);
    return NIL_VAL;
}

/* (sdl-clear) -- fill backing pixmap with current color */
static Val prim_sdl_clear(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    XFillRectangle(g_dpy, g_back, g_gc, 0, 0,
                   (unsigned)g_w, (unsigned)g_h);
    return NIL_VAL;
}

/* (sdl-present) -- blit backing pixmap to window */
static Val prim_sdl_present(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    XCopyArea(g_dpy, g_back, g_win, g_gc, 0, 0,
              (unsigned)g_w, (unsigned)g_h, 0, 0);
    XFlush(g_dpy);
    return NIL_VAL;
}

/* (sdl-point x y) */
static Val prim_sdl_point(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    XDrawPoint(g_dpy, g_back, g_gc,
               INT_VAL(CAR(args)), INT_VAL(CADR(args)));
    return NIL_VAL;
}

/* (sdl-line x1 y1 x2 y2) */
static Val prim_sdl_line(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    XDrawLine(g_dpy, g_back, g_gc,
              INT_VAL(CAR(args)),   INT_VAL(CADR(args)),
              INT_VAL(CADDR(args)), INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}

/* (sdl-rect x y w h) */
static Val prim_sdl_rect(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    XDrawRectangle(g_dpy, g_back, g_gc,
                   INT_VAL(CAR(args)),   INT_VAL(CADR(args)),
                   (unsigned)INT_VAL(CADDR(args)),
                   (unsigned)INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}

/* (sdl-fill x y w h) */
static Val prim_sdl_fill(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    XFillRectangle(g_dpy, g_back, g_gc,
                   INT_VAL(CAR(args)),   INT_VAL(CADR(args)),
                   (unsigned)INT_VAL(CADDR(args)),
                   (unsigned)INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}

/* Bresenham circle helper */
static void draw_circle_x11(int cx, int cy, int radius, int filled) {
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        if (filled) {
            XDrawLine(g_dpy, g_back, g_gc, cx - x, cy + y, cx + x, cy + y);
            XDrawLine(g_dpy, g_back, g_gc, cx - x, cy - y, cx + x, cy - y);
            XDrawLine(g_dpy, g_back, g_gc, cx - y, cy + x, cx + y, cy + x);
            XDrawLine(g_dpy, g_back, g_gc, cx - y, cy - x, cx + y, cy - x);
        } else {
            XDrawPoint(g_dpy, g_back, g_gc, cx + x, cy + y);
            XDrawPoint(g_dpy, g_back, g_gc, cx - x, cy + y);
            XDrawPoint(g_dpy, g_back, g_gc, cx + x, cy - y);
            XDrawPoint(g_dpy, g_back, g_gc, cx - x, cy - y);
            XDrawPoint(g_dpy, g_back, g_gc, cx + y, cy + x);
            XDrawPoint(g_dpy, g_back, g_gc, cx - y, cy + x);
            XDrawPoint(g_dpy, g_back, g_gc, cx + y, cy - x);
            XDrawPoint(g_dpy, g_back, g_gc, cx - y, cy - x);
        }
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0)  { x--; err -= 2 * x + 1; }
    }
}

/* (sdl-circle cx cy r) */
static Val prim_sdl_circle(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    draw_circle_x11(INT_VAL(CAR(args)), INT_VAL(CADR(args)),
                    INT_VAL(CADDR(args)), 0);
    return NIL_VAL;
}

/* (sdl-fill-circle cx cy r) */
static Val prim_sdl_fill_circle(Val args, Val env) {
    (void)env;
    if (!g_dpy) return NIL_VAL;
    draw_circle_x11(INT_VAL(CAR(args)), INT_VAL(CADR(args)),
                    INT_VAL(CADDR(args)), 1);
    return NIL_VAL;
}

/* =========================================================================
 * Timing
 * ===================================================================== */

/* (sdl-ticks) → ms as INT */
static Val prim_sdl_ticks(Val args, Val env) {
    (void)args; (void)env;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int32_t ms = (int32_t)((ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL)
                           & INT64_C(0x7FFFFFFF));
    return MAKE_INT(ms);
}

/* (sdl-delay ms) */
static Val prim_sdl_delay(Val args, Val env) {
    (void)env;
    int ms = INT_VAL(CAR(args));
    if (ms <= 0) return NIL_VAL;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
    return NIL_VAL;
}

/* =========================================================================
 * Key mapping
 * ===================================================================== */
/* Return the SDL-compatible key name for a KeySym, or NULL for printable chars.
 * Names match SDL_GetKeyName() output so that lib/vi/gfx.l can consume them. */
static const char *keysym_to_name(KeySym ks) {
    switch (ks) {
        case XK_Return:    case XK_KP_Enter: return "Return";
        case XK_Escape:                      return "Escape";
        case XK_BackSpace:                   return "Backspace";
        case XK_Tab:                         return "Tab";
        case XK_Up:                          return "Up";
        case XK_Down:                        return "Down";
        case XK_Left:                        return "Left";
        case XK_Right:                       return "Right";
        case XK_Delete:                      return "Delete";
        case XK_Home:                        return "Home";
        case XK_End:                         return "End";
        case XK_Page_Up:                     return "Page_Up";
        case XK_Page_Down:                   return "Page_Down";
        case XK_Insert:                      return "Insert";
        case XK_F1:                          return "F1";
        case XK_F2:                          return "F2";
        case XK_F3:                          return "F3";
        case XK_F4:                          return "F4";
        case XK_F5:                          return "F5";
        case XK_F6:                          return "F6";
        case XK_F7:                          return "F7";
        case XK_F8:                          return "F8";
        case XK_F9:                          return "F9";
        case XK_F10:                         return "F10";
        case XK_F11:                         return "F11";
        case XK_F12:                         return "F12";
        case XK_Shift_L:   case XK_Shift_R:  return "Shift";
        case XK_Control_L: case XK_Control_R:return "Ctrl";
        case XK_Alt_L:     case XK_Alt_R:    return "Alt";
        case XK_Super_L:   case XK_Super_R:  return "Super";
        case XK_space:                       return "Space";
        default:
            if (ks >= 0x20 && ks <= 0x7E) return NULL; /* printable -- use char */
            return "unknown";
    }
}

/* (sdl-key-name scancode) → string
 * In our X11 backend, we receive a KeySym directly as the integer arg,
 * but for compatibility we also accept a raw ASCII code. */
static Val prim_sdl_key_name(Val args, Val env) {
    (void)env;
    int code = INT_VAL(CAR(args));
    char buf[8];
    const char *name = keysym_to_name((KeySym)code);
    if (!name) {
        buf[0] = (char)code; buf[1] = '\0';
        name = buf;
    }
    uint32_t si = str_intern(name, strlen(name));
    return MAKE_STR(si);
}

/* =========================================================================
 * Event processing helpers
 * ===================================================================== */

/* Build a flat plist for a keydown event from an XEvent.
 * Returns the plist Val. */
static Val build_keydown_event(XEvent *ev) {
    char keybuf[16] = {0};
    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    int ctrl_held = (ev->xkey.state & ControlMask) != 0;

    /* Get a printable representation */
    const char *fixed = keysym_to_name(ks);
    const char *kname;
    if (fixed) {
        /* For bare modifier keys, just emit their name */
        kname = fixed;
    } else if (ks >= 0x20 && ks <= 0x7E) {
        /* Single printable ASCII key.
         * When Ctrl is held, synthesize "ctrl-X" names (lowercase letter)
         * so that lib/vi/gfx.l sdl-key->vi-key can map them to symbols. */
        if (ctrl_held && ((ks >= 'a' && ks <= 'z') || (ks >= 'A' && ks <= 'Z'))) {
            char lc = (char)(ks | 0x20); /* force lowercase */
            keybuf[0] = 'c'; keybuf[1] = 't'; keybuf[2] = 'r'; keybuf[3] = 'l';
            keybuf[4] = '-'; keybuf[5] = lc; keybuf[6] = '\0';
            kname = keybuf;
        } else {
            keybuf[0] = (char)ks; keybuf[1] = '\0';
            kname = keybuf;
        }
    } else {
        /* Try XLookupString for composed chars */
        int n = XLookupString(&ev->xkey, keybuf, (int)sizeof(keybuf) - 1,
                              &ks, NULL);
        if (n > 0) {
            keybuf[n] = '\0';
            kname = keybuf;
        } else {
            kname = "unknown";
        }
    }

    uint32_t s_type    = str_intern("type",    4);
    uint32_t s_key     = str_intern("key",     3);
    uint32_t s_mod     = str_intern("mod",     3);
    uint32_t s_keydown = str_intern("keydown", 7);
    uint32_t s_kname   = str_intern(kname, strlen(kname));

    /* modifier state: map X11 modifier mask to SDL-compatible bits
     * SDL_KMOD_SHIFT=0x0003, SDL_KMOD_CTRL=0x00C0, SDL_KMOD_ALT=0x0300
     * We use a simplified mapping; Lisp code only checks broad categories. */
    int32_t mod_val = 0;
    if (ev->xkey.state & ShiftMask)   mod_val |= 0x0001; /* SHIFT */
    if (ev->xkey.state & ControlMask) mod_val |= 0x0040; /* CTRL  */
    if (ev->xkey.state & Mod1Mask)    mod_val |= 0x0100; /* ALT   */

    Val list = pl_cons(MAKE_STR(s_mod), MAKE_INT(mod_val));   /* ("mod" . bits) */
    PUSH_ROOT(list);
    list = pl_cons(list, NIL_VAL);
    g_gc_roots[g_gc_root_top - 1] = list;

    Val kp = pl_cons(MAKE_STR(s_key), MAKE_STR(s_kname));
    PUSH_ROOT(kp);
    list = pl_cons(kp, list);
    POP_ROOT();
    g_gc_roots[g_gc_root_top - 1] = list;

    Val tp = pl_cons(MAKE_STR(s_type), MAKE_STR(s_keydown));
    PUSH_ROOT(tp);
    list = pl_cons(tp, list);
    POP_ROOT();
    g_gc_roots[g_gc_root_top - 1] = list;

    POP_ROOT();
    return list;
}

static Val build_quit_event(void) {
    uint32_t s_type = str_intern("type", 4);
    uint32_t s_quit = str_intern("quit", 4);
    Val pair = pl_cons(MAKE_STR(s_type), MAKE_STR(s_quit));
    PUSH_ROOT(pair);
    Val list = pl_cons(pair, NIL_VAL);
    POP_ROOT();
    return list;
}

static Val build_mousemove_event(int mx, int my) {
    uint32_t s_type      = str_intern("type",      4);
    uint32_t s_x         = str_intern("x",         1);
    uint32_t s_y         = str_intern("y",         1);
    uint32_t s_mousemove = str_intern("mousemove", 9);

    Val list = pl_cons(MAKE_STR(s_y), MAKE_INT((int32_t)my));
    PUSH_ROOT(list);
    list = pl_cons(list, NIL_VAL);
    g_gc_roots[g_gc_root_top - 1] = list;

    Val xp = pl_cons(MAKE_STR(s_x), MAKE_INT((int32_t)mx));
    PUSH_ROOT(xp);
    list = pl_cons(xp, list);
    POP_ROOT();
    g_gc_roots[g_gc_root_top - 1] = list;

    Val tp = pl_cons(MAKE_STR(s_type), MAKE_STR(s_mousemove));
    PUSH_ROOT(tp);
    list = pl_cons(tp, list);
    POP_ROOT();
    g_gc_roots[g_gc_root_top - 1] = list;

    POP_ROOT();
    return list;
}

static Val dispatch_xevent(XEvent *ev) {
    switch (ev->type) {
        case KeyPress:
            return build_keydown_event(ev);
        case ClientMessage:
            if (g_wm_delete_window &&
                (Atom)ev->xclient.data.l[0] == g_wm_delete_window)
                return build_quit_event();
            return NIL_VAL;
        case DestroyNotify:
            /* Safety net: some WMs send DestroyNotify */
            return build_quit_event();
        case MotionNotify:
            return build_mousemove_event(ev->xmotion.x, ev->xmotion.y);
        case Expose:
            if (ev->xexpose.count == 0 && g_dpy) {
                XCopyArea(g_dpy, g_back, g_win, g_gc, 0, 0,
                          (unsigned)g_w, (unsigned)g_h, 0, 0);
                XFlush(g_dpy);
            }
            return NIL_VAL;
        default:
            return NIL_VAL;
    }
}

/* =========================================================================
 * Event primitives
 * ===================================================================== */

/* (sdl-poll) → flat plist or NIL */
static Val prim_sdl_poll(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    while (XPending(g_dpy)) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        Val result = dispatch_xevent(&ev);
        if (!IS_NIL(result)) return result;
    }
    return NIL_VAL;
}

/* (sdl-wait-event) → flat plist */
static Val prim_sdl_wait_event(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    for (;;) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        Val result = dispatch_xevent(&ev);
        if (!IS_NIL(result)) return result;
        /* skip unrecognised events */
    }
}

/* (sdl-quit) */
static Val prim_sdl_quit(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    if (g_gc)   { XFreeGC(g_dpy, g_gc);       g_gc   = 0; }
    if (g_back) { XFreePixmap(g_dpy, g_back);  g_back = 0; }
    if (g_win)  { XDestroyWindow(g_dpy, g_win); g_win  = 0; }
    XCloseDisplay(g_dpy);
    g_dpy = NULL;
    free(g_pixels); g_pixels = NULL;
    return NIL_VAL;
}

/* =========================================================================
 * Text rendering -- sdl-font-load, sdl-text, sdl-text-size
 * ===================================================================== */

/* (sdl-font-load path ptsize) → font-index or -1 */
static Val prim_sdl_font_load(Val args, Val env) {
    (void)env;
    const char *path   = str_ptr(STR_IDX(CAR(args)));
    int         ptsize = INT_VAL(CADR(args));
    if (g_font_count >= MAX_FONTS) return MAKE_INT(-1);
    TtfFont *font = ttf_load(path);
    if (!font) {
        fprintf(stderr, "[x11] sdl-font-load FAILED for '%s'\n", path);
        return MAKE_INT(-1);
    }
    fprintf(stderr, "[x11] sdl-font-load OK: '%s' @%dpx\n", path, ptsize);
    g_fonts[g_font_count]      = font;
    g_font_sizes[g_font_count] = ptsize;
    return MAKE_INT(g_font_count++);
}

/* (sdl-text x y "string") */
static Val prim_sdl_text(Val args, Val env) {
    (void)env;
    if (!g_dpy || g_font_count == 0) return NIL_VAL;
    int         px   = INT_VAL(CAR(args));
    int         py   = INT_VAL(CADR(args));
    const char *text = str_ptr(STR_IDX(CADDR(args)));
    if (!text) return NIL_VAL;

    TtfFont *font    = g_fonts[0];
    int      ptsize  = g_font_sizes[0];

    /* Extract current foreground color components */
    int fr = (int)((g_color >> 16) & 0xFF);
    int fg = (int)((g_color >>  8) & 0xFF);
    int fb = (int)( g_color        & 0xFF);

    int asc = 0, desc = 0, gap = 0;
    ttf_line_metrics(font, ptsize, &asc, &desc, &gap);

    int pen_x = px;
    int pen_y = py + asc;  /* baseline */

    for (const char *p = text; *p; p++) {
        uint32_t cp = (uint8_t)*p;
        int bw = 0, bh = 0, xoff = 0, yoff = 0;
        uint8_t *bmp = ttf_render_glyph(font, cp, ptsize, &bw, &bh, &xoff, &yoff);
        if (bmp) {
            for (int row = 0; row < bh; row++) {
                for (int col = 0; col < bw; col++) {
                    int a = bmp[(bh - 1 - row) * bw + col]; /* bitmap is bottom-up; flip to screen */
                    if (a == 0) continue;
                    int dx = pen_x + xoff + col;
                    int dy = pen_y + yoff + row;
                    /* Alpha-blend formula from spec */
                    uint32_t blended = (uint32_t)((((fr * a) >> 8) << 16) |
                                                  (((fg * a) >> 8) <<  8) |
                                                   ((fb * a) >> 8));
                    XSetForeground(g_dpy, g_gc, (unsigned long)blended);
                    XDrawPoint(g_dpy, g_back, g_gc, dx, dy);
                }
            }
            free(bmp);
        }
        pen_x += ttf_glyph_advance(font, cp, ptsize);
    }
    /* Restore draw color */
    XSetForeground(g_dpy, g_gc, (unsigned long)g_color);
    return NIL_VAL;
}

/* (sdl-text-size "string") → (w . h) */
static Val prim_sdl_text_size(Val args, Val env) {
    (void)env;
    if (g_font_count == 0) return pl_cons(MAKE_INT(0), MAKE_INT(0));
    const char *text = str_ptr(STR_IDX(CAR(args)));
    TtfFont *font   = g_fonts[0];
    int      ptsize = g_font_sizes[0];

    int w = 0;
    if (text) {
        for (const char *p = text; *p; p++)
            w += ttf_glyph_advance(font, (uint32_t)(uint8_t)*p, ptsize);
    }
    int asc = 0, desc = 0, gap = 0;
    ttf_line_metrics(font, ptsize, &asc, &desc, &gap);
    int h = asc - desc + gap;
    return pl_cons(MAKE_INT(w), MAKE_INT(h));
}

/* =========================================================================
 * ALSA audio
 * ===================================================================== */

/* (audio-init) → T or NIL */
static Val prim_audio_init(Val args, Val env) {
    (void)args; (void)env;
    if (g_pcm) return T_VAL; /* already open */

    int err = snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "[x11] snd_pcm_open: %s\n", snd_strerror(err));
        g_pcm = NULL;
        return NIL_VAL;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(g_pcm, params);
    snd_pcm_hw_params_set_access(g_pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(g_pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(g_pcm, params, (unsigned)AUDIO_CH);
    unsigned int rate = AUDIO_SR;
    snd_pcm_hw_params_set_rate_near(g_pcm, params, &rate, NULL);
    /* Set buffer large enough for the mixer thread (~185ms at 44.1kHz) */
    snd_pcm_uframes_t bufsize = 8192;
    snd_pcm_hw_params_set_buffer_size_near(g_pcm, params, &bufsize);
    snd_pcm_uframes_t period = 1024;
    snd_pcm_hw_params_set_period_size_near(g_pcm, params, &period, NULL);

    err = snd_pcm_hw_params(g_pcm, params);
    if (err < 0) {
        fprintf(stderr, "[x11] snd_pcm_hw_params: %s\n", snd_strerror(err));
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
        return NIL_VAL;
    }
    snd_pcm_prepare(g_pcm);
    return T_VAL;
}

/* Forward declarations for voice mixer (defined after sample loading) */
#include <pthread.h>
#define MAX_VOICES 32
typedef struct {
    const float *pcm; int frames; int pos; float speed;
    int64_t start_sample; volatile int active;
} Voice;
static Voice g_voices[MAX_VOICES];
static pthread_mutex_t g_voice_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_mixer_running = 0;
static volatile int64_t g_mixer_sample_pos = 0;
static void mixer_start(void);
static void mixer_stop(void);
static void voice_add(const float *pcm, int frames, float speed);

/* (audio-tone wave freq-hz dur-ms [vol])
 * wave: 0=sine 1=square 2=sawtooth 3=triangle
 * vol: 0-100 */
static Val prim_audio_tone(Val args, Val env) {
    (void)env;
    if (!g_pcm) return NIL_VAL;
    int   wave = INT_VAL(CAR(args));
    float freq = (float)INT_VAL(CADR(args));
    int   ms   = INT_VAL(CADDR(args));
    float vol  = IS_CONS(CDDDR(args))
                 ? (float)INT_VAL(CAR(CDDDR(args))) / 100.0f : 0.5f;

    int n = (AUDIO_SR * ms) / 1000;
    if (n <= 0) return NIL_VAL;
    float *buf = (float *)malloc((size_t)(n * AUDIO_CH) * sizeof(float));
    if (!buf) return NIL_VAL;

    float phase = 0.0f, phase_inc = freq / (float)AUDIO_SR;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / (float)(n > 1 ? n - 1 : 1);
        float env2 = expf(-4.0f * t) * vol;
        float pp  = phase - floorf(phase);
        float s   = 0.0f;
        switch (wave) {
            case 0:  s = sinf(pp * 6.2831853f); break;
            case 1:  s = pp < 0.5f ? 1.0f : -1.0f; break;
            case 2:  s = 2.0f * pp - 1.0f; break;
            case 3:  s = pp < 0.5f ? 4.0f*pp - 1.0f : 3.0f - 4.0f*pp; break;
            default: s = sinf(pp * 6.2831853f); break;
        }
        buf[i*2] = buf[i*2+1] = s * env2;
        phase += phase_inc;
    }
    /* Use voice system -- buf must persist, so store in ring */
    static float *temp_ring[16]; static int temp_idx = 0;
    free(temp_ring[temp_idx]);
    temp_ring[temp_idx] = buf;
    temp_idx = (temp_idx + 1) % 16;
    voice_add(buf, n, 1.0f);
    return NIL_VAL;
}

/* (audio-drums mask dur-ms)
 * mask bits: bit0=kick, bit1=snare, bit2=closed-hihat, bit3=open-hihat */
static Val prim_audio_drums(Val args, Val env) {
    (void)env;
    if (!g_pcm) return NIL_VAL;
    int mask = INT_VAL(CAR(args));
    int ms   = INT_VAL(CADR(args));

    int n = (AUDIO_SR * ms) / 1000;
    if (n <= 0) return NIL_VAL;
    float *buf = (float *)calloc((size_t)(n * AUDIO_CH), sizeof(float));
    if (!buf) return NIL_VAL;

    /* --- Kick: frequency-swept sine 150→45 Hz --- */
    if (mask & 1) {
        float phase = 0.0f;
        int kn = (AUDIO_SR * 220) / 1000; if (kn > n) kn = n;
        for (int i = 0; i < kn; i++) {
            float t    = (float)i / (float)AUDIO_SR;
            float f    = 45.0f + 110.0f * expf(-t * 28.0f);
            float amp  = 0.85f * expf(-t * 7.0f);
            float s    = sinf(phase * 6.2831853f) * amp;
            buf[i*2] += s; buf[i*2+1] += s;
            phase += f / (float)AUDIO_SR;
        }
    }

    /* --- Snare: noise burst + 200 Hz sine body --- */
    if (mask & 2) {
        unsigned int rng = 12345;
        int sn = (AUDIO_SR * 160) / 1000; if (sn > n) sn = n;
        for (int i = 0; i < sn; i++) {
            float t = (float)i / (float)AUDIO_SR;
            rng = rng * 1664525u + 1013904223u;
            float noise = ((float)(rng >> 1) / (float)0x7FFFFFFFu - 1.0f)
                          * 0.55f * expf(-t * 20.0f);
            float body  = sinf((float)i * 6.2831853f * 200.0f / (float)AUDIO_SR)
                          * 0.30f * expf(-t * 32.0f);
            float s = noise + body;
            buf[i*2] += s; buf[i*2+1] += s;
        }
    }

    /* --- Closed hi-hat: short high-freq noise --- */
    if (mask & 4) {
        unsigned int rng = 67890;
        int hn = (AUDIO_SR * 45) / 1000; if (hn > n) hn = n;
        for (int i = 0; i < hn; i++) {
            float t = (float)i / (float)AUDIO_SR;
            rng = rng * 1664525u + 1013904223u;
            float noise = ((float)(rng >> 1) / (float)0x7FFFFFFFu - 1.0f)
                          * 0.35f * expf(-t * 90.0f);
            buf[i*2] += noise; buf[i*2+1] += noise;
        }
    }

    /* --- Open hi-hat: longer high-freq noise --- */
    if (mask & 8) {
        unsigned int rng = 99999;
        int hn = (AUDIO_SR * 220) / 1000; if (hn > n) hn = n;
        for (int i = 0; i < hn; i++) {
            float t = (float)i / (float)AUDIO_SR;
            rng = rng * 1664525u + 1013904223u;
            float noise = ((float)(rng >> 1) / (float)0x7FFFFFFFu - 1.0f)
                          * 0.28f * expf(-t * 10.0f);
            buf[i*2] += noise; buf[i*2+1] += noise;
        }
    }

    /* Soft clip */
    for (int i = 0; i < n * AUDIO_CH; i++) {
        float s = buf[i];
        if      (s >  0.95f) s =  0.95f;
        else if (s < -0.95f) s = -0.95f;
        buf[i] = s;
    }

    static float *drum_ring[16]; static int drum_idx = 0;
    free(drum_ring[drum_idx]);
    drum_ring[drum_idx] = buf;
    drum_idx = (drum_idx + 1) % 16;
    voice_add(buf, n, 1.0f);
    return NIL_VAL;
}

/* (audio-pending) → number of active voices */
static Val prim_audio_pending(Val args, Val env) {
    (void)args; (void)env;
    int count = 0;
    if (g_mixer_running) {
        pthread_mutex_lock(&g_voice_lock);
        for (int i = 0; i < MAX_VOICES; i++)
            if (g_voices[i].active) count++;
        pthread_mutex_unlock(&g_voice_lock);
    }
    return MAKE_INT(count);
}

/* (audio-clear) -- stop mixer and drop buffered PCM */
static Val prim_audio_clear(Val args, Val env) {
    (void)args; (void)env;
    mixer_stop();
    return NIL_VAL;
}

/* (audio-quit) */
static Val prim_audio_quit(Val args, Val env) {
    (void)args; (void)env;
    mixer_stop();
    if (g_pcm) {
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    return NIL_VAL;
}

/* (audio-volume pct) 0-100 */
static Val prim_audio_volume(Val args, Val env) {
    (void)env;
    int pct = INT_VAL(CAR(args));
    g_volume = (float)pct / 100.0f;
    return NIL_VAL;
}

/* (audio-stop-all) */
static Val prim_audio_stop_all(Val args, Val env) {
    (void)args; (void)env;
    mixer_stop();
    return NIL_VAL;
}

/* =========================================================================
 * Sample loading -- minimal WAV parser
 * ===================================================================== */

static int sample_find(const char *name) {
    for (int i = 0; i < g_sample_count; i++)
        if (strcmp(g_samples[i].name, name) == 0) return i;
    return -1;
}

/* Read a little-endian 16-bit or 32-bit word from file */
static uint16_t read_u16le(FILE *f) {
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return (uint16_t)(b[0] | (b[1] << 8));
}
static uint32_t read_u32le(FILE *f) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (uint32_t)(b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24));
}

/* Load WAV file, convert to F32 stereo at AUDIO_SR.
 * Returns slot index on success, -1 on failure. */
static int sample_load_impl(const char *name, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* RIFF header */
    char tag[5] = {0};
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "RIFF", 4) != 0) { fclose(f); return -1; }
    read_u32le(f); /* chunk size */
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "WAVE", 4) != 0) { fclose(f); return -1; }

    /* Find fmt chunk */
    uint16_t audio_format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    int found_fmt = 0;
    for (;;) {
        if (fread(tag, 1, 4, f) != 4) break;
        uint32_t chunk_size = read_u32le(f);
        if (memcmp(tag, "fmt ", 4) == 0) {
            audio_format = read_u16le(f); /* 1=PCM, 3=IEEE float */
            channels     = read_u16le(f);
            sample_rate  = read_u32le(f);
            read_u32le(f); /* byte rate */
            read_u16le(f); /* block align */
            bits         = read_u16le(f);
            if (chunk_size > 16)
                fseek(f, (long)(chunk_size - 16), SEEK_CUR);
            found_fmt = 1;
        } else if (memcmp(tag, "data", 4) == 0) {
            if (!found_fmt) { fclose(f); return -1; }

            /* Read raw bytes */
            uint8_t *raw = (uint8_t *)malloc(chunk_size);
            if (!raw) { fclose(f); return -1; }
            size_t got = fread(raw, 1, chunk_size, f);
            if (got == 0) { free(raw); fclose(f); return -1; }
            uint32_t actual_bytes = (uint32_t)got;

            /* Convert to F32 mono/stereo */
            uint32_t bytes_per_sample = (uint32_t)bits / 8u;
            if (bytes_per_sample == 0 || channels == 0) { free(raw); fclose(f); return -1; }
            uint32_t src_frames = actual_bytes / (bytes_per_sample * channels);

            /* Resample ratio (integer for simplicity; skip resampling if rates match) */
            /* We'll just do a nearest-neighbour resample for src_rate → AUDIO_SR */
            uint32_t dst_frames = (uint32_t)((uint64_t)src_frames * AUDIO_SR / sample_rate);
            if (dst_frames == 0) dst_frames = 1;

            float *pcm = (float *)malloc((size_t)dst_frames * AUDIO_CH * sizeof(float));
            if (!pcm) { free(raw); fclose(f); return -1; }

            for (uint32_t di = 0; di < dst_frames; di++) {
                uint32_t si = (uint32_t)((uint64_t)di * src_frames / dst_frames);
                if (si >= src_frames) si = src_frames - 1;

                float ch_l = 0.0f, ch_r = 0.0f;
                for (uint32_t c = 0; c < (uint32_t)channels && c < 2; c++) {
                    uint32_t byte_off = (si * channels + c) * bytes_per_sample;
                    float v = 0.0f;
                    if (audio_format == 3 && bits == 32) {
                        /* IEEE float32 -- need 4 bytes */
                        if (byte_off + 3 < actual_bytes) {
                            float tmp;
                            memcpy(&tmp, raw + byte_off, 4);
                            v = tmp;
                        }
                    } else if (bits == 16) {
                        /* 16-bit PCM -- need 2 bytes */
                        if (byte_off + 1 < actual_bytes) {
                            int16_t s;
                            memcpy(&s, raw + byte_off, 2);
                            v = (float)s / 32768.0f;
                        }
                    } else if (bits == 8) {
                        /* 8-bit PCM -- need 1 byte (bounds already implied by src_frames calc) */
                        if (byte_off < actual_bytes) {
                            v = ((float)raw[byte_off] - 128.0f) / 128.0f;
                        }
                    } else if (bits == 24) {
                        /* 24-bit PCM -- need 3 bytes */
                        if (byte_off + 2 < actual_bytes) {
                            int32_t s = (int32_t)((uint32_t)raw[byte_off] |
                                                  ((uint32_t)raw[byte_off+1] << 8) |
                                                  ((uint32_t)raw[byte_off+2] << 16));
                            if (s & 0x800000) s |= (int32_t)0xFF000000;
                            v = (float)s / 8388608.0f;
                        }
                    }
                    if (c == 0) ch_l = v;
                    else        ch_r = v;
                }
                if (channels == 1) ch_r = ch_l;
                pcm[di*2]   = ch_l;
                pcm[di*2+1] = ch_r;
            }

            free(raw);
            fclose(f);

            int slot = sample_find(name);
            if (slot < 0) {
                if (g_sample_count >= MAX_SAMPLES) { free(pcm); return -1; }
                slot = g_sample_count++;
            } else {
                free(g_samples[slot].pcm);
            }
            snprintf(g_samples[slot].name, sizeof(g_samples[slot].name), "%s", name);
            g_samples[slot].pcm    = pcm;
            g_samples[slot].frames = (int)dst_frames;
            return slot;
        } else {
            /* Skip unknown chunk */
            fseek(f, (long)chunk_size, SEEK_CUR);
        }
    }
    fclose(f);
    return -1;
}

/* (sample-load 'name "path") → T or NIL */
static Val prim_sample_load(Val args, Val env) {
    (void)env;
    if (!IS_SYM(CAR(args)) || !IS_STR(CADR(args))) return NIL_VAL;
    const char *name = sym_name(SYM_IDX(CAR(args)));
    const char *path = str_ptr(STR_IDX(CADR(args)));
    return sample_load_impl(name, path) >= 0 ? T_VAL : NIL_VAL;
}

/* (samples-load-dir "path") → count of loaded samples */
static Val prim_samples_load_dir(Val args, Val env) {
    (void)env;
    if (!IS_STR(CAR(args))) return MAKE_INT(0);
    const char *dir = str_ptr(STR_IDX(CAR(args)));
    int count = 0;

    DIR *d = opendir(dir);
    if (!d) return MAKE_INT(0);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *fname = ent->d_name;
        const char *dot   = strrchr(fname, '.');
        if (!dot) continue;
        if (strcmp(dot, ".wav") != 0 && strcmp(dot, ".WAV") != 0) continue;
        char stem[64];
        size_t slen = (size_t)(dot - fname);
        if (slen >= sizeof(stem)) slen = sizeof(stem) - 1;
        memcpy(stem, fname, slen); stem[slen] = '\0';
        char fpath[520];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir, fname);
        if (sample_load_impl(stem, fpath) >= 0) count++;
    }
    closedir(d);
    return MAKE_INT(count);
}

/* ---- Background voice mixer (matches Win32 architecture) ----
 * A pthread continuously mixes active voices into 20ms chunks
 * and writes them to ALSA via snd_pcm_writei. */

#define MIX_CHUNK_MS     20
#define MIX_CHUNK_FRAMES (AUDIO_SR * MIX_CHUNK_MS / 1000)

static pthread_t g_mixer_thread;

static void *mixer_thread_fn(void *arg) {
    (void)arg;
    while (g_mixer_running) {
        float mix[MIX_CHUNK_FRAMES * AUDIO_CH];
        memset(mix, 0, sizeof(mix));
        int64_t chunk_start = g_mixer_sample_pos;
        int64_t chunk_end   = chunk_start + MIX_CHUNK_FRAMES;

        pthread_mutex_lock(&g_voice_lock);
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vc = &g_voices[v];
            if (!vc->active) continue;
            if (vc->pos < 0) {
                if (vc->start_sample >= chunk_end) continue;
                int offset = (int)(vc->start_sample - chunk_start);
                if (offset < 0) offset = 0;
                vc->pos = 0;
                for (int i = offset; i < MIX_CHUNK_FRAMES; i++) {
                    int si = (vc->speed == 1.0f) ? vc->pos
                             : (int)((float)vc->pos * vc->speed);
                    if (si >= vc->frames) { vc->active = 0; break; }
                    mix[i*2]   += vc->pcm[si*2];
                    mix[i*2+1] += vc->pcm[si*2+1];
                    vc->pos++;
                }
                continue;
            }
            for (int i = 0; i < MIX_CHUNK_FRAMES; i++) {
                int si = (vc->speed == 1.0f) ? vc->pos + i
                         : (int)((float)(vc->pos + i) * vc->speed);
                if (si >= vc->frames) { vc->active = 0; break; }
                mix[i*2]   += vc->pcm[si*2];
                mix[i*2+1] += vc->pcm[si*2+1];
            }
            if (vc->active) {
                vc->pos += (vc->speed == 1.0f) ? MIX_CHUNK_FRAMES
                           : (int)((float)MIX_CHUNK_FRAMES * vc->speed);
                if (vc->pos >= vc->frames) vc->active = 0;
            }
        }
        pthread_mutex_unlock(&g_voice_lock);
        g_mixer_sample_pos = chunk_end;

        /* Convert to S16 and write to ALSA */
        int16_t s16[MIX_CHUNK_FRAMES * AUDIO_CH];
        for (int i = 0; i < MIX_CHUNK_FRAMES * AUDIO_CH; i++) {
            float s = mix[i] * g_volume;
            if      (s >  1.0f) s =  1.0f;
            else if (s < -1.0f) s = -1.0f;
            s16[i] = (int16_t)(s * 32767.0f);
        }
        if (g_pcm) {
            const int16_t *ptr = s16;
            int remaining = MIX_CHUNK_FRAMES;
            int retries = 0;
            while (remaining > 0 && g_mixer_running && retries < 3) {
                snd_pcm_sframes_t wr = snd_pcm_writei(g_pcm, ptr, (snd_pcm_uframes_t)remaining);
                if (wr == -EAGAIN) { usleep(1000); continue; }
                if (wr < 0) {
                    snd_pcm_recover(g_pcm, (int)wr, 1); /* 1 = silent */
                    retries++;
                    continue; /* retry after recovery, don't break */
                }
                retries = 0;
                ptr += wr * AUDIO_CH;
                remaining -= (int)wr;
            }
        }
    }
    return NULL;
}

static void mixer_start(void) {
    if (g_mixer_running) return;
    memset(g_voices, 0, sizeof(g_voices));
    g_mixer_running = 1;
    g_mixer_sample_pos = 0;
    pthread_create(&g_mixer_thread, NULL, mixer_thread_fn, NULL);
}

static void mixer_stop(void) {
    if (!g_mixer_running) return;
    g_mixer_running = 0;
    pthread_join(g_mixer_thread, NULL);
    if (g_pcm) snd_pcm_drop(g_pcm);
}

static void voice_add_at(const float *pcm, int frames, float speed, int64_t start_sample) {
    if (!g_mixer_running) mixer_start();
    pthread_mutex_lock(&g_voice_lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            g_voices[i].pcm          = pcm;
            g_voices[i].frames       = frames;
            g_voices[i].pos          = (start_sample < 0) ? 0 : -1;
            g_voices[i].speed        = speed;
            g_voices[i].start_sample = start_sample;
            g_voices[i].active       = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_voice_lock);
}

static void voice_add(const float *pcm, int frames, float speed) {
    voice_add_at(pcm, frames, speed, -1);
}

/* sample-play now uses voice_add instead of blocking mix_add+mix_flush */

/* (sample-flush) -- no-op with background mixer */
static Val prim_sample_flush(Val args, Val env) {
    (void)args; (void)env;
    return NIL_VAL;
}

/* (audio-time) → current mixer output position in ms */
static Val prim_audio_time(Val args, Val env) {
    (void)args; (void)env;
    int64_t ms = g_mixer_sample_pos * 1000 / AUDIO_SR;
    return MAKE_INT((int32_t)(ms & 0x7FFFFFFF));
}

/* (audio-at time-ms 'sample [speed]) -- schedule sample at absolute mixer time */
static Val prim_audio_at(Val args, Val env) {
    (void)env;
    if (!g_pcm) return NIL_VAL;
    Val time_val = CAR(args);
    Val name_val = CADR(args);
    if (!IS_INT(time_val) || !IS_SYM(name_val)) return NIL_VAL;
    int32_t time_ms = INT_VAL(time_val);
    const char *name = sym_name(SYM_IDX(name_val));
    float speed = 1.0f;
    if (IS_CONS(CDDR(args)) && IS_INT(CADDR(args)))
        speed = (float)INT_VAL(CADDR(args)) / 100.0f;
    int slot = sample_find(name);
    if (slot < 0) return NIL_VAL;
    SampleSlot *ss = &g_samples[slot];
    int64_t start_sample = (int64_t)time_ms * AUDIO_SR / 1000;
    voice_add_at(ss->pcm, ss->frames, speed, start_sample);
    return T_VAL;
}

/* (sample-play 'name [speed]) -- immediate voice playback */
static Val prim_sample_play(Val args, Val env) {
    (void)env;
    if (!g_pcm || !IS_SYM(CAR(args))) return NIL_VAL;
    const char *name = sym_name(SYM_IDX(CAR(args)));
    float speed = 1.0f;
    if (IS_CONS(CDR(args)) && IS_INT(CADR(args)))
        speed = (float)INT_VAL(CADR(args)) / 100.0f;
    int slot = sample_find(name);
    if (slot < 0) return NIL_VAL;
    voice_add(g_samples[slot].pcm, g_samples[slot].frames, speed);
    return T_VAL;
}

/* =========================================================================
 * TinyGL interface -- gfx_get_offscreen_pixels / gfx_mark_pixels_dirty
 * ===================================================================== */

uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes) {
    if (!g_pixels)
        g_pixels = (uint32_t *)calloc((size_t)g_w * (size_t)g_h, 4);
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (stride_bytes) *stride_bytes = g_w * 4;
    return g_pixels;
}

void gfx_mark_pixels_dirty(void) {
    if (!g_dpy || !g_pixels) return;
    int scr = DefaultScreen(g_dpy);
    XImage *img = XCreateImage(g_dpy,
                               DefaultVisual(g_dpy, scr),
                               (unsigned)DefaultDepth(g_dpy, scr),
                               ZPixmap, 0,
                               (char *)g_pixels,
                               (unsigned)g_w, (unsigned)g_h,
                               32, g_w * 4);
    if (!img) return;
    XPutImage(g_dpy, g_back, g_gc, img, 0, 0, 0, 0,
              (unsigned)g_w, (unsigned)g_h);
    img->data = NULL; /* don't free g_pixels */
    XDestroyImage(img);
    /* Also present to the window (tgl-present doesn't call sdl-present) */
    XCopyArea(g_dpy, g_back, g_win, g_gc, 0, 0,
              (unsigned)g_w, (unsigned)g_h, 0, 0);
    XFlush(g_dpy);
}

/* =========================================================================
 * Registration
 * ===================================================================== */

void gfx_prims_register(void) {
    /* Register WM_DELETE_WINDOW after display is available.
     * We set it lazily in prim_sdl_window; store the atom there. */

    prim_register("sdl-window",      prim_sdl_window,      3, 3);
    prim_register("sdl-color",       prim_sdl_color,       3, 3);
    prim_register("sdl-clear",       prim_sdl_clear,       0, 0);
    prim_register("sdl-present",     prim_sdl_present,     0, 0);
    prim_register("sdl-point",       prim_sdl_point,       2, 2);
    prim_register("sdl-line",        prim_sdl_line,        4, 4);
    prim_register("sdl-rect",        prim_sdl_rect,        4, 4);
    prim_register("sdl-fill",        prim_sdl_fill,        4, 4);
    prim_register("sdl-circle",      prim_sdl_circle,      3, 3);
    prim_register("sdl-fill-circle", prim_sdl_fill_circle, 3, 3);
    prim_register("sdl-ticks",       prim_sdl_ticks,       0, 0);
    prim_register("sdl-delay",       prim_sdl_delay,       1, 1);
    prim_register("sdl-key-name",    prim_sdl_key_name,    1, 1);
    prim_register("sdl-quit",        prim_sdl_quit,        0, 0);
    prim_register("sdl-poll",        prim_sdl_poll,        0, 0);
    prim_register("sdl-wait-event",  prim_sdl_wait_event,  0, 0);
    prim_register("sdl-font-load",   prim_sdl_font_load,   2, 2);
    prim_register("sdl-text",        prim_sdl_text,        3, 3);
    prim_register("sdl-text-size",   prim_sdl_text_size,   1, 1);
    prim_register("audio-init",        prim_audio_init,        0, 0);
    prim_register("audio-tone",        prim_audio_tone,        3, 4);
    prim_register("audio-drums",       prim_audio_drums,       2, 2);
    prim_register("audio-pending",     prim_audio_pending,     0, 0);
    prim_register("audio-clear",       prim_audio_clear,       0, 0);
    prim_register("audio-quit",        prim_audio_quit,        0, 0);
    prim_register("audio-volume",      prim_audio_volume,      1, 1);
    prim_register("audio-stop-all",    prim_audio_stop_all,    0, 0);
    prim_register("sample-load",       prim_sample_load,       2, 2);
    prim_register("samples-load-dir",  prim_samples_load_dir,  1, 1);
    prim_register("sample-play",       prim_sample_play,       1, 2);
    prim_register("sample-flush",     prim_sample_flush,      0, 0);
    prim_register("audio-time",       prim_audio_time,        0, 0);
    prim_register("audio-at",         prim_audio_at,          2, 3);
}
