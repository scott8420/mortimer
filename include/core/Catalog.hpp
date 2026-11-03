#pragma once
// core::Catalog -- what Mortimer knows about each GTK widget it can place.
//
// This is the one table the whole generator reads: the gtkmm class and header,
// how the widget takes children, each property (its JSON kind, the setter it
// generates), and each signal (the gtkmm signal it connects, the hook's
// signature). Add a widget here and the validator, the code generator and the
// `catalog` listing all know it -- there is no second list to keep in step.
//
// GTK-FREE on purpose (the two-layer seam): the catalog is strings ABOUT gtkmm,
// never gtkmm itself, so the generator runs headless and the selftest walks it.
//
// Setter templates are written as the call on the widget, with `{v}` for the
// rendered value: `set_label({v})`. The generator puts `m_<id>.` in front (or
// nothing, on the form's own window). A StringList setter with `{item}` is
// emitted once per item; with `{list}` it gets the whole vector.
#include <string>
#include <utility>
#include <vector>

namespace mortimer::core {

enum class PropKind { Bool, Int, Double, String, Enum, StringList, Ref };

struct PropSpec {
    std::string name;      // JSON key, e.g. "label"
    PropKind    kind;
    std::string setter;    // "set_label({v})"
    std::string def;       // default, as text; used when `always` (else omitted = GTK's own)
    bool        always = false;   // always emitted (e.g. a Scale's range -- GTK's default is 0..0)
    std::vector<std::pair<std::string, std::string>> values;   // Enum: JSON value -> C++ spelling
    std::string ref_type;  // Ref: the widget type it must point at ("Stack")
    std::string doc;
};

// How a container takes its children -- decides which packing keys a child may
// carry and what the generator emits to put it there.
enum class ChildKind {
    None,       // a leaf
    Single,     // set_child (ScrolledWindow)
    Window,     // set_child, plus one child with slot "titlebar" -> set_titlebar
    Box,        // append, in order
    Grid,       // attach(column, row, width, height)
    Paned,      // slot "start" | "end"; resize / shrink per side
    Stack,      // add(child, name, title); icon_name on the page
    HeaderBar,  // slot "start" | "end" | "title"
    ListBox,    // append, in order
    Row,        // s011: ONE child -- the row template, built once per visible row (not a member)
    Notebook,   // s022: append_page(child, title) -- pages with tabs
    Overlay,    // s022: one main child (set_child) + slot "overlay" ones (add_overlay)
    CenterBox,  // s022: slot "start" | "center" | "end", one each
};

struct SignalSpec {
    std::string name;       // JSON / hook name part: "clicked" -> on_<id>_clicked
    std::string connect;    // the gtkmm accessor: "signal_clicked()"
    std::string ret;        // "void" | "bool"
    std::string params;     // the hook's parameter list: "Gtk::ListBoxRow* row"
    std::string ret_default;// what an unhooked bool hook returns ("false")
    bool        before = false;  // connect(slot, false): run before GTK's default handler
    std::string doc;
};

struct WidgetSpec {
    std::string type;        // "Button" -- the JSON "type"
    std::string gtk_class;   // "Gtk::Button"
    std::string header;      // "gtkmm/button.h"
    ChildKind   children = ChildKind::None;
    bool        root = false;   // a form's top level (Window, ApplicationWindow) -- only there
    std::vector<PropSpec>   props;     // the type's own (common ones are separate)
    std::vector<SignalSpec> signals;
    std::vector<std::string> extra_headers;   // what a generated setter needs ("gtkmm/stringlist.h")
    std::string doc;
};

// Every placeable widget type, in palette order.
const std::vector<WidgetSpec>& catalog();
// The properties every widget has (hexpand, margins, tooltip, ...).
const std::vector<PropSpec>& common_props();

const WidgetSpec* find_widget(const std::string& type);
// The type's own property first, then the common ones; nullptr if neither.
const PropSpec* find_prop(const WidgetSpec& w, const std::string& name);
const SignalSpec* find_signal(const WidgetSpec& w, const std::string& name);

const char* kind_name(PropKind k);
const char* child_kind_name(ChildKind k);
// The packing keys a child of this kind of container may carry.
std::vector<std::string> packing_keys(ChildKind k);

}  // namespace mortimer::core
