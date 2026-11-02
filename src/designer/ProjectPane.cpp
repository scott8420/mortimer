#include "designer/ProjectPane.hpp"
#include "core/Project.hpp"
#include "core/Resources.hpp"

#include <giomm/menu.h>
#include <filesystem>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>

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
    auto forms = Gio::Menu::create();
    forms->append("Window…", "win.add-window");
    forms->append("Dialog…", "win.add-dialog");
    forms->append("About dialog", "win.add-about");   // s010: logo, name, version, Close
    menu->append_section("Forms", forms);
    auto res = Gio::Menu::create();
    res->append("Icon (.svg)…", "win.add-icon");
    res->append("Image…", "win.add-image");
    res->append("Stylesheet", "win.edit-style");   // s012: makes it if needed, opens the editor
    res->append("Data file…", "win.add-data");
    menu->append_section("Resources", res);
    m_add.set_menu_model(menu);
    m_add.set_icon_name("list-add-symbolic");
    m_add.set_has_frame(false);
    m_add.set_tooltip_text("Add a form or a resource to the project");
    m_add.set_margin_end(4);
    head->append(m_add);
    append(*head);

    m_list.set_selection_mode(Gtk::SelectionMode::NONE);
    m_list.set_activate_on_single_click(true);
    m_list.add_css_class("navigation-sidebar");
    m_list.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const auto i = static_cast<std::size_t>(row->get_index());
        if (m_painting || i >= m_acts.size()) return;
        const RowAct a = m_acts[i];
        if (a.kind == RowAct::Form) m_open_form.emit(a.key);
        else if (a.kind == RowAct::Resource && a.what.empty()) m_open_style.emit();   // s012: the stylesheet -> its editor
        else if (a.kind == RowAct::Resource) m_copy.emit(a.use, a.what);
    });
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
    m_list.append(*row);
    m_acts.push_back({});
}

void ProjectPane::item(const std::string& name, const std::string& detail, const std::string& trash_tip,
                       bool can_trash, RowAct act, std::function<void()> trash) {
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
    m_acts.push_back(std::move(act));
}

void ProjectPane::repaint(const std::string& app_dir, const std::string& current_form) {
    m_painting = true;
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    m_acts.clear();
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
                 !main, {RowAct::Form, f, "", ""}, [this, f] { m_remove_form.emit(f); });
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
                     [this, file = r.file] { m_remove_resource.emit(file); });
            }
        }
    } catch (const std::exception& e) {
        auto* l = Gtk::make_managed<Gtk::Label>(e.what());
        l->add_css_class("error");
        l->set_wrap(true);
        m_list.append(*l);
        m_acts.push_back({});
    }
    m_painting = false;
}

}  // namespace mortimer::designer
