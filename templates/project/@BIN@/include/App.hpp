#pragma once
#include <gtkmm/application.h>

// App -- the Gtk::Application. Thin (CANON: main -> App::create() -> window):
// logging up in create(), the desktop's light/dark and the compiled-in icons
// on activate, then the main window.
namespace @NS@ {

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

protected:
    App();
    void on_activate() override;
};

}  // namespace @NS@
