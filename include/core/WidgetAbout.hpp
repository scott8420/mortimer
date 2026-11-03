#pragma once
// core::WidgetAbout -- what a widget type is FOR, in plain words, and where
// GTK documents it (s025; Scott: Alt+click a palette type -> "an image of the
// widget with a clear description of what it does. then there is a more... to
// go to the website page or widget page").
//
// The catalog's `doc` is one line for a tooltip. This is the card: two or
// three sentences a young programmer can act on -- what it shows, what it's
// for, the one thing to know -- written for each palette type. GTK-free; the
// selftest checks every catalog type has one, so a new widget can't ship
// without its card.
#include <string>

namespace mortimer::core {

struct WidgetAbout {
    std::string about;   // "" = none written (the selftest refuses that)
    std::string tip;     // the one thing to know -- "" if nothing stands out
};

WidgetAbout widget_about(const std::string& type);

// docs.gtk.org's page for the type's GTK class ("Button" ->
// https://docs.gtk.org/gtk4/class.Button.html). "" for an unknown type.
std::string widget_doc_url(const std::string& type);

// GTK's own picture gallery of every widget.
inline const char* kWidgetGalleryUrl = "https://docs.gtk.org/gtk4/visual_index.html";

}  // namespace mortimer::core
