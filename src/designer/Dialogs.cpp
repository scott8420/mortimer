#include "designer/Dialogs.hpp"
#include "core/Project.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
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

}  // namespace mortimer::designer
