# ARCHITECTURE.md — what @APP@ is

## What @APP@ is FOR

*(Write this first, in plain words: the problem, who has it, what @APP@ does
about it that nothing else does well. Everything below serves this.)*

## The shape

- **Forms** (`forms/*.mort`) are the windows. Mortimer generates their half
  (`generated/`); the hooks you override are yours (`include/`, `src/`).
- **The core** (`include/core/`, `src/core/`, `@BIN@_core`) is the model --
  GTK-free, proven by `@BIN@_selftest`. The UI reads it and writes it through
  verbs; it never reaches into widgets (docs/two-layer-seam.md).
- **Undo** ships with the first write (RULES: "Undo from the first write";
  docs/undo.md).
