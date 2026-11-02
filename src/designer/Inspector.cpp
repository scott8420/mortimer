#include "designer/Inspector.hpp"
#include "designer/GProps.hpp"
#include "core/Catalog.hpp"

#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/window.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/adjustment.h>
#include <glibmm/markup.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mortimer::designer {

namespace {

using core::PropKind;
using core::Value;

std::string join(const core::StringList& l) {
    std::string out;
    for (const auto& s : l) out += (out.empty() ? "" : ", ") + s;
    return out;
}
core::StringList split(const std::string& s) {
    core::StringList out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto a = item.find_first_not_of(" \t");
        const auto b = item.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
    }
    return out;
}
double dbl(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<double>(std::get<std::int64_t>(v));
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? 1 : 0;
    return 0;
}

// What a packing key is, given the parent's kind.
core::PropSpec pack_spec(core::ChildKind k, const std::string& key) {
    core::PropSpec p;
    p.name = key;
    if (key == "column" || key == "row" || key == "width" || key == "height") p.kind = PropKind::Int;
    else if (key == "resize" || key == "shrink") p.kind = PropKind::Bool;
    else if (key == "slot") {
        p.kind = PropKind::Enum;
        if (k == core::ChildKind::Window)    p.values = {{"", ""}, {"titlebar", ""}};
        if (k == core::ChildKind::Paned)     p.values = {{"start", ""}, {"end", ""}};
        if (k == core::ChildKind::HeaderBar) p.values = {{"start", ""}, {"end", ""}, {"title", ""}};
    } else p.kind = PropKind::String;
    return p;
}
std::optional<Value> pack_default(const std::string& key) {
    if (key == "width" || key == "height") return Value(std::int64_t{1});
    if (key == "column" || key == "row")   return Value(std::int64_t{0});
    if (key == "resize") return Value(true);
    if (key == "shrink") return Value(false);
    if (key == "slot")   return Value(std::string());
    return std::nullopt;
}

}  // namespace

// A wheel over a spin button changes its value -- in a scrolling inspector
// that means scrolling past a row edits it (found driving s002: margin went to
// -1). Until the editor has the keyboard, the wheel scrolls the pane instead.
void Inspector::no_wheel_unless_focused(Gtk::Widget& w) {
    auto wheel = Gtk::EventControllerScroll::create();
    wheel->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    wheel->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    wheel->signal_scroll().connect([this, &w](double, double dy) {
        auto* win = dynamic_cast<Gtk::Window*>(w.get_root());
        const Gtk::Widget* f = win ? win->get_focus() : nullptr;
        if (f && (f == &w || f->is_ancestor(w))) return false;   // focused: the wheel is the editor's
        auto adj = get_vadjustment();
        adj->set_value(adj->get_value() + dy * 48);
        return true;
    }, false);
    w.add_controller(wheel);
}

Inspector::Inspector(core::Design& d, core::DesignJournal& j)
    : widgets::ScrolledWindow("designer.inspector"),
      m_d(d), m_j(j),
      m_page("designer.inspector.page", Gtk::Orientation::VERTICAL) {
    m_page.add_css_class("mortimer-inspector");
    set_child(m_page);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_size_request(330, -1);
}

bool Inspector::show(const std::string& sel) {
    const auto r = m_d.get(sel);
    std::string ptype;
    if (r && !r->parent.empty())
        if (auto p = m_d.get(r->parent)) ptype = p->type;
    // The action pickers' choices and the Actions rows are built from the
    // form's action NAMES: a name added / removed / renamed rebuilds.
    std::string sig;
    if (const auto f = m_d.get(core::Design::kForm))
    {
        for (const auto& a : f->actions) sig += a.name + ">" + a.opens + ",";   // s010: and what it opens
        for (const auto& t : m_targets) sig += "~" + t;                        // and what it could open
        sig += "|";   // s005: and the file dialogs' names
        for (const auto& x : f->files) sig += x.name + ",";
        sig += "|";   // s006: and the models' names (the model picker's choices)
        for (const auto& m : f->models) {   // and their fields (a row each)
            sig += m.name + ":";
            for (const auto& x : m.fields) sig += x.name + ",";
            sig += ";";
        }
        // s007: a ColumnView's editable checks are its model's fields -- picking
        // another model rebuilds them.
        if (r && r->type == "ColumnView")
            if (const auto it = r->props.find("model"); it != r->props.end() && std::holds_alternative<std::string>(it->second))
                sig += "|view:" + std::get<std::string>(it->second);
    }
    if (sel != m_key || !r || r->type != m_type || ptype != m_parent_type || sig != m_actions_sig) {
        m_actions_sig = sig;
        m_key = sel;
        m_type = r ? r->type : "";
        m_parent_type = ptype;
        rebuild();
        refresh();
        return true;
    }
    refresh();
    return false;
}

void Inspector::refresh() {
    const auto r = m_d.get(m_key);
    if (!r) return;
    m_painting = true;
    for (auto& row : m_rows) row.paint(*r);
    m_painting = false;
}

void Inspector::rebuild() {
    // Tearing down a focused Entry fires its focus-leave: that is not a user choice.
    m_rebuilding = true;
    while (auto* c = m_page.get_first_child()) m_page.remove(*c);
    m_rebuilding = false;
    m_rows.clear();
    m_grid = nullptr;
    m_line = 0;

    const auto r = m_d.get(m_key);
    if (!r) {
        auto* l = Gtk::make_managed<Gtk::Label>("Nothing selected.\nPick a widget in the tree or on the canvas.");
        l->add_css_class("dim-label");
        l->set_margin_top(24);
        m_page.append(*l);
        return;
    }
    const core::WidgetSpec* w = core::find_widget(r->type);

    // Who it is.
    auto* head = Gtk::make_managed<Gtk::Label>();
    head->set_markup("<b>" + Glib::Markup::escape_text(r->type) + "</b>");
    head->set_xalign(0);
    head->set_margin_top(8);
    m_page.append(*head);
    if (w && !w->doc.empty()) {
        auto* doc = Gtk::make_managed<Gtk::Label>(w->doc);
        doc->add_css_class("doc");
        doc->set_wrap(true);
        doc->set_xalign(0);
        m_page.append(*doc);
    }
    const bool is_root = r->parent.empty();
    section("Name", "The C++ name: member m_<name>, hooks on_<name>_<signal>.");
    name_row(false);
    if (is_root) {
        section("Form", "The class this form becomes: <Name>Ui (generated) and <Name> (yours).");
        name_row(true);
        section("Actions", "win.<name>: a menu row, a button or its key runs it. It runs its hook "
                           "on_action_<name>(), or opens a window (built once, hides on close). The second "
                           "line's words are its row in the keyboard-shortcuts window.");
        actions_rows();
        section("File dialogs", "Gtk::FileDialog: call choose_<name>() from a hook; it returns at once, and "
                                "the answer arrives at on_<name>_chosen(path) -- Cancel at "
                                "on_<name>_cancelled() if ticked.");
        files_rows();
        section("Models", "Row types for tables: <Name>Item, a C++ member per field and a unique id() "
                          "(automatic, or yours). A ColumnView shows one, a column per field.");
        models_rows();
    }
    if (!w) return;

    if (!w->props.empty()) {
        section("Properties");
        for (const auto& p : w->props) prop_row(r->type, p.name, false);
    }
    if (!is_root) {
        const auto pr = m_d.get(r->parent);
        const core::WidgetSpec* pw = pr ? core::find_widget(pr->type) : nullptr;
        if (pw) {
            const auto keys = core::packing_keys(pw->children);
            if (!keys.empty()) {
                section("Packing", "How it sits in its " + pr->type + ".");
                for (const auto& k : keys) prop_row(pr->type, k, true);
            }
        }
    }
    section("Common");
    for (const auto& p : core::common_props()) prop_row(r->type, p.name, false);

    if (!w->signals.empty()) {
        section("Signals", "Tick one to get a hook to override; Required makes it pure virtual.");
        for (const auto& s : w->signals) signal_row(r->type, s.name);
    }
}

void Inspector::section(const std::string& title, const std::string& doc) {
    auto* l = Gtk::make_managed<Gtk::Label>(title);
    l->add_css_class("section");
    l->set_xalign(0);
    m_page.append(*l);
    if (!doc.empty()) {
        auto* d = Gtk::make_managed<Gtk::Label>(doc);
        d->add_css_class("doc");
        d->set_wrap(true);
        d->set_xalign(0);
        m_page.append(*d);
    }
    m_grid = Gtk::make_managed<Gtk::Grid>();
    m_grid->set_column_spacing(8);
    m_grid->set_row_spacing(4);
    m_grid->set_margin_top(4);
    m_page.append(*m_grid);
    m_line = 0;
}

void Inspector::commit_prop(const std::string& prop, bool pack, const std::optional<Value>& v) {
    if (m_painting || m_rebuilding) return;
    if (pack) core::design::set_pack(m_j, m_d, m_key, prop, v);
    else      core::design::set_prop(m_j, m_d, m_key, prop, v);
}

void Inspector::name_row(bool form) {
    auto* e = Gtk::make_managed<Gtk::Entry>();
    e->set_hexpand(true);
    e->set_name(form ? "designer.inspector.form_name" : "designer.inspector.name");
    auto* why = Gtk::make_managed<Gtk::Label>();
    why->add_css_class("why");
    why->set_xalign(0);
    why->set_wrap(true);
    why->set_visible(false);
    m_grid->attach(*e, 0, m_line++, 3, 1);
    m_grid->attach(*why, 0, m_line++, 3, 1);
    // Bound to THIS row's widget: by the time a focus-leave arrives the
    // selection may already have moved on (s002: "sidebar is taken").
    auto commit = [this, e, why, form, key = m_key] {
        if (m_painting || m_rebuilding) return;
        const std::string text = e->get_text();
        const auto r = m_d.get(key);
        if (!r || text == (form ? m_d.form_name() : r->name)) return;
        std::string err;
        const bool ok = form ? core::design::rename_form(m_j, m_d, text, &err)
                             : core::design::rename(m_j, m_d, key, text, &err);
        why->set_text(err);
        why->set_visible(!ok && !err.empty());
        if (!ok && !err.empty()) m_said.emit(err);
    };
    e->signal_activate().connect(commit);
    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_leave().connect(commit);
    e->add_controller(focus);
    m_rows.push_back({[this, e, why, form](const core::Rec& r) {
        // Not while it's being typed in: the text there is the user's, not yet a step.
        if (e->has_focus()) return;
        const std::string want = form ? m_d.form_name() : r.name;
        if (e->get_text().raw() != want) e->set_text(want);
        why->set_visible(false);
    }});
}

// s011 -- is the selection inside a ListView's row, and which model does the view show?
Inspector::RowModel Inspector::row_model() const {
    RowModel out;
    const auto r = m_d.get(m_key);
    if (!r) return out;
    for (std::string up = r->parent; !up.empty() && up != core::Design::kForm;) {
        const auto u = m_d.get(up);
        if (!u) break;
        if (u->type == "ListView") {
            out.in_row = true;
            const auto it = u->props.find("model");
            const auto f = m_d.get(core::Design::kForm);
            if (f && it != u->props.end() && std::holds_alternative<std::string>(it->second))
                for (const auto& m : f->models)
                    if (m.name == std::get<std::string>(it->second)) out.model = m;
            break;
        }
        up = u->parent;
    }
    return out;
}

void Inspector::prop_row(const std::string& type, const std::string& prop, bool pack) {
    core::PropSpec spec;
    std::optional<Value> dflt;
    if (pack) {
        const core::WidgetSpec* pw = core::find_widget(type);
        spec = pack_spec(pw ? pw->children : core::ChildKind::None, prop);
        dflt = pack_default(prop);
    } else {
        const core::WidgetSpec* w = core::find_widget(type);
        const core::PropSpec* ps = w ? core::find_prop(*w, prop) : nullptr;
        if (!ps) return;
        spec = *ps;
        dflt = gprops::default_of(type, prop);
        // s011: "bind" means something only inside a ListView's row -- elsewhere
        // it isn't offered; there it picks one of the view's model's fields.
        if (prop == "bind") {
            if (!row_model().in_row) return;
            spec.kind = PropKind::Ref;
            spec.ref_type = "@field";
        }
        if (!dflt && !spec.def.empty()) {
            if (spec.kind == PropKind::Double) dflt = Value(std::stod(spec.def));
            else if (spec.kind == PropKind::Int) dflt = Value(static_cast<std::int64_t>(std::stoll(spec.def)));
            else if (spec.kind == PropKind::Enum) dflt = Value(spec.def);   // s007: selection -> single
        }
    }

    auto* label = Gtk::make_managed<Gtk::Label>(prop);
    label->add_css_class("prop");
    label->set_xalign(0);
    if (!spec.doc.empty()) label->set_tooltip_text(spec.doc);
    auto* reset = Gtk::make_managed<Gtk::Button>();
    reset->set_icon_name("edit-clear-symbolic");
    reset->set_has_frame(false);
    reset->set_tooltip_text("Unset: back to GTK's default");
    reset->set_focusable(false);
    reset->signal_clicked().connect([this, prop, pack] { commit_prop(prop, pack, std::nullopt); });

    // s003: two properties get their own editors.
    if (!pack && prop == "menu") { menu_rows(label, reset); return; }
    if (!pack && prop == "editable" && type == "ColumnView") { editable_rows(label, reset); return; }
    if (!pack && prop == "action_name") {   // pick one of the form's actions (or app.quit)
        spec.kind = PropKind::Ref;
        spec.ref_type = "@action";
    }

    Gtk::Widget* editor = nullptr;
    std::function<void(const std::optional<Value>&)> show;   // paint the editor's value

    switch (spec.kind) {
        case PropKind::Bool: {
            auto* c = Gtk::make_managed<Gtk::CheckButton>();
            c->signal_toggled().connect([this, c, prop, pack] { commit_prop(prop, pack, Value(c->get_active())); });
            show = [c](const std::optional<Value>& v) { c->set_active(v && std::holds_alternative<bool>(*v) && std::get<bool>(*v)); };
            editor = c;
            break;
        }
        case PropKind::Int:
        case PropKind::Double: {
            const bool is_int = spec.kind == PropKind::Int;
            // Sizes that can't go below zero don't offer -1 (GTK's "unset" for the others).
            const bool natural = prop.starts_with("margin") || prop.ends_with("spacing") || prop == "digits" ||
                                 prop == "max_length" || prop == "width" || prop == "height" ||
                                 prop == "column" || prop == "row";
            const double lo = is_int ? (natural ? 0 : -1) : -1e6;
            auto adj = Gtk::Adjustment::create(0, lo, is_int ? 100000 : 1e6, is_int ? 1 : 0.05, is_int ? 10 : 1);
            auto* s = Gtk::make_managed<Gtk::SpinButton>(adj, 1, is_int ? 0 : 2);
            s->set_hexpand(true);
            no_wheel_unless_focused(*s);
            s->signal_value_changed().connect([this, s, prop, pack, is_int] {
                commit_prop(prop, pack, is_int ? Value(static_cast<std::int64_t>(std::llround(s->get_value())))
                                               : Value(s->get_value()));
            });
            show = [s](const std::optional<Value>& v) { s->set_value(v ? dbl(*v) : 0); };
            editor = s;
            break;
        }
        case PropKind::String:
        case PropKind::StringList: {
            const bool list = spec.kind == PropKind::StringList;
            auto* e = Gtk::make_managed<Gtk::Entry>();
            e->set_hexpand(true);
            if (list) e->set_placeholder_text("a, b, c");
            e->signal_changed().connect([this, e, prop, pack, list] {
                const std::string t = e->get_text();
                if (t.empty()) commit_prop(prop, pack, std::nullopt);
                else if (list) commit_prop(prop, pack, Value(split(t)));
                else commit_prop(prop, pack, Value(t));
            });
            show = [e, list](const std::optional<Value>& v) {
                std::string want;
                if (v && std::holds_alternative<std::string>(*v)) want = std::get<std::string>(*v);
                if (v && list && std::holds_alternative<core::StringList>(*v)) want = join(std::get<core::StringList>(*v));
                if (e->get_text().raw() != want) e->set_text(want);
            };
            editor = e;
            break;
        }
        case PropKind::Enum:
        case PropKind::Ref: {
            std::vector<std::string> values;
            if (spec.kind == PropKind::Enum) {
                for (const auto& [json, cpp] : spec.values) values.push_back(json);
            } else {
                values.push_back("");   // (none)
                if (spec.ref_type == "@action") {
                    if (const auto f = m_d.get(core::Design::kForm))
                        for (const auto& a : f->actions) values.push_back("win." + core::gio_action_name(a.name));
                    values.push_back("app.quit");
                    values.push_back("window.close");   // s010: GTK's own -- closes (hides) this window
                    // A value typed by hand elsewhere stays choosable.
                    if (const auto r = m_d.get(m_key))
                        if (const auto it = r->props.find(prop); it != r->props.end() &&
                            std::holds_alternative<std::string>(it->second) &&
                            std::find(values.begin(), values.end(), std::get<std::string>(it->second)) == values.end())
                            values.push_back(std::get<std::string>(it->second));
                } else if (spec.ref_type == "@field") {   // s011: a field this widget can show
                    if (const auto rm = row_model(); rm.model)
                        for (const auto& x : rm.model->fields)
                            if (core::bind_refusal(type, x.type).empty()) values.push_back(x.name);
                    if (const auto r = m_d.get(m_key))   // a field it no longer fits stays visible
                        if (const auto it = r->props.find(prop); it != r->props.end() &&
                            std::holds_alternative<std::string>(it->second) &&
                            std::find(values.begin(), values.end(), std::get<std::string>(it->second)) == values.end())
                            values.push_back(std::get<std::string>(it->second));
                } else if (spec.ref_type == "@model") {   // s006: one of the form's models
                    if (const auto f = m_d.get(core::Design::kForm))
                        for (const auto& m : f->models) values.push_back(m.name);
                    if (const auto r = m_d.get(m_key))   // a name the form no longer has stays visible
                        if (const auto it = r->props.find(prop); it != r->props.end() &&
                            std::holds_alternative<std::string>(it->second) &&
                            std::find(values.begin(), values.end(), std::get<std::string>(it->second)) == values.end())
                            values.push_back(std::get<std::string>(it->second));
                } else {
                    for (const auto& k : m_d.all())
                        if (const auto o = m_d.get(k); o && o->type == spec.ref_type) values.push_back(o->name);
                }
            }
            std::vector<Glib::ustring> shown;
            for (const auto& v : values)
                shown.push_back(v.empty() ? (spec.kind == PropKind::Ref ? "(none)" : "(child)") : v);
            auto* dd = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
            dd->set_hexpand(true);
            no_wheel_unless_focused(*dd);
            dd->property_selected().signal_changed().connect([this, dd, values, prop, pack, ref = spec.kind == PropKind::Ref] {
                const auto i = dd->get_selected();
                if (i >= values.size()) return;
                if (ref && values[i].empty()) commit_prop(prop, pack, std::nullopt);
                else commit_prop(prop, pack, Value(values[i]));
            });
            show = [dd, values](const std::optional<Value>& v) {
                const std::string want = v && std::holds_alternative<std::string>(*v) ? std::get<std::string>(*v) : "";
                for (std::size_t i = 0; i < values.size(); ++i)
                    if (values[i] == want) { dd->set_selected(static_cast<guint>(i)); return; }
                dd->set_selected(GTK_INVALID_LIST_POSITION);
            };
            editor = dd;
            break;
        }
    }
    if (!editor) return;
    editor->set_name("designer.inspector." + std::string(pack ? "pack." : "prop.") + prop);
    m_grid->attach(*label, 0, m_line);
    m_grid->attach(*editor, 1, m_line);
    m_grid->attach(*reset, 2, m_line);
    ++m_line;

    m_rows.push_back({[label, reset, show, prop, pack, dflt](const core::Rec& r) {
        const auto& m = pack ? r.pack : r.props;
        const auto it = m.find(prop);
        const bool set = it != m.end();
        if (set) label->add_css_class("set");
        else     label->remove_css_class("set");
        reset->set_opacity(set ? 1.0 : 0.0);
        reset->set_sensitive(set);
        show(set ? std::optional<Value>(it->second) : dflt);
    }});
}

// ── s007: a ColumnView's editable fields ────────────────────────────────────
// One check per field of its model (in the model's order); a tick is one step
// of its own. A name the list holds that the model doesn't have is shown too
// (struck through, so it can be unticked) -- validate says what's wrong.
void Inspector::editable_rows(Gtk::Label* label, Gtk::Button* reset) {
    const auto r = m_d.get(m_key);
    const auto f = m_d.get(core::Design::kForm);
    if (!r || !f) return;
    const core::ModelDef* md = nullptr;
    if (const auto it = r->props.find("model"); it != r->props.end() && std::holds_alternative<std::string>(it->second))
        for (const auto& m : f->models)
            if (m.name == std::get<std::string>(it->second)) md = &m;
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("designer.inspector.prop.editable");
    box->set_hexpand(true);
    label->set_valign(Gtk::Align::START);   // beside the first check, not the middle of the list
    label->set_margin_top(4);
    m_grid->attach(*label, 0, m_line);
    m_grid->attach(*box, 1, m_line);
    m_grid->attach(*reset, 2, m_line);
    ++m_line;
    if (!md) {
        auto* none = Gtk::make_managed<Gtk::Label>("pick a model first");
        none->add_css_class("dim-label");
        none->set_xalign(0);
        box->append(*none);
    }
    std::vector<std::string> names;
    if (md) for (const auto& x : md->fields) names.push_back(x.name);
    for (const auto& e : core::view_editable(core::Node{r->type, r->name, r->props, {}, {}, {}}))
        if (std::find(names.begin(), names.end(), e) == names.end()) names.push_back(e);
    const std::size_t known = md ? md->fields.size() : 0;
    auto checks = std::make_shared<std::vector<std::pair<std::string, Gtk::CheckButton*>>>();
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string name = names[i];
        auto* c = Gtk::make_managed<Gtk::CheckButton>(name);
        c->set_name("designer.inspector.editable." + name);
        if (i >= known) {
            c->add_css_class("stale");
            c->set_tooltip_text("\"" + name + "\" isn't a field of the model -- untick it");
        } else {
            c->set_tooltip_text("Edits in place; then on_" + r->name + "_edited(item, \"" + name + "\")");
        }
        box->append(*c);
        checks->push_back({name, c});
        c->signal_toggled().connect([this, checks] {
            if (m_painting || m_rebuilding) return;
            core::StringList now;   // in the model's order, stale names last
            for (const auto& [n, w] : *checks) if (w->get_active()) now.push_back(n);
            if (now.empty()) core::design::set_prop(m_j, m_d, m_key, "editable", std::nullopt, false);
            else core::design::set_prop(m_j, m_d, m_key, "editable", Value(now), false);
        });
    }
    m_rows.push_back({[label, reset, checks](const core::Rec& rec) {
        const auto ed = core::view_editable(core::Node{rec.type, rec.name, rec.props, {}, {}, {}});
        const bool set = !ed.empty();
        if (set) label->add_css_class("set");
        else     label->remove_css_class("set");
        reset->set_opacity(set ? 1.0 : 0.0);
        reset->set_sensitive(set);
        for (const auto& [n, w] : *checks)
            w->set_active(std::find(ed.begin(), ed.end(), n) != ed.end());
    }});
}

// ── s003: the form's actions ────────────────────────────────────────────────
// One row per action: name (commits on Enter / leaving it -- a rename carries
// the menu rows that name it), key, required, delete. Every edit a verb.
void Inspector::actions_rows() {
    const auto f = m_d.get(core::Design::kForm);
    if (!f) return;
    for (const auto& a0 : f->actions) {
        const std::string name = a0.name;
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_hexpand(true);
        e->set_tooltip_text(a0.opens.empty() ? "on_action_" + name + "()  ·  win." + core::gio_action_name(name)
                                             : "win." + core::gio_action_name(name) + " opens " +
                                                   (a0.opens == core::kShortcutsTarget ? std::string("the shortcuts window")
                                                                                       : a0.opens));
        auto* key = Gtk::make_managed<Gtk::Entry>();
        key->set_placeholder_text("<Control>s");
        key->set_width_chars(10);
        key->set_tooltip_text("The key, in GTK's spelling: <Control>s, <Control><Shift>z, F5. Empty = none.");
        auto* req = Gtk::make_managed<Gtk::CheckButton>();
        req->set_tooltip_text(a0.opens.empty() ? "Required: the hook is pure virtual"
                                               : "It opens a window -- there is no hook to require");
        req->set_sensitive(a0.opens.empty());
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->set_has_frame(false);
        del->set_tooltip_text("Delete the action (and the menu rows that run it)");
        m_grid->attach(*e, 0, m_line);
        m_grid->attach(*key, 1, m_line);
        auto* tail = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
        tail->append(*req);
        tail->append(*del);
        m_grid->attach(*tail, 2, m_line++);

        // s010 -- the second line: the shortcuts window's words for it, and what
        // it does -- run its hook, or open a window (built once, hides on close).
        auto* title = Gtk::make_managed<Gtk::Entry>();
        title->set_hexpand(true);
        title->set_tooltip_text("Its row in the keyboard-shortcuts window. Empty = the menu row's label, "
                                "else the name made readable.");
        std::vector<std::string> opens{"", core::kShortcutsTarget};
        for (const auto& t : m_targets) opens.push_back(t);
        if (std::find(opens.begin(), opens.end(), a0.opens) == opens.end()) opens.push_back(a0.opens);   // stale: shown
        std::vector<Glib::ustring> shown;
        for (const auto& o : opens)
            shown.push_back(o.empty() ? "runs its hook" : o == core::kShortcutsTarget ? "opens Shortcuts" : "opens " + o);
        auto* op = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
        op->set_tooltip_text("Run on_action_" + name + "(), or open a window of the project: built the first "
                             "time, then shown again (the X hides it). No hook, no code.");
        no_wheel_unless_focused(*op);
        m_grid->attach(*title, 0, m_line);
        m_grid->attach(*op, 1, m_line++, 2, 1);

        auto current = [this, name]() -> std::optional<core::ActionDef> {
            if (const auto f = m_d.get(core::Design::kForm))
                for (const auto& a : f->actions) if (a.name == name) return a;
            return std::nullopt;
        };
        title->signal_changed().connect([this, title, name, current] {
            if (m_painting || m_rebuilding) return;
            if (auto a = current()) { a->title = title->get_text(); core::design::set_action(m_j, m_d, name, *a); }
        });
        op->property_selected().signal_changed().connect([this, op, opens, name, current] {
            if (m_painting || m_rebuilding) return;
            const auto i = op->get_selected();
            if (i >= opens.size()) return;
            if (auto a = current(); a && a->opens != opens[i]) {
                a->opens = opens[i];
                if (!a->opens.empty()) a->required = false;   // nothing to require
                std::string why;
                if (!core::design::set_action(m_j, m_d, name, *a, &why) && !why.empty()) m_said.emit(why);
            }
        });
        auto rename = [this, e, name, current] {
            if (m_painting || m_rebuilding) return;
            auto a = current();
            if (!a || e->get_text().raw() == name) return;
            a->name = e->get_text();
            std::string why;
            if (!core::design::set_action(m_j, m_d, name, *a, &why) && !why.empty()) m_said.emit(why);
        };
        e->signal_activate().connect(rename);
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect(rename);
        e->add_controller(focus);
        key->signal_changed().connect([this, key, name, current] {
            if (m_painting || m_rebuilding) return;
            if (auto a = current()) { a->accel = key->get_text(); core::design::set_action(m_j, m_d, name, *a); }
        });
        req->signal_toggled().connect([this, req, name, current] {
            if (m_painting || m_rebuilding) return;
            if (auto a = current()) { a->required = req->get_active(); core::design::set_action(m_j, m_d, name, *a); }
        });
        del->signal_clicked().connect([this, name] { core::design::remove_action(m_j, m_d, name); });
        m_rows.push_back({[this, e, key, req, title, op, opens, current](const core::Rec&) {
            const auto a = current();
            if (!a) return;
            if (!e->has_focus() && e->get_text().raw() != a->name) e->set_text(a->name);
            if (key->get_text().raw() != a->accel) key->set_text(a->accel);
            req->set_active(a->required);
            req->set_sensitive(a->opens.empty());
            if (title->get_text().raw() != a->title) title->set_text(a->title);
            auto bare = *a;   // what it would say with no title of its own
            bare.title.clear();
            title->set_placeholder_text(core::action_title(m_d.to_form(), bare));
            const auto at = std::find(opens.begin(), opens.end(), a->opens);
            if (at != opens.end() && op->get_selected() != static_cast<guint>(at - opens.begin()))
                op->set_selected(static_cast<guint>(at - opens.begin()));
        }});
    }
    auto* add = Gtk::make_managed<Gtk::Button>();
    auto* add_in = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    add_in->append(*Gtk::make_managed<Gtk::Image>(Glib::ustring("list-add-symbolic")));
    add_in->append(*Gtk::make_managed<Gtk::Label>("Add action"));
    add->set_child(*add_in);
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this] { core::design::add_action(m_j, m_d); });
    m_grid->attach(*add, 0, m_line++, 3, 1);
}

// ── s005: the form's file dialogs ───────────────────────────────────────────
// A small block per dialog: name (commits on Enter / leaving it) + mode +
// delete; title + accept label; filters (a comma list of "Label=*.a;*.b");
// the cancel hook and required. Every edit is set_file (a field's typing
// folds into one step); the block repaints in place, never under the cursor.
void Inspector::files_rows() {
    const auto f = m_d.get(core::Design::kForm);
    if (!f) return;
    // A GTK4 Entry's keyboard focus sits on its inner text: ask the child too.
    auto typing = [](Gtk::Widget* w) { return w->has_focus() || gtk_widget_get_focus_child(w->gobj()) != nullptr; };
    auto join = [](const core::StringList& l) {
        std::string s;
        for (const auto& x : l) s += (s.empty() ? "" : ", ") + x;
        return s;
    };
    auto split = [](const std::string& s) {
        core::StringList out;
        std::stringstream ss(s);
        std::string one;
        while (std::getline(ss, one, ',')) {
            while (!one.empty() && one.front() == ' ') one.erase(one.begin());
            while (!one.empty() && one.back() == ' ') one.pop_back();
            if (!one.empty()) out.push_back(one);
        }
        return out;
    };
    const auto& modes = core::file_modes();
    for (const auto& d0 : f->files) {
        const std::string name = d0.name;
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_hexpand(true);
        e->set_margin_top(6);
        e->set_tooltip_text("choose_" + name + "()  ->  on_" + name + "_chosen()");
        std::vector<Glib::ustring> shown(modes.begin(), modes.end());
        auto* mode = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
        mode->set_margin_top(6);
        mode->set_tooltip_text("open: one file  ·  open_multiple: several (the hook gets paths)  ·  "
                               "save: a file to write  ·  folder: a folder");
        no_wheel_unless_focused(*mode);
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->set_has_frame(false);
        del->set_margin_top(6);
        del->set_tooltip_text("Delete the file dialog");
        m_grid->attach(*e, 0, m_line);
        m_grid->attach(*mode, 1, m_line);
        m_grid->attach(*del, 2, m_line++);

        auto* title = Gtk::make_managed<Gtk::Entry>();
        title->set_placeholder_text("Title");
        title->set_tooltip_text("The dialog's title. Empty = GTK's own (\"Open File\", \"Save File\" ...)");
        auto* accept = Gtk::make_managed<Gtk::Entry>();
        accept->set_placeholder_text("Accept label");
        accept->set_width_chars(10);
        accept->set_tooltip_text("The accept button's label (\"_Open\", \"_Export\"). Empty = GTK's own.");
        m_grid->attach(*title, 0, m_line);
        m_grid->attach(*accept, 1, m_line++, 2, 1);

        auto* filters = Gtk::make_managed<Gtk::Entry>();
        filters->set_placeholder_text("Text=*.txt;*.md, Images=image/*");
        filters->set_tooltip_text("Filters, a comma list: Label=pattern;pattern. A '/' makes a MIME type "
                                  "(image/*). The first is the one shown. Ignored by a folder dialog.");
        m_grid->attach(*filters, 0, m_line++, 3, 1);

        auto* checks = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto* cancel = Gtk::make_managed<Gtk::CheckButton>("Cancel hook");
        cancel->set_tooltip_text("Also a hook on_" + name + "_cancelled() for Cancel / Escape");
        auto* req = Gtk::make_managed<Gtk::CheckButton>("Required");
        req->set_tooltip_text("on_" + name + "_chosen is pure virtual: the class won't build without it");
        checks->append(*cancel);
        checks->append(*req);
        m_grid->attach(*checks, 0, m_line++, 3, 1);

        auto current = [this, name]() -> std::optional<core::FileDef> {
            if (const auto f = m_d.get(core::Design::kForm))
                for (const auto& x : f->files) if (x.name == name) return x;
            return std::nullopt;
        };
        auto edit = [this, name, current](const std::function<void(core::FileDef&)>& change) {
            if (m_painting || m_rebuilding) return;
            auto x = current();
            if (!x) return;
            change(*x);
            std::string why;
            if (!core::design::set_file(m_j, m_d, name, *x, &why) && !why.empty()) m_said.emit(why);
        };
        auto rename = [this, e, name, edit] {
            if (m_painting || m_rebuilding || e->get_text().raw() == name) return;
            edit([e](core::FileDef& x) { x.name = e->get_text(); });
        };
        e->signal_activate().connect(rename);
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect(rename);
        e->add_controller(focus);
        mode->property_selected().signal_changed().connect([mode, edit, &modes] {
            const auto i = mode->get_selected();
            if (i < modes.size()) edit([&](core::FileDef& x) { x.mode = modes[i]; });
        });
        title->signal_changed().connect([title, edit] { edit([&](core::FileDef& x) { x.title = title->get_text(); }); });
        accept->signal_changed().connect([accept, edit] { edit([&](core::FileDef& x) { x.accept = accept->get_text(); }); });
        filters->signal_changed().connect([filters, edit, split] {
            edit([&](core::FileDef& x) { x.filters = split(filters->get_text()); });
        });
        cancel->signal_toggled().connect([cancel, edit] { edit([&](core::FileDef& x) { x.cancel = cancel->get_active(); }); });
        req->signal_toggled().connect([req, edit] { edit([&](core::FileDef& x) { x.required = req->get_active(); }); });
        del->signal_clicked().connect([this, name] { core::design::remove_file(m_j, m_d, name); });

        m_rows.push_back({[=, &modes](const core::Rec&) {
            const auto x = current();
            if (!x) return;
            if (!typing(e) && e->get_text().raw() != x->name) e->set_text(x->name);
            for (std::size_t i = 0; i < modes.size(); ++i)
                if (modes[i] == x->mode && mode->get_selected() != i) mode->set_selected(static_cast<guint>(i));
            if (!typing(title) && title->get_text().raw() != x->title) title->set_text(x->title);
            if (!typing(accept) && accept->get_text().raw() != x->accept) accept->set_text(x->accept);
            // A comma list typed half-way ("Text=*.txt, ") isn't rewritten under the cursor.
            if (!typing(filters) && split(filters->get_text()) != x->filters) filters->set_text(join(x->filters));
            filters->set_sensitive(x->mode != "folder");
            cancel->set_active(x->cancel);
            req->set_active(x->required);
        }});
    }
    auto* add = Gtk::make_managed<Gtk::Button>();
    auto* add_in = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    add_in->append(*Gtk::make_managed<Gtk::Image>(Glib::ustring("list-add-symbolic")));
    add_in->append(*Gtk::make_managed<Gtk::Label>("Add file dialog"));
    add->set_child(*add_in);
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this] { core::design::add_file(m_j, m_d); });
    m_grid->attach(*add, 0, m_line++, 3, 1);
}

// ── s006: the form's models ─────────────────────────────────────────────────
// A block per model: its name (commits on Enter / leaving it; a rename carries
// the ColumnViews that show it) + delete; then a line per field -- name,
// header (placeholder: the one made from the name), type, delete; Add field.
void Inspector::models_rows() {
    const auto f = m_d.get(core::Design::kForm);
    if (!f) return;
    auto typing = [](Gtk::Widget* w) { return w->has_focus() || gtk_widget_get_focus_child(w->gobj()) != nullptr; };
    const auto& types = core::field_types();
    for (const auto& m0 : f->models) {
        const std::string name = m0.name;
        auto current = [this, name]() -> std::optional<core::ModelDef> {
            if (const auto f = m_d.get(core::Design::kForm))
                for (const auto& m : f->models) if (m.name == name) return m;
            return std::nullopt;
        };
        auto edit = [this, name, current](const std::function<void(core::ModelDef&)>& change) {
            if (m_painting || m_rebuilding) return;
            auto m = current();
            if (!m) return;
            change(*m);
            std::string why;
            if (!core::design::set_model(m_j, m_d, name, *m, &why) && !why.empty()) m_said.emit(why);
        };

        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_hexpand(true);
        e->set_margin_top(6);
        e->set_tooltip_text("The class: " + name + "Item (create it with " + name + "Item::create(...))");
        auto* cls = Gtk::make_managed<Gtk::Label>(name + "Item");
        cls->add_css_class("dim-label");
        cls->set_margin_top(6);
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->set_has_frame(false);
        del->set_margin_top(6);
        del->set_tooltip_text("Delete the model (not while a ColumnView shows it)");
        m_grid->attach(*e, 0, m_line);
        m_grid->attach(*cls, 1, m_line);
        m_grid->attach(*del, 2, m_line++);
        auto rename = [this, e, name, edit] {
            if (m_painting || m_rebuilding || e->get_text().raw() == name) return;
            edit([e](core::ModelDef& m) { m.name = e->get_text(); });
        };
        e->signal_activate().connect(rename);
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect(rename);
        e->add_controller(focus);
        del->signal_clicked().connect([this, name] {
            std::string why;
            if (!core::design::remove_model(m_j, m_d, name, &why) && !why.empty()) m_said.emit(why);
        });
        m_rows.push_back({[=](const core::Rec&) {
            if (!typing(e) && e->get_text().raw() != name) e->set_text(name);
        }});

        for (std::size_t i = 0; i < m0.fields.size(); ++i) {
            auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            line->set_margin_start(12);
            auto* fname = Gtk::make_managed<Gtk::Entry>();
            fname->set_hexpand(true);
            fname->set_width_chars(6);
            fname->set_tooltip_text("The field: a C++ member of the item (title, due_date)");
            auto* title = Gtk::make_managed<Gtk::Entry>();
            title->set_width_chars(6);
            title->set_hexpand(true);
            title->set_tooltip_text("The column's header. Empty = made from the name.");
            line->append(*fname);
            line->append(*title);
            std::vector<Glib::ustring> shown(types.begin(), types.end());
            auto* type = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
            type->set_tooltip_text("string, int (64-bit), double, bool (a check box)");
            no_wheel_unless_focused(*type);
            auto* fdel = Gtk::make_managed<Gtk::Button>();
            fdel->set_icon_name("list-remove-symbolic");
            fdel->set_has_frame(false);
            fdel->set_tooltip_text("Remove the field");
            m_grid->attach(*line, 0, m_line);
            m_grid->attach(*type, 1, m_line);
            m_grid->attach(*fdel, 2, m_line++);

            auto commit_name = [this, fname, i, edit, current] {
                if (m_painting || m_rebuilding) return;
                const auto m = current();
                if (!m || i >= m->fields.size() || m->fields[i].name == fname->get_text().raw()) return;
                edit([&](core::ModelDef& x) { x.fields[i].name = fname->get_text(); });
            };
            fname->signal_activate().connect(commit_name);
            auto ff = Gtk::EventControllerFocus::create();
            ff->signal_leave().connect(commit_name);
            fname->add_controller(ff);
            title->signal_changed().connect([title, i, edit] {
                edit([&](core::ModelDef& x) { if (i < x.fields.size()) x.fields[i].title = title->get_text(); });
            });
            type->property_selected().signal_changed().connect([type, i, edit, &types] {
                const auto k = type->get_selected();
                if (k < types.size()) edit([&](core::ModelDef& x) { if (i < x.fields.size()) x.fields[i].type = types[k]; });
            });
            fdel->signal_clicked().connect([i, edit] {
                edit([&](core::ModelDef& x) { if (i < x.fields.size()) x.fields.erase(x.fields.begin() + static_cast<long>(i)); });
            });
            m_rows.push_back({[=, &types](const core::Rec&) {
                const auto m = current();
                if (!m || i >= m->fields.size()) return;
                const auto& x = m->fields[i];
                if (!typing(fname) && fname->get_text().raw() != x.name) fname->set_text(x.name);
                if (!typing(title) && title->get_text().raw() != x.title) title->set_text(x.title);
                title->set_placeholder_text(core::field_title({x.name, x.type, ""}));
                for (std::size_t k = 0; k < types.size(); ++k)
                    if (types[k] == x.type && type->get_selected() != k) type->set_selected(static_cast<guint>(k));
            }});
        }
        auto* addf = Gtk::make_managed<Gtk::Button>("Add field");
        addf->set_has_frame(false);
        addf->set_halign(Gtk::Align::START);
        addf->set_margin_start(12);
        addf->signal_clicked().connect([edit] {
            edit([](core::ModelDef& x) {
                std::string n;
                for (int k = 1; n.empty() || std::any_of(x.fields.begin(), x.fields.end(),
                                                         [&](const core::FieldDef& y) { return y.name == n; }); ++k)
                    n = "field" + std::to_string(k);
                x.fields.push_back({n, "string", ""});
            });
        });
        m_grid->attach(*addf, 0, m_line++, 3, 1);
    }
    auto* add = Gtk::make_managed<Gtk::Button>();
    auto* add_in = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    add_in->append(*Gtk::make_managed<Gtk::Image>(Glib::ustring("list-add-symbolic")));
    add_in->append(*Gtk::make_managed<Gtk::Label>("Add model"));
    add->set_child(*add_in);
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this] { core::design::add_model(m_j, m_d); });
    m_grid->attach(*add, 0, m_line++, 3, 1);
}

// ── s003: a MenuButton's menu ───────────────────────────────────────────────
// Rows: a label and what it runs (one of the form's actions, app.quit, or
// nothing); a separator starts a new section. Up / down / delete per row.
// The whole list is the property's value: each edit writes it back through
// set_prop (typing in a label merges into one step).
void Inspector::menu_rows(Gtk::Label* label, Gtk::Button* reset) {
    m_grid->attach(*label, 0, m_line);
    m_grid->attach(*reset, 2, m_line++);
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    m_grid->attach(*box, 0, m_line++, 3, 1);

    std::vector<std::string> targets{""};
    if (const auto f = m_d.get(core::Design::kForm))
        for (const auto& a : f->actions) targets.push_back("win." + core::gio_action_name(a.name));
    targets.push_back("app.quit");

    auto rows_now = [this]() -> core::StringList {
        if (const auto r = m_d.get(m_key))
            if (const auto it = r->props.find("menu"); it != r->props.end() && std::holds_alternative<core::StringList>(it->second))
                return std::get<core::StringList>(it->second);
        return {};
    };
    auto write = [this](const core::StringList& l) {
        commit_prop("menu", false, l.empty() ? std::nullopt : std::optional<core::Value>(core::Value(l)));
    };
    // Build the row widgets for a given shape; repainted in place while the
    // shape (count + which rows are separators) stays the same.
    auto shape_of = [](const core::StringList& l) {
        std::string s;
        for (const auto& r : l) s += core::parse_menu_entry(r).separator ? '-' : 'r';
        return s;
    };
    auto built = std::make_shared<std::string>("?");
    auto fill = std::make_shared<std::function<void(const core::StringList&)>>();
    *fill = [this, box, targets, rows_now, write, shape_of, built, fill](const core::StringList& l) {
        *built = shape_of(l);
        m_rebuilding = true;
        while (auto* c = box->get_first_child()) box->remove(*c);
        m_rebuilding = false;
        for (std::size_t i = 0; i < l.size(); ++i) {
            const auto e = core::parse_menu_entry(l[i]);
            auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            if (e.separator) {
                auto* sep = Gtk::make_managed<Gtk::Label>("── section ──");
                sep->add_css_class("dim-label");
                sep->set_hexpand(true);
                line->append(*sep);
            } else {
                auto* text = Gtk::make_managed<Gtk::Entry>();
                text->set_text(e.label);
                text->set_hexpand(true);
                text->set_width_chars(8);
                text->signal_changed().connect([this, text, i, rows_now, write] {
                    if (m_painting || m_rebuilding) return;
                    auto l = rows_now();
                    if (i >= l.size()) return;
                    const auto old = core::parse_menu_entry(l[i]);
                    l[i] = text->get_text() + (old.action.empty() ? "" : "=" + old.action);
                    write(l);
                });
                std::vector<Glib::ustring> shown;
                std::vector<std::string> vals = targets;
                if (std::find(vals.begin(), vals.end(), e.action) == vals.end()) vals.push_back(e.action);
                for (const auto& v : vals) shown.push_back(v.empty() ? "(nothing)" : v);
                auto* dd = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
                for (std::size_t k = 0; k < vals.size(); ++k)
                    if (vals[k] == e.action) dd->set_selected(static_cast<guint>(k));
                no_wheel_unless_focused(*dd);
                dd->property_selected().signal_changed().connect([this, dd, vals, i, rows_now, write] {
                    if (m_painting || m_rebuilding) return;
                    auto l = rows_now();
                    if (i >= l.size() || dd->get_selected() >= vals.size()) return;
                    const auto old = core::parse_menu_entry(l[i]);
                    const auto& v = vals[dd->get_selected()];
                    l[i] = old.label + (v.empty() ? "" : "=" + v);
                    write(l);
                });
                line->append(*text);
                line->append(*dd);
            }
            auto tool = [&](const char* icon, const char* tip, std::function<void(core::StringList&)> op) {
                auto* b = Gtk::make_managed<Gtk::Button>();
                b->set_icon_name(icon);
                b->set_has_frame(false);
                b->set_tooltip_text(tip);
                b->signal_clicked().connect([rows_now, write, op] { auto l = rows_now(); op(l); write(l); });
                line->append(*b);
            };
            tool("go-up-symbolic", "Move up", [i](core::StringList& l) { if (i > 0 && i < l.size()) std::swap(l[i], l[i - 1]); });
            tool("go-down-symbolic", "Move down", [i](core::StringList& l) { if (i + 1 < l.size()) std::swap(l[i], l[i + 1]); });
            tool("list-remove-symbolic", "Delete the row", [i](core::StringList& l) { if (i < l.size()) l.erase(l.begin() + static_cast<long>(i)); });
            box->append(*line);
        }
        auto* adds = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* row = Gtk::make_managed<Gtk::Button>("Add row");
        row->signal_clicked().connect([rows_now, write] { auto l = rows_now(); l.push_back("Item"); write(l); });
        auto* sec = Gtk::make_managed<Gtk::Button>("Add section break");
        sec->signal_clicked().connect([rows_now, write] { auto l = rows_now(); l.push_back("-"); write(l); });
        adds->append(*row);
        adds->append(*sec);
        box->append(*adds);
    };
    m_rows.push_back({[box, built, fill, shape_of, label, reset](const core::Rec& r) {
        const bool set = r.props.count("menu") > 0;
        if (set) label->add_css_class("set");
        else     label->remove_css_class("set");
        reset->set_opacity(set ? 1.0 : 0.0);
        reset->set_sensitive(set);
        core::StringList l;
        if (const auto it = r.props.find("menu"); it != r.props.end() && std::holds_alternative<core::StringList>(it->second))
            l = std::get<core::StringList>(it->second);
        if (shape_of(l) != *built) { (*fill)(l); return; }   // rows came or went: rebuild the list
        // Same shape: refresh labels in place (never under the cursor).
        int i = 0;
        for (auto* line = box->get_first_child(); line && i < static_cast<int>(l.size()); line = line->get_next_sibling(), ++i) {
            const auto e = core::parse_menu_entry(l[static_cast<std::size_t>(i)]);
            if (e.separator) continue;
            if (auto* text = dynamic_cast<Gtk::Entry*>(line->get_first_child()); text && !text->has_focus() &&
                text->get_text().raw() != e.label)
                text->set_text(e.label);
        }
        (void)box;
    }});
}

void Inspector::signal_row(const std::string& type, const std::string& signal) {
    const core::WidgetSpec* w = core::find_widget(type);
    const core::SignalSpec* ss = w ? core::find_signal(*w, signal) : nullptr;
    if (!ss) return;
    auto* hook = Gtk::make_managed<Gtk::CheckButton>(signal);
    hook->set_tooltip_text(ss->doc);
    hook->set_name("designer.inspector.signal." + signal);
    auto* req = Gtk::make_managed<Gtk::CheckButton>("required");
    req->set_tooltip_text("Pure virtual: your class won't compile until you write the hook.");
    auto* name = Gtk::make_managed<Gtk::Label>();
    name->add_css_class("hook");
    name->set_xalign(0);
    name->set_selectable(true);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    m_grid->attach(*hook, 0, m_line);
    m_grid->attach(*req, 1, m_line++);
    m_grid->attach(*name, 0, m_line++, 3, 1);
    hook->signal_toggled().connect([this, hook, signal] {
        if (!m_painting) core::design::tick(m_j, m_d, m_key, signal, hook->get_active());
    });
    req->signal_toggled().connect([this, req, signal] {
        if (!m_painting) core::design::require(m_j, m_d, m_key, signal, req->get_active());
    });
    m_rows.push_back({[this, hook, req, name, signal, ss](const core::Rec& r) {
        const core::SignalHook* h = nullptr;
        for (const auto& s : r.signals)
            if (s.signal == signal) h = &s;
        hook->set_active(h != nullptr);
        req->set_active(h && h->required);
        core::Node n{r.type, r.name, {}, {}, {}, {}};
        const core::SignalHook probe = h ? *h : core::SignalHook{signal, false, ""};
        name->set_text(ss->ret + " " + core::hook_name(n, probe) + "(" + ss->params + ")");
        name->set_opacity(h ? 1.0 : 0.5);
        (void)this;
    }});
}

}  // namespace mortimer::designer
