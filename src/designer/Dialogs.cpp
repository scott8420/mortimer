#include "designer/Dialogs.hpp"
#include "core/Project.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/stringlist.h>
#include <glibmm/main.h>
#include <algorithm>
#include <gtkmm/filedialog.h>
#include <gtkmm/headerbar.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>

#include <filesystem>

namespace mortimer::designer {

namespace fs = std::filesystem;

// ── New Project ────────────────────────────────────────────────────────────

NewProjectDialog::NewProjectDialog(Gtk::Window& parent) {
    set_name("designer.new_project");
    registry::add("designer.new_project", this);
    set_title("New Project");
    set_modal(true);
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(460, -1);
    set_resizable(false);

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(false);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    hb->pack_start(*cancel);
    m_create.set_label("Create");
    m_create.add_css_class("suggested-action");
    m_create.signal_clicked().connect([this] {
        set_visible(false);
        if (m_done) m_done(m_name.get_text(), m_parent);
    });
    hb->pack_end(m_create);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    page->set_margin(18);
    auto* l1 = Gtk::make_managed<Gtk::Label>("App name");
    l1->set_xalign(0);
    l1->add_css_class("heading");
    page->append(*l1);
    m_name.set_placeholder_text("Notes");
    m_name.signal_changed().connect([this] { update(); });
    m_name.signal_activate().connect([this] { if (m_create.get_sensitive()) m_create.activate(); });
    page->append(m_name);
    auto* l2 = Gtk::make_managed<Gtk::Label>("Made in");
    l2->set_xalign(0);
    l2->add_css_class("heading");
    l2->set_margin_top(6);
    page->append(*l2);
    m_folder.signal_clicked().connect([this] {
        auto dlg = Gtk::FileDialog::create();
        dlg->set_title("Make the app in…");
        dlg->set_initial_folder(Gio::File::create_for_path(m_parent));
        dlg->select_folder(*this, [this, dlg](const Glib::RefPtr<Gio::AsyncResult>& r) {
            try {
                if (auto f = dlg->select_folder_finish(r)) { m_parent = f->get_path(); update(); }
            } catch (const Glib::Error&) {
            }
        });
    });
    page->append(m_folder);
    m_what.set_xalign(0);
    m_what.set_wrap(true);
    m_what.add_css_class("dim-label");
    m_what.set_margin_top(6);
    m_what.set_selectable(true);
    page->append(m_what);
    set_child(*page);
}

void NewProjectDialog::ask(std::function<void(const std::string&, const std::string&)> done) {
    m_done = std::move(done);
    if (m_parent.empty()) {
        const fs::path dev = fs::path(Glib::get_home_dir()) / "Develop";
        m_parent = fs::is_directory(dev) ? dev.string() : Glib::get_home_dir();
    }
    m_name.set_text("");
    update();
    present();
    m_name.grab_focus();
}

void NewProjectDialog::update() {
    m_folder.set_label(m_parent);
    const std::string name = m_name.get_text();
    if (name.empty()) {
        m_what.set_text("The whole workspace: docs/, scripts/, backups/, and the app with its build, "
                        "install and uninstall scripts and a starter window.");
        m_create.set_sensitive(false);
        return;
    }
    try {
        const auto app = core::derive_app(name);
        const fs::path ws = fs::path(m_parent) / app.bin;
        if (fs::exists(ws) && !fs::is_empty(ws)) {
            m_what.set_text(ws.string() + " already exists -- Mortimer never writes over a folder.");
            m_create.set_sensitive(false);
            return;
        }
        m_what.set_text("Makes " + ws.string() + "/\n  binary " + app.bin + " · namespace " + app.ns +
                        " · id " + app.app_id + "\n  the app in " + (ws / app.bin).string() +
                        "/, its first form forms/MainWindow.mort");
        m_create.set_sensitive(true);
    } catch (const std::exception& e) {
        m_what.set_text(e.what());
        m_create.set_sensitive(false);
    }
}

// ── a name ─────────────────────────────────────────────────────────────────

NameDialog::NameDialog(Gtk::Window& parent) {
    set_name("designer.name_dialog");
    registry::add("designer.name_dialog", this);
    set_modal(true);
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(380, -1);
    set_resizable(false);

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(false);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    hb->pack_start(*cancel);
    m_ok.set_label("Add");
    m_ok.add_css_class("suggested-action");
    m_ok.signal_clicked().connect([this] {
        set_visible(false);
        if (m_done) m_done(m_name.get_text());
    });
    hb->pack_end(m_ok);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    page->set_margin(18);
    m_prompt.set_xalign(0);
    m_prompt.set_wrap(true);
    page->append(m_prompt);
    m_name.signal_changed().connect([this] {
        const std::string why = m_check ? m_check(m_name.get_text()) : "";
        m_why.set_text(why);
        m_ok.set_sensitive(why.empty());
    });
    m_name.signal_activate().connect([this] { if (m_ok.get_sensitive()) m_ok.activate(); });
    page->append(m_name);
    m_why.set_xalign(0);
    m_why.set_wrap(true);
    m_why.add_css_class("error");
    page->append(m_why);
    set_child(*page);
}

void NameDialog::ask(const std::string& title, const std::string& prompt, const std::string& initial,
                     std::function<std::string(const std::string&)> check,
                     std::function<void(const std::string&)> done) {
    set_title(title);
    m_prompt.set_text(prompt);
    m_check = std::move(check);
    m_done = std::move(done);
    m_name.set_text(initial);
    m_name.select_region(0, -1);
    const std::string why = m_check ? m_check(initial) : "";
    m_why.set_text(why);
    m_ok.set_sensitive(why.empty());
    present();
    m_name.grab_focus();
}

// ── s013 m3: a shared model, edited ────────────────────────────────────────

ModelDialog::ModelDialog(Gtk::Window& parent) {
    set_name("designer.model_dialog");
    registry::add("designer.model_dialog", this);
    set_modal(true);
    set_transient_for(parent);
    set_hide_on_close(true);
    set_default_size(460, -1);

    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(false);
    auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
    cancel->signal_clicked().connect([this] { set_visible(false); });
    hb->pack_start(*cancel);
    m_ok.set_label("Apply");
    m_ok.add_css_class("suggested-action");
    m_ok.signal_clicked().connect([this] {
        set_visible(false);
        if (m_done) m_done(m_m);
    });
    hb->pack_end(m_ok);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    page->set_margin(18);
    auto* say = Gtk::make_managed<Gtk::Label>(
        "A shared model belongs to every form: Apply rewrites mortimer.json, and every view, editable "
        "column and row bind that shows it follows a rename.");
    say->set_xalign(0);
    say->set_wrap(true);
    say->add_css_class("dim-label");
    page->append(*say);
    m_name.signal_changed().connect([this] { m_m.name = m_name.get_text(); check(); });
    page->append(m_name);
    m_fields.set_column_spacing(6);
    m_fields.set_row_spacing(4);
    page->append(m_fields);
    auto* add = Gtk::make_managed<Gtk::Button>("Add field");
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this] {
        std::string n;
        for (int k = 1; n.empty() || std::any_of(m_m.fields.begin(), m_m.fields.end(),
                                                [&](const core::FieldDef& x) { return x.name == n; }); ++k)
            n = "field" + std::to_string(k);
        m_m.fields.push_back({n, "string", ""});
        rebuild();
    });
    page->append(*add);
    m_why.set_xalign(0);
    m_why.add_css_class("error");
    page->append(m_why);
    set_child(*page);
}

void ModelDialog::ask(const core::ModelDef& m, std::function<void(const core::ModelDef&)> done) {
    m_m = m;
    m_done = std::move(done);
    set_title("Shared model " + m.name);
    m_name.set_text(m.name);
    rebuild();
    present();
}

void ModelDialog::rebuild() {
    while (auto* c = m_fields.get_first_child()) m_fields.remove(*c);
    const auto& types = core::field_types();
    for (std::size_t i = 0; i < m_m.fields.size(); ++i) {
        auto* n = Gtk::make_managed<Gtk::Entry>();
        n->set_text(m_m.fields[i].name);
        n->set_hexpand(true);
        n->signal_changed().connect([this, n, i] { if (i < m_m.fields.size()) { m_m.fields[i].name = n->get_text(); check(); } });
        auto* t = Gtk::make_managed<Gtk::Entry>();
        t->set_text(m_m.fields[i].title);
        t->set_placeholder_text(core::field_title({m_m.fields[i].name, m_m.fields[i].type, ""}));
        t->signal_changed().connect([this, t, i] { if (i < m_m.fields.size()) m_m.fields[i].title = t->get_text(); });
        std::vector<Glib::ustring> shown(types.begin(), types.end());
        auto* ty = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create(shown));
        const auto at = std::find(types.begin(), types.end(), m_m.fields[i].type);
        ty->set_selected(at == types.end() ? 0 : static_cast<guint>(at - types.begin()));
        ty->property_selected().signal_changed().connect([this, ty, i, types] {
            if (i < m_m.fields.size() && ty->get_selected() < types.size()) { m_m.fields[i].type = types[ty->get_selected()]; check(); }
        });
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->set_has_frame(false);
        del->set_tooltip_text("Remove the field (views showing it lose that column / bind)");
        del->signal_clicked().connect([this, i] {
            if (i < m_m.fields.size()) m_m.fields.erase(m_m.fields.begin() + static_cast<long>(i));
            Glib::signal_idle().connect_once([this] { rebuild(); });   // not under the button's own click
        });
        const int r = static_cast<int>(i);
        m_fields.attach(*n, 0, r);
        m_fields.attach(*t, 1, r);
        m_fields.attach(*ty, 2, r);
        m_fields.attach(*del, 3, r);
    }
    check();
}

void ModelDialog::check() {
    std::string why;
    for (const auto& i : core::validate_models({m_m}))
        if (i.error) { why = i.where + ": " + i.what; break; }
    m_why.set_text(why);
    m_why.set_visible(!why.empty());
    m_ok.set_sensitive(why.empty());
}

}  // namespace mortimer::designer
