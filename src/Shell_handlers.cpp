#include "Shell.hpp"
#include "core/Resources.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "HelpWindow.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Project.hpp"
#include "core/Recents.hpp"
#include "core/UiFile.hpp"

#include <gtkmm/alertdialog.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>

#include <glibmm/main.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

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
                    std::string path;
                    project_step("Add " + std::string(root_type == "Dialog" ? "dialog " : "window ") + n, [&] {
                        path = core::add_form(dir, n, root_type);
                        core::generate_project(dir);
                    });
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
        project_step("Add About", [this] {
            core::add_about(m_app_dir);
            core::generate_project(m_app_dir);
        });
        rebuild_forms_menu();
        repaint();
        say("Added About (forms/About.mort; your src/About.cpp shows the version). To show it: select the "
            "window, and under Actions set an action to \"opens About\" -- then a menu row or a key runs it.");
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

void Shell::on_edit_style() {  // handler: s012 the stylesheet editor -- makes resources/<bin>.css if needed
    if (m_app_dir.empty()) { say("A stylesheet belongs to a project -- open one, or File ▸ New Project.", 1); return; }
    try {
        const auto pr = core::load_project(m_app_dir);
        const auto path = std::filesystem::path(m_app_dir) / "resources" / (pr.app.bin + ".css");
        if (!std::filesystem::exists(path)) {
            project_step("Add the stylesheet", [this] { core::add_stylesheet(m_app_dir); });
            rebuild_forms_menu();
            say("Added resources/" + pr.app.bin + ".css -- every window of the app loads it (generated/Style.cpp).");
        }
        if (!m_style_editor) m_style_editor = std::make_unique<designer::StyleEditor>(m_canvas);
        m_style_editor->open(path.string(), *this);
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

void Shell::on_share_model(const std::string& name) {  // handler: s013 the form's model -> the project's
    if (m_app_dir.empty() || m_path.empty()) {
        say("Sharing a model needs the form in a project -- save it into an app's forms/ first.", 1);
        return;
    }
    // share_model edits the .mort on disk, so the form is saved first (if it has changes).
    if (dirty() && !save_to(m_path)) return;
    const std::string dir = m_app_dir, path = m_path;
    const std::string stem = std::filesystem::path(path).stem().string();
    try {
        std::vector<std::string> changed;
        project_step("Share model " + name, [&] {
            changed = core::share_model(dir, stem, name);
            core::generate_project(dir);
        });
        load_form(core::load_form(path), path);   // reopened as it is now (the journal starts fresh)
        std::string out;
        for (const auto& c : changed) out += (out.empty() ? "" : ", ") + c;
        say(name + " is the project's now (mortimer.json) -- taken out of " + out +
            ". Every form's views can show it; your code still says " + name + "Item.");
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

bool Shell::save_then(const std::string& what) {  // helper: these change forms on disk -- this one saved first
    if (m_app_dir.empty() || m_path.empty()) {
        say(what + " needs the form in a project -- save it into an app's forms/ first.", 1);
        return false;
    }
    return !dirty() || save_to(m_path);
}

void Shell::on_edit_shared(const std::string& name) {  // handler: s013 m3 the dialog -> every form follows
    if (!save_then("Editing a shared model")) return;
    core::ModelDef m;
    for (const auto& x : m_design.shared()) if (x.name == name) m = x;
    if (m.name.empty()) return;
    if (!m_model_dialog) m_model_dialog = std::make_unique<designer::ModelDialog>(*this);
    const std::string dir = m_app_dir, path = m_path;
    m_model_dialog->ask(m, [this, dir, path, name](const core::ModelDef& now) {
        try {
            std::vector<std::string> changed;
            project_step("Edit shared model " + name, [&] {
                changed = core::set_shared_model(dir, name, now);
                core::generate_project(dir);
            });
            load_form(core::load_form(path), path);
            std::string out;
            for (const auto& c : changed) out += (out.empty() ? "" : ", ") + c;
            say("Shared model " + now.name + " saved in mortimer.json" +
                (out.empty() ? std::string(" -- no form needed to change.") : " -- followed in " + out + "."));
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

void Shell::on_unshare(const std::string& name) {  // handler: s013 m3 each user gets its copy back
    if (!save_then("Unsharing a model")) return;
    const std::string dir = m_app_dir, path = m_path;
    try {
        std::vector<std::string> users;
        project_step("Unshare model " + name, [&] {
            users = core::unshare_model(dir, name);
            core::generate_project(dir);
        });
        load_form(core::load_form(path), path);
        std::string out;
        for (const auto& c : users) out += (out.empty() ? "" : ", ") + c;
        say(users.empty() ? name + " is gone -- no form showed it."
                          : name + " is each form's own again: " + out + " (Share puts it back).");
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

void Shell::on_open_form(const std::string& stem) {  // handler: another form of this project
    if (m_app_dir.empty()) return;
    open_path((std::filesystem::path(m_app_dir) / "forms" / (stem + ".mort")).string());
}

// ── s009: the navigator's + and trash ─────────────────────────────────────

void Shell::on_add_resource(const std::string& kind, std::function<void(const std::string&)> then) {  // handler: + ▸ icon / image / data / stylesheet
    if (m_app_dir.empty()) { say("Resources belong to a project -- open one, or File ▸ New Project.", 1); return; }
    const std::string dir = m_app_dir;
    auto done = [this, then](const core::Resource& r) {
        rebuild_forms_menu();
        if (then && r.kind == "icon") then(r.use);   // s015: the picker's "Add an icon..." picks it too
        if (r.kind == "icon")
            say("Added icon " + r.use + " -- an Image's or Button's icon_name; rebuild the app to bundle it.");
        else if (r.kind == "css")
            say("Added resources/" + r.file + " -- every window loads it (generated/Style.cpp). Edit it, rebuild.");
        else
            say("Added resources/" + r.file + " -- load it from " + r.path + "; rebuild the app to bundle it.");
    };
    if (kind == "stylesheet") {
        try {
            core::Resource r;
            project_step("Add the stylesheet", [&] { r = core::add_stylesheet(dir); });
            done(r);
        } catch (const std::exception& e) { say(e.what(), 2); }
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
            core::Resource r;
            const std::string what = kind == "icon" ? "icon " : kind == "image" ? "image " : "data file ";
            project_step("Add " + what + file->get_basename(), [&] { r = core::add_resource(dir, kind, file->get_path()); });
            done(r);
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
            std::string went;
            project_step("Remove form " + stem, [&] {
                went = core::remove_form(dir, stem);
                core::generate_project(dir);
            });
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
            std::string went;
            project_step("Remove " + name, [&] { went = core::remove_resource(dir, file); });
            rebuild_forms_menu();
            say("Removed " + name + " from the bundle -- the file is in " + went + ".");
        } catch (const std::exception& e) {
            say(e.what(), 2);
        }
    });
}

// ── s021 m2: the navigator's pencil ───────────────────────────────────────

void Shell::on_rename_form(const std::string& stem) {  // handler: ask a name -> (open: the inspector's rename + Save) / rename_project_form
    if (m_app_dir.empty()) return;
    if (!m_name_dialog) m_name_dialog = std::make_unique<designer::NameDialog>(*this);
    const std::string dir = m_app_dir;
    m_name_dialog->ask(
        "Rename " + stem,
        "The new class name. forms/<Name>.mort, generated <Name>Ui and your include/<Name>.hpp + src/<Name>.cpp "
        "follow (the class renamed inside); the forms that open it follow; the originals of your files go to removed/.",
        stem,
        [dir, stem](const std::string& n) -> std::string {
            namespace fs = std::filesystem;
            if (n == stem) return "that's its name now";
            if (!core::is_identifier(n) || core::is_cpp_keyword(n)) return "\"" + n + "\" isn't a C++ class name";
            for (const fs::path rel : {fs::path("forms") / (n + ".mort"), fs::path("include") / (n + ".hpp"),
                                       fs::path("src") / (n + ".cpp")})
                if (fs::exists(fs::path(dir) / rel)) return rel.string() + " is already there";
            return {};
        },
        [this, dir, stem](const std::string& n) {
            const bool open = !m_path.empty() && std::filesystem::path(m_path).stem() == stem;
            if (open) {   // the open form: the same rename the inspector makes, then Save does the files
                std::string why;
                if (!core::design::rename_form(m_journal, m_design, n, &why)) { say(why, 2); return; }
                save_to(m_path);
                return;
            }
            if (!save_then("Renaming a form")) return;   // this form may open it: saved, then reloaded
            const std::string path = m_path;
            try {
                core::RenamedForm r;
                core::GenerateResult g;
                project_step("Rename form " + stem + " -> " + n, [&] {
                    r = core::rename_project_form(dir, stem, n);
                    g = core::generate_project(dir);
                });
                load_form(core::load_form(path), path);
                std::string s = "Renamed " + stem + " -> " + n;
                if (!r.edited.empty()) s += " (" + std::to_string(r.edited.size()) + " file" + (r.edited.size() == 1 ? "" : "s") +
                                            " of yours followed; the originals are in " + r.kept + "/)";
                if (!r.forms.empty()) s += ", " + std::to_string(r.forms.size()) + (r.forms.size() == 1 ? " form opens it" : " forms open it");
                say(s + (g.ok() ? "; generated." : "; generating found a problem -- Save shows it."));
                if (auto lg = log::get(log::Area::Io)) lg->info("renamed form '{}' -> '{}' in '{}'", stem, n, dir);
            } catch (const std::exception& e) {
                say(e.what(), 2);
            }
        },
        "Rename");
}

void Shell::on_rename_resource(const std::string& file) {  // handler: ask a name -> rename_resource -> forms follow
    if (!save_then("Renaming a resource")) return;   // forms on disk may change: this one saved, then reloaded
    if (!m_name_dialog) m_name_dialog = std::make_unique<designer::NameDialog>(*this);
    const std::string dir = m_app_dir, path = m_path;
    const std::string name = std::filesystem::path(file).filename().string();
    const bool icon = file.rfind("icons/", 0) == 0;
    m_name_dialog->ask(
        "Rename " + name,
        icon ? "The new file name (the icon name is the name without .svg). Every form's icon_name that says the old "
               "one follows; code that names it in a string is yours -- you'll be told where."
             : "The new file name (no extension keeps the old one). Code names it by its resource path, in a string -- "
               "that's yours; you'll be told where it still says the old one.",
        name,
        [dir, file, name](const std::string& n) -> std::string {
            namespace fs = std::filesystem;
            std::string full = n;
            if (fs::path(full).extension().empty()) full += fs::path(name).extension().string();
            if (full == name) return "that's its name now";
            for (unsigned char c : full)
                if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') return "letters, digits, . _ - only";
            if (fs::exists(fs::path(dir) / "resources" / fs::path(file).parent_path() / full)) return full + " is already there";
            return {};
        },
        [this, dir, path, file](const std::string& n) {
            try {
                core::RenamedResource r;
                project_step("Rename " + std::filesystem::path(file).filename().string() + " -> " + n, [&] {
                    r = core::rename_resource(dir, file, n);
                    if (!r.forms.empty()) core::generate_project(dir);
                });
                load_form(core::load_form(path), path);   // its icon_name may have followed
                std::string s = "Renamed " + r.was + " -> " + r.now.use;
                if (!r.forms.empty()) {
                    s += " -- followed in ";
                    for (std::size_t i = 0; i < r.forms.size(); ++i) s += (i ? ", " : "") + r.forms[i];
                }
                if (!r.mentions.empty()) {
                    s += ". Still says the old one (a string -- yours): ";
                    for (std::size_t i = 0; i < r.mentions.size(); ++i) s += (i ? ", " : "") + r.mentions[i];
                }
                say(s + ". Rebuild the app to bundle it.", r.mentions.empty() ? 0 : 1);
                if (auto lg = log::get(log::Area::Io)) lg->info("renamed resource '{}' -> '{}'", file, r.now.file);
            } catch (const std::exception& e) {
                say(e.what(), 2);
            }
        },
        "Rename");
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

// s024 m4 -- a GtkBuilder .ui (Glade, Cambalache, an export) as a form. With a
// project open it becomes the project's next form (generated, then opened --
// like Add Window); without one, an unsaved form. Not an undo step: it's a new
// document, like Open. What it couldn't keep (and the actions it made for menu
// rows) goes to the log, the count to the status line.
void Shell::on_import_ui() {  // handler: file dialog -> import_ui_from
    auto f = Gtk::FileFilter::create();
    f->set_name("GtkBuilder (*.ui)");
    f->add_pattern("*.ui");
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(f);
    filters->append(all);
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Import a GtkBuilder .ui");
    dialog->set_filters(filters);
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            if (auto file = dialog->open_finish(result)) import_ui_from(file->get_path());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("import-ui: dismissed ({})", e.what());
        }
    });
}

void Shell::import_ui_from(const std::string& path) {  // handler: core::import_ui -> a form
    const std::string dir = m_app_dir;
    confirm_discard([this, path, dir] {
        try {
            std::ifstream in(path);
            if (!in) throw std::runtime_error("can't read it");
            std::stringstream text;
            text << in.rdbuf();
            std::string name = core::form_name_from(std::filesystem::path(path).stem().string());
            if (!dir.empty())   // the project's: a name it doesn't have yet (Notes, Notes2, ...)
                for (int n = 2; std::filesystem::exists(std::filesystem::path(dir) / "forms" / (name + ".mort")); ++n)
                    name = core::form_name_from(std::filesystem::path(path).stem().string()) + std::to_string(n);
            const auto r = core::import_ui(text.str(), name);
            for (const auto& note : r.notes)
                if (auto lg = log::get(log::Area::Io)) lg->info("import-ui: {}", note);
            const std::string tail = r.notes.empty() ? std::string(".")
                : " -- " + std::to_string(r.notes.size()) + " notes in the log, the first: " + r.notes.front();
            const std::string file = std::filesystem::path(path).filename().string();
            if (dir.empty()) {
                load_form(r.form, "");
                say("Imported " + file + " as " + name + " (unsaved: Save keeps it)" + tail);
            } else {
                std::string at;
                project_step("Import " + file + " as " + name, [&] {
                    at = core::add_form_as(dir, r.form);
                    core::generate_project(dir);
                });
                open_path(at);
                say("Imported " + file + " as the project's form " + name + tail);
            }
        } catch (const std::exception& e) {
            say("Couldn't import " + path + ": " + e.what(), 2);
        }
    });
}

// s024 m3 -- the form as a GtkBuilder .ui, for Glade / Cambalache / Gtk::Builder.
// A copy, not a save: the form (and its undo) are untouched, and what a .ui
// can't say (actions, file dialogs, models...) is listed in the file's header.
void Shell::on_export_ui() {  // handler: file dialog -> core::export_ui
    auto f = Gtk::FileFilter::create();
    f->set_name("GtkBuilder (*.ui)");
    f->add_pattern("*.ui");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(f);
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Export as GtkBuilder .ui");
    dialog->set_filters(filters);
    dialog->set_initial_name(m_design.form_name() + ".ui");
    if (!m_path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(std::filesystem::path(m_path).parent_path().string()));
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->save_finish(result);
            if (!file) return;
            std::string p = file->get_path();
            if (std::filesystem::path(p).extension() != ".ui") p += ".ui";
            const auto x = core::export_ui(m_design.to_form());
            std::ofstream out(p);
            out << x.xml;
            if (!out) { say("Couldn't write " + p); return; }
            if (auto lg = log::get(log::Area::Io)) lg->info("export-ui: {} ({} left out)", p, x.left_out.size());
            say("Exported " + std::filesystem::path(p).filename().string() +
                (x.left_out.empty() ? "" : " -- " + std::to_string(x.left_out.size()) +
                                           " things a .ui can't say are listed at its top"));
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Io)) lg->info("export-ui: dismissed ({})", e.what());
        }
    });
}

bool Shell::on_close() {  // handler: unsaved changes?
    if (!dirty()) return false;
    confirm_discard([this] { close(); });
    return true;   // keep it open until the answer
}

// ── undo ───────────────────────────────────────────────────────────────────

void Shell::on_undo() {  // handler: the one history's last -- a form step or a project step (s025 m2)
    if (m_journal.in_gesture()) m_journal.commit();   // a drag in progress is a step first
    if (m_done.empty()) return;
    if (m_done.back() == 'P') { project_undo(true); return; }
    m_done.pop_back();
    m_undone.push_back('F');
    const auto r = m_journal.undo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? m_design.root() : r.selection.front();
    m_sels = r.selection;   // s018: all of it (repaint prunes what's gone)
    say("Undid: " + r.label);
}

void Shell::on_redo() {  // handler
    if (m_journal.in_gesture()) m_journal.commit();   // (a new step: the redo is gone, as anywhere)
    if (m_undone.empty()) return;
    if (m_undone.back() == 'P') { project_undo(false); return; }
    m_undone.pop_back();
    m_done.push_back('F');
    const auto r = m_journal.redo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? m_design.root() : r.selection.front();
    m_sels = r.selection;
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
    if (m_sels.size() > 1 && m_sels.front() == m_sel) {   // s018: all of them, one step
        const std::string next = core::design::survivor(m_design, m_sels);
        std::string why;
        if (!core::design::remove_many(m_journal, m_design, m_sels, &why)) {
            if (!why.empty()) say(why, 2);
            return;
        }
        m_sel = next;
        m_sels = {next};
        m_journal.reselect({next});
        return;
    }
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

// s020: the clipboard. Mortimer's own (subtrees, not text): it outlives the
// form, so a copy pastes into another form opened after it.
void Shell::on_copy() {  // handler
    auto c = core::design::copy_of(m_design, m_sels.empty() ? std::vector<std::string>{m_sel} : m_sels);
    if (c.empty()) { say("Nothing to copy -- the window is the form", 1); return; }
    m_clip = std::move(c);
    const std::size_t n = m_clip.tops.size();
    say(n == 1 ? "Copied “" + m_clip.recs.front().second.name + "”" + (m_clip.recs.size() > 1 ? " (and what it holds)" : "")
               : "Copied " + std::to_string(n) + " widgets");
    if (m_act_paste) m_act_paste->set_enabled(true);
}

void Shell::on_cut() {  // handler
    auto c = core::design::copy_of(m_design, m_sels.empty() ? std::vector<std::string>{m_sel} : m_sels);
    if (c.empty()) { say("Nothing to cut -- the window is the form", 1); return; }
    m_clip = std::move(c);
    const std::size_t n = m_clip.tops.size();
    const std::size_t before = m_journal.size();
    on_delete();   // the step is the delete: Ctrl+Z puts them back (the clip stays)
    if (m_journal.size() != before)
        say(n == 1 ? "Cut “" + m_clip.recs.front().second.name + "” -- Ctrl+V puts it back somewhere"
                   : "Cut " + std::to_string(n) + " widgets -- Ctrl+V puts them back somewhere");
}

void Shell::on_paste() {  // handler
    if (m_clip.empty()) { say("Nothing to paste -- copy something first", 1); return; }
    // Into the selection if it can take the first one, else just after it (the palette's rule).
    const auto at = core::design::landing_on(m_design, m_clip.recs.front().second.type, "", m_sel);
    std::string why;
    const auto made = at.parent.empty() ? std::vector<std::string>{}
                                        : core::design::paste(m_journal, m_design, m_clip, at.parent, at.index, &why);
    if (made.empty()) { say("Can't paste here: " + (at.parent.empty() ? at.why : why), 2); return; }
    m_sel = made.front();
    m_sels = made;
    m_journal.reselect(made);
    m_keys_to_tree = true;
}

void Shell::on_duplicate() {  // handler
    std::string why;
    const auto made = core::design::duplicate(m_journal, m_design,
                                              m_sels.empty() ? std::vector<std::string>{m_sel} : m_sels, &why);
    if (made.empty()) { if (!why.empty()) say("Can't duplicate: " + why, 2); return; }
    m_sel = made.front();
    m_sels = made;
    m_journal.reselect(made);
    m_keys_to_tree = true;
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
        if (key.find(',') != std::string::npos) {   // s019: several, one step; the selection stays
            if (!core::design::move_many(m_journal, m_design, core::design::split_keys(key), parent, index, &why)) {
                if (!why.empty()) say("Can't drop there: " + why, 2);
                return;
            }
            m_journal.reselect(m_sels);
            m_keys_to_tree = true;
            return;
        }
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

void Shell::on_pick(const std::string& key, int how) {  // handler: s018 -- Ctrl / Shift grow the selection
    if (how == 0 || !m_design.get(key)) { on_select(key); return; }
    std::vector<std::string> cur = m_sels;
    if (cur.empty() || cur.front() != m_sel) cur = {m_sel};
    std::vector<std::string> next;
    if (how == 1) next = core::design::toggle(cur, key);
    else {
        // The range, with the clicked one as primary (the inspector follows the click).
        next = core::design::range(m_design, m_sel, key);
        std::erase(next, key);
        next.insert(next.begin(), key);
    }
    if (next.empty()) return;
    m_sels = next;
    m_sel = next.front();
    m_keys_to_tree = true;
    repaint();
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

void Shell::on_help() {  // handler: s024 m6 -- the user guide, built once
    if (!m_help) m_help = std::make_unique<HelpWindow>();
    m_help->show(*this);
}

void Shell::on_shortcuts() {  // handler: renders from core::shortcut_registry()
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

}  // namespace mortimer
