#pragma once
// designer::ProjectPane -- the navigator's top: the project's forms and its
// bundled resources, with a + that reveals what can be added (s009, Scott:
// "a button near / in the navigator that allows various resources to reveal
// to add to the list" -- "also a way to delete them").
//
//   Project                                    [+]
//   FORMS      MainWindow  ApplicationWindow · main
//              Prefs       Window                 [trash]
//   ICONS      star-symbolic                      [trash]
//   ...
//
// A form row opens the form; a resource row copies what code names (the icon
// name, the resource path). The trash moves things to <app>/removed/ (the
// shell asks first); the pencil renames (s021 m2 -- the shell asks the name). The + menu runs the shell's win.add-* actions.
//
// s025 m1 (Scott: "not obvious how to delete a resource once added"): the
// trash and pencil only show on hover, so the list also answers the keys and
// a right-click. A clicked row is SELECTED (it wears the highlight, so you
// can see what Delete will take); with the list focused:
//   Delete       -> that row's trash (the shell still asks first)
//   F2           -> that row's pencil (GNOME's rename key)
//   Menu, Shift+F10 -> the row's menu, as a right-click opens it
// The menu: the row's own verbs (Open / Copy, Rename..., Remove...) above the
// same Add sections as the + -- on empty space, just the Add sections.
#include "widgets/Widgets.hpp"

#include <functional>

#include <gtkmm/box.h>
#include <gtkmm/listbox.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/scrolledwindow.h>

#include <string>
#include <vector>

namespace mortimer::designer {

class ProjectPane : public widgets::Box {
public:
    ProjectPane();
    ~ProjectPane() override;
    // app_dir "" = a loose form: the list says so and + is greyed by the actions.
    void repaint(const std::string& app_dir, const std::string& current_form);

    sigc::signal<void(const std::string&)>& signal_open_form() { return m_open_form; }
    sigc::signal<void(const std::string&, const std::string&)>& signal_copy() { return m_copy; }   // (text, what)
    sigc::signal<void(const std::string&)>& signal_remove_form() { return m_remove_form; }
    sigc::signal<void(const std::string&)>& signal_remove_resource() { return m_remove_resource; }
    sigc::signal<void(const std::string&)>& signal_rename_form() { return m_rename_form; }          // s021 m2
    sigc::signal<void(const std::string&)>& signal_rename_resource() { return m_rename_resource; }  // s021 m2
    sigc::signal<void()>& signal_open_style() { return m_open_style; }   // s012: the STYLE row

private:
    // What a row does. key = the form's stem or the resource's file; use =
    // what code names (icon name / resource path); what = how the status line
    // says it ("" for the stylesheet: its click opens the editor).
    struct RowAct {
        enum { None, Form, Resource } kind = None;
        std::string key, use, what, name;
        bool can_trash = false;
        std::function<void()> trash, rename;   // the row's trash / pencil; rename empty = no pencil
    };
    void heading(const std::string& text);
    void item(const std::string& name, const std::string& detail, const std::string& trash_tip, bool can_trash,
              RowAct act, std::function<void()> trash, std::function<void()> rename = {});
    void activate_row(std::size_t i);              // a click / Enter: open, copy, or the stylesheet's editor
    const RowAct* row_act(Gtk::ListBoxRow* row) const;   // nullptr for headings and notes
    Gtk::ListBoxRow* key_row() const;              // the row the keys mean: the focused one, else the selected
    void popup_menu(Gtk::ListBoxRow* row, double x, double y);   // row nullptr = empty space: Add only

    Glib::RefPtr<Gio::Menu> m_forms_menu, m_res_menu;   // the + menu's sections, shared with the right-click
    Glib::RefPtr<Gio::SimpleActionGroup> m_row_actions; // "project.open", ".copy", ".rename", ".remove"
    Glib::RefPtr<Gio::SimpleAction> m_act_open, m_act_copy, m_act_rename, m_act_remove;
    Gtk::PopoverMenu        m_menu;     // parented to m_list (unparented in the destructor)
    long                    m_menu_row = -1;   // the row the open menu is about

    widgets::MenuButton     m_add;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox        m_list;
    std::vector<RowAct>     m_acts;   // row index -> what a click does
    bool                    m_painting = false;
    sigc::signal<void(const std::string&)> m_open_form;
    sigc::signal<void(const std::string&, const std::string&)> m_copy;
    sigc::signal<void(const std::string&)> m_remove_form;
    sigc::signal<void(const std::string&)> m_remove_resource;
    sigc::signal<void(const std::string&)> m_rename_form;
    sigc::signal<void(const std::string&)> m_rename_resource;
    sigc::signal<void()> m_open_style;
};

}  // namespace mortimer::designer
