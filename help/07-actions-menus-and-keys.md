# Actions, menus and keys

An **action** is something a window can do -- *Save*, *Refresh*, *About* --
named once and run from anywhere: a key, a menu row, a button. Add them on
the window (select the root): the inspector's **Actions**.

| Field | Means |
|---|---|
| name | `save_as` -> the action `win.save-as`, the hook `on_action_save_as()` |
| key | `<Control><Shift>s` -- GTK's spelling; empty = no key |
| title | what the keyboard-shortcuts window says for it |
| opens | another form (a dialog, About) or the shortcuts window: no hook -- it just opens it, built once |
| does | **undo** or **redo**: the app's own undo -- no hook |

A **required** action's hook is pure virtual: your class must write it.

## Menus

A MenuButton's **menu** is a list of rows:

```
Undo=win.undo
Redo=win.redo
-
Save As…=win.save-as
About Notes=win.about
```

`Label=action`, and `-` between sections. A row naming an action the form
doesn't have is an error -- add the action. `app.*` rows are the app's own
actions (your `App.cpp`).

When the menu has Undo / Redo rows, they **name the step**: "Undo Delete
Note", greyed when there's nothing to undo.

## The keyboard-shortcuts window

Every key a form gives an action is listed in a generated **Keyboard
Shortcuts** window, under the window's title, in the action's words. The key
and its row are one fact: change the key in the form and both follow.

What isn't an action -- a gesture, a key a widget handles itself -- goes in
the window's **keys** rows: `Ctrl+scroll=Zoom the canvas`. Lists and tables
add their own rows (open a row, pick several, edit a cell).

Give an action `opens` = the shortcuts window and a key (F1 or Ctrl+?) to
show it.
