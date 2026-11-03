#include "designer/ProjectPane.hpp"
#include "core/Project.hpp"
#include "core/Resources.hpp"
#include "Log.hpp"

#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include <glibmm/variant.h>
#include <glibmm/main.h>
#include <algorithm>
#include <filesystem>
#include <gdk/gdkkeysyms.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/root.h>

namespace mortimer::designer {

ProjectPane::ProjectPane()
    : widgets::Box("designer.project", Gtk::Orientation::VERTICAL),
      m_add("designer.project.add"),
      m_scroll("designer.project.scroll"),
      m_list("designer.project.list") {
    add_css_class("mortimer-project");
    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    auto* title = Gtk::make_managed<Gtk::Label>("Project");
    title->add_css_class("mortimer-pane-title");
    title->set_xalign(0);
    title->set_hexpand(true);
    head->append(*title);

    // The +: what can be added, revealed (the shell's actions -- greyed when
    // there's no project).
    auto menu = Gio::Menu::create();
    m_forms_menu = Gio::Menu::create();
    m_forms_menu->append("Window…", "win.add-window");
    m_forms_menu->append("Dialog…", "win.add-dialog");
    m_forms_menu->append("About dialog", "win.add-about");   // s010: logo, name, version, Close
    menu->append_section("Forms", m_forms_menu);
    m_res_menu = Gio::Menu::create();
    m_res_menu->append("Icon (.svg)…", "win.add-icon");
    m_res_menu->append("Image…", "win.add-image");
    m_res_menu->append("Stylesheet", "win.edit-style");   // s012: makes it if needed, opens the editor
    m_res_menu->append("Data file…", "win.add-data");
    menu->append_section("Resources", m_res_menu);
    m_add.set_menu_model(menu);
    m_add.set_icon_name("list-add-symbolic");
    m_add.set_has_frame(false);
    m_add.set_tooltip_text("Add a form or a resource to the project");
    m_add.set_margin_end(4);
    head->append(m_add);
    append(*head);

    // s025 m1: SINGLE, so a clicked row wears the highlight -- what Delete
    // will take is on screen. (Headings and notes aren't selectable.)
    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list.set_activate_on_single_click(true);
    m_list.add_css_class("navigation-sidebar");
    m_list.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (!m_painting) activate_row(static_cast<std::size_t>(row->get_index()));
    });

    // s025 m1: the row's verbs as actions, so a PopoverMenu can show them (and
    // grey Remove for the main form and the logo, with the reason in the tip
    // the trash already carries). Each acts on m_menu_row.
    m_row_actions = Gio::SimpleActionGroup::create();
    m_act_open = m_row_actions->add_action("open", [this] {
        if (m_menu_row >= 0) activate_row(static_cast<std::size_t>(m_menu_row));
    });
    m_act_copy = m_row_actions->add_action("copy", [this] {
        if (m_menu_row >= 0) activate_row(static_cast<std::size_t>(m_menu_row));
    });
    m_act_rename = m_row_actions->add_action("rename", [this] {
        const auto i = static_cast<std::size_t>(m_menu_row);
        if (m_menu_row >= 0 && i < m_acts.size() && m_acts[i].rename) {
            auto f = m_acts[i].rename;   // a copy: the call may repaint (m_acts cleared)
            f();
        }
    });
    m_act_remove = m_row_actions->add_action("remove", [this] {
        const auto i = static_cast<std::size_t>(m_menu_row);
        if (m_menu_row >= 0 && i < m_acts.size() && m_acts[i].can_trash && m_acts[i].trash) {
            auto f = m_acts[i].trash;
            f();
        }
    });
    insert_action_group("project", m_row_actions);

    // The menu hangs off the list (a popover is a native: the list doesn't lay
    // it out). Pointed at the click, no arrow -- a context menu, not a callout.
    m_menu.set_parent(*this);
    m_menu.set_has_arrow(false);
    m_menu.set_halign(Gtk::Align::START);
    // A closing popover hands the focus back to whatever had it BEFORE the
    // right-click (the canvas, say) -- so the row we meant takes it back, a
    // beat later (after GTK's own hand-back). Then Delete is the list's.
    m_menu.signal_closed().connect([this] {
        if (m_menu_row < 0) return;
        const int i = static_cast<int>(m_menu_row);
        Glib::signal_idle().connect_once([this, i] {
            if (auto* row = m_list.get_row_at_index(i); row && row_act(row)) row->grab_focus();
        });
    });

    // Right-click: select the row under the pointer (so the highlight says
    // which row the menu is about), then the menu.
    auto right = Gtk::GestureClick::create();
    right->set_button(GDK_BUTTON_SECONDARY);
    right->signal_pressed().connect([this, g = right.get()](int, double x, double y) {
        g->set_state(Gtk::EventSequenceState::CLAIMED);
        popup_menu(m_list.get_row_at_y(static_cast<int>(y)), x, y);
    });
    m_list.add_controller(right);

    // The keys, heard by the LIST: only while it has the focus. Claimed, so
    // the window's Delete (the canvas selection's) never hears them.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint keyval, guint, Gdk::ModifierType state) {
        const auto mods = state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                   Gdk::ModifierType::ALT_MASK);
        const bool plain = mods == Gdk::ModifierType{};
        const bool menu_key = (keyval == GDK_KEY_Menu && plain) ||
                              (keyval == GDK_KEY_F10 && mods == Gdk::ModifierType::SHIFT_MASK);
        // Backspace is Delete here too: a laptop's "delete" key sends it (s025, Scott).
        const bool del = (keyval == GDK_KEY_Delete || keyval == GDK_KEY_BackSpace) && plain;
        if (!del && !(keyval == GDK_KEY_F2 && plain) && !menu_key) return false;
        auto* row = key_row();
        const RowAct* a = row_act(row);
        if (auto lg = log::get(log::Area::Shell))   // s025 m1: which row the key means
            lg->debug("project list key {:#x} -> row {} '{}'", keyval, row ? row->get_index() : -1, a ? a->name : "");
        if (menu_key) {
            // Under the row's name (its allocation is in the list's coordinates).
            const auto r = row ? row->get_allocation() : Gtk::Allocation(0, 0, 0, 0);
            popup_menu(row, r.get_x() + 24, r.get_y() + r.get_height());
            return true;
        }
        // Not a form or a resource (a heading, a note): nothing -- but the key
        // stays HERE. Falling through, Delete would take the canvas's widget.
        if (!a) return true;
        if (del) {
            if (a->can_trash && a->trash) { auto f = a->trash; f(); }
            return true;   // the main form / the logo: nothing, as the greyed trash
        }
        if (a->rename) { auto f = a->rename; f(); }
        return true;
    }, false);
    m_list.add_controller(keys);
    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_propagate_natural_height(true);
    m_scroll.set_max_content_height(300);   // the widget tree keeps the rest
    append(m_scroll);
}

void ProjectPane::heading(const std::string& text) {
    auto* l = Gtk::make_managed<Gtk::Label>(text);
    l->add_css_class("mortimer-project-group");
    l->set_xalign(0);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*l);
    row->set_activatable(false);
    row->set_selectable(false);
    row->set_focusable(false);   // s025 m1: the arrows step over headings
    m_list.append(*row);
    m_acts.push_back({});
}

void ProjectPane::item(const std::string& name, const std::string& detail, const std::string& trash_tip,
                       bool can_trash, RowAct act, std::function<void()> trash, std::function<void()> rename) {
    auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* n = Gtk::make_managed<Gtk::Label>(name);
    n->set_xalign(0);
    // The name stays whole; the detail gives way (a path loses its start, a form's kind its end).
    line->append(*n);
    auto* d = Gtk::make_managed<Gtk::Label>(detail);
    d->add_css_class("type");
    d->set_xalign(0);
    d->set_hexpand(true);
    d->set_ellipsize(act.kind == RowAct::Form ? Pango::EllipsizeMode::END : Pango::EllipsizeMode::START);
    line->append(*d);
    if (rename) {   // s021 m2: shows on hover, like the trash
        auto* p = Gtk::make_managed<Gtk::Button>();
        p->set_icon_name("document-edit-symbolic");
        p->set_has_frame(false);
        p->set_focusable(false);
        p->add_css_class("mortimer-project-trash");
        p->set_tooltip_text("Rename " + name);
        p->signal_clicked().connect(rename);
        line->append(*p);
    }
    auto* t = Gtk::make_managed<Gtk::Button>();
    t->set_icon_name("user-trash-symbolic");
    t->set_has_frame(false);
    t->set_focusable(false);
    t->add_css_class("mortimer-project-trash");
    t->set_tooltip_text(trash_tip);
    t->set_sensitive(can_trash);
    t->signal_clicked().connect(trash);
    line->append(*t);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*line);
    row->set_tooltip_text(act.kind == RowAct::Form ? "Open " + act.key
                                                   : "Click to copy: " + act.use);
    row->set_name("designer.project.row." + name);
    m_list.append(*row);
    act.name = name;
    act.can_trash = can_trash;
    act.trash = std::move(trash);
    act.rename = std::move(rename);
    m_acts.push_back(std::move(act));
}

ProjectPane::~ProjectPane() {
    m_menu.unparent();   // a widget we parented by hand goes back the same way
}

void ProjectPane::activate_row(std::size_t i) {
    if (i >= m_acts.size()) return;
    const RowAct a = m_acts[i];   // a copy: opening a form repaints us
    if (a.kind == RowAct::Form) m_open_form.emit(a.key);
    else if (a.kind == RowAct::Resource && a.what.empty()) m_open_style.emit();   // s012: the stylesheet -> its editor
    else if (a.kind == RowAct::Resource) m_copy.emit(a.use, a.what);
}

const ProjectPane::RowAct* ProjectPane::row_act(Gtk::ListBoxRow* row) const {
    if (!row) return nullptr;
    const auto i = static_cast<std::size_t>(row->get_index());
    if (i >= m_acts.size() || m_acts[i].kind == RowAct::None) return nullptr;
    return &m_acts[i];
}

Gtk::ListBoxRow* ProjectPane::key_row() const {
    // The keyboard's row first (Tab / arrows move focus without selecting),
    // then the clicked one.
    auto& list = const_cast<widgets::ListBox&>(m_list);
    if (auto* root = list.get_root())
        for (auto* w = root->get_focus(); w && w != &list; w = w->get_parent())
            if (auto* row = dynamic_cast<Gtk::ListBoxRow*>(w); row && row->get_parent() == &list) return row;
    return list.get_selected_row();
}

void ProjectPane::popup_menu(Gtk::ListBoxRow* row, double x, double y) {
    const RowAct* a = row_act(row);
    // The row takes the focus too: when the menu (and any dialog its verb
    // opens) closes, focus comes back HERE -- so the next Delete is the
    // list's, not the canvas's (seen under Xvfb: Cancel, then Delete took a
    // widget).
    if (a && row) { m_list.select_row(*row); row->grab_focus(); }
    m_menu_row = a ? row->get_index() : -1;
    auto menu = Gio::Menu::create();
    if (a) {
        auto own = Gio::Menu::create();
        if (a->kind == RowAct::Form) own->append("Open " + a->name, "project.open");
        else if (a->what.empty()) own->append("Edit Stylesheet…", "project.open");
        else own->append("Copy the " + a->what, "project.copy");
        // The keys shown, not bound (the list hears them): GTK draws "accel".
        auto keyed = [&](const Glib::ustring& label, const Glib::ustring& action, const Glib::ustring& accel) {
            auto item = Gio::MenuItem::create(label, action);
            item->set_attribute_value("accel", Glib::Variant<Glib::ustring>::create(accel));
            own->append_item(item);
        };
        if (a->rename) keyed("Rename…", "project.rename", "F2");
        keyed("Remove…", "project.remove", "Delete");
        menu->append_section(own);
        m_act_remove->set_enabled(a->can_trash);
        m_act_rename->set_enabled(static_cast<bool>(a->rename));
    }
    menu->append_section("Add a form", m_forms_menu);
    menu->append_section("Add a resource", m_res_menu);
    m_menu.set_menu_model(menu);
    double px = x, py = y;   // the list's coordinates -> the pane's (the menu's parent)
    m_list.translate_coordinates(*this, x, y, px, py);
    m_menu.set_pointing_to(Gdk::Rectangle(static_cast<int>(px), static_cast<int>(py), 1, 1));
    m_menu.popup();
}

void ProjectPane::repaint(const std::string& app_dir, const std::string& current_form) {
    m_painting = true;
    // s025 m1: what was selected (and whether the keyboard was in the list)
    // survives the repaint: the same row if it's still there, else the one
    // that took its place -- so a second Delete takes the next row, instead of
    // falling through to the window and deleting a widget on the canvas.
    std::string was_key;
    int was_index = -1;
    if (auto* r = m_list.get_selected_row(); const RowAct* a = row_act(r)) {
        was_key = a->key;
        was_index = r->get_index();
    }
    bool had_focus = false;
    if (auto* root = get_root())
        if (auto* f = root->get_focus()) had_focus = f == &m_list || f->is_ancestor(m_list);
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    m_acts.clear();
    m_menu_row = -1;
    if (app_dir.empty()) {
        auto* l = Gtk::make_managed<Gtk::Label>("A loose form -- no project. File ▸ New Project, or Open Project.");
        l->add_css_class("dim-label");
        l->set_wrap(true);
        l->set_xalign(0);
        l->set_margin_start(10);
        l->set_margin_end(10);
        m_list.append(*l);
        m_acts.push_back({});
        m_painting = false;
        return;
    }
    try {
        const auto pr = core::load_project(app_dir);
        heading("FORMS");
        for (const auto& f : pr.forms) {
            const bool main = f == pr.main_form;
            std::string detail = core::form_kind(app_dir, f);
            if (main) detail += " · main";
            if (f == current_form) detail += " · open";
            item(f, detail, main ? "The main form stays: the app opens it" : "Remove " + f + " (moves to removed/)",
                 !main, {RowAct::Form, f, "", ""}, [this, f] { m_remove_form.emit(f); },
                 [this, f] { m_rename_form.emit(f); });
        }
        const auto res = core::list_resources(app_dir);
        for (const auto& [kind, title] : std::vector<std::pair<std::string, std::string>>{
                 {"icon", "ICONS"}, {"image", "IMAGES"}, {"css", "STYLE"}, {"data", "DATA"}}) {
            bool any = false;
            for (const auto& r : res) {
                if (r.kind != kind) continue;
                if (!any) heading(title);
                any = true;
                const std::string name = kind == "icon" ? r.use : std::filesystem::path(r.file).filename().string();
                const bool logo = r.use == pr.app.bin + "-logo-symbolic";
                const std::string what = kind == "icon" ? "icon name" : kind == "css" ? "" : "resource path";
                item(name, kind == "icon" ? (logo ? "the app's logo" : "") : kind == "css" ? r.use : r.path,
                     logo ? "The logo stays: the .desktop file and About use it"
                          : "Remove " + name + " from the bundle (the file moves to removed/)",
                     !logo, {RowAct::Resource, r.file, kind == "css" ? r.path : r.use, what},
                     [this, file = r.file] { m_remove_resource.emit(file); },
                     logo || kind == "css" ? std::function<void()>{}
                                           : [this, file = r.file] { m_rename_resource.emit(file); });
            }
        }
    } catch (const std::exception& e) {
        auto* l = Gtk::make_managed<Gtk::Label>(e.what());
        l->add_css_class("error");
        l->set_wrap(true);
        m_list.append(*l);
        m_acts.push_back({});
    }
    // Reselect: the same key, else the row now at its place (or the last
    // item above it), else the open form's row.
    Gtk::ListBoxRow* pick = nullptr;
    for (std::size_t i = 0; i < m_acts.size() && !pick; ++i)
        if (m_acts[i].kind != RowAct::None && !was_key.empty() && m_acts[i].key == was_key)
            pick = m_list.get_row_at_index(static_cast<int>(i));
    if (!pick && was_index >= 0)
        for (int i = std::min(was_index, static_cast<int>(m_acts.size()) - 1); i >= 0 && !pick; --i)
            if (m_acts[static_cast<std::size_t>(i)].kind != RowAct::None) pick = m_list.get_row_at_index(i);
    if (!pick)
        for (std::size_t i = 0; i < m_acts.size() && !pick; ++i)
            if (m_acts[i].kind == RowAct::Form && m_acts[i].key == current_form)
                pick = m_list.get_row_at_index(static_cast<int>(i));
    if (pick) {
        m_list.select_row(*pick);
        if (had_focus) pick->grab_focus();
    }
    m_painting = false;
}

}  // namespace mortimer::designer
