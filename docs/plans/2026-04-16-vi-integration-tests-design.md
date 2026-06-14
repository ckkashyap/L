# Vi Integration Test Suite — Design

**Date:** 2026-04-16

## Goal

Add pre-written integration tests that drive the vi mode-dispatch coroutine
with synthetic key sequences, covering all major vi features, without
requiring a terminal or SDL window.

## Architecture

### Shared Harness (`tests/vi_harness.l`)

Loads all vi modules (no term/gfx), defines helpers:

- `(vi-run text keys)` — creates buf+state, primes dispatch, feeds each key,
  returns final state
- `(vi-text state)` — extract buffer as string via `buf-to-string`
- `(vi-cursor state)` — return `(row . col)` pair
- `(vi-mode state)` — return mode symbol
- `(check expected actual label)` — print PASS/FAIL, quit 1 on failure

Key representation matches what `make-term-key-source` yields:
- Printable chars: strings (`"a"`, `"w"`, `":"`)
- Special keys: symbols (`'ret`, `'bs`, `'esc`, `'up`, `'down`, `'ctrl-r`, `'ctrl-v`, etc.)

### Test Files (each loads vi_harness.l, ends with "ALL TESTS PASSED")

| File | Coverage |
|------|----------|
| `tests/test_vi_normal.l` | Motions: h/j/k/l, w/b/e, 0/^/$, G/gg, H/M/L; counts; operators: x, dd/dw/d$, yy/yw, p/P; search: /n/N/? |
| `tests/test_vi_insert.l` | i/a/o/O entry, typing chars, backspace, ESC return to normal |
| `tests/test_vi_visual.l` | v (char), V (line), ctrl-v (block); d/y/c on selection |
| `tests/test_vi_cmd.l` | :w (write), :q (quit sets running=NIL), :s/old/new/g substitution |
| `tests/test_vi_undo_macro.l` | u (undo), ctrl-r (redo), qa…q record, @a playback |

### Integration with Existing Runner

`run_tests.sh` already globs `tests/test_*.l` and checks for
`"ALL TESTS PASSED"` — no changes needed to the runner. The harness file
`vi_harness.l` is not prefixed `test_` so it won't be run directly.

## Key Design Decisions

- **No term/gfx deps**: harness loads only `lib/vi/{buffer,state,cursor,ops,search,visual,macro,mode,cmd,file}.l`
- **Minimal mock**: `:w` in cmd.l writes a real file; test supplies a temp path and checks `dirty=NIL`
- **Coroutine priming**: `(co-resume disp NIL)` must be called once before feeding keys (matches vi.l main loop)
- **Count tests**: feed digit chars before operator (e.g. `"3" "j"` moves 3 lines down)
