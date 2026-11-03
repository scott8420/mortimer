#include "designer/Panes.hpp"
#include "core/Catalog.hpp"

#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <glibmm/main.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/gestureclick.h>
#include <algorithm>
#include <functional>
#include <gtkmm/widgetpaintable.h>
#include <gdkmm/contentprovider.h>

namespace mortimer::designer {

Drop drop_for(const core::Design& d, const std::string& sel, const std::string& type) {
    Drop out;
    const auto r = d.get(sel);
    if (!r || sel == core::Design::kForm) {
        out.why = "select where it goes";
        return out;
    }
    // Into the selection, at the end, if it's a container that takes this type.
    const std::string into = core::design::can_hold(d, sel, type);
    if (into.empty()) { out.parent = sel; return out; }
    // Else after the selection, beside it.
    if (!r->parent.empty()) {
        const std::string beside = core::design::can_hold(d, r->parent, type);
        if (beside.empty()) {
            const auto sib = d.children(r->parent);
            for (std::size_t i = 0; i < sib.size(); ++i)
                if (sib[i] == sel) out.index = static_cast<int>(i) + 1;
            out.parent = r->parent;
            return out;
        }
        const auto* w = core::find_widget(r->type);
        out.why = (w && w->children != core::ChildKind::None) ? into : beside;
        return out;
    }
    out.why = into;
    return out;
}

std::optional<Payload> parse_payload(const std::string& s) {
    if (s.rfind("add:", 0) == 0)  return Payload{true, s.substr(4)};
    if (s.rfind("move:", 0) == 0) return Payload{false, s.substr(5)};
    return std::nullopt;
}

namespace {
// A drag source that carries one string (the payload). The drag icon is a
// picture of the widget it started on (else GTK draws the payload text).
Glib::RefPtr<Gtk::DragSource> string_source(std::function<std::string()> payload) {
    auto src = Gtk::DragSource::create();
    src->set_actions(Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    src->signal_drag_begin().connect([s = src.get()](const Glib::RefPtr<Gdk::Drag>&) {
        if (auto* w = s->get_widget()) s->set_icon(Gtk::WidgetPaintable::create(*w), 12, 12);
    });
    src->signal_prepare().connect([payload](double, double) {
        Glib::Value<Glib::ustring> v;
        v.init(v.value_type());
        v.set(payload());
        return Gdk::ContentProvider::create(v);
    }, false);
    return src;
}
std::string payload_of(const Glib::ValueBase& value) {
    if (G_VALUE_HOLDS_STRING(value.gobj())) {
        const char* s = g_value_get_string(value.gobj());
        return s ? s : "";
    }
    return {};
}
}  // namespace

// ── Palette ────────────────────────────────────────────────────────────────

Palette::Palette()
    : widgets::Box("designer.palette", Gtk::Orientation::VERTICAL),
      m_scroll("designer.palette.scroll"),
      m_list("designer.palette.list", Gtk::Orientation::VERTICAL, 2) {
    add_css_class("mortimer-palette");
    auto* title = Gtk::make_managed<Gtk::Label>("Palette");
    title->add_css_class("mortimer-pane-title");
    title->set_xalign(0);
    append(*title);

    m_list.set_margin_start(6);
    m_list.set_margin_end(6);
    m_list.set_margin_bottom(6);
    // Two columns of flat buttons per section: all 18 types fit without scrolling.
    Gtk::FlowBox* flow = nullptr;
    auto section = [&](const char* name) {
        auto* l = Gtk::make_managed<Gtk::Label>(name);
        l->add_css_class("dim-label");
        l->set_xalign(0);
        l->set_margin_top(4);
        l->set_margin_start(4);
        m_list.append(*l);
        flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::NONE);
        flow->set_homogeneous(true);
        flow->set_min_children_per_line(2);
        flow->set_max_children_per_line(2);
        flow->set_column_spacing(0);
        flow->set_row_spacing(0);
        m_list.append(*flow);
    };
    auto add = [&](const core::WidgetSpec& w) {
        auto* b = Gtk::make_managed<Gtk::Button>(w.type);
        b->set_has_frame(false);
        if (auto* l = dynamic_cast<Gtk::Label*>(b->get_child())) l->set_xalign(0);
        b->set_tooltip_text(w.doc);
        b->set_name("designer.palette." + w.type);
        b->signal_clicked().connect([this, t = w.type] { m_add.emit(t); });
        b->add_controller(string_source([p = "add:" + w.type] { return p; }));   // ...or drag it where it goes
        m_buttons[w.type] = b;
        m_docs[w.type] = w.doc;
        flow->append(*b);
        if (auto* child = b->get_parent()) child->set_focusable(false);   // the button takes the key, not its cell
    };
    section("Containers");
    for (const auto& w : core::catalog())
        if (!w.root && w.children != core::ChildKind::None) add(w);
    section("Controls");
    for (const auto& w : core::catalog())
        if (!w.root && w.children == core::ChildKind::None) add(w);

    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void Palette::repaint(const core::Design& d, const std::string& sel) {
    for (auto& [type, b] : m_buttons) {
        const Drop drop = drop_for(d, sel, type);
        b->set_sensitive(!drop.parent.empty());
        b->set_tooltip_text(drop.parent.empty() ? m_docs[type] + "\n\nNot here: " + drop.why : m_docs[type]);
    }
}

// ── TreePane ───────────────────────────────────────────────────────────────

TreePane::TreePane()
    : widgets::Box("designer.tree", Gtk::Orientation::VERTICAL),
      m_scroll("designer.tree.scroll"),
      m_list("designer.tree.list") {
    add_css_class("mortimer-tree");
    auto* title = Gtk::make_managed<Gtk::Label>("Widgets");
    title->add_css_class("mortimer-pane-title");
    title->set_xalign(0);
    append(*title);

    m_list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    m_list.add_css_class("navigation-sidebar");
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_painting || !row) return;
        const auto i = static_cast<std::size_t>(row->get_index());
        if (i < m_row_keys.size() && m_row_keys[i] != m_sel) m_select.emit(m_row_keys[i]);
    });
    // s018: Ctrl / Shift + click grows the selection. Heard in CAPTURE and
    // claimed, so the ListBox's own (one-row) selection never moves -- its
    // selected row stays the primary, the others wear a class.
    auto more = Gtk::GestureClick::create();
    more->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    more->signal_pressed().connect([this, g = more.get()](int, double, double y) {
        const auto st = g->get_current_event_state();
        const int how = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK ? 1
                      : (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK     ? 2 : 0;
        if (!how) return;   // a plain click: the ListBox's, as before
        auto* row = m_list.get_row_at_y(static_cast<int>(y));
        if (!row) return;
        const auto i = static_cast<std::size_t>(row->get_index());
        if (i >= m_row_keys.size()) return;
        g->set_state(Gtk::EventSequenceState::CLAIMED);
        const std::string key = m_row_keys[i];
        Glib::signal_idle().connect_once([this, key, how] { m_select_more.emit(key, how); });   // repaints us
    });
    m_list.add_controller(more);
    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void TreePane::repaint(const core::Design& d, const std::string& sel, const std::vector<std::string>& also) {
    m_painting = true;
    m_also = also;
    m_d = &d;
    m_marked = nullptr;   // the rows are about to go
    // A selection that MOVES into a collapsed branch opens the branch. Only when
    // it moves: a repaint for a collapse would otherwise reopen at once any
    // branch holding the selection -- the root never closed (Scott, s008).
    const bool moved = sel != m_sel;
    m_sel = sel;
    if (auto r = d.get(sel); r && moved)
        for (std::string k = r->parent; !k.empty(); k = d.get(k) ? d.get(k)->parent : "")
            m_collapsed.erase(k);
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    m_row_keys.clear();
    if (!d.root().empty()) add_rows(d, d.root(), 0);
    for (std::size_t i = 0; i < m_row_keys.size(); ++i)
        if (m_row_keys[i] == sel) m_list.select_row(*m_list.get_row_at_index(static_cast<int>(i)));
    m_painting = false;
}

void TreePane::add_rows(const core::Design& d, const std::string& key, int depth) {
    const core::Rec r = *d.get(key);
    const auto kids = d.children(key);
    const bool open = !m_collapsed.count(key);

    auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    line->set_margin_start(depth * 14);
    if (!kids.empty()) {
        auto* ex = Gtk::make_managed<Gtk::Button>();
        ex->set_icon_name(open ? "pan-down-symbolic" : "pan-end-symbolic");
        ex->set_has_frame(false);
        ex->add_css_class("expander");
        ex->set_focusable(false);
        ex->set_tooltip_text(open ? "Collapse" : "Expand");
        ex->signal_clicked().connect([this, key, &d] {
            bool select_it = false;
            if (m_collapsed.count(key)) m_collapsed.erase(key);
            else {
                m_collapsed.insert(key);
                // Collapsing over the selection hands it to the collapsed row (as
                // GTK's own trees do), so it isn't left on a row nobody can see.
                if (auto r = d.get(m_sel))
                    for (std::string k = r->parent; !k.empty(); k = d.get(k) ? d.get(k)->parent : "")
                        if (k == key) { select_it = true; break; }
            }
            // Deferred: this button is inside the row being rebuilt.
            Glib::signal_idle().connect_once([this, &d, key, select_it] {
                if (select_it) m_select.emit(key);   // the shell repaints us with it
                else repaint(d, m_sel);
            });
        });
        line->append(*ex);
    } else {
        auto* gap = Gtk::make_managed<Gtk::Box>();
        gap->set_size_request(16, -1);
        line->append(*gap);
    }
    auto* name = Gtk::make_managed<Gtk::Label>(r.name);
    name->set_xalign(0);
    line->append(*name);
    std::string what = r.cls.empty() ? r.type : r.cls + " : " + r.type;   // s022: promoted
    if (const auto it = r.pack.find("slot"); it != r.pack.end() && std::holds_alternative<std::string>(it->second))
        what += " · " + std::get<std::string>(it->second);
    if (!r.signals.empty()) what += " · " + std::to_string(r.signals.size()) + " hook" + (r.signals.size() > 1 ? "s" : "");
    auto* type = Gtk::make_managed<Gtk::Label>(what);
    type->add_css_class("type");
    type->set_xalign(0);
    type->set_hexpand(true);
    type->set_ellipsize(Pango::EllipsizeMode::END);
    line->append(*type);

    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*line);
    row->set_name("designer.tree.row." + r.name);
    if (std::find(m_also.begin(), m_also.end(), key) != m_also.end()) row->add_css_class("mortimer-also-selected");
    if (!r.parent.empty()) make_draggable(*row, key);
    make_drop_target(*row, key);
    m_list.append(*row);
    m_row_keys.push_back(key);
    if (open)
        for (const auto& c : kids) add_rows(d, c, depth + 1);
}

void TreePane::make_draggable(Gtk::Widget& row, const std::string& key) {
    // s019: a row that is one of several selected carries them all -- read at
    // drag time, the selection may have grown since the row was built.
    row.add_controller(string_source([this, key] {
        std::vector<std::string> sel{m_sel};
        sel.insert(sel.end(), m_also.begin(), m_also.end());
        if (sel.size() > 1 && std::find(sel.begin(), sel.end(), key) != sel.end())
            return "move:" + core::design::join_keys(sel);
        return "move:" + key;
    }));
}

core::design::Landing TreePane::resolve(const Payload& p, const std::string& key, double y, int height) const {
    core::design::Landing none;
    if (!m_d) return none;
    const auto t = m_d->get(key);
    if (!t) return none;
    if (!p.add && p.what.find(',') != std::string::npos) {   // s019: several
        const auto many = core::design::split_keys(p.what);
        const auto* tw = core::find_widget(t->type);
        const bool cont = tw && tw->children != core::ChildKind::None;
        return core::design::landing_many(*m_d, many, key, core::design::zone_at(y, height, cont, t->parent.empty()));
    }
    std::string type = p.what;
    if (!p.add) {
        const auto m = m_d->get(p.what);
        if (!m) return none;
        type = m->type;
    }
    const auto* w = core::find_widget(t->type);
    const bool container = w && w->children != core::ChildKind::None;
    const auto z = core::design::zone_at(y, height, container, t->parent.empty());
    return core::design::landing(*m_d, type, p.add ? "" : p.what, key, z);
}

void TreePane::clear_marks() {
    if (!m_marked) return;
    for (const char* c : {"mortimer-drop-before", "mortimer-drop-after", "mortimer-drop-inside"})
        m_marked->remove_css_class(c);
    m_marked = nullptr;
}

void TreePane::make_drop_target(Gtk::Widget& row, const std::string& key) {
    auto drop = Gtk::DropTarget::create(G_TYPE_STRING, Gdk::DragAction::COPY | Gdk::DragAction::MOVE);
    // The ALIVE flag (tree-and-dnd.md): the drop rebuilds the tree, and a
    // leave for the destroyed row may still be in flight.
    auto alive = std::make_shared<bool>(true);
    row.signal_destroy().connect([alive] { *alive = false; });
    // The payload is only readable at drop on some backends; preload asks for it on enter.
    drop->set_preload(true);
    // (A raw pointer to the controller: capturing its RefPtr in its own signal would be a cycle.)
    auto mark = [this, &row, key, alive](Gtk::DropTarget* t, double y) -> Gdk::DragAction {
        if (!*alive) return {};
        clear_marks();
        const auto p = parse_payload(payload_of(t->get_value()));
        if (!p) return {};
        const auto at = resolve(*p, key, y, row.get_height());
        if (at.parent.empty()) return {};
        const auto* w = core::find_widget(m_d->get(key)->type);
        const bool container = w && w->children != core::ChildKind::None;
        const auto z = core::design::zone_at(y, row.get_height(), container, m_d->get(key)->parent.empty());
        row.add_css_class(z == core::design::Zone::Inside ? "mortimer-drop-inside"
                          : z == core::design::Zone::Before ? "mortimer-drop-before" : "mortimer-drop-after");
        m_marked = &row;
        return p->add ? Gdk::DragAction::COPY : Gdk::DragAction::MOVE;
    };
    drop->signal_enter().connect([mark, d = drop.get()](double, double y) { return mark(d, y); }, false);
    drop->signal_motion().connect([mark, d = drop.get()](double, double y) { return mark(d, y); }, false);
    drop->signal_leave().connect([this, alive] { if (*alive) clear_marks(); }, false);
    drop->signal_drop().connect([this, &row, key, alive](const Glib::ValueBase& value, double, double y) {
        if (!*alive) return false;
        clear_marks();
        const std::string payload = payload_of(value);
        const auto p = parse_payload(payload);
        if (!p) return false;
        const auto at = resolve(*p, key, y, row.get_height());
        if (at.parent.empty()) {
            if (!at.why.empty()) m_refused.emit(at.why);
            return false;
        }
        // Defer: the verb repaints the tree, and that must not happen under
        // GTK's drag machinery. Everything the idle needs is captured NOW.
        Glib::signal_idle().connect_once([this, payload, parent = at.parent, index = at.index] {
            m_drop.emit(payload, parent, index);
        });
        return true;
    }, false);
    row.add_controller(drop);
}

void TreePane::focus_selection() {
    if (auto* row = m_list.get_selected_row()) row->grab_focus();
}

}  // namespace mortimer::designer
