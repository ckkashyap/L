# ============================================================
# 19-plist.l -- Property lists and OOP-style programming
#
# What this covers:
#   1. put/get: storing named properties on symbols
#   2. prop: live cell access for in-place mutation
#   3. plist: inspecting a symbol's full property list
#   4. with/:/=:/:: OOP shorthands
#   5. Building objects from symbols and plists
#   6. Contrast: plist vs pmap vs alist
#
# Run:  L demos/19-plist.l
# Spec: L.spec.txt Part VII -- Property Lists
# ============================================================

# ------------------------------------------------------------
# Section 1: What is a property list?
# ------------------------------------------------------------
# Every symbol in L has an associated alist -- a list of (key . value) pairs --
# called its property list or plist. This is built into the symbol itself, not
# a separate data structure. It is the basis for OOP-style programming in L
# (inspired by PicoLisp).

# A fresh symbol has an empty plist
(println (plist 'alice))  # => NIL

# put associates a key with a value on a symbol
(put 'alice 'name "Alice")
(put 'alice 'age 30)

# get retrieves by key; NIL if not found
(println (get 'alice 'name))  # => "Alice"
(println (get 'alice 'age))   # => 30
(println (get 'alice 'email)) # => NIL  (edge case: missing key, not an error)

# plist returns the full alist
(println (plist 'alice))      # => ((age . 30) (name . "Alice"))

# ------------------------------------------------------------
# Section 2: put updates in place
# ------------------------------------------------------------
# put on an existing key replaces the value -- the plist is not duplicated
(put 'alice 'age 31)
(println (get 'alice 'age))   # => 31

# ------------------------------------------------------------
# Section 3: prop -- the live cons cell
# ------------------------------------------------------------
# prop returns the actual (key . val) cons cell in the plist
# Mutating its cdr directly updates the plist
(put 'alice 'score 100)
(let ((cell (prop 'alice 'score)))
  (println (car cell))   # => score
  (println (cdr cell))   # => 100
  # set-cdr mutates the cdr (tail) of a cons cell in place -- used here to update the property value directly
  (set-cdr cell 200))    # mutate in place
(println (get 'alice 'score))  # => 200
(println (prop 'alice 'missing)) # => NIL  (edge case: prop on missing key)

# ------------------------------------------------------------
# Section 4: with / : / =: / :: OOP shorthands
# ------------------------------------------------------------
# (with sym body...) binds This to sym for the duration of body.
# Inside with:
#   (: key)      -- get property key from This (key is NOT evaluated -- special form)
#   (=: key val) -- put property key on This
#   (:: key)     -- return the live (key . val) cons cell from This

(put 'point 'x 10)
(put 'point 'y 20)

(with 'point
  (println (: x))    # => 10
  (println (: y))    # => 20
  (=: x 99)         # update x
  (println (: x))    # => 99
  (let ((cell (:: y)))
    (set-cdr cell 55))  # update y via live cell
  (println (: y)))   # => 55

# Changes persist outside the with block
(println (get 'point 'x))  # => 99
(println (get 'point 'y))  # => 55

# NOTE: : and :: are SPECIAL FORMS -- the key is unevaluated.
# (: x) reads the property named x, not the value of the variable x.

# ------------------------------------------------------------
# Section 5: Simulating objects
# ------------------------------------------------------------
# A symbol + plists = a lightweight object.
# Methods are just regular functions that know the symbol name.

(de make-counter (name start)
  (put name 'n start)
  name)

(de counter-inc (name)
  (put name 'n (1+ (get name 'n))))

(de counter-val (name)
  (get name 'n))

(make-counter 'c1 0)
(make-counter 'c2 100)

(counter-inc 'c1)
(counter-inc 'c1)
(counter-inc 'c2)

(println (counter-val 'c1))  # => 2
(println (counter-val 'c2))  # => 101

# Symbols are independent -- c1 and c2 do not share state

# ------------------------------------------------------------
# Section 6: Choosing the right structure
# ------------------------------------------------------------
# When to use what:
#
#   plist  -- properties ON a named symbol; OOP-style; fast by symbol
#   pmap   -- functional/immutable key→value map; safe to share/copy
#   alist  -- simple association list; O(n) lookup; fine for small data
#
# plist is best when:
#   - The "object" is naturally identified by a symbol name
#   - You need OOP-style with/:/=: syntax
#   - Properties change over time (put updates in place)
#
# pmap is best when:
#   - You need immutable updates (functional style)
#   - Multiple versions of the map must coexist
#
# alist is best when:
#   - The list is small (< 20 entries)
#   - You need portability (alist is just a list -- no special primitives)

(println "19-plist.l done")
