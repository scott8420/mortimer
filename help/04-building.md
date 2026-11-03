# Building and checking

In the app folder:

```
./build.sh                    # build/<app>, build/<app>_selftest; runs the undo gate
./build/<app>                 # run it
./build/<app>_selftest        # your headless checks
./install.sh                  # ~/.local (or --system)
```

## The build regenerates

Change a `forms/*.mort` or `mortimer.json` **any** way -- in Mortimer, in an
editor, by a `git pull` -- and the next build regenerates before it compiles.
It can't build a stale window. A new form's files are in that same build.

- A form with errors **stops the build**, with Mortimer's message; the last
  good `generated/` stays until you fix it.
- Without `mortimer-gen` (someone building your published app) the build
  uses `generated/` as it is -- that's why `generated/` is checked in.
- Turn it off: `cmake -B build -D<APP>_REGENERATE=OFF`. Point it at a
  `mortimer-gen` that isn't on your path: `-DMORTIMER_GEN=/path/to/it`.

## The undo gate

Every build runs `build/<app>_undo_gate`. It takes each kind of row your
app has (each form's models and the shared ones) through the app's undo the
way a user would: add a row, change each field on its own, remove it -- undo
and redo each time. If undo is broken, **the build fails**, and stays failed
until it's fixed: the user never meets the bug.

`./build/<app>_undo_gate -v` shows every check.

## The selftest

`src/selftest.cpp` is yours: the headless checks for your app's own logic
(the code in `src/core/`, which never includes GTK). Every model verb you
add deserves a check there.

## Where code goes

| Folder | What |
|---|---|
| `src/core/`, `include/core/` | your app's logic -- no GTK (the build enforces it) |
| `src/`, `include/` | your half of each window, the app, logging |
| `src/widgets/` | your promoted widget classes |
| `generated/` | Mortimer's half -- never edit |
| `forms/` | the designs |
| `resources/` | icons, images, the stylesheet, data -- compiled into the app |
