#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow> in ~Shell()
#include "ShortcutsDialog.hpp"  // ditto
#include "Log.hpp"
#include "core/Project.hpp"

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass; every other
// method body lives in a category file (the index is Shell.hpp).

namespace mortimer {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_open_button("shell.open_button"),
      m_undo_button("shell.undo_button"),
      m_redo_button("shell.redo_button"),
      m_save_button("shell.save_button"),
      m_menu_button("shell.menu_button"),
      m_title("shell.title"),
      m_subtitle("shell.subtitle"),
      m_root("shell.root", Gtk::Orientation::VERTICAL),
      m_outer("shell.outer", Gtk::Orientation::HORIZONTAL),
      m_left("shell.left", Gtk::Orientation::VERTICAL),
      m_right("shell.right", Gtk::Orientation::HORIZONTAL),
      m_center("shell.center", Gtk::Orientation::VERTICAL),
      m_status("shell.status") {
    set_name("shell.window");
}

Shell::~Shell() = default;

// construct the frame -> the panes -> actions -> the model wiring -> keys ->
// the first document (the starter form, so the window opens on something).
void Shell::build_ui() {
    build_header();    // zone
    build_body();      // zone
    bind_actions();    // bindings
    bind_model();      // bindings
    bind_keys();       // bindings
    load_form(core::starter_form(core::derive_app("Untitled")), "");
    if (auto lg = log::get(log::Area::Shell)) lg->info("designer ready");
}

}  // namespace mortimer
