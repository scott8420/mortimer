#pragma once
#include <gtkmm/window.h>

// ─────────────────────────────────────────────────────────────────────────────
// AboutWindow -- Mortimer's About box: two pages, About and Credits, switched in
// the header (s024: Scott -- credits belong on a tab of their own, not on the
// About page).
//
// Custom window, NOT Gtk::AboutDialog: that stock dialog is DEPRECATED in GTK4 --
// a custom Gtk::Window is the forward path. Hide-on-close SINGLETON lifetime
// (CANON: "Lifetime shape is design"): the Shell builds it once and presents it
// again; the X hides it, so its named children never re-register.
//
// The logo is the symbolic icon from the compiled-in gresource bundle,
// recoloured to the theme by GTK. See docs/resources-and-dialog-lifetime.md.
// ─────────────────────────────────────────────────────────────────────────────
namespace mortimer {

class AboutWindow : public Gtk::Window {
public:
    explicit AboutWindow(Gtk::Window& parent);
    ~AboutWindow() override;
};

}  // namespace mortimer
