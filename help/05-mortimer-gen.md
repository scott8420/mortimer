# mortimer-gen: the terminal

Everything the designer does to a project, from a terminal. `APP_DIR`
defaults to the current folder.

## Projects

| Command | Does |
|---|---|
| `mortimer-gen new <Name> [--id APP_ID] [--in DIR]` | a new app workspace at `DIR/<name>/` |
| `mortimer-gen generate [APP_DIR]` | regenerate every form; writes only what changed |
| `mortimer-gen hooks [APP_DIR]` | the hooks report: hooked / unhooked / missing / stale |
| `mortimer-gen check <form.mort>` | validate one form |
| `mortimer-gen catalog [Type]` | every widget: properties, signals, packing |

## Forms

| Command | Does |
|---|---|
| `mortimer-gen add about [APP_DIR]` | an About dialog form (+ your half) |
| `mortimer-gen remove form STEM [APP_DIR]` | the form and your halves move to `removed/` |
| `mortimer-gen rename form OLD NEW [APP_DIR]` | the form, your halves (class inside), the forms that open it |
| `mortimer-gen import-ui FILE.ui [Name] [APP_DIR]` | a GtkBuilder .ui as a new form |
| `mortimer-gen import-ui FILE.ui --to OUT.mort` | ...or as a loose form |
| `mortimer-gen export-ui <form.mort> [OUT.ui]` | a form as a GtkBuilder .ui |

## Resources

| Command | Does |
|---|---|
| `mortimer-gen resources [APP_DIR]` | what's bundled, and the name code uses for each |
| `mortimer-gen add icon\|image\|data FILE [APP_DIR]` | copy it into `resources/` and bundle it |
| `mortimer-gen add stylesheet [APP_DIR]` | `resources/<app>.css`, loaded for every window |
| `mortimer-gen remove resource FILE [APP_DIR]` | unbundle it; the file moves to `removed/` |
| `mortimer-gen rename resource FILE NAME [APP_DIR]` | forms that name the icon follow |

## Models

| Command | Does |
|---|---|
| `mortimer-gen share FORM MODEL [APP_DIR]` | a form's model becomes the project's |
| `mortimer-gen unshare MODEL [APP_DIR]` | each form showing it gets its own copy back |
| `mortimer-gen models [APP_DIR]` | the project's shared models |

## Help

| Command | Does |
|---|---|
| `mortimer-gen help` | the list of help pages |
| `mortimer-gen help <page or words>` | a page, or the pages that mention the words |
