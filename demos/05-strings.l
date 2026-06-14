# ============================================================
# 05-strings.l -- String literals, operations, formatting, predicates
#
# What this covers:
#   1. String literals and escapes
#   2. Case, trim, sub -- transforming strings
#   3. pack / unpack -- strings as character sequences
#   4. fmt -- template formatting
#   5. String predicates -- starts-with?, ends-with?, contains?
#   6. split / join, repeat, pad -- building and decomposing strings
#
# Run:  L demos/05-strings.l
# Spec: L.spec.txt Part 5 -- Strings and Characters
# ============================================================

(load "lib/string.l")

(println "=== Strings Demo ===")

# ---- 1. String literals and escapes -----------------------
# Strings in L are double-quoted. Standard escapes: `\n`, `\t`,
# `\\`, `\"`. Strings are immutable -- you cannot mutate a string
# in place; every operation returns a new one. This is the same
# model as Python's str or JavaScript's String primitive.
#
# L strings are UTF-8 internally. `unpack` and `pack` (section 3)
# work at the codepoint level, so they handle multi-byte
# characters correctly.

# ---- 2. Case, trim, sub -----------------------------------
# `upcase` / `downcase` return new strings with the case changed.
# `trim` strips leading and trailing whitespace; `ltrim` and
# `rtrim` strip only one side.
#
# `sub` extracts a substring. The second argument is the start
# index (0-based) and the third is the number of characters to
# extract (the length of the result, not the end index). So
# `(sub "hello world" 6 5)` starts at position 6 and takes 5
# characters, giving "world". This is the same convention as
# JavaScript's substr (not slice).

(println (upcase "hello world"))       # => HELLO WORLD
(println (downcase "HELLO WORLD"))     # => hello world
(println (trim "   hello   "))         # => hello
(println (sub "hello world" 6 5))      # => world

# ---- 3. pack / unpack -------------------------------------
# `unpack` converts a string to a list of character codepoints
# (integers). `pack` does the inverse: converts a list of
# integers to a string. This lets you process strings as
# sequences of characters -- filtering, mapping, sorting -- using
# all the list operations you already know.
#
# `pack` is also variadic over strings: you can pass a list of
# strings and it concatenates them. The example below shows both
# uses: unpack for character-level inspection, and pack-as-concat
# for building a greeting from parts.

(let ((chars (unpack "Hello")))
  (println "chars:" chars)            # => (72 101 108 108 111)
  (println "repacked:" (pack chars))) # => Hello

# String construction with pack (concatenation of string parts)
(let ((greeting (pack (list "Hello" ", " "World" "!"))))
  (println greeting))                 # => Hello, World!

# ---- 4. fmt -- template formatting -------------------------
# `fmt` takes a template string and arguments. `~A` is replaced
# by the next argument's printed representation. `~%` inserts a
# newline. This is L's alternative to sprintf / str.format().
#
# In Python terms: `(fmt "Name: ~A, Age: ~A" "Alice" 30)` is
# roughly `f"Name: {name}, Age: {age}"`. The directives are
# borrowed from Common Lisp's FORMAT function. Only `~A` (any
# value) and `~%` (newline) are shown here; check the spec for
# the full directive list.

(println (fmt "Name: ~A, Age: ~A" "Alice" 30))  # => Name: Alice, Age: 30
(println (fmt "~A + ~A = ~A" 2 3 (+ 2 3)))      # => 2 + 3 = 5
(println (fmt "Line 1~%Line 2~%Line 3"))
# => Line 1
# => Line 2
# => Line 3

# ---- 5. String predicates ---------------------------------
# `starts-with?`, `ends-with?`, `contains?` are defined in
# stdlib.l. They test string relationships and return T or NIL.
# The `?` suffix is a conventional naming hint that the function
# is a predicate -- it always returns a boolean-like value.
# This convention is borrowed from Scheme/Clojure (isX / hasX
# in JavaScript, str.startswith() in Python).

(println (starts-with? "hello world" "hello"))  # => T
(println (ends-with? "hello world" "world"))    # => T
(println (contains? "hello world" "lo w"))      # => T

# ---- 6. split / join, repeat, pad -------------------------
# `split` breaks a string at a delimiter, returning a list of
# substrings (not including the delimiter itself). `join`
# concatenates a list of strings with a delimiter between them
# -- the inverse of split. Together they let you parse and
# reassemble delimited formats like CSV or path components.
#
# `repeat` repeats a string n times. Useful for building
# separator lines, padding elements, or generating test data.
#
# `pad-left` / `pad-right` pad a string to a minimum width
# using a fill character. `(pad-left "42" 6 "0")` produces
# "000042" -- the same as Python's "42".zfill(6) or
# JavaScript's "42".padStart(6, "0"). `pad-right` is analogous.

(let ((parts (split "a,b,c,d" ",")))
  (println "split:" parts)              # => (a b c d)
  (println "joined:" (join parts "-"))) # => a-b-c-d

# `char` converts between integer codepoints and single-character
# strings. Called with an integer it returns the character string;
# called with a string it returns the codepoint integer.
(println "char 65:" (char 65))   # => A
(println "char A:" (char "A"))   # => 65

(println (repeat "ab" 3))          # => ababab
(println (pad-left "42" 6 "0"))    # => 000042
(println (pad-right "hi" 8 "."))   # => hi......

(println "Done.")
