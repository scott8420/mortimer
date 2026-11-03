# The designer

```
 palette | project  ||  live canvas    ||  inspector
         | widgets  ||  hooks (live)   ||
```

## The canvas

The form as **real GTK widgets**, rebuilt on every change and wearing your
app's stylesheet. Hooks are inert here: **click a widget to select it** -- it
never sees the click. Ctrl / Shift + click selects several.

Drag a widget on the canvas to move it. Where you point decides where it lands:
before, into, or after the widget under the pointer, along its parent's
direction -- a line shows the spot.

## The palette

Every widget Mortimer knows, grouped by kind.
**Click** one to pick it -- it's highlighted, nothing is made. To add it,
**double-click** it (or press Enter / Space on it): it goes into the
selection, or just after it when the selection can't hold children. Its
tooltip names where. **Right-click** one for a menu that says the same --
"Add Label after “title”". Or **drag** one onto the canvas or the tree.

**Alt+click** a type to see what it is: a live picture of the widget (in
your theme), what it's for, and the one thing to know about it. **More...**
links to its class page on docs.gtk.org and to GTK's gallery of every
widget. Greyed types answer Alt+click too -- it's how to find out what they
are before they can go anywhere.

## The project and the widgets tree

**Project** lists the app's forms and its bundled resources (icons, images,
the stylesheet, data files). **+** adds a Window, a Dialog, an About dialog
or a resource; the **pencil** renames; the **bin** moves it to `removed/`
(nothing is deleted). The pencil and the bin show when the pointer is over a
row.

**Right-click** a row for its menu: Open (a form), Copy the icon name or the
resource path, Rename..., Remove... -- and, below them, everything the **+**
adds. Right-click empty space for just the adds.

From the keyboard, click a row (it stays highlighted) or Tab to it, then:

| Key | Does |
|---|---|
| Delete, Backspace | Remove the row (Mortimer asks first) |
| F2 | Rename it |
| Menu, Shift+F10 | Its menu |

These keys belong to the list only while it has the focus; elsewhere Delete
removes the selected widget. Backspace does what Delete does (a laptop's
"delete" key sends Backspace), except in a text box, which keeps its own.

The main form and the app's logo can't be removed: their bin and the menu's
Remove are greyed.

**Widgets** is the form as a tree. Drag a row to reorder or reparent it:
the top, middle or bottom of a row means before, into or after.

**Right-click** a widget -- on the canvas or its row in the tree -- for its
menu: Cut, Copy, Paste, Duplicate, Delete, Move Up, Move Down (each with its
key), and **What is a ...?**, which shows the same card as Alt+click in the
palette. The widget is selected first, so the highlight shows what the menu
will act on; right-click one of several selected widgets and the menu acts
on all of them.

## The inspector

For the selected widget:

- **Name** -- the C++ name: the member is `m_<name>`, its hooks
  `on_<name>_<signal>`.
- **Class** -- promote it to a class of yours (see *Forms and your code*).
- **Properties** -- a **bold** one is set by the form, with a reset button;
  an unset one shows GTK's own default.
- **Packing** -- how it sits in its parent (a slot, a grid cell, a page).
- **Common** -- expand, align, margins, tooltip, CSS classes (as chips).
- **Signals** -- tick one and it becomes a **hook**; mark it *required* and
  your class won't compile until you write it.

The window itself (select the root) also has the form's **actions**, **file
dialogs** and **models**.

## The hooks pane

Live, before you save: each hook the form has, against your header --
**hooked**, **unhooked**, **MISSING** (required, not written) or **STALE**
(you wrote one the form no longer has). **Copy lines to paste** gives you the
declarations and bodies for the ones you haven't written.

## Undo

Every edit is one step: Ctrl+Z / Ctrl+Shift+Z, and the menu names the step.
Typing in a field is one step, not one per key. Several widgets deleted,
dragged or pasted together are one step.

The project's own changes are steps too, in the same history: adding,
removing or renaming a form or a resource, sharing, editing or unsharing a
model, importing a .ui. Ctrl+Z undoes whichever came last -- a widget you
added, or the icon you removed before it. Undo puts the files back exactly
as they were (a removed form comes back out of `removed/`).

Two things stop an undo, and the status line says which:

- **A file it would put back has changed since** -- you edited
  `src/Prefs.cpp`, or git pulled. Undoing would lose that, so it doesn't.
  (Files in `generated/` don't count: Mortimer makes them again.)
- **It changes the open form's file while the form has unsaved edits.**
  Save first.

Opening another form starts its widget history fresh; the project's steps
stay until you open another project. Saving is not a step.

## Changed on disk

The open form's file is watched. Edit `forms/<Form>.mort` in a text editor,
or let git pull a new version, and the designer notices:

- **Nothing unsaved here** -- it reloads the file, keeps the selection where
  the names still match, and says so. Its undo history starts again there.
- **Unsaved changes here** -- it asks: **Reload** (take the file, lose your
  changes) or **Keep Mine** (keep editing; your next Save overwrites the
  file).
- **Deleted or moved** -- it says so and keeps the form; Save writes it back.
- **Unreadable** (a half-written file, a merge conflict) -- it says why and
  keeps its copy.

Save checks too: if the file changed since the designer last read or wrote
it, Save asks before writing over it. A file only re-spaced by hand is the
same form, so it doesn't count.

When `mortimer.json` changes (a form added or dropped by a pull), the project
list and the Forms menu follow.

## Copy, cut, paste, duplicate

Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D work on whole subtrees, several at once.
Pasted widgets get fresh names; their hooks come along.
