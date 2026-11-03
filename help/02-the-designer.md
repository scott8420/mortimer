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
**Click** one to add it into the selection (or just after it, when the
selection can't hold children). **Drag** one onto the canvas or the tree.

## The project and the widgets tree

**Project** lists the app's forms and its bundled resources (icons, images,
the stylesheet, data files). **+** adds a Window, a Dialog, an About dialog
or a resource; the **pencil** renames; the **bin** moves it to `removed/`
(nothing is deleted).

**Widgets** is the form as a tree. Drag a row to reorder or reparent it:
the top, middle or bottom of a row means before, into or after.

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

## Copy, cut, paste, duplicate

Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D work on whole subtrees, several at once.
Pasted widgets get fresh names; their hooks come along.
