# File dialogs

Open, save or pick a folder the GTK 4 way -- **asynchronously** -- without
writing the dance yourself. Add one on the window (select the root): the
inspector's **File dialogs**.

| Field | Means |
|---|---|
| name | `open_doc` -> `choose_open_doc()`, the hook `on_open_doc_chosen(path)` |
| mode | **open**, **open_multiple** (the hook gets `paths`), **save**, **folder** |
| title, accept | the dialog's title and its button; empty = GTK's own |
| filters | rows like `Text=*.txt;*.md` or `Images=image/*` (a `/` = a MIME type) |
| cancel | also a hook `on_<name>_cancelled()` |

## Using one

Call `choose_open_doc();` from a hook -- a button, an action. It **returns
at once**; the dialog opens over the window, and the answer arrives later
at your hook:

```
void Editor::on_open_clicked() {
    choose_open_doc();          // returns now; the answer comes below
}

void Editor::on_open_doc_chosen(const std::string& path) {
    // read the file here
}
```

- `choose_open_doc(initial)` -- a folder to start in, a file to start on,
  or (save) a bare name.
- One at a time: a second call while it's up is refused, and logged.
- Cancel delivers nothing (or `on_<name>_cancelled()` if you asked for it).
- Close the window with the dialog up and the dialog goes with it; nothing
  is delivered to a window that's gone.
- A file that isn't local arrives as its URI.

A new app's **Open** button is wired this way already.
