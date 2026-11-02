#include "core/Catalog.hpp"

// The widget table. Every setter and signal accessor here is spelled against
// gtkmm 4.10 (the oldest we build on); the s001 proof compiles a form that uses
// EVERY entry, so a misspelling fails the build rather than a user's project.

namespace mortimer::core {

namespace {

PropSpec b(std::string n, std::string set, std::string doc = "") {
    return {std::move(n), PropKind::Bool, std::move(set), "", false, {}, "", std::move(doc)};
}
PropSpec i(std::string n, std::string set, std::string doc = "") {
    return {std::move(n), PropKind::Int, std::move(set), "", false, {}, "", std::move(doc)};
}
PropSpec d(std::string n, std::string set, std::string def = "", bool always = false,
           std::string doc = "") {
    return {std::move(n), PropKind::Double, std::move(set), std::move(def), always, {}, "",
            std::move(doc)};
}
PropSpec s(std::string n, std::string set, std::string doc = "") {
    return {std::move(n), PropKind::String, std::move(set), "", false, {}, "", std::move(doc)};
}
PropSpec e(std::string n, std::string set,
           std::vector<std::pair<std::string, std::string>> vals, std::string doc = "") {
    return {std::move(n), PropKind::Enum, std::move(set), "", false, std::move(vals), "",
            std::move(doc)};
}
PropSpec list(std::string n, std::string set, std::string doc = "") {
    return {std::move(n), PropKind::StringList, std::move(set), "", false, {}, "",
            std::move(doc)};
}
PropSpec ref(std::string n, std::string set, std::string type, std::string doc = "") {
    return {std::move(n), PropKind::Ref, std::move(set), "", false, {}, std::move(type),
            std::move(doc)};
}

std::vector<std::pair<std::string, std::string>> orient() {
    return {{"horizontal", "Gtk::Orientation::HORIZONTAL"},
            {"vertical", "Gtk::Orientation::VERTICAL"}};
}
std::vector<std::pair<std::string, std::string>> align() {
    return {{"fill", "Gtk::Align::FILL"},     {"start", "Gtk::Align::START"},
            {"end", "Gtk::Align::END"},       {"center", "Gtk::Align::CENTER"},
            {"baseline", "Gtk::Align::BASELINE"}};
}
std::vector<std::pair<std::string, std::string>> policy() {
    return {{"automatic", "Gtk::PolicyType::AUTOMATIC"}, {"always", "Gtk::PolicyType::ALWAYS"},
            {"never", "Gtk::PolicyType::NEVER"},         {"external", "Gtk::PolicyType::EXTERNAL"}};
}

// A Scale / SpinButton range. GTK's default adjustment is 0..0 -- a slider that
// cannot move -- so lower/upper/step are ALWAYS emitted, and value comes after
// them (setting a value outside the range clamps it).
std::vector<PropSpec> range_props() {
    return {
        d("lower", "get_adjustment()->set_lower({v})", "0", true),
        d("upper", "get_adjustment()->set_upper({v})", "100", true),
        d("step", "get_adjustment()->set_step_increment({v})", "1", true),
        d("page", "get_adjustment()->set_page_increment({v})", "10", true),
        d("value", "set_value({v})"),
        i("digits", "set_digits({v})"),
    };
}

std::vector<PropSpec> window_props() {
    return {
        s("title", "set_title({v})"),
        i("default_width", "property_default_width().set_value({v})"),
        i("default_height", "property_default_height().set_value({v})"),
        b("resizable", "set_resizable({v})"),
    };
}
std::vector<SignalSpec> window_signals() {
    return {{"close_request", "signal_close_request()", "bool", "", "false", true,
             "The window is asked to close. Return true to keep it open."}};
}

std::vector<WidgetSpec> build() {
    std::vector<WidgetSpec> c;

    // ── the top levels: one per form, and only there ────────────────────────
    c.push_back({"ApplicationWindow", "Gtk::ApplicationWindow", "gtkmm/applicationwindow.h",
                 ChildKind::Window, true, window_props(), window_signals(), {},
                 "The app's main window (it owns the menu-bar / win.* actions)."});
    c.push_back({"Window", "Gtk::Window", "gtkmm/window.h", ChildKind::Window, true,
                 window_props(), window_signals(), {},
                 "A secondary window: a tool palette, an inspector, a preferences window."});

    // ── containers ──────────────────────────────────────────────────────────
    c.push_back({"Box", "Gtk::Box", "gtkmm/box.h", ChildKind::Box, false,
                 {e("orientation", "set_orientation({v})", orient()),
                  i("spacing", "set_spacing({v})"),
                  b("homogeneous", "set_homogeneous({v})")},
                 {}, {}, "Children in a row or a column."});
    c.push_back({"Grid", "Gtk::Grid", "gtkmm/grid.h", ChildKind::Grid, false,
                 {i("row_spacing", "set_row_spacing({v})"),
                  i("column_spacing", "set_column_spacing({v})"),
                  b("row_homogeneous", "set_row_homogeneous({v})"),
                  b("column_homogeneous", "set_column_homogeneous({v})")},
                 {}, {}, "Children on rows and columns; a child may span several."});
    c.push_back({"Paned", "Gtk::Paned", "gtkmm/paned.h", ChildKind::Paned, false,
                 {e("orientation", "set_orientation({v})", orient()),
                  i("position", "set_position({v})"),
                  b("wide_handle", "set_wide_handle({v})")},
                 {}, {}, "Two children with a draggable divider."});
    c.push_back({"Stack", "Gtk::Stack", "gtkmm/stack.h", ChildKind::Stack, false,
                 {e("transition", "set_transition_type({v})",
                    {{"none", "Gtk::StackTransitionType::NONE"},
                     {"crossfade", "Gtk::StackTransitionType::CROSSFADE"},
                     {"slide_left_right", "Gtk::StackTransitionType::SLIDE_LEFT_RIGHT"},
                     {"slide_up_down", "Gtk::StackTransitionType::SLIDE_UP_DOWN"}})},
                 {{"visible_child_changed", "property_visible_child().signal_changed()", "void",
                   "", "", false, "Another page is showing."}},
                 {}, "Pages, one showing at a time. A StackSidebar or code picks the page."});
    c.push_back({"StackSidebar", "Gtk::StackSidebar", "gtkmm/stacksidebar.h", ChildKind::None,
                 false, {ref("stack", "set_stack({v})", "Stack")}, {}, {},
                 "A list of a Stack's pages; clicking one shows it. No wiring needed."});
    c.push_back({"ScrolledWindow", "Gtk::ScrolledWindow", "gtkmm/scrolledwindow.h",
                 ChildKind::Single, false,
                 {e("hscrollbar", "property_hscrollbar_policy().set_value({v})", policy()),
                  e("vscrollbar", "property_vscrollbar_policy().set_value({v})", policy()),
                  b("has_frame", "set_has_frame({v})"),
                  b("propagate_natural_width", "set_propagate_natural_width({v})"),
                  b("propagate_natural_height", "set_propagate_natural_height({v})")},
                 {}, {}, "Scrollbars around one child that may be bigger than the space."});
    c.push_back({"HeaderBar", "Gtk::HeaderBar", "gtkmm/headerbar.h", ChildKind::HeaderBar, false,
                 {b("show_title_buttons", "set_show_title_buttons({v})")}, {}, {},
                 "The title bar: put it in a window's \"titlebar\" slot."});

    // ── leaves ──────────────────────────────────────────────────────────────
    c.push_back({"Label", "Gtk::Label", "gtkmm/label.h", ChildKind::None, false,
                 {b("use_markup", "set_use_markup({v})"), s("label", "set_label({v})"),
                  b("wrap", "set_wrap({v})"), b("selectable", "set_selectable({v})"),
                  d("xalign", "set_xalign({v})")},
                 {}, {}, "Text. With use_markup, Pango markup: <b>bold</b>."});
    c.push_back({"Button", "Gtk::Button", "gtkmm/button.h", ChildKind::None, false,
                 {s("label", "set_label({v})"), s("icon_name", "set_icon_name({v})"),
                  b("has_frame", "set_has_frame({v})"),
                  s("action_name", "set_action_name({v})",
                    "Run a Gio action instead of (or as well as) the clicked hook.")},
                 {{"clicked", "signal_clicked()", "void", "", "", false, "The button was pressed."}},
                 {}, "A push button."});
    c.push_back({"Entry", "Gtk::Entry", "gtkmm/entry.h", ChildKind::None, false,
                 {s("text", "set_text({v})"), s("placeholder", "set_placeholder_text({v})"),
                  b("visibility", "set_visibility({v})", "false = a password field"),
                  i("max_length", "set_max_length({v})")},
                 {{"activate", "signal_activate()", "void", "", "", false, "Enter was pressed."},
                  {"changed", "signal_changed()", "void", "", "", false, "The text changed."}},
                 {}, "One line of text."});
    c.push_back({"CheckButton", "Gtk::CheckButton", "gtkmm/checkbutton.h", ChildKind::None, false,
                 {s("label", "set_label({v})"), b("active", "set_active({v})")},
                 {{"toggled", "signal_toggled()", "void", "", "", false, "Ticked or unticked."}},
                 {}, "A tick box with a label."});
    c.push_back({"Switch", "Gtk::Switch", "gtkmm/switch.h", ChildKind::None, false,
                 {b("active", "set_active({v})")},
                 {{"active_changed", "property_active().signal_changed()", "void", "", "", false,
                   "Flipped on or off."}},
                 {}, "An on/off switch."});
    {
        auto p = range_props();
        p.insert(p.begin(), e("orientation", "set_orientation({v})", orient()));
        p.push_back(b("draw_value", "set_draw_value({v})"));
        c.push_back({"Scale", "Gtk::Scale", "gtkmm/scale.h", ChildKind::None, false, p,
                     {{"value_changed", "signal_value_changed()", "void", "", "", false,
                       "The slider moved."}},
                     {"gtkmm/adjustment.h"}, "A slider."});
    }
    c.push_back({"SpinButton", "Gtk::SpinButton", "gtkmm/spinbutton.h", ChildKind::None, false,
                 range_props(),
                 {{"value_changed", "signal_value_changed()", "void", "", "", false,
                   "The number changed."}},
                 {"gtkmm/adjustment.h"}, "A number with up/down buttons."});
    c.push_back({"DropDown", "Gtk::DropDown", "gtkmm/dropdown.h", ChildKind::None, false,
                 {list("items", "set_model(Gtk::StringList::create({list}))"),
                  i("selected", "set_selected({v})")},
                 {{"selected_changed", "property_selected().signal_changed()", "void", "", "",
                   false, "Another item was chosen."}},
                 {"gtkmm/stringlist.h"}, "Pick one of a list of strings."});
    c.push_back({"ListBox", "Gtk::ListBox", "gtkmm/listbox.h", ChildKind::ListBox, false,
                 {e("selection_mode", "set_selection_mode({v})",
                    {{"none", "Gtk::SelectionMode::NONE"},
                     {"single", "Gtk::SelectionMode::SINGLE"},
                     {"browse", "Gtk::SelectionMode::BROWSE"},
                     {"multiple", "Gtk::SelectionMode::MULTIPLE"}})},
                 {{"row_activated", "signal_row_activated()", "void", "Gtk::ListBoxRow* row", "",
                   false, "A row was double-clicked or Enter was pressed on it."},
                  {"row_selected", "signal_row_selected()", "void", "Gtk::ListBoxRow* row", "",
                   false, "The selection moved (row is null when nothing is selected)."}},
                 {"gtkmm/listboxrow.h"},
                 "A vertical list of rows, each any widget. (Long lists: a ListView, later.)"});
    c.push_back({"Image", "Gtk::Image", "gtkmm/image.h", ChildKind::None, false,
                 {s("icon_name", "set_from_icon_name({v})"), i("pixel_size", "set_pixel_size({v})")},
                 {}, {}, "An icon or picture."});
    c.push_back({"MenuButton", "Gtk::MenuButton", "gtkmm/menubutton.h", ChildKind::None, false,
                 {s("icon_name", "set_icon_name({v})"), s("label", "set_label({v})")},
                 {}, {}, "A button that drops down a menu (the menu editor comes later)."});
    return c;
}

}  // namespace

const std::vector<WidgetSpec>& catalog() {
    static const std::vector<WidgetSpec> c = build();
    return c;
}

const std::vector<PropSpec>& common_props() {
    static const std::vector<PropSpec> p = {
        b("hexpand", "set_hexpand({v})", "Take spare width."),
        b("vexpand", "set_vexpand({v})", "Take spare height."),
        e("halign", "set_halign({v})", align()),
        e("valign", "set_valign({v})", align()),
        i("margin", "set_margin({v})", "All four sides."),
        i("margin_start", "set_margin_start({v})"),
        i("margin_end", "set_margin_end({v})"),
        i("margin_top", "set_margin_top({v})"),
        i("margin_bottom", "set_margin_bottom({v})"),
        i("width_request", "property_width_request().set_value({v})"),
        i("height_request", "property_height_request().set_value({v})"),
        b("sensitive", "set_sensitive({v})", "false = greyed out."),
        b("visible", "set_visible({v})"),
        s("tooltip", "set_tooltip_text({v})"),
        list("css_classes", "add_css_class({item})", "Style classes: \"suggested-action\", ..."),
    };
    return p;
}

const WidgetSpec* find_widget(const std::string& type) {
    for (const auto& w : catalog())
        if (w.type == type) return &w;
    return nullptr;
}

const PropSpec* find_prop(const WidgetSpec& w, const std::string& name) {
    for (const auto& p : w.props)
        if (p.name == name) return &p;
    for (const auto& p : common_props())
        if (p.name == name) return &p;
    return nullptr;
}

const SignalSpec* find_signal(const WidgetSpec& w, const std::string& name) {
    for (const auto& s : w.signals)
        if (s.name == name) return &s;
    return nullptr;
}

const char* kind_name(PropKind k) {
    switch (k) {
        case PropKind::Bool:       return "bool";
        case PropKind::Int:        return "int";
        case PropKind::Double:     return "number";
        case PropKind::String:     return "string";
        case PropKind::Enum:       return "enum";
        case PropKind::StringList: return "string list";
        case PropKind::Ref:        return "widget id";
    }
    return "?";
}

const char* child_kind_name(ChildKind k) {
    switch (k) {
        case ChildKind::None:      return "none";
        case ChildKind::Single:    return "one child";
        case ChildKind::Window:    return "one child + titlebar";
        case ChildKind::Box:       return "children in order";
        case ChildKind::Grid:      return "children at column/row";
        case ChildKind::Paned:     return "start + end";
        case ChildKind::Stack:     return "pages";
        case ChildKind::HeaderBar: return "start / end / title";
        case ChildKind::ListBox:   return "rows in order";
    }
    return "?";
}

std::vector<std::string> packing_keys(ChildKind k) {
    switch (k) {
        case ChildKind::Window:    return {"slot"};
        case ChildKind::Grid:      return {"column", "row", "width", "height"};
        case ChildKind::Paned:     return {"slot", "resize", "shrink"};
        case ChildKind::Stack:     return {"name", "title", "icon_name"};
        case ChildKind::HeaderBar: return {"slot"};
        default:                   return {};
    }
}

}  // namespace mortimer::core
