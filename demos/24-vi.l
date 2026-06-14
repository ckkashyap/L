# ============================================================
# 24-vi.l -- Vi editor demo launcher
#
# Terminal mode: L demos/24-vi.l [filename]
# Graphics mode: L demos/24-vi.l --gfx [filename]  (native window + TTF)
#
# Implemented features:
#   Normal/insert/visual modes, d/c/y/p/P/x operators
#   Motions: h/j/k/l/w/b/e/0/^/$/G/gg/H/M/L
#   Commands: :w :q :wq :q! :e :s///g
#   Search: /pattern<Enter> to jump, ? to search back, n N repeat
#   Undo: u, Redo: Ctrl+R (via redo-pop)
#   Macros: qa-qz record, @a-@z play
# ============================================================
(load "demos/vi/vi.l")
