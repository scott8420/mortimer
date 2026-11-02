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
uninstall scripts, a `.desktop` file and icons, RULES / CANON / HANDOFF in
`docs/`, and a starter window that builds and runs.

The designer window (palette, tree, canvas, properties) is next: it drives
the same core API as `mortimer-gen`.

## Where things are

| | |
|---|---|
| `include/core/Catalog.hpp` | every placeable widget: gtkmm class, properties (with setters), signals (with hook signatures), packing |
| `include/core/Form.hpp` | a form: the node tree, JSON on disk, `validate()` |
| `include/core/Codegen.hpp` | form -> `<Form>Ui` base + the one-time stubs |
| `include/core/HookScan.hpp` | reads your header: hooked / unhooked / missing / stale |
| `include/core/Project.hpp` | `mortimer.json`, generate-all, a new workspace |
| `templates/project/` | the new-app workspace, `@TOKEN@`s, compiled into the binary (`cmake/embed.cmake`) |
| `tests/KitchenSink.mort` | a form using **every** catalog entry -- the selftest gate requires it, `proof.sh` compiles it |

The app's own window is still the Cairn spine (recents, the undo stone, the
registry dump) until the designer replaces it.

## License

MIT -- see `LICENSE`. Public: https://github.com/scott8420/mortimer
