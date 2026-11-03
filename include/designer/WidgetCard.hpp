#pragma once
// designer::WidgetCard -- Alt+click a palette type: what it looks like and
// what it's for (s025, Scott: "Show an image of the widget with a clear
// description of what it does. then there is a more... to go to the website
// page or widget page." Palette only.)
//
//   ┌──────────────────────────────────┐
//   │ Paned                  Gtk::Paned│
//   │ ┌──────────────────────────────┐ │
//   │ │  (a live Paned, two panes)   │ │   <- a real widget, in your theme,
//   │ └──────────────────────────────┘ │      not a screenshot; it takes no
//   │ Two panes with a divider ...     │      clicks (it's a picture)
//   │ Its window's remember keeps ...  │   <- the tip, dimmed
//   │ ▸ More…                          │
//   │     GtkPaned on docs.gtk.org     │   <- widget_doc_url
//   │     Every widget, pictured       │   <- GTK's gallery
//   └──────────────────────────────────┘
//
// The words live in core::widget_about (GTK-free, every type checked by the
// selftest); the pictures are made here by make_sample().
#include <gtkmm/box.h>
#include <gtkmm/popover.h>
#include <gtkmm/widget.h>

#include <string>

namespace mortimer::designer {

// A small, representative, LIVE instance of a palette type -- a Box with three
// buttons, a Grid of fields, a Notebook with tabs. Managed; the caller packs it.
Gtk::Widget* make_sample(const std::string& type);

class WidgetCard : public Gtk::Popover {
public:
    WidgetCard();
    // Fill for `type` and pop up pointing at `at` (in the parent's coordinates).
    void show_for(const std::string& type, const Gdk::Rectangle& at);

private:
    Gtk::Box m_box{Gtk::Orientation::VERTICAL, 8};
};

}  // namespace mortimer::designer
