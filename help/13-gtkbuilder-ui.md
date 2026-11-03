# GtkBuilder .ui files

Glade, Cambalache and `Gtk::Builder` share the `.ui` format. Mortimer reads
and writes it.

## Import

File ▸ **Import .ui…** (or `mortimer-gen import-ui FILE.ui`). With a project
open the file becomes its next form, generated and opened; without one, an
unsaved form.

- Every class and property Mortimer's catalog knows comes across: layout,
  Grid cells, Stack and Notebook pages, header slots, menus, styles.
- Ids become C++ names: `main-box` -> `main_box`, `class` -> `class_`; a
  widget with no id gets one (`button1`).
- Your handler names are **kept** as the hook names: `new_note_cb`.
- A menu row naming `win.export-notes` becomes an action of the form, with a
  hook. An `app.*` row is your App's own.
- What Mortimer can't keep is **noted** (the log in the designer, the
  terminal from `mortimer-gen`): a widget it doesn't know (left out with
  what's inside it), a property it doesn't have.

## Export

File ▸ **Export as .ui…** (or `mortimer-gen export-ui form.mort`). A copy for
other tools -- the form stays the source.

What a `.ui` can't say -- actions and keys, file dialogs, models, window
state, a list's designed row, a promoted class -- is **listed at the top of
the file**, never dropped silently.

Export, import, export again: the same `.ui`.
