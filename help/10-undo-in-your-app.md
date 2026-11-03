# Undo in your app

Every edit a user makes to the rows in your lists and tables is an undoable
**step** -- with nothing to write. A new app has Ctrl+Z / Ctrl+Shift+Z and
Undo / Redo menu rows that name the step.

## What's a step already

- A cell edited in a table; a check, switch or field bound in a list row.
- `add_<view>(item)` and `remove_<view>(item)` -- a removed row comes back
  on undo: the same item, same id, same fields, in the same place.
- Typing in one cell is **one** step, not one per key.

## Your own edits

When your code changes an item, go through the same door, and it's a step:

```
ui::undo::edit(note, "Mark done", [&] { note->done = true; });
```

The label is what the menu says: "Undo Mark done". Every row and cell
showing that item re-shows it. Changed an item outside `edit()`? Call
`ui::undo::refresh(note)` so what shows it catches up.

## The keys

Ctrl+Z and Ctrl+Shift+Z are the **window's** keys, heard after the focused
widget has had its turn: in an Entry, Ctrl+Z undoes the typing there (the
Entry's own undo); anywhere else, it undoes the last step. Undo never steals
the key from the field you're typing in.

## The gate

Every build runs the app's **undo gate** (see *Building and checking*): it
walks every kind of row through add, edit and remove with undo and redo,
and fails the build if any of it doesn't come back exactly.
