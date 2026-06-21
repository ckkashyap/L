# L — A Compact Lisp Interpreter

L is a pragmatic Lisp interpreter for systems programming, live coding, and multimedia applications. Written in portable C17, it targets Windows (MSVC) and POSIX (Linux, macOS, BSD) with **zero external runtime dependencies** — I/O, graphics, audio, and font rendering are all built-in native platform backends.

The entire interpreter fits in ~12,000 lines of C.

## Features

- **NaN-boxed values** — Every Lisp value is a single 64-bit word. Uniform, cache-friendly.
- **Tail call optimization** — The last expression in every body runs without consuming a C stack frame.
- **Dynamic scoping** (PicoLisp-style) with optional lexical closures via `lambda`.
- **Arbitrary-precision arithmetic** — Bignums and 128-bit floats via [libbf](https://bellard.org/libbf/) (optional; falls back to int64).
- **Coroutines** — First-class generators with `yield` / `co-resume`. GC-aware suspended roots.
- **Persistent data structures** — HAMT hash maps (O(log₃₂ n)), 32-way trie vectors (Clojure-style).
- **Destructuring bind** — Nested patterns in `let`, `let*`, `lambda`, and `de`.
- **Native I/O** — Built-in event loop (poll/WSAPoll), TCP, timers, HTTP server, WebSocket, subprocess spawning.
- **Native 2D graphics** — Win32 GDI on Windows, Xlib on Linux. Built-in TTF rasterizer with 4× supersampled antialiasing.
- **Native audio** — WASAPI (Windows) / ALSA (Linux). 32-voice mixer, 20ms latency, ahead-of-time beat scheduling.
- **TinyGL** — Optional software-rendered OpenGL 1.x for 3D demos.
- **FFI** — Call C libraries via `native`. x64 ABI caller on Windows, libffi on POSIX.
- **Vi editor** — A built-in vi clone with graphical mode (`vi --gfx`).

## Quick Start

### Prerequisites

| Platform | Requirements |
|----------|-------------|
| Linux    | `gcc`, `make`, `pkg-config`, `libx11-dev`, `libasound2-dev`, `libffi-dev` |
| Windows  | Visual Studio 2022 (or Build Tools) with C++ workload |

### Setup & Build

**Linux / macOS / WSL:**
```bash
bash setup.sh          # downloads deps, builds libbf & TinyGL
make                   # produces build/l
```

**Windows:**
```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
build.bat
```

Pass `-max` to setup to also download the Salamander Grand Piano samples (~1.2 GB):
```bash
bash setup.sh -max
```

### Run

```bash
build/l                      # REPL
build/l demos/01-basics.l    # run a demo
build/l demos/28-livecode.l  # live coding with audio
```

## Demos

| # | Demo | Description |
|---|------|-------------|
| 01 | basics | Core language: lists, math, recursion |
| 02 | functions | `de`, `dm`, higher-order functions |
| 03 | macros | Macro definition and expansion |
| 04 | ds | Data structures: HAMT, vectors |
| 05 | strings | String manipulation |
| 06 | coroutines | Generators, pipelines |
| 07 | io | File I/O, TCP, HTTP server |
| 08 | bignum | Arbitrary-precision arithmetic |
| 09 | http | HTTP client/server |
| 10 | gfx | 2D graphics |
| 11 | music | Audio synthesis |
| 12 | samples | Drum sequencer + piano sampler |
| 13 | tinygl | 3D rendering |
| 14 | spawn | Subprocesses |
| 15 | ffi | Foreign function interface |
| 16–18 | algo/coro | Algorithms with coroutines |
| 24 | vi | Built-in vi editor |
| 28–29 | livecode | Live coding with real-time audio |

## Testing

```bash
# Linux
bash run_tests.sh

# Windows
powershell -File run_tests.ps1
```

## Project Structure

```
src/            C source (interpreter core, platform backends)
lib/            Standard library (.l files loaded at runtime)
demos/          Example programs
tests/          Test suite
tools/          Build/setup utilities
deps/           Downloaded dependencies (gitignored)
compat/         MSVC compatibility shims
```

## Design Inspirations

- **PicoLisp** — Dynamic scoping, `de`/`dm`, property lists, minimalism
- **Clojure** — Persistent data structures, structural sharing, destructuring
- **SonicPi** — Temporal model for drift-free beat scheduling, live coding culture
- **libbf** — Arbitrary-precision numerics from Fabrice Bellard

## License

See repository for license details.
