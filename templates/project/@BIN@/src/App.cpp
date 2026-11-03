#include "App.hpp"
#include "Appearance.hpp"
#include "Log.hpp"
#include "MainWindow.hpp"   // your half of forms/MainWindow.mort

#include <gdkmm/display.h>
#include <gtkmm/icontheme.h>

// The resource bundle (resources/@BIN@.gresource.xml -> @BIN@-resources.c) is
// compiled as C, so its getter has C linkage -- no generated header to sync.
// (docs/resources-and-dialog-lifetime.md)
extern "C" GResource* @BIN@_resources_get_resource(void);

namespace @NS@ {

Glib::RefPtr<App> App::create() {
    log::init();
    return Glib::make_refptr_for_instance(new App());
}

App::App() : Gtk::Application("@APP_ID@") {}

void App::on_activate() {
    // A second launch just raises the window that's open.
    if (auto* w = get_active_window()) {
        w->present();
        return;
    }
    // The desktop's light/dark preference FIRST, before any window exists --
    // GTK4 doesn't follow it on its own without libadwaita.
    // (docs/following-the-desktop-appearance.md)
    appearance::follow_system();

    // Icons from the bundle: set_icon_name("@BIN@-logo-symbolic") resolves out of
    // the binary. GTK appends scalable/apps/ itself -- pass only .../icons.
    g_resources_register(@BIN@_resources_get_resource());
    if (auto display = Gdk::Display::get_default())
        Gtk::IconTheme::get_for_display(display)->add_resource_path("@APP_ID_PATH@/icons");

    // The window is the application's: it goes when it's closed (its hooks are
    // cut first -- see the generated destructor).
    //
    // Why hide_on_close: in GTK 4 the X DESTROYS a window outright -- no hide --
    // so a delete hung on signal_hide alone never ran, and the C++ destructor
    // (which cuts the hooks and saves the window's state) was skipped. Closing
    // now hides it, the hide deletes it, and with no window left the app ends.
    auto* win = new MainWindow();
    add_window(*win);
    win->set_hide_on_close(true);
    win->signal_hide().connect([win] { delete win; });
    win->present();
    if (auto lg = log::get(log::Area::App)) lg->info("@APP@ activated");
}

}  // namespace @NS@
