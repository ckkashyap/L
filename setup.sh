#!/usr/bin/env bash
set -e
# setup.sh -- First-time dependency setup for Linux/WSL/macOS
#
# Dependencies:
#   Required: gcc, make, libx11-dev, libasound2-dev
#   Optional: libffi-dev (for FFI), libbf (bignum), TinyGL (3D)

MAX_SETUP=false
for arg in "$@"; do
    case "$arg" in
        -max|--max) MAX_SETUP=true ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== L Interpreter Setup ==="

# Create deps directories
mkdir -p deps/libbf deps/fonts deps/tinygl temp

# Check for required tools
for tool in gcc make curl; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: $tool is required but not found. Please install it."
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# libbf -- arbitrary precision bignum library
# ---------------------------------------------------------------------------
if [ ! -f "deps/libbf/libbf.a" ]; then
    echo "--- Building libbf ---"
    if [ ! -f "deps/libbf/libbf.c" ]; then
        echo "Downloading libbf-2025-06-03..."
        curl -sL "https://bellard.org/libbf/libbf-2025-06-03.tar.gz" \
            | tar -xz --strip-components=1 -C deps/libbf
        if [ ! -f "deps/libbf/libbf.c" ]; then
            echo "ERROR: download failed. Place libbf sources in deps/libbf/ manually."
            echo "Get them from: https://bellard.org/libbf/"
            exit 1
        fi
    fi
    cd deps/libbf
    gcc -std=c99 -O2 -c libbf.c -o libbf.o
    gcc -std=c99 -O2 -c cutils.c -o cutils.o
    ar rcs libbf.a libbf.o cutils.o
    cd "$SCRIPT_DIR"
    echo "libbf built OK"
else
    echo "libbf already built, skipping."
fi

# ---------------------------------------------------------------------------
# TinyGL -- software OpenGL rasterizer (optional, for 3D demos)
# ---------------------------------------------------------------------------
if [ ! -f "deps/tinygl/lib/libTinyGL.a" ]; then
    echo "--- Building TinyGL ---"
    if [ ! -f "deps/tinygl/src/list.c" ]; then
        echo "Downloading TinyGL 0.4.1 from bellard.org..."
        curl -sL "https://bellard.org/TinyGL/TinyGL-0.4.1.tar.gz" \
            | tar -xz --strip-components=1 -C deps/tinygl
        if [ ! -f "deps/tinygl/src/list.c" ]; then
            echo "WARNING: TinyGL download failed. Skipping (3D demos will not work)."
        fi
    fi
    if [ -f "deps/tinygl/src/list.c" ]; then
        mkdir -p deps/tinygl/lib
        make -C deps/tinygl/src \
            CC=gcc CFLAGS="-O2 -fPIC -Wall" \
            INCLUDES="-I../include" \
            TINYGL_USE_GLX= TINYGL_USE_NANOX= TINYGL_USE_BEOS=
        cp deps/tinygl/src/libTinyGL.a deps/tinygl/lib/libTinyGL.a
        echo "TinyGL built OK"
    fi
else
    echo "TinyGL already built, skipping."
fi

# ---------------------------------------------------------------------------
# JetBrainsMono font -- for vi --gfx TTF rendering
# ---------------------------------------------------------------------------
if [ ! -f "deps/fonts/JetBrainsMono-Regular.ttf" ]; then
    echo "--- Downloading JetBrainsMono font ---"
    mkdir -p deps/fonts
    JBM_VER="2.304"
    if curl -fsSL "https://github.com/JetBrains/JetBrainsMono/releases/download/v${JBM_VER}/JetBrainsMono-${JBM_VER}.zip" \
            -o temp/JetBrainsMono.zip; then
        unzip -p temp/JetBrainsMono.zip "fonts/ttf/JetBrainsMono-Regular.ttf" \
            > deps/fonts/JetBrainsMono-Regular.ttf 2>/dev/null || \
        unzip -j temp/JetBrainsMono.zip "*JetBrainsMono-Regular.ttf" \
            -d deps/fonts/ 2>/dev/null
        rm -f temp/JetBrainsMono.zip
        echo "JetBrainsMono-Regular.ttf downloaded."
    else
        echo "WARNING: Could not download JetBrainsMono font."
        echo "  Place JetBrainsMono-Regular.ttf in deps/fonts/ manually."
    fi
else
    echo "JetBrainsMono font already present."
fi

# ---------------------------------------------------------------------------
# Generate audio samples (WAV files) for live-coding demos
# Uses tools/gen_samples.c -- no Python needed, just the C compiler.
# ---------------------------------------------------------------------------
if [ ! -f "deps/samples/drums/kick.wav" ]; then
    echo "--- Generating audio samples ---"
    gcc -O2 -o temp/gen_samples tools/gen_samples.c -lm
    if [ $? -eq 0 ]; then
        temp/gen_samples
        rm -f temp/gen_samples
    else
        echo "WARNING: Could not compile tools/gen_samples.c"
    fi
else
    echo "Audio samples already present."
fi

# ---------------------------------------------------------------------------
# Salamander Grand Piano -- real piano samples (requires -max flag)
# The archive is ~1.2GB but we cache it so re-runs after clean clone are fast.
# ---------------------------------------------------------------------------
if [ "$MAX_SETUP" = true ] && [ ! -f "deps/samples/piano-real/C4.wav" ]; then
    echo "--- Salamander Grand Piano ---"
    mkdir -p deps/samples/piano-real
    SALAM_CACHE="temp/salamander_piano_v3.tar.xz"
    SALAM_URL="https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPianoV3+20161209_48khz24bit.tar.xz"
    if [ ! -f "$SALAM_CACHE" ]; then
        echo "  Downloading (~1.2GB, cached in $SALAM_CACHE for future runs)..."
        curl -fSL "$SALAM_URL" -o "$SALAM_CACHE" || {
            echo "  WARNING: Download failed. Skipping real piano samples."
            rm -f "$SALAM_CACHE"
        }
    else
        echo "  Using cached archive: $SALAM_CACHE"
    fi
    if [ -f "$SALAM_CACHE" ]; then
        echo "  Extracting mezzo-forte layer..."
        tar -xJf "$SALAM_CACHE" --strip-components=2 -C deps/samples/piano-real \
            --wildcards '*/48khz24bit/*v8.wav' 2>/dev/null || \
        tar -xJf "$SALAM_CACHE" --strip-components=2 -C deps/samples/piano-real 2>/dev/null
        # Keep ONLY v8 layer, delete everything else
        find deps/samples/piano-real -name '*.wav' ! -name '*v8*' -delete 2>/dev/null
        for f in deps/samples/piano-real/*v8.wav; do
            [ -f "$f" ] && mv "$f" "${f%v8.wav}.wav" 2>/dev/null
        done
        find deps/samples/piano-real -type f ! -name '*.wav' -delete 2>/dev/null
        find deps/samples/piano-real -type d -empty -delete 2>/dev/null
        COUNT=$(ls deps/samples/piano-real/*.wav 2>/dev/null | wc -l)
        echo "  Salamander Piano: $COUNT samples (~$(du -sh deps/samples/piano-real 2>/dev/null | cut -f1))"
    fi
elif [ "$MAX_SETUP" = true ]; then
    echo "Salamander Piano already present."
else
    echo "Skipping Salamander Piano (pass -max to download)."
fi

echo ""
echo "Setup complete."
echo "  Install system deps: sudo apt-get install -y libx11-dev libasound2-dev libffi-dev pkgconf"
echo "  Build:   make"
echo "  Test:    bash run_tests.sh"
echo "  Options: make NO_TINYGL=1  (skip TinyGL)"
