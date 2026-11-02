#pragma once
// designer::NewProjectDialog / designer::NameDialog -- the two small asks.
//
// Same lifetime stone as About (docs/resources-and-dialog-lifetime.md): built
// lazily once, hidden on close, re-presented. Each says, as you type, what
// the answer will make (the binary, the app id, the files) and refuses in
// words before you press the button -- the button greys until it's valid.
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/grid.h>
#include <gtkmm/window.h>
#include "core/Form.hpp"

#include <functional>
#include <string>

namespace mortimer::designer {

class NewProjectDialog : public Gtk::Window {
public:
    explicit NewProjectDialog(Gtk::Window& parent);
    // Present, empty; `done(name, parent_folder)` on Create.
    void ask(std::function<void(const std::string&, const std::string&)> done);

private:
    void update();
    Gtk::Entry  m_name;
    Gtk::Button m_folder;
    Gtk::Label  m_what;
    Gtk::Button m_create;
    std::string m_parent;
    std::function<void(const std::string&, const std::string&)> m_done;
};

class NameDialog : public Gtk::Window {
public:
    explicit NameDialog(Gtk::Window& parent);
    // `check(name)` returns "" when fine, else the reason; `done(name)` on OK.
    void ask(const std::string& title, const std::string& prompt, const std::string& initial,
             std::function<std::string(const std::string&)> check,
             std::function<void(const std::string&)> done);

private:
    Gtk::Label  m_prompt;
    Gtk::Entry  m_name;
    Gtk::Label  m_why;
    Gtk::Button m_ok;
    std::function<std::string(const std::string&)> m_check;
    std::function<void(const std::string&)>        m_done;
};

// s013 m3 -- a SHARED model, edited: its name, and per field a name, a header
// and a type; Add field / remove. Apply hands back the model (checked as you go:
// the button greys with the reason); the shell rewrites mortimer.json and every
// form follows. A file operation like Share -- Cancel is the undo.
class ModelDialog : public Gtk::Window {
public:
    explicit ModelDialog(Gtk::Window& parent);
    void ask(const core::ModelDef& m, std::function<void(const core::ModelDef&)> done);
private:
    void rebuild();        // the field rows from m_m
    void check();          // validate m_m -> the reason, the button
    core::ModelDef  m_m;
    Gtk::Entry      m_name;
    Gtk::Grid       m_fields;
    Gtk::Label      m_why;
    Gtk::Button     m_ok;
    std::function<void(const core::ModelDef&)> m_done;
};

}  // namespace mortimer::designer
