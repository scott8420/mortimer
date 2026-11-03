#include "Shell.hpp"

#include <giomm/menuitem.h>
#include <glibmm/variant.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/separator.h>

// Shell_zones.cpp -- ZONES. The titlebar, the four panes, the status line,
// and the hamburger menu model.

namespace mortimer {

void Shell::build_header() {  // zone: titlebar
    set_title("Mortimer");
    set_default_size(1280, 800);
    auto* header = Gtk::make_managed<Gtk::HeaderBar>();

    // Brand logo (a symbolic icon out of the compiled-in bundle) -> About.
    m_logo_button.set_icon_name("mortimer-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Mortimer");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);

    m_open_button.set_icon_name("document-open-symbolic");
    m_open_button.set_tooltip_text("Open a form… (Ctrl+O)");
    m_open_button.set_action_name("win.open");
    header->pack_start(m_open_button);

    m_undo_button.set_icon_name("edit-undo-symbolic");
    m_undo_button.set_action_name("win.undo");
    m_redo_button.set_icon_name("edit-redo-symbolic");
    m_redo_button.set_action_name("win.redo");
    auto* undo_redo = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    undo_redo->add_css_class("linked");
    undo_redo->append(m_undo_button);
    undo_redo->append(m_redo_button);
    header->pack_start(*undo_redo);

    // Title: the form's class name, the file (or "not saved") beneath it.
    auto* titles = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    titles->set_valign(Gtk::Align::CENTER);
    m_title.add_css_class("title");
    m_subtitle.add_css_class("subtitle");
    m_subtitle.set_ellipsize(Pango::EllipsizeMode::START);
    titles->append(m_title);
    titles->append(m_subtitle);
    header->set_title_widget(*titles);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    m_menu_button.set_tooltip_text("Main menu");
    header->pack_end(m_menu_button);

    m_save_button.set_label("Save");
    m_save_button.add_css_class("suggested-action");
    m_save_button.set_tooltip_text("Save the form (Ctrl+S)");
    m_save_button.set_action_name("win.save");
    header->pack_end(m_save_button);
    set_titlebar(*header);
}

void Shell::build_body() {  // zone: the panes + status line
    // palette over tree, on the left
    m_left.set_start_child(m_palette);
    // s009: the navigator -- the project (forms, resources, +) over the widget tree.
    m_tree.set_vexpand(true);
    m_nav.append(m_project);
    m_nav.append(m_tree);
    m_left.set_end_child(m_nav);
    m_left.set_resize_start_child(false);
    m_left.set_shrink_start_child(false);
    m_left.set_shrink_end_child(false);
    m_left.set_position(420);
    m_left.set_size_request(240, -1);

    // canvas over the hooks panel
    m_center.set_start_child(m_canvas);
    m_center.set_end_child(m_hooks);
    m_center.set_resize_end_child(false);
    m_center.set_shrink_end_child(false);
    m_center.set_shrink_start_child(false);
    m_hooks.set_size_request(-1, 150);

    // (canvas / hooks) | inspector
    m_right.set_start_child(m_center);
    m_right.set_end_child(m_inspector);
    m_right.set_resize_end_child(false);
    m_right.set_shrink_end_child(false);
    m_right.set_shrink_start_child(false);

    m_outer.set_start_child(m_left);
    m_outer.set_end_child(m_right);
    m_outer.set_resize_start_child(false);
    m_outer.set_shrink_start_child(false);
    m_outer.set_shrink_end_child(false);
    m_outer.set_vexpand(true);

    m_status.add_css_class("mortimer-status");
    m_status.set_xalign(0);
    m_status.set_ellipsize(Pango::EllipsizeMode::END);
    m_status.set_selectable(true);
    m_status.set_focusable(false);

    m_root.append(m_outer);
    m_root.append(m_status);
    set_child(m_root);
}

// A menu row that SHOWS its key without owning it (s025, Scott: "why not add
// DEL hotkey on Delete Widget menuitem"). The edit keys aren't accelerators
// on purpose -- an accelerator fires before the focused widget and would take
// Delete / Ctrl+Z / Ctrl+C from every text box in the inspector; the window
// hears them instead (Shell_bindings.cpp, bind_keys). The menu's "accel"
// attribute only DRAWS the key beside the label (GTK reads it for display;
// nothing is bound), so the row tells the truth and the text boxes keep theirs.
void append_keyed(const Glib::RefPtr<Gio::Menu>& menu, const Glib::ustring& label, const Glib::ustring& action,
                  const Glib::ustring& accel) {
    auto item = Gio::MenuItem::create(label, action);
    item->set_attribute_value("accel", Glib::Variant<Glib::ustring>::create(accel));
    menu->append_item(item);
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    file->append("New Scratch Form", "win.new");   // a loose form, no project
    file->append("Open…", "win.open");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Open Recent", m_recents_menu);
    file->append("Save", "win.save");
    file->append("Save As…", "win.save-as");
    file->append("Import .ui…", "win.import-ui");      // s024 m4: Glade / Cambalache -> a form
    file->append("Export as .ui…", "win.export-ui");   // s024 m3: a GtkBuilder copy (the form stays the source)
    menu->append_section(file);

    // The app the form lives in: Mortimer makes the whole workspace.
    auto project = Gio::Menu::create();
    project->append("New Project…", "win.new-project");
    project->append("Open Project…", "win.open-project");
    m_forms_menu = Gio::Menu::create();
    project->append_submenu("Forms", m_forms_menu);
    project->append("Add Window…", "win.add-window");
    project->append("Add Dialog…", "win.add-dialog");
    project->append("Edit Stylesheet…", "win.edit-style");   // s012
    menu->append_section(project);

    // Undo / Redo re-labelled live ("Undo Delete “ok”"), greyed when there's nothing.
    m_edit_menu = Gio::Menu::create();
    append_keyed(m_edit_menu, "Undo", "win.undo", "<Ctrl>z");
    append_keyed(m_edit_menu, "Redo", "win.redo", "<Ctrl><Shift>z");
    menu->append_section(m_edit_menu);

    auto widget = Gio::Menu::create();
    append_keyed(widget, "Cut", "win.cut", "<Ctrl>x");             // s020
    append_keyed(widget, "Copy", "win.copy", "<Ctrl>c");
    append_keyed(widget, "Paste", "win.paste", "<Ctrl>v");
    append_keyed(widget, "Duplicate", "win.duplicate", "<Ctrl>d");
    append_keyed(widget, "Delete Widget", "win.delete", "Delete");
    append_keyed(widget, "Move Up", "win.move-up", "<Alt>Up");
    append_keyed(widget, "Move Down", "win.move-down", "<Alt>Down");
    menu->append_section(widget);

    auto app = Gio::Menu::create();
    app->append("Help", "win.help");   // s024 m6: the user guide (F1)
    app->append("Keyboard Shortcuts", "win.shortcuts");
    app->append("Dump Widget Registry", "win.dump-registry");
    app->append("About Mortimer", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace mortimer
