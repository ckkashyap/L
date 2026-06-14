# ============================================================
# test_string.l -- String operations and symbol/string interplay
#
# What this covers:
#   • Predicates, substring extraction (sub), case conversion
#   • Whitespace trimming: trim, ltrim, rtrim
#   • Searching: index (1-based, returns NIL on miss)
#   • Substitution: replace (literal, not regex)
#   • Symbol<->string conversion: name, intern/sym
#   • Printing representation: str
#   • Template formatting: fmt with ~A and ~n
#
# Run:  L tests/test_string.l
# Spec: L.spec.txt Part 4 -- String and Symbol Operations
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Basic string predicates ------------------------------
# Strings in L are immutable interned byte arrays.  stringp returns
# T only for string objects; numbers and symbols are distinct types
# and return NIL even if they look like strings.
(test T (stringp "hello"))
(test NIL (stringp 42))
(test NIL (stringp 'sym))

# ---- 2. sub -- 0-indexed substring extraction -----------------
# sub is 0-indexed.  The second argument is the start index
# (inclusive) and the third is the LENGTH (not the end index).
# sub "hello" 1 3 => "ell" (start at index 1, take 3 chars).
# Requesting 0 characters returns the empty string.
(test "hel" (sub "hello" 0 3))   # start=0, length=3
(test "ell" (sub "hello" 1 3))   # third arg is LENGTH not end index: start=1, len=3 => "ell"
(test "llo" (sub "hello" 2 3))   # start=2, length=3
(test "o" (sub "hello" 4 1))     # start=4, length=1 => last char
(test "" (sub "hello" 5 0))      # edge case: length=0 => empty string

# ---- 3. upcase / downcase ------------------------------------
# upcase and downcase return NEW strings; the originals are not
# modified (strings are immutable).  Both handle full words including
# spaces -- only letter characters are changed.
(test "HELLO" (upcase "hello"))
(test "hello" (downcase "HELLO"))
(test "HELLO WORLD" (upcase "hello world"))

# ---- 4. trim / ltrim / rtrim ---------------------------------
# trim strips leading AND trailing whitespace.  ltrim strips only
# leading (left) whitespace; rtrim strips only trailing (right)
# whitespace.  All three return new strings.
(test "hello" (trim "  hello  "))
(test "hello  " (ltrim "  hello  "))   # left side stripped, right preserved
(test "  hello" (rtrim "  hello  "))   # right side stripped, left preserved

# ---- 5. index -- 1-based position search ---------------------
# index returns the 1-based position of the first occurrence of the
# search string within the target string.  Returns NIL (not -1 or 0)
# when the search string is not found -- callers must check for NIL.
(test 1 (index "h" "hello"))   # NOTE: 1-indexed in this implementation
(test 5 (index "o" "hello"))   # "o" is at position 5
(test NIL (index "z" "hello")) # edge case: NIL for missing substring, not -1

# ---- 6. replace -- literal string substitution ----------------
# replace performs literal (non-regex) substitution of all
# occurrences of the search string with the replacement string.
# It is NOT a regular expression engine -- special characters in the
# search string are treated as literal characters.
(test "hXllo" (replace "hello" "e" "X"))
(test "heXXo" (replace "hello" "l" "X"))  # replaces ALL occurrences of "l"

# ---- 7. name / intern / sym ----------------------------------
# name converts a symbol to its string name: (name 'foo) => "foo".
# intern and sym both convert a string to the interned symbol with
# that name: (intern "foo") => 'foo, (sym "bar") => 'bar.
# intern is idempotent -- interning the same string always returns
# the same symbol object.
(test "foo" (name 'foo))
(test 'foo (intern "foo"))
(test 'bar (sym "bar"))

# ---- 8. str -- value to printed string representation ---------
# str converts any L value to the string form it would print.
# Numbers become their decimal string; NIL becomes "NIL".
# This is the string equivalent of println output (without newline).
(test "42" (str 42))
(test "NIL" (str NIL))

# ---- 9. fmt -- template string formatting ---------------------
# fmt uses ~A (or ~a) as the placeholder for each successive argument
# and ~n for a newline character.  Arguments are consumed left to
# right; there must be exactly one argument per ~A placeholder.
(test "hello world" (fmt "hello ~A" "world"))
(test "x = 42" (fmt "x = ~A" 42))
(test "a/b" (fmt "~A/~A" "a" "b"))  # multiple placeholders consume args in order

(println "")
(println "test_string.l: ALL TESTS PASSED")
