#include "designer/Canvas.hpp"
#include "core/Catalog.hpp"
#include "designer/GProps.hpp"
#include "Log.hpp"

#include <gtkmm/adjustment.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/droptarget.h>
#include <glibmm/main.h>
#include <gtkmm/grid.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <giomm/menu.h>
#include <gtkmm/paned.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/switch.h>
#include <gtkmm/columnview.h>
#include <gtkmm/columnviewcolumn.h>
#include <gtkmm/noselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <giomm/liststore.h>

#include <algorithm>

namespace mortimer::designer {

namespace {

using core::Value;

std::string str(const std::map<std::string, Value>& m, const std::string& k, const std::string& dflt = "") {
    const auto it = m.find(k);
    return it != m.end() && std::holds_alternative<std::string>(it->second)
               ? std::get<std::string>(it->second) : dflt;
}
std::int64_t num(const std::map<std::string, Value>& m, const std::string& k, std::int64_t dflt) {
    const auto it = m.find(k);
    if (it == m.end()) return dflt;
    if (std::holds_alternative<std::int64_t>(it->second)) return std::get<std::int64_t>(it->second);
    if (std::holds_alternative<double>(it->second)) return static_cast<std::int64_t>(std::get<double>(it->second));
    return dflt;
}
bool flag(const std::map<std::string, Value>& m, const std::string& k, bool dflt) {
    const auto it = m.find(k);
    return it != m.end() && std::holds_alternative<bool>(it->second) ? std::get<bool>(it->second) : dflt;
}
double dbl(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<double>(std::get<std::int64_t>(v));
    return 0;
}

Gtk::Adjustment* adjustment_of(Gtk::Widget& w) {
    if (auto* s = dynamic_cast<Gtk::SpinButton*>(&w)) return s->get_adjustment().get();
    if (auto* r = dynamic_cast<Gtk::Range*>(&w)) return r->get_adjustment().get();
    return nullptr;
}

bool is_container(const std::string& type) {
    const auto* w = core::find_widget(type);
    return w && w->children != core::ChildKind::None;
}

}  // namespace

Canvas::Canvas()
    : widgets::ScrolledWindow("designer.canvas"),
      m_stage("designer.canvas.stage", Gtk::Orientation::VERTICAL) {
    m_stage.add_css_class("mortimer-stage");
    m_stage.set_hexpand(true);
    m_stage.set_vexpand(true);
    // Nothing on the stage takes the keyboard: typing never lands in a designed Entry.
    m_stage.set_can_focus(false);
    set_child(m_stage);
    set_hexpand(true);
    set_vexpand(true);

    // CAPTURE phase: the stage sees the press before the designed widget does,
    // picks what is under the pointer and claims the press -- the widget never
    // gets it (no button press, no dropdown opening, no entry focus).
    auto click = Gtk::GestureClick::create();
    click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    click->signal_pressed().connect([this, g = click.get()](int n, double x, double y) {
        on_press(n, x, y);
        g->set_state(Gtk::EventSequenceState::CLAIMED);
    });
    m_stage.add_controller(click);

    // Drops: from the palette ("add:<Type>") or the tree ("move:<key>"). The
    // widget under the pointer takes it if it can; else it lands just after it.
    auto drop = Gtk::DropTarget::create(G_TYPE_STRING, Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    drop->set_preload(true);
    auto resolve = [this](const Glib::ValueBase& v, double x, double y, std::string* payload) {
        core::design::Landing none;
        if (!m_d || !G_VALUE_HOLDS_STRING(v.gobj())) return none;
        const char* raw = g_value_get_string(v.gobj());
        const std::string s = raw ? raw : "";
        if (payload) *payload = s;
        const bool add = s.rfind("add:", 0) == 0;
        if (!add && s.rfind("move:", 0) != 0) return none;
        const std::string what = s.substr(add ? 4 : 5);
        std::string type = what;
        if (!add) {
            const auto m = m_d->get(what);
            if (!m) return none;
            type = m->type;
        }
        const std::string key = key_at(x, y);
        if (key.empty()) return none;
        auto at = core::design::landing_on(*m_d, type, add ? "" : what, key);
        if (!at.parent.empty()) set_drop_mark(at.parent == key ? key : at.parent);
        return at;
    };
    drop->signal_motion().connect([this, resolve, d = drop.get()](double x, double y) {
        set_drop_mark("");
        const auto at = resolve(d->get_value(), x, y, nullptr);
        if (at.parent.empty()) return Gdk::DragAction{};
        return Gdk::DragAction::COPY;
    }, false);
    drop->signal_leave().connect([this] { set_drop_mark(""); }, false);
    drop->signal_drop().connect([this, resolve](const Glib::ValueBase& v, double x, double y) {
        std::string payload;
        const auto at = resolve(v, x, y, &payload);
        set_drop_mark("");
        if (at.parent.empty()) {
            if (!at.why.empty()) m_refused.emit(at.why);
            return false;
        }
        Glib::signal_idle().connect_once([this, payload, parent = at.parent, index = at.index] {
            m_drop.emit(payload, parent, index);
        });
        return true;
    }, false);
    m_stage.add_controller(drop);
}

std::string Canvas::key_at(double x, double y) const {
    for (Gtk::Widget* w = const_cast<widgets::Box&>(m_stage).pick(x, y); w && w != &m_stage; w = w->get_parent())
        if (const auto it = m_keys.find(w); it != m_keys.end()) return it->second;
    return {};
}

void Canvas::set_drop_mark(const std::string& key) {
    if (m_drop_marked) m_drop_marked->remove_css_class("mortimer-drop-target");
    m_drop_marked = nullptr;
    if (const auto it = m_widgets.find(key); it != m_widgets.end()) {
        m_drop_marked = it->second;
        m_drop_marked->add_css_class("mortimer-drop-target");
    }
}

void Canvas::on_press(int, double x, double y) {
    const std::string key = key_at(x, y);
    if (key.empty()) return;
    if (auto lg = log::get(log::Area::Canvas)) lg->debug("pick {}", key);
    m_pick.emit(key);
}

void Canvas::show(const core::Design& d, const std::string& sel) {
    m_design = &d;
    // Remember which page each Stack showed, so a rebuild doesn't flip it back.
    for (const auto& [key, w] : m_widgets)
        if (auto* st = dynamic_cast<Gtk::Stack*>(w))
            m_page[key] = st->get_visible_child_name();

    m_d = &d;
    m_drop_marked = nullptr;
    if (m_card) m_stage.remove(*m_card);   // managed: the old tree goes with it
    m_card = nullptr;
    m_keys.clear();
    m_widgets.clear();
    m_refs.clear();

    const std::string root = d.root();
    if (root.empty()) return;
    m_card = frame_for(d, root);
    m_stage.append(*m_card);

    // Refs resolve after everything is built (a sidebar may come before its stack).
    for (auto& [w, name] : m_refs) {
        auto* sb = dynamic_cast<Gtk::StackSidebar*>(w);
        const auto it = m_widgets.find(d.find(name));
        auto* st = it == m_widgets.end() ? nullptr : dynamic_cast<Gtk::Stack*>(it->second);
        if (sb && st) sb->set_stack(*st);
    }
    // Each Stack shows its remembered page...
    for (const auto& [key, page] : m_page) {
        const auto it = m_widgets.find(key);
        if (it == m_widgets.end() || page.empty()) continue;
        if (auto* st = dynamic_cast<Gtk::Stack*>(it->second); st && st->get_child_by_name(page))
            st->set_visible_child(page);
    }
    // ...unless the selection is on another page: then that one.
    for (std::string k = sel; !k.empty();) {
        const auto r = d.get(k);
        if (!r) break;
        const auto pr = d.get(r->parent);
        if (pr && pr->type == "Stack") {
            auto* st = dynamic_cast<Gtk::Stack*>(m_widgets[r->parent]);
            auto* page = m_widgets[k];
            if (st && page) st->set_visible_child(*page);
        }
        k = r->parent;
    }
    if (const auto it = m_widgets.find(sel); it != m_widgets.end())
        it->second->add_css_class("mortimer-selected");
}

Gtk::Widget* Canvas::frame_for(const core::Design& d, const std::string& root) {
    const core::Rec r = *d.get(root);
    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    card->add_css_class("mortimer-window");
    card->set_overflow(Gtk::Overflow::HIDDEN);
    card->set_halign(Gtk::Align::CENTER);
    card->set_valign(Gtk::Align::START);
    card->set_name("canvas." + r.name);
    const int w = static_cast<int>(num(r.props, "default_width", 480));
    const int h = static_cast<int>(num(r.props, "default_height", 320));
    card->set_size_request(std::max(160, w), std::max(100, h));
    m_keys[card] = root;
    m_widgets[root] = card;

    const std::string title = str(r.props, "title", r.name);
    Gtk::Widget* titlebar = nullptr;
    Gtk::Widget* body = nullptr;
    for (const auto& c : d.children(root)) {
        const auto cr = *d.get(c);
        Gtk::Widget* built = build(d, c);
        if (str(cr.pack, "slot") == "titlebar") titlebar = built;
        else if (!body) body = built;
    }
    if (titlebar) {
        // A designed HeaderBar shows the window's title, as it would in a real window.
        if (auto* hb = dynamic_cast<Gtk::HeaderBar*>(titlebar); hb && !hb->get_title_widget()) {
            auto* t = Gtk::make_managed<Gtk::Label>(title);
            t->add_css_class("title");
            hb->set_title_widget(*t);
        }
        card->append(*titlebar);
    } else {
        // No titlebar designed: the desktop's own -- a plain strip with the title.
        auto* bar = Gtk::make_managed<Gtk::CenterBox>();
        bar->add_css_class("mortimer-titlebar");
        bar->set_center_widget(*Gtk::make_managed<Gtk::Label>(title));
        card->append(*bar);
        m_keys[bar] = root;
    }
    if (body) {
        body->set_vexpand(true);
        card->append(*body);
    } else {
        auto* empty = Gtk::make_managed<Gtk::Box>();
        empty->add_css_class("mortimer-empty");
        empty->set_vexpand(true);
        empty->set_margin(12);
        card->append(*empty);
        m_keys[empty] = root;
    }
    return card;
}

Gtk::Widget* Canvas::build(const core::Design& d, const std::string& key) {
    const core::Rec r = *d.get(key);
    Gtk::Widget* w = make(r);
    w->set_name("canvas." + r.name);
    m_keys[w] = key;
    m_widgets[key] = w;
    apply(*w, r);
    const auto kids = d.children(key);
    if (is_container(r.type) && kids.empty()) w->add_css_class("mortimer-empty");
    for (const auto& c : kids) place(*w, r, *build(d, c), *d.get(c));
    return w;
}

Gtk::Widget* Canvas::make(const core::Rec& r) {
    const auto& t = r.type;
    auto placeholder = [&](Gtk::Widget& w) { w.add_css_class("mortimer-placeholder"); };
    if (t == "Box")            return Gtk::make_managed<Gtk::Box>();
    if (t == "Grid")           return Gtk::make_managed<Gtk::Grid>();
    if (t == "Paned")          return Gtk::make_managed<Gtk::Paned>();
    if (t == "Stack")          return Gtk::make_managed<Gtk::Stack>();
    if (t == "StackSidebar")   return Gtk::make_managed<Gtk::StackSidebar>();
    if (t == "ScrolledWindow") return Gtk::make_managed<Gtk::ScrolledWindow>();
    if (t == "HeaderBar")      return Gtk::make_managed<Gtk::HeaderBar>();
    if (t == "ListBox")        return Gtk::make_managed<Gtk::ListBox>();
    if (t == "Entry")          return Gtk::make_managed<Gtk::Entry>();
    if (t == "Switch") {
        auto* s = Gtk::make_managed<Gtk::Switch>();
        s->set_halign(Gtk::Align::START);
        return s;
    }
    if (t == "Scale")          return Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
    if (t == "SpinButton")     return Gtk::make_managed<Gtk::SpinButton>();
    if (t == "DropDown")       return Gtk::make_managed<Gtk::DropDown>();
    if (t == "ColumnView")     return Gtk::make_managed<Gtk::ColumnView>();   // s006
    if (t == "Image") {
        auto* i = Gtk::make_managed<Gtk::Image>();
        if (!r.props.count("icon_name")) i->set_from_icon_name("image-x-generic-symbolic");
        return i;
    }
    // Leaves that would be invisible with nothing set show their name, dimmed.
    if (t == "Label") {
        auto* l = Gtk::make_managed<Gtk::Label>();
        if (!r.props.count("label")) { l->set_text(r.name); placeholder(*l); }
        return l;
    }
    if (t == "Button") {
        auto* b = Gtk::make_managed<Gtk::Button>();
        if (!r.props.count("label") && !r.props.count("icon_name")) { b->set_label(r.name); placeholder(*b); }
        return b;
    }
    if (t == "CheckButton") {
        auto* c = Gtk::make_managed<Gtk::CheckButton>();
        if (!r.props.count("label")) c->set_label(r.name);
        return c;
    }
    if (t == "MenuButton") {
        auto* m = Gtk::make_managed<Gtk::MenuButton>();
        if (!r.props.count("label") && !r.props.count("icon_name")) m->set_icon_name("open-menu-symbolic");
        return m;
    }
    // A type the canvas doesn't know: say so on the canvas itself.
    m_unapplied.insert(t);
    auto* l = Gtk::make_managed<Gtk::Label>("[" + t + "]");
    placeholder(*l);
    return l;
}

void Canvas::apply(Gtk::Widget& w, const core::Rec& r) {
    const core::WidgetSpec* spec = core::find_widget(r.type);
    if (!spec) return;
    // Catalog order, like the generator: a range's bounds before its value,
    // use_markup before the label. `always` props get their default when unset.
    auto run = [&](const core::PropSpec& p) {
        const auto it = r.props.find(p.name);
        Value v;
        if (it != r.props.end()) v = it->second;
        else if (p.always && !p.def.empty()) v = std::stod(p.def);
        else return;
        if (!apply_one(w, r, p.name, v)) {
            const std::string what = r.type + "." + p.name;
            if (m_unapplied.insert(what).second)
                if (auto lg = log::get(log::Area::Canvas)) lg->warn("canvas can't show {} yet", what);
        }
    };
    for (const auto& p : spec->props) run(p);
    for (const auto& p : core::common_props()) run(p);
}

bool Canvas::apply_one(Gtk::Widget& w, const core::Rec& r, const std::string& p, const Value& v) {
    // The spelled-out few.
    if (p == "margin") { w.set_margin(static_cast<int>(dbl(v))); return true; }
    if (p == "tooltip" && std::holds_alternative<std::string>(v)) {
        w.set_tooltip_text(std::get<std::string>(v));
        return true;
    }
    if (p == "css_classes" && std::holds_alternative<core::StringList>(v)) {
        for (const auto& c : std::get<core::StringList>(v)) w.add_css_class(c);
        return true;
    }
    if (p == "lower" || p == "upper" || p == "step" || p == "page" || p == "value") {
        auto* adj = adjustment_of(w);
        if (!adj) return false;
        const double x = dbl(v);
        if (p == "lower") adj->set_lower(x);
        else if (p == "upper") adj->set_upper(x);
        else if (p == "step") adj->set_step_increment(x);
        else if (p == "page") adj->set_page_increment(x);
        else adj->set_value(x);
        return true;
    }
    if (p == "items" && std::holds_alternative<core::StringList>(v)) {
        auto* dd = dynamic_cast<Gtk::DropDown*>(&w);
        if (!dd) return false;
        std::vector<Glib::ustring> items(std::get<core::StringList>(v).begin(), std::get<core::StringList>(v).end());
        dd->set_model(Gtk::StringList::create(items));
        return true;
    }
    if (p == "model" && r.type == "ColumnView" && std::holds_alternative<std::string>(v)) {
        // s006: the headers the generated table will have; no rows (they're
        // yours, filled at run time).
        auto* cv = dynamic_cast<Gtk::ColumnView*>(&w);
        const auto f = m_design ? m_design->get(core::Design::kForm) : std::nullopt;
        if (!cv || !f) return false;
        cv->set_model(Gtk::NoSelection::create(Gio::ListStore<Glib::Object>::create()));
        for (const auto& m : f->models)
            if (m.name == std::get<std::string>(v))
                for (const auto& x : m.fields) {
                    // s007: an editable column wears a pencil -- a designer affordance only.
                    const auto ed = r.props.find("editable");
                    const bool pencil = ed != r.props.end() && std::holds_alternative<core::StringList>(ed->second) &&
                                        std::ranges::count(std::get<core::StringList>(ed->second), x.name) > 0;
                    auto col = Gtk::ColumnViewColumn::create(core::field_title(x) + (pencil ? " \u270E" : ""),
                                                             Gtk::SignalListItemFactory::create());
                    col->set_expand(x.type == "string");
                    cv->append_column(col);
                }
        return true;
    }
    if ((p == "editable" || p == "selection") && r.type == "ColumnView")
        return true;   // s007: run-time behaviour; "model" above marks the editable headers
    if (p == "stack" && r.type == "StackSidebar" && std::holds_alternative<std::string>(v)) {
        m_refs.emplace_back(&w, std::get<std::string>(v));
        return true;
    }
    if (p == "menu" && std::holds_alternative<core::StringList>(v)) {
        auto* mb = dynamic_cast<Gtk::MenuButton*>(&w);
        if (!mb) return false;
        // The same rows the generator writes (inert here: the canvas claims clicks).
        auto menu = Gio::Menu::create();
        auto section = Gio::Menu::create();
        for (const auto& row : std::get<core::StringList>(v)) {
            const auto e = core::parse_menu_entry(row);
            if (e.separator) { menu->append_section(section); section = Gio::Menu::create(); }
            else section->append(e.label, e.action);
        }
        menu->append_section(section);
        mb->set_menu_model(menu);
        return true;
    }
    if (p == "icon_name" && r.type == "Image" && std::holds_alternative<std::string>(v)) {
        static_cast<Gtk::Image&>(w).set_from_icon_name(std::get<std::string>(v));
        return true;
    }
    // Everything else: the GObject property behind it (designer/GProps).
    return gprops::set(w, p, v);
}

void Canvas::place(Gtk::Widget& parent, const core::Rec& prec, Gtk::Widget& child, const core::Rec& crec) {
    const auto& pk = crec.pack;
    const std::string slot = str(pk, "slot");
    if (auto* box = dynamic_cast<Gtk::Box*>(&parent))              box->append(child);
    else if (auto* g = dynamic_cast<Gtk::Grid*>(&parent))
        g->attach(child, static_cast<int>(num(pk, "column", 0)), static_cast<int>(num(pk, "row", 0)),
                  static_cast<int>(num(pk, "width", 1)), static_cast<int>(num(pk, "height", 1)));
    else if (auto* p = dynamic_cast<Gtk::Paned*>(&parent)) {
        if (slot == "end") {
            p->set_end_child(child);
            p->set_resize_end_child(flag(pk, "resize", true));
            p->set_shrink_end_child(flag(pk, "shrink", false));
        } else {
            p->set_start_child(child);
            p->set_resize_start_child(flag(pk, "resize", true));
            p->set_shrink_start_child(flag(pk, "shrink", false));
        }
    } else if (auto* st = dynamic_cast<Gtk::Stack*>(&parent)) {
        auto page = st->add(child, str(pk, "name", crec.name), str(pk, "title", crec.name));
        if (const auto icon = str(pk, "icon_name"); !icon.empty()) page->property_icon_name() = icon;
    } else if (auto* hb = dynamic_cast<Gtk::HeaderBar*>(&parent)) {
        if (slot == "title") hb->set_title_widget(child);
        else if (slot == "end") hb->pack_end(child);
        else hb->pack_start(child);
    } else if (auto* sw = dynamic_cast<Gtk::ScrolledWindow*>(&parent)) {
        sw->set_child(child);
    } else if (auto* lb = dynamic_cast<Gtk::ListBox*>(&parent)) {
        lb->append(child);
    } else if (auto lg = log::get(log::Area::Canvas)) {
        lg->warn("canvas can't place a child in a {}", prec.type);
    }
}

}  // namespace mortimer::designer
