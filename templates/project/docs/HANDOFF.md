# @APP@ handoff — s000 -> s001

## Next session

**`s001_first_form`**

*(@APP@ was just made by Mortimer: the workspace, the build, the scripts, and a
starter main window that builds and runs. s001 decides what @APP@ is FOR --
write it into ARCHITECTURE.md -- and lays out the first real form.)*

## Files to upload

- `s001_first_form.zip` — the workspace, made by
  `scripts/@BIN@_backup.sh s001_first_form` (the zip carries the next
  session's name). docs/ rides inside it.
- Nothing session-specific.

---

## What exists

- `@BIN@/` builds with `./build.sh` into `./build/@BIN@` and
  `./build/@BIN@_selftest`.
- `forms/MainWindow.mort` -- the starter window (header bar, a greeting, a
  button). `generated/` is Mortimer's half; `include/MainWindow.hpp` +
  `src/MainWindow.cpp` are yours.
- `install.sh` / `uninstall.sh` (~/.local by default; `--system`; `--prefix`).

## What to look at first

1. `./build.sh && ./build/@BIN@_selftest && ./build/@BIN@`
2. Click **Say hello**: the log says `MainWindow: hello clicked` (hooked).
3. Tick another signal in the form, `mortimer-gen generate`, rebuild, fire it:
   the log says `unhooked: ...` once, and `generated/MainWindow.hooks.txt` has
   the lines to paste.

---

## Standing rules

**⚠ DO NOT DELETE — carried forward verbatim from RULES.md into every handoff.**

@RULES@
