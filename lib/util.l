# util.l -- Threading macros, partial application, sets, convenience forms

# ---- Threading macros (Clojure-style) ----

# (-> x (f a) (g b)) → (g (f x a) b)
# Threads x as the FIRST argument of each form.
(dmacro -> (x . forms)
  (if (null forms) x
    (let* ((form (car forms))
           (rest (cdr forms))
           (step (if (pair form)
                   (cons (car form) (cons x (cdr form)))
                   (list form x))))
      (cons '-> (cons step rest)))))

# (->> x (f a) (g b)) → (g a (f b x))
# Threads x as the LAST argument of each form.
(dmacro ->> (x . forms)
  (if (null forms) x
    (let* ((form (car forms))
           (rest (cdr forms))
           (step (if (pair form)
                   (append form (list x))
                   (list form x))))
      (cons '->> (cons step rest)))))

# ---- Partial application ----

# (partial f arg1 arg2 ...) → (lambda (. rest) (apply f (append args rest)))
(de partial (f . bound-args)
  (let ((args bound-args)
        (fn f))
    (lambda (. rest) (apply fn (append args rest)))))

# ---- Persistent set (wrapper around pmap) ----

(de pset-new () (pmap-new))
(de pset? (s) (pmap? s))
(de pset-add (s x) (pmap-put s x T))
(de pset-del (s x) (pmap-del s x))
(de pset-member? (s x) (if (pmap-get s x) T NIL))
(de pset-count (s) (pmap-count s))
(de pset-to-list (s) (pmap-keys s))
(de pset-union (a b)
  (let ((result a)
        (items (pset-to-list b)))
    (while items
      (setq result (pset-add result (car items)))
      (setq items (cdr items)))
    result))
(de pset-intersection (a b)
  (let ((result (pset-new))
        (items (pset-to-list a)))
    (while items
      (when (pset-member? b (car items))
        (setq result (pset-add result (car items))))
      (setq items (cdr items)))
    result))
(de pset-difference (a b)
  (let ((result a)
        (items (pset-to-list b)))
    (while items
      (setq result (pset-del result (car items)))
      (setq items (cdr items)))
    result))

# Variadic constructor: (pset 1 2 3) → set containing 1, 2, 3
(de pset (. elems)
  (let ((s (pset-new)) (rest elems))
    (while rest
      (setq s (pset-add s (car rest)))
      (setq rest (cdr rest)))
    s))

# ---- when-let / if-let ----

# (when-let ((var expr)) body...) -- bind var, run body only if non-NIL
(dmacro when-let (binding . body)
  (let ((var (car (car binding)))
        (val (cadr (car binding))))
    (list 'let (list (list var val))
      (cons 'when (cons var body)))))

# (if-let ((var expr)) then else) -- bind var, branch on non-NIL
(dmacro if-let (binding then . else-forms)
  (let ((var (car (car binding)))
        (val (cadr (car binding))))
    (list 'let (list (list var val))
      (list 'if var then (if else-forms (car else-forms) NIL)))))
