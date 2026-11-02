#include "Shell.hpp"
#include "core/Resources.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Project.hpp"
#include "core/Recents.hpp"

#include <gtkmm/alertdialog.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>

#include <glibmm/main.h>

#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. Each verb handler is ONE call into
// core::design:: (which goes through the journal), then says where the
// selection goes. None writes the design directly -- the selftest gate walks
// the verbs, so a handler that wrote around them would be invisible to it.

namespace mortimer {

namespace {
Glib::RefPtr<Gio::ListStore<Gtk::FileFilter>> mort_filters() {
    auto f = Gtk::FileFilter::create();
    f->set_name("Mortimer forms (*.mort)");
    f->add_pattern("*.mort");
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    auto list = Gio::ListStore<Gtk::FileFilter>::create();
    list->append(f);
    list->append(all);
    return list;
}
}  // namespace

// ── documents ──────────────────────────────────────────────────────────────

void Shell::on_new() {  // handler: a fresh starter form
    confirm_discard([this] { load_form(core::starter_form(core::derive_app("Untitled")), ""); });
}

void Shell::on_open() {  // handler: file dialog -> open_path
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open a form");
    dialog->set_filters(mort_filters());
    if (!m_path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(std::filesystem::path(m_path).parent_path().string()));
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto file = dialog->open_finish(result)) open_path(file->get_path());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("open: dismissed ({})", e.what());
        }
    });
}

void Shell::open_path(const std::string& path) {  // handler: open a .mort
    confirm_discard([this, path] {
        try {
            const auto f = core::load_form(path);
            load_form(f, path);
            note_recent(path);
        } catch (const std::exception& e) {
            say(e.what(), 2);
            if (auto lg = log::get(log::Area::Io)) lg->warn("open '{}': {}", path, e.what());
        }
    });
}

// ── projects ───────────────────────────────────────────────────────────────

void Shell::on_new_project() {  // handler: ask -> create_workspace -> open its form
    if (!m_new_project) m_new_project = std::make_unique<designer::NewProjectDialog>(*this);
    m_new_project->ask([this](const std::string& name, const std::string& parent) {
        confirm_discard([this, name, parent] {
            try {
                const auto app = core::derive_app(name);
                const std::string dir = core::create_workspace(app, parent);
                core::generate_project(dir);   // generated/ + your half, so it builds at once
                open_path((std::filesystem::path(dir) / "forms" / "MainWindow.mort").string());
                say("Made " + std::filesystem::path(dir).parent_path().string() + " -- build it: cd " + dir +
                    " && ./build.sh && ./build/" + app.bin);
            } catch (const std::exception& e) {
                say(std::string("Couldn't make the project: ") + e.what(), 2);
            }
        });
    });
}

void Shell::on_open_project() {  // handler: folder -> its main form
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open a Mortimer app (its folder, or the workspace around it)");
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto dir = dialog->select_folder_finish(result);
            if (!dir) return;
            namespace fs = std::filesystem;
            fs::path app = dir->get_path();
            if (!fs::exists(app / "mortimer.json") && fs::exists(app / app.filename() / "mortimer.json"))
                app /= app.filename();   // the workspace root: <ws>/<bin>/mortimer.json
            const auto pr = core::load_project(app.string());
            open_path((app / "forms" / (pr.main_form + ".mort")).string());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("open project: dismissed ({})", e.what());
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

void Shell::on_add_form(const std::string& root_type) {  // handler: ask a name -> add_form -> open it
    if (m_app_dir.empty()) { say("Adding a window needs a project -- open one, or File ▸ New Project.", 1); return; }
    const bool dialog = root_type == "Dialog";
    if (!m_name_dialog) m_name_dialog = std::make_unique<designer::NameDialog>(*this);
    const std::string dir = m_app_dir;
    m_name_dialog->ask(
        dialog ? "Add Dialog" : "Add Window",
        std::string("The class name of the new ") + (dialog ? "dialog" : "window") +
            " -- it becomes <Name>Ui (generated) and <Name> (yours).",
        dialog ? "PreferencesDialog" : "Preferences",
        [dir](const std::string& n) -> std::string {
            if (!core::is_identifier(n) || core::is_cpp_keyword(n)) return "\"" + n + "\" isn't a C++ class name";
            if (std::filesystem::exists(std::filesystem::path(dir) / "forms" / (n + ".mort"))) return n + " exists";
            return {};
        },
        [this, dir, root_type](const std::string& n) {
            confirm_discard([this, dir, n, root_type] {
                try {
                    const std::string path = core::add_form(dir, n, root_type);
                    core::generate_project(dir);
                    open_path(path);
                    say("Added " + n + " -- generated/" + n + "Ui.* and your include/" + n + ".hpp, src/" + n +
                        ".cpp. " + (root_type == "Dialog"
                            ? std::string("Show it from your code: keep one (a member), then dlg.present_for(*this).")
                            : std::string("Show it from your code: an instance, then present().")));
                } catch (const std::exception& e) {
                    say(e.what(), 2);
                }
            });
        });
}

void Shell::on_add_about() {  // handler: s010 + ▸ About dialog -> add_about -> generate
    if (m_app_dir.empty()) { say("An About dialog belongs to a project -- open one, or File ▸ New Project.", 1); return; }
    try {
        core::add_about(m_app_dir);
        core::generate_project(m_app_dir);
        rebuild_forms_menu();
        repaint();
        say("Added About (forms/About.mort; your src/About.cpp shows the version). To show it: select the "
            "window, and under Actions set an action to \"opens About\" -- then a menu row or a key runs it.");
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

void Shell::on_open_form(const std::string& stem) {  // handler: another form of this project
    if (m_app_dir.empty()) return;
    open_path((std::filesystem::path(m_app_dir) / "forms" / (stem + ".mort")).string());
}

// ── s009: the navigator's + and trash ─────────────────────────────────────

void Shell::on_add_resource(const std::string& kind) {  // handler: + ▸ icon / image / data / stylesheet
    if (m_app_dir.empty()) { say("Resources belong to a project -- open one, or File ▸ New Project.", 1); return; }
    const std::string dir = m_app_dir;
    auto done = [this](const core::Resource& r) {
        rebuild_forms_menu();
        if (r.kind == "icon")
            say("Added icon " + r.use + " -- an Image's or Button's icon_name; rebuild the app to bundle it.");
        else if (r.kind == "css")
            say("Added resources/" + r.file + " -- every window loads it (generated/Style.cpp). Edit it, rebuild.");
        else
            say("Added resources/" + r.file + " -- load it from " + r.path + "; rebuild the app to bundle it.");
    };
    if (kind == "stylesheet") {
        try { done(core::add_stylesheet(dir)); } catch (const std::exception& e) { say(e.what(), 2); }
        return;
    }
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(kind == "icon" ? "Add an icon (.svg)" : kind == "image" ? "Add an image" : "Add a data file");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto ff = Gtk::FileFilter::create();
    if (kind == "icon") { ff->set_name("SVG icons"); ff->add_pattern("*.svg"); }
    else if (kind == "image") { ff->set_name("Images"); ff->add_mime_type("image/*"); }
    else { ff->set_name("Any file"); ff->add_pattern("*"); }
    filters->append(ff);
    dialog->set_filters(filters);
    dialog->open(*this, [this, dialog, dir, kind, done](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->open_finish(result);
            if (!file) return;
            done(core::add_resource(dir, kind, file->get_path()));
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("add {}: dismissed ({})", kind, e.what());
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

void Shell::on_remove_form(const std::string& stem) {  // handler: trash on a form: ask -> remove_form -> generate
    if (m_app_dir.empty()) return;
    const std::string dir = m_app_dir;
    const bool open = !m_path.empty() && std::filesystem::path(m_path).stem() == stem;
    auto alert = Gtk::AlertDialog::create("Remove " + stem + " from the project?");
    alert->set_detail("forms/" + stem + ".mort, your include/" + stem + ".hpp and src/" + stem +
                      ".cpp move to removed/" + stem + "/ -- nothing is deleted. Its generated files go.\n\n"
                      "If other code uses " + stem + " (an #include, a member), that code won't build "
                      "until you take it out." + (open && dirty() ? "\n\nIt's open, with unsaved changes: they're lost." : ""));
    alert->set_buttons({"Cancel", "Remove"});
    alert->set_cancel_button(0);
    alert->set_default_button(0);
    alert->choose(*this, [this, alert, dir, stem, open](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int which = 0;
        try { which = alert->choose_finish(result); } catch (const Glib::Error&) { return; }
        if (which != 1) return;
        try {
            const std::string went = core::remove_form(dir, stem);
            core::generate_project(dir);
            if (open) {   // the open form went: show the main one (its changes were warned about)
                m_saved = core::form_to_json(m_design.to_form());
                const auto pr = core::load_project(dir);
                on_open_form(pr.main_form);
            }
            rebuild_forms_menu();
            say("Removed " + stem + " -- its files are in " + went + "/; generated/" + stem + "Ui.* pruned.");
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

void Shell::on_remove_resource(const std::string& file) {  // handler: trash on a resource: ask -> remove_resource
    if (m_app_dir.empty()) return;
    const std::string dir = m_app_dir;
    const std::string name = std::filesystem::path(file).filename().string();
    auto alert = Gtk::AlertDialog::create("Remove " + name + " from the bundle?");
    alert->set_detail("resources/" + file + " moves to removed/resources/ -- nothing is deleted. "
                      "Code (or a form's icon_name) that still names it will find nothing.");
    alert->set_buttons({"Cancel", "Remove"});
    alert->set_cancel_button(0);
    alert->set_default_button(0);
    alert->choose(*this, [this, alert, dir, file, name](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int which = 0;
        try { which = alert->choose_finish(result); } catch (const Glib::Error&) { return; }
        if (which != 1) return;
        try {
            const std::string went = core::remove_resource(dir, file);
            rebuild_forms_menu();
            say("Removed " + name + " from the bundle -- the file is in " + went + ".");
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

void Shell::on_save() {  // handler: to its file, or Save As
    if (m_path.empty()) on_save_as();
    else save_to(m_path);
}

void Shell::on_save_as() {  // handler: file dialog -> save_to
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Save the form");
    dialog->set_filters(mort_filters());
    dialog->set_initial_name(m_design.form_name() + ".mort");
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto file = dialog->save_finish(result)) {
                std::string p = file->get_path();
                if (std::filesystem::path(p).extension() != ".mort") p += ".mort";
                if (save_to(p)) note_recent(p);
            }
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("save: dismissed ({})", e.what());
        }
    });
}

bool Shell::on_close() {  // handler: unsaved changes?
    if (!dirty()) return false;
    confirm_discard([this] { close(); });
    return true;   // keep it open until the answer
}

// ── undo ───────────────────────────────────────────────────────────────────

void Shell::on_undo() {  // handler
    const auto r = m_journal.undo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? m_design.root() : r.selection.front();
    say("Undid: " + r.label);
}

void Shell::on_redo() {  // handler
    const auto r = m_journal.redo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? m_design.root() : r.selection.front();
    say("Redid: " + r.label);
}

// ── the verbs ──────────────────────────────────────────────────────────────

void Shell::on_add(const std::string& type) {  // handler: palette -> design::add
    const auto drop = designer::drop_for(m_design, m_sel, type);
    if (drop.parent.empty()) { say("Can't add a " + type + ": " + drop.why, 2); return; }
    std::string why;
    const auto made = core::design::add(m_journal, m_design, type, drop.parent, drop.index, &why);
    if (made.empty()) { say("Can't add a " + type + ": " + why, 2); return; }
    m_sel = made;
    m_journal.reselect({made});
}

void Shell::on_delete() {  // handler: design::remove; neighbour selected
    const std::string gone = m_sel;
    const std::string next = core::design::neighbour(m_design, gone);
    std::string why;
    if (!core::design::remove(m_journal, m_design, gone, &why)) {
        if (!why.empty()) say(why, 2);
        return;
    }
    m_sel = next;
    m_journal.reselect({next});
}

void Shell::on_move_up() {  // handler
    core::design::move_up(m_journal, m_design, m_sel);
}

void Shell::on_move_down() {  // handler
    core::design::move_down(m_journal, m_design, m_sel);
}

void Shell::on_drop(const std::string& payload, const std::string& parent, int index) {  // handler: a drag landed
    // Already resolved by core::design::landing; still a verb each, so the
    // verb checks again (the model may have moved between motion and drop).
    std::string why;
    if (payload.rfind("add:", 0) == 0) {
        const auto made = core::design::add(m_journal, m_design, payload.substr(4), parent, index, &why);
        if (made.empty()) { say("Can't drop there: " + why, 2); return; }
        m_sel = made;
        m_journal.reselect({made});
    } else if (payload.rfind("move:", 0) == 0) {
        const std::string key = payload.substr(5);
        if (!core::design::move(m_journal, m_design, key, parent, index, &why)) {
            if (!why.empty()) say("Can't drop there: " + why, 2);
            return;
        }
        m_sel = key;
        m_journal.reselect({key});
    }
    m_keys_to_tree = true;
}

void Shell::on_select(const std::string& key) {  // handler: tree / canvas picked one
    m_keys_to_tree = true;   // a pick is a pointing gesture: the keys follow it to the tree
    select(key);
}

// ── recents, about, shortcuts, registry ────────────────────────────────────

void Shell::on_recent_open(const std::string& path) {  // handler
    if (path.empty()) return;
    if (!std::filesystem::exists(path)) {
        core::recents_remove(m_recents, path);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents_menu();
        say(path + " is gone -- removed from Open Recent.", 1);
        return;
    }
    open_path(path);
}

void Shell::on_recent_clear() {  // handler
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

void Shell::on_dump_registry() { registry::dump(); }   // handler

void Shell::on_about() {  // handler: hide-on-close singleton, built on first open
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

void Shell::on_shortcuts() {  // handler: renders from core::shortcut_registry()
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

}  // namespace mortimer
