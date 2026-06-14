/* native_gfx_win32.c -- Win32 + GDI + WinMM graphics/audio backend */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include "picolisp.h"
#include "native_gfx.h"
#include "ttf.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Global Win32 state
 * ===================================================================== */
static HWND       g_hwnd   = NULL;
static HDC        g_hdc    = NULL;   /* window DC */
static HDC        g_memdc  = NULL;   /* offscreen DC */
static HBITMAP    g_bmp    = NULL;   /* DIB section */
static HBITMAP    g_old_bmp = NULL;  /* previous bitmap in g_memdc (for cleanup) */
static BITMAPINFO g_bmi;
static uint32_t  *g_pixels = NULL;   /* DIB pixels (XRGB8888, top-down) */
static int        g_w = 0, g_h = 0;
static COLORREF   g_color  = RGB(255,255,255);

/* =========================================================================
 * Global font state
 * ===================================================================== */
#define MAX_FONTS 32
static TtfFont *g_fonts[MAX_FONTS];
static int      g_font_sizes[MAX_FONTS];
static int      g_font_count = 0;

/* =========================================================================
 * Audio constants & WinMM state
 * ===================================================================== */
#define AUDIO_SR 44100
#define AUDIO_CH 2

static HWAVEOUT   g_wave_out   = NULL;
static float      g_volume     = 1.0f;
static int        g_audio_open = 0;

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
 * Event queue (keyboard events captured in WndProc)
 * ===================================================================== */
#define EVT_QUEUE_SIZE 64

typedef struct {
    UINT  message;
    WPARAM wParam;
    LPARAM lParam;
} WinEvent;

static WinEvent g_evt_queue[EVT_QUEUE_SIZE];
static int      g_evt_head = 0;
static int      g_evt_tail = 0;

static void evt_push(UINT msg, WPARAM wp, LPARAM lp) {
    int next = (g_evt_tail + 1) % EVT_QUEUE_SIZE;
    if (next == g_evt_head) return; /* queue full, drop */
    g_evt_queue[g_evt_tail].message = msg;
    g_evt_queue[g_evt_tail].wParam  = wp;
    g_evt_queue[g_evt_tail].lParam  = lp;
    g_evt_tail = next;
}

static int evt_pop(WinEvent *out) {
    if (g_evt_head == g_evt_tail) return 0;
    *out = g_evt_queue[g_evt_head];
    g_evt_head = (g_evt_head + 1) % EVT_QUEUE_SIZE;
    return 1;
}

/* =========================================================================
 * Window procedure
 * ===================================================================== */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY || msg == WM_CLOSE) {
        evt_push(WM_QUIT, 0, 0);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_memdc)
            BitBlt(dc, 0, 0, g_w, g_h, g_memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        evt_push(msg, wp, lp);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* =========================================================================
 * Window management -- sdl-window
 * ===================================================================== */

/* Recreate the DIB section for the current g_w/g_h.  g_memdc must exist. */
static int gfx_create_dib(void) {
    if (g_old_bmp) { SelectObject(g_memdc, g_old_bmp); g_old_bmp = NULL; }
    if (g_bmp)     { DeleteObject(g_bmp); g_bmp = NULL; g_pixels = NULL; }

    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth       = g_w;
    g_bmi.bmiHeader.biHeight      = -g_h;  /* top-down */
    g_bmi.bmiHeader.biPlanes      = 1;
    g_bmi.bmiHeader.biBitCount    = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    g_bmp = CreateDIBSection(g_memdc, &g_bmi, DIB_RGB_COLORS,
                              (void **)&g_pixels, NULL, 0);
    if (!g_bmp) return 0;
    g_old_bmp = (HBITMAP)SelectObject(g_memdc, g_bmp);
    return 1;
}

static Val prim_sdl_window(Val args, Val env) {
    (void)env;
    const char *title = str_ptr(STR_IDX(CAR(args)));
    int new_w = INT_VAL(CADR(args));
    int new_h = INT_VAL(CADDR(args));

    if (g_hwnd) {
        /* Resize the existing window rather than creating a new one.
         * This avoids leaking the first window when sdl-window is called
         * a second time (e.g. after measuring font metrics). */
        g_w = new_w;
        g_h = new_h;
        SetWindowTextA(g_hwnd, title);
        RECT rc = { 0, 0, g_w, g_h };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(g_hwnd, NULL, 0, 0,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!gfx_create_dib()) return NIL_VAL;
        return T_VAL;
    }

    g_w = new_w;
    g_h = new_h;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "LWindow";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    /* Adjust window size so client area matches requested dimensions */
    RECT rc = { 0, 0, g_w, g_h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = rc.right - rc.left;
    int wh = rc.bottom - rc.top;

    g_hwnd = CreateWindowExA(0, "LWindow", title,
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                              NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) return NIL_VAL;

    g_hdc   = GetDC(g_hwnd);
    g_memdc = CreateCompatibleDC(g_hdc);

    if (!gfx_create_dib()) return NIL_VAL;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return T_VAL;
}

/* =========================================================================
 * 2D drawing primitives
 * ===================================================================== */

/* (sdl-color r g b) */
static Val prim_sdl_color(Val args, Val env) {
    (void)env;
    int r = INT_VAL(CAR(args));
    int g = INT_VAL(CADR(args));
    int b = INT_VAL(CADDR(args));
    g_color = RGB(r, g, b);
    return NIL_VAL;
}

/* (sdl-clear) -- fill with current color */
static Val prim_sdl_clear(Val args, Val env) {
    (void)args; (void)env;
    if (!g_memdc) return NIL_VAL;
    RECT rc = { 0, 0, g_w, g_h };
    HBRUSH br = CreateSolidBrush(g_color);
    FillRect(g_memdc, &rc, br);
    DeleteObject(br);
    return NIL_VAL;
}

/* (sdl-present) -- blit offscreen to window */
static Val prim_sdl_present(Val args, Val env) {
    (void)args; (void)env;
    if (!g_hdc || !g_memdc) return NIL_VAL;
    BitBlt(g_hdc, 0, 0, g_w, g_h, g_memdc, 0, 0, SRCCOPY);
    return NIL_VAL;
}

/* (sdl-point x y) */
static Val prim_sdl_point(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    SetPixel(g_memdc, INT_VAL(CAR(args)), INT_VAL(CADR(args)), g_color);
    return NIL_VAL;
}

/* (sdl-line x1 y1 x2 y2) */
static Val prim_sdl_line(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    int x1 = INT_VAL(CAR(args));
    int y1 = INT_VAL(CADR(args));
    int x2 = INT_VAL(CADDR(args));
    int y2 = INT_VAL(CAR(CDDDR(args)));
    HPEN pen = CreatePen(PS_SOLID, 1, g_color);
    HPEN old = (HPEN)SelectObject(g_memdc, pen);
    MoveToEx(g_memdc, x1, y1, NULL);
    LineTo(g_memdc, x2, y2);
    SelectObject(g_memdc, old);
    DeleteObject(pen);
    return NIL_VAL;
}

/* (sdl-rect x y w h) -- outline only */
static Val prim_sdl_rect(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    int x = INT_VAL(CAR(args));
    int y = INT_VAL(CADR(args));
    int w = INT_VAL(CADDR(args));
    int h = INT_VAL(CAR(CDDDR(args)));
    HPEN   pen = CreatePen(PS_SOLID, 1, g_color);
    HBRUSH br  = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN   op  = (HPEN)SelectObject(g_memdc, pen);
    HBRUSH ob  = (HBRUSH)SelectObject(g_memdc, br);
    Rectangle(g_memdc, x, y, x + w, y + h);
    SelectObject(g_memdc, op);
    SelectObject(g_memdc, ob);
    DeleteObject(pen);
    return NIL_VAL;
}

/* (sdl-fill x y w h) */
static Val prim_sdl_fill(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    int x = INT_VAL(CAR(args));
    int y = INT_VAL(CADR(args));
    int w = INT_VAL(CADDR(args));
    int h = INT_VAL(CAR(CDDDR(args)));
    RECT rc = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(g_color);
    FillRect(g_memdc, &rc, br);
    DeleteObject(br);
    return NIL_VAL;
}

/* Bresenham circle helper */
static void draw_circle_win32(int cx, int cy, int radius, int filled) {
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        if (filled) {
            /* Draw horizontal scan lines for fill */
            MoveToEx(g_memdc, cx - x, cy + y, NULL); LineTo(g_memdc, cx + x, cy + y);
            MoveToEx(g_memdc, cx - x, cy - y, NULL); LineTo(g_memdc, cx + x, cy - y);
            MoveToEx(g_memdc, cx - y, cy + x, NULL); LineTo(g_memdc, cx + y, cy + x);
            MoveToEx(g_memdc, cx - y, cy - x, NULL); LineTo(g_memdc, cx + y, cy - x);
        } else {
            SetPixel(g_memdc, cx + x, cy + y, g_color);
            SetPixel(g_memdc, cx - x, cy + y, g_color);
            SetPixel(g_memdc, cx + x, cy - y, g_color);
            SetPixel(g_memdc, cx - x, cy - y, g_color);
            SetPixel(g_memdc, cx + y, cy + x, g_color);
            SetPixel(g_memdc, cx - y, cy + x, g_color);
            SetPixel(g_memdc, cx + y, cy - x, g_color);
            SetPixel(g_memdc, cx - y, cy - x, g_color);
        }
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0)  { x--; err -= 2 * x + 1; }
    }
}

/* (sdl-circle cx cy r) */
static Val prim_sdl_circle(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    HPEN pen = CreatePen(PS_SOLID, 1, g_color);
    HPEN op  = (HPEN)SelectObject(g_memdc, pen);
    draw_circle_win32(INT_VAL(CAR(args)), INT_VAL(CADR(args)),
                      INT_VAL(CADDR(args)), 0);
    SelectObject(g_memdc, op);
    DeleteObject(pen);
    return NIL_VAL;
}

/* (sdl-fill-circle cx cy r) */
static Val prim_sdl_fill_circle(Val args, Val env) {
    (void)env;
    if (!g_memdc) return NIL_VAL;
    HPEN pen = CreatePen(PS_SOLID, 1, g_color);
    HPEN op  = (HPEN)SelectObject(g_memdc, pen);
    draw_circle_win32(INT_VAL(CAR(args)), INT_VAL(CADR(args)),
                      INT_VAL(CADDR(args)), 1);
    SelectObject(g_memdc, op);
    DeleteObject(pen);
    return NIL_VAL;
}

/* =========================================================================
 * Timing
 * ===================================================================== */

/* (sdl-ticks) → ms as INT */
static Val prim_sdl_ticks(Val args, Val env) {
    (void)args; (void)env;
    ULONGLONG ms = GetTickCount64();
    return MAKE_INT((int32_t)(ms & 0x7FFFFFFF));
}

/* (sdl-delay ms) */
static Val prim_sdl_delay(Val args, Val env) {
    (void)env;
    int ms = INT_VAL(CAR(args));
    if (ms > 0) Sleep((DWORD)ms);
    return NIL_VAL;
}

/* =========================================================================
 * Key mapping
 * ===================================================================== */

static const char *vk_to_name(WPARAM vk) {
    switch (vk) {
        case VK_RETURN:  return "Return";
        case VK_ESCAPE:  return "Escape";
        case VK_BACK:    return "Backspace";
        case VK_TAB:     return "Tab";
        case VK_UP:      return "Up";
        case VK_DOWN:    return "Down";
        case VK_LEFT:    return "Left";
        case VK_RIGHT:   return "Right";
        case VK_DELETE:  return "Delete";
        case VK_HOME:    return "Home";
        case VK_END:     return "End";
        case VK_PRIOR:   return "Page_Up";
        case VK_NEXT:    return "Page_Down";
        case VK_INSERT:  return "Insert";
        case VK_F1:      return "F1";
        case VK_F2:      return "F2";
        case VK_F3:      return "F3";
        case VK_F4:      return "F4";
        case VK_F5:      return "F5";
        case VK_F6:      return "F6";
        case VK_F7:      return "F7";
        case VK_F8:      return "F8";
        case VK_F9:      return "F9";
        case VK_F10:     return "F10";
        case VK_F11:     return "F11";
        case VK_F12:     return "F12";
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:  return "Shift";
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:return "Ctrl";
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:   return "Alt";
        case VK_LWIN:
        case VK_RWIN:    return "Super";
        case VK_SPACE:   return "Space";
        default:         return NULL;
    }
}

/* (sdl-key-name scancode) → string */
static Val prim_sdl_key_name(Val args, Val env) {
    (void)env;
    int code = INT_VAL(CAR(args));
    char buf[8];
    const char *name = vk_to_name((WPARAM)code);
    if (!name) {
        buf[0] = (char)code; buf[1] = '\0';
        name = buf;
    }
    uint32_t si = str_intern(name, strlen(name));
    return MAKE_STR(si);
}

/* =========================================================================
 * Event building helpers
 * ===================================================================== */

/* Build event alists: (("type" . "keydown") ("key" . name) ("mod" . bits))
 * and (("type" . "quit")).  Callers use (cdr (assoc "key" ev)) to access. */

static Val build_keydown_event_win32(const char *kname) {
    uint32_t s_type    = str_intern("type",    4);
    uint32_t s_key     = str_intern("key",     3);
    uint32_t s_mod     = str_intern("mod",     3);
    uint32_t s_keydown = str_intern("keydown", 7);
    uint32_t s_kname   = str_intern(kname, strlen(kname));

    int32_t mod_val = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mod_val |= 0x0001;
    if (GetKeyState(VK_CONTROL) & 0x8000) mod_val |= 0x0040;
    if (GetKeyState(VK_MENU)    & 0x8000) mod_val |= 0x0100;

    /* Build alist back-to-front, keeping accumulation rooted across each pl_cons */
    Val list = pl_cons(MAKE_STR(s_mod), MAKE_INT(mod_val));   /* ("mod" . bits) */
    PUSH_ROOT(list);
    list = pl_cons(list, NIL_VAL);                             /* (("mod" . bits)) */
    g_gc_roots[g_gc_root_top - 1] = list;

    Val kp = pl_cons(MAKE_STR(s_key), MAKE_STR(s_kname));     /* ("key" . name) */
    PUSH_ROOT(kp);
    list = pl_cons(kp, list);
    POP_ROOT();
    g_gc_roots[g_gc_root_top - 1] = list;                     /* (("key"...) ("mod"...)) */

    Val tp = pl_cons(MAKE_STR(s_type), MAKE_STR(s_keydown));  /* ("type" . "keydown") */
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
    Val pair = pl_cons(MAKE_STR(s_type), MAKE_STR(s_quit));   /* ("type" . "quit") */
    PUSH_ROOT(pair);
    Val list = pl_cons(pair, NIL_VAL);                         /* (("type" . "quit")) */
    POP_ROOT();
    return list;
}

static Val process_win_event(WinEvent *ev) {
    if (ev->message == WM_QUIT) {
        return build_quit_event();
    }

    if (ev->message == WM_KEYDOWN || ev->message == WM_SYSKEYDOWN) {
        WPARAM vk = ev->wParam;
        int ctrl_held = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        /* Check named keys first */
        const char *fixed = vk_to_name(vk);
        if (fixed) {
            /* Skip bare modifier key events */
            if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                vk == VK_LWIN || vk == VK_RWIN)
                return NIL_VAL;
            return build_keydown_event_win32(fixed);
        }

        /* Ctrl + letter: synthesize "ctrl-x" */
        if (ctrl_held && vk >= 'A' && vk <= 'Z') {
            char buf[8];
            char lc = (char)('a' + (vk - 'A'));
            buf[0]='c'; buf[1]='t'; buf[2]='r'; buf[3]='l';
            buf[4]='-'; buf[5]=lc; buf[6]='\0';
            return build_keydown_event_win32(buf);
        }

        /* For other VK keys, try to get the character via MapVirtualKey */
        UINT ch = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_CHAR);
        if (ch >= 0x20 && ch <= 0x7E) {
            char buf[2] = { (char)ch, '\0' };
            return build_keydown_event_win32(buf);
        }

        return NIL_VAL;
    }

    if (ev->message == WM_CHAR) {
        WPARAM ch = ev->wParam;
        if (ch >= 0x20 && ch <= 0x7E) {
            /* Only emit WM_CHAR for printable chars not already handled
             * by WM_KEYDOWN (to avoid duplicates, we skip WM_CHAR here
             * since WM_KEYDOWN already covers printable ASCII via MapVirtualKey).
             * We DO emit WM_CHAR for characters that can't be derived from VK alone. */
            return NIL_VAL; /* suppress -- WM_KEYDOWN handles printable */
        }
        return NIL_VAL;
    }

    return NIL_VAL;
}

/* =========================================================================
 * Event primitives
 * ===================================================================== */

/* (sdl-poll) → event plist or NIL */
static Val prim_sdl_poll(Val args, Val env) {
    (void)args; (void)env;

    /* Drain Windows message queue into our event queue */
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* Return first pending event */
    WinEvent ev;
    while (evt_pop(&ev)) {
        Val result = process_win_event(&ev);
        if (!IS_NIL(result)) return result;
    }
    return NIL_VAL;
}

/* (sdl-wait-event) → event plist */
static Val prim_sdl_wait_event(Val args, Val env) {
    (void)args; (void)env;
    for (;;) {
        /* Process pending events first */
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        WinEvent ev;
        while (evt_pop(&ev)) {
            Val result = process_win_event(&ev);
            if (!IS_NIL(result)) return result;
        }

        /* Wait for next message */
        WaitMessage();
    }
}

/* (sdl-quit) */
static Val prim_sdl_quit(Val args, Val env) {
    (void)args; (void)env;
    if (g_hdc && g_hwnd) { ReleaseDC(g_hwnd, g_hdc); g_hdc = NULL; }
    if (g_memdc) {
        if (g_old_bmp) { SelectObject(g_memdc, g_old_bmp); g_old_bmp = NULL; }
        DeleteDC(g_memdc); g_memdc = NULL;
    }
    if (g_bmp)   { DeleteObject(g_bmp); g_bmp = NULL; g_pixels = NULL; }
    if (g_hwnd)  { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    return NIL_VAL;
}

/* =========================================================================
 * Text rendering -- sdl-font-load, sdl-text, sdl-text-size
 * ===================================================================== */

/* Alpha-blend a glyph bitmap into the DIB pixel buffer */
static void blit_glyph_dib(int px, int py, uint8_t *bmp, int bw, int bh,
                            COLORREF col) {
    if (!g_pixels || !bmp) return;   /* guard: no pixel buffer */
    uint8_t fr = GetRValue(col);
    uint8_t fg = GetGValue(col);
    uint8_t fb = GetBValue(col);
    for (int y = 0; y < bh; y++) {
        int gy = py + y;
        if (gy < 0 || gy >= g_h) continue;
        for (int x = 0; x < bw; x++) {
            int gx = px + x;
            if (gx < 0 || gx >= g_w) continue;
            uint8_t a = bmp[(bh - 1 - y) * bw + x]; /* bitmap is bottom-up; flip to screen */
            if (!a) continue;
            uint32_t *dst = &g_pixels[gy * g_w + gx];
            /* Win32 DIB 32-bit: stored as 0x00RRGGBB (little-endian bytes: B,G,R,0) */
            uint8_t dr = (uint8_t)((*dst >> 16) & 0xFF);
            uint8_t dg = (uint8_t)((*dst >>  8) & 0xFF);
            uint8_t db = (uint8_t)((*dst)        & 0xFF);
            uint8_t nr = (uint8_t)(dr + ((int)(fr - dr) * a >> 8));
            uint8_t ng = (uint8_t)(dg + ((int)(fg - dg) * a >> 8));
            uint8_t nb = (uint8_t)(db + ((int)(fb - db) * a >> 8));
            *dst = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
        }
    }
}

/* (sdl-font-load path ptsize) → font-index or -1 */
static Val prim_sdl_font_load(Val args, Val env) {
    (void)env;
    const char *path   = str_ptr(STR_IDX(CAR(args)));
    int         ptsize = INT_VAL(CADR(args));
    if (g_font_count >= MAX_FONTS) return MAKE_INT(-1);
    TtfFont *font = ttf_load(path);
    if (!font) {
        fprintf(stderr, "[win32] sdl-font-load FAILED for '%s'\n", path);
        return MAKE_INT(-1);
    }
    fprintf(stderr, "[win32] sdl-font-load OK: '%s' @%dpx\n", path, ptsize);
    g_fonts[g_font_count]      = font;
    g_font_sizes[g_font_count] = ptsize;
    return MAKE_INT(g_font_count++);
}

/* (sdl-text x y "string") */
static Val prim_sdl_text(Val args, Val env) {
    (void)env;
    if (!g_memdc || g_font_count == 0) return NIL_VAL;
    int         px   = INT_VAL(CAR(args));
    int         py   = INT_VAL(CADR(args));
    Val         sv   = CADDR(args);
    if (!IS_STR(sv)) return NIL_VAL;   /* guard: non-string arg */
    const char *text = str_ptr(STR_IDX(sv));
    if (!text) return NIL_VAL;

    TtfFont *font   = g_fonts[0];
    int      ptsize = g_font_sizes[0];

    int asc = 0, desc = 0, gap = 0;
    ttf_line_metrics(font, ptsize, &asc, &desc, &gap);

    int pen_x = px;
    int pen_y = py + asc; /* baseline */

    for (const char *p = text; *p; p++) {
        uint32_t cp = (uint8_t)*p;
        int bw = 0, bh = 0, xoff = 0, yoff = 0;
        uint8_t *bmp = ttf_render_glyph(font, cp, ptsize, &bw, &bh, &xoff, &yoff);
        if (bmp) {
            blit_glyph_dib(pen_x + xoff, pen_y + yoff, bmp, bw, bh, g_color);
            free(bmp);
        }
        pen_x += ttf_glyph_advance(font, cp, ptsize);
    }
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
 * WinMM audio helpers
 * ===================================================================== */

/* ---- Voice-based audio mixer with background thread ----
 * sample-play adds a voice (pointer into sample PCM + position).
 * A background thread continuously mixes all active voices into
 * small chunks (~20ms) and feeds them to waveOut. This gives true
 * simultaneous playback of multiple samples. */

#include <process.h> /* _beginthreadex */

#define MAX_VOICES     32
#define MIX_CHUNK_MS   20
#define MIX_CHUNK_FRAMES (AUDIO_SR * MIX_CHUNK_MS / 1000)  /* 882 frames */
#define WAVE_BUFS       4  /* double-buffering for waveOut */

typedef struct {
    const float *pcm;       /* points into SampleSlot.pcm (not owned) */
    int          frames;    /* total frames in sample */
    int          pos;       /* playback position (-1 = waiting for start_sample) */
    float        speed;
    int64_t      start_sample; /* absolute sample position to begin playback */
    volatile int active;
} Voice;

static Voice            g_voices[MAX_VOICES];
static CRITICAL_SECTION g_voice_lock;
static volatile int     g_mixer_running = 0;
static HANDLE           g_mixer_thread  = NULL;
static volatile int64_t g_mixer_sample_pos = 0; /* output stream position in frames */

/* waveOut double-buffer */
typedef struct { WAVEHDR hdr; int16_t data[MIX_CHUNK_FRAMES * AUDIO_CH]; int prepared; } MixBuf;
static MixBuf g_mix_bufs[WAVE_BUFS];

static unsigned __stdcall mixer_thread_fn(void *arg) {
    (void)arg;
    int buf_idx = 0;
    while (g_mixer_running) {
        MixBuf *mb = &g_mix_bufs[buf_idx];

        /* Wait for previous use of this buffer to finish */
        if (mb->prepared) {
            while (!(mb->hdr.dwFlags & WHDR_DONE)) Sleep(1);
            waveOutUnprepareHeader(g_wave_out, &mb->hdr, sizeof(mb->hdr));
            mb->prepared = 0;
        }

        /* Mix all active voices into this chunk.
         * Voices with pos == -1 are waiting for their start_sample time. */
        float mix[MIX_CHUNK_FRAMES * AUDIO_CH];
        memset(mix, 0, sizeof(mix));
        int64_t chunk_start = g_mixer_sample_pos;
        int64_t chunk_end   = chunk_start + MIX_CHUNK_FRAMES;

        EnterCriticalSection(&g_voice_lock);
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vc = &g_voices[v];
            if (!vc->active) continue;

            /* Check if this voice should start during this chunk */
            if (vc->pos < 0) {
                if (vc->start_sample >= chunk_end) continue; /* not yet */
                /* Start partway through this chunk */
                int offset = (int)(vc->start_sample - chunk_start);
                if (offset < 0) offset = 0;
                vc->pos = 0;
                /* Mix from offset within this chunk */
                for (int i = offset; i < MIX_CHUNK_FRAMES; i++) {
                    int si = (vc->speed == 1.0f) ? vc->pos
                             : (int)((float)vc->pos * vc->speed);
                    if (si >= vc->frames) { vc->active = 0; break; }
                    mix[i * 2]     += vc->pcm[si * 2];
                    mix[i * 2 + 1] += vc->pcm[si * 2 + 1];
                    vc->pos++;
                }
                continue;
            }

            /* Already playing -- mix full chunk */
            for (int i = 0; i < MIX_CHUNK_FRAMES; i++) {
                int si = (vc->speed == 1.0f) ? vc->pos + i
                         : (int)((float)(vc->pos + i) * vc->speed);
                if (si >= vc->frames) { vc->active = 0; break; }
                mix[i * 2]     += vc->pcm[si * 2];
                mix[i * 2 + 1] += vc->pcm[si * 2 + 1];
            }
            if (vc->active) {
                vc->pos += (vc->speed == 1.0f) ? MIX_CHUNK_FRAMES
                           : (int)((float)MIX_CHUNK_FRAMES * vc->speed);
                if (vc->pos >= vc->frames) vc->active = 0;
            }
        }
        LeaveCriticalSection(&g_voice_lock);
        g_mixer_sample_pos = chunk_end;

        /* Convert to S16 and submit */
        for (int i = 0; i < MIX_CHUNK_FRAMES * AUDIO_CH; i++) {
            float s = mix[i] * g_volume;
            if      (s >  1.0f) s =  1.0f;
            else if (s < -1.0f) s = -1.0f;
            mb->data[i] = (int16_t)(s * 32767.0f);
        }

        memset(&mb->hdr, 0, sizeof(mb->hdr));
        mb->hdr.lpData         = (LPSTR)mb->data;
        mb->hdr.dwBufferLength = sizeof(mb->data);
        if (waveOutPrepareHeader(g_wave_out, &mb->hdr, sizeof(mb->hdr)) == MMSYSERR_NOERROR) {
            waveOutWrite(g_wave_out, &mb->hdr, sizeof(mb->hdr));
            mb->prepared = 1;
        }

        buf_idx = (buf_idx + 1) % WAVE_BUFS;
    }
    return 0;
}

static void mixer_start(void) {
    if (g_mixer_running) return;
    InitializeCriticalSection(&g_voice_lock);
    memset(g_voices, 0, sizeof(g_voices));
    memset(g_mix_bufs, 0, sizeof(g_mix_bufs));
    g_mixer_running = 1;
    g_mixer_thread = (HANDLE)_beginthreadex(NULL, 0, mixer_thread_fn, NULL, 0, NULL);
}

static void mixer_stop(void) {
    if (!g_mixer_running) return;
    g_mixer_running = 0;
    if (g_mixer_thread) { WaitForSingleObject(g_mixer_thread, 1000); CloseHandle(g_mixer_thread); g_mixer_thread = NULL; }
    waveOutReset(g_wave_out);
    for (int i = 0; i < WAVE_BUFS; i++) {
        if (g_mix_bufs[i].prepared)
            waveOutUnprepareHeader(g_wave_out, &g_mix_bufs[i].hdr, sizeof(g_mix_bufs[i].hdr));
    }
    DeleteCriticalSection(&g_voice_lock);
}

/* Add a voice. start_sample = -1 means play immediately (pos = 0).
 * start_sample >= 0 means wait until the mixer reaches that position (pos = -1). */
static void voice_add_at(const float *pcm, int frames, float speed, int64_t start_sample) {
    if (!g_mixer_running) mixer_start();
    EnterCriticalSection(&g_voice_lock);
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
    LeaveCriticalSection(&g_voice_lock);
}

static void voice_add(const float *pcm, int frames, float speed) {
    voice_add_at(pcm, frames, speed, -1); /* immediate */
}

/* Write F32 stereo PCM via the mixer (used by audio-tone / audio-drums).
 * Copies data to a temp buffer since the mixer reads asynchronously. */
static void winmm_write_f32(const float *buf, int frames) {
    if (!g_audio_open || !buf || frames <= 0) return;
    float *copy = (float *)malloc((size_t)frames * AUDIO_CH * sizeof(float));
    if (!copy) return;
    memcpy(copy, buf, (size_t)frames * AUDIO_CH * sizeof(float));
    /* voice_add takes ownership -- but we need the data to persist.
     * Store in a small ring of temp buffers. */
    static float *temp_ring[16]; static int temp_idx = 0;
    free(temp_ring[temp_idx]);
    temp_ring[temp_idx] = copy;
    temp_idx = (temp_idx + 1) % 16;
    voice_add(copy, frames, 1.0f);
}

/* =========================================================================
 * Audio primitives
 * ===================================================================== */

/* (audio-init) → T or NIL */
static Val prim_audio_init(Val args, Val env) {
    (void)args; (void)env;
    if (g_audio_open) return T_VAL;

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = AUDIO_CH;
    wfx.nSamplesPerSec  = AUDIO_SR;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    MMRESULT res = waveOutOpen(&g_wave_out, WAVE_MAPPER, &wfx,
                               0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        fprintf(stderr, "[win32] waveOutOpen failed: %u\n", (unsigned)res);
        return NIL_VAL;
    }
    g_audio_open = 1;
    return T_VAL;
}

/* (audio-tone wave freq-hz dur-ms [vol])
 * wave: 0=sine 1=square 2=sawtooth 3=triangle */
static Val prim_audio_tone(Val args, Val env) {
    (void)env;
    if (!g_audio_open) return NIL_VAL;
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
        float t    = (float)i / (float)(n > 1 ? n - 1 : 1);
        float env2 = expf(-4.0f * t) * vol;
        float pp   = phase - floorf(phase);
        float s    = 0.0f;
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
    winmm_write_f32(buf, n);
    free(buf);
    return NIL_VAL;
}

/* (audio-drums mask dur-ms)
 * mask bits: bit0=kick, bit1=snare, bit2=closed-hihat, bit3=open-hihat */
static Val prim_audio_drums(Val args, Val env) {
    (void)env;
    if (!g_audio_open) return NIL_VAL;
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
            float t   = (float)i / (float)AUDIO_SR;
            float f   = 45.0f + 110.0f * expf(-t * 28.0f);
            float amp = 0.85f * expf(-t * 7.0f);
            float s   = sinf(phase * 6.2831853f) * amp;
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

    winmm_write_f32(buf, n);
    free(buf);
    return NIL_VAL;
}

/* (sample-flush) -- no-op with voice mixer (mixing is continuous) */
static Val prim_sample_flush(Val args, Val env) {
    (void)args; (void)env;
    return NIL_VAL;
}

/* (audio-time) → current mixer output position in milliseconds */
static Val prim_audio_time(Val args, Val env) {
    (void)args; (void)env;
    int64_t ms = g_mixer_sample_pos * 1000 / AUDIO_SR;
    return MAKE_INT((int32_t)(ms & 0x7FFFFFFF));
}

/* (audio-at time-ms 'sample [speed]) -- schedule sample at absolute mixer time */
static Val prim_audio_at(Val args, Val env) {
    (void)env;
    if (!g_audio_open) return NIL_VAL;
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

/* (audio-pending) → number of active voices */
static Val prim_audio_pending(Val args, Val env) {
    (void)args; (void)env;
    int count = 0;
    if (g_mixer_running) {
        EnterCriticalSection(&g_voice_lock);
        for (int i = 0; i < MAX_VOICES; i++)
            if (g_voices[i].active) count++;
        LeaveCriticalSection(&g_voice_lock);
    }
    return MAKE_INT(count);
}

static void wave_pool_flush(void) {
    mixer_stop();
}

/* (audio-clear) -- reset audio device */
static Val prim_audio_clear(Val args, Val env) {
    (void)args; (void)env;
    wave_pool_flush();
    return NIL_VAL;
}

/* (audio-quit) */
static Val prim_audio_quit(Val args, Val env) {
    (void)args; (void)env;
    if (g_audio_open) {
        wave_pool_flush();
        waveOutClose(g_wave_out);
        g_wave_out   = NULL;
        g_audio_open = 0;
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
    wave_pool_flush();
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

/* Load WAV, convert to F32 stereo at AUDIO_SR.
 * Returns slot index on success, -1 on failure. */
static int sample_load_impl(const char *name, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char tag[5] = {0};
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "RIFF", 4) != 0) { fclose(f); return -1; }
    read_u32le(f); /* chunk size */
    if (fread(tag, 1, 4, f) != 4 || memcmp(tag, "WAVE", 4) != 0) { fclose(f); return -1; }

    uint16_t audio_format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    int found_fmt = 0;

    for (;;) {
        if (fread(tag, 1, 4, f) != 4) break;
        uint32_t chunk_size = read_u32le(f);
        if (memcmp(tag, "fmt ", 4) == 0) {
            audio_format = read_u16le(f);
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

            uint8_t *raw = (uint8_t *)malloc(chunk_size);
            if (!raw) { fclose(f); return -1; }
            size_t got = fread(raw, 1, chunk_size, f);
            if (got == 0) { free(raw); fclose(f); return -1; }
            uint32_t actual_bytes = (uint32_t)got;

            uint32_t bytes_per_sample = (uint32_t)bits / 8u;
            if (bytes_per_sample == 0 || channels == 0) { free(raw); fclose(f); return -1; }
            uint32_t src_frames = actual_bytes / (bytes_per_sample * channels);

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
                        if (byte_off + 3 < actual_bytes) {
                            float tmp;
                            memcpy(&tmp, raw + byte_off, 4);
                            v = tmp;
                        }
                    } else if (bits == 16) {
                        if (byte_off + 1 < actual_bytes) {
                            int16_t s;
                            memcpy(&s, raw + byte_off, 2);
                            v = (float)s / 32768.0f;
                        }
                    } else if (bits == 8) {
                        if (byte_off < actual_bytes)
                            v = ((float)raw[byte_off] - 128.0f) / 128.0f;
                    } else if (bits == 24) {
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

    char pattern[520];
    snprintf(pattern, sizeof(pattern), "%s\\*.wav", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        /* Try lowercase .wav via explicit enumeration */
        snprintf(pattern, sizeof(pattern), "%s\\*", dir);
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return MAKE_INT(0);
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const char *fname = fd.cFileName;
            const char *dot   = strrchr(fname, '.');
            if (!dot) continue;
            if (_stricmp(dot, ".wav") != 0) continue;
            char stem[64];
            size_t slen = (size_t)(dot - fname);
            if (slen >= sizeof(stem)) slen = sizeof(stem) - 1;
            memcpy(stem, fname, slen); stem[slen] = '\0';
            char fpath[520];
            snprintf(fpath, sizeof(fpath), "%s\\%s", dir, fname);
            if (sample_load_impl(stem, fpath) >= 0) count++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        return MAKE_INT(count);
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const char *fname = fd.cFileName;
        const char *dot   = strrchr(fname, '.');
        if (!dot) continue;
        char stem[64];
        size_t slen = (size_t)(dot - fname);
        if (slen >= sizeof(stem)) slen = sizeof(stem) - 1;
        memcpy(stem, fname, slen); stem[slen] = '\0';
        char fpath[520];
        snprintf(fpath, sizeof(fpath), "%s\\%s", dir, fname);
        if (sample_load_impl(stem, fpath) >= 0) count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return MAKE_INT(count);
}

/* (sample-play 'name [speed-pct]) -- adds a voice to the background mixer */
static Val prim_sample_play(Val args, Val env) {
    (void)env;
    if (!g_audio_open || !IS_SYM(CAR(args))) return NIL_VAL;

    const char *name = sym_name(SYM_IDX(CAR(args)));
    float speed = 1.0f;
    if (IS_CONS(CDR(args)) && IS_INT(CADR(args)))
        speed = (float)INT_VAL(CADR(args)) / 100.0f;

    int slot = sample_find(name);
    if (slot < 0) return NIL_VAL;

    SampleSlot *ss = &g_samples[slot];
    voice_add(ss->pcm, ss->frames, speed);
    return T_VAL;
}

/* =========================================================================
 * TinyGL interface
 * ===================================================================== */

uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes) {
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (stride_bytes) *stride_bytes = g_w * 4;
    return g_pixels; /* direct DIB section pointer */
}

void gfx_mark_pixels_dirty(void) {
    /* DIB pixels are already in g_memdc via CreateDIBSection -- blit to screen */
    if (g_hdc && g_memdc)
        BitBlt(g_hdc, 0, 0, g_w, g_h, g_memdc, 0, 0, SRCCOPY);
}

/* =========================================================================
 * Registration
 * ===================================================================== */

void gfx_prims_register(void) {
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
    prim_register("audio-init",      prim_audio_init,      0, 0);
    prim_register("audio-tone",      prim_audio_tone,      3, 4);
    prim_register("audio-drums",     prim_audio_drums,     2, 2);
    prim_register("audio-pending",   prim_audio_pending,   0, 0);
    prim_register("audio-clear",     prim_audio_clear,     0, 0);
    prim_register("audio-quit",      prim_audio_quit,      0, 0);
    prim_register("audio-volume",    prim_audio_volume,    1, 1);
    prim_register("audio-stop-all",  prim_audio_stop_all,  0, 0);
    prim_register("sample-load",     prim_sample_load,     2, 2);
    prim_register("samples-load-dir",prim_samples_load_dir,1, 1);
    prim_register("sample-play",     prim_sample_play,     1, 2);
    prim_register("sample-flush",   prim_sample_flush,    0, 0);
    prim_register("audio-time",     prim_audio_time,      0, 0);
    prim_register("audio-at",       prim_audio_at,        2, 3);
}
