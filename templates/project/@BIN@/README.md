# @APP@

A GTK 4 / gtkmm 4 desktop app, made with **Mortimer**.

```
./build.sh                  # -> ./build/@BIN@ and ./build/@BIN@_selftest
./build/@BIN@_selftest      # the headless core checks
./build/@BIN@
./install.sh                # ~/.local (no sudo); --system for /usr/local
./uninstall.sh              # exactly what install.sh put down; --purge for settings too
```

## The two halves

Each window is a **form**, `forms/<Form>.mort` (JSON). From it Mortimer makes
two halves:

| | Who writes it | What it holds |
|---|---|---|
| `generated/<Form>Ui.hpp/.cpp` | Mortimer, every time the form changes | the widgets (`m_<id>`), their properties and layout, and a **virtual hook** per ticked signal |
| `include/<Form>.hpp`, `src/<Form>.cpp` | Mortimer **once**, then you | `class <Form> : public ui::<Form>Ui` -- override the hooks you want |

Regenerating never touches your half. A hook can be:

- **hooked** -- you override it;
- **unhooked** -- you don't; it logs a warning (area `ui`) the first time it fires;
- **required** -- pure virtual: your class won't compile until you write it;
- **stale** -- you override a hook the form no longer has: `override` is a
  compile error, so a renamed button can't leave a dead handler behind.

`generated/<Form>.hooks.txt` lists them all, with the lines to paste.

```
mortimer-gen generate       # after changing a form (run in this folder)
mortimer-gen hooks          # just the report
```

Logging: `@BIN_UPPER@_DEBUG=debug ./build/@BIN@` (or `trace:ui` for one area).
