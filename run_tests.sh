#!/usr/bin/env bash
set -e
PASS=0
FAIL=0
SKIP=0

INTERP="${1:-./build/l}"

if [ ! -x "$INTERP" ]; then
    echo "Interpreter not found: $INTERP"
    echo "Run 'make' first."
    exit 1
fi

for test_file in tests/test_*.l; do
    name=$(basename "$test_file")
    # Skip SDL tests unless display available
    if [[ "$name" == *"sdl"* ]] || [[ "$name" == *"gfx"* ]]; then
        echo "SKIP $name (SDL)"
        SKIP=$((SKIP+1))
        continue
    fi
    if timeout 30 "$INTERP" "$test_file" > /tmp/test_out.txt 2>&1; then
        if grep -q "ALL TESTS PASSED" /tmp/test_out.txt; then
            echo "PASS $name"
            PASS=$((PASS+1))
        else
            echo "FAIL $name (no PASSED marker)"
            cat /tmp/test_out.txt
            FAIL=$((FAIL+1))
        fi
    else
        echo "FAIL $name (exit code $?)"
        cat /tmp/test_out.txt
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ]
