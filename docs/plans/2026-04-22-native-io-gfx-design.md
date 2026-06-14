# Native I/O and Graphics — Drop libuv and SDL

**Date:** 2026-04-22

## Goal

Remove all libuv and SDL dependencies from the L interpreter. Replace with OS-native
implementations using platform-specific source files. Zero Lisp-visible changes.

## Motivation

- Eliminate two large external build dependencies (libuv, SDL3, SDL3_ttf)
- Simplify setup: Linux needs only `libx11-dev` and `libasound2-dev` (system packages);
  Windows needs only the Windows SDK (always present)
- Remove font dependency on SDL_ttf and Xft by writing a minimal TTF parser/rasterizer
- Each platform file is pure platform code — no `#ifdef` guards inside new files

---

## New File Structure

| New file | Platform | Replaces |
|---|---|---|
| `src/native_gfx_win32.c` | Windows | `src/sdl_gfx.c` (graphics + audio) |
| `src/native_gfx_x11.c` | Linux/macOS | `src/sdl_gfx.c` (graphics + audio) |
| `src/native_io_win32.c` | Windows | libuv sections of `src/io_uv.c` |
| `src/native_io_posix.c` | Linux/macOS | libuv sections of `src/io_uv.c` |
| `src/coro.c` | All | coroutine code extracted from `src/io_uv.c` verbatim |
| `src/ttf.c` | All | TTF parser + rasterizer (replaces SDL_ttf and Xft) |
| `src/tinygl_bridge.c` | All | updated: accepts native window handle instead of SDL renderer |

Files deleted: `src/sdl_gfx.c`, `src/io_uv.c`.

---

## Section 1: I/O Layer

### `src/native_io_posix.c` (Linux/macOS)

**Event loop:** single `poll()` call over a dynamic fd table. Each registered fd carries
a Lisp callback `Val`. Loop runs until no active handles remain.

**Timers:** sorted array of `{expiry_ms, callback}`. Poll timeout is set to
`min(nearest_expiry - now, INT_MAX)`. Expired timers fire after `poll` returns.

**TCP:** non-blocking BSD sockets. `tcp-connect` registers the fd; `on_writable`
fires the connect callback. `tcp-listen` binds/listens; `on_readable` accepts and
fires the connection callback. `tcp-write`/`tcp-close` are synchronous.

**HTTP client:** `http_sync_request` from `io_uv.c` copied verbatim (no libuv
dependency). Runs on a `pthread` worker thread to avoid blocking the event loop.

**HTTP server:** `accept` registered in poll fd table. Accumulates request bytes,
calls Lisp handler, writes response — same logic as current libuv server.

**File I/O:** `fopen`/`fread`/`fwrite` — copied unchanged from existing stubs.

### `src/native_io_win32.c` (Windows)

Identical model using `WSAPoll` (Winsock2) instead of `poll`. Timers via
`GetTickCount64`. HTTP worker thread via `_beginthreadex`. Winsock initialized
with `WSAStartup` on first use.

### `src/coro.c` (All platforms)

Coroutine implementation extracted from `io_uv.c` verbatim:
- Windows: Windows Fibers (`CreateFiber`/`SwitchToFiber`)
- POSIX: `ucontext_t` (`makecontext`/`swapcontext`)
- GC stack save/restore helpers (`coro_save_stacks`, `coro_restore_stacks`)
- `gc_mark_coros` for the GC to mark live coroutine values

No changes to logic; only the file changes.

### Primitives preserved (identical signatures)

`tcp-connect`, `tcp-listen`, `tcp-write`, `tcp-close`, `timer`, `file-read`,
`file-write`, `event-loop`, `uv-run`, `http-get`, `http-post`, `http-serve`,
`co`, `yield`, `co-resume`, `co-alive?`, `make-parameter`

---

## Section 2: TTF Rendering (`src/ttf.c`)

A minimal TrueType parser and scanline rasterizer covering ASCII 32–127.

### Tables parsed

`head`, `hhea`, `maxp`, `cmap` (format 4), `loca`, `glyf`, `hmtx`

### Glyph pipeline

1. `ttf_load(path)` — reads file, parses all tables, builds codepoint→glyph index map
2. `ttf_render_glyph(font, codepoint, px_size, out_bitmap)` — extracts outline,
   scales to pixel size, rasterizes via scanline fill into an 8-bit alpha bitmap
3. `ttf_glyph_metrics(font, codepoint, px_size, advance, lsb, ascent, descent)` — metrics
4. `ttf_free(font)` — releases all heap memory

### Rasterization

Quadratic Bézier curves (TrueType on-curve/off-curve) are flattened to line segments
at sub-pixel precision. Scanline fill uses a sorted edge table and non-zero winding rule.
Output: 8-bit alpha, caller composites onto RGBA surface.

### API used by platform files

```c
TtfFont *ttf_load(const char *path);
void     ttf_free(TtfFont *font);
int      ttf_render_glyph(TtfFont *f, uint32_t cp, int px,
                           uint8_t **bitmap, int *w, int *h,
                           int *xoff, int *yoff);
void     ttf_glyph_advance(TtfFont *f, uint32_t cp, int px, int *advance);
```

---

## Section 3: Graphics Layer

Both platform files implement the same set of internal functions; `main.c` calls
`gfx_prims_register()` which maps them to the Lisp primitives.

### `src/native_gfx_win32.c`

**Window:** `RegisterClass` + `CreateWindowEx` + `ShowWindow`. Message pump via
`PeekMessage`/`TranslateMessage`/`DispatchMessage` inside `sdl-poll`.

**2D rendering:** Offscreen `HBITMAP` (`CreateCompatibleBitmap`). All drawing
commands go to the offscreen DC. `sdl-present` calls `BitBlt` to copy it to the
window DC. Drawing primitives use GDI (`MoveToEx`/`LineTo`, `Rectangle`,
`Ellipse`) with a solid color pen/brush set per `sdl-color`.

**Text:** `ttf_render_glyph` produces an 8-bit alpha bitmap. Alpha-composite onto
the offscreen HBITMAP pixel-by-pixel (or via `AlphaBlend` with a pre-multiplied
DIB section).

**Audio:** WASAPI in shared mode. `audio-init` opens `IMMDeviceEnumerator` →
`IMMDevice` → `IAudioClient` → `IAudioRenderClient`. Synthesis (tone, drums,
samples) fills a float buffer; a background thread feeds it to WASAPI.

**TinyGL:** TinyGL renders to a `uint32_t` pixel buffer. `sdl-present` copies that
buffer into the offscreen HBITMAP via `SetDIBits`, then `BitBlt` to screen.

### `src/native_gfx_x11.c`

**Window:** `XOpenDisplay` + `XCreateSimpleWindow` + `XMapWindow` +
`XSelectInput` for key/mouse events.

**2D rendering:** Backing `Pixmap` (`XCreatePixmap`). Drawing commands use Xlib
primitives (`XDrawLine`, `XFillRectangle`, `XDrawArc`) with a `GC` whose
foreground is set per `sdl-color`. `sdl-present` calls `XCopyArea` to the window.

**Text:** `ttf_render_glyph` produces an 8-bit alpha bitmap. Composited onto the
backing Pixmap via `XPutImage` with a per-pixel alpha blend in C.

**Audio:** ALSA PCM (`snd_pcm_open` / `snd_pcm_writei`). Same synthesis logic as
Win32; background `pthread` feeds the PCM device.

**TinyGL:** TinyGL pixel buffer wrapped in an `XImage`; `XPutImage` copies it to
the backing Pixmap, then `XCopyArea` to screen on present.

### Primitives preserved (identical signatures)

`sdl-window`, `sdl-color`, `sdl-clear`, `sdl-present`, `sdl-point`, `sdl-line`,
`sdl-rect`, `sdl-fill`, `sdl-circle`, `sdl-fill-circle`, `sdl-ticks`, `sdl-delay`,
`sdl-key-name`, `sdl-quit`, `sdl-poll`, `sdl-wait-event`, `sdl-font-load`,
`sdl-text`, `sdl-text-size`, `audio-init`, `audio-tone`, `audio-drums`,
`audio-pending`, `audio-clear`, `audio-quit`, `audio-volume`, `audio-stop-all`,
`sample-load`, `samples-load-dir`, `sample-play`

---

## Section 4: Build System

### Makefile

```makefile
ifeq ($(OS),Windows_NT)
  SRCS    += src/native_gfx_win32.c src/native_io_win32.c
  LDFLAGS += -lgdi32 -lole32 -lksuser -lws2_32 -lwinmm
else
  SRCS    += src/native_gfx_x11.c src/native_io_posix.c
  CFLAGS  += $(shell pkg-config --cflags x11 alsa)
  LDFLAGS += $(shell pkg-config --libs x11 alsa) -lpthread
endif
SRCS += src/coro.c src/ttf.c
```

Removed: all libuv detection blocks, all SDL3/SDL3_ttf detection blocks,
`HAVE_UV`, `HAVE_SDL`, `HAVE_SDL_TTF`, `NO_SDL`, `NO_TINYGL`, `HAVE_UV` flags.
TinyGL detection block is kept (still optional, controlled by `NO_TINYGL=1`).

### setup.sh

Sections removed:
- libuv download/build
- SDL3_ttf clone/build
- JetBrainsMono font download (Xft no longer needed; TTF loaded directly)

Sections kept:
- libbf build
- TinyGL build (optional)
- JetBrainsMono font download — still needed as the default font for `sdl-font-load`

### New Linux system packages

```
libx11-dev  libasound2-dev
```

Both are standard packages available in every major distribution. No source builds.

### Windows

GDI32, WASAPI (ole32 + ksuser), Winsock2 (ws2_32), WinMM — all Windows SDK.
Zero new downloads or builds.
