#pragma once
#include <gtkmm/application.h>
#include <memory>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate. (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace mortimer {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
    void on_startup() override;   // accelerators, driven from core::shortcut_registry()
    void on_open(const type_vec_files& files, const Glib::ustring& hint) override;   // `mortimer Form.mort`

private:
    Shell* shell();               // the one window, built on first need

    Shell* m_shell = nullptr;   // owned by the application via add_window
};

}  // namespace mortimer
