# Native I/O and Graphics Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove libuv and SDL dependencies; replace with OS-native C (Win32/GDI/WASAPI on Windows, Xlib/ALSA on Linux) plus a hand-written TTF rasterizer.

**Architecture:** Each platform gets its own source file (no `#ifdef` inside new files). The Makefile selects the right files based on `$(OS)`. All Lisp primitives keep identical signatures — only the C backend changes.

**Tech Stack:** C17, Win32 API, Xlib, WASAPI, ALSA, BSD sockets / Winsock2, `poll`/`WSAPoll`, `ucontext_t` / Windows Fibers.

---

## Shared headers to create first

### `src/coro.h`
```c
#pragma once
#include "picolisp.h"
void coro_init(void);
void coro_prims_register(void);
void gc_mark_coros(void);
```

### `src/native_io.h`
```c
#pragma once
void io_init(void);
void io_prims_register(void);
```

### `src/native_gfx.h`
```c
#pragma once
#include <stdint.h>
void      gfx_prims_register(void);
/* Used by tinygl_bridge.c to write its pixel buffer into the offscreen surface.
 * Returns pointer to XRGB8888 row-major pixel data. stride_bytes is row stride. */
uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes);
void      gfx_mark_pixels_dirty(void);   /* signal tgl-present was called */
```

### `src/ttf.h`
```c
#pragma once
#include <stdint.h>
typedef struct TtfFont TtfFont;
TtfFont *ttf_load(const char *path);
void     ttf_free(TtfFont *f);
/* Render one glyph. Returns malloc'd 8-bit alpha bitmap (caller frees).
 * w/h = bitmap dimensions. xoff/yoff = pixel offset from pen origin. */
uint8_t *ttf_render_glyph(TtfFont *f, uint32_t cp, int px,
                           int *w, int *h, int *xoff, int *yoff);
int      ttf_glyph_advance(TtfFont *f, uint32_t cp, int px);
void     ttf_line_metrics(TtfFont *f, int px, int *asc, int *desc, int *gap);
```

---

## Task 1: Create branch and extract `src/coro.c`

**Files:**
- Run: `git checkout -b native-io-gfx`
- Create: `src/coro.h` (above)
- Create: `src/coro.c`
- Modify: `src/picolisp.h:370-397` (remove `#ifdef HAVE_UV` around Coro struct)
- Modify: `src/heap.c` (remove `#ifdef HAVE_UV` around `gc_mark_coros()` call)
- Modify: `src/main.c` (replace `uv_io_init` call with `coro_init`)

**Step 1: Create `src/coro.c`**

Copy the coroutine section verbatim from `src/io_uv.c` (lines 693–1023 of io_uv.c,
the `coro_save_stacks` through the `#endif /* _WIN32 */` block), plus `prim_make_parameter`.
Add a `coro_init` function and a `coro_prims_register` function.

```c
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* coro.c — Coroutines (Windows Fibers / POSIX ucontext) + make-parameter.
 * Extracted from io_uv.c; no libuv dependency. */

#include "picolisp.h"
#include "coro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef CORO_POOL_SIZE
#define CORO_POOL_SIZE 4096
#endif

Coro     g_coros[CORO_POOL_SIZE];
uint32_t g_coro_count = 0;

/* g_cur_coro: index of currently running coro, UINT32_MAX = main thread */
static uint32_t g_cur_coro = UINT32_MAX;

void coro_init(void) {
    memset(g_coros, 0, sizeof(g_coros));
    g_coro_count = 0;
    g_cur_coro   = UINT32_MAX;
}

/* ---------- coro_save_stacks / coro_restore_stacks / gc_mark_coros ----------
 * Copy verbatim from io_uv.c lines 703-783. */

/* [paste coro_save_stacks here] */
/* [paste coro_restore_stacks here] */
/* [paste gc_mark_coros here] */
/* [paste prim_co_alivep here] */

/* ---------- Platform: Windows Fibers --------------------------------------- */
#ifdef _WIN32
#include <windows.h>
static LPVOID g_main_fiber = NULL;

/* [paste coro_caller_fiber, coro_fiber_fn, prim_co, prim_yield,
      prim_co_resume verbatim from io_uv.c Windows section] */

/* ---------- Platform: POSIX ucontext --------------------------------------- */
#else
#include <ucontext.h>
#define CORO_STACK_SIZE (512 * 1024)
typedef struct { ucontext_t ctx; void *stack; } PosixCoroCtx;
static ucontext_t g_main_ctx;

static ucontext_t *get_ucontext(uint32_t idx) {
    if (idx == UINT32_MAX) return &g_main_ctx;
    return &((PosixCoroCtx *)g_coros[idx].fiber)->ctx;
}

/* [paste coro_entry, prim_co, prim_yield, prim_co_resume verbatim
      from io_uv.c POSIX section] */
#endif /* _WIN32 */

/* ---------- make-parameter ------------------------------------------------- */
static Val prim_make_parameter(Val args, Val env) {
    (void)env;
    Val init = IS_CONS(args) ? CAR(args) : NIL_VAL;
    uint32_t tag_idx = sym_intern("*parameter*", 11);
    PUSH_ROOT(init);
    Val param = pl_cons(MAKE_SYM(tag_idx), init);
    POP_ROOT();
    return param;
}

/* ---------- Registration --------------------------------------------------- */
void coro_prims_register(void) {
    prim_register("co",             prim_co,             1, -1);
    prim_register("yield",          prim_yield,          1,  1);
    prim_register("co-resume",      prim_co_resume,      2,  2);
    prim_register("co-alive?",      prim_co_alivep,      1,  1);
    prim_register("make-parameter", prim_make_parameter, 1,  1);
}
```

**Step 2: Update `src/picolisp.h`**

Remove the `#ifdef HAVE_UV` / `#endif` guards around lines 370–397 (the Coro struct
and extern declarations). The Coro struct, `g_coros`, `g_coro_count` must always
be visible. Also remove the `#ifdef HAVE_UV` block around `gc_mark_coros` and
`uv_io_init` at lines 487–491. Replace with:

```c
/* --- coro.c --------------------------------------------------------------- */
extern Coro     g_coros[];
extern uint32_t g_coro_count;
void coro_init(void);
void gc_mark_coros(void);
```

Remove the HAVE_UV guard around `mark_pipe`/`gc_sweep_pipes`/`pipe_prims_register`
at lines 493–498 — these will be always-compiled too.

**Step 3: Update `src/heap.c`**

Remove `#ifdef HAVE_UV` / `#endif` around the `gc_mark_coros()` call (lines 193–195).
Remove `#ifdef HAVE_UV` / `#endif` around the `mark_pipe(PIPE_IDX(cur))` call (lines 152–154).

**Step 4: Update `src/main.c`**

Replace:
```c
#ifdef HAVE_UV
    uv_io_init();
    extern void uv_prims_register(void);
    uv_prims_register();
    extern void pipe_prims_register(void);
    pipe_prims_register();
#endif
#if defined(HAVE_FFI) || defined(_WIN32)
    extern void ffi_prims_register(void);
    ffi_prims_register();
#endif
#ifdef HAVE_SDL
    extern void sdl_prims_register(void);
    sdl_prims_register();
#endif
```

With:
```c
#include "coro.h"
#include "native_io.h"
#include "native_gfx.h"

    coro_init();
    coro_prims_register();
    io_init();
    io_prims_register();
    extern void pipe_prims_register(void);
    pipe_prims_register();
    extern void ffi_prims_register(void);
    ffi_prims_register();
    gfx_prims_register();
```

**Step 5: Verify build still works (io_uv.c and sdl_gfx.c still present, Makefile unchanged)**

```bash
make 2>&1 | tail -5
# Expected: build/picolisp linked successfully (io_uv.c and sdl_gfx.c
# will have duplicate symbol errors until Makefile is updated — that is fine;
# we will fix in Task 8. For now just confirm coro.c compiles cleanly.)
gcc -std=c17 -c -o build/coro.o src/coro.c -Isrc
# Expected: no errors
```

**Step 6: Run tests**
```bash
bash run_tests.sh
# Expected: 34 passed, 0 failed
```

**Step 7: Commit**
```bash
git add src/coro.c src/coro.h src/native_io.h src/native_gfx.h src/ttf.h \
        src/picolisp.h src/heap.c src/main.c
git commit -m "refactor: extract coroutines into coro.c, add native header stubs"
```

---

## Task 2: Write `src/ttf.c` — TTF parser + scanline rasterizer

**Files:**
- Create: `src/ttf.c`
- Reference: `src/ttf.h` (created in Task 1)

The font used is JetBrainsMono-Regular.ttf (deps/fonts/).
Only ASCII 32–127 is required. Composite glyphs can be skipped (JetBrainsMono has none in ASCII range).

**Step 1: Data structures**

```c
/* ttf.c — Minimal TrueType parser + scanline rasterizer. ASCII 32-127 only. */
#include "ttf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Big-endian readers -------------------------------------------------- */
static uint8_t  r8 (const uint8_t *p)             { return p[0]; }
static uint16_t r16(const uint8_t *p)             { return (uint16_t)((p[0]<<8)|p[1]); }
static int16_t  r16s(const uint8_t *p)            { return (int16_t)r16(p); }
static uint32_t r32(const uint8_t *p)             { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }

/* ---- Table directory ----------------------------------------------------- */
static const uint8_t *find_table(const uint8_t *data, size_t len,
                                  const char tag[4], uint32_t *out_len) {
    if (len < 12) return NULL;
    uint16_t ntables = r16(data + 4);
    for (uint16_t i = 0; i < ntables; i++) {
        const uint8_t *rec = data + 12 + i * 16;
        if (rec + 16 > data + len) break;
        if (memcmp(rec, tag, 4) == 0) {
            uint32_t offset = r32(rec + 8);
            uint32_t tlen   = r32(rec + 12);
            if (out_len) *out_len = tlen;
            return data + offset;
        }
    }
    return NULL;
}

/* ---- TtfFont struct ------------------------------------------------------- */
struct TtfFont {
    uint8_t  *data;
    size_t    data_len;
    int       units_per_em;
    int       ascent, descent, line_gap;
    int       num_glyphs;
    int       index_to_loc_format;   /* 0 = short offsets, 1 = long */
    int       num_h_metrics;

    /* cmap format-4 segments */
    int        seg_count;
    uint16_t  *seg_end;     /* [seg_count] */
    uint16_t  *seg_start;   /* [seg_count] */
    int16_t   *seg_delta;   /* [seg_count] */
    uint16_t  *seg_range;   /* [seg_count] range_offset */
    uint16_t  *glyph_ids;   /* glyph id array following segments */
    int        glyph_id_count;

    /* glyph offsets from loca */
    uint32_t  *glyph_off;   /* [num_glyphs + 1] */

    /* hmtx: advance widths and lsbs in font units */
    uint16_t  *adv;         /* [num_h_metrics] */
    int16_t   *lsb;         /* [num_h_metrics] */

    /* pointers into data for glyf table */
    const uint8_t *glyf;
    size_t         glyf_len;
};
```

**Step 2: `ttf_load`**

```c
TtfFont *ttf_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, (size_t)sz, f);
    fclose(f);

    TtfFont *font = calloc(1, sizeof(TtfFont));
    font->data = data; font->data_len = (size_t)sz;

    /* head */
    const uint8_t *head = find_table(data, (size_t)sz, "head", NULL);
    if (!head) { ttf_free(font); return NULL; }
    font->units_per_em       = r16(head + 18);
    font->index_to_loc_format = r16s(head + 50);

    /* hhea */
    const uint8_t *hhea = find_table(data, (size_t)sz, "hhea", NULL);
    font->ascent       = r16s(hhea + 4);
    font->descent      = r16s(hhea + 6);
    font->line_gap     = r16s(hhea + 8);
    font->num_h_metrics = r16(hhea + 34);

    /* maxp */
    const uint8_t *maxp = find_table(data, (size_t)sz, "maxp", NULL);
    font->num_glyphs = r16(maxp + 4);

    /* glyf */
    uint32_t glyf_len;
    font->glyf = find_table(data, (size_t)sz, "glyf", &glyf_len);
    font->glyf_len = glyf_len;

    /* loca */
    const uint8_t *loca = find_table(data, (size_t)sz, "loca", NULL);
    font->glyph_off = malloc((size_t)(font->num_glyphs + 1) * sizeof(uint32_t));
    for (int i = 0; i <= font->num_glyphs; i++) {
        if (font->index_to_loc_format == 0)
            font->glyph_off[i] = (uint32_t)r16(loca + i * 2) * 2;
        else
            font->glyph_off[i] = r32(loca + i * 4);
    }

    /* hmtx */
    const uint8_t *hmtx = find_table(data, (size_t)sz, "hmtx", NULL);
    font->adv = malloc((size_t)font->num_h_metrics * sizeof(uint16_t));
    font->lsb = malloc((size_t)font->num_h_metrics * sizeof(int16_t));
    for (int i = 0; i < font->num_h_metrics; i++) {
        font->adv[i] = r16(hmtx + i * 4);
        font->lsb[i] = r16s(hmtx + i * 4 + 2);
    }

    /* cmap: find format 4 (BMP Unicode) */
    const uint8_t *cmap = find_table(data, (size_t)sz, "cmap", NULL);
    uint16_t num_subtables = r16(cmap + 2);
    const uint8_t *fmt4 = NULL;
    for (uint16_t i = 0; i < num_subtables; i++) {
        const uint8_t *rec = cmap + 4 + i * 8;
        uint16_t plat = r16(rec), enc = r16(rec+2);
        uint32_t off  = r32(rec+4);
        if ((plat == 0) || (plat == 3 && enc == 1)) {
            if (r16(cmap + off) == 4) { fmt4 = cmap + off; break; }
        }
    }
    if (!fmt4) { ttf_free(font); return NULL; }
    uint16_t seg2 = r16(fmt4 + 6);
    font->seg_count = seg2 / 2;
    font->seg_end   = malloc((size_t)font->seg_count * sizeof(uint16_t));
    font->seg_start = malloc((size_t)font->seg_count * sizeof(uint16_t));
    font->seg_delta = malloc((size_t)font->seg_count * sizeof(int16_t));
    font->seg_range = malloc((size_t)font->seg_count * sizeof(uint16_t));
    const uint8_t *p = fmt4 + 14;
    for (int i = 0; i < font->seg_count; i++) font->seg_end[i]   = r16(p + i*2);
    p += font->seg_count * 2 + 2; /* skip reservedPad */
    for (int i = 0; i < font->seg_count; i++) font->seg_start[i] = r16(p + i*2);
    p += font->seg_count * 2;
    for (int i = 0; i < font->seg_count; i++) font->seg_delta[i] = r16s(p + i*2);
    p += font->seg_count * 2;
    const uint8_t *range_base = p;
    for (int i = 0; i < font->seg_count; i++) font->seg_range[i] = r16(p + i*2);
    p += font->seg_count * 2;
    /* remaining bytes in cmap subtable are the glyph id array */
    uint16_t total_len = r16(fmt4 + 2);
    int glyph_id_start_off = (int)(p - fmt4);
    font->glyph_id_count = ((int)total_len - glyph_id_start_off) / 2;
    if (font->glyph_id_count < 0) font->glyph_id_count = 0;
    font->glyph_ids = NULL;
    if (font->glyph_id_count > 0) {
        font->glyph_ids = malloc((size_t)font->glyph_id_count * sizeof(uint16_t));
        for (int i = 0; i < font->glyph_id_count; i++)
            font->glyph_ids[i] = r16(p + i * 2);
    }
    (void)range_base;

    return font;
}

void ttf_free(TtfFont *f) {
    if (!f) return;
    free(f->data); free(f->glyph_off); free(f->adv); free(f->lsb);
    free(f->seg_end); free(f->seg_start); free(f->seg_delta);
    free(f->seg_range); free(f->glyph_ids);
    free(f);
}
```

**Step 3: Codepoint → glyph index (cmap format 4)**

```c
static uint16_t cp_to_glyph(TtfFont *f, uint32_t cp) {
    if (cp > 0xFFFF) return 0;
    uint16_t c = (uint16_t)cp;
    for (int i = 0; i < f->seg_count; i++) {
        if (c > f->seg_end[i]) continue;
        if (c < f->seg_start[i]) return 0;
        if (f->seg_range[i] == 0)
            return (uint16_t)(c + f->seg_delta[i]);
        /* rangeOffset != 0: index into glyph_ids array */
        int idx = (f->seg_range[i] / 2) + (c - f->seg_start[i])
                  - (f->seg_count - i);
        if (idx < 0 || idx >= f->glyph_id_count) return 0;
        uint16_t gid = f->glyph_ids[idx];
        return gid ? (uint16_t)(gid + f->seg_delta[i]) : 0;
    }
    return 0;
}
```

**Step 4: Glyph outline extraction**

```c
typedef struct { float x, y; int on_curve; } Pt;

/* Decode one simple TrueType glyph into a flat array of Pt.
 * Returns number of points; *contour_ends is malloc'd array of ncontours ints. */
static int decode_glyph(TtfFont *f, uint16_t glyph_idx, float scale,
                         Pt **pts_out, int **ends_out, int *ncontours_out) {
    if (glyph_idx >= (uint16_t)f->num_glyphs) return 0;
    uint32_t off = f->glyph_off[glyph_idx];
    uint32_t end = f->glyph_off[glyph_idx + 1];
    if (off == end) { /* empty glyph (space) */
        *pts_out = NULL; *ends_out = NULL; *ncontours_out = 0; return 0;
    }
    const uint8_t *g = f->glyf + off;
    int16_t ncontours = r16s(g);
    if (ncontours < 0) { /* composite — skip */
        *pts_out = NULL; *ends_out = NULL; *ncontours_out = 0; return 0;
    }
    *ncontours_out = ncontours;
    int *ends = malloc((size_t)ncontours * sizeof(int));
    for (int i = 0; i < ncontours; i++) ends[i] = r16(g + 10 + i * 2);
    int npts = ends[ncontours - 1] + 1;
    uint16_t instructions_len = r16(g + 10 + ncontours * 2);
    const uint8_t *fp = g + 10 + ncontours * 2 + 2 + instructions_len;

    /* Decode flags */
    uint8_t *flags = malloc((size_t)npts);
    for (int i = 0; i < npts; ) {
        uint8_t fl = *fp++;
        flags[i++] = fl;
        if (fl & 0x08) { /* repeat */
            int rep = *fp++;
            while (rep-- && i < npts) flags[i++] = fl;
        }
    }

    /* Decode x coordinates */
    float *xs = malloc((size_t)npts * sizeof(float));
    int cx = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x02) { /* x is 1 byte */
            int dx = *fp++;
            cx += (fl & 0x10) ? dx : -dx;
        } else if (!(fl & 0x10)) { /* x is 2 bytes */
            cx += r16s(fp); fp += 2;
        } /* else same as prev */
        xs[i] = (float)cx * scale;
    }
    /* Decode y coordinates */
    Pt *pts = malloc((size_t)npts * sizeof(Pt));
    int cy = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x04) {
            int dy = *fp++;
            cy += (fl & 0x20) ? dy : -dy;
        } else if (!(fl & 0x20)) {
            cy += r16s(fp); fp += 2;
        }
        pts[i].x = xs[i]; pts[i].y = (float)cy * scale;
        pts[i].on_curve = (fl & 0x01);
    }
    free(flags); free(xs);
    *pts_out = pts; *ends_out = ends;
    return npts;
}
```

**Step 5: Scanline rasterizer**

```c
typedef struct { float x0,y0,x1,y1; } Edge;

/* Flatten quadratic bezier (p0 on-curve, p1 off-curve, p2 on-curve) */
static void flatten_bezier(Edge **edges, int *ne, int *cap,
                            float x0,float y0, float x1,float y1,
                            float x2,float y2, int depth) {
    if (depth > 6) {
        /* Add line segment p0->p2 */
        if (*ne >= *cap) { *cap *= 2; *edges = realloc(*edges, (size_t)*cap*sizeof(Edge)); }
        (*edges)[(*ne)++] = (Edge){x0,y0,x2,y2};
        return;
    }
    /* Midpoints */
    float mx0=(x0+x1)/2, my0=(y0+y1)/2;
    float mx1=(x1+x2)/2, my1=(y1+y2)/2;
    float mx=(mx0+mx1)/2, my=(my0+my1)/2;
    flatten_bezier(edges,ne,cap, x0,y0,mx0,my0,mx,my, depth+1);
    flatten_bezier(edges,ne,cap, mx,my,mx1,my1,x2,y2, depth+1);
}

/* Convert glyph outline to edge list */
static Edge *outline_to_edges(Pt *pts, int npts, int *ends, int ncontours,
                               int *out_ne) {
    int cap = 256, ne = 0;
    Edge *edges = malloc((size_t)cap * sizeof(Edge));
    int start = 0;
    for (int c = 0; c < ncontours; c++) {
        int end = ends[c];
        int count = end - start + 1;
        for (int i = 0; i < count; i++) {
            int i0 = start + i;
            int i1 = start + (i + 1) % count;
            Pt p0 = pts[i0], p1 = pts[i1];
            if (p0.on_curve && p1.on_curve) {
                if (ne >= cap) { cap*=2; edges=realloc(edges,(size_t)cap*sizeof(Edge)); }
                edges[ne++] = (Edge){p0.x,p0.y,p1.x,p1.y};
            } else if (p0.on_curve && !p1.on_curve) {
                Pt p2 = pts[start + (i + 2) % count];
                if (!p2.on_curve) { /* implied on-curve midpoint */
                    Pt mid = {(p1.x+p2.x)/2, (p1.y+p2.y)/2, 1};
                    flatten_bezier(&edges,&ne,&cap, p0.x,p0.y,p1.x,p1.y,mid.x,mid.y,0);
                    /* continue from mid next iter — handled by incrementing i inside loop */
                } else {
                    flatten_bezier(&edges,&ne,&cap, p0.x,p0.y,p1.x,p1.y,p2.x,p2.y,0);
                    i++; /* consumed p2 */
                }
            }
            /* off-curve p0 handled when prev iteration set p1 */
        }
        start = end + 1;
    }
    *out_ne = ne;
    return edges;
}

/* Scanline fill: output 8-bit alpha bitmap */
static uint8_t *scanfill(Edge *edges, int ne, int w, int h, float ox, float oy) {
    uint8_t *bmp = calloc((size_t)w * (size_t)h, 1);
    for (int y = 0; y < h; y++) {
        float fy = (float)y + oy + 0.5f;
        /* Collect x intersections */
        float xs[512]; int nx = 0;
        for (int i = 0; i < ne && nx < 511; i++) {
            float y0=edges[i].y0, y1=edges[i].y1;
            if ((y0 <= fy && y1 > fy) || (y1 <= fy && y0 > fy)) {
                float t = (fy - y0) / (y1 - y0);
                xs[nx++] = edges[i].x0 + t * (edges[i].x1 - edges[i].x0) - ox;
            }
        }
        /* Sort xs */
        for (int a=0;a<nx-1;a++) for(int b=a+1;b<nx;b++)
            if(xs[b]<xs[a]){float tmp=xs[a];xs[a]=xs[b];xs[b]=tmp;}
        /* Fill between pairs */
        for (int i = 0; i+1 < nx; i += 2) {
            int x0 = (int)floorf(xs[i]), x1 = (int)ceilf(xs[i+1]);
            if (x0 < 0) x0 = 0; if (x1 > w) x1 = w;
            for (int x = x0; x < x1; x++) bmp[y*w+x] = 255;
        }
    }
    return bmp;
}
```

**Step 6: Public API**

```c
uint8_t *ttf_render_glyph(TtfFont *f, uint32_t cp, int px,
                           int *out_w, int *out_h, int *xoff, int *yoff) {
    uint16_t gid = cp_to_glyph(f, cp);
    float scale = (float)px / (float)f->ascent;

    Pt *pts = NULL; int *ends = NULL; int ncontours = 0;
    int npts = decode_glyph(f, gid, scale, &pts, &ends, &ncontours);
    if (npts == 0) {
        /* Space or missing — return 1x1 transparent bitmap */
        *out_w = 1; *out_h = 1; *xoff = 0; *yoff = 0;
        free(pts); free(ends);
        return calloc(1, 1);
    }

    /* Bounding box */
    float minx=pts[0].x,miny=pts[0].y,maxx=minx,maxy=miny;
    for (int i=1;i<npts;i++) {
        if(pts[i].x<minx)minx=pts[i].x; if(pts[i].x>maxx)maxx=pts[i].x;
        if(pts[i].y<miny)miny=pts[i].y; if(pts[i].y>maxy)maxy=pts[i].y;
    }
    int w = (int)ceilf(maxx - minx) + 2;
    int h = (int)ceilf(maxy - miny) + 2;
    if (w < 1) w = 1; if (h < 1) h = 1;
    *out_w = w; *out_h = h;
    *xoff = (int)floorf(minx);
    *yoff = (int)floorf(miny);

    int ne = 0;
    Edge *edges = outline_to_edges(pts, npts, ends, ncontours, &ne);
    free(pts); free(ends);

    uint8_t *bmp = scanfill(edges, ne, w, h, minx, miny);
    free(edges);
    return bmp;
}

int ttf_glyph_advance(TtfFont *f, uint32_t cp, int px) {
    uint16_t gid = cp_to_glyph(f, cp);
    float scale = (float)px / (float)f->ascent;
    uint16_t adv_units = (gid < (uint16_t)f->num_h_metrics)
                         ? f->adv[gid] : f->adv[f->num_h_metrics - 1];
    return (int)roundf((float)adv_units * scale);
}

void ttf_line_metrics(TtfFont *f, int px, int *asc, int *desc, int *gap) {
    float scale = (float)px / (float)f->ascent;
    if (asc)  *asc  = (int)roundf((float)f->ascent   * scale);
    if (desc) *desc = (int)roundf((float)f->descent   * scale);
    if (gap)  *gap  = (int)roundf((float)f->line_gap  * scale);
}
```

**Step 7: Compile and test**
```bash
gcc -std=c17 -O2 -Wall -c -o build/ttf.o src/ttf.c -lm
# Expected: no errors or warnings
```

**Step 8: Commit**
```bash
git add src/ttf.c src/ttf.h
git commit -m "feat: minimal TrueType parser and scanline rasterizer (ASCII 32-127)"
```

---

## Task 3: Write `src/native_io_posix.c`

**Files:**
- Create: `src/native_io_posix.c`
- Create: `src/pipe_posix.c` (replaces pipe_proc.c POSIX section)
- Modify: `src/pipe_proc.c` (add non-HAVE_UV path or replace)

**Step 1: Data structures**

```c
/* native_io_posix.c — poll(2)-based event loop; replaces libuv for POSIX. */
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
#include <pthread.h>

/* ---- Timer table --------------------------------------------------------- */
#define MAX_TIMERS 256
typedef struct { uint64_t expiry_ms; Val callback; int active; } Timer;
static Timer g_timers[MAX_TIMERS];
static int   g_ntimers = 0;

/* ---- fd table ------------------------------------------------------------ */
#define MAX_FDS 256
typedef enum { FD_TCP_CLIENT, FD_TCP_SERVER, FD_HTTP_CLIENT } FdKind;
typedef struct {
    int     fd;
    FdKind  kind;
    Val     callback;
    int     active;
    /* HTTP server accumulation */
    char   *rbuf;
    size_t  rused, rcap;
    Val     handler;
} FdSlot;
static FdSlot g_fds[MAX_FDS];
static int    g_nfds = 0;
```

**Step 2: Helper — millisecond clock**

```c
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
```

**Step 3: `prim_timer`**

```c
static Val prim_timer(Val args, Val env) {
    (void)env;
    int ms = INT_VAL(CAR(args));
    Val cb = CADR(args);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].expiry_ms = now_ms() + (uint64_t)ms;
            g_timers[i].callback  = cb;
            g_timers[i].active    = 1;
            if (i >= g_ntimers) g_ntimers = i + 1;
            return NIL_VAL;
        }
    }
    pl_error_str("timer: too many active timers");
    return NIL_VAL;
}
```

**Step 4: `prim_event_loop` (poll-based)**

```c
static void fire_expired_timers(void) {
    uint64_t now = now_ms();
    for (int i = 0; i < g_ntimers; i++) {
        if (g_timers[i].active && now >= g_timers[i].expiry_ms) {
            g_timers[i].active = 0;
            pl_apply(g_timers[i].callback, NIL_VAL, NIL_VAL);
        }
    }
}

static int compute_poll_timeout(void) {
    uint64_t now = now_ms();
    int timeout = -1; /* infinite */
    for (int i = 0; i < g_ntimers; i++) {
        if (!g_timers[i].active) continue;
        int64_t delta = (int64_t)(g_timers[i].expiry_ms - now);
        if (delta < 0) delta = 0;
        if (timeout < 0 || (int)delta < timeout) timeout = (int)delta;
    }
    return timeout;
}

static int has_active_handles(void) {
    for (int i = 0; i < g_ntimers; i++)
        if (g_timers[i].active) return 1;
    for (int i = 0; i < g_nfds; i++)
        if (g_fds[i].active) return 1;
    return 0;
}

static Val prim_event_loop(Val args, Val env) {
    (void)args; (void)env;
    while (has_active_handles()) {
        struct pollfd pfds[MAX_FDS]; int nfds = 0;
        for (int i = 0; i < g_nfds; i++) {
            if (!g_fds[i].active) continue;
            pfds[nfds].fd      = g_fds[i].fd;
            pfds[nfds].events  = (g_fds[i].kind == FD_TCP_SERVER ||
                                   g_fds[i].kind == FD_HTTP_CLIENT)
                                  ? POLLIN : POLLOUT;
            pfds[nfds].revents = 0;
            nfds++;
        }
        int timeout = compute_poll_timeout();
        int r = poll(pfds, (nfds_t)nfds, timeout);
        if (r < 0 && errno != EINTR) break;
        fire_expired_timers();
        /* Handle ready fds (connect callbacks, accept, read) — see below */
        /* ... dispatch per g_fds[i].kind ... */
    }
    return NIL_VAL;
}
```

**Step 5: TCP primitives**

Implement `prim_tcp_connect`, `prim_tcp_listen`, `prim_tcp_write`, `prim_tcp_close`
using non-blocking `connect(2)`, `accept(2)`, `send(2)`, `recv(2)`.
Set sockets non-blocking with `fcntl(fd, F_SETFL, O_NONBLOCK)`.
Register fd in `g_fds` table with the Lisp callback.

**Step 6: HTTP client (async via pthread)**

Copy `http_sync_request` and `parse_http_url` verbatim from `src/io_uv.c`
(they have no libuv dependency). Wrap in a pthread worker the same way as
the libuv `uv_queue_work` pattern:

```c
typedef struct {
    char *method, *host, *path, *req_body;
    int port; size_t req_blen; Val callback;
    char *resp_body; size_t resp_blen; int resp_status; int failed;
} HttpWork;

static void *http_thread(void *arg) {
    HttpWork *w = arg;
    w->resp_body = http_sync_request(w->method, w->host, w->port, w->path,
                                      w->req_body, w->req_blen,
                                      &w->resp_status, &w->resp_blen);
    w->failed = !w->resp_body;
    /* Post result back: write a byte to a self-pipe so poll wakes up */
    /* ... self-pipe trick or call callback directly if single-threaded ... */
    return NULL;
}
```

For simplicity, for HTTP GET/POST with no event loop running, call
`http_sync_request` synchronously (same as the existing `!g_loop` fallback).

**Step 7: HTTP server**

Implement `prim_http_serve`: bind/listen on port, register server fd in fd table.
In the poll loop, when POLLIN fires on a server fd, `accept` the client,
read request, call Lisp handler, write response, close.

**Step 8: File I/O**

Copy `prim_file_read` and `prim_file_write` verbatim from `src/io_uv.c`
(lines 197–258 for HAVE_UV path — they use only `fopen`/`fread`/`fwrite`).

**Step 9: `pipe_posix.c`**

Replace libuv process spawning with `posix_spawn` / `fork+exec`:

```c
/* pipe_posix.c — subprocess I/O via POSIX fork/exec */
#include "picolisp.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#define PIPE_POOL_SIZE 1024
typedef struct {
    pid_t  pid;
    int    stdin_fd;   /* write end of stdin pipe  */
    int    stdout_fd;  /* read end of stdout pipe  */
    int    alive;
    int    exit_code;
    uint8_t mark;
    /* read buffer */
    char  *rbuf; size_t rlen, rcap;
} PipeSlot;

static PipeSlot g_pipes[PIPE_POOL_SIZE];
static int      g_pipe_count = 0;

/* (spawn cmd arg...) */
static Val prim_spawn(Val args, Val env) {
    (void)env;
    /* Build argv from args list */
    /* pipe(): stdin_pipe[0]=read stdin_pipe[1]=write (parent writes to [1]) */
    /* fork(), dup2 fds, execvp */
    /* Store pid, fd in g_pipes, return MAKE_PIPE(idx) */
    int stdin_pipe[2], stdout_pipe[2];
    pipe(stdin_pipe); pipe(stdout_pipe);
    /* Build argv */
    int argc = 0; Val a = args;
    while (!IS_NIL(a)) { argc++; a = CDR(a); }
    char **argv = malloc((size_t)(argc+1)*sizeof(char*));
    a = args;
    for (int i=0;i<argc;i++,a=CDR(a)) argv[i]=(char*)str_ptr(STR_IDX(CAR(a)));
    argv[argc] = NULL;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(stdin_pipe[0],  STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[1]); close(stdout_pipe[0]);
        execvp(argv[0], argv);
        _exit(127);
    }
    free(argv);
    close(stdin_pipe[0]); close(stdout_pipe[1]);
    int idx = g_pipe_count++;
    g_pipes[idx].pid       = pid;
    g_pipes[idx].stdin_fd  = stdin_pipe[1];
    g_pipes[idx].stdout_fd = stdout_pipe[0];
    g_pipes[idx].alive     = 1;
    g_pipes[idx].exit_code = 0;
    return MAKE_PIPE((uint32_t)idx);
}
```

Implement `pipe-read-all`, `pipe-write`, `pipe-close-stdin`, `pipe-close`,
`pipe-alive?` using `read(2)`/`write(2)`/`waitpid(2)`.

**Step 10: Registration**

```c
void io_init(void) {
    memset(g_timers, 0, sizeof(g_timers));
    memset(g_fds,    0, sizeof(g_fds));
    g_ntimers = g_nfds = 0;
}

void io_prims_register(void) {
    prim_register("tcp-connect",  prim_tcp_connect,  3, 3);
    prim_register("tcp-listen",   prim_tcp_listen,   2, 2);
    prim_register("tcp-write",    prim_tcp_write,    2, 2);
    prim_register("tcp-close",    prim_tcp_close,    1, 1);
    prim_register("timer",        prim_timer,        2, 2);
    prim_register("file-read",    prim_file_read,    2, 2);
    prim_register("file-write",   prim_file_write,   3, 3);
    prim_register("event-loop",   prim_event_loop,   0, 0);
    prim_register("uv-run",       prim_event_loop,   0, 0);
    prim_register("http-get",     prim_http_get,     2, 2);
    prim_register("http-post",    prim_http_post,    3, 3);
    prim_register("http-serve",   prim_http_serve,   2, 2);
}

void pipe_prims_register(void) {
    prim_register("spawn",            prim_spawn,            1, -1);
    prim_register("pipe-read-all",    prim_pipe_read_all,    1,  1);
    prim_register("pipe-write",       prim_pipe_write,       2,  2);
    prim_register("pipe-close-stdin", prim_pipe_close_stdin, 1,  1);
    prim_register("pipe-close",       prim_pipe_close,       1,  1);
    prim_register("pipe-alive?",      prim_pipe_alivep,      1,  1);
}
```

**Step 11: Verify compile**
```bash
gcc -std=c17 -O2 -Wall -c -o build/native_io_posix.o src/native_io_posix.c
gcc -std=c17 -O2 -Wall -c -o build/pipe_posix.o src/pipe_posix.c
# Expected: no errors
```

**Step 12: Commit**
```bash
git add src/native_io_posix.c src/pipe_posix.c
git commit -m "feat: poll-based event loop and POSIX process spawning"
```

---

## Task 4: Write `src/native_io_win32.c`

**Files:**
- Create: `src/native_io_win32.c`
- Create: `src/pipe_win32.c`

Same structure as Task 3, but using Winsock2:

```c
/* native_io_win32.c — WSAPoll-based event loop for Windows */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>   /* _beginthreadex */
#include "picolisp.h"
#include "native_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
```

Key differences from POSIX:
- `WSAStartup(MAKEWORD(2,2), &wsa_data)` in `io_init()`
- `WSAPoll` instead of `poll`
- `SOCKET` type instead of `int` fd
- `ioctlsocket(sock, FIONBIO, &mode)` for non-blocking
- `closesocket` instead of `close`
- `GetTickCount64()` instead of `clock_gettime` for timers
- `_beginthreadex` instead of `pthread_create` for HTTP worker threads

**`pipe_win32.c`** uses `CreateProcess` with `STARTUPINFO` and inheritable pipe handles:

```c
/* pipe_win32.c — subprocess I/O via CreateProcess */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "picolisp.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    HANDLE proc;
    HANDLE stdin_wr;   /* parent writes here */
    HANDLE stdout_rd;  /* parent reads here  */
    int    alive;
    DWORD  exit_code;
    uint8_t mark;
    char  *rbuf; size_t rlen, rcap;
} PipeSlot;

static PipeSlot g_pipes[1024];
static int      g_pipe_count = 0;

static Val prim_spawn(Val args, Val env) {
    (void)env;
    /* Build command line string from args */
    /* CreatePipe for stdin and stdout */
    /* SetHandleInformation to make parent ends non-inheritable */
    /* CreateProcess with STARTUPINFO.hStdInput/Output set */
    /* Store process HANDLE, pipe HANDLEs, return MAKE_PIPE(idx) */
    HANDLE stdin_rd, stdin_wr, stdout_rd, stdout_wr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    CreatePipe(&stdin_rd,  &stdin_wr,  &sa, 0);
    CreatePipe(&stdout_rd, &stdout_wr, &sa, 0);
    SetHandleInformation(stdin_wr,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    /* Build cmdline from args list */
    char cmdline[4096] = {0};
    Val a = args;
    while (!IS_NIL(a)) {
        strncat(cmdline, str_ptr(STR_IDX(CAR(a))), sizeof(cmdline)-strlen(cmdline)-2);
        if (!IS_NIL(CDR(a))) strcat(cmdline, " ");
        a = CDR(a);
    }
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = stdin_rd;
    si.hStdOutput  = stdout_wr;
    si.hStdError   = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        pl_error_str("spawn: CreateProcess failed");
    CloseHandle(stdin_rd); CloseHandle(stdout_wr);
    CloseHandle(pi.hThread);
    int idx = g_pipe_count++;
    g_pipes[idx].proc      = pi.hProcess;
    g_pipes[idx].stdin_wr  = stdin_wr;
    g_pipes[idx].stdout_rd = stdout_rd;
    g_pipes[idx].alive     = 1;
    return MAKE_PIPE((uint32_t)idx);
}
```

Implement remaining pipe primitives using `ReadFile`/`WriteFile`/`CloseHandle`/
`GetExitCodeProcess`/`WaitForSingleObject`.

`mark_pipe`, `gc_sweep_pipes`, `pipe_prims_register` are defined in `pipe_win32.c`.

**Commit:**
```bash
git add src/native_io_win32.c src/pipe_win32.c
git commit -m "feat: WSAPoll event loop and Win32 process spawning"
```

---

## Task 5: Write `src/native_gfx_x11.c`

**Files:**
- Create: `src/native_gfx_x11.c`

```c
/* native_gfx_x11.c — Xlib + ALSA graphics backend, replaces sdl_gfx.c on Linux */
#include "picolisp.h"
#include "native_gfx.h"
#include "ttf.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

static Display  *g_dpy    = NULL;
static Window    g_win    = 0;
static Pixmap    g_back   = 0;   /* offscreen backing pixmap */
static GC        g_gc     = 0;
static int       g_w      = 0, g_h = 0;
static uint32_t  g_color  = 0;  /* current draw color (XRGB) */
static XImage   *g_ximg   = NULL;
static uint32_t *g_pixels = NULL;  /* for tgl-present; NULL when not needed */
```

**Window creation (`sdl-window`):**
```c
static Val prim_sdl_window(Val args, Val env) {
    (void)env;
    const char *title = str_ptr(STR_IDX(CAR(args)));
    int w = INT_VAL(CADR(args)), h = INT_VAL(CADDR(args));
    if (!g_dpy) { g_dpy = XOpenDisplay(NULL); }
    if (!g_dpy) return NIL_VAL;
    int scr  = DefaultScreen(g_dpy);
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
    XFlush(g_dpy);
    return T_VAL;
}
```

**Color/clear/present:**
```c
static Val prim_sdl_color(Val args, Val env) {
    (void)env;
    int r=INT_VAL(CAR(args)), g=INT_VAL(CADR(args)), b=INT_VAL(CADDR(args));
    g_color = ((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
    XSetForeground(g_dpy, g_gc, g_color);
    return NIL_VAL;
}
static Val prim_sdl_clear(Val args, Val env) {
    (void)args; (void)env;
    XFillRectangle(g_dpy, g_back, g_gc, 0, 0, (unsigned)g_w, (unsigned)g_h);
    return NIL_VAL;
}
static Val prim_sdl_present(Val args, Val env) {
    (void)args; (void)env;
    XCopyArea(g_dpy, g_back, g_win, g_gc, 0, 0, (unsigned)g_w, (unsigned)g_h, 0, 0);
    XFlush(g_dpy);
    return NIL_VAL;
}
```

**Drawing primitives:**
```c
static Val prim_sdl_point(Val args, Val env) {
    (void)env;
    XDrawPoint(g_dpy, g_back, g_gc, INT_VAL(CAR(args)), INT_VAL(CADR(args)));
    return NIL_VAL;
}
static Val prim_sdl_line(Val args, Val env) {
    (void)env;
    XDrawLine(g_dpy, g_back, g_gc, INT_VAL(CAR(args)), INT_VAL(CADR(args)),
              INT_VAL(CADDR(args)), INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}
static Val prim_sdl_rect(Val args, Val env) {
    (void)env;
    XDrawRectangle(g_dpy, g_back, g_gc,
        INT_VAL(CAR(args)), INT_VAL(CADR(args)),
        (unsigned)INT_VAL(CADDR(args)), (unsigned)INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}
static Val prim_sdl_fill(Val args, Val env) {
    (void)env;
    XFillRectangle(g_dpy, g_back, g_gc,
        INT_VAL(CAR(args)), INT_VAL(CADR(args)),
        (unsigned)INT_VAL(CADDR(args)), (unsigned)INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}
static Val prim_sdl_circle(Val args, Val env) {
    (void)env;
    int cx=INT_VAL(CAR(args)),cy=INT_VAL(CADR(args)),r=INT_VAL(CADDR(args));
    XDrawArc(g_dpy,g_back,g_gc,cx-r,cy-r,(unsigned)(2*r),(unsigned)(2*r),0,360*64);
    return NIL_VAL;
}
static Val prim_sdl_fill_circle(Val args, Val env) {
    (void)env;
    int cx=INT_VAL(CAR(args)),cy=INT_VAL(CADR(args)),r=INT_VAL(CADDR(args));
    XFillArc(g_dpy,g_back,g_gc,cx-r,cy-r,(unsigned)(2*r),(unsigned)(2*r),0,360*64);
    return NIL_VAL;
}
```

**Text rendering:**
```c
#define MAX_FONTS 32
static TtfFont *g_fonts[MAX_FONTS];
static int      g_font_sizes[MAX_FONTS];
static int      g_font_count = 0;
static int      g_cur_font = -1;

static Val prim_sdl_font_load(Val args, Val env) {
    (void)env;
    const char *path = str_ptr(STR_IDX(CAR(args)));
    int size = INT_VAL(CADR(args));
    if (g_font_count >= MAX_FONTS) return NIL_VAL;
    TtfFont *f = ttf_load(path);
    if (!f) return NIL_VAL;
    int idx = g_font_count++;
    g_fonts[idx]      = f;
    g_font_sizes[idx] = size;
    g_cur_font        = idx;
    return MAKE_INT(idx);
}

/* Alpha-composite 8-bit alpha glyph bitmap onto the backing pixmap */
static void blit_glyph(int px, int py, uint8_t *bmp, int bw, int bh,
                        uint32_t color) {
    uint8_t fr=(color>>16)&0xFF, fg=(color>>8)&0xFF, fb=color&0xFF;
    for (int y=0; y<bh; y++) {
        for (int x=0; x<bw; x++) {
            uint8_t a = bmp[y*bw+x];
            if (!a) continue;
            /* Read current pixel, alpha-blend, write back */
            XColor xc = {0}; /* XGetPixel on pixmap needs XImage; use XPutPixel */
            uint32_t blended = (((uint32_t)fr*a)>>8)<<16 |
                               (((uint32_t)fg*a)>>8)<<8  |
                               (((uint32_t)fb*a)>>8);
            XSetForeground(g_dpy, g_gc, blended);
            XDrawPoint(g_dpy, g_back, g_gc, px+x, py+y);
            (void)xc;
        }
    }
    XSetForeground(g_dpy, g_gc, g_color);  /* restore current color */
}

static Val prim_sdl_text(Val args, Val env) {
    (void)env;
    int x=INT_VAL(CAR(args)), y=INT_VAL(CADR(args));
    const char *s = str_ptr(STR_IDX(CADDR(args)));
    if (g_cur_font < 0) return NIL_VAL;
    TtfFont *f = g_fonts[g_cur_font];
    int px = g_font_sizes[g_cur_font];
    int asc; ttf_line_metrics(f, px, &asc, NULL, NULL);
    int pen_x = x;
    for (; *s; s++) {
        int w,h,xoff,yoff;
        uint8_t *bmp = ttf_render_glyph(f,(uint32_t)(unsigned char)*s,px,&w,&h,&xoff,&yoff);
        if (bmp) { blit_glyph(pen_x+xoff, y+asc+yoff, bmp, w, h, g_color); free(bmp); }
        pen_x += ttf_glyph_advance(f,(uint32_t)(unsigned char)*s,px);
    }
    return NIL_VAL;
}

static Val prim_sdl_text_size(Val args, Val env) {
    (void)env;
    const char *s = str_ptr(STR_IDX(CAR(args)));
    if (g_cur_font < 0) return NIL_VAL;
    TtfFont *f = g_fonts[g_cur_font]; int px = g_font_sizes[g_cur_font];
    int w=0, asc, desc;
    ttf_line_metrics(f,px,&asc,&desc,NULL);
    for(;*s;s++) w += ttf_glyph_advance(f,(uint32_t)(unsigned char)*s,px);
    PUSH_ROOT(MAKE_INT(w));
    Val r = pl_cons(MAKE_INT(w), pl_cons(MAKE_INT(asc-desc), NIL_VAL));
    POP_ROOT();
    return r;
}
```

**Event polling (`sdl-poll`):**

Map Xlib key events to the same symbol/string format as the old SDL backend.
Return a list `(type . details)` or NIL if no event.

```c
static Val xkey_to_val(KeySym ks) {
    /* Map common keys to symbols matching old SDL key names */
    if (ks == XK_Return    || ks == XK_KP_Enter) return MAKE_STR(str_intern("return",6));
    if (ks == XK_Escape)   return MAKE_STR(str_intern("escape",6));
    if (ks == XK_BackSpace) return MAKE_STR(str_intern("backspace",9));
    if (ks == XK_Tab)       return MAKE_STR(str_intern("tab",3));
    if (ks == XK_Up)        return MAKE_STR(str_intern("up",2));
    if (ks == XK_Down)      return MAKE_STR(str_intern("down",4));
    if (ks == XK_Left)      return MAKE_STR(str_intern("left",4));
    if (ks == XK_Right)     return MAKE_STR(str_intern("right",5));
    if (ks == XK_Delete)    return MAKE_STR(str_intern("delete",6));
    if (ks == XK_Home)      return MAKE_STR(str_intern("home",4));
    if (ks == XK_End)       return MAKE_STR(str_intern("end",3));
    if (ks == XK_Page_Up)   return MAKE_STR(str_intern("pageup",6));
    if (ks == XK_Page_Down) return MAKE_STR(str_intern("pagedown",8));
    if (ks == XK_F1)  return MAKE_STR(str_intern("f1",2));
    /* ... F2-F12 ... */
    /* Printable ASCII */
    if (ks >= 0x20 && ks < 0x7F) {
        char buf[2] = {(char)ks, 0};
        return MAKE_STR(str_intern(buf, 1));
    }
    return NIL_VAL;
}

static Val prim_sdl_poll(Val args, Val env) {
    (void)args; (void)env;
    if (!g_dpy) return NIL_VAL;
    XEvent ev;
    while (XPending(g_dpy)) {
        XNextEvent(g_dpy, &ev);
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            Val kv = xkey_to_val(ks);
            if (!IS_NIL(kv)) {
                PUSH_ROOT(kv);
                Val r = pl_cons(MAKE_SYM(sym_intern("key-down",8)),
                                pl_cons(kv, NIL_VAL));
                POP_ROOT();
                return r;
            }
        }
        if (ev.type == ClientMessage) return MAKE_SYM(sym_intern("quit",4));
    }
    return NIL_VAL;
}
```

**Ticks/delay/quit:**
```c
static Val prim_sdl_ticks(Val args, Val env) {
    (void)args; (void)env;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return MAKE_INT((int)(ts.tv_sec*1000 + ts.tv_nsec/1000000));
}
static Val prim_sdl_delay(Val args, Val env) {
    (void)env;
    int ms = INT_VAL(CAR(args));
    struct timespec ts = {ms/1000, (ms%1000)*1000000L};
    nanosleep(&ts, NULL);
    return NIL_VAL;
}
static Val prim_sdl_quit(Val args, Val env) {
    (void)args; (void)env;
    if (g_gc)  { XFreeGC(g_dpy, g_gc); g_gc = 0; }
    if (g_back){ XFreePixmap(g_dpy, g_back); g_back = 0; }
    if (g_win) { XDestroyWindow(g_dpy, g_win); g_win = 0; }
    if (g_dpy) { XCloseDisplay(g_dpy); g_dpy = NULL; }
    return NIL_VAL;
}
```

**ALSA audio** — implement `prim_audio_init`, `prim_audio_tone`, `prim_audio_drums`,
`prim_audio_pending`, `prim_audio_clear`, `prim_audio_quit`, `prim_sample_load`,
`prim_samples_load_dir`, `prim_sample_play`, `prim_audio_volume`, `prim_audio_stop_all`
using `snd_pcm_open`/`snd_pcm_writei`. The synthesis logic (sine wave, drum synthesis,
sample mixing) is identical to the SDL version — copy the math, replace only the
output calls.

**TinyGL pixel access:**
```c
uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes) {
    if (!g_pixels) g_pixels = calloc((size_t)g_w * (size_t)g_h, 4);
    if (w) *w = g_w; if (h) *h = g_h;
    if (stride_bytes) *stride_bytes = g_w * 4;
    return g_pixels;
}
void gfx_mark_pixels_dirty(void) {
    if (!g_dpy || !g_pixels) return;
    /* Put pixels into backing pixmap via XImage */
    XImage *img = XCreateImage(g_dpy, DefaultVisual(g_dpy, DefaultScreen(g_dpy)),
                               24, ZPixmap, 0, (char*)g_pixels,
                               (unsigned)g_w, (unsigned)g_h, 32, g_w*4);
    XPutImage(g_dpy, g_back, g_gc, img, 0,0,0,0,(unsigned)g_w,(unsigned)g_h);
    img->data = NULL; XDestroyImage(img); /* don't free g_pixels */
}
```

**Registration:**
```c
void gfx_prims_register(void) {
    prim_register("sdl-window",      prim_sdl_window,      3, 3);
    prim_register("sdl-color",       prim_sdl_color,       3, 4);
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
    prim_register("sample-load",     prim_sample_load,     2, 2);
    prim_register("samples-load-dir",prim_samples_load_dir,1, 1);
    prim_register("sample-play",     prim_sample_play,     1, 2);
    prim_register("audio-volume",    prim_audio_volume,    1, 1);
    prim_register("audio-stop-all",  prim_audio_stop_all,  0, 0);
}
```

**Commit:**
```bash
git add src/native_gfx_x11.c
git commit -m "feat: Xlib + ALSA + TTF graphics backend for Linux"
```

---

## Task 6: Write `src/native_gfx_win32.c`

**Files:**
- Create: `src/native_gfx_win32.c`

```c
/* native_gfx_win32.c — Win32 + GDI + WASAPI graphics backend */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "picolisp.h"
#include "native_gfx.h"
#include "ttf.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static HWND      g_hwnd    = NULL;
static HDC       g_hdc     = NULL;   /* window DC */
static HDC       g_memdc   = NULL;   /* offscreen DC */
static HBITMAP   g_bmp     = NULL;   /* offscreen bitmap */
static BITMAPINFO g_bmi    = {0};
static uint32_t *g_pixels  = NULL;   /* DIB section pixels (XRGB8888) */
static int       g_w = 0, g_h = 0;
static COLORREF  g_color   = RGB(255,255,255);
```

**Window procedure:**
```c
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY || msg == WM_CLOSE) { PostQuitMessage(0); return 0; }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        BitBlt(dc, 0,0,g_w,g_h, g_memdc, 0,0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
```

**`sdl-window`:**
```c
static Val prim_sdl_window(Val args, Val env) {
    (void)env;
    const char *title = str_ptr(STR_IDX(CAR(args)));
    g_w = INT_VAL(CADR(args)); g_h = INT_VAL(CADDR(args));
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "LWindow";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    g_hwnd = CreateWindowExA(0, "LWindow", title,
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, g_w, g_h,
                              NULL, NULL, wc.hInstance, NULL);
    g_hdc   = GetDC(g_hwnd);
    g_memdc = CreateCompatibleDC(g_hdc);
    /* Create DIB section for pixel-level access */
    g_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth       = g_w;
    g_bmi.bmiHeader.biHeight      = -g_h;  /* top-down */
    g_bmi.bmiHeader.biPlanes      = 1;
    g_bmi.bmiHeader.biBitCount    = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
    g_bmp = CreateDIBSection(g_memdc, &g_bmi, DIB_RGB_COLORS,
                              (void**)&g_pixels, NULL, 0);
    SelectObject(g_memdc, g_bmp);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return T_VAL;
}
```

**Color/clear/present:**
```c
static Val prim_sdl_color(Val args, Val env) {
    (void)env;
    int r=INT_VAL(CAR(args)),g=INT_VAL(CADR(args)),b=INT_VAL(CADDR(args));
    g_color = RGB(r,g,b);
    return NIL_VAL;
}
static Val prim_sdl_clear(Val args, Val env) {
    (void)args; (void)env;
    HBRUSH br = CreateSolidBrush(g_color);
    RECT rc = {0,0,g_w,g_h};
    FillRect(g_memdc, &rc, br);
    DeleteObject(br);
    return NIL_VAL;
}
static Val prim_sdl_present(Val args, Val env) {
    (void)args; (void)env;
    BitBlt(g_hdc, 0,0,g_w,g_h, g_memdc, 0,0, SRCCOPY);
    return NIL_VAL;
}
```

**Drawing primitives** — use `MoveToEx`/`LineTo`, `Rectangle`, `Ellipse`
with a `HPEN` and `HBRUSH` set to the current color before each call.

**Text rendering** — same as X11: use `ttf_render_glyph` to get alpha bitmap,
alpha-blend pixel-by-pixel into `g_pixels` (since we have direct DIB access).

**Event polling (`sdl-poll`):**
```c
static Val prim_sdl_poll(Val args, Val env) {
    (void)args; (void)env;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
            return MAKE_SYM(sym_intern("quit",4));
        if (msg.message == WM_KEYDOWN) {
            Val kv = vkey_to_val((int)msg.wParam, (int)(msg.lParam >> 16));
            if (!IS_NIL(kv)) {
                PUSH_ROOT(kv);
                Val r = pl_cons(MAKE_SYM(sym_intern("key-down",8)),
                                pl_cons(kv, NIL_VAL));
                POP_ROOT();
                return r;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return NIL_VAL;
}
```

Map VK_ codes to the same key-name strings as the X11 backend:
`VK_RETURN→"return"`, `VK_ESCAPE→"escape"`, `VK_BACK→"backspace"`,
`VK_UP/DOWN/LEFT/RIGHT→"up"/"down"/"left"/"right"`, `VK_F1-F12→"f1"-"f12"`,
printable keys via `ToAscii`.

**WASAPI audio** — `prim_audio_init` opens `IMMDeviceEnumerator → IMMDevice →
IAudioClient → IAudioRenderClient`. Synthesis math is copied from `sdl_gfx.c`.
A background thread (`CreateThread`) feeds the render client in 10ms chunks.

**TinyGL pixel access:**
```c
uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes) {
    if (w) *w = g_w; if (h) *h = g_h;
    if (stride_bytes) *stride_bytes = g_w * 4;
    return g_pixels;   /* direct DIB section pointer */
}
void gfx_mark_pixels_dirty(void) {
    BitBlt(g_hdc, 0,0,g_w,g_h, g_memdc, 0,0, SRCCOPY);
}
```

**Commit:**
```bash
git add src/native_gfx_win32.c
git commit -m "feat: Win32/GDI/WASAPI graphics backend"
```

---

## Task 7: Update `src/tinygl_bridge.c`

**Files:**
- Modify: `src/tinygl_bridge.c`

Replace the SDL include and `sdl_get_renderer()` call with `native_gfx.h`:

**Remove:**
```c
#include <SDL3/SDL.h>
#include "sdl_gfx.h"

static SDL_Texture  *g_tgl_tex = NULL;
```

**Add:**
```c
#include "native_gfx.h"
```

**Replace `prim_tgl_init`:**

Old: creates `SDL_Texture`.
New: allocates nothing extra — the pixel buffer lives in the platform backend.

```c
static Val prim_tgl_init(Val args, Val env) {
    (void)env;
    int w = INT_VAL(CAR(args)), h = INT_VAL(CADR(args));
    if (g_zb) { ZB_close(g_zb); g_zb = NULL; }
    g_zb = TGL_ZB_OPEN(w, h);
    if (!g_zb) return NIL_VAL;
    g_tgl_w = g_zb->xsize;
    g_tgl_h = g_zb->ysize;
    return NIL_VAL;
}
```

**Replace `prim_tgl_present`:**

Old: `SDL_UpdateTexture` → `SDL_RenderTexture` → `SDL_RenderPresent`.
New: copy TinyGL pixel buffer into native offscreen surface, then call `gfx_mark_pixels_dirty`.

```c
static Val prim_tgl_present(Val args, Val env) {
    (void)args; (void)env;
    if (!g_zb) return NIL_VAL;
    int out_w, out_h, stride;
    uint32_t *dst = gfx_get_offscreen_pixels(&out_w, &out_h, &stride);
    if (!dst) return NIL_VAL;
    int copy_w = g_tgl_w < out_w ? g_tgl_w : out_w;
    int copy_h = g_tgl_h < out_h ? g_tgl_h : out_h;
    /* g_zb->pbuf is XRGB8888 (32-bit); linesize is bytes per row */
    for (int y = 0; y < copy_h; y++) {
        uint32_t *src_row = (uint32_t*)((uint8_t*)g_zb->pbuf + y * g_zb->linesize);
        uint32_t *dst_row = (uint32_t*)((uint8_t*)dst + y * stride);
        memcpy(dst_row, src_row, (size_t)copy_w * 4);
    }
    gfx_mark_pixels_dirty();
    return NIL_VAL;
}
```

Also remove the `#include <SDL3/SDL.h>` at line 6 and `#include "sdl_gfx.h"` at line 7.
The `HAVE_TINYGL` guard at line 3 stays.

**Commit:**
```bash
git add src/tinygl_bridge.c
git commit -m "refactor: wire tinygl_bridge to native_gfx instead of SDL"
```

---

## Task 8: Update `Makefile` and `setup.sh`

**Files:**
- Modify: `Makefile`
- Modify: `setup.sh`

**Step 1: Rewrite the relevant Makefile sections**

Replace everything from the `# SDL3:` block (line 38) to the end of the `HAVE_UV ?= 1` block (line 84) with:

```makefile
# Platform-specific I/O and graphics (no libuv, no SDL)
ifeq ($(OS),Windows_NT)
  SRCS    += src/native_gfx_win32.c src/native_io_win32.c src/pipe_win32.c
  LDFLAGS += -lgdi32 -lole32 -lksuser -lws2_32 -lwinmm
else
  SRCS    += src/native_gfx_x11.c src/native_io_posix.c src/pipe_posix.c
  CFLAGS  += $(shell pkg-config --cflags x11 alsa 2>/dev/null)
  LDFLAGS += $(shell pkg-config --libs   x11 alsa 2>/dev/null || echo "-lX11 -lasound") \
             -lpthread -lm
endif
SRCS += src/coro.c src/ttf.c

# TinyGL: still optional; disable with NO_TINYGL=1
ifndef NO_TINYGL
ifneq ($(wildcard deps/tinygl/lib/libTinyGL.a),)
CFLAGS  += -DHAVE_TINYGL=1 -Ideps/tinygl/include -Ideps/tinygl/src
LDFLAGS += -Ldeps/tinygl/lib -lTinyGL -lgomp
SRCS    += src/tinygl_bridge.c
endif
endif
```

Also remove `src/io_uv.c` and `src/sdl_gfx.c` from the `SRCS` line (they will be deleted in Task 9).

Update the `all` target:
```makefile
all: build/picolisp build/term_helper.so
```
(Remove `build/cb_helper.so` from `all` if it was there; it stays in `test` target.)

**Step 2: Update `setup.sh`**

Remove the following blocks:
- `# Build libuv` (lines 42–60)
- `# SDL3_ttf —` (lines 107–131)

Keep:
- `# Build libbf` (lines 22–40)
- `# Build TinyGL` (lines 63–84)
- `# JetBrainsMono font` (lines 86–105) — **keep this**, ttf_load needs a .ttf file

Add at the end a note about Linux system deps:
```bash
echo ""
echo "Setup complete."
echo "  Linux users: sudo apt-get install -y libx11-dev libasound2-dev"
echo "  Run: make"
echo "  Disable TinyGL: make NO_TINYGL=1"
```

**Step 3: Build**
```bash
make 2>&1 | tail -10
# Expected: build/picolisp linked successfully, 0 errors
```

**Step 4: Run tests**
```bash
bash run_tests.sh
# Expected: 34 passed, 0 failed, 0 skipped
```

**Step 5: Commit**
```bash
git add Makefile setup.sh
git commit -m "build: replace libuv+SDL with native platform backends"
```

---

## Task 9: Delete old files and final verification

**Files:**
- Delete: `src/io_uv.c`
- Delete: `src/io_uv.h`
- Delete: `src/sdl_gfx.c`
- Delete: `src/sdl_gfx.h`
- Delete: `src/pipe_proc.c` (replaced by pipe_posix.c / pipe_win32.c)

**Step 1: Delete old files**
```bash
git rm src/io_uv.c src/io_uv.h src/sdl_gfx.c src/sdl_gfx.h src/pipe_proc.c
```

**Step 2: Final build**
```bash
make clean && make 2>&1 | tail -10
# Expected: clean build, no errors, no warnings about missing symbols
```

**Step 3: Run full test suite**
```bash
bash run_tests.sh
# Expected: 34 passed, 0 failed, 0 skipped
```

**Step 4: Final commit**
```bash
git commit -m "cleanup: remove io_uv.c, sdl_gfx.c, pipe_proc.c — replaced by native backends"
```

**Step 5: Summary**

Confirm the following are gone from the build:
- No `-luv` in link flags
- No `-lSDL3` in link flags
- No `-lSDL3_ttf` in link flags
- `ldd build/picolisp` (Linux) shows only: libX11, libasound, libm, libpthread, libc
- `objdump -p build/picolisp | grep NEEDED` shows same

---

## Build commands reference

```bash
# Linux full build
make

# Linux, disable TinyGL
make NO_TINYGL=1

# Windows (from Developer Command Prompt or with mingw)
# Makefile detects OS=Windows_NT automatically

# Run tests
bash run_tests.sh

# Check a single test manually
./build/picolisp tests/test_ds.l
```
