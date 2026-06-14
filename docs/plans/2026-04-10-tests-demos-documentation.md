# Tests & Demos as Living Documentation — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enhance all 23 test files and 20 demo files so they serve as complete, narrative documentation of the L interpreter, and add 4 new demo files covering currently undocumented features.

**Architecture:** Each file gets a standard header block, section-level prose, and inline edge-case comments. New demo files follow the same template. All changes are purely additive (comments + reordering for clarity) — no test logic is altered, no demo output changes.

**Tech Stack:** L (Lisp interpreter), plain text comments in `.l` files

---

## Universal Style Guide

Every file MUST conform to this style. Subagents should apply it mechanically.

### File Header (all files)

```lisp
# ============================================================
# FILENAME — One-line description
#
# What this covers:
#   • Point 1: brief
#   • Point 2: brief
#   • Point 3: brief
#
# Run:  L tests/test_foo.l        (or demos/NN-topic.l)
# Spec: L.spec.txt Part N — Section Title
# ============================================================
```

### Test file section headers

```lisp
# ---- N. Section Title -------------------------------------
# 1-2 sentences explaining WHY this behavior is the way it is.
# Mention any gotchas or non-obvious semantics here.
```

### Demo file section headers

```lisp
# ---- N. Section Title -------------------------------------
# Prose explaining what the code block below demonstrates,
# including the "why" and any surprising edge cases.
```

### Inline comments

- Mark non-obvious results: `# => 42`
- Mark edge cases: `# edge case: NIL for missing key, not an error`
- Mark gotchas: `# NOTE: append takes exactly 2 args — third is silently ignored`
- Mark spec references: `# spec: Part V §TCO`

### Test harness (keep as-is — do NOT change)

```lisp
(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))
```

---

## Task 1 — Group A: Core list & math tests

**Files to modify:**
- `tests/test_core.l`
- `tests/test_math.l`
- `tests/test_char.l`
- `tests/test_string.l`

**What each file must document:**

`test_core.l` — Core list operations. Key semantics to explain:
- `cons` builds a pair; `car`/`cdr` destructure. NIL is the empty list.
- `eq` is pointer identity; `equal` is structural equality (recursive).
- `append` takes exactly 2 args — the third is silently ignored (NOTE this explicitly).
- `member` uses `equal` not `eq`; returns the tail, not T.
- `assoc` returns the whole pair `(key . val)`, not just the value.

`test_math.l` — Arithmetic & bitwise. Key semantics:
- All integers are exact; no floats.
- `mod` returns non-negative (sign of divisor); `rem` sign of dividend.
- `**` is integer exponentiation (returns bignum for large results).
- `gcd`/`lcm` always return non-negative.
- Bit shifts: `<<`/`>>` are arithmetic shifts on signed integers.

`test_char.l` — Character literals & conversions. Key semantics:
- `#\A` is a character literal (integer codepoint internally).
- `(char 65)` → `"A"` (string of length 1); `(char "A")` → 65.
- `pack`/`unpack` convert between char-list and string.

`test_string.l` — String operations. Key semantics:
- Strings are immutable interned byte arrays.
- `sub` is 0-indexed: `(sub "hello" 1 3)` → `"ell"`.
- `index` returns 0-based position or NIL (not -1).
- `name`/`sym` convert between symbol and string.
- `intern` creates or finds a symbol by name.

**Step 1: Read each file**

Read the current content of all four files before modifying.

**Step 2: Rewrite each file with documentation**

Apply the universal style guide. Add the file header, enhance section headers with prose, add inline comments for edge cases. Do NOT change any test logic or expected values.

**Step 3: Verify**

After modifying, confirm the file starts with the header block and each section has prose. Do a quick visual scan for any section that is just bare assertions with no comment.

---

## Task 2 — Group B: Data structures & algorithms tests

**Files to modify:**
- `tests/test_ds.l`
- `tests/test_vec.l`
- `tests/test_make.l`
- `tests/test_algo.l`

**What each file must document:**

`test_ds.l` — Persistent maps & vectors. Key semantics:
- `pmap` is a persistent (immutable) hash map. `(pmap-put m k v)` returns a NEW map.
- `pvec` is a persistent vector. `(pvec-put v i x)` returns a NEW vector.
- Neither mutates in place — the original is unchanged after every operation.
- These are for functional-style code; for mutable use `vec`.

`test_vec.l` — Mutable vectors. Key semantics:
- `(vec n)` creates a mutable vector of length n, all NIL.
- `(vec-set! v i x)` mutates in place, returns NIL.
- `(vec-get v i)` is 0-indexed.
- `(vec-push! v x)` appends, growing the vector.
- Contrast with `pvec` (persistent, returns new) vs `vec` (mutable, in-place).

`test_make.l` — make/link/chain accumulator. Key semantics:
- `(make ...)` sets up an implicit accumulator list; returns it at the end.
- `(link x)` appends x to the accumulator.
- `(chain lst)` splices a list into the accumulator.
- This is the idiomatic way to build lists without `reverse`.
- Equivalent to Clojure's transient + persistent! pattern but simpler.

`test_algo.l` — Sorting, grouping, statistics. Key semantics:
- `(sort lst)` sorts by natural order (numbers, then strings).
- `(by key-fn lst)` sorts by extracted key.
- `(maxi key-fn lst)` / `(mini key-fn lst)` find max/min by key.
- `(group-by key-fn lst)` returns an alist of `(key . members)`.
- `(reduce f init lst)` is left-fold with explicit initial value.
- `(any pred lst)` / `(every pred lst)` short-circuit on first match.

**Step 1–3:** Same as Task 1 — read, rewrite with style guide, verify.

---

## Task 3 — Group C: Control flow & scoping tests

**Files to modify:**
- `tests/test_do.l`
- `tests/test_letrec.l`
- `tests/test_guard.l`
- `tests/test_quasiquote.l`
- `tests/test_macro_only.l`

**What each file must document:**

`test_do.l` — do loop form. Key semantics:
- `(do ((var init step) ...) (test result) body...)` — Scheme-style iteration.
- Variables are updated simultaneously each step (not sequentially like `let*`).
- Loop terminates when `test` is true; returns `result`.
- Body forms are run for side effects only.

`test_letrec.l` — Mutually recursive bindings. Key semantics:
- `letrec` allows bindings to reference each other (mutual recursion).
- All RHS expressions see all binding names (unlike `let` where they don't).
- `letrec*` is sequential: each binding sees previous ones (like `let*`) but still recursive.
- Use `letrec` for even?/odd? style mutual recursion.

`test_guard.l` — Exception handling. Key semantics:
- `(catch tag body...)` establishes a catch point with a symbolic tag.
- `(throw tag value)` unwinds to the nearest matching `catch`, returning value.
- Tags are compared with `eq` (symbol identity).
- If no matching catch exists, the throw propagates (currently exits).
- This is NOT condition/restart — it is non-resumable escape.

`test_quasiquote.l` — Quasiquote & splicing. Key semantics:
- `` `(a ,b c) `` expands to `(list 'a b 'c)` — `,` evaluates.
- `` `(a ,@lst c) `` splices `lst` inline — `,@` must be a list.
- Nesting: `` `(a `(b ,,x)) `` — inner backtick creates another quasiquote.
- Used heavily in macros; also useful for building data structures.

`test_macro_only.l` — Macro definitions. Key semantics:
- `(dmacro name (args) body)` defines a macro — body runs at expand time.
- Macros receive unevaluated argument forms.
- `gensym` generates a unique symbol to avoid hygiene issues.
- Macros expand before evaluation — they are NOT functions.

**Step 1–3:** Same pattern.

---

## Task 4 — Group D: Advanced feature tests

**Files to modify:**
- `tests/test_bignum.l`
- `tests/test_io.l`
- `tests/test_plist.l`
- `tests/test_coro.l`
- `tests/test_gc.l`

**What each file must document:**

`test_bignum.l` — Arbitrary precision integers. Key semantics:
- Integers exceeding 32-bit automatically promote to bignum.
- All arithmetic operators work transparently on bignums.
- `(big? x)` tests if a value is a bignum (not a fixnum).
- No floats in L — use bignums or scaled integers for precision.
- Backend: libbf (same as QuickJS).

`test_io.l` — File I/O. Key semantics:
- `(write-lines path lst)` writes each element as a line (appends newline).
- `(read-all-lines path)` returns a list of strings, one per line.
- No trailing newline on last element from `read-all-lines`.

`test_plist.l` — Property lists. Key semantics:
- Every symbol has an associated alist `(key . val)` called its plist.
- `(put sym key val)` adds/updates; `(get sym key)` retrieves or NIL.
- `(prop sym key)` returns the live `(key . val)` cons cell for mutation.
- `(plist sym)` returns the full alist.
- `(with sym body...)` binds `This` to sym; `:`, `=:`, `::` are shorthands.
- `:` and `::` take unevaluated key symbols — they are special forms, not functions.

`test_coro.l` — Coroutines. Key semantics:
- `(co-new fn)` creates a suspended coroutine from a 1-arg function.
- `(co-resume coro val)` resumes, passing val as the result of the last `yield`.
- `(co-yield val)` suspends, returning val to the caller.
- `(co-alive? coro)` is false after the body returns.
- **Drain pattern**: prime outside loop, check `co-alive?` in condition,
  collect inside, resume at bottom — avoids capturing the body's return value.

`test_gc.l` — GC stress tests. Key semantics:
- GC is mark-sweep, triggered automatically at allocation.
- All live values must be reachable via the GC root stack (PUSH_ROOT/POP_ROOT in C).
- Coroutine stacks are snapshotted on yield for GC safety.
- This file exists to ensure GC correctness under stress, not algorithmic correctness.

**Step 1–3:** Same pattern.

---

## Task 5 — Group E: Coverage & performance tests

**Files to modify:**
- `tests/test_new_features.l`
- `tests/test_prims_coverage.l`
- `tests/test_stdlib.l`
- `tests/test_perf2.l`
- `tests/test_perf3.l`

**What each file must document:**

`test_new_features.l` — Feature regression suite. Document what each section is regressing: TCO depth guarantee (100k frames), closure correctness, multiple return values, `case` dispatch, `cond` fallthrough, `while` semantics.

`test_prims_coverage.l` — Primitive smoke tests. Add prose per group: list ops, predicates, arithmetic combinators, string ops, functional combinators (`identity`, `constantly`, `compose`). Each section should explain what "primitive" means — implemented in C, not L.

`test_stdlib.l` — Standard library (lib/stdlib.l). Explain that stdlib.l is loaded automatically at startup if present. Document each module: string utilities (split, join, pad, repeat), math utilities (range, iota, sqrt, even?, odd?).

`test_perf2.l` — Performance baseline. Add comment explaining: fib(30) ≈ 2.7M recursive calls; must finish in <5s on any supported platform. This is a sanity check for TCO and call overhead, not correctness.

`test_perf3.l` — List performance baseline. Add comment: 1000-element list operations, sum verification. Tests that list allocation and traversal scale linearly.

**Step 1–3:** Same pattern.

---

## Task 6 — Group F: Tutorial demos 01–05

**Files to modify:**
- `demos/01-basics.l`
- `demos/02-functions.l`
- `demos/03-macros.l`
- `demos/04-ds.l`
- `demos/05-strings.l`

These are the **beginner tutorial** files. They must read like a guided tour with each section building on the previous. Narrative style is most important here — imagine teaching someone coming from Python or JavaScript.

`01-basics.l` topics with prose:
1. Atoms (numbers, symbols, strings, NIL, T) — "Everything in L is a value. NIL is both false and the empty list."
2. Lists and S-expressions — "Code and data have the same syntax."
3. Arithmetic — basic ops, operator precedence doesn't exist (prefix always)
4. Conditionals (if, cond) — "NIL is false; everything else including 0 is true."
5. Recursion (factorial) — "L has no loops at the language level; recursion is the primary mechanism."

`02-functions.l` topics with prose:
1. `de` (named functions with dynamic scope)
2. `lambda` (anonymous functions with lexical scope)
3. Closures — counter example showing captured mutable state
4. Higher-order functions (mapcar, filter, apply)
5. TCO — show that `(sum-to 100000)` works without stack overflow

`03-macros.l` topics with prose:
1. `dmacro` definition — explain expand-time vs runtime
2. `my-and` / `my-unless` — short-circuit via macro, impossible as function
3. `breakable-while` with `catch`/`throw` for early exit
4. `swap!` — show quasiquote in macro body
5. `gensym` for hygiene

`04-ds.l` topics with prose:
1. `pmap` — persistent hash map; immutable update returns new map
2. `pvec` — persistent vector; immutable update returns new vec
3. Sharing structure — show two "updated" maps both derived from original
4. When to use pmap vs alist vs plist

`05-strings.l` topics with prose:
1. String literals and escapes
2. Case, trim, sub, index, replace
3. `pack`/`unpack` — character list to string and back
4. `fmt` — template formatting with `~a` and `~n`
5. Predicates (starts-with?, ends-with?, contains?)
6. split/join, repeat, pad-left/pad-right

**Step 1–3:** Read, rewrite with style guide, verify narrative flow.

---

## Task 7 — Group G: Coroutine & algorithm demos

**Files to modify (including a full rewrite of 06):**
- `demos/06-coroutines.l` ← **full rewrite** (currently a stub)
- `demos/17-coro.l`
- `demos/18-coro-algos.l`
- `demos/16-algo.l`

`06-coroutines.l` — **Full rewrite**. This is the primary coroutine tutorial. Sections:
1. What is a coroutine? — suspended computation, contrasted with a function
2. Infinite generator: `integers-from` with `co-yield`
3. Finite generator with return value: show `co-alive?` transition
4. **The drain pattern** — explain why prime-and-pump avoids capturing the return value. Show the WRONG pattern (while co-alive? collect) and then the CORRECT one.
5. Bidirectional: accumulator that receives values via `co-resume` return
6. Pipeline: producer → filter → consumer

`17-coro.l` — Already good, add prose to each section explaining the pattern name and use case.

`18-coro-algos.l` — Add prose explaining: how Sieve is a pipeline of filters, how tree traversal naturally maps to coroutines, how permutations use a coroutine as a search cursor.

`16-algo.l` — Add prose per algorithm group. Explain `by`/`maxi`/`mini` as "key-based ordering", `group-by` as "partition by equivalence class", `reduce` as "structural recursion over a list".

---

## Task 8 — Group H: System integration demos

**Files to modify:**
- `demos/07-io.l`
- `demos/08-bignum.l`
- `demos/09-http.l`
- `demos/14-spawn.l`
- `demos/15-ffi.l`

`07-io.l` — Add prose: file I/O model (ports), write-lines/read-all-lines, fmt output, JSON encoding. Note that I/O primitives are synchronous in non-UV builds.

`08-bignum.l` — Add prose: explain libbf backend, show how factorial(100) works, explain that all arithmetic auto-promotes, note there are no floats.

`09-http.l` — Add prose: requires HAVE_UV build flag. Explain event-loop model. Note that http-serve/http-get are built on libuv callbacks bridged to L via coroutines.

`14-spawn.l` — Add prose: pipe-open creates bidirectional pipe to subprocess. Show stdin/stdout communication. Note platform differences (findstr on Windows vs grep on POSIX).

`15-ffi.l` — Add prose: explain type codes (`:int`, `:ptr`, `:str`, `:void`). Note Windows-only `GetCurrentProcessId` examples. Explain that FFI calls are synchronous (no async FFI).

---

## Task 9 — Group I: Graphics & audio demos

**Files to modify:**
- `demos/10-gfx.l`
- `demos/11-music.l`
- `demos/13-tinygl.l`
- `demos/showcase.l`

`10-gfx.l` — Add prose: requires HAVE_SDL. Explain SDL3 window creation, event loop, drawing primitives. Note that all coordinates are integers.

`11-music.l` — Add prose per section: step sequencer state model, timing calculation (BPM → ms), SDL rendering of grid, keyboard event handling, audio synthesis calls. This is a real application — document it like one.

`13-tinygl.l` — Add prose: TinyGL is a software OpenGL 1.x rasterizer. Integers scaled by 100 for fixed-point. Matrix stack, perspective projection, SDL blit for display.

`showcase.l` — Rewrite as a guided tour of ALL major features, one section per subsystem: core Lisp, closures/TCO, macros, bignum, strings, pmap/pvec, coroutines, property lists. Each section should be self-contained and runnable as a snippet.

---

## Task 10 — New demo: 19-plist.l

**File to create:** `demos/19-plist.l`

Full tutorial on property lists and OOP-style programming. Sections:
1. What is a property list? — every symbol has an associated alist
2. `put`/`get` basics — store and retrieve named properties
3. `prop` — access the live cons cell for direct mutation
4. `plist` — inspect the full property list of a symbol
5. OOP shorthand — `with`, `:`, `=:`, `::` — show all four together
6. Simulation: a "point" object with x/y properties and methods via `de`
7. Contrast with `pmap` — when to use each

Template:
```lisp
# ============================================================
# 19-plist.l — Property lists and OOP-style programming
#
# What this covers:
#   • put/get: storing named properties on symbols
#   • prop: live cell access for in-place mutation
#   • with/:/=:/:: OOP shorthands
#   • Building objects from plain symbols + plists
#
# Run:  L demos/19-plist.l
# Spec: L.spec.txt Part VII — Property Lists
# ============================================================
```

---

## Task 11 — New demo: 20-exceptions.l

**File to create:** `demos/20-exceptions.l`

Full tutorial on error handling. Sections:
1. `throw`/`catch` basics — non-resumable escape
2. Tag matching — only the nearest matching tag catches
3. Returning a value from catch — the thrown value becomes the catch result
4. `guard`-style pattern — wrapping a computation with error recovery
5. Using catch for early exit from loops (breakable-while pattern)
6. Nesting catches — innermost match wins
7. What happens with no matching catch — process exits (document this limitation)

Template header:
```lisp
# ============================================================
# 20-exceptions.l — Exception handling: catch and throw
#
# What this covers:
#   • catch/throw: non-resumable stack unwinding
#   • Tag-based matching (symbol identity via eq)
#   • Returning values through a throw
#   • Early exit from loops using catch
#
# Run:  L demos/20-exceptions.l
# Spec: L.spec.txt Part VI — Special Forms: catch/throw
# ============================================================
```

---

## Task 12 — New demo: 21-control-flow.l

**File to create:** `demos/21-control-flow.l`

Full tutorial on control flow beyond if/cond. Sections:
1. `while` — basic loop with mutable state
2. `do` — Scheme-style loop with simultaneous variable steps
3. `letrec` — mutual recursion (even?/odd? example)
4. `letrec*` — sequential recursive bindings
5. Named-let pattern — `(let loop ((i 0)) ... (loop (1+ i)))` for iteration
6. `for` / `mapc` — iteration over lists
7. `case` — dispatch on value
8. `cond` — multi-branch conditional; `T` as else clause

---

## Task 13 — New demo: 22-vectors.l

**File to create:** `demos/22-vectors.l`

Full tutorial contrasting mutable `vec` and persistent `pvec`. Sections:
1. `vec` — mutable, created with `(vec n)`, get/set with index, push to grow
2. `pvec` — persistent, `pvec-put` returns new vector, original unchanged
3. When to use which — `vec` for performance-critical loops, `pvec` for functional pipelines
4. `vec-length`, `vec-push!` semantics
5. Building a matrix as a vector of vectors
6. Converting between vec and list (`vec->list`, list building with `make`/`link`)

---

## Verification (after all tasks)

Run the full test suite to confirm no test file was accidentally broken:

```bash
for f in tests/test_*.l; do echo "--- $f ---"; L "$f"; done
```

Expected: every file prints only dots and a "done" line, no FAIL lines.

Run all demos to confirm they still execute (demos that require HAVE_UV/HAVE_SDL will print errors if the feature is not compiled in — that is acceptable):

```bash
for f in demos/*.l; do echo "--- $f ---"; L "$f" 2>&1 | head -5; done
```

---

**Plan complete and saved to `docs/plans/2026-04-10-tests-demos-documentation.md`.**

Two execution options:

**1. Subagent-Driven (this session)** — I dispatch a fresh subagent per task group, review between tasks, fast iteration

**2. Parallel Session (separate)** — Open a new session with executing-plans, batch execution with checkpoints

Which approach?
