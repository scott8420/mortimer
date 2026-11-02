#pragma once
#include "widgets/Widgets.hpp"
#include "core/Design.hpp"
#include "designer/ProjectPane.hpp"
#include "designer/Canvas.hpp"
#include "designer/Dialogs.hpp"
#include "designer/HooksPane.hpp"
#include "designer/Inspector.hpp"
#include "designer/Panes.hpp"

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- Mortimer's window: the designer (s002), and still the split-class
// exemplar. The header is the index: every method's `// category:` tag names
// the file its body lives in.
//
//   palette | tree  ||  live canvas  ||  inspector          (status line below)
//
// The window owns ONE document: a Design (core/Design.hpp) and the journal over
// it. Nothing in the window writes the design except through core::design::
// verbs -- the panes emit what the user did, handlers call verbs, the design's
// change queues ONE repaint of every pane at idle. Undo puts records back and
// the same repaint follows; there is no second path to the screen.
//
// Vocabulary
//   Zones    -- the frame, header, the panes, the menu.       src/Shell_zones.cpp
//   Bindings -- actions, keys, the model<->journal<->UI wiring. src/Shell_bindings.cpp
//   Handlers -- what a user action does (one verb, then where the selection goes).
//                                                             src/Shell_handlers.cpp
//   Helpers  -- repaint, files, title, status, recents.       src/Shell_helpers.cpp
//   Glue     -- ctor + build_ui().                            src/Shell.cpp
//
// Finding things
//   "Where does a palette click go?"  -> Shell_handlers.cpp (on_add)
//   "Where does Ctrl+Z go?"           -> Shell_bindings.cpp (the window's keys) + on_undo
//   "Where is the .mort written?"     -> Shell_helpers.cpp  (save_to)
//   "Who repaints what?"              -> Shell_helpers.cpp  (repaint)
// ─────────────────────────────────────────────────────────────────────────────
namespace mortimer {

class AboutWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue
    void open_path(const std::string& path);     // category: handler: open a .mort (App's on_open)

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_header();                         // category: zone: titlebar
    void build_body();                           // category: zone: the panes + status line
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: win.* + recents group
    void bind_model();                           // category: bindings: design <-> journal <-> panes
    void bind_keys();                            // category: bindings: Ctrl+Z, Delete, Alt+arrows

    // ── handlers ───────────────────────────────────────────────────────────
    void on_new();                               // category: handler: a fresh starter form
    void on_open();                              // category: handler: file dialog -> open_path
    void on_save();                              // category: handler: to its file, or Save As
    void on_save_as();                           // category: handler: file dialog -> save_to
    void on_new_project();                       // category: handler: ask -> create_workspace -> open its form
    void on_open_project();                      // category: handler: folder -> its main form
    void on_add_form(const std::string& root_type);   // category: handler: ask a name -> add_form (Window / Dialog) -> open it
    void on_open_form(const std::string& stem);  // category: handler: another form of this project
    void on_add_resource(const std::string& kind);    // category: handler: s009 + ▸ icon / image / data / stylesheet
    void on_remove_form(const std::string& stem);     // category: handler: s009 trash: ask -> remove_form -> generate
    void on_remove_resource(const std::string& file); // category: handler: s009 trash: ask -> remove_resource
    void on_undo();                              // category: handler
    void on_redo();                              // category: handler
    void on_add(const std::string& type);        // category: handler: palette -> design::add
    void on_delete();                            // category: handler: design::remove; neighbour selected
    void on_move_up();                           // category: handler
    void on_move_down();                         // category: handler
    void on_select(const std::string& key);      // category: handler: tree / canvas picked one
    void on_drop(const std::string& payload, const std::string& parent, int index);   // category: handler: a drag landed
    void on_recent_open(const std::string& p);   // category: handler
    void on_recent_clear();                      // category: handler
    void on_dump_registry();                     // category: handler
    void on_about();                             // category: handler
    void on_shortcuts();                         // category: handler
    bool on_close();                             // category: handler: unsaved changes?

    // ── helpers ────────────────────────────────────────────────────────────
    void load_form(const core::Form& f, const std::string& path);   // category: helper: a new document
    void confirm_discard(std::function<void()> then);   // category: helper: unsaved? ask, then go on
    void generate();                             // category: helper: the project, after a save
    void rebuild_forms_menu();                   // category: helper: the project's forms
    bool save_to(const std::string& path);       // category: helper: write the .mort
    void queue_repaint();                        // category: helper: one repaint per idle
    void repaint();                              // category: helper: every pane from the model
    void select(const std::string& key);         // category: helper: move the selection (not a step)
    void update_undo_ui();                       // category: helper: labels + greying
    void update_title();                         // category: helper: name, file, dirty dot
    bool dirty() const;                          // category: helper
    void say(const std::string& text, int level = 0);   // category: helper: status line (0 info, 1 warn, 2 error)
    void say_validation();                       // category: helper: the form's problems, or "ok"
    void note_recent(const std::string& path);   // category: helper
    void rebuild_recents_menu();                 // category: helper
    std::string recents_file() const;            // category: helper
    bool focus_is_text() const;                  // category: helper

    // Titlebar.
    widgets::Button     m_logo_button;
    widgets::Button     m_open_button;
    widgets::Button     m_undo_button;
    widgets::Button     m_redo_button;
    widgets::Button     m_save_button;
    widgets::MenuButton m_menu_button;
    widgets::Label      m_title;
    widgets::Label      m_subtitle;

    // The document. Declaration order matters: the design before the journal
    // that holds a reference to it, both before the inspector that holds both.
    core::Design        m_design;
    core::DesignJournal m_journal{m_design};
    std::string         m_sel;                 // the selected widget's key
    std::string         m_path;                // "" = never saved
    std::string         m_app_dir;             // the project the form is in; "" = a loose form
    std::string         m_saved;               // form JSON as last saved / loaded
    bool                m_repaint_queued = false;
    bool                m_keys_to_tree = false;    // the next repaint gives the tree the keyboard
    bool                m_said_recently = false;   // a refusal is on the status line: keep it one repaint

    // Body.
    widgets::Box        m_root;                // vertical: panes, status
    widgets::Paned      m_outer;               // left | (canvas | inspector)
    widgets::Paned      m_left;                // palette / tree
    widgets::Paned      m_right;               // (canvas / hooks) | inspector
    widgets::Paned      m_center;              // canvas over the hooks panel
    designer::Palette   m_palette;
    widgets::Box        m_nav;                 // s009: the project over the widget tree
    designer::ProjectPane m_project;
    designer::TreePane  m_tree;
    designer::Canvas    m_canvas;
    designer::HooksPane m_hooks;
    designer::Inspector m_inspector{m_design, m_journal};
    widgets::Label      m_status;

    // Menus + actions.
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;
    Glib::RefPtr<Gio::Menu>         m_edit_menu;
    Glib::RefPtr<Gio::Menu>         m_forms_menu;     // this project's forms, rebuilt on load
    Glib::RefPtr<Gio::SimpleAction> m_act_add_form, m_act_add_dialog;
    std::vector<Glib::RefPtr<Gio::SimpleAction>> m_act_add_res;   // s009: icon, image, data, stylesheet
    Glib::RefPtr<Gio::SimpleAction> m_act_undo, m_act_redo, m_act_delete, m_act_up, m_act_down;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
    std::unique_ptr<designer::NewProjectDialog> m_new_project;
    std::unique_ptr<designer::NameDialog>       m_name_dialog;
};

}  // namespace mortimer
