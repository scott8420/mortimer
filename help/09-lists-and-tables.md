# Lists and tables

## Models: the rows

A **model** is a row type: fields of **string**, **int**, **double** or
**bool**. Add one on the window (select the root): the inspector's
**Models**. A model `Note` becomes a class `NoteItem`:

```
auto note = NoteItem::create("Buy milk", 2);   // fields in order; an automatic id
m_notes_store->append(note);                   // the table shows it
```

Every item has a unique `id()` (pass your own -- a database key -- as the
last argument). Filling a store in your constructor is just `append`.

## A table: ColumnView

Give a ColumnView a **model** and it gets a column per field, sorted by
clicking a header. Its properties:

- **selection** -- single (the default), multiple, or none.
- **editable** -- the fields a user may change in place: double-click a
  cell (or Enter), type, and any click outside commits; Escape puts it back.

Its hooks hand you the row: **activated** (opened), **selected** (with
multiple: every picked row), **edited** (the row and the field).

## A list with a designed row: ListView

A ListView's one child is the **row**, designed like any widget. Give the
row's widgets **bind** = a field: a Label or an Image shows it; an input --
a CheckButton, a Switch, an Entry, a SpinButton -- shows it and *writes* it
back when the user changes it. GTK builds one row per visible line and refills it as
you scroll. The row's own hooks hand you its item.

## Adding and removing rows, undoably

```
add_notes(NoteItem::create("New"));    // a step: Ctrl+Z takes it out
remove_notes(note);                    // a step: Ctrl+Z puts it back, in its place
```

A user's edits in a cell or a bound widget are steps already. See *Undo in
your app*.

## Shared models

A model one window has can become the **project's**: the Models section's
**Share** (or `mortimer-gen share`). Every form can then show it, and
`generated/Models.hpp` holds it. Edit it once, every form follows;
**Unshare** gives each form its own copy back.

## Remembered

A table's column order, widths and sort come back as they were left, when
its window **remembers** (see *Window state*).
