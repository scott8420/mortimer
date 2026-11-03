# Widgets and packing

Every widget Mortimer can place is in its **catalog**: the gtkmm class, each
property it sets, each signal it can hook, how it holds children.
`mortimer-gen catalog` lists it all; `mortimer-gen catalog Button` one.

## Where a child sits: packing

A container decides how its children sit, and the inspector's **Packing**
shows only what that container understands:

| Container | Its children |
|---|---|
| Box, ListBox | in order -- move them with Alt+Up / Alt+Down or a drag |
| Grid | a cell: **column**, **row**, and how many columns / rows it spans |
| Paned | slot **start** or **end**; resize / shrink per side |
| Stack | a page: its **name**, **title** and an icon; a StackSidebar lists them |
| Notebook | a page with a tab: its **title** |
| HeaderBar | slot **start**, **end** or **title** |
| Window | the window's child, or slot **titlebar** (a HeaderBar) |
| Overlay | the main child, and slot **overlay** ones laid over it |
| CenterBox | slot **start**, **center** or **end** |
| ScrolledWindow, Frame, Expander | one child |

## Properties

A property the form sets is **bold** in the inspector, with a reset button;
an unset one shows GTK's own default and costs nothing in the generated code.
Text, numbers, switches, choices -- and pickers where a name means something:
an **icon name** offers your project's icons and the theme's, searchable;
**CSS classes** are chips, offering your stylesheet's classes and GTK's.

## Names

A widget's **name** is a C++ identifier, unique in the form. It becomes the
member `m_<name>` and the hooks `on_<name>_<signal>`. Every widget is also
registered by `<window>.<name>` -- Ctrl+Shift+D in the designer dumps the
live tree, and so can your app.

## Promoted widgets

A widget with a **Class** is yours: see *Forms and your code*.
