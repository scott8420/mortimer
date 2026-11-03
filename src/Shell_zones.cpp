#include "Shell.hpp"

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

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    file->append("New Scratch Form", "win.new");   // a loose form, no project
    file->append("Open…", "win.open");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Open Recent", m_recents_menu);
    file->append("Save", "win.save");
    file->append("Save As…", "win.save-as");
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
    m_edit_menu->append("Undo", "win.undo");
    m_edit_menu->append("Redo", "win.redo");
    menu->append_section(m_edit_menu);

    auto widget = Gio::Menu::create();
    widget->append("Cut", "win.cut");             // s020
    widget->append("Copy", "win.copy");
    widget->append("Paste", "win.paste");
    widget->append("Duplicate", "win.duplicate");
    widget->append("Delete Widget", "win.delete");
    widget->append("Move Up", "win.move-up");
    widget->append("Move Down", "win.move-down");
    menu->append_section(widget);

    auto app = Gio::Menu::create();
    app->append("Keyboard Shortcuts", "win.shortcuts");
    app->append("Dump Widget Registry", "win.dump-registry");
    app->append("About Mortimer", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace mortimer
