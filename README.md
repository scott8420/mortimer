# Mortimer

A GTK 4 / gtkmm 4 app designer that **owns the app**. Mortimer makes a whole
app workspace, and from each window's design (a **form**) it generates the
C++ half you never edit. Your half overrides the hooks you care about.
The idea is Qt Designer's (the UI is a generated base class), done the gtkmm
way and wired for the house style (Cairn's spine: `Named<W>` widgets, a live
registry, per-area logging, the two-layer seam).

```
./build.sh                       # -> build/mortimer, build/mortimer-gen, build/mortimer_selftest
./build/mortimer_selftest        # the headless checks (incl. the catalog gate)
tests/proof.sh                   # end to end: make an app, build it, walk the hook lifecycle
./install.sh                     # ~/.local (no sudo); --system for /usr/local
```

## The split

| | Written by | Holds |
|---|---|---|
| `generated/<Form>Ui.hpp/.cpp` | Mortimer, whenever the form changes | the widgets (`m_<id>`), properties, layout, and one **virtual hook** per ticked signal |
| `include/<Form>.hpp` + `src/<Form>.cpp` | Mortimer **once**, then you | `class <Form> : public ui::<Form>Ui` -- override the hooks you want |

Regenerating never touches your half, and a hook can't go quietly wrong:

| State | What happens |
|---|---|
| hooked | your override runs |
| unhooked | the default logs a warning (area `ui`) the first time it fires |
| **required** | the hook is pure virtual -- your class won't compile without it |
| **stale** | you override a hook the form no longer has -- `override` won't compile |

`generated/<Form>.hooks.txt` lists every hook with its state and the lines to
paste for the ones you haven't written.

## mortimer-gen (s001: the generator, from a terminal)

```
mortimer-gen new Notes [--id io.github.scott8420.Notes] [--in ~/Develop]
mortimer-gen generate [APP_DIR]       # every form; writes only what changed
mortimer-gen hooks [APP_DIR]          # the report, nothing written
mortimer-gen check forms/X.mort       # validate one form
mortimer-gen catalog [Type]           # what can be placed: properties, signals, packing
```

`new` lays out the house workspace -- `backups/` `backups/Old/` `docs/`
`fixes/` `scripts/<bin>_backup.sh` `<bin>/` -- with build / install /
uninstall scripts, a `.desktop` file and icons, and a starter window that
builds and runs. `docs/` starts empty (it is outside the app's repo -- your
notes, never pushed).

## The designer

`mortimer` (or `mortimer path/to/Form.mort`) is the designer, driving the same
core API as `mortimer-gen`:

```
 palette | tree  ||  live canvas  ||  inspector
                  ||  hooks (live) ||
```

- **Live canvas** -- the form as real gtkmm widgets, rebuilt on every change;
  hooks inert; click a widget to select it (it never sees the click).
- **Palette** -- click to add into (or just after) the selection, or drag onto
  the tree / canvas. **Tree** -- drag to reorder or reparent.
- **Inspector** -- name, properties (set = bold + reset; unset shows GTK's own
  default), packing, signals (tick for a hook, mark required = pure virtual).
- **Undo** -- every edit is a step (Ctrl+Z / Ctrl+Shift+Z); typing in a field
  is one step.
- **Projects** -- File ▸ New Project makes the whole workspace; Save in a
  project generates; the hooks panel shows hooked / unhooked / MISSING / STALE
  against your header as you edit, with the lines to paste.

## Where things are

| | |
|---|---|
| `include/core/Catalog.hpp` | every placeable widget: gtkmm class, properties (with setters), signals (with hook signatures), packing |
| `include/core/Form.hpp` | a form: the node tree, JSON on disk, `validate()` |
| `include/core/Codegen.hpp` | form -> `<Form>Ui` base + the one-time stubs |
| `include/core/HookScan.hpp` | reads your header: hooked / unhooked / missing / stale |
| `include/core/Project.hpp` | `mortimer.json`, generate-all, a new workspace, add a form, the live hooks report |
| `include/core/Design.hpp` | what the designer edits: the form flattened by stable key under the undo journal; every designer verb + drop landing |
| `include/Shell.hpp` | the designer window (the header is the index); panes in `include/designer/` |
| `templates/project/` | the new-app workspace, `@TOKEN@`s, compiled into the binary (`cmake/embed.cmake`) |
| `tests/KitchenSink.mort` | a form using **every** catalog entry -- the selftest gate requires it, `proof.sh` compiles it |

## License

MIT -- see `LICENSE`. Public: https://github.com/scott8420/mortimer
