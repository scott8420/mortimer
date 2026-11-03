#include "Shell.hpp"
#include "Log.hpp"
#include "core/Recents.hpp"
#include "core/Project.hpp"
#include "core/Resources.hpp"

#include <gtkmm/alertdialog.h>
#include <giomm/asyncresult.h>

#include <gtkmm/editable.h>
#include <gtkmm/textview.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/variant.h>
#include <giomm/menuitem.h>

#include <algorithm>
#include <filesystem>
#include <set>

#include <gdkmm/display.h>
#include <gtkmm/icontheme.h>

// Shell_helpers.cpp -- HELPERS. The repaint (every pane from the model), the
// document's files and title, the status line, recents.

namespace mortimer {

// ── the document ───────────────────────────────────────────────────────────

void Shell::load_form(const core::Form& f, const std::string& path) {  // helper: a new document
    m_journal.clear();          // ids of the old document mean nothing in this one
    m_design.load(f);           // a load is not a user choice: no step
    m_path = path;
    m_app_dir = path.empty() ? std::string{} : core::project_of(path);
    if (m_files.root != m_app_dir) {   // s025 m2: another project (or none) -- its own history
        m_files.root = m_app_dir;
        m_pjournal.clear();
    }
    rebuild_forms_menu();
    m_saved = core::form_to_json(m_design.to_form());
    m_disk = path.empty() ? std::string{} : m_saved;   // s025 m3: what the file says, as of now
    watch_form();
    m_sel = m_design.root();
    // The first row a user wants is usually the window's body.
    for (const auto& k : m_design.children(m_sel))
        if (const auto r = m_design.get(k); r && r->pack.find("slot") == r->pack.end()) { m_sel = k; break; }
    update_undo_ui();
    repaint();
    if (!path.empty()) say("Opened " + path);
}

bool Shell::save_to(const std::string& path) {  // helper: write the .mort
    try {
        const core::Form f = m_design.to_form();
        // In a project the file's stem IS the class (forms/<Form>.mort). A
        // renamed form would orphan your include/<Old>.hpp -- refuse in words.
        const std::string app = core::project_of(path);
        const std::string stem = std::filesystem::path(path).stem().string();
        // s021: in a project the file's stem IS the class (forms/<Form>.mort).
        // The form renamed in the inspector + Save = the project renames it:
        // the file, the list, your halves (originals kept in removed/).
        // s025 m3: the file changed on disk since we read or wrote it (an
        // editor, git) -- don't write over it without asking. Keep Mine
        // acknowledges what's there and saves again.
        if (path == m_path && !m_disk_asking) {
            const auto look = core::look_at_disk(path, m_disk, true);
            if (look.what == core::DiskChange::Ask || look.what == core::DiskChange::Broken) {
                ask_disk(look, [this, path] { save_to(path); });
                return false;
            }
        }
        std::string to = path;
        std::string lead;
        if (!app.empty() && f.name != stem) {
            if (path != m_path) {   // Save As onto another form's file: not a rename
                say("That file is the project's form " + stem + " -- save this one under its own name.", 2);
                return false;
            }
            const auto r = core::rename_project_form(app, stem, f.name);   // throws, in words
            to = (std::filesystem::path(app) / "forms" / (f.name + ".mort")).string();
            lead = "Renamed " + stem + " -> " + f.name;
            if (!r.edited.empty()) lead += " (" + std::to_string(r.edited.size()) + " file" + (r.edited.size() == 1 ? "" : "s") +
                                           " of yours followed; the originals are in " + r.kept + "/)";
            if (!r.forms.empty()) lead += ", " + std::to_string(r.forms.size()) + (r.forms.size() == 1 ? " form opens it" : " forms open it");
            core::recents_remove(m_recents, path);
            if (auto lg = log::get(log::Area::Io)) lg->info("renamed form '{}' -> '{}' in '{}'", stem, f.name, app);
        }
        core::save_form(f, to);
        m_path = to;
        m_app_dir = app;
        m_saved = core::form_to_json(f);
        m_disk = m_saved;   // s025 m3: our own write -- the watch will find it the same
        watch_form();
        update_title();
        if (!lead.empty()) note_recent(to);
        if (auto lg = log::get(log::Area::Io)) lg->info("saved '{}'", to);
        if (!m_app_dir.empty()) { generate(lead); rebuild_forms_menu(); repaint(); return true; }
        const auto issues = core::validate(f);
        if (core::has_errors(issues)) say("Saved " + path + " -- but it has problems the generator will refuse.", 1);
        else say("Saved " + path + " (a loose form: save it into an app's forms/ to generate)");
        return true;
    } catch (const std::exception& e) {
        say(std::string("Couldn't save: ") + e.what(), 2);
        return false;
    }
}

void Shell::generate(const std::string& lead) {  // helper: the project, after a save
    try {
        const auto r = core::generate_project(m_app_dir);
        std::string s = (lead.empty() ? "" : lead + ". ") + "Saved and generated: " + std::to_string(r.written.size()) + " written, " +
                        std::to_string(r.unchanged.size()) + " unchanged";
        if (!r.stubs.empty()) s += ", " + std::to_string(r.stubs.size()) + " of yours started";
        // s022: a promoted widget's class -- name it, so you know where to write it.
        for (const auto& f : r.stubs)
            if (f.rfind("include/widgets/", 0) == 0) s += " (" + f + ")";
        if (!r.pruned.empty()) s += ", " + std::to_string(r.pruned.size()) + " pruned";
        int level = 0;
        for (const auto& [form, issue] : r.issues)
            if (issue.error) { s += " -- " + form + ": " + issue.what; level = 2; break; }
        if (!level && !r.notes.empty()) { s += " -- " + r.notes.front(); level = 1; }
        say(s, level);
        m_said_recently = true;
        if (auto lg = log::get(log::Area::Io)) lg->info("generate '{}': {}", m_app_dir, s);
    } catch (const std::exception& e) {
        say(std::string("Saved, but generating failed: ") + e.what(), 2);
        m_said_recently = true;
    }
}

void Shell::confirm_discard(std::function<void()> then) {  // helper: unsaved? ask, then go on
    if (!dirty()) { then(); return; }
    auto alert = Gtk::AlertDialog::create("Save changes to " + m_design.form_name() + "?");
    alert->set_detail("If you don't save, the changes are lost.");
    alert->set_buttons({"Cancel", "Discard", "Save"});
    alert->set_cancel_button(0);
    alert->set_default_button(2);
    alert->choose(*this, [this, alert, then](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int which = 0;
        try { which = alert->choose_finish(result); } catch (const Glib::Error&) { return; }
        if (which == 1) { m_saved = core::form_to_json(m_design.to_form()); then(); }
        else if (which == 2 && !m_path.empty()) { if (save_to(m_path)) then(); }
        else if (which == 2) on_save_as();   // (then the user repeats what they asked)
    });
}

void Shell::rebuild_forms_menu() {  // helper: the project's forms
    if (!m_forms_menu) return;
    m_forms_menu->remove_all();
    if (m_act_add_form) m_act_add_form->set_enabled(!m_app_dir.empty());
    if (m_act_add_dialog) m_act_add_dialog->set_enabled(!m_app_dir.empty());
    for (auto& a : m_act_add_res) a->set_enabled(!m_app_dir.empty());
    // s010: what this form's actions may open, and whether + ▸ About… can add one.
    {
        std::vector<std::string> targets;
        bool has_about = true;
        if (!m_app_dir.empty()) {
            try {
                const auto pr = core::load_project(m_app_dir);
                const std::string me = m_path.empty() ? "" : std::filesystem::path(m_path).stem().string();
                for (const auto& f : pr.forms)
                    if (f != me && f != pr.main_form) targets.push_back(f);
                has_about = std::find(pr.forms.begin(), pr.forms.end(), "About") != pr.forms.end();
            } catch (const std::exception&) {}
        }
        m_inspector.set_targets(std::move(targets));
        // s013: the project's models, for the views' pickers, the canvas and validate.
        try { m_design.set_shared(m_app_dir.empty() ? std::vector<core::ModelDef>{} : core::load_project(m_app_dir).models); }
        catch (const std::exception&) { m_design.set_shared({}); }
        // s012: the canvas wears the project's stylesheet (resources/<bin>.css).
        std::string css;
        if (!m_app_dir.empty()) {
            try {
                css = (std::filesystem::path(m_app_dir) / "resources" / (core::load_project(m_app_dir).app.bin + ".css")).string();
            } catch (const std::exception&) {}
        }
        m_canvas.set_stylesheet(css);
        m_inspector.set_stylesheet(css);
        // s015: the project's own icons, for the icon picker.
        std::vector<std::string> icons;
        if (!m_app_dir.empty()) {
            try {
                for (const auto& r : core::list_resources(m_app_dir)) if (r.kind == "icon") icons.push_back(r.use);
            } catch (const std::exception&) {}
        }
        m_icons = icons;
        m_inspector.set_project_icons(std::move(icons));
        // ...and the canvas draws the project's own icons (resources/icons/scalable/apps/*.svg),
        // as the built app does from its bundle. Added once per project; GTK finds an
        // unthemed <name>.svg in a search-path folder.
        if (!m_app_dir.empty()) {
            static std::set<std::string> added;
            const std::string icons = (std::filesystem::path(m_app_dir) / "resources" / "icons" / "scalable" / "apps").string();
            if (std::filesystem::is_directory(icons) && added.insert(icons).second)
                if (auto display = Gdk::Display::get_default())
                    Gtk::IconTheme::get_for_display(display)->add_search_path(icons);
        }
        if (m_act_add_about) m_act_add_about->set_enabled(!m_app_dir.empty() && !has_about);
        if (m_act_edit_style) m_act_edit_style->set_enabled(!m_app_dir.empty());
    }
    // s009: the navigator lists the same project (and its resources).
    m_project.repaint(m_app_dir, m_path.empty() ? "" : std::filesystem::path(m_path).stem().string());
    if (m_app_dir.empty()) {
        m_forms_menu->append("(not in a project)", "win.none");
        return;
    }
    try {
        const auto pr = core::load_project(m_app_dir);
        for (const auto& f : pr.forms) {
            auto item = Gio::MenuItem::create(f + (f == pr.main_form ? "  (main)" : ""), "");
            item->set_action_and_target("win.open-form", Glib::Variant<Glib::ustring>::create(f));
            m_forms_menu->append_item(item);
        }
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
}

bool Shell::dirty() const {  // helper
    return !m_design.root().empty() && core::form_to_json(m_design.to_form()) != m_saved;
}

// ── repaint ────────────────────────────────────────────────────────────────

void Shell::queue_repaint() {  // helper: one repaint per idle, after the writes
    if (m_repaint_queued) return;
    m_repaint_queued = true;
    Glib::signal_idle().connect_once([this] {
        m_repaint_queued = false;
        repaint();
    });
}

void Shell::repaint() {  // helper: every pane from the model
    if (!m_design.get(m_sel)) m_sel = m_design.root();
    // s018: the selection list follows m_sel (primary first); anything that
    // set m_sel alone (an add, a pick) made it one again.
    std::erase_if(m_sels, [this](const std::string& k) { return !m_design.get(k); });
    if (m_sels.empty() || m_sels.front() != m_sel) m_sels = {m_sel};
    const std::vector<std::string> also(m_sels.begin() + 1, m_sels.end());
    // The tree is rebuilt: if the keyboard was on it (or nowhere), it has to be
    // handed back to the selected row, or GTK drops it into the inspector's
    // first Entry and Ctrl+Z / Delete land in a text box (found driving s002).
    const Gtk::Widget* f = get_focus();
    // s005: so is a button the inspector rebuilds away (Add action / Add file
    // dialog, a delete) -- any inspector control that isn't a text box.
    const bool in_inspector = f && f->is_ancestor(m_inspector);
    bool keys_to_tree = !f || f->is_ancestor(m_tree) || f->is_ancestor(m_canvas) || m_keys_to_tree ||
                        (in_inspector && !focus_is_text());
    m_keys_to_tree = false;
    m_palette.repaint(m_design, m_sel);
    m_tree.repaint(m_design, m_sel, also);
    m_canvas.show(m_design, m_sel, also);
    // A rebuild while the keyboard was in it (a rename committed with Enter)
    // destroyed the focused box: GTK would drop the keyboard into the first
    // Entry, text selected, and the next keys would rename the root.
    if (m_inspector.show(m_sel) && in_inspector) keys_to_tree = true;
    const auto r = m_design.get(m_sel);
    const bool child = r && !r->parent.empty();
    if (m_act_delete) m_act_delete->set_enabled(child);
    if (m_act_up)     m_act_up->set_enabled(child);
    if (m_act_down)   m_act_down->set_enabled(child);
    if (m_act_copy)   m_act_copy->set_enabled(child);
    if (m_act_cut)    m_act_cut->set_enabled(child);
    if (m_act_dup)    m_act_dup->set_enabled(child);
    if (m_act_paste)  m_act_paste->set_enabled(!m_clip.empty() && !m_design.root().empty());
    update_title();
    if (!m_design.root().empty()) m_hooks.show(core::live_report(m_app_dir, m_design.to_form()), m_app_dir);
    if (keys_to_tree) m_tree.focus_selection();
    if (m_said_recently) m_said_recently = false;   // the refusal stays up for this repaint
    else if (m_sels.size() > 1)
        say(std::to_string(m_sels.size()) + " selected -- the inspector shows “" + m_design.get(m_sel)->name +
            "”; Delete removes them all, Escape keeps just it");
    else say_validation();
}

void Shell::select(const std::string& key) {  // helper: move the selection (not a step)
    if ((key == m_sel && m_sels.size() <= 1) || !m_design.get(key)) return;
    m_sel = key;
    m_sels = {key};
    repaint();
}

void Shell::update_undo_ui() {  // helper: labels + greying
    // s025 m2: what Ctrl+Z / Ctrl+Y would do -- the one history's ends.
    const bool cu = !m_done.empty(), cr = !m_undone.empty();
    if (m_act_undo) m_act_undo->set_enabled(cu);
    if (m_act_redo) m_act_redo->set_enabled(cr);
    const std::string u = cu ? "Undo " + (m_done.back() == 'P' ? m_pjournal.undo_label() : m_journal.undo_label()) : "Undo";
    const std::string r = cr ? "Redo " + (m_undone.back() == 'P' ? m_pjournal.redo_label() : m_journal.redo_label()) : "Redo";
    m_undo_button.set_tooltip_text(u + " (Ctrl+Z)");
    m_redo_button.set_tooltip_text(r + " (Ctrl+Shift+Z)");
    if (m_edit_menu) {
        m_edit_menu->remove_all();
        append_keyed(m_edit_menu, u, "win.undo", "<Ctrl>z");
        append_keyed(m_edit_menu, r, "win.redo", "<Ctrl><Shift>z");
    }
}

void Shell::update_title() {  // helper: name, file, dirty dot
    const std::string name = m_design.form_name().empty() ? "Untitled" : m_design.form_name();
    const bool d = dirty();
    m_title.set_text((d ? "• " : "") + name);
    if (m_path.empty()) m_subtitle.set_text("not saved");
    else if (!m_app_dir.empty()) {
        std::string app = std::filesystem::path(m_app_dir).filename().string();
        try { app = core::load_project(m_app_dir).app.name; } catch (const std::exception&) {}
        m_subtitle.set_text(app + " · forms/" + std::filesystem::path(m_path).filename().string());
        m_subtitle.set_tooltip_text(m_path);
    } else m_subtitle.set_text(m_path);
    set_title(name + " — Mortimer");
}

// ── the status line ────────────────────────────────────────────────────────

void Shell::say(const std::string& text, int level) {  // helper: 0 info, 1 warn, 2 error
    m_status.set_text(text);
    m_status.remove_css_class("warn");
    m_status.remove_css_class("error");
    if (level == 1) m_status.add_css_class("warn");
    if (level == 2) m_status.add_css_class("error");
    // A message said during a verb outlives the repaint the verb queues.
    m_said_recently = m_repaint_queued;
}

void Shell::say_validation() {  // helper: the form's problems, or "ok"
    if (m_design.root().empty()) return;
    const auto issues = core::validate(m_design.to_form());
    int errors = 0, warnings = 0;
    const core::Issue* first = nullptr;
    for (const auto& i : issues) {
        (i.error ? errors : warnings)++;
        if (!first || (i.error && !first->error)) first = &i;
    }
    if (!first) {
        // s015: an icon_name neither the project nor the theme has draws as "missing".
        std::vector<std::string> missing;
        auto theme = Gtk::IconTheme::get_for_display(get_display());
        for (const auto& k : m_design.all()) {
            const auto r = m_design.get(k);
            for (const auto* m : {&r->props, &r->pack}) {
                const auto it = m->find("icon_name");
                if (it == m->end() || !std::holds_alternative<std::string>(it->second)) continue;
                const auto& n = std::get<std::string>(it->second);
                if (n.empty() || std::find(m_icons.begin(), m_icons.end(), n) != m_icons.end() || theme->has_icon(n))
                    continue;
                missing.push_back(r->name + ": \u201C" + n + "\u201D");
            }
        }
        if (!missing.empty()) {
            say((missing.size() > 1 ? std::to_string(missing.size()) + " icons missing -- " : std::string("icon missing -- ")) +
                    missing.front() + " isn't in the project or this theme (it draws as missing; the picker adds one)",
                1);
            m_said_recently = false;
            return;
        }
        say(std::to_string(m_design.count()) + " widgets · the form is valid");
        return;
    }
    std::string s = first->where + ": " + first->what;
    if (errors + warnings > 1) s = std::to_string(errors + warnings) + " problems -- " + s;
    say(s, errors ? 2 : 1);
    m_said_recently = false;
}

// ── recents ────────────────────────────────────────────────────────────────

std::string Shell::recents_file() const {  // helper: the XDG path (the UI resolves it -- the seam)
    return (std::filesystem::path(Glib::get_user_data_dir()) / "mortimer" / "recent-forms.json").string();
}

void Shell::note_recent(const std::string& path) {  // helper
    if (path.empty()) return;
    core::recents_add(m_recents, path);
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
}

void Shell::rebuild_recents_menu() {  // helper
    if (!m_recents_menu) return;
    m_recents_menu->remove_all();
    for (const auto& p : m_recents) {
        const std::filesystem::path fp(p);
        auto item = Gio::MenuItem::create(fp.stem().string() + "  (" + fp.parent_path().filename().string() + ")", "");
        item->set_action_and_target("recents.open", Glib::Variant<Glib::ustring>::create(p));
        m_recents_menu->append_item(item);
    }
    auto clear = Gio::Menu::create();
    clear->append("Clear Recent", "recents.clear");
    m_recents_menu->append_section("", clear);
    if (m_recents_clear) m_recents_clear->set_enabled(!m_recents.empty());
}

bool Shell::focus_is_text() const {  // helper: the keyboard is in a text box (its own Ctrl+Z)
    for (const Gtk::Widget* w = get_focus(); w; w = w->get_parent())
        if (dynamic_cast<const Gtk::TextView*>(w) || dynamic_cast<const Gtk::Editable*>(w)) return true;
    return false;
}

}  // namespace mortimer

namespace mortimer {

// ── s025 m2: the project's verbs in the undo ──────────────────────────────

bool Shell::project_step(const std::string& label, const std::function<void()>& op) {  // helper: a project verb, one undo step
    return core::project_run(m_pjournal, label, op);   // op's exception comes back out, after the step is kept
}

// Keep m_done / m_undone in step with the two journals. A journal says only
// "something changed"; what changed is read from its counts:
//   kept() grew      -> new steps (a new action: everything redoable goes,
//                       in BOTH journals -- one history, one future);
//   done() < marked  -> steps went (a merge emptied one, a clear, the cap);
//   redo count < marked -> redo steps went.
// Undo / redo move their mark BEFORE calling the journal, so their notify
// finds the counts already right.
void Shell::reconcile_history() {  // helper: the one history <- both journals
    if (m_reconciling) return;   // drop_redo() below notifies us again
    m_reconciling = true;
    auto erase_one = [](std::vector<char>& v, char c, bool from_front) {
        if (from_front) { if (auto it = std::find(v.begin(), v.end(), c); it != v.end()) v.erase(it); }
        else if (auto it = std::find(v.rbegin(), v.rend(), c); it != v.rend()) v.erase(std::next(it).base());
    };
    auto sync = [&](char c, std::size_t kept, std::size_t& seen, std::size_t done, std::size_t size, auto&& drop_other) {
        const std::size_t fresh = kept - seen;
        seen = kept;
        for (std::size_t i = 0; i < fresh; ++i) m_done.push_back(c);
        if (fresh > 0) {
            m_undone.clear();
            drop_other();
        }
        auto have = static_cast<std::size_t>(std::count(m_done.begin(), m_done.end(), c));
        for (; have > done; --have) erase_one(m_done, c, fresh > 0);   // the cap drops the oldest; else the newest
        auto later = static_cast<std::size_t>(std::count(m_undone.begin(), m_undone.end(), c));
        for (; later > size - done; --later) erase_one(m_undone, c, true);   // the farthest redo first
    };
    sync('F', m_journal.kept(), m_kept_f, m_journal.done(), m_journal.size(), [this] { m_pjournal.drop_redo(); });
    sync('P', m_pjournal.kept(), m_kept_p, m_pjournal.done(), m_pjournal.size(), [this] { m_journal.drop_redo(); });
    m_reconciling = false;
}

// Undo (or redo) the project step at the history's end. Refused, in words,
// when a file it would put back was changed since (yours, never overwritten),
// or when it changes the open form's file while the form has unsaved edits.
bool Shell::project_undo(bool undo) {  // helper: check, put the files back, follow
    namespace fs = std::filesystem;
    const std::string label = undo ? m_pjournal.undo_label() : m_pjournal.redo_label();
    const std::string verb = undo ? "undo" : "redo";
    if (const std::string c = core::project_conflict(m_pjournal, undo); !c.empty()) {
        say("Can't " + verb + " \"" + label + "\": " + c + " changed since -- " + verb + "ing would lose that.", 2);
        return false;
    }
    const auto touched = core::project_touched(m_pjournal, undo);
    const std::string open_rel = m_path.empty() || m_app_dir.empty()
        ? std::string{} : fs::path(m_path).lexically_relative(m_app_dir).generic_string();
    const bool hits_open = std::find_if(touched.begin(), touched.end(), [&](const std::string& t) {
        return t == open_rel || t == "mortimer.json";
    }) != touched.end();
    if (hits_open && dirty()) {
        say("Save " + m_design.form_name() + " first -- " + verb + "ing \"" + label + "\" changes its file.", 1);
        return false;
    }
    if (undo) { m_done.pop_back(); m_undone.push_back('P'); m_pjournal.undo(); }
    else      { m_undone.pop_back(); m_done.push_back('P'); m_pjournal.redo(); }
    // Follow the files: the open form gone (an add undone) -> the main form;
    // its file (or the shared models) changed -> reopened as it is now.
    try {
        if (!m_path.empty() && !fs::exists(m_path) && !m_app_dir.empty()) {
            const auto pr = core::load_project(m_app_dir);
            const std::string main = (fs::path(m_app_dir) / "forms" / (pr.main_form + ".mort")).string();
            load_form(core::load_form(main), main);
        } else if (hits_open && !m_path.empty()) {
            load_form(core::load_form(m_path), m_path);
        }
    } catch (const std::exception& e) {
        say(e.what(), 2);
    }
    rebuild_forms_menu();
    repaint();
    say((undo ? "Undid: " : "Redid: ") + label);
    if (auto lg = log::get(log::Area::Io)) lg->info("project {}: '{}' ({} files)", verb, label, touched.size());
    return true;
}

}  // namespace mortimer

namespace mortimer {

// ── s025 m3: the open form vs its file ────────────────────────────────────

void Shell::watch_form() {  // helper: watch m_path and its forms/
    if (m_path != m_watched_path) {
        m_watched_path = m_path;
        m_disk_later.disconnect();
        m_form_watch.reset();
        if (!m_path.empty()) {
            // Editors save by writing, or by renaming a new file over the old;
            // git replaces it. Any event -> one look, a beat after the last.
            m_form_watch = Gio::File::create_for_path(m_path)->monitor_file();
            m_form_watch->signal_changed().connect([this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                                                          Gio::FileMonitor::Event) {
                m_disk_later.disconnect();
                m_disk_later = Glib::signal_timeout().connect([this] { check_disk(); return false; }, 250);
            });
        }
    }
    // The project's forms are mortimer.json's list (a loose .mort in forms/
    // isn't one): a git pull that adds or drops a form changes that file.
    const std::string dir = m_app_dir.empty() ? std::string{} : (std::filesystem::path(m_app_dir) / "mortimer.json").string();
    if (dir != m_watched_dir) {
        m_watched_dir = dir;
        m_dir_later.disconnect();
        m_forms_dir_watch.reset();
        if (!dir.empty()) {
            m_forms_dir_watch = Gio::File::create_for_path(dir)->monitor_file();
            m_forms_dir_watch->signal_changed().connect([this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                                                               Gio::FileMonitor::Event) {
                m_dir_later.disconnect();
                m_dir_later = Glib::signal_timeout().connect([this] {
                    // Just the list and the Forms menu -- a whole repaint would
                    // also re-say the status line over what was just said.
                    rebuild_forms_menu();
                    m_project.repaint(m_app_dir, m_path.empty() ? "" : std::filesystem::path(m_path).stem().string());
                    return false;
                }, 400);
            });
        }
    }
}

void Shell::check_disk() {  // helper: the file changed? reload / ask / say
    if (m_path.empty() || m_disk_asking) return;
    const auto look = core::look_at_disk(m_path, m_disk, dirty());
    const std::string file = std::filesystem::path(m_path).filename().string();
    switch (look.what) {
        case core::DiskChange::Same:
            return;
        case core::DiskChange::Reload:
            reload_from_disk();
            say(file + " changed on disk -- reloaded. (Its undo history starts here.)");
            return;
        case core::DiskChange::Ask:
            ask_disk(look);
            return;
        case core::DiskChange::Gone:
            say(file + " was deleted or moved on disk -- the designer still has it; Save writes it back.", 1);
            return;
        case core::DiskChange::Broken:
            say(file + " on disk doesn't read as a form (" + look.error + ") -- kept the designer's copy.", 2);
            return;
    }
}

void Shell::reload_from_disk() {  // helper: the file's form, the selection kept by name
    std::vector<std::string> names;
    for (const auto& k : m_sels)
        if (const auto r = m_design.get(k)) names.push_back(r->name);
    try {
        load_form(core::load_form(m_path), m_path);
    } catch (const std::exception& e) {
        say(e.what(), 2);
        return;
    }
    std::vector<std::string> keep;
    for (const auto& n : names)
        if (const auto k = m_design.find(n); !k.empty()) keep.push_back(k);
    if (!keep.empty()) { m_sel = keep.front(); m_sels = keep; repaint(); }
}

// The file changed AND there are unsaved edits here (or it doesn't parse and
// a Save is about to write over it). Reload -- lose mine; Keep Mine -- what's
// on disk is acknowledged, the next Save overwrites it (after_keep: the Save
// that asked goes ahead).
void Shell::ask_disk(const core::DiskLook& look, std::function<void()> after_keep) {  // helper: reload or keep mine
    m_disk_asking = true;
    const std::string file = std::filesystem::path(m_path).filename().string();
    const bool broken = look.what == core::DiskChange::Broken;
    auto alert = Gtk::AlertDialog::create(file + (broken ? " on disk doesn't read as a form" : " changed on disk"));
    alert->set_detail(broken
        ? "Another program left it unreadable (" + look.error + "). Keep Mine writes the designer's copy over it."
        : "Another program changed it (an editor, a git pull) while you have unsaved changes here.\n\n"
          "Reload takes the file as it is now -- your unsaved changes are lost.\n"
          "Keep Mine keeps editing; your next Save overwrites the file on disk.");
    if (broken) alert->set_buttons({"Cancel", "Keep Mine"});
    else alert->set_buttons({"Keep Mine", "Reload"});
    alert->set_cancel_button(0);
    alert->set_default_button(0);
    const std::string json = look.json;
    alert->choose(*this, [this, alert, broken, json, after_keep](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int which = 0;
        try { which = alert->choose_finish(result); } catch (const Glib::Error&) { which = 0; }
        m_disk_asking = false;
        if (broken) {
            if (which != 1) return;
            m_disk = core::look_at_disk(m_path, "", true).json;   // "" for a broken file: Save goes ahead
            m_disk_asking = true;   // the Save below must not ask again
            if (after_keep) after_keep();
            m_disk_asking = false;
            return;
        }
        if (which == 1) {
            reload_from_disk();
            say("Reloaded " + std::filesystem::path(m_path).filename().string() + " from disk -- your unsaved changes are gone.");
            return;
        }
        m_disk = json;   // acknowledged: the next Save overwrites it
        say("Kept your copy -- Save overwrites " + std::filesystem::path(m_path).filename().string() + " on disk.", 1);
        if (after_keep) after_keep();
    });
}

}  // namespace mortimer
