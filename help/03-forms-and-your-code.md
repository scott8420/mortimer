# Forms and your code

Each window is a **form**: `forms/<Name>.mort`, plain JSON. From it Mortimer
writes half of a C++ class; the other half is yours.

| File | Written by | Holds |
|---|---|---|
| `generated/<Name>Ui.hpp/.cpp` | Mortimer, every time the form changes | the widgets (`m_<id>`), their properties and layout, one **virtual hook** per ticked signal |
| `include/<Name>.hpp`, `src/<Name>.cpp` | Mortimer **once** -- then you | `class <Name> : public ui::<Name>Ui`, overriding the hooks you want |

Never edit `generated/`: it is rewritten. Mortimer never writes your half
again -- not when you regenerate, not when you add widgets.

## Hooks

Tick a signal in the inspector and the form gets a hook:
`virtual void on_save_clicked();`. Override it in your class:

```
void Editor::on_save_clicked() {
    // save the document
}
```

A hook can't go wrong quietly:

| State | What happens |
|---|---|
| **hooked** | your override runs |
| **unhooked** | a default runs and logs a warning (area `ui`) the first time it fires |
| **required** | the hook is pure virtual: your class won't compile without it |
| **stale** | you override a hook the form no longer has: `override` won't compile |

`generated/<Name>.hooks.txt` lists them all, with the lines to paste for the
ones you haven't written. `mortimer-gen hooks` prints the same report.

Rename a widget and its hooks are renamed: your old override is then
**stale**, and the compiler tells you where.

## Slots: hooks for code that isn't the class

Every hook also has a **slot**, `<hook>_slot`, a `std::function` with the
same signature. Code outside your class -- a controller, a test -- can wire
the same signal:

```
auto* win = new MainWindow();
win->on_save_clicked_slot = [&] { autosave.mark(); };
```

The hook runs first, then the slot: **both run**. A hook that returns `bool`
(close_request) answers true if either says true. A hook wired only by a slot
doesn't log "unhooked".

## Promote a widget to a class of yours

Give a widget a **Class** in the inspector ("NoteTable"). The form then
declares it as your class, and Save writes `include/widgets/NoteTable.hpp` +
`src/widgets/NoteTable.cpp` **once** -- a subclass of the GTK widget, yours to
fill in. The designer still shows and edits it as the widget it is.

## Renaming a form

Rename a form from the project list's pencil (or `mortimer-gen rename form`).
Your two files follow -- with the class inside them renamed -- and so does
every form that opens it. The originals are kept in `removed/`.
