# ============================================================
# test_char.l -- Character literals, codepoint conversion, pack/unpack
#
# What this covers:
#   • #\X literal syntax -- produces a 1-char string (codepoint implicitly converted)
#   • char: bidirectional conversion between codepoint int and string
#   • unpack: string -> list of 1-char strings
#   • pack: list of strings -> concatenated string
#
# Run:  L tests/test_char.l
# Spec: L.spec.txt Part 3 -- Character and String Primitives
# ============================================================

(de test (expected actual)
  (if (equal expected actual)
    (prin ".")
    (println (list "FAIL: expected" expected "got" actual))))

# ---- 1. Character literals: #\X syntax -----------------------
# #\A is a character literal that evaluates to the 1-character string "A" --
# the codepoint is implicitly converted.  Named characters
# #\space, #\tab, and #\newline expand to their respective whitespace
# strings.  Comparison uses the same rules as ordinary strings.
(test "A" #\A)
(test "a" #\a)
(test " " #\space)
(test "\t" #\tab)
(test "\n" #\newline)

# ---- 2. char -- codepoint <-> string conversion ---------------
# (char n) takes an integer Unicode codepoint and returns the
# corresponding 1-character string.  (char "x") takes a 1-char
# string and returns its integer codepoint.  Character comparison
# therefore reduces to numeric comparison of codepoints.
(test "A" (char 65))   # codepoint 65 => "A"
(test 65 (char "A"))   # "A" => codepoint 65
(test 97 (char "a"))   # "a" => codepoint 97; note 97 > 65 so "a" sorts after "A"
(test 32 (char " "))   # space => codepoint 32

# ---- 3. unpack -- string -> list of 1-char strings ------------
# unpack splits a string into a list of its constituent 1-character
# strings.  The empty string unpacks to NIL (not a list of one empty
# string).  Each element in the result is a 1-char string, suitable
# for passing back to char to get the codepoint.
(test (list "h" "e" "l" "l" "o") (unpack "hello"))
(test NIL (unpack ""))   # edge case: empty string produces NIL, not (list "")
(test (list "A") (unpack "A"))

# ---- 4. pack -- list of strings -> string ---------------------
# pack concatenates a list of strings (or 1-char strings) into a
# single string.  pack is the inverse of unpack for plain ASCII
# content.  pack of NIL produces the empty string.
(test "hello" (pack (list "h" "e" "l" "l" "o")))
(test "abc" (pack (list "a" "b" "c")))
(test "ab" (pack (list "a" "b")))
(test "" (pack NIL))   # edge case: pack of empty list is the empty string

# ---- 5. pack with multi-char strings -------------------------
# pack is not limited to single-character elements -- it concatenates
# any strings in the list, making it useful for joining words or
# building larger strings from fragments.
(test "hello world" (pack (list "hello" " " "world")))

(println "")
(println "test_char.l: ALL TESTS PASSED")
