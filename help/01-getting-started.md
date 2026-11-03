# Getting started

Mortimer designs GTK 4 windows and **owns the app around them**. You draw a
window -- a **form** -- and Mortimer writes the C++ that builds it. You write
only what the window *does*.

## Install

From the Mortimer source folder:

```
./build.sh              # build/mortimer, build/mortimer-gen, build/mortimer_selftest
./install.sh            # into ~/.local -- no sudo
./install.sh --system   # into /usr/local (asks for sudo)
```

It needs gtkmm 4, spdlog and CMake. On Fedora:
`sudo dnf install gtkmm4.0-devel spdlog-devel cmake gcc-c++`.
`uninstall.sh` takes away exactly what `install.sh` put down.

## Your first app

1. Start **Mortimer**. File ▸ **New Project…** (Ctrl+Shift+N), give it a name
   ("Notes") and a folder.
2. Mortimer makes a whole **workspace**: the app folder, plus `backups/`,
   `docs/` (your notes -- never pushed) and `scripts/<app>_backup.sh`.
3. The app opens with its **MainWindow** form on the canvas: a header bar with
   an Open button and a menu, a title, a Say hello button.
4. In a terminal, in the app folder: `./build.sh && ./build/notes`. It builds
   and runs the first time.

The same thing from a terminal: `mortimer-gen new Notes --in ~/Develop`.

## What a new app already has

- **Open** that opens a file chooser, and a hook that hands you the path.
- **Undo / Redo** keys and menu rows over everything a user edits in your
  lists and tables.
- **Keyboard Shortcuts** and **About** windows, made from the forms.
- Window size and state **remembered** between runs.
- A **selftest** and an **undo gate** that every build runs.
- `install.sh`, `uninstall.sh`, a `.desktop` file and an icon.

## The loop

Change the form in Mortimer, **Save**: Mortimer regenerates the window's code.
Build: your app has the new window. Your own code is never touched -- see
*Forms and your code*.
