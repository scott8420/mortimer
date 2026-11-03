#pragma once
// core::UiFile -- a form as a GtkBuilder .ui file (s024 m3: export).
//
// The .ui is the format Glade, Cambalache and Gtk::Builder share. Mortimer's
// forms say MORE than a .ui can (actions with keys, file dialogs, models, window
// state, promoted classes, the undo) -- those are left out, and each one left
// out is NAMED, so an export never loses something silently.
//
// One table: each catalog property's builder name is DERIVED from its setter
// ("set_placeholder_text({v})" -> placeholder-text; "property_default_width()"
// -> default-width), with the few GTK spells differently listed by hand
// (set_label_align -> label-xalign, set_from_icon_name -> icon-name). A widget
// added to the catalog exports with no new code unless its setter is unusual.
//
// GTK-FREE (the two-layer seam): text about GtkBuilder, never GtkBuilder.
#include "core/Catalog.hpp"
#include "core/Form.hpp"

#include <string>
#include <vector>

namespace mortimer::core {

struct UiExport {
    std::string xml;                   // the .ui text
    std::vector<std::string> left_out; // "actions: save (<Control>s) -- a .ui has no actions"
};

UiExport export_ui(const Form& f);

// s024 m4 -- a .ui in (Glade, Cambalache, or one Mortimer exported), a form out:
// the window (or, with none, the first widget in a new Window), each class the
// catalog knows, each property it names, signals as hooks, Grid cells, pages,
// slots, menus. Ids become C++ names, unique ("main-box" -> main_box; none ->
// button1). What it can't keep is in left_out. Throws on bad XML / no widgets.
struct UiImport {
    Form form;
    std::vector<std::string> notes;   // what was left out, and what was made for it (an action for a menu row)
};
UiImport import_ui(const std::string& xml, const std::string& form_name);
// A file stem as a form name: "main-window" -> "MainWindow".
std::string form_name_from(const std::string& stem);

// How one catalog property is written in a .ui (exposed for the selftest).
struct BuilderProp {
    enum class How {
        Plain,       // <property name="NAME">value</property>
        Adjustment,  // on the widget's GtkAdjustment (a Scale's / SpinButton's range)
        Buffer,      // on a TextView's GtkTextBuffer
        Dialog,      // on a ColorDialogButton's / FontDialogButton's dialog
        Margins,     // "margin": all four margin-* (GTK 4 has no "margin")
        Css,         // <style><class name=""/></style>
        Strings,     // a GtkStringList model (a DropDown's items)
        Menu,        // a <menu> and menu-model (a MenuButton's menu)
        Skip,        // Mortimer's own (remember, bind, model, ...)
    };
    How how = How::Skip;
    std::string name;   // the GTK property ("placeholder-text"); for Skip: why
};
BuilderProp builder_prop(const WidgetSpec& w, const PropSpec& p);
// "signal_clicked()" -> "clicked"; "property_rgba().signal_changed()" ->
// "notify::rgba"; "" when it isn't a signal of the widget itself.
std::string builder_signal(const SignalSpec& s);
// "Gtk::ColorDialogButton" -> "GtkColorDialogButton".
std::string builder_class(const WidgetSpec& w);

}  // namespace mortimer::core
