# Window state

Tick a window's **remember** and it comes back as the user left it:

- its size, and whether it was maximized;
- every Paned's divider;
- every Stack's page;
- every table's column order, widths and sort.

It's kept in `~/.config/<app>/window-state.json`, saved when the window
leaves the screen (closed, hidden). Nothing to write.

A new app's main window remembers already.
