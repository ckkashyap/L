# Makefile for L interpreter — Linux/WSL/macOS (GCC or Clang)
#
# Default build uses platform-native I/O and graphics (no libuv, no SDL).
# Disable TinyGL with: make NO_TINYGL=1
#
CC      = gcc
CFLAGS  = -std=c17 -O2 -Wall -Wextra -Isrc

# libbf: prefer system pkg-config, fall back to local deps/libbf/
ifeq ($(shell pkg-config --exists libbf 2>/dev/null && echo yes),yes)
CFLAGS  += -DHAVE_LIBBF=1 $(shell pkg-config --cflags libbf)
LDFLAGS += $(shell pkg-config --libs libbf)
else ifneq ($(wildcard deps/libbf/libbf.a),)
CFLAGS  += -DHAVE_LIBBF=1 -Ideps/libbf
LDFLAGS += -Ldeps/libbf -lbf
else
$(info libbf not found — bignum will use built-in fallback.)
endif

LDFLAGS += -lm
TARGET  = build/l
SRCS    = src/main.c src/heap.c src/sym.c src/reader.c src/eval.c \
          src/print.c src/prims.c src/bignum.c
OBJS    = $(SRCS:src/%.c=build/%.o)

# Platform-specific I/O and graphics (no libuv, no SDL)
ifeq ($(OS),Windows_NT)
  SRCS    += src/native_gfx_win32.c src/native_io_win32.c src/pipe_win32.c
  LDFLAGS += -lgdi32 -lole32 -lksuser -lws2_32 -lwinmm
else
  SRCS    += src/native_gfx_x11.c src/native_io_posix.c src/pipe_posix.c
  CFLAGS  += $(shell pkg-config --cflags x11 alsa 2>/dev/null)
  LDFLAGS += $(shell pkg-config --libs   x11 alsa 2>/dev/null || echo "-lX11 -lasound") \
             -lpthread -lm -rdynamic
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

# libffi: optional FFI support
ifeq ($(shell pkg-config --exists libffi 2>/dev/null && echo yes),yes)
CFLAGS  += -DHAVE_FFI=1 $(shell pkg-config --cflags libffi)
LDFLAGS += $(shell pkg-config --libs libffi) -ldl
SRCS    += src/ffi.c src/callbacks.c
endif

.PHONY: all clean test

all: build/l build/term_helper.so build/cb_helper.so build/gen_samples

build/l: $(OBJS) | build
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

build/gen_samples: tools/gen_samples.c | build
	$(CC) -O2 -o $@ $< -lm

clean:
	rm -rf build/

# cb_helper shared library for callback tests
build/cb_helper.so: tests/helpers/cb_helper.c | build
	$(CC) -std=c17 -shared -fPIC -o $@ $<

build/term_helper.so: src/term_helper.c | build
	$(CC) -std=c17 -shared -fPIC -o $@ $<

test: all build/cb_helper.so build/term_helper.so
	bash run_tests.sh

# Dependency on header
$(OBJS): src/picolisp.h
