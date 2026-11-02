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
// shell asks first). The + menu runs the shell's win.add-* actions.
#include "widgets/Widgets.hpp"

#include <functional>

#include <gtkmm/box.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/scrolledwindow.h>

#include <string>
#include <vector>

namespace mortimer::designer {

class ProjectPane : public widgets::Box {
public:
    ProjectPane();
    // app_dir "" = a loose form: the list says so and + is greyed by the actions.
    void repaint(const std::string& app_dir, const std::string& current_form);

    sigc::signal<void(const std::string&)>& signal_open_form() { return m_open_form; }
    sigc::signal<void(const std::string&, const std::string&)>& signal_copy() { return m_copy; }   // (text, what)
    sigc::signal<void(const std::string&)>& signal_remove_form() { return m_remove_form; }
    sigc::signal<void(const std::string&)>& signal_remove_resource() { return m_remove_resource; }

private:
    struct RowAct { enum { None, Form, Resource } kind = None; std::string key, use, what; };
    void heading(const std::string& text);
    void item(const std::string& name, const std::string& detail, const std::string& trash_tip, bool can_trash,
              RowAct act, std::function<void()> trash);

    widgets::MenuButton     m_add;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox        m_list;
    std::vector<RowAct>     m_acts;   // row index -> what a click does
    bool                    m_painting = false;
    sigc::signal<void(const std::string&)> m_open_form;
    sigc::signal<void(const std::string&, const std::string&)> m_copy;
    sigc::signal<void(const std::string&)> m_remove_form;
    sigc::signal<void(const std::string&)> m_remove_resource;
};

}  // namespace mortimer::designer
