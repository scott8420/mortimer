#include "AboutWindow.hpp"
#include "widgets/Widgets.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/grid.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>

#include <string>
#include <utility>
#include <vector>

#ifndef MORTIMER_VERSION
#define MORTIMER_VERSION "0.0.0"
#endif

namespace mortimer {

namespace {
const char* const kSite = "https://github.com/scott8420/mortimer";

Gtk::Label* label(const std::string& markup, bool dim = false) {
    auto* l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(markup);
    l->set_wrap(true);
    l->set_justify(Gtk::Justification::CENTER);
    l->set_max_width_chars(44);
    if (dim) l->add_css_class("dim-label");
    return l;
}
}  // namespace

AboutWindow::AboutWindow(Gtk::Window& parent) {
    set_name("shell.about");
    registry::add("shell.about", this);

    set_title("About Mortimer");
    set_modal(true);
    // Resizable, at a set size: a NON-resizable GTK 4 window resizes itself to
    // its content's natural size whenever the content changes -- switching to
    // Credits blew it from 420 to 1058 px wide (s024, Scott's screen). A
    // resizable one keeps the size it has.
    set_resizable(true);
    set_default_size(440, 480);
    set_transient_for(parent);
    set_hide_on_close(true);   // singleton: X hides, never destructs mid-session

    // The two pages, switched in the header: About | Credits.
    auto* stack = Gtk::make_managed<Gtk::Stack>();
    stack->set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    stack->set_hhomogeneous(true);   // both pages one size: nothing moves on a switch
    stack->set_vhomogeneous(true);
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    auto* tabs = Gtk::make_managed<Gtk::StackSwitcher>();
    tabs->set_stack(*stack);
    hb->set_title_widget(*tabs);
    set_titlebar(*hb);

    // ── About ────────────────────────────────────────────────────────────────
    auto* about = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.about.page", Gtk::Orientation::VERTICAL);
    about->set_spacing(8);
    about->set_margin(28);
    about->set_valign(Gtk::Align::CENTER);

    auto* logo = Gtk::make_managed<widgets::Image>(widgets::unregistered, "shell.about.logo");
    logo->set_from_icon_name("mortimer-logo-symbolic");
    logo->set_pixel_size(96);
    about->append(*logo);
    auto* name = label("<span size='xx-large' weight='bold'>Mortimer</span>");
    name->set_margin_top(4);
    about->append(*name);
    about->append(*label("Version " MORTIMER_VERSION, true));
    auto* tagline = label("<b>Design the window. Mortimer writes the GTK.</b>");
    tagline->set_margin_top(10);
    about->append(*tagline);
    about->append(*label("A GTK 4 / gtkmm 4 app designer that owns the app: you draw the windows, "
                         "Mortimer writes the C++ that builds them, and your code only says what "
                         "they do.", true));
    auto* site = Gtk::make_managed<Gtk::LinkButton>(kSite, "Website");
    site->set_halign(Gtk::Align::CENTER);
    site->set_margin_top(6);
    about->append(*site);
    stack->add(*about, "about", "About");

    // ── Credits ──────────────────────────────────────────────────────────────
    auto* credits = Gtk::make_managed<Gtk::Grid>();
    credits->set_margin(28);
    credits->set_row_spacing(6);
    credits->set_column_spacing(18);
    credits->set_halign(Gtk::Align::CENTER);
    credits->set_valign(Gtk::Align::CENTER);
    int row = 0;
    auto section = [&](const std::string& title, const std::vector<std::string>& lines) {
        auto* t = Gtk::make_managed<Gtk::Label>();
        t->set_markup("<b>" + title + "</b>");
        t->set_xalign(1);
        t->set_yalign(0);
        t->add_css_class("dim-label");
        credits->attach(*t, 0, row);
        for (const auto& l : lines) {
            auto* v = Gtk::make_managed<Gtk::Label>();
            v->set_markup(l);
            v->set_xalign(0);
            credits->attach(*v, 1, row++);
        }
        auto* gap = Gtk::make_managed<Gtk::Label>("");
        credits->attach(*gap, 0, row++);
    };
    section("Created by", {"Scott (scott8420)"});
    section("Built with", {"GTK 4 and gtkmm 4", "spdlog", "nlohmann/json", "CMake"});
    section("License", {"MIT License", "Copyright © 2026 scott8420"});
    stack->add(*credits, "credits", "Credits");

    set_child(*stack);
}

AboutWindow::~AboutWindow() {
    registry::remove(this);
}

}  // namespace mortimer
